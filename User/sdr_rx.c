#include "sdr_rx.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

/*
 * Raw circular buffer (stereo, L/R interleaved) - statically sized at
 * SDR_RX_BLOCK_SAMPLES_MAX (512, WFM's rate) always, so it can hold
 * either mode's block without reallocating; only
 * s_raw_half_words/s_raw_total_words (derived from the CURRENT active
 * block size, s_block_samples) worth of it is actually meaningful at
 * any given moment - see sdr_rx_reconfigure(). Twice the delivered
 * block size: first half + second half, ping-pong via the DMA
 * channel's own HTF/FTF flags (same non-blocking poll pattern used in
 * gd32_i2s_dma_start_test_tone(), consistent with the rest of the
 * project).
 */
#define RAW_HALF_WORDS_MAX  (SDR_RX_BLOCK_SAMPLES_MAX * 2U) /* 16-bit words per half (L/R interleaved) */
#define RAW_TOTAL_WORDS_MAX (RAW_HALF_WORDS_MAX * 2U)

static int16_t s_raw_buf[RAW_TOTAL_WORDS_MAX];

/*
 * Active block size + its derived word counts - set by sdr_rx_init()
 * (boots at SDR_RX_BLOCK_SAMPLES/48kHz) and sdr_rx_reconfigure()
 * (05/08/2026, added for WFM's 192kHz reactivation - see sdr_rx.h's
 * comment on SDR_RX_BLOCK_SAMPLES_MAX for why the buffer above stays
 * fixed-size while THESE track which part of it is live).
 */
static uint32_t s_block_samples  = SDR_RX_BLOCK_SAMPLES;
static uint32_t s_raw_half_words = SDR_RX_BLOCK_SAMPLES * 2U;
static uint32_t s_raw_total_words = SDR_RX_BLOCK_SAMPLES * 4U;

/* 0 = no half delivered yet; 1 = the first half was the last one
 * delivered (waiting on FTF for the second); 2 = the second half was
 * the last one delivered (waiting on HTF for the first). */
static uint8_t s_last_half_delivered;

/*
 * INTERRUPT-DRIVEN DELIVERY (30/07/2026): the HTF/FTF flags are now
 * consumed by the DMA0_Channel3 interrupt handler, not polled - the
 * main loop blocks for longer than one 2.67ms block while drawing a
 * spectrum frame, which is fine for the display (skipping blocks just
 * drops frames) but fatal for AUDIO. The ISR calls the registered
 * block hook (the AM demodulator) synchronously with a pointer to the
 * completed raw half, so the audio path is immune to whatever the
 * main loop is doing. It also leaves a "pending half" note that
 * sdr_rx_poll_block_iq() consumes for the display path (latest wins;
 * halves the display misses are simply skipped, as before).
 */
static void (*s_block_hook)(const int16_t *raw_interleaved) = 0;
static volatile uint8_t  s_pending_half = 0xFFU; /* 0xFF = none */
static volatile uint32_t s_hook_runs = 0U;       /* diagnostics */

/*
 * Shared DMA-arming step - used by BOTH sdr_rx_init() (which does the
 * one-time RXORERR bring-up around it) and sdr_rx_start() (which
 * doesn't, since that dance only makes sense once, at cold boot,
 * before SPI1's receive path has ever been serviced - see
 * sdr_rx_init()'s own comment). Uses the CURRENT s_raw_total_words -
 * caller is responsible for that already reflecting the size they
 * want (sdr_rx_reconfigure() updates it before calling this).
 */
static void sdr_rx_arm_dma(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);

    /*
     * SPI1_RX -> DMA0, Channel 3, DMA_SUBPERI0 - confirmed against
     * the DMA request mapping table (same source that resolved the TX
     * channel). Direction is PERIPH_TO_MEMORY (reading from the
     * peripheral into RAM), the opposite of the test tone's TX path.
     */
    dma_deinit(DMA0, DMA_CH3);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI1);
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr        = (uint32_t)s_raw_buf;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number              = s_raw_total_words;
    /*
     * *** 05/08/2026, REVERTED back to ULTRA_HIGH *** - the HIGH
     * experiment (see git history/transcript for that comment) was
     * tried specifically to test whether DMA0 arbitration between this
     * channel and gd32_i2s.c's TX channel (CH4) explained WFM's
     * continuous FERR. Real hardware logs proved it doesn't: with RX
     * at HIGH and TX at ULTRA_HIGH, RX (SPI1) FERR and TX (I2S1_ADD)
     * FERR came back essentially IDENTICAL, in lockstep, every single
     * check window (e.g. 626/626, 691/691, 658/658) - not what an
     * arbitration imbalance would produce (that would move one channel
     * and not the other). RX and TX share the exact same physical
     * WS/BCLK lines - both peripherals apparently see the same
     * electrical/timing event at the same moment, at a steady rate,
     * sustained for the ENTIRE WFM session (not just a brief window
     * after the rate switch). That points at a marginal signal/timing
     * budget specific to running this I2S slave setup at 192kHz's
     * 6.144MHz BCLK (4x the 1.536MHz used at 48kHz, where RX FERR is
     * reliably 0), not at DMA0 channel contention - reverted back to
     * ULTRA_HIGH since there's no upside left to keeping RX at HIGH,
     * and it was the one place in this project with an actual real
     * hardware win (RXORERR) it might still be protecting against.
     */
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH3, &dma_init_struct);

    dma_channel_subperipheral_select(DMA0, DMA_CH3, DMA_SUBPERI0);

    dma_flag_clear(DMA0, DMA_CH3, DMA_FLAG_HTF);
    dma_flag_clear(DMA0, DMA_CH3, DMA_FLAG_FTF);

    /* Half/full-transfer interrupts drive block delivery (see the
     * ISR-model comment at the top of this file). NVIC preemption
     * priority 6: below nothing critical, above the main loop. */
    dma_interrupt_enable(DMA0, DMA_CH3, DMA_CHXCTL_HTFIE | DMA_CHXCTL_FTFIE);
    nvic_irq_enable(DMA0_Channel3_IRQn, 6U, 0U);
    dma_channel_enable(DMA0, DMA_CH3);

    /* Enable the DMA receive request on SPI1 itself - without
     * this, the channel is armed but nothing ever triggers it (the
     * mirror image of the same mistake we had on the TX side). */
    spi_dma_enable(SPI1, SPI_DMA_RECEIVE);
}

/*
 * Clears a possible RXORERR (receive overrun) AND unconditionally
 * forces a hard resync by disabling/re-enabling SPI1's I2S block -
 * originally written for the boot-time gap between i2s_enable(SPI1)
 * and this function running (see the long comment this replaced in
 * sdr_rx_init() for that story) - factored out 05/08/2026 and ALSO
 * called from sdr_rx_start() now: a LIVE rate switch makes the
 * codec's BCLK/WCLK glitch through a power-down/reconfigure/power-up
 * cycle (see aic3204_set_rate_registers()'s comment in aic3204.c).
 *
 * *** 05/08/2026, made UNCONDITIONAL after further hardware testing
 * ***: this used to only do the harder disable/re-enable reset IF the
 * simple read-based clear left RXORERR still set. Real hardware logs
 * showed that's not sufficient - RXORERR (and I2S1_ADD's FERR, see
 * gd32_i2s.c's equivalent resync) could clear via the simple read,
 * report success, and then RE-APPEAR moments later once real data
 * started flowing normally, not just at the transition itself. The
 * likely reason: clearing an error FLAG doesn't reset the
 * peripheral's own internal bit/frame-position counter, which free-
 * runs from whenever I2SEN was last set - if the codec's BCLK/WS
 * genuinely glitched (extra or missing edges) during its own power-
 * down/reconfigure/power-up cycle, SPI1's internal counter can end up
 * counting from the WRONG phase relative to the new, now-stable
 * clock, and clearing the error flag alone does nothing to fix THAT
 * - it just silences the symptom until the misalignment causes
 * another error. Only a real disable/re-enable of I2SEN forces the
 * whole shifter/frame-counter back to a known state, realigned to
 * whatever the clock is doing NOW - so this always happens now,
 * without waiting to see if the cheaper option "worked" first (it can
 * look like it worked and still be wrong).
 */
/*
 * *** 05/08/2026, SIMPLIFIED for the "full reinit instead of live
 * resync" rewrite *** - this used to also do a disable/re-enable of
 * I2SEN (see git history/transcript for that version and its own long
 * reasoning). That's now redundant AND counterproductive: this
 * function is only ever called right after gd32_i2s_init_slave(rate)
 * has already done a FULL spi_i2s_deinit()/i2s_init() teardown-rebuild
 * of SPI1 (see gd32_i2s.c) - SPI1 is already freshly enabled at this
 * point, synced to a clock that's still silent (the codec hasn't been
 * told to start driving BCLK/WCLK yet - see main.c's apply_demod_
 * mode()). Adding ANOTHER disable/re-enable on top of that fresh
 * enable would just reintroduce exactly the kind of "live peripheral
 * toggle" this whole rewrite exists to get away from. All that's left
 * to do here is the RXORERR clear-by-read - a genuine CPU read of
 * SPI_DATA followed by a read of SPI_STAT, which is how this silicon
 * (like the STM32 family it closely mirrors) clears that flag - kept
 * as cheap insurance in case any stray bit arrived in the brief window
 * between i2s_enable(SPI1) and this function running.
 */
static void sdr_rx_resync_spi(void)
{
    volatile uint32_t discard;
    uint32_t stat_before, stat_after;
    (void)stat_before;

    stat_before = SPI_STAT(SPI1);
    discard = SPI_DATA(SPI1);
    (void)discard;
    stat_after = SPI_STAT(SPI1);

    debug_print_hex32("sdr_rx: SPI_STAT(SPI1) before RXORERR clear-by-read",
                       stat_before);
    debug_print_hex32("sdr_rx: SPI_STAT(SPI1) after RXORERR clear-by-read",
                       stat_after);
    if ((stat_after & SPI_STAT_RXORERR) == 0U) {
        debug_print("sdr_rx: SPI1 RXORERR clear\n");
    } else {
        debug_print("sdr_rx: *** RXORERR STILL set after clear-by-read ***\n");
    }
}

/*
 * *** 05/08/2026, added for the "full reinit instead of live resync"
 * rewrite *** - the real bring-up logic, parametrized by block size so
 * it can be shared between cold boot (sdr_rx_init(), which just calls
 * this with SDR_RX_BLOCK_SAMPLES) and every live rate switch (main.c's
 * apply_demod_mode(), which calls this directly with whichever block
 * size the new rate needs). Previously a live switch used a much
 * lighter sdr_rx_reconfigure()+sdr_rx_start() pair that only updated
 * the block-size variables and re-armed DMA, relying on
 * gd32_i2s_init_slave()'s (then only called at cold boot)
 * configuration to already be correct. Now that gd32_i2s_init_slave()
 * itself runs on every switch too, this function can just be the
 * SAME bring-up every time - no separate "lighter" path to maintain
 * or keep in sync.
 */
void sdr_rx_bringup(uint32_t block_samples)
{
    s_last_half_delivered = 0U;
    s_block_samples   = block_samples;
    s_raw_half_words  = block_samples * 2U;
    s_raw_total_words = block_samples * 4U;

    /* Drop any pending half a previous session's ISR left behind - it
     * would otherwise reference stale buffer geometry (see
     * sdr_rx_stop()'s own comment). */
    s_pending_half = 0xFFU;

    sdr_rx_resync_spi();

    sdr_rx_arm_dma();

    debug_print("sdr_rx: DMA0 CH3 (SPI1_RX) armed in circular ping-pong mode, "
                "block = ");
    debug_print_dec("sdr_rx: block_samples", s_block_samples);

    /*
     * Bring-up diagnostic (moved here 05/08/2026 so it runs on EVERY
     * bring-up, cold boot or live switch, not just cold boot) - reads
     * the remaining transfer count twice with a delay in between, and
     * checks the HTF/FTF/error flags plus SPI_STAT itself (RBNE/
     * RXORERR/FERR). On a live switch this is exactly the confirmation
     * that matters: if this whole rewrite worked, FERR should read 0
     * here just like it always has at cold boot.
     */
    {
        uint32_t cnt_a, cnt_b;
        volatile uint32_t d;

        debug_print_hex32("sdr_rx: SPI_CTL1(SPI1) after spi_dma_enable "
                           "(bit0=DMAREN should be 1)", SPI_CTL1(SPI1));
        debug_print_hex32("sdr_rx: DMA_CHCTL(DMA0,CH3) raw (bit0=CHEN should be 1)",
                           DMA_CHCTL(DMA0, DMA_CH3));

        cnt_a = dma_transfer_number_get(DMA0, DMA_CH3);
        for (d = 0; d < 2000000U; d++) { __NOP(); }
        cnt_b = dma_transfer_number_get(DMA0, DMA_CH3);

        debug_print_dec("sdr_rx: DMA_CHCNT remaining, reading A", cnt_a);
        debug_print_dec("sdr_rx: DMA_CHCNT remaining, reading B (after delay)", cnt_b);
        if (cnt_a == cnt_b) {
            debug_print("sdr_rx: *** DMA_CHCNT NOT MOVING - the RX channel is armed but "
                        "INERT, never triggers a request ***\n");
        } else {
            debug_print("sdr_rx: DMA_CHCNT IS decrementing - the RX channel is firing "
                        "requests; if I/Q data is still stuck, the issue is upstream "
                        "(SPI1 not actually receiving real bits, or the AIC3204 ADC "
                        "not producing real samples)\n");
        }
        if (dma_flag_get(DMA0, DMA_CH3, DMA_FLAG_TAE) == SET) {
            debug_print("sdr_rx: *** DMA flag TAE (transfer access error) set ***\n");
        }
        if (dma_flag_get(DMA0, DMA_CH3, DMA_FLAG_FEE) == SET) {
            debug_print("sdr_rx: *** DMA flag FEE (FIFO error/exception) set ***\n");
        }

        {
            uint32_t stat = SPI_STAT(SPI1);
            debug_print_hex32("sdr_rx: SPI_STAT(SPI1) raw (bit0=RBNE, bit6=RXORERR, "
                               "bit8=FERR)", stat);
            if ((stat & SPI_STAT_FERR) != 0U) {
                debug_print("sdr_rx: *** SPI_STAT_FERR (format error) SET ***\n");
            }
            if ((stat & SPI_STAT_RXORERR) != 0U) {
                debug_print("sdr_rx: *** SPI_STAT_RXORERR (receive overrun) SET ***\n");
            }
            if ((stat & SPI_STAT_RBNE) != 0U) {
                debug_print("sdr_rx: SPI_STAT_RBNE is set right now - a received word is "
                            "sitting in the data register waiting to be read\n");
            }
        }
    }
}

void sdr_rx_init(void)
{
    sdr_rx_bringup(SDR_RX_BLOCK_SAMPLES);
}

void sdr_rx_stop(void)
{
    /* Same disable-then-wait-for-CHEN-to-drop pattern gd32_i2s.c
     * already uses for its own DMA channels - a plain
     * dma_channel_disable() request can take a few cycles to actually
     * complete, so blindly reconfiguring right after it would race
     * the hardware. spi_dma_enable() is NOT undone here (SPI1's DMA
     * receive request stays enabled) - re-arming the channel via
     * sdr_rx_start()/sdr_rx_arm_dma() is what matters; leaving the
     * SPI-side enable bit alone is harmless and one less thing to get
     * back in sync. */
    dma_channel_disable(DMA0, DMA_CH3);
    while ((DMA_CHCTL(DMA0, DMA_CH3) & DMA_CHXCTL_CHEN) != 0U) {
        /* a disable request can take a few cycles to complete */
    }
    /* Drop any pending half the ISR left behind - it references the
     * OLD buffer geometry and would otherwise get de-interleaved with
     * the wrong (stale or about-to-change) block size by
     * sdr_rx_poll_block_iq() on the very next poll. */
    s_pending_half = 0xFFU;
}

/*
 * *** 05/08/2026, REMOVED sdr_rx_reconfigure()/sdr_rx_start() -
 * superseded by sdr_rx_bringup() above ***, part of the "full reinit
 * instead of live resync" rewrite - see sdr_rx_bringup()'s own
 * comment.
 */

uint32_t sdr_rx_get_block_samples(void)
{
    return s_block_samples;
}

void sdr_rx_set_block_hook(void (*hook)(const int16_t *raw_interleaved))
{
    s_block_hook = hook;
}

/*
 * DMA0 Channel 3 (SPI1_RX) interrupt: fires on half-transfer (first
 * half of the raw buffer complete) and full-transfer (second half
 * complete). While this handler processes half N, the DMA is filling
 * the OTHER half, so the data is stable for the duration.
 *
 * Priority note: enabled at NVIC preemption priority 6 (see
 * sdr_rx_init()) - the demod hook costs on the order of 100us per
 * block, so even where it delays the 1kHz SysTick encoder sampling,
 * the quadrature transition-table decoder tolerates that jitter by
 * construction.
 */
/*
 * *** 05/08/2026, added for WFM FERR frequency diagnostics *** - the
 * periodic sdr_tick check in main.c only samples SPI_STAT once every
 * ~30 waterfall ticks (roughly 1.5s), so "FERR was set" tells us
 * nothing about the REAL rate: once per check would sound like
 * occasional clicks, hundreds of times per second would sound like
 * continuous noise - which is what's actually reported, but we've had
 * no way to tell them apart. This counts every real occurrence, right
 * here in the ISR that already runs every RX block (~2.667ms either
 * rate), with a read-based clear (STAT then DATA, same sequence
 * sdr_rx_resync_spi() already uses) so each block sees a fresh flag,
 * not one still latched from a prior block. No debug_print anywhere
 * near this - cheap on purpose, same reasoning as demod_am.c's own
 * ISR timing instrumentation.
 */
static volatile uint32_t s_rx_ferr_count = 0U;

uint32_t sdr_rx_get_ferr_count(void)
{
    return s_rx_ferr_count;
}

void sdr_rx_reset_ferr_count(void)
{
    s_rx_ferr_count = 0U;
}

static volatile uint8_t s_last_block_had_ferr = 0U;

uint8_t sdr_rx_last_block_corrupted(void)
{
    return s_last_block_had_ferr;
}

/*
 * *** 05/08/2026, added to test the "channel/sample misalignment, not
 * missing data" theory *** - SPI_STAT_FERR's own datasheet meaning is
 * a frame-sync (WS-vs-internal-counter) mismatch, not a data-underrun/
 * overrun flag. That would explain something otherwise odd: the
 * panadapter spectrum (computed from these same raw I/Q samples, via
 * sdr_rx_poll_block_iq() below) has consistently looked fine even
 * during sessions with hundreds of FERR/window, while the demodulated
 * AUDIO is badly corrupted. A power spectrum barely reacts to a
 * handful of misassigned I/Q samples per block; a phase-sensitive FM
 * discriminator reacts violently to exactly that. This snapshot grabs
 * the raw interleaved I/Q words from the SAME block a FERR just fired
 * on, cheaply (a plain memcpy, no math) right here in the ISR, so
 * main.c can run a cross-correlation between the I and Q streams at a
 * few small sample shifts in the (slow, UART-affordable) main loop -
 * if the true peak correlation sits away from zero shift, that's
 * direct evidence of a systematic I/Q misalignment, not just noise or
 * missing samples. Only captures ONE snapshot at a time (a "pending"
 * flag guards it) - deliberately not on every single FERR, to avoid
 * this diagnostic itself adding ISR overhead.
 */
#define SDR_RX_FERR_SNAPSHOT_FRAMES 64U

/*
 * Per-block wild-jump corruption check constants (05/08/2026) - same
 * jump threshold as main.c's RX_LOCK_JUMP_THRESHOLD (~half full-scale
 * int16, "genuinely garbled" not "subtly off"), but a looser bad-
 * sample fraction (1/20 = 5%, vs rx_lock's stricter one at lock time)
 * since this runs on every single block during normal playback, not
 * just during the one-shot lock-verification step - see this check's
 * own call site in DMA0_Channel3_IRQHandler() for the reasoning.
 */
#define SDR_RX_BLOCK_JUMP_THRESHOLD 16000
#define SDR_RX_BLOCK_BAD_FRACTION_NUM 1U
#define SDR_RX_BLOCK_BAD_FRACTION_DEN 20U
static int16_t s_ferr_snapshot[SDR_RX_FERR_SNAPSHOT_FRAMES * 2U];
static volatile uint8_t s_ferr_snapshot_ready = 0U;

uint8_t sdr_rx_get_ferr_snapshot(int16_t *i_out, int16_t *q_out, uint32_t n)
{
    uint32_t k;

    if (s_ferr_snapshot_ready == 0U) {
        return 0U;
    }
    if (n > SDR_RX_FERR_SNAPSHOT_FRAMES) {
        n = SDR_RX_FERR_SNAPSHOT_FRAMES;
    }
    for (k = 0; k < n; k++) {
        i_out[k] = s_ferr_snapshot[2U * k];
        q_out[k] = s_ferr_snapshot[2U * k + 1U];
    }
    s_ferr_snapshot_ready = 0U; /* consumed - next FERR can capture again */
    return 1U;
}

void DMA0_Channel3_IRQHandler(void)
{
    const int16_t *half = 0;
    uint8_t ferr_this_block = 0U;

    if (dma_interrupt_flag_get(DMA0, DMA_CH3, DMA_INT_FLAG_HTF) == SET) {
        dma_interrupt_flag_clear(DMA0, DMA_CH3, DMA_INT_FLAG_HTF);
        half = &s_raw_buf[0];
        s_pending_half = 0U;
    }
    if (dma_interrupt_flag_get(DMA0, DMA_CH3, DMA_INT_FLAG_FTF) == SET) {
        dma_interrupt_flag_clear(DMA0, DMA_CH3, DMA_INT_FLAG_FTF);
        half = &s_raw_buf[s_raw_half_words];
        s_pending_half = 1U;
    }

    {
        /*
         * *** 05/08/2026, FIXED after a severe regression *** - this
         * used to also do `discard = SPI_DATA(SPI1)` here, copied from
         * sdr_rx_resync_spi()'s clear sequence. That's safe there
         * because DMA isn't running yet at that point - but THIS ISR
         * runs with DMA0/CH3 actively, continuously consuming SPI_DATA
         * via its own peripheral request (DMAREN). A manual CPU read of
         * the same register here can race the DMA controller for the
         * same word, and every FERR occurrence (frequent) was another
         * chance to do it - real hardware logs showed this compounding
         * into full-scale-saturated raw I/Q and near-100% FERR, far
         * worse than before this read existed. Just reading SPI_STAT
         * (no DATA touch) is enough to count/flag FERR without
         * touching the data path DMA owns.
         */
        uint32_t stat = SPI_STAT(SPI1);
        if ((stat & SPI_STAT_FERR) != 0U) {
            s_rx_ferr_count++;
            ferr_this_block = 1U;
        }
    }

    if ((half != 0) && (ferr_this_block != 0U) && (s_ferr_snapshot_ready == 0U)) {
        uint32_t k;
        uint32_t n = SDR_RX_FERR_SNAPSHOT_FRAMES;
        if (n > (s_raw_half_words / 2U)) {
            n = s_raw_half_words / 2U;
        }
        for (k = 0; k < (2U * n); k++) {
            s_ferr_snapshot[k] = half[k];
        }
        s_ferr_snapshot_ready = 1U;
    }

    /*
     * *** 05/08/2026, REPLACED after a second real regression *** -
     * this used to skip a block purely because SPI_STAT_FERR was set
     * (ferr_this_block above). Real hardware logs then showed FERR
     * firing on ~70% of blocks in some WFM sessions (highly variable
     * run to run, 0 to 400+/window, likely genuine hardware timing
     * margin - see aic3204_rate_switch_reset()'s comment for that
     * whole investigation) while the DC-removed I/Q correlation
     * diagnostic kept coming back "inconclusive" or shift=0 for most
     * of those same FERR-flagged blocks - meaning FERR fires far more
     * often than the actual sample data looks bad. Skipping on FERR
     * alone, at a 70% rate, meant the TX buffer almost never got
     * refreshed - reported as complete silence in WFM, which is worse
     * than the noise this was meant to fix. Now does the SAME kind of
     * per-sample wild-jump check rx_capture_looks_corrupted() already
     * uses at lock time (see main.c, RX_LOCK_JUMP_THRESHOLD's comment)
     * but on THIS block, every block, cheaply (plain integer deltas,
     * a few thousand cycles worst case at 512 samples - trivial next
     * to WFM's ~190k/532k cycle budget) - only skips a block that
     * genuinely LOOKS corrupted, regardless of whether FERR happened
     * to be set for it, which should skip far less often and preserve
     * far more real audio.
     */
    {
        uint8_t corrupted = 0U;
        if (half != 0) {
            uint32_t n_samples = s_raw_half_words / 2U; /* stereo frames in this half */
            if (n_samples > 1U) {
                uint32_t k;
                uint32_t bad = 0U;
                int32_t prev_i = half[0];
                int32_t prev_q = half[1];
                for (k = 1U; k < n_samples; k++) {
                    int32_t i_v = half[2U * k];
                    int32_t q_v = half[2U * k + 1U];
                    int32_t di = i_v - prev_i;
                    int32_t dq = q_v - prev_q;
                    if (di < 0) { di = -di; }
                    if (dq < 0) { dq = -dq; }
                    if ((di > SDR_RX_BLOCK_JUMP_THRESHOLD) || (dq > SDR_RX_BLOCK_JUMP_THRESHOLD)) {
                        bad++;
                    }
                    prev_i = i_v;
                    prev_q = q_v;
                }
                if (bad * SDR_RX_BLOCK_BAD_FRACTION_DEN > n_samples * SDR_RX_BLOCK_BAD_FRACTION_NUM) {
                    corrupted = 1U;
                }
            }
        }
        s_last_block_had_ferr = corrupted;
    }

    if ((half != 0) && (s_block_hook != 0)) {
        s_block_hook(half);
        s_hook_runs++;
    }
}

uint32_t sdr_rx_poll_block_iq(int16_t *i_out, int16_t *q_out)
{
    uint32_t n;
    uint8_t half;
    uint32_t base;
    uint32_t primask;

    /* Take (and clear) the ISR's pending-half note atomically. */
    primask = __get_PRIMASK();
    __disable_irq();
    half = s_pending_half;
    s_pending_half = 0xFFU;
    __set_PRIMASK(primask);

    if (half == 0xFFU) {
        return 0U;
    }

    /* NOTE: while we de-interleave half N here, the DMA is writing
     * the other half - unless the main loop was so late that the ISR
     * already flipped again, in which case a torn block is possible.
     * That was equally true of the old flag-polling version, and for
     * a DISPLAY consumer a rare torn spectrum frame is acceptable;
     * the audio path (the hook, in ISR context) is never affected. */
    base = (half == 0U) ? 0U : s_raw_half_words;
    for (n = 0; n < s_block_samples; n++) {
        /* de-interleave: even=left ADC channel, odd=right ADC channel */
        i_out[n] = s_raw_buf[base + 2U * n];
        q_out[n] = s_raw_buf[base + 2U * n + 1U];
    }
    (void)s_last_half_delivered; /* superseded by the ISR model */
    return 1U;
}

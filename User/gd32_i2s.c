#include "gd32_i2s.h"
#include "gd32f4xx.h"
#include "debug_uart.h"
#include "sdr_rx.h" /* SDR_RX_BLOCK_SAMPLES - see STREAM_FRAMES_PER_HALF's
                      * comment below for why this MUST be the single
                      * source of truth here, not a hand-copied number. */
#include "aic3204.h" /* only for the shared AIC3204_TEST_LOOPBACK diagnostic
                      * flag - temporary coupling, see the note below where
                      * it's used. */

static void tone_buf_fill_1khz(void);
static float sinf_approx(float x);

/*
 * Diagnostic utility: toggles the five I2S pins as plain GPIO output
 * (peripheral untouched). Useful to isolate a "no signal" symptom
 * between a peripheral/clock configuration issue and a physical
 * wiring issue - if even a plain GPIO toggle doesn't show up on a
 * scope, the problem is upstream of the I2S peripheral entirely.
 */
void gd32_i2s_pins_gpio_toggle_test(uint32_t cycles)
{
    uint32_t i;

    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);

    {
        uint32_t pinsB = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, pinsB);
        gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pinsB);
        gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_6);
        gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
    }

    debug_print("gd32_i2s: GPIO toggle test on PB12/13/14/15 and PC6 "
                "(no I2S) - probe now\n");

    for (i = 0; i < cycles; i++) {
        gpio_bit_set(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
        gpio_bit_set(GPIOC, GPIO_PIN_6);
        {
            volatile uint32_t d;
            for (d = 0; d < 500000U; d++) {
                __NOP();
            }
        }
        gpio_bit_reset(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
        gpio_bit_reset(GPIOC, GPIO_PIN_6);
        {
            volatile uint32_t d;
            for (d = 0; d < 500000U; d++) {
                __NOP();
            }
        }
    }

    debug_print("gd32_i2s: GPIO toggle test done\n");
}

void gd32_i2s_init_slave_192k(void)
{
    gd32_i2s_init_slave(AIC3204_RATE_96K);
}

/*
 * *** 05/08/2026, generalized from gd32_i2s_init_slave_192k() for the
 * "full reinit instead of live resync" rewrite ***
 *
 * HISTORY: every earlier attempt at a LIVE rate switch tried to keep
 * SPI1/I2S1_ADD's existing configuration and just nudge them back into
 * sync - a plain disable/re-enable of I2SEN (sdr_rx_resync_spi()'s and
 * gd32_i2s_stream_start()'s old approach), or reordering that against
 * the codec's own reset/reconfigure. Real hardware logs kept finding
 * the SAME result no matter which of those was tried: FERR starts
 * firing after a live switch and NEVER clears again, even switching
 * back to a rate that was clean moments earlier - while running a
 * given rate from a genuine COLD BOOT (this same function, called
 * once from main.c before the main loop starts) is rock solid, FERR
 * always 0, indefinitely.
 *
 * That gap - cold boot always clean, live switch never fully clean -
 * means something LIVE switching left touched (PLLI2S's own state,
 * SPI1/I2S1_ADD's internal shift/frame counters, or some combination)
 * that a bare I2SEN toggle doesn't reset, even though every DIRECTLY
 * READABLE register looks identical either way. Rather than keep
 * chasing which specific bit that is, this function is now called on
 * EVERY rate change (cold boot AND live), doing the FULL teardown/
 * rebuild cold boot always did: spi_i2s_deinit(SPI1) (which also
 * clears I2S1_ADD's extension state, since it's the same peripheral
 * instance), a full PLLI2S off/reconfigure/on/wait-for-lock, and a
 * full i2s_init()/i2s_full_duplex_mode_config() + GPIO AF replay - not
 * just re-enabling what was already configured. Slower (a few hundred
 * microseconds of extra I2C-scale bring-up, irrelevant next to the
 * WFM_SETTLE_MUTE_BLOCKS-sized silence a mode switch already has), but
 * this is the ONLY sequence that's ever measured clean on real
 * hardware - see main.c's apply_demod_mode() for where this now sits
 * in the full switch sequence.
 */
void gd32_i2s_init_slave(aic3204_rate_t rate)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_SPI1);

    /*
     * Clean reset of SPI1/I2S1/I2S1_ADD in case any state was
     * inherited from the bootloader or a previous configuration -
     * this board is chained from a bootloader rather than power-cycled
     * on every flash, so residual peripheral state is possible. On a
     * LIVE switch (not just cold boot), this is also what tears down
     * whatever residual internal state the previous rate's I2SEN left
     * behind - see this function's own header comment.
     */
    spi_i2s_deinit(SPI1);

    /*
     * PLLI2S must be configured explicitly (not left at its reset/
     * inherited value) to get a known, reproducible i2sclock.
     *
     * IMPORTANT: the PSC field used here (via rcu_plli2s_config's M
     * divider) is the SAME field as the main system PLL's PSC in
     * system_gd32f4xx.c. If that PSC value ever changes, N/R here must
     * be recalculated together with it to keep the same i2sclock -
     * they are not independent.
     *
     * Current values (PSC=8 in system_gd32f4xx.c, N=400/R=4 here):
     * i2sclock = (12.288MHz/8) * 400/4 = 49.152MHz exactly. This does
     * NOT depend on `rate` - i2sclock feeds I2S_AUDIOSAMPLE_48K/192K's
     * OWN internal divider math below, so the same PLLI2S setting
     * covers both rates.
     */
    rcu_osci_off(RCU_PLLI2S_CK);
    if (rcu_plli2s_config(400U, 4U) != SUCCESS) {
        debug_print("gd32_i2s: rcu_plli2s_config(400,4) FAILED\n");
    }
    rcu_osci_on(RCU_PLLI2S_CK);
    if (rcu_osci_stab_wait(RCU_PLLI2S_CK) != SUCCESS) {
        debug_print("gd32_i2s: *** PLLI2S DID NOT LOCK (stab_wait ERROR) - no i2sclock ***\n");
    }

    /*
     * BLOCK ROLES SWAPPED (28/07/2026): a real I2C capture combined
     * with disassembly of the original firmware's own I2S setup code
     * shows the struct tied to the SPI1_BASE literal being initialized
     * with Mode=0x100 - which matches I2S_MODE_SLAVERX exactly (this
     * project's own I2SCTL_I2SOPMOD(1) encoding). That means the
     * ORIGINAL firmware configures the MAIN SPI1/I2S1 block as the
     * RECEIVE side (ADC capture, on PB15/SD) and the I2S1_ADD
     * extension as the TRANSMIT side (DAC feed, on PB14/SDext) - the
     * OPPOSITE of what this driver assumed for a very long time (main
     * block = TX, extension = RX). The GD32F4xx User Manual's DMA0
     * request mapping table confirms both halves of this swap exist
     * as distinct, real options on the exact same DMA channels we
     * were already using (DMA0/CH3: SPI1_RX at SUBPERI0 vs
     * I2S1_ADD_RX at SUBPERI3; DMA0/CH4: I2S1_ADD_TX at SUBPERI2 vs
     * SPI1_TX at SUBPERI0) - meaning this whole time, ADC samples were
     * very plausibly being captured from the wrong subperipheral of
     * the right DMA channel, which would explain why no amount of
     * clock, DMA priority, or master/slave fixing on the OTHER half
     * ever changed anything: the real ADC data path was never the one
     * being read from at all.
     *
     * SLAVE mode (still applies, unrelated to this swap): a real I2C
     * capture of the original firmware's AIC3204 configuration shows
     * register 0x1B=0x0C, which (confirmed against a real TI forum
     * example using this exact byte, plus TI's own SLAA404C app note
     * describing bits D3:D2 of this register as the BCLK/WCLK
     * direction control) sets BOTH BCLK and WCLK as OUTPUTS on the
     * codec - i.e. the codec is the I2S master in this design. The
     * GD32 must be the SLAVE to match (confirmed clean, single-source
     * BCLK/WCLK on a real oscilloscope once this was corrected).
     */
    i2s_init(SPI1, I2S_MODE_SLAVERX, I2S_STD_PHILLIPS, I2S_CKPL_LOW);

    /*
     * MCLK: TIMER2_CH0/PC6, started once at cold boot by
     * gd32_i2s_mclk_timer_start() (main.c) and never touched again -
     * fixed 1.536MHz regardless of `rate`. This is genuinely required
     * (confirmed both by a real-hardware experiment and by re-decoding
     * the actual I2C capture - see aic3204.c's header comment,
     * corrected 01/09/2026): the codec's PLL takes MCLK as its
     * reference (PLL_CLKIN=MCLK, NOT BCLK - the earlier version of
     * this comment had that backwards) and always produces the same
     * fixed CODEC_CLKIN=86.016MHz from it, regardless of `rate` - it's
     * the NDAC/MDAC/DOSR dividers downstream of CODEC_CLKIN (which DO
     * change with `rate`) that actually produce the different Fs, not
     * MCLK/PLL_CLKIN itself.
     *
     * i2s_psc_config's audiosample argument IS now rate-dependent -
     * this is the one piece of this function that actually differs
     * between the two calls (48kHz cold boot vs either rate on a live
     * switch). Whether SPI_I2SPSC does anything in SLAVE mode is still
     * genuinely unclear from the source alone (see the historical note
     * this replaced - real 192kHz captures worked fine even with a
     * stale 48kHz value here) - passed correctly anyway, on the "make
     * the code say what's actually true" principle.
     */
    i2s_psc_config(SPI1,
                    (rate == AIC3204_RATE_192K) ? I2S_AUDIOSAMPLE_192K : I2S_AUDIOSAMPLE_48K,
                    I2S_FRAMEFORMAT_DT16B_CH16B, I2S_MCKOUT_DISABLE);

    /* I2S1_ADD extension block: passed the SAME mode as the main block
     * (I2S_MODE_SLAVERX) - the library internally derives the OPPOSITE
     * role for the extension block, I2S_MODE_SLAVETX in this case
     * (shares the clock with the master - now the CODEC, not the GD32
     * - transmit-only). This is now the path that feeds the AIC3204's
     * DAC (silence/test tone), captured... err, driven, by DMA in
     * gd32_i2s_dma_start_silence()/gd32_i2s_dma_start_test_tone()
     * below, via PB14 (SDext). The MAIN block above is now the one
     * capturing ADC samples on PB15 (SD), via DMA in sdr_rx.c. */
    i2s_full_duplex_mode_config(I2S1_ADD, I2S_MODE_SLAVERX, I2S_STD_PHILLIPS,
                                 I2S_CKPL_LOW, I2S_FRAMEFORMAT_DT16B_CH16B);

    /*
     * GPIO/AF configuration must happen BEFORE i2s_enable(). If the
     * pins are still plain GPIO (e.g. left over from
     * gd32_i2s_pins_gpio_toggle_test) when the peripheral starts, the
     * internal clock generator can run with no signal reaching the
     * physical pins at all. Re-doing this on every call (not just cold
     * boot) is cheap and removes any doubt about GPIO state surviving
     * a live switch correctly.
     *
     * WS, CK, SD(TX): AF5 on PB12/13/14/15 (SPI1/I2S1 block). PC6
     * (MCLK) is intentionally NOT configured - see the MCKOUT note
     * above.
     */
    {
        uint32_t pins = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
        gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, pins);
        gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pins);
        gpio_af_set(GPIOB, GPIO_AF_5, pins);
    }
    {
        /*
         * AF6 for I2S1_ADD_SD (RX data in), confirmed against the
         * GD32F450xx datasheet's Port B alternate function table:
         * PB14 lists SPI1_MISO on AF5 and I2S1_ADD_SD on AF6.
         */
        gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_14);
        gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_14);
        gpio_af_set(GPIOB, GPIO_AF_6, GPIO_PIN_14);
    }

    i2s_enable(SPI1);
    i2s_enable(I2S1_ADD);

    debug_print_hex32("gd32_i2s: SPI_I2SCTL after i2s_enable (SPI1, bit10=I2SEN should be 1)",
                       SPI_I2SCTL(SPI1));
    debug_print_hex32("gd32_i2s: RCU_CFG0 raw (I2SSEL toggle bit is bit 23)", RCU_CFG0);

    debug_print_dec("gd32_i2s: full slave reinit done for rate (0=96K,1=192K,2=48K)",
                     (uint32_t)rate);

#if AIC3204_TEST_LOOPBACK
    /* Loopback test needs a real, recognizable TX pattern - silence
     * looped back is still silence, not a useful test. */
    gd32_i2s_dma_start_test_tone();
#else
    gd32_i2s_dma_start_silence();
#endif
}

/*
 * 1kHz sine tone table, stereo (L=R), 16-bit, at the real confirmed
 * frame rate (192kHz). 192000/1000 = 192 samples per cycle exactly, so
 * the table is short and there is no frequency rounding error.
 * Amplitude is moderate (not full-scale) to be safe on a speaker or
 * headphones during bring-up testing.
 */
#define TONE_SAMPLES_PER_CYCLE   192U
static int16_t s_tone_buf[TONE_SAMPLES_PER_CYCLE * 2U]; /* *2: interleaved L/R */

static void tone_buf_fill_1khz(void)
{
    uint32_t n;
    for (n = 0; n < TONE_SAMPLES_PER_CYCLE; n++) {
        /* Amplitude ~+-8000 (25% of 16-bit full scale). Computed once
         * at init time (not in a hot path), so a compile-time LUT
         * isn't needed - a runtime float fill is fine here. */
        float angle = (2.0f * 3.14159265358979f * (float)n) / (float)TONE_SAMPLES_PER_CYCLE;
        int16_t sample = (int16_t)(8000.0f * sinf_approx(angle));
        s_tone_buf[2U * n]      = sample; /* left */
        s_tone_buf[2U * n + 1U] = sample; /* right (same tone on both) */
    }
}

/* Sine approximation with no libm dependency (this project links with
 * nano.specs and no -lm): Bhaskara I approximation, typical error
 * <0.2% across the full range - more than enough for an audible test
 * tone. */
static float sinf_approx(float x)
{
    float pi = 3.14159265358979f;
    float x2;
    int negate = 0;
    while (x > pi) { x -= 2.0f * pi; }
    while (x < -pi) { x += 2.0f * pi; }
    if (x < 0.0f) { x = -x; negate = 1; }
    x2 = (16.0f * x * (pi - x)) / (5.0f * pi * pi - 4.0f * x * (pi - x));
    return negate ? -x2 : x2;
}

/*
 * IMPORTANT: this MCU's I2S master clock generator only produces
 * continuous BCLK/WS while TX data keeps flowing - it is NOT
 * free-running (see the manual feed loop in
 * gd32_i2s_init_slave_192k()). Since I2S1_ADD (RX) is wired as a
 * SLAVE sharing the SAME BCLK/WS lines as the TX master block, RX
 * capture silently stops the moment BCLK/WS stop - even though RX
 * itself has nothing to do with the TX audio content. A continuous TX
 * feed is therefore required to keep the whole bus alive, not just to
 * produce audio output. Feeding silence (all zeros) instead of the
 * audible test tone keeps BCLK/WS running without an audible tone.
 */
static const int16_t s_silence_buf[16] = {0}; /* small, doesn't need to match any tone period */

/*
 * MCLK generation via TIMER7_CH0 on PC6 - moved here 01/09/2026 from
 * TIMER2_CH0 (same pin, different timer) specifically to free TIMER2
 * for lo_gen_gd32.c's PA6/PA7 quadrature LO generator, which needs
 * TIMER2_CH0/CH1 and has no other option on those two exact pins -
 * see lo_gen_gd32.h's own comment for why, and main.c's boot sequence
 * comment for the two failed workarounds tried first (disabling MCLK
 * entirely broke reception - see aic3204.c's corrected clock-chain
 * comment for why MCLK is genuinely required, not vestigial). The
 * project owner's own real datasheet pinout table confirmed PC6 also
 * has TIMER7_CH0 as an alternate function, alongside the TIMER2_CH0
 * this project already used - a straight swap, not a redesign.
 *
 * *** AF NUMBER CONFIRMED 01/09/2026 *** - the project owner supplied
 * the real GD32F450xx datasheet's Table 2-8 (Port C alternate
 * functions summary): PC6's row shows TIMER2_CH0 under AF2 and
 * TIMER7_CH0 under AF3, exactly matching this family's usual
 * convention (TIMER0/1 typically AF1, TIMER2-5 AF2, TIMER7-11 AF3,
 * TIMER12-14 AF9) that GPIO_AF_3 below was originally chosen from
 * before this table was available. No longer a guess.
 *
 * Targets exactly 1.536MHz, matching the real board's confirmed MCLK
 * frequency. TIMER7 is on APB2 (an advanced-control timer, same bus
 * as TIMER0 - both share BRKIN/CH0_ON alternate functions on PA6/PA7,
 * a strong family-architecture tell even without a direct clock-tree
 * confirmation table). With this project's clock config (APB2_PSC=2
 * in system_gd32f4xx.c -> PCLK2=SYSCLK/2=99.84MHz -> TIMER7CLK=
 * 2xPCLK2=199.68MHz when the APB2 prescaler != 1, standard on this
 * family): 199.68MHz / 130 = 1.536MHz exactly. PSC=0, period=129 (130
 * total counts) gives that division with no remainder - AND, unlike
 * the old TIMER2-based 65-count division, 130 is even, so this also
 * gets an exact 50% duty cycle (65/65) as a free bonus, not just an
 * approximation.
 */
void gd32_i2s_mclk_timer_start(void)
{
    timer_parameter_struct timer_init_struct;
    timer_oc_parameter_struct oc_init_struct;

    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_TIMER7);

    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_6);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
    gpio_af_set(GPIOC, GPIO_AF_3, GPIO_PIN_6);

    timer_deinit(TIMER7);

    timer_struct_para_init(&timer_init_struct);
    timer_init_struct.prescaler         = 0U;
    timer_init_struct.alignedmode       = TIMER_COUNTER_EDGE;
    timer_init_struct.counterdirection  = TIMER_COUNTER_UP;
    timer_init_struct.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init_struct.period            = 129U; /* 130 counts: 199.68MHz/130 = 1.536MHz exact */
    timer_init_struct.repetitioncounter = 0U;
    gd32_timer_init(TIMER7, &timer_init_struct);

    timer_channel_output_struct_para_init(&oc_init_struct);
    oc_init_struct.outputstate  = TIMER_CCX_ENABLE;
    oc_init_struct.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_channel_output_config(TIMER7, TIMER_CH_0, &oc_init_struct);

    timer_channel_output_pulse_value_config(TIMER7, TIMER_CH_0, 65U); /* exact 50% duty */
    timer_channel_output_mode_config(TIMER7, TIMER_CH_0, TIMER_OC_MODE_PWM0);

    /* TIMER7 is an advanced-control timer (like TIMER0) - its outputs
     * stay disabled at the peripheral level until the main output
     * enable is set, unlike the plain general-purpose TIMER2 this
     * function used before. Without this, PC6 would stay silent
     * despite everything above looking correctly configured. */
    timer_primary_output_config(TIMER7, ENABLE);

    timer_auto_reload_shadow_enable(TIMER7);
    timer_enable(TIMER7);

    debug_print("gd32_i2s: MCLK started via TIMER7_CH0/PC6 (AF3, confirmed against the real "
                "datasheet's Port C AF table), target 1.536MHz exact\n");
}


void gd32_i2s_dma_start_silence(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);

    /* DMA0, Channel 4, PERIEN=010 (SUBPERI2) for I2S1_ADD_TX - now the
     * TX side after the block-role swap (see gd32_i2s_init_slave_192k)
     * - confirmed against the GD32F4xx User Manual's DMA request
     * mapping table (Table 10-2, "Peripheral requests to DMA0"). Same
     * channel as gd32_i2s_dma_start_test_tone(), just a silent source
     * buffer. */
    dma_deinit(DMA0, DMA_CH4);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(I2S1_ADD);
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr        = (uint32_t)s_silence_buf;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.number              = 16U;
    /*
     * *** 05/08/2026 fix ***: was DMA_PRIORITY_HIGH, one level below
     * sdr_rx.c's RX channel (DMA0/CH3, DMA_PRIORITY_ULTRA_HIGH - see
     * its own comment for why RX was bumped there). Both channels
     * share DMA0, so on any request collision RX always won and TX
     * (this channel, feeding I2S1_ADD/the DAC) went unserved that
     * cycle. Invisible at 48kHz - request rates low enough that
     * simultaneous collisions were rare - but at WFM's 192kHz (4x the
     * request rate on BOTH channels), collisions became frequent
     * enough to starve I2S1_ADD's data register right before a BCLK
     * edge, which is exactly what SPI_STAT_FERR flags: real hardware
     * logs showed it set on essentially every sdr_tick check for the
     * entire duration of a WFM session, and never once at 48kHz -
     * matching the project owner's "ruido de fondo" report (a starved
     * TX register underneath otherwise-correct audio, not a gain/
     * volume issue - the RX side, which always won the arbitration,
     * stayed clean the whole time, which is also why the spectrum
     * display and rx_lock's corruption check never showed anything
     * wrong). Bumped to match RX's ULTRA_HIGH rather than lowering RX
     * - RX was raised in the first place to fight a real, persistent
     * RXORERR (see sdr_rx.c's own comment), and undoing that risks
     * bringing that back. Matching priorities won't guarantee TX wins
     * a tie, but stops it from strictly losing every single time
     * (unclear from the docs alone how the underlying arbiter breaks a
     * same-priority tie between CH3/CH4, so validate this reduces or
     * clears the FERR reports on real hardware, don't assume from the
     * datasheet). Applied identically here (silence keeper) and in
     * gd32_i2s_dma_start_test_tone() and stream_arm_dma() below, since
     * whichever last armed DMA0/CH4 needs the same priority - not
     * something that should vary by which TX source is active.
     */
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH4, &dma_init_struct);

    dma_channel_subperipheral_select(DMA0, DMA_CH4, DMA_SUBPERI2);

    dma_circulation_enable(DMA0, DMA_CH4);
    dma_channel_enable(DMA0, DMA_CH4);

    spi_dma_enable(I2S1_ADD, SPI_DMA_TRANSMIT);
}

/*
 * *** BUG FOUND AND FIXED 04/08/2026 - THIS is almost certainly what
 * the project owner heard as a high-pitched hiss, a "repeating
 * pattern", and general slowdown right after the 48kHz migration ***
 *
 * This was previously a HAND-COPIED literal `512U`, with a comment
 * literally saying "MUST equal SDR_RX_BLOCK_SAMPLES" - a manually
 * maintained invariant, not an enforced one. When SDR_RX_BLOCK_SAMPLES
 * dropped from 512 to 128 (see sdr_rx.h's own comment) for the 48kHz
 * move, THIS constant was never touched, because nothing forced it to
 * be. The result: gd32_i2s_stream_write_half() below copies
 * STREAM_WORDS_PER_HALF (1024 int16 words, derived from the STALE
 * 512) from whatever buffer the caller passes it - but demod_am.c's
 * s_audio_out[] is only SDR_RX_BLOCK_SAMPLES*2 = 256 words long now.
 * Every single call read 768 words PAST THE END of s_audio_out[] -
 * whatever static memory happens to sit after it in the linker layout
 * - and played that garbage out the DAC alongside the real 256 words
 * of audio. That's a textbook explanation for all three symptoms:
 * hiss/noise (reading unrelated memory as PCM), a "repeating pattern"
 * (that adjacent memory's own contents are fairly static from block
 * to block), and it plausibly contributing to the reported slowdown
 * too (see gd32_i2s_stream_write_half()'s DMA-position math below,
 * which was also computing on the wrong buffer geometry).
 *
 * FIX: reference SDR_RX_BLOCK_SAMPLES (sdr_rx.h) directly instead of
 * hand-copying it - this file now has ONE source of truth for the
 * frame count, and it can never silently drift out of sync with the
 * actual RX block size again, the way the plain literal just did.
 *
 * --- TX audio stream (demodulated audio -> DAC) ---------------------
 *
 * A 2-half ping-pong buffer played by the same circular DMA channel
 * (DMA0/CH4) that the silence/test-tone paths use. Each half holds
 * exactly one RX block's worth of stereo frames: since RX and TX are
 * clocked by the SAME codec-driven BCLK/WS at the same rate, every
 * completed RX half corresponds to exactly one TX half consumed -
 * the two sides cannot drift, only the phase offset at startup
 * varies, and gd32_i2s_stream_write_half() re-derives the safe half
 * from the live DMA position on every call, so it self-corrects.
 */
#define STREAM_FRAMES_PER_HALF_MAX SDR_RX_BLOCK_SAMPLES_MAX /* 512 - see
                                    * sdr_rx.h's own comment on SDR_RX_BLOCK_SAMPLES_MAX.
                                    * Buffer below is ALWAYS this size; the ACTIVE half
                                    * size (s_stream_frames_per_half) tracks whichever
                                    * mode is running - see gd32_i2s_stream_reconfigure(). */
#define STREAM_WORDS_PER_HALF_MAX  (STREAM_FRAMES_PER_HALF_MAX * 2U) /* stereo */
#define STREAM_TOTAL_WORDS_MAX     (STREAM_WORDS_PER_HALF_MAX * 2U)

static int16_t s_stream_buf[STREAM_TOTAL_WORDS_MAX]; /* zero-initialized: silence */

/* Active stream geometry - defaults to SDR_RX_BLOCK_SAMPLES (48kHz);
 * gd32_i2s_stream_reconfigure() (05/08/2026, added for WFM's 192kHz
 * reactivation) updates these when switching rates, following the
 * exact same stop/reconfigure/start split sdr_rx.h/.c already
 * established - see its own comments for why the order matters. */
static uint32_t s_stream_frames_per_half = SDR_RX_BLOCK_SAMPLES;
static uint32_t s_stream_words_per_half  = SDR_RX_BLOCK_SAMPLES * 2U;
static uint32_t s_stream_total_words     = SDR_RX_BLOCK_SAMPLES * 4U;

/* See gd32_i2s_stream_write_half()'s own comment - counts real FERR
 * occurrences on I2S1_ADD, cheaply, once per TX block. */
static volatile uint32_t s_tx_ferr_count = 0U;

/* Shared DMA-arming step - used by both gd32_i2s_dma_start_stream()
 * and gd32_i2s_stream_start() (the re-arm half of reconfigure - see
 * sdr_rx_start()'s equivalent for the reasoning on why this is
 * split out). Silences the WHOLE buffer first, not just the active
 * portion - harmless and simpler than tracking which bytes are
 * "stale" from a previous, differently-sized configuration. */
static void stream_arm_dma(void)
{
    dma_single_data_parameter_struct dma_init_struct;
    uint32_t w;

    for (w = 0; w < STREAM_TOTAL_WORDS_MAX; w++) {
        s_stream_buf[w] = 0;
    }

    rcu_periph_clock_enable(RCU_DMA0);

    dma_channel_disable(DMA0, DMA_CH4);
    while ((DMA_CHCTL(DMA0, DMA_CH4) & DMA_CHXCTL_CHEN) != 0U) {
        /* a disable request can take a few cycles to complete */
    }
    dma_deinit(DMA0, DMA_CH4);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(I2S1_ADD);
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr        = (uint32_t)s_stream_buf;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.number              = s_stream_total_words;
    /* *** 05/08/2026 fix *** - was DMA_PRIORITY_HIGH, strictly below
     * sdr_rx.c's RX channel (DMA0/CH3, ULTRA_HIGH). THIS is the
     * instance that actually matters for the WFM "ruido de fondo"
     * report - stream_arm_dma() is what's live during real audio
     * playback (the silence-keeper/test-tone instances only run
     * before/outside a real stream). See gd32_i2s_dma_start_silence()'s
     * comment for the full reasoning. */
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH4, &dma_init_struct);

    dma_channel_subperipheral_select(DMA0, DMA_CH4, DMA_SUBPERI2);
    dma_circulation_enable(DMA0, DMA_CH4);
    dma_channel_enable(DMA0, DMA_CH4);
    spi_dma_enable(I2S1_ADD, SPI_DMA_TRANSMIT);
}

void gd32_i2s_dma_start_stream(void)
{
    gd32_i2s_stream_arm(SDR_RX_BLOCK_SAMPLES);
}

/*
 * *** 05/08/2026, added for the "full reinit instead of live resync"
 * rewrite *** - the real bring-up logic, parametrized by frame count
 * so cold boot (gd32_i2s_dma_start_stream(), unchanged call site in
 * main.c) and every live rate switch (main.c's apply_demod_mode(),
 * called directly with the new rate's frame count) share ONE path,
 * the same way sdr_rx_bringup() unified sdr_rx.c's side. No disable/
 * re-enable of I2S1_ADD here anymore - gd32_i2s_init_slave(rate) (see
 * this file's own header comment) already did a FULL teardown/rebuild
 * of I2S1_ADD immediately before this runs on every path now, cold
 * boot or live, so I2S1_ADD is already freshly enabled and there's
 * nothing stale left to resync.
 */
void gd32_i2s_stream_arm(uint32_t frames_per_half)
{
    s_stream_frames_per_half = frames_per_half;
    s_stream_words_per_half  = frames_per_half * 2U;
    s_stream_total_words     = frames_per_half * 4U;

    stream_arm_dma();

    debug_print_dec("gd32_i2s: TX stream DMA armed, stereo frames per half (starts silent)",
                     s_stream_frames_per_half);
}

/*
 * Cleanly stops the TX stream DMA channel - call before reconfiguring
 * the codec's own clock registers for a rate change, mirroring
 * sdr_rx_stop()'s own comment on why (see main.c's mode-switch
 * sequence for the full ordering across both channels).
 */
void gd32_i2s_stream_stop(void)
{
    dma_channel_disable(DMA0, DMA_CH4);
    while ((DMA_CHCTL(DMA0, DMA_CH4) & DMA_CHXCTL_CHEN) != 0U) {
        /* a disable request can take a few cycles to complete */
    }
}

/*
 * *** 05/08/2026, REMOVED gd32_i2s_stream_reconfigure()/
 * gd32_i2s_stream_start() - superseded by gd32_i2s_stream_arm() above
 * ***, part of the "full reinit instead of live resync" rewrite. The
 * old split (reconfigure the frame count, then separately resync
 * I2S1_ADD via disable/re-enable and re-arm DMA) existed because a
 * live switch used to leave I2S1_ADD's existing configuration in
 * place and just nudge it - gd32_i2s_init_slave(rate) now does a FULL
 * teardown/rebuild of I2S1_ADD on every switch (see its own comment),
 * so there's nothing left for a separate resync step to do; a single
 * gd32_i2s_stream_arm(frames_per_half) call right after covers both
 * what reconfigure() and start() used to do together.
 */

/* Diagnostic-only: logs the first several gd32_i2s_stream_write_half()
 * calls after each gd32_i2s_stream_start() (stream_arm_dma() resets
 * this to 0) - added 05/08/2026 to chase the "first mode switch after
 * boot is silent, but the switch after that works" report. Original
 * theory: if dma_transfer_number_get() returns something unexpected
 * right after a fresh arm, the playing_half/write_half math below
 * could pick the WRONG half for the first few calls, writing into
 * audio that's actively being played instead of the free half.
 *
 * *** RULED OUT by real hardware logs *** - the project owner
 * captured one failing AM->WFM switch and one working AM->WFM switch
 * (same session, second attempt) and the DMA_CH4 remaining/pos/
 * playing_half/write_half sequence for the first 6 calls was
 * IDENTICAL between them. Whatever's failing on the first switch, it
 * isn't this half-selection logic - see aic3204_set_rate_power_up()'s
 * comment for the theory being tried next (codec PLL settling time).
 * Left in place since it's cheap and still useful for spotting a
 * genuine half-selection bug if one shows up later, just no longer
 * the leading suspect for this particular report. */
void gd32_i2s_stream_write_half(const int16_t *stereo_frames)
{
    /*
     * *** 05/08/2026, ALL per-call debug_print* output removed from
     * this function *** - it did its job (both the original 6-call
     * startup dump and the later same-half/heartbeat additions helped
     * track down and confirm the I2S frame-sync bug fixed in
     * main.c's apply_demod_mode()) but real hardware logs then showed
     * something important: the diagnostic prints themselves were
     * causing the NEXT symptom being chased. Once rx_lock's retry
     * fix guaranteed a clean raw I/Q capture, the project owner still
     * heard WFM audio as "muy distorsionado/entrecortado" - and the
     * log showed "SAME HALF WRITTEN TWICE" firing in tight clusters
     * immediately after every logged/heartbeat block, far too
     * regularly (every exact multiple of the heartbeat interval) to
     * be a real, independent glitch. debug_print()/debug_print_dec()
     * go out over a slow, blocking UART - stacking several of them
     * back-to-back inside this function, which runs synchronously in
     * the audio ISR chain every 2.67ms, can itself eat enough of that
     * budget to desync the very ping-pong buffer position being
     * reported, especially for WFM's much chattier per-block logging
     * elsewhere in this same call chain (demod_wfm_process_raw()'s
     * own checkpoints). In short: the instrumentation was measuring
     * its own footprint, not a real audio-path bug. Removed entirely
     * now that the diagnostics have already served their purpose -
     * see git history (or the transcript) if this class of bug ever
     * needs revisiting; re-add sparingly and expect to interpret
     * results with this caveat in mind.
     */
    uint32_t remaining = dma_transfer_number_get(DMA0, DMA_CH4);
    uint32_t pos = s_stream_total_words - remaining;
    uint32_t playing_half = (pos < s_stream_words_per_half) ? 0U : 1U;
    uint32_t write_half = 1U - playing_half;
    int16_t *dst = &s_stream_buf[write_half * s_stream_words_per_half];
    uint32_t w;

    /*
     * *** 05/08/2026, added for WFM FERR frequency diagnostics *** -
     * same reasoning as sdr_rx.c's DMA0_Channel3_IRQHandler() counter:
     * this function already runs every TX block, so it's the cheapest
     * place to count REAL FERR occurrences instead of just sampling
     * "was it set" once every ~1.5s from main.c. No debug_print here -
     * this specific function has its own hard-won lesson about UART
     * cost inside the audio ISR chain (see the big comment above).
     */
    {
        /*
         * *** 05/08/2026, FIXED alongside sdr_rx.c's identical fix ***
         * - removed the `discard = SPI_DATA(I2S1_ADD)` read that used
         * to be here. DMA0/CH4 is actively driving I2S1_ADD's transmit
         * FIFO from SPI_DATA while this function runs every block - a
         * manual CPU read/write of that register here can race the
         * DMA controller. See sdr_rx.c's DMA0_Channel3_IRQHandler()
         * comment for the full story (a severe real-hardware
         * regression - full-scale-saturated raw I/Q, near-constant
         * FERR - traced to this exact pattern on the RX side).
         */
        uint32_t stat = SPI_STAT(I2S1_ADD);
        if ((stat & SPI_STAT_FERR) != 0U) {
            s_tx_ferr_count++;
        }
    }

    for (w = 0; w < s_stream_words_per_half; w++) {
        dst[w] = stereo_frames[w];
    }
}

uint32_t gd32_i2s_get_tx_ferr_count(void)
{
    return s_tx_ferr_count;
}

void gd32_i2s_reset_tx_ferr_count(void)
{
    s_tx_ferr_count = 0U;
}

void gd32_i2s_dma_start_test_tone(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    tone_buf_fill_1khz();

    rcu_periph_clock_enable(RCU_DMA0);

    debug_print_hex32("gd32_i2s: SPI_STAT(I2S1_ADD) right before arming DMA "
                       "(bit1=TBE should be 1 if ready to write)",
                       SPI_STAT(I2S1_ADD));

    /* DMA0, Channel 4, PERIEN=010 (SUBPERI2) for I2S1_ADD_TX - now the
     * TX side after the block-role swap - confirmed against the
     * GD32F4xx User Manual's DMA request mapping table (Table 10-2,
     * "Peripheral requests to DMA0"). */
    dma_deinit(DMA0, DMA_CH4);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(I2S1_ADD);
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr        = (uint32_t)s_tone_buf;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.number              = TONE_SAMPLES_PER_CYCLE * 2U;
    /* *** 05/08/2026 fix *** - was DMA_PRIORITY_HIGH, see
     * gd32_i2s_dma_start_silence()'s comment for why this now matches
     * RX's ULTRA_HIGH. */
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH4, &dma_init_struct);

    dma_channel_subperipheral_select(DMA0, DMA_CH4, DMA_SUBPERI2);

    dma_circulation_enable(DMA0, DMA_CH4);
    dma_channel_enable(DMA0, DMA_CH4);

    /* Enable the DMA transmit request on I2S1_ADD itself - without
     * this, the DMA channel is armed but the peripheral never
     * triggers it. */
    spi_dma_enable(I2S1_ADD, SPI_DMA_TRANSMIT);

    debug_print("gd32_i2s: circular DMA armed, 1kHz test tone should now be continuous "
                "(no longer stopping) on PB14, and audible on the AIC3204 output if "
                "phase 1 (I2C) left the codec unmuted\n");

    /*
     * Bring-up diagnostic: reads the DMA channel's remaining transfer
     * count twice, separated by a delay. If it doesn't change, the
     * channel is armed but inert (wrong DMA channel/subperipheral, or
     * DMATEN did not actually take); if it decrements, the channel is
     * firing and any remaining issue is elsewhere in the chain. Also
     * checks the DMATEN readback and the channel's error flags
     * (FEE/SDE/TAE), which would indicate an invalid
     * channel/subperipheral selection some other way.
     */
    {
        uint32_t cnt_a, cnt_b;
        volatile uint32_t d;

        debug_print_hex32("gd32_i2s: SPI_CTL1(I2S1_ADD) after spi_dma_enable (bit1=DMATEN should be 1)",
                           SPI_CTL1(I2S1_ADD));
        debug_print_hex32("gd32_i2s: DMA_CHCTL(DMA0,CH4) raw (bit0=CHEN should be 1)",
                           DMA_CHCTL(DMA0, DMA_CH4));

        cnt_a = dma_transfer_number_get(DMA0, DMA_CH4);
        for (d = 0; d < 2000000U; d++) { __NOP(); }
        cnt_b = dma_transfer_number_get(DMA0, DMA_CH4);

        debug_print_dec("gd32_i2s: DMA_CHCNT remaining, reading A", cnt_a);
        debug_print_dec("gd32_i2s: DMA_CHCNT remaining, reading B (after delay)", cnt_b);
        if (cnt_a == cnt_b) {
            debug_print("gd32_i2s: *** DMA_CHCNT NOT MOVING - channel is armed but INERT, "
                        "never triggers a request (check the DMA channel/subperipheral "
                        "selection against the request mapping table) ***\n");
        } else {
            debug_print("gd32_i2s: DMA_CHCNT IS decrementing - the channel is firing "
                        "requests, look elsewhere in the chain\n");
        }

        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_FEE) == SET) {
            debug_print("gd32_i2s: *** DMA flag FEE (FIFO error/exception) set ***\n");
        }
        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_SDE) == SET) {
            debug_print("gd32_i2s: *** DMA flag SDE (single data mode exception) set ***\n");
        }
        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_TAE) == SET) {
            debug_print("gd32_i2s: *** DMA flag TAE (transfer access error) set - channel/"
                        "subperipheral is very likely misconfigured ***\n");
        }
        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_HTF) == SET) {
            debug_print("gd32_i2s: DMA flag HTF (half transfer) set - "
                        "the channel HAS transferred at least half the buffer\n");
        }
        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_FTF) == SET) {
            debug_print("gd32_i2s: DMA flag FTF (full transfer) set - "
                        "the channel HAS completed at least one circular round\n");
        }
        if (dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_HTF) == RESET &&
            dma_flag_get(DMA0, DMA_CH4, DMA_FLAG_FTF) == RESET &&
            cnt_a == cnt_b) {
            debug_print("gd32_i2s: neither HTF nor FTF ever set - the channel has not "
                        "executed a single transfer since it was armed\n");
        }
    }
}

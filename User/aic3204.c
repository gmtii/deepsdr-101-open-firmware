#include "aic3204.h"
#include "i2c_bitbang.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

#define AIC3204_NRST_PORT GPIOB
#define AIC3204_NRST_PIN  GPIO_PIN_11

static uint8_t s_addr = AIC3204_ADDR_DEFAULT;

/* ms tick counter from main.c, same mechanism already used by
 * rm68120_exmc.c and touch.c, for reset timing. */
extern volatile uint32_t g_msticks;

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_msticks;
    while ((g_msticks - start) < ms) {
        __NOP();
    }
}

static void print_addr(uint8_t addr)
{
    debug_print_hex32("aic3204: I2C device found at", addr);
}

/*
 * Releases the AIC3204 from hardware reset before any I2C
 * transaction. Without this the chip can stay in reset indefinitely
 * (depending on how nRESET is pulled) and never respond reliably on
 * the bus.
 *
 * The timing used here (10ms held in reset, 10ms after release) is
 * conservative and has not been trimmed against the exact datasheet
 * minimums for this specific board - if more margin is ever needed,
 * this is the place to adjust.
 *
 * Extended from an initial 10ms to 50ms per side, matching the more
 * generous timing (200ms) used by an earlier known-working driver for
 * this same board - cheap extra margin while chasing an RX capture
 * bug that could plausibly be a power-up/reset sequencing issue.
 */
static void aic3204_hw_reset(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_mode_set(AIC3204_NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, AIC3204_NRST_PIN);
    gpio_output_options_set(AIC3204_NRST_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, AIC3204_NRST_PIN);

    gpio_bit_reset(AIC3204_NRST_PORT, AIC3204_NRST_PIN); /* hold in reset */
    delay_ms(50);
    gpio_bit_set(AIC3204_NRST_PORT, AIC3204_NRST_PIN); /* release */
    delay_ms(50);

    debug_print("aic3204: nRESET (PB11) released\n");
}

void aic3204_init(uint8_t i2c_addr)
{
    s_addr = i2c_addr;
    aic3204_hw_reset();
    i2c_bitbang_init();
}

uint8_t aic3204_write_reg(uint8_t page, uint8_t reg, uint8_t value)
{
    uint8_t buf[2];

    buf[0] = 0x00; /* Page Control Register, always 0x00 on every page */
    buf[1] = page;
    if (!i2c_write(s_addr, buf, 2)) {
        return 0;
    }

    buf[0] = reg;
    buf[1] = value;
    return i2c_write(s_addr, buf, 2);
}

uint8_t aic3204_read_reg(uint8_t page, uint8_t reg, uint8_t *value)
{
    uint8_t buf[2];

    buf[0] = 0x00;
    buf[1] = page;
    if (!i2c_write(s_addr, buf, 2)) {
        return 0;
    }

    /* "Register to read" write with no real repeated-start in
     * between: i2c_write/i2c_read each do a full START..STOP. The
     * AIC3204 accepts this (a real repeated-start is not mandatory,
     * unlike some sensors). If this ever causes issues on real
     * hardware, switching to a true repeated-start would be the first
     * thing to try. */
    buf[0] = reg;
    if (!i2c_write(s_addr, buf, 1)) {
        return 0;
    }

    return i2c_read(s_addr, value, 1);
}

/* Raw register read WITHOUT selecting a page first (unlike
 * aic3204_read_reg, meant for normal use). Only useful to verify that
 * a previously written value (e.g. the active page) actually
 * persisted, without the read itself masking anything. */
static uint8_t read_reg_raw(uint8_t reg, uint8_t *value)
{
    uint8_t r = reg;

    if (!i2c_write(s_addr, &r, 1)) {
        return 0;
    }
    return i2c_read(s_addr, value, 1);
}

uint8_t aic3204_probe_and_reset(void)
{
    uint8_t buf[2];
    uint8_t readback;
    uint8_t ok = 1;

    debug_print("aic3204: Page Control Register closed-loop test...\n");

    /* Select page 1 (direct write, not via aic3204_write_reg, to avoid
     * confusing "page" with "target register within that page" -
     * here both happen to be 0x00). */
    buf[0] = 0x00;
    buf[1] = 1;
    if (!i2c_write(s_addr, buf, 2)) {
        debug_print("aic3204: NACK writing page 1\n");
        ok = 0;
    } else if (!read_reg_raw(0x00, &readback) || readback != 1) {
        debug_print_dec("aic3204: page read back after selecting 1", readback);
        ok = 0;
    } else {
        debug_print("aic3204: page 1 confirmed\n");
    }

    buf[0] = 0x00;
    buf[1] = 0;
    if (!i2c_write(s_addr, buf, 2)) {
        debug_print("aic3204: NACK writing page 0\n");
        ok = 0;
    } else if (!read_reg_raw(0x00, &readback) || readback != 0) {
        debug_print_dec("aic3204: page read back after selecting 0", readback);
        ok = 0;
    } else {
        debug_print("aic3204: page 0 confirmed\n");
    }

    if (!ok) {
        debug_print("aic3204: closed-loop test FAILED - check the I2C address with "
                     "aic3204_scan_bus(), codec power, or the SCL/SDA wiring\n");
        return 0;
    }

    debug_print("aic3204: closed-loop OK, I2C link confirmed\n");

    /* Software reset: page 0, register 0x01, bit 0 = 1. Self-clearing,
     * no register to read back for confirmation - relying on the
     * closed-loop test above having already validated the link. */
    if (!aic3204_write_reg(0, 0x01, 0x01)) {
        debug_print("aic3204: software reset write failed (no ACK)\n");
        return 0;
    }
    debug_print("aic3204: software reset sent\n");

    return 1;
}

void aic3204_scan_bus(void)
{
    debug_print("aic3204: scanning I2C bus (0x08-0x77)...\n");
    i2c_scan(0x08, 0x77, print_addr);
    debug_print("aic3204: scan complete\n");
}

/*
 * Phase 2, rewritten (28/07/2026) from a REAL I2C bus capture of the
 * original, known-working firmware's AIC3204 configuration - not
 * derived, not extrapolated, not guessed. Every write below is a
 * literal (page, register, value) triple exactly as captured on the
 * real bus, in the exact order it was sent, address 0x18 only (the
 * capture also showed unrelated traffic to 0x60 - the MS5351 LO chip
 * on the same bus - filtered out here).
 *
 * The clock chain this reveals is genuinely different from what this
 * driver assumed for a long time - and was STILL wrong even after that
 * revision, corrected again 01/09/2026 after the project owner
 * pointed out the earlier claim made no physical sense: the codec is
 * the I2S MASTER here (BCLK/WCLK are ITS outputs, per R27 above), so
 * "the PLL is clocked from BCLK" would mean the PLL derives its own
 * reference from a signal the codec itself only generates downstream
 * of that very PLL - backwards. Re-decoded directly against register
 * 4's real bit fields (confirmed against two independent real AIC3204
 * configs, not this project's own guess): P0R4=0x43's low nibble
 * matches the documented "0x03" pattern (PLL_CLKIN=MCLK device pin,
 * CODEC_CLKIN=PLL_CLK) exactly - PLL_CLKIN is MCLK, not BCLK. P0R5=0x94
 * decodes as PLL_ON=1, P=1 (bits 6:4=001), R=4 (bits 3:0=0100) using
 * the same bit layout confirmed against a real working AIC3204 config
 * (P0R5=0x91 documented there as "P=1 and R=1"). With PLL_CLKIN=MCLK=
 * 1.536MHz (this project's own TIMER2_CH0/PC6 signal - see
 * gd32_i2s_mclk_timer_start()) and R=4, J=14, D=0, P=1: CODEC_CLKIN =
 * MCLK x R x J / P = 1.536MHz x 4 x 14 / 1 = 86.016MHz exactly -
 * matches independently, with no need to invoke BCLK at all. This is
 * ALSO now consistent with a real-hardware experiment the SAME day:
 * disabling gd32_i2s_mclk_timer_start() (on the theory that this
 * comment's old, wrong claim made it vestigial) caused real, observed
 * problems (frequencies off, degraded WFM reception) - MCLK generation
 * is genuinely required, not vestigial, and was restored. The earlier
 * "gd32_i2s.c no longer configures an MCLK pin at all" note that used
 * to follow this paragraph was simply wrong and has been removed.
 *
 * *** 04/08/2026: Fs 192kHz -> 48kHz *** (per the project owner, to
 * fit the SSB/NR audio ISR back inside its real-time budget - see
 * nr_ss.c's header comment). BCLK/MCLK/PLL above are ALL UNCHANGED -
 * the GD32 is the I2S SLAVE here (see gd32_i2s.h), so WCLK simply
 * BECOMES whatever Fs the codec's own divider chain below produces;
 * nothing about how BCLK itself is generated needed to move. Only the
 * divider chain downstream of the (unchanged) 86.016MHz CODEC_CLKIN
 * changed, and by design as little of it as possible:
 *
 *   ADC: NADC(1) x MADC(28) x AOSR(64) = 1792 -> 86.016MHz/1792 = 48kHz
 *        (was NADC(1) x MADC(7) x AOSR(64) = 448 -> 192kHz - only
 *        MADC changed, x4, matching the x4 Fs drop. AOSR=64 stays
 *        valid: PRB_R1 - the ADC processing block already selected,
 *        unchanged below - supports AOSR 128 or 64 per the TLV320AIC3204
 *        datasheet's Table 2-10 (ADC decimation Filter A), confirmed
 *        against TI's own app-note guidance, not assumed.)
 *
 *   DAC: NDAC(2) x MDAC(7) x DOSR(128) = 1792 -> 86.016MHz/1792 = 48kHz
 *        (was NDAC(2) x MDAC(7) x DOSR(32) = 448 -> 192kHz - NDAC/MDAC
 *        UNCHANGED, only DOSR changed, x4. This ALSO required changing
 *        the DAC processing block itself, from PRB_P17 to PRB_P1: the
 *        old PRB_P17 is DAC "Filter Type C", which TI's own
 *        documentation says is specifically for 192ksps operation
 *        with DOSR=32 - not valid at 48kHz. PRB_P1 is "Filter Type A",
 *        for which TI's guidance is DOSR=(48000/target_fs)*128, which
 *        at target_fs=48000 works out to exactly DOSR=128 - the value
 *        used here. *** PRB_P1 SPECIFICALLY (as opposed to another
 *        Filter-A block, e.g. PRB_P2-P6) is the one part of this
 *        change NOT taken from a real capture or a fully unambiguous
 *        datasheet table read - it's this port's best-supported
 *        inference (PRB_P17's role here is bare pass-through, no
 *        on-chip biquads/AGC/etc - all filtering is done on the GD32
 *        side - and PRB_P1 is Filter A's equivalent "no extra
 *        processing" block). TI's own docs warn that a PRB/AOSR-DOSR
 *        mismatch shows up as "noisy, gain reduced" audio - worth a
 *        careful listen on first boot specifically for that failure
 *        mode before trusting this value long-term.)
 *
 *   BCLK: R30 (page0, reg 0x1E) - "BCLK N Divider" - MISSED entirely in
 *        the first pass of this migration, found by the project owner
 *        via a real oscilloscope measurement: BCLK read 6.144MHz (the
 *        OLD 192kHz-correct value, 192000*32) instead of the expected
 *        1.536MHz (48000*32 for 16-bit stereo), even though WCLK
 *        correctly read 48kHz. This divider runs off DAC_CLK
 *        (=CODEC_CLKIN/NDAC=86.016MHz/2=43.008MHz, confirmed by
 *        reverse-fitting the OLD captured N=7 against the OLD measured
 *        6.144MHz: 43.008MHz/7=6.144MHz exactly) - a path independent
 *        of the NADC/MADC/AOSR and NDAC/MDAC/DOSR dividers above, so
 *        changing those never touched this one. Fixed the same way as
 *        MADC: N=7 -> N=28 (same x4 as everywhere else), giving
 *        43.008MHz/28=1.536MHz=32*48kHz exactly.
 *
 * The ADC input routing (P1_R52/54/55/57) and ADC power-up (R81/R82)
 * already matched this capture exactly even before this rewrite - so
 * those were never the bug. Registers 8/9/44/46 (pages, not
 * addresses) further down are DRC/biquad filter coefficients (audio
 * processing, not core bring-up) - included for completeness since
 * they're genuinely part of the real sequence, not because their
 * exact function has been analyzed here.
 */
static void wr(uint8_t page, uint8_t reg, uint8_t value, const char *what)
{
    if (!aic3204_write_reg(page, reg, value)) {
        debug_print("aic3204: *** write with NO ACK: ");
        debug_print(what);
        debug_print(" ***\n");
    }
}

/*
 * *** 05/08/2026, split out from aic3204_run_full_sequence() below ***
 * - see aic3204_configure_rate()'s comment for why this needed to
 * become its own callable step: even with a genuine hardware nRESET
 * pulse (this exact function) wrapped in the exact same full register
 * sequence as cold boot, real hardware logs showed a live rate switch
 * still produces continuous SPI_STAT_FERR (hundreds per check window)
 * while cold boot with the identical codec-side sequence measures
 * ZERO - meaning the codec itself now reaches an identical state
 * either way, and whatever's still different has to be on the GD32
 * side. The leading remaining theory: at cold boot, gd32_i2s_init_
 * slave_192k() enables the GD32's I2S peripherals BEFORE the codec
 * ever outputs real BCLK/WS (those don't exist until aic3204_phase2_
 * init()'s PLL/divider writes land) - the GD32 slave is listening from
 * the very first real clock edge. On a live switch, the OLD order was
 * backwards: the codec's full reconfigure (BCLK/WS resuming partway
 * through it, well before the sequence returns) happened BEFORE
 * sdr_rx_start()/gd32_i2s_stream_start()'s disable/re-enable of the
 * GD32 side - so the GD32 peripheral re-enables into an ALREADY-
 * RUNNING clock at an arbitrary phase, not from a clean first edge.
 * main.c's apply_demod_mode() now calls this reset (codec falls
 * silent - no BCLK/WS) FIRST, then re-enables/arms the GD32 side
 * WHILE the codec is still silent, and only THEN calls aic3204_
 * configure_rate() below to bring BCLK/WS back - reproducing the cold
 * boot ordering for every live switch too.
 */
void aic3204_rate_switch_reset(void)
{
    aic3204_hw_reset();
}

/*
 * *** 05/08/2026, refactored for the WFM FERR investigation *** -
 * this used to be aic3204_phase2_init() alone, called ONCE at cold
 * boot, hardcoded to whatever the default rate was at the time (48kHz
 * then, 96kHz now). Live rate switches (aic3204_set_rate_
 * registers()) used a much smaller, separate register set that only
 * touched the divider chain (MADC/DOSR/PRB/BCLK-N) and power-cycled
 * ADC/DAC - it never re-ran the software reset (R1=0x01) or anything
 * upstream of the dividers (PLL config, page 1 analog bias/routing,
 * MIC_PGA, the DRC/biquad coefficient banks). Real hardware logs
 * showed a from-cold WFM boot (this same 192kHz register set, but via
 * a full reset) runs with ZERO SPI_STAT_FERR for as long as no rate
 * switch happens - the moment a LIVE switch occurs (in either
 * direction), FERR starts firing continuously and, critically, NEVER
 * clears again even switching back to a mode that was clean moments
 * before, on either RX or TX (they always match exactly, since they
 * share WS/CK). That rules out the PCB/signal-integrity theory
 * entirely (same wiring, same rate, works perfectly from cold) and
 * points at some persistent internal state the small register subset
 * doesn't reset.
 *
 * *** Second finding, same investigation ***: even adding a genuine
 * hardware nRESET pulse (aic3204_rate_switch_reset() above) - the
 * exact same reset cold boot uses - to a live switch did NOT clear
 * FERR either. That rules the codec itself back OUT again: it reaches
 * an identical state on both paths now, so this function alone isn't
 * the fix - see aic3204_rate_switch_reset()'s comment for where the
 * remaining difference is believed to be (GD32-side enable ordering),
 * which is why the reset call was pulled OUT of this function and
 * into its own step, called separately, with the GD32 I2S resync
 * sandwiched in between it and this one - see main.c's apply_demod_
 * mode().
 *
 * This function is now just the register configuration itself
 * (parameterized by rate), assuming the caller already reset the
 * codec via aic3204_rate_switch_reset() (or, for cold boot, via
 * aic3204_phase2_init() calling both in sequence below). Leaves
 * ADC/DAC powered OFF at the end - aic3204_set_rate_power_up() turns
 * them on, once the caller's DMA is armed and waiting.
 */
void aic3204_configure_rate(aic3204_rate_t rate)
{
    debug_print("\naic3204: register configuration (post-reset) ported from a real I2C "
                "capture - now shared by cold boot AND every live rate switch\n");

    wr(0, 0x00, 0x00, "select page 0 (after hardware nRESET)");

    /* --- Clock/PLL: CODEC_CLKIN sourced via PLL from MCLK (P0R4's
     * real bits - see this function's header comment for the
     * corrected 01/09/2026 decode; NOT from BCLK, the earlier
     * comment here was wrong), PLL ON, P=1/R=4/J=14/D=0, giving
     * CODEC_CLKIN=86.016MHz from MCLK=1.536MHz (this project's own
     * TIMER2_CH0/PC6 signal) --- */
#if AIC3204_TEST_LOOPBACK
    wr(0, 0x04, 0x43, "R4 CODEC_CLKIN mux (captured)");
    wr(0, 0x05, 0x94, "R5 PLL on + P/R (captured)");
    wr(0, 0x06, 0x0E, "R6 PLL J=14 (captured)");
    wr(0, 0x07, 0x00, "R7 PLL D hi (captured)");
    wr(0, 0x08, 0x00, "R8 PLL D lo (captured)");
    wr(0, 0x0B, 0x82, "R11 NDAC=2, power on (captured)");
    wr(0, 0x0C, 0x87, "R12 MDAC=7, power on (captured)");
    switch (rate) {
    case AIC3204_RATE_192K:
        wr(0, 0x0D, 0x00, "R13 DOSR hi (192kHz: DOSR=32)");
        wr(0, 0x0E, 0x20, "R14 DOSR=32 lo (192kHz)");
        wr(0, 0x3C, 0x11 | 0x20, "R60(pg0) PRB_P17 (192kHz) + AUDIO BUS LOOPBACK (TEST)");
        break;
    case AIC3204_RATE_48K:
        wr(0, 0x0D, 0x00, "R13 DOSR hi (48kHz: DOSR=128)");
        wr(0, 0x0E, 0x80, "R14 DOSR=128 lo (48kHz)");
        wr(0, 0x3C, 0x01 | 0x20, "R60(pg0) PRB_P1 (48kHz) + AUDIO BUS LOOPBACK (TEST)");
        break;
    case AIC3204_RATE_96K:
    default:
        wr(0, 0x0D, 0x00, "R13 DOSR hi (96kHz: DOSR=64)");
        wr(0, 0x0E, 0x40, "R14 DOSR=64 lo (96kHz)");
        /* bit D5 (0x20) = Audio Bus Loopback, on top of the PRB_P1 base (0x01) */
        wr(0, 0x3C, 0x01 | 0x20, "R60(pg0) PRB_P1 (96kHz) + AUDIO BUS LOOPBACK (TEST)");
        break;
    }
    debug_print("aic3204: *** LOOPBACK TEST MODE ENABLED - RX should mirror TX, ADC/DAC "
                "bypassed. Remember to feed a real TX pattern, not silence, and to set "
                "AIC3204_TEST_LOOPBACK back to 0 afterwards ***\n");
#else
    wr(0, 0x04, 0x43, "R4 CODEC_CLKIN mux (captured)");
    wr(0, 0x05, 0x94, "R5 PLL on + P/R (captured)");
    wr(0, 0x06, 0x0E, "R6 PLL J=14 (captured)");
    wr(0, 0x07, 0x00, "R7 PLL D hi (captured)");
    wr(0, 0x08, 0x00, "R8 PLL D lo (captured)");
    wr(0, 0x0B, 0x82, "R11 NDAC=2, power on (captured)");
    wr(0, 0x0C, 0x87, "R12 MDAC=7, power on (captured)");
    switch (rate) {
    case AIC3204_RATE_192K:
        wr(0, 0x0D, 0x00, "R13 DOSR hi (192kHz: DOSR=32)");
        wr(0, 0x0E, 0x20, "R14 DOSR=32 lo (192kHz)");
        wr(0, 0x3C, 0x11, "R60(pg0) PRB_P17, Filter C (192kHz)");
        break;
    case AIC3204_RATE_48K:
        /* Added 01/09/2026 - restores the ORIGINAL 04/08/2026 48kHz
         * values (see this function's own header comment history -
         * this project ran AM/USB/LSB/NFM at 48kHz before later moving
         * up to 96kHz for wider RF coverage), as a user-selectable
         * escape hatch: a birdie tied to N*Fs (confirmed 287*96kHz=
         * 27.552MHz on real hardware, landing right in the 11m/CB
         * band) moves to a different, less troublesome spot at 48kHz
         * instead - see main.c's RATE tile. Same Filter-A processing
         * block (PRB_P1) as 96kHz - only DOSR doubles again (64->128),
         * same simple "double DOSR, halve Fs" pattern each step down
         * from 192kHz's 32.
         */
        wr(0, 0x0D, 0x00, "R13 DOSR hi (48kHz: DOSR=128)");
        wr(0, 0x0E, 0x80, "R14 DOSR=128 lo (48kHz)");
        wr(0, 0x3C, 0x01, "R60(pg0) PRB_P1, Filter A (48kHz) - same block 96kHz uses");
        break;
    case AIC3204_RATE_96K:
    default:
        wr(0, 0x0D, 0x00, "R13 DOSR hi (96kHz: DOSR=64)");
        wr(0, 0x0E, 0x40, "R14 DOSR=64 lo (96kHz)");
        wr(0, 0x3C, 0x01, "R60(pg0) PRB_P1, Filter A (96kHz) - same Filter-A block as "
                           "48kHz used, DOSR=(48000/target_fs)*128=64 per the same TI "
                           "formula, no reason to switch families for an intermediate rate");
        break;
    }
#endif
    /*
     * *** 05/08/2026, R27/R30 MOVED OUT OF HERE - see
     * aic3204_start_bclk_wclk() below ***: R27 (0x1B) is what actually
     * puts BCLK/WCLK into OUTPUT/master mode on this codec (see this
     * function's own long-standing comment on register 0x1B, bits
     * D3:D2) - the moment this write completes, the codec starts
     * driving BCLK/WCLK on the shared pins, REGARDLESS of whether the
     * GD32 side has been resynced yet. Everything below this point in
     * this function (R37, NADC/MADC/AOSR/PRB_R, page 1 analog, the
     * page 8/9/46 biquad banks, the HP gain ramp) is roughly 75 more
     * bit-banged I2C register writes at ~100kHz-ish - tens of
     * milliseconds during which a live BCLK/WCLK is already toggling
     * on the bus while sdr_rx_start()/gd32_i2s_stream_start() haven't
     * run yet (they run AFTER this whole function returns, back in
     * main.c's apply_demod_mode()). That's very likely the real source
     * of the "needs several rx_lock retries, frequency varies session
     * to session" pattern: the GD32 slave peripherals get resynced
     * against an already-running clock at whatever phase tens of
     * milliseconds of I2C traffic happened to land on, not against a
     * fresh first edge the way the design intended.
     *
     * Fix: this function no longer starts the clock at all - it only
     * gets the codec's clock tree READY (PLL locked, NDAC/MDAC/DOSR/
     * NADC/MADC/AOSR dividers set) without ever driving BCLK/WCLK as
     * outputs. aic3204_start_bclk_wclk() (new, see below) writes ONLY
     * R30 then R27 (divider set BEFORE the pins go to output mode, so
     * there's no brief burst of BCLK at the previous/default divider
     * value before R30 corrects it - the old code did these in the
     * OPPOSITE order) and is called separately, from apply_demod_mode()
     * in main.c, AFTER sdr_rx_start()/gd32_i2s_stream_start() have
     * already put the GD32 side into a resynced, listening state. That
     * collapses the "resync happens against an already-running clock"
     * window from tens of milliseconds down to whatever this one I2C
     * register write itself takes (two bytes, comfortably under 1ms at
     * this bit rate) - not a full elimination of the race, but a large
     * reduction, and worth trying given everything else already ruled
     * out.
     */
    wr(0, 0x25, 0xEE, "R37 (captured)");
    wr(0, 0x12, 0x81, "R18 NADC=1, power on (captured)");
    switch (rate) {
    case AIC3204_RATE_192K:
        wr(0, 0x13, 0x87, "R19 MADC=7 (192kHz)");
        break;
    case AIC3204_RATE_48K:
        /* Added 01/09/2026, restoring the original 04/08/2026 48kHz
         * value - see aic3204_configure_rate()'s DAC-side comment for
         * the full "user-selectable birdie escape hatch" reasoning.
         * 0x80 (power on) | 28 (MADC) = 0x9C - matches the same
         * "double MADC each step down from 192kHz's 7" pattern
         * DOSR already follows on the DAC side (7/14/28, same ratios
         * as DOSR's 32/64/128). */
        wr(0, 0x13, 0x9C, "R19 MADC=28 (48kHz)");
        break;
    case AIC3204_RATE_96K:
    default:
        wr(0, 0x13, 0x8E, "R19 MADC=14 (96kHz)");
        break;
    }
    wr(0, 0x14, 0x40, "R20 AOSR=64 (unchanged - still valid for PRB_R1)");
    wr(0, 0x3D, 0x01, "R61 ADC processing block PRB_R1 (captured)");

    /* --- Page 1: analog power + HP/LO bias config --- */
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x01, 0x08, "P1R1 disable AVDD-from-DVDD (captured)");
    wr(1, 0x02, 0x01, "P1R2 analog blocks + LDO ON (captured)");
    wr(1, 0x7B, 0x01, "P1R123 (captured)");
    wr(1, 0x14, 0x25, "P1R20 (captured)");
    wr(1, 0x0A, 0x3B, "P1R10 (captured)");
    wr(1, 0x0C, 0x08, "P1R12 (captured)");
    wr(1, 0x0D, 0x08, "P1R13 (captured)");
    wr(1, 0x0E, 0x08, "P1R14 (captured)");
    wr(1, 0x03, 0x00, "P1R3 (captured)");
    wr(1, 0x04, 0x00, "P1R4 (captured)");
    wr(1, 0x10, 0x00, "P1R16 HPL gain (captured)");
    wr(1, 0x11, 0x00, "P1R17 HPR gain (captured)");
    wr(1, 0x12, 0x00, "P1R18 (captured)");
    wr(1, 0x09, 0x38, "P1R9 (captured)");
    wr(1, 0x3D, 0x00, "P1R61 (captured)");
    wr(1, 0x47, 0x32, "P1R71 (captured)");
    wr(1, 0x7B, 0x01, "P1R123 again (captured)");

    /*
    Lo que significa registro por registro
    Canal Izquierdo (Left ADC):
    0x34 = 0x10 → IN2L entra al terminal positivo del MICPGA izquierdo con resistencia de 10 kΩ 
    0x36 = 0x10 → IN2R entra al terminal negativo del MICPGA izquierdo con resistencia de 10 kΩ 
    Esto configura el canal izquierdo como diferencial: la señal se toma entre IN2L (+) e IN2R (−), no contra common-mode.
    Canal Derecho (Right ADC):
    0x37 = 0x04 → IN3R entra al terminal positivo del MICPGA derecho con 10 kΩ 
    0x39 = 0x04 → IN3R entra al terminal negativo del MICPGA derecho con 10 kΩ 
    Esto conecta IN3R a ambos terminales (+ y −) del PGA derecho, lo cual anularía la señal (ganancia diferencial ≈ 0). 

    Ganancia y Mute:
    0x3B = 0x00 y 0x3C = 0x00 → Ambos MICPGA están desmuteados con ganancia de 0 dB 
    MICBIAS:
    0x33 = 0x60 → El bias de micrófono está activo a 2.0 V, alimentado desde AVdd (no desde LDOIN) 
    */

    wr(1, 0x34, 0x10, "P1R52 IN2L -> LADC_P, 10k (captured)");
    wr(1, 0x36, 0x10, "P1R54 (captured)");
    wr(1, 0x37, 0x04, "P1R55 (captured)");
    wr(1, 0x39, 0x04, "P1R57 (captured)");
    wr(1, 0x3B, 0x00, "P1R59 MIC_PGA_L (captured)");
    wr(1, 0x3C, 0x00, "P1R60 MIC_PGA_R (captured)");
    wr(1, 0x33, 0x60, "P1R51 MICBIAS level (captured - NOT off, unlike the earlier "
                      "single-ended baseline which used 0x00 here)");

    /* --- Pages 8/44: DRC/biquad processing coefficients (page 8 =
     * ADC filter bank, page 44 = another coefficient bank) --- */
    wr(8,  0x00, 0x08, "select page 8");
    wr(8,  0x01, 0x04, "P8R1 (captured)");
    wr(44, 0x00, 0x2C, "select page 44");
    wr(44, 0x01, 0x04, "P44R1 (captured)");

    /* --- Page 0: DAC power/routing + ADC registers - LEFT OFF here
     * (0x16/0x00, "still down") on purpose - see this function's
     * header comment for why. aic3204_set_rate_power_up() is what
     * actually turns them on, once the caller has DMA armed and
     * waiting. --- */
    wr(0, 0x00, 0x00, "select page 0");
    wr(0, 0x3F, 0x16, "R63 DAC power/routing, DAC still DOWN (power-up deferred)");
    wr(0, 0x40, 0x00, "R64 (captured)");
    wr(0, 0x41, 0x0C, "R65 DAC L volume (captured baseline)");
    wr(0, 0x42, 0x0C, "R66 DAC R volume (captured baseline)");
    wr(0, 0x51, 0x00, "R81 ADC still DOWN (power-up deferred)");
    wr(0, 0x52, 0x00, "R82 ADC unmuted, 0dB fine gain (captured, matches proven baseline)");
    wr(0, 0x43, 0x93, "R67 (captured)");

    /* --- Short page1/page0 dance adjusting HP driver gain a few
     * times (captured exactly as-is - looks like the original
     * firmware's own gain ramp-up sequence for the headphone path) --- */
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x12, 0x3A, "P1R18 (captured)");
    wr(0, 0x00, 0x00, "select page 0");
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x10, 0x3A, "P1R16 HPL gain step (captured)");
    wr(1, 0x11, 0x3A, "P1R17 HPR gain step (captured)");
    wr(0, 0x00, 0x00, "select page 0");
    wr(0, 0x00, 0x00, "select page 0 (captured - repeated in the original)");
    wr(0, 0x41, 0x00, "R65 DAC L volume step (captured)");
    wr(0, 0x42, 0x00, "R66 DAC R volume step (captured)");
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x3B, 0x28, "P1R59 MIC_PGA_L step (captured)");
    wr(1, 0x3C, 0x28, "P1R60 MIC_PGA_R step (captured)");
    wr(0, 0x00, 0x00, "select page 0");
    wr(0, 0x00, 0x00, "select page 0 (captured - repeated in the original)");
    wr(0, 0x53, 0x00, "R83 (captured)");
    wr(0, 0x54, 0x00, "R84 (captured)");
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x3B, 0x28, "P1R59 (captured, repeated)");
    wr(1, 0x3C, 0x28, "P1R60 (captured, repeated)");
    wr(0, 0x00, 0x00, "select page 0");

    /* --- Page 8/9: DRC/biquad coefficient banks (ADC filter) --- */
    wr(8, 0x00, 0x08, "select page 8");
    wr(8, 0x18, 0x7F, "P8R24 biquad coeff (captured)");
    wr(8, 0x19, 0xFA, "P8R25 biquad coeff (captured)");
    wr(8, 0x1A, 0xDA, "P8R26 biquad coeff (captured)");
    wr(8, 0x1B, 0x00, "P8R27 biquad coeff (captured)");
    wr(8, 0x1C, 0x80, "P8R28 biquad coeff (captured)");
    wr(8, 0x1D, 0x05, "P8R29 biquad coeff (captured)");
    wr(8, 0x1E, 0x26, "P8R30 biquad coeff (captured)");
    wr(8, 0x1F, 0x00, "P8R31 biquad coeff (captured)");
    wr(8, 0x20, 0x7F, "P8R32 biquad coeff (captured)");
    wr(8, 0x21, 0xF5, "P8R33 biquad coeff (captured)");
    wr(8, 0x22, 0xB5, "P8R34 biquad coeff (captured)");
    wr(8, 0x23, 0x00, "P8R35 biquad coeff (captured)");
    wr(9, 0x00, 0x09, "select page 9");
    wr(9, 0x20, 0x7F, "P9R32 biquad coeff (captured)");
    wr(9, 0x21, 0xFA, "P9R33 biquad coeff (captured)");
    wr(9, 0x22, 0xDA, "P9R34 biquad coeff (captured)");
    wr(9, 0x23, 0x00, "P9R35 biquad coeff (captured)");
    wr(9, 0x24, 0x80, "P9R36 biquad coeff (captured)");
    wr(9, 0x25, 0x05, "P9R37 biquad coeff (captured)");
    wr(9, 0x26, 0x26, "P9R38 biquad coeff (captured)");
    wr(9, 0x27, 0x00, "P9R39 biquad coeff (captured)");
    wr(9, 0x28, 0x7F, "P9R40 biquad coeff (captured)");
    wr(9, 0x29, 0xF5, "P9R41 biquad coeff (captured)");
    wr(9, 0x2A, 0xB5, "P9R42 biquad coeff (captured)");
    wr(9, 0x2B, 0x00, "P9R43 biquad coeff (captured)");
    wr(8, 0x00, 0x08, "select page 8");
    wr(8, 0x01, 0x05, "P8R1 (captured, updated)");
    wr(0, 0x00, 0x00, "select page 0");

    /* --- Page 46: another DRC/biquad coefficient bank --- */
    wr(46, 0x00, 0x2E, "select page 46");
    wr(46, 0x1C, 0x04, "P46R28 biquad coeff (captured)");
    wr(46, 0x1D, 0x0E, "P46R29 biquad coeff (captured)");
    wr(46, 0x1E, 0xB5, "P46R30 biquad coeff (captured)");
    wr(46, 0x1F, 0x00, "P46R31 biquad coeff (captured)");
    wr(46, 0x20, 0x04, "P46R32 biquad coeff (captured)");
    wr(46, 0x21, 0x0E, "P46R33 biquad coeff (captured)");
    wr(46, 0x22, 0xB5, "P46R34 biquad coeff (captured)");
    wr(46, 0x23, 0x00, "P46R35 biquad coeff (captured)");
    wr(46, 0x24, 0x77, "P46R36 biquad coeff (captured)");
    wr(46, 0x25, 0xE2, "P46R37 biquad coeff (captured)");
    wr(46, 0x26, 0x93, "P46R38 biquad coeff (captured)");
    wr(46, 0x27, 0x00, "P46R39 biquad coeff (captured)");
    wr(46, 0x28, 0x04, "P46R40 biquad coeff (captured)");
    wr(46, 0x29, 0x0E, "P46R41 biquad coeff (captured)");
    wr(46, 0x2A, 0xB5, "P46R42 biquad coeff (captured)");
    wr(46, 0x2B, 0x00, "P46R43 biquad coeff (captured)");
    wr(46, 0x2C, 0x04, "P46R44 biquad coeff (captured)");
    wr(46, 0x2D, 0x0E, "P46R45 biquad coeff (captured)");
    wr(46, 0x2E, 0xB5, "P46R46 biquad coeff (captured)");
    wr(46, 0x2F, 0x00, "P46R47 biquad coeff (captured)");
    wr(46, 0x30, 0x77, "P46R48 biquad coeff (captured)");
    wr(46, 0x31, 0xE2, "P46R49 biquad coeff (captured)");
    wr(46, 0x32, 0x93, "P46R50 biquad coeff (captured)");
    wr(46, 0x33, 0x00, "P46R51 biquad coeff (captured)");
    wr(44, 0x00, 0x2C, "select page 44");
    wr(44, 0x01, 0x05, "P44R1 (captured, updated)");
    wr(0, 0x00, 0x00, "select page 0");

    /* --- Final HP gain step (captured) --- */
    wr(1, 0x00, 0x01, "select page 1");
    wr(1, 0x3B, 0x28, "P1R59 (captured, final)");
    wr(1, 0x3C, 0x28, "P1R60 (captured, final)");
    wr(0, 0x00, 0x00, "select page 0 (leave the codec on page 0)");

    debug_print_dec("aic3204: full sequence done for rate (0=96K,1=192K,2=48K) - clock "
                     "tree ready but BCLK/WCLK NOT yet driven (see "
                     "aic3204_start_bclk_wclk()), ADC/DAC still DOWN",
                     (uint32_t)rate);
}

/*
 * *** 05/08/2026, added alongside the WFM FERR/phase investigation ***
 * - the other half of the R27/R30 split pulled out of
 * aic3204_configure_rate() (see its own comment for the full
 * reasoning). This is the ONLY thing that actually makes the codec
 * start driving BCLK/WCLK as a master onto the shared pins - call it
 * as late as possible, i.e. AFTER sdr_rx_start()/gd32_i2s_stream_
 * start() have already put the GD32 side into a resynced, listening
 * state (see main.c's apply_demod_mode()), so the gap between "clock
 * goes live" and "GD32 is already waiting for it" is as small as this
 * project can currently make it - one two-byte I2C register write,
 * not the ~75-write, tens-of-milliseconds tail of
 * aic3204_configure_rate().
 *
 * R30 (BCLK N divider) is written FIRST, R27 (I2S format/master
 * enable) SECOND - the opposite order from before this split. This
 * way the rate-correct divider is already in place the instant the
 * pins go to output mode, instead of a brief moment of BCLK at
 * whatever divider value was left over from before (either the
 * previous rate, or 0 fresh out of aic3204_rate_switch_reset()'s
 * hardware nRESET).
 */
void aic3204_start_bclk_wclk(aic3204_rate_t rate)
{
    wr(0, 0x00, 0x00, "select page 0 (aic3204_start_bclk_wclk)");
    switch (rate) {
    case AIC3204_RATE_192K:
        wr(0, 0x1E, 0x87, "R30 BCLK N Divider, N=7 (192kHz: DAC_CLK/N = "
                           "43.008MHz/7 = 6.144MHz = 32*192kHz)");
        break;
    case AIC3204_RATE_48K:
        /* Added 01/09/2026 alongside the 48kHz rate option - see
         * aic3204_configure_rate()'s DAC-side comment. Same "BCLK =
         * 32*Fs" convention the other two rates already follow -
         * verified: 43.008MHz/28 = 1.536MHz = 32*48kHz exactly. */
        wr(0, 0x1E, 0x9C, "R30 BCLK N Divider, N=28 (48kHz: DAC_CLK/N = "
                           "43.008MHz/28 = 1.536MHz = 32*48kHz)");
        break;
    case AIC3204_RATE_96K:
    default:
        wr(0, 0x1E, 0x8E, "R30 BCLK N Divider, N=14 (96kHz: DAC_CLK/N = "
                           "43.008MHz/14 = 3.072MHz = 32*96kHz)");
        break;
    }
    wr(0, 0x1B, 0x0C, "R27 I2S format (captured) - BCLK/WCLK go live NOW");
    debug_print("aic3204: BCLK/WCLK now driven (R30 then R27) - GD32 side should "
                "already be resynced and listening at this point\n");
}

/*
 * Cold-boot entry point - now just the shared sequence above,
 * parameterized by `rate`, plus the power-up that used to be inlined
 * at the end of this function. Was hardcoded to AIC3204_RATE_96K with
 * no parameter until 01/09/2026 - see this function's header comment
 * in aic3204.h for why main() now needs to pick the boot rate itself
 * (matching whatever mode the saved config says to boot into, so WFM
 * gets a single genuine cold reset instead of a 96K cold boot
 * immediately followed by a second, warm switch into 192K).
 */
void aic3204_phase2_init(aic3204_rate_t rate)
{
    aic3204_rate_switch_reset();
    aic3204_configure_rate(rate);
    /* Cold boot's own GD32-side bring-up (gd32_i2s_init_slave()/
     * sdr_rx_init(), both called earlier in main.c's boot sequence)
     * already left SPI1/I2S1_ADD freshly enabled and listening before
     * this function even starts - so, same as a live switch's
     * apply_demod_mode() now, BCLK/WCLK only need to go live AFTER
     * that, which is exactly what this call does. Cold boot and every
     * live switch now go through this exact same two-call sequence, in
     * the exact same order, with no separate code path to drift out of
     * sync again. */
    aic3204_start_bclk_wclk(rate);
    aic3204_set_rate_power_up();
    debug_print_dec("aic3204: phase 2 complete - full sequence ported from a real I2C "
                     "capture of the original firmware. PLL sourced from MCLK (not BCLK - "
                     "corrected 01/09/2026), P=1/R=4/J=14/D=0 -> CODEC_CLKIN=86.016MHz -> Fs "
                     "exact on both ADC and DAC divider chains (0=96K,1=192K,2=48K)", (uint32_t)rate);
}


/*
 * --- Live rate switch (05/08/2026, for WFM's 192kHz reactivation;
 * *** REWRITTEN 05/08/2026, THIRD pass *** - split into separately
 * callable steps instead of one function, so main.c can sandwich the
 * GD32 I2S resync in between the reset and the register configuration
 * - see aic3204_rate_switch_reset()'s comment for the full reasoning
 * on why the ORDER of codec-reset vs GD32-resync turned out to matter,
 * not just whether the codec gets reset at all) ---
 *
 * HISTORY, briefly: the ORIGINAL live-switch code reprogrammed only
 * the handful of registers that differ between rates, leaving the PLL
 * and everything else untouched - measurably got the right sample
 * rate (confirmed on scope) but left SPI_STAT_FERR firing continuously
 * after every switch, on both RX and TX, forever. Wrapping the SAME
 * full register sequence cold boot uses (software reset first) helped
 * nothing. Upgrading that to a genuine hardware nRESET pulse - the
 * literal same reset cold boot performs - ALSO helped nothing: real
 * hardware logs showed FERR still firing at essentially the same rate
 * (hundreds per check window) even though the codec now reaches an
 * IDENTICAL state on both paths. That ruled the codec back out and
 * pointed at the GD32 side's enable ORDER relative to the codec's
 * clock output - see aic3204_rate_switch_reset()'s comment for the
 * theory and the reordering this led to.
 *
 * There is no longer a single "aic3204_set_rate_registers()" call for
 * this - main.c's apply_demod_mode() now calls, in order:
 *   1. aic3204_rate_switch_reset()   - codec falls silent, no BCLK/WS
 *   2. sdr_rx_reconfigure() / gd32_i2s_stream_reconfigure()
 *   3. sdr_rx_start() / gd32_i2s_stream_start() - GD32 I2S resync +
 *      DMA armed, all while the codec is STILL silent
 *   4. aic3204_configure_rate(rate)  - NOW bring BCLK/WS back, with
 *      the GD32 side already listening from the first real edge
 *   5. aic3204_set_rate_power_up()   - ADC/DAC on, real samples flow
 */

/*
 * Powers the DAC/ADC channels back UP - call this AFTER the RX/TX DMA
 * is already armed for the new rate (see main.c's apply_demod_mode()),
 * so real samples never arrive faster than they're drained.
 *
 * *** 05/08/2026, changed from a blind 50ms delay to the SAME ADC-
 * ready polling loop aic3204_phase2_init() always used at boot ***:
 * now that every live switch runs a genuine hardware reset on
 * EVERY live switch (not just once at cold boot), every switch is a
 * genuine fresh PLL lock, not a "re-lock from an already-running
 * state" - the earlier "first switch needs 50ms, later ones don't"
 * reasoning (see git history/transcript if this comment's prior
 * version is needed) no longer applies cleanly, since there's no
 * meaningful difference between "first" and "later" switches anymore
 * - they're all effectively first switches now. Polling the real ADC
 * Flag Register (0x24) instead of guessing a fixed delay is the more
 * correct match for that: it waits exactly as long as the hardware
 * actually needs, up to the same ~200ms ceiling phase2_init() already
 * used at boot, rather than hoping 50ms is enough on every switch
 * forever.
 */
void aic3204_set_rate_power_up(void)
{
    wr(0, 0x00, 0x00, "select page 0 (rate power-up)");
    wr(0, 0x3F, 0xD6, "R63 DAC power back UP, same value as phase2_init");
    wr(0, 0x51, 0xC0, "R81 ADC power back UP, same value as phase2_init");

    {
        uint8_t flag = 0x00;
        uint8_t left_up = 0, right_up = 0;
        uint8_t tries;

        for (tries = 0; tries < 20U; tries++) {
            if (aic3204_read_reg(0, 0x24, &flag)) {
                left_up  = (uint8_t)((flag & 0x40U) != 0U);
                right_up = (uint8_t)((flag & 0x04U) != 0U);
                if (left_up && right_up) {
                    break;
                }
            }
            delay_ms(10);
        }

        debug_print_hex32("aic3204: R36 ADC Flag Register raw (rate switch power-up)", flag);
        if (!left_up || !right_up) {
            debug_print("aic3204: *** rate-switch power-up: ADC did NOT confirm fully "
                        "powered up within ~200ms ***\n");
        }
    }

    debug_print("aic3204: ADC/DAC powered back up at the new rate\n");
}

/*
 * DAC digital output volume, both channels identically: Page 0,
 * Register 65 (0x41) = Left DAC Digital Volume, Register 66 (0x42) =
 * Right DAC Digital Volume. Per the TLV320AIC3204 datasheet this is a
 * plain signed 8-bit field in 0.5dB steps, range -63.5dB (0x81) to
 * +24dB (0x30) - register_value = round(db * 2). aic3204_phase2_init()
 * leaves both at 0x00 (0dB, unity) as part of its byte-exact captured
 * sequence; this is the first thing in this driver that actually
 * moves them away from that baseline on purpose.
 *
 * This is a DIGITAL gain stage after the DAC's own volume DSP, not
 * the analog HPL/HPR headphone driver gain (Page 1, R16/R17, also
 * captured fixed at 0x3A) - deliberately not exposing that one here:
 * it's an analog output stage bias/gain with a less-documented bit
 * layout in this codebase (see the "(captured)" comments on it in
 * aic3204_phase2_init() - nobody has decoded its exact dB mapping
 * yet), and getting it wrong risks driving the headphone amp harder
 * than intended. The digital DAC volume is the well-documented,
 * predictable, safe control to expose first; if +24dB of digital
 * headroom on top of the analog stage still isn't enough, that's the
 * next place to look.
 */
uint8_t aic3204_set_volume_db(float db)
{
    int32_t reg_val;
    uint8_t ok;

    if (db > 24.0f)  { db = 24.0f; }
    if (db < -63.5f) { db = -63.5f; }

    /* round-to-nearest 0.5dB step, then encode as the signed register
     * value (already in 0.5dB units: reg = db * 2). */
    reg_val = (int32_t)((db * 2.0f) + ((db >= 0.0f) ? 0.5f : -0.5f));
    if (reg_val > 48)   { reg_val = 48; }   /* +24.0dB */
    if (reg_val < -127) { reg_val = -127; } /* -63.5dB */

    ok  = aic3204_write_reg(0, 0x41, (uint8_t)(int8_t)reg_val); /* R65 DAC L volume */
    ok &= aic3204_write_reg(0, 0x42, (uint8_t)(int8_t)reg_val); /* R66 DAC R volume */

    if (reg_val < 0) {
        debug_print_dec("aic3204: DAC volume set, register value NEGATIVE, abs (0.5dB units)",
                         (uint32_t)(-reg_val));
    } else {
        debug_print_dec("aic3204: DAC volume set, register value (0.5dB units)",
                         (uint32_t)reg_val);
    }
    if (!ok) {
        debug_print("aic3204: *** DAC volume write with NO ACK ***\n");
    }
    return ok;
}

/*
 * MIC_PGA_L/R (Page 1, R59/R60) - the analog input gain stage BEFORE
 * the ADC, captured fixed at 0x28 (20dB, see aic3204_phase2_init())
 * until now. Exposed 31/07/2026 per the project owner, for the menu's
 * PGA tile - see main.c's ENCODER_TARGET_PGA.
 *
 * 7-bit field, 0.5dB/count: 0x00-0x5F (0-95) = 0-47.5dB, 0x60-0x7F
 * reserved per the TLV320AIC3204 datasheet - do not use. Bit 7 is
 * unused/reserved here too (always write 0 there), unlike the DAC
 * volume register above, which is a signed field - MIC_PGA gain is
 * unsigned, there's no "cut" direction, only 0dB (no boost) up to
 * 47.5dB.
 */
uint8_t aic3204_set_pga_gain_db(float db)
{
    int32_t reg_val;
    uint8_t ok;

    if (db > 47.5f) { db = 47.5f; }
    if (db < 0.0f)  { db = 0.0f; }

    /* round-to-nearest 0.5dB step (reg = db * 2, unsigned). */
    reg_val = (int32_t)((db * 2.0f) + 0.5f);
    if (reg_val > 95) { reg_val = 95; } /* 47.5dB - see the field-range note above */
    if (reg_val < 0)  { reg_val = 0; }

    ok  = aic3204_write_reg(1, 0x3BU, (uint8_t)reg_val); /* P1R59 MIC_PGA_L */
    ok &= aic3204_write_reg(1, 0x3CU, (uint8_t)reg_val); /* P1R60 MIC_PGA_R */

    debug_print_dec("aic3204: PGA gain set, register value (0.5dB units)", (uint32_t)reg_val);
    if (!ok) {
        debug_print("aic3204: *** PGA gain write with NO ACK ***\n");
    }
    return ok;
}

/*
 * Input impedance / Rin selector (Page 1, R52/R54/R55/R57) - added
 * 07/08/2026, per the project owner, after the datasheet's "Analog
 * PGA versus Input Configuration" table (2.3.2.1) turned up something
 * useful for the RF-level auto-AGC (see main.c's rf_agc_poll()):
 * choosing Rin=20k or 40k instead of the captured baseline's 10k
 * shifts the WHOLE PGA gain range down by 6dB/12dB respectively
 * (10k: 0..47.5dB, 20k: -6..41.5dB, 40k: -12..35.5dB, single-ended) -
 * a coarse attenuation option that reaches BELOW what the PGA's own
 * 0dB floor can do alone, for when even that isn't enough backoff on
 * a genuinely blowtorch-strong local signal.
 *
 * *** IMPORTANT UNVERIFIED ASSUMPTION *** - aic3204_phase2_init()'s
 * captured 10k values (P1R52/P1R54 = 0x10, P1R55/P1R57 = 0x04) are
 * real, sniffed-from-hardware bytes (see the comment right above
 * those wr() calls for the full field decode). The 20k/40k values
 * THIS function writes are NOT captured - they're inferred from the
 * datasheet's standard 2-bit field encoding (00=off, 01=10k, 10=20k,
 * 11=40k), applied to the SAME field position the captured 10k byte
 * already uses in each register (bits[5:4] for P1R52/P1R54, bits[3:2]
 * for P1R55/P1R57 - matching how 0x10 decodes to "bits[5:4]=01" and
 * 0x04 decodes to "bits[3:2]=01" respectively). This is a reasonable,
 * standard-encoding inference, not a guess pulled from nowhere - but
 * it has NOT been confirmed against a real I2C capture the way
 * everything else in this file has. Test on real hardware before
 * trusting it blind: tune to a known strong signal, step through
 * AIC3204_RIN_10K/20K/40K, and confirm both channels stay balanced
 * (equal I/Q amplitude - see the WFM gain-chain investigation for how
 * to check that from the raw min/max debug prints) and the effective
 * attenuation roughly matches the datasheet's 6dB/12dB steps. If a
 * real capture ever turns up different bytes for 20k/40k, THAT'S the
 * ground truth - fix this function's field values to match, not the
 * other way around.
 *
 * Left/right (I/Q) channels are ALWAYS switched together, same
 * atomic call - see the WFM gain-chain investigation for why an
 * asymmetric change here would introduce a real I/Q balance problem
 * (degraded SSB image rejection), not just a level mismatch.
 *
 * Unlike aic3204_set_pga_gain_db()'s soft-stepped gain register, this
 * reconnects the input MUX itself - the datasheet doesn't document
 * soft-stepping for these registers, so treat this as an abrupt,
 * audible-if-unmuted transition, NOT something to call on every fine
 * adjustment. Callers should mute (demod_am_reset_diag() or
 * demod_wfm_reset_diag(), whichever matches the active mode) around
 * any call to this - see rf_agc_poll()'s rin escalation for the
 * pattern.
 */
uint8_t aic3204_set_input_impedance(aic3204_rin_t level)
{
    uint8_t field = (uint8_t)((uint8_t)level + 1U); /* 01/10/11 = 10k/20k/40k */
    uint8_t left  = (uint8_t)(field << 4);  /* P1R52/P1R54 field is bits[5:4] */
    uint8_t right = (uint8_t)(field << 2);  /* P1R55/P1R57 field is bits[3:2] */
    uint8_t ok;

    ok  = aic3204_write_reg(1, 0x34U, left);  /* P1R52 IN2L -> LADC_P */
    ok &= aic3204_write_reg(1, 0x36U, left);  /* P1R54 IN2R -> LADC_M */
    ok &= aic3204_write_reg(1, 0x37U, right); /* P1R55 IN3R -> RADC_P */
    ok &= aic3204_write_reg(1, 0x39U, right); /* P1R57 IN3R -> RADC_M */

    debug_print_dec("aic3204: Rin set, level (0=10k/1=20k/2=40k)", (uint32_t)level);
    if (!ok) {
        debug_print("aic3204: *** Rin write with NO ACK ***\n");
    }
    return ok;
}

/* See this function's own comment in aic3204.h for the full "why" -
 * BENCH-TEST ONLY, single-ended override for HFDL testing with the
 * RF front-end disconnected. Only the M-terminal registers change;
 * P1R52/P1R55 (the P-terminal/hot-pin routing) are left untouched,
 * whatever aic3204_phase2_init() (or aic3204_set_input_impedance())
 * most recently set them to. */
uint8_t aic3204_set_input_single_ended_test(void)
{
    uint8_t ok;

    ok  = aic3204_write_reg(1, 0x36U, 0x40U); /* P1R54 CM1L -> LADC_M, 10k (was IN2R) */
    ok &= aic3204_write_reg(1, 0x39U, 0x40U); /* P1R57 CM1R -> RADC_M, 10k (was IN3x) */

    debug_print("aic3204: *** BENCH TEST *** single-ended M-terminal override applied (CM1L/CM1R at 10k)\n");
    if (!ok) {
        debug_print("aic3204: *** single-ended override write with NO ACK ***\n");
    }
    return ok;
}


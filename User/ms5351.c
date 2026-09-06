#include "ms5351.h"
#include "i2c_bitbang.h"
#include "rf_lpf.h"
#include "debug_uart.h"
#include "gd32f4xx.h" /* DWT/CoreDebug - low-band phase-alignment delay, see delay_us_dwt() */

/* Runtime crystal reference - see ms5351_get_xtal_hz()/
 * ms5351_set_xtal_hz()'s comment in ms5351.h for why this replaced
 * the old MS5351_XTAL_HZ #define. Every frac_divide() call below that
 * used to reference the macro now reads this instead. */
static uint32_t s_xtal_hz = MS5351_XTAL_HZ_DEFAULT;

uint32_t ms5351_get_xtal_hz(void)
{
    return s_xtal_hz;
}

void ms5351_set_xtal_hz(uint32_t xtal_hz)
{
    s_xtal_hz = xtal_hz;
}

/*
 * Si5351/MS5351 register map, the subset we use:
 *
 *   3        Output Enable Control (1 = disabled, active-low mask)
 *   16..18   CLK0..CLK2 Control (PDN, INT, PLL source, invert, src, drive)
 *   26..33   MSNA - PLLA feedback Multisynth (P1/P2/P3)
 *   34..41   MSNB - PLLB feedback Multisynth (P1/P2/P3)
 *   42..49   MS0  - CLK0 output Multisynth   (P1/P2/P3 + R div)
 *   50..57   MS1  - CLK1 output Multisynth
 *   58..65   MS2  - CLK2 output Multisynth
 *   165      CLK0 initial phase offset (7 bits, units of 1/(4*fVCO))
 *   166      CLK1 initial phase offset
 *   177      PLL soft reset (0x20 = PLLA, 0x80 = PLLB, 0xA0 = both)
 *   183      Crystal load capacitance
 *
 * Multisynth encoding (both feedback and output, fractional a + b/c):
 *   P1 = 128*a + floor(128*b/c) - 512
 *   P2 = 128*b - c*floor(128*b/c)
 *   P3 = c
 * Integer value N is the special case b=0: P1 = 128*N - 512, P2 = 0,
 * P3 = 1. Register packing for a block starting at base register B:
 *   B+0 = P3[15:8]   B+1 = P3[7:0]
 *   B+2 = P1[17:16]  B+3 = P1[15:8]  B+4 = P1[7:0]
 *   B+5 = P3[19:16]<<4 | P2[19:16]
 *   B+6 = P2[15:8]   B+7 = P2[7:0]
 * (Output Multisynths additionally carry the R divider in B+2 bits
 * 6:4; we always use R = /1, so those bits stay 0.)
 */

#define REG_OE_CTRL      3U
#define REG_CLK0_CTRL    16U
#define REG_CLK2_CTRL    18U
#define REG_MSNB_BASE    34U
#define REG_MS0_BASE     42U
#define REG_MS1_BASE     50U
#define REG_CLK0_PHASE   165U
#define REG_CLK1_PHASE   166U
#define REG_PLL_RESET    177U

#define PLL_RESET_B      0x80U
#define PLL_RESET_AB     0xA0U

/* CLK control value used by the capture for both quadrature outputs:
 * powered up, fractional allowed, source = PLLB, output = Multisynth,
 * 8mA drive. */
#define CLKCTRL_QUAD_PLLB 0x2FU

/* Fractional denominator used by the original firmware (20-bit max).
 * Verified: with this c, floor arithmetic reproduces the captured
 * PLLB registers for 90.800MHz bit-exactly. */
#define FRAC_C           0xFFFFFUL

/* VCO window. Datasheet says 600-900MHz; the capture itself runs PLLB
 * at 908MHz, so this clone demonstrably tolerates a little more. We
 * keep the ceiling just above the captured point so that
 * ms5351_set_lo_freq(90800000) selects divider 10 and reproduces the
 * capture, without wandering far off-datasheet in the general case. */
#define VCO_MIN_HZ       600000000UL
#define VCO_MAX_HZ       908000000UL

/* Phase-offset register is 7 bits, so the (even) divider it must hold
 * is capped here. */
#define DIV_MAX          126U
#define DIV_MIN          4U

static uint32_t s_last_div = 0; /* 0 = never tuned -> force PLL reset */

/* ---- low-level helpers ------------------------------------------- */

static uint8_t wr_buf(const uint8_t *buf, uint8_t len, const char *what)
{
    if (!i2c_write(MS5351_ADDR, buf, len)) {
        debug_print("ms5351: NACK writing ");
        debug_print(what);
        debug_print("\n");
        return 0;
    }
    return 1;
}

static uint8_t wr_reg(uint8_t reg, uint8_t val, const char *what)
{
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = val;
    return wr_buf(buf, 2, what);
}

/* Write one Multisynth P1/P2/P3 block at base register `base`. */
static uint8_t wr_ms(uint8_t base, uint32_t p1, uint32_t p2, uint32_t p3,
                     const char *what)
{
    uint8_t buf[9];
    buf[0] = base;
    buf[1] = (uint8_t)((p3 >> 8) & 0xFFU);
    buf[2] = (uint8_t)(p3 & 0xFFU);
    buf[3] = (uint8_t)((p1 >> 16) & 0x03U);
    buf[4] = (uint8_t)((p1 >> 8) & 0xFFU);
    buf[5] = (uint8_t)(p1 & 0xFFU);
    buf[6] = (uint8_t)(((p3 >> 12) & 0xF0U) | ((p2 >> 16) & 0x0FU));
    buf[7] = (uint8_t)((p2 >> 8) & 0xFFU);
    buf[8] = (uint8_t)(p2 & 0xFFU);
    return wr_buf(buf, 9, what);
}

/* ---- captured startup block --------------------------------------- */

uint8_t ms5351_init(void)
{
    uint8_t ok = 1;

    debug_print("\nms5351: base init (byte-exact replay of captured startup)\n");

    /* 1. All outputs disabled while we reconfigure. */
    ok &= wr_reg(REG_OE_CTRL, 0xFF, "reg3 OE=all off");

    /* 2. CLK0..CLK2 powered down (captured single burst 16,17,18). */
    {
        static const uint8_t pdn[4] = { REG_CLK0_CTRL, 0x80, 0x80, 0x80 };
        ok &= wr_buf(pdn, 4, "regs16-18 CLK0-2 power down");
    }

    /* 3. Crystal load capacitance = 8pF (captured 0x80). */
    ok &= wr_reg(183U, 0x80, "reg183 XTAL load 8pF");

    /* 4. PLLA = x32 integer -> 26MHz * 32 = 832MHz. P1 = 128*32-512
     *    = 3584, P2 = 0, P3 = 1. Fixed forever; only CLK2 uses it. */
    ok &= wr_ms(26U, 3584UL, 0UL, 1UL, "MSNA PLLA x32 (832MHz)");

    /* 5. Reset both PLLs (captured 0xA0). */
    ok &= wr_reg(REG_PLL_RESET, PLL_RESET_AB, "reg177 PLL reset A+B");

    /* 6. MS2 = /104 -> 832/104 = 8.000MHz on CLK2. P1 = 128*104-512
     *    = 12800, integer. */
    ok &= wr_ms(58U, 12800UL, 0UL, 1UL, "MS2 /104 (8MHz)");

    /* 7. CLK2 control = 0xCC: integer mode, PLLA, Multisynth source,
     *    2mA - and PDN bit SET. The original firmware leaves this
     *    output fully configured but off; replicated verbatim. */
    ok &= wr_reg(REG_CLK2_CTRL, 0xCC, "reg18 CLK2 cfg (powered down)");

    /* 8. Enable CLK0+CLK1 outputs only (0xFC: bits 0,1 low). They are
     *    still powered down at the CLK-control level until the first
     *    tune, so nothing toggles yet. */
    ok &= wr_reg(REG_OE_CTRL, 0xFC, "reg3 OE=CLK0|CLK1");

    s_last_div = 0; /* next tune must latch phases with a PLL reset */

    debug_print_dec("ms5351: base init done, all ACKed", ok);
    return ok;
}

/* ---- captured tune block (LO = 90.800MHz quadrature) --------------- */

uint8_t ms5351_tune_captured(void)
{
    uint8_t ok = 1;

    debug_print("ms5351: captured tune replay - LO 90.800MHz quadrature\n");

    /* PLLB = 26MHz * (34 + 967915/1048575) = 908.000MHz.
     * Captured bytes: FF FF 00 0F 76 F2 75 F6. */
    ok &= wr_ms(REG_MSNB_BASE, 3958UL, 161270UL, FRAC_C, "MSNB PLLB 908MHz");

    /* MS0 = /10 integer (P1=768), CLK0 up from PLLB, phase = 10 (90deg). */
    ok &= wr_ms(REG_MS0_BASE, 768UL, 0UL, 1UL, "MS0 /10");
    ok &= wr_reg(REG_CLK0_CTRL, CLKCTRL_QUAD_PLLB, "reg16 CLK0 on/PLLB");
    ok &= wr_reg(REG_CLK0_PHASE, 10U, "reg165 CLK0 phase=10");

    /* MS1 = /10 integer, CLK1 up from PLLB, phase = 0. */
    ok &= wr_ms(REG_MS1_BASE, 768UL, 0UL, 1UL, "MS1 /10");
    ok &= wr_reg(REG_CLK0_CTRL + 1U, CLKCTRL_QUAD_PLLB, "reg17 CLK1 on/PLLB");
    ok &= wr_reg(REG_CLK1_PHASE, 0U, "reg166 CLK1 phase=0");

    /* PLLB soft reset latches the new phase relationship. */
    ok &= wr_reg(REG_PLL_RESET, PLL_RESET_B, "reg177 PLLB reset");

    s_last_div = 10U;

    /* Keep the front-end low-pass bank tracking the LO. */
    rf_lpf_select(MS5351_CAPTURED_LO_HZ);

    debug_print_dec("ms5351: captured tune done, all ACKed", ok);
    return ok;
}

/* ---- low-band quadrature (below the phase-offset register floor) -- */

/*
 * PORTED 31/07/2026 from a well-known, widely-used SI5351 driver
 * (JF3HZB / T.UEBO, "Get IQ local signal down to 100kHz" - the
 * project owner supplied it as a reference: VFOsys2_23.ino /
 * si5351.cpp/.h) - NOT a byte-exact capture of THIS board like the
 * rest of this file. It's a trusted, community-vetted technique
 * (used in several published SDR designs) ported to our 26MHz
 * crystal and PLLB allocation (the reference uses a 25MHz crystal and
 * PLLA for its quadrature pair - functionally symmetric substitutions,
 * not a design change).
 *
 * WHY THE REGISTER-OFFSET TRICK (above, ms5351_set_lo_freq's main
 * path) CAN'T GO THIS LOW: it needs fVCO = freq * div >= VCO_MIN_HZ
 * with div an EVEN INTEGER the 7-bit CLKx_PHOFF register can hold
 * (<=126) - below ~4.8MHz even div=126 can't push fVCO up to
 * VCO_MIN_HZ. This low-band technique sidesteps that ceiling entirely
 * by using a genuinely FRACTIONAL output divider (not just an integer
 * with phase-offset registers) and establishing the 90-degree
 * relationship a completely different way:
 *
 *   1. Pick fVCO = freq * MULT, where MULT (300/600/1800, by zone -
 *      see LOWF_ZONE_*) is chosen so fVCO stays in a workable range
 *      across a wide swath of frequency without needing to touch the
 *      output divider ratio again until the zone changes.
 *   2. Set MS0 AND MS1 to the IDENTICAL divider for (freq - df) - a
 *      few Hz below the real target (LOWF_PHASE_DF_HZ) - then reset
 *      PLLB. Both channels start out perfectly IN PHASE (0 degrees
 *      apart), running slightly slow.
 *   3. Immediately retune MS0 (CLK0 only) up to the CORRECT divider
 *      for the real freq - CLK0 jumps to the right frequency, CLK1
 *      keeps running df Hz slow.
 *   4. Wait exactly LOWF_PHASE_SHIFT_US - during this window CLK1
 *      keeps falling behind CLK0 at a rate of df cycles/second. The
 *      wait time is chosen so the accumulated lag comes out to EXACTLY
 *      90 degrees: phase_lag = 2*pi*df*t, solved for t at phase_lag=
 *      pi/2 (90 degrees) gives t = 1/(4*df) - note the pi cancels
 *      completely, so this works out to a clean constant (62500us for
 *      df=4Hz) that DOESN'T depend on the target frequency at all.
 *   5. Retune MS1 (CLK1) up to the same correct divider CLK0 already
 *      has. CLK1 jumps to the right frequency too, but by now carries
 *      exactly 90 degrees of lag relative to CLK0 - and since both
 *      channels share the IDENTICAL, unchanging divider ratio from
 *      here on, that 90-degree relationship survives any further
 *      retuning THAT ONLY MOVES PLLB'S FEEDBACK (fVCO) - which is
 *      exactly what normal tuning within a zone does (see the "always"
 *      block below, and why steps 1-5 only run when the ZONE changes,
 *      not on every retune).
 *
 *   *** 01/09/2026: steps 3+5 SWAPPED to CLK1-leads, THEN SWAPPED BACK
 *   to CLK0-leads later the same day *** - briefly changed to speeding
 *   up CLK1/MS1 first (based on re-decoding the Si5351/MS5351 CLKx_
 *   PHOFF register as a time DELAY, which would make CLK1 lead in the
 *   high-band path instead of CLK0 as originally assumed) - but that
 *   exact deduction was later found backwards for lo_gen_gd32.c's own
 *   PA6/PA7 quadrature generator (below 300kHz), confirmed by the
 *   project owner with a real external signal generator: CLK0 needed
 *   to lead there after all. Since both swaps came from the identical
 *   reasoning, this one was reverted too - back to ITS original
 *   convention (CLK0 leads, as written below: MS0/CLK0 sped up first
 *   in step 3, MS1/CLK1 catches up in step 5). NOT yet independently
 *   re-confirmed with a generator on this specific low-band path -
 *   see ms5351_set_lo_freq_lowband()'s own step-3 comment for the
 *   fuller caveat.
 *
 * PLL FEEDBACK MULTIPLIER: the SI5351 datasheet's nominal range for
 * the PLL feedback multiplier (fVCO/fXTAL) is roughly 15-90. At our
 * 26MHz crystal, every one of the 10 zones below keeps this between
 * ~23 and ~35 across its own range - comfortably nominal, verified
 * numerically for all 10 zones (see LOWF_ZONE_01_HZ..LOWF_ZONE_10_HZ's
 * own derivation history). This used to be a real WARNING here - the
 * OLD 3-zone table (mult=300/600/1800) let the multiplier dip as low
 * as ~6.9 at LOWF_FLOOR_HZ, and - more seriously, found by the project
 * owner via real hardware testing 01/09/2026 - let fVCO itself run
 * completely outside this MS5351 clone's actual VCO range (600-
 * 908MHz, see VCO_MIN_HZ/VCO_MAX_HZ) for large stretches of the
 * claimed zone ranges (e.g. zone A's own mult=300 only kept fVCO in
 * range for roughly 2.0-3.02MHz, NOT the whole 1.5MHz-4.8MHz it
 * claimed to cover) - with NO bounds checking anywhere catching this,
 * so the PLL just got fed a bogus fVCO and produced no usable output.
 * Symptom on real hardware: a generator tone at 4.5MHz (squarely in
 * the old zone A's broken range) showed no signal at all, while
 * nearby 2MHz (in zone A's own narrow actually-valid slice) worked
 * fine. Rederived from scratch as 10 zones, each with its OWN
 * multiplier chosen so fVCO stays in [VCO_MIN_HZ, VCO_MAX_HZ] across
 * that zone's entire range, tiling LOWF_FLOOR_HZ..LOWF_HANDOFF_HZ
 * (100kHz-4.8MHz) with no gaps.
 *
 * BLOCKING DELAY: LOWF_PHASE_SHIFT_US (62.5ms) blocks the whole
 * system - main loop, touch, encoder polling, everything - since this
 * driver has no interrupt-driven timing. This ONLY happens when
 * crossing a zone boundary, not on every retune within a zone -
 * tuning around inside one zone is exactly as responsive as the
 * existing high-band path. With 10 zones now (up from 3), there are
 * proportionally more crossing points where a ~62ms stutter can be
 * noticed - still only right at those specific boundaries, never
 * mid-zone, but flagging that this happens more often now than it
 * used to.
 */
#define LOWF_ZONE_01_HZ    100000UL /* floor - see LOWF_FLOOR_HZ below */
#define LOWF_ZONE_02_HZ    115831UL
#define LOWF_ZONE_03_HZ    175285UL
#define LOWF_ZONE_04_HZ    265252UL
#define LOWF_ZONE_05_HZ    401338UL
#define LOWF_ZONE_06_HZ    607288UL
#define LOWF_ZONE_07_HZ    918837UL
#define LOWF_ZONE_08_HZ   1388889UL
#define LOWF_ZONE_09_HZ   2097903UL
#define LOWF_ZONE_10_HZ   3174604UL /* up to LOWF_HANDOFF_HZ (4.8MHz) */
#define LOWF_FLOOR_HZ      100000UL /* below this, refuse - see the PLL
                                      * FEEDBACK MULTIPLIER WARNING above */
#define LOWF_ZONE_01_MULT     7839UL
#define LOWF_ZONE_02_MULT     5180UL
#define LOWF_ZONE_03_MULT     3423UL
#define LOWF_ZONE_04_MULT     2262UL
#define LOWF_ZONE_05_MULT     1495UL
#define LOWF_ZONE_06_MULT      988UL
#define LOWF_ZONE_07_MULT      653UL
#define LOWF_ZONE_08_MULT      432UL
#define LOWF_ZONE_09_MULT      286UL
#define LOWF_ZONE_10_MULT      189UL
#define LOWF_PHASE_DF_HZ         4UL /* Hz - matches the reference's df */
/* = 1e6 / (4*LOWF_PHASE_DF_HZ), see step 4 above for the derivation
 * (the pi cancels out - this is exact, not a trig approximation). */
#define LOWF_PHASE_SHIFT_US  62500UL

/* 0 = no low-band zone established yet (forces the phase-alignment
 * maneuver on the next low-band call); 1-10 = one of the 10 zones,
 * matching LOWF_ZONE_01_HZ..LOWF_ZONE_10_HZ's ordering (zone 1 =
 * lowest frequencies, zone 10 = just below LOWF_HANDOFF_HZ).
 * Deliberately invalidated (set to 0) by the HIGH-band path too - see
 * ms5351_set_lo_freq() - so crossing back down after having been in
 * high-band always redoes the maneuver rather than trusting stale
 * state. */
static uint8_t s_last_lowf_zone = 0U;

/* DWT-cycle-counter busy-wait - genuine hardware-timer precision (this
 * project already relies on DWT->CYCCNT for cycle-accurate timing
 * elsewhere, e.g. demod_am.c's ISR profiling) rather than a NOP-loop
 * guess, which would be a poor way to hit a specific 62.5ms window.
 * Blocking - see this section's BLOCKING DELAY note above for why
 * that's acceptable here. */
static void delay_us_dwt(uint32_t us)
{
    uint32_t start;
    uint32_t cycles = us * (SystemCoreClock / 1000000UL);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
        /* busy-wait */
    }
}

/* Shared fractional-divider math (a + b/c encoded as P1/P2, c=FRAC_C
 * fixed) - factored out here since the low-band maneuver needs it
 * three times (PLLB feedback, MS0/MS1 at freq-df, MS0/MS1 at freq).
 * Identical arithmetic to what ms5351_set_lo_freq()'s high-band path
 * already does inline; not refactored to share this helper, to avoid
 * touching that proven-working code path for this change. */
static void frac_divide(uint64_t num, uint32_t denom, uint32_t *p1, uint32_t *p2)
{
    uint32_t a, b, f128;
    uint64_t rem;

    a = (uint32_t)(num / denom);
    rem = num % denom;
    b = (uint32_t)((rem * FRAC_C) / denom);
    f128 = (uint32_t)(((uint64_t)b * 128U) / FRAC_C);
    *p1 = 128U * a + f128 - 512U;
    *p2 = 128U * b - (uint32_t)FRAC_C * f128;
}

static uint8_t ms5351_set_lo_freq_lowband(uint32_t freq_hz)
{
    uint8_t  zone;
    uint32_t mult;
    uint64_t fvco;
    uint32_t pll_p1, pll_p2;
    uint8_t  ok = 1;

    if (freq_hz < LOWF_FLOOR_HZ) {
        debug_print_dec("ms5351: LO below the low-band floor, Hz", freq_hz);
        return 0;
    }

    if (freq_hz >= LOWF_ZONE_10_HZ) {
        zone = 10U; mult = LOWF_ZONE_10_MULT;
    } else if (freq_hz >= LOWF_ZONE_09_HZ) {
        zone = 9U; mult = LOWF_ZONE_09_MULT;
    } else if (freq_hz >= LOWF_ZONE_08_HZ) {
        zone = 8U; mult = LOWF_ZONE_08_MULT;
    } else if (freq_hz >= LOWF_ZONE_07_HZ) {
        zone = 7U; mult = LOWF_ZONE_07_MULT;
    } else if (freq_hz >= LOWF_ZONE_06_HZ) {
        zone = 6U; mult = LOWF_ZONE_06_MULT;
    } else if (freq_hz >= LOWF_ZONE_05_HZ) {
        zone = 5U; mult = LOWF_ZONE_05_MULT;
    } else if (freq_hz >= LOWF_ZONE_04_HZ) {
        zone = 4U; mult = LOWF_ZONE_04_MULT;
    } else if (freq_hz >= LOWF_ZONE_03_HZ) {
        zone = 3U; mult = LOWF_ZONE_03_MULT;
    } else if (freq_hz >= LOWF_ZONE_02_HZ) {
        zone = 2U; mult = LOWF_ZONE_02_MULT;
    } else {
        zone = 1U; mult = LOWF_ZONE_01_MULT;
    }

    fvco = (uint64_t)freq_hz * mult;

    debug_print_dec("ms5351: tune Hz (low-band)", freq_hz);
    debug_print_dec("ms5351: low-band zone", zone);

    if (zone != s_last_lowf_zone) {
        /* --- establish the 90-degree relationship, see the big
         * comment above this function for the full derivation --- */
        uint32_t freq_minus_df = freq_hz - (uint32_t)LOWF_PHASE_DF_HZ;
        uint32_t ms_p1, ms_p2;

        ok &= wr_reg(REG_CLK0_CTRL, CLKCTRL_QUAD_PLLB, "reg16 CLK0 on/PLLB (low-band)");
        ok &= wr_reg(REG_CLK0_CTRL + 1U, CLKCTRL_QUAD_PLLB, "reg17 CLK1 on/PLLB (low-band)");
        /* Re-assert the output-enable mask here too - same gap-fix
         * and same "s_last_lowf_zone gets force-invalidated by
         * ms5351_lo_disable(), so THIS is where a prior disable
         * actually gets undone" reasoning as ms5351_set_lo_freq()'s
         * own high-band reprogram branch - see its comment. Without
         * this, retuning back into the low band after a
         * ms5351_lo_disable() would silently stay Hi-Z even though
         * every other register looks correctly reprogrammed. */
        ok &= wr_reg(REG_OE_CTRL, 0xFCU, "reg3 OE=CLK0|CLK1 (re-assert, low-band)");

        /* Step 1: PLLB feedback for fVCO. The "always" block below
         * redoes this identically right after we return - a harmless
         * no-op THIS call, and the real fine-retune path on every
         * subsequent call within this zone. */
        frac_divide(fvco, s_xtal_hz, &pll_p1, &pll_p2);
        ok &= wr_ms(REG_MSNB_BASE, pll_p1, pll_p2, FRAC_C, "MSNB PLLB fVCO (low-band align)");

        /* Step 2: MS0=MS1=divider for (freq-df), phase registers
         * zeroed, PLLB reset - CLK0/CLK1 start perfectly in phase,
         * running df Hz slow. */
        frac_divide(fvco, freq_minus_df, &ms_p1, &ms_p2);
        ok &= wr_ms(REG_MS0_BASE, ms_p1, ms_p2, FRAC_C, "MS0 (freq-df), phase align");
        ok &= wr_ms(REG_MS1_BASE, ms_p1, ms_p2, FRAC_C, "MS1 (freq-df), phase align");
        ok &= wr_reg(REG_CLK0_PHASE, 0U, "reg165 CLK0 phase=0 (low-band)");
        ok &= wr_reg(REG_CLK1_PHASE, 0U, "reg166 CLK1 phase=0 (low-band)");
        ok &= wr_reg(REG_PLL_RESET, PLL_RESET_B, "reg177 PLLB reset (low-band align)");

        /* Step 3: MS1 (CLK1 only) up to the real freq - CLK1 jumps,
         * CLK0 keeps running slow.
         *
         * *** 01/09/2026, SWAPPED BACK to MS1/CLK1 AGAIN, same day -
         * real hardware A/B test, per the project owner *** - this
         * function has now been swapped THREE times in one day; see
         * its own header comment for the fuller history. Short
         * version: it was swapped to CLK1-leads (this state) based on
         * the PHOFF-register deduction; then swapped BACK to CLK0-
         * leads by analogy with lo_gen_gd32.c's own real-generator
         * finding (a DIFFERENT, unrelated mechanism - a register-
         * offset scheme there, a timed divider-speedup race here);
         * then swapped back to THIS (CLK1-leads) a second time after
         * the project owner directly A/B-tested both at 2MHz and
         * found CLK0-leads produced a STRONGER wrong-sideband image
         * than CLK1-leads did. The analogy with lo_gen_gd32.c was the
         * mistake, not the original PHOFF-based reasoning - two
         * genuinely different physical mechanisms (a register write
         * vs. a timing race between two independent MultiSynths) have
         * no real reason to share one "which clock leads" convention,
         * and assuming they must was what caused this back-and-forth.
         * NOT a fully confirmed fix even now - the project owner's
         * own A/B test was at 2MHz only; still investigating a
         * SEPARATE, likely unrelated issue where 4.5MHz shows no
         * signal at all (not a sideband problem - see this function's
         * own notes elsewhere, or ask before assuming this phase
         * relationship explains that too). */
        frac_divide(fvco, freq_hz, &ms_p1, &ms_p2);
        ok &= wr_ms(REG_MS1_BASE, ms_p1, ms_p2, FRAC_C, "MS1 -> freq (low-band align)");

        /* Step 4: wait for exactly 90 degrees of accumulated lag. */
        delay_us_dwt(LOWF_PHASE_SHIFT_US);

        /* Step 5: MS0 (CLK0) catches up to the real freq too - now
         * carrying the 90-degree lag forward (see step 3's comment
         * for why CLK0, not CLK1, is the one left behind now). */
        ok &= wr_ms(REG_MS0_BASE, ms_p1, ms_p2, FRAC_C, "MS0 -> freq (low-band align)");

        if (ok) {
            s_last_lowf_zone = zone;
        }
    }

    /* --- always: fine-retune PLLB feedback for the exact target
     * frequency within the current zone (MS0/MS1's divider ratio,
     * and therefore the 90-degree relationship established above,
     * stays untouched). */
    frac_divide(fvco, s_xtal_hz, &pll_p1, &pll_p2);
    ok &= wr_ms(REG_MSNB_BASE, pll_p1, pll_p2, FRAC_C, "MSNB PLLB (low-band retune)");

    if (ok) {
        s_last_div = 0U; /* invalidate the high-band path's state - see
                           * ms5351_set_lo_freq()'s comment on why. */
        rf_lpf_select(freq_hz);
    }

    return ok;
}

/* ---- general quadrature tune -------------------------------------- */

/*
 * DISPATCHES to the low-band technique above (ms5351_set_lo_freq_lowband())
 * for freq_hz < LOWF_HANDOFF_HZ - added 31/07/2026, see that function's
 * big comment for how it works and its caveats. LOWF_HANDOFF_HZ (4.8MHz,
 * the phase-offset register scheme's own documented floor) keeps this
 * a clean, non-overlapping split: nothing about the high-band path
 * below changed.
 */
#define LOWF_HANDOFF_HZ 4800000UL

uint8_t ms5351_set_lo_freq(uint32_t freq_hz)
{
    uint32_t div;
    uint64_t fvco;
    uint32_t a, b;
    uint64_t rem;
    uint32_t f128;      /* floor(128*b/c) */
    uint32_t pll_p1, pll_p2;
    uint32_t ms_p1;
    uint8_t  ok = 1;

    if (freq_hz == 0U) {
        return 0;
    }

    if (freq_hz < LOWF_HANDOFF_HZ) {
        return ms5351_set_lo_freq_lowband(freq_hz);
    }
    s_last_lowf_zone = 0U; /* invalidate the low-band path's state - see
                             * ms5351_set_lo_freq_lowband()'s comment on why. */

    /* Largest even divider with fVCO <= VCO_MAX, clamped to what the
     * 7-bit phase register can hold. */
    div = (uint32_t)(VCO_MAX_HZ / freq_hz);
    div &= ~(uint32_t)1U;           /* force even (round down) */
    if (div > DIV_MAX) {
        div = DIV_MAX;
    }
    if (div < DIV_MIN) {
        debug_print_dec("ms5351: LO too high for quadrature, Hz", freq_hz);
        return 0;
    }

    fvco = (uint64_t)freq_hz * div;
    if (fvco < VCO_MIN_HZ) {
        /* Below ~4.8MHz even div=126 can't reach the VCO window; the
         * phase-offset quadrature scheme runs out of road here. */
        debug_print_dec("ms5351: LO too low for quadrature, Hz", freq_hz);
        return 0;
    }

    /* PLLB feedback: fvco / xtal = a + b/c, c = FRAC_C, floor
     * arithmetic throughout (verified bit-exact against the capture
     * for 90.800MHz -> div 10). */
    a   = (uint32_t)(fvco / s_xtal_hz);
    rem = fvco % s_xtal_hz;
    b   = (uint32_t)((rem * FRAC_C) / s_xtal_hz);

    f128   = (uint32_t)(((uint64_t)b * 128U) / FRAC_C);
    pll_p1 = 128U * a + f128 - 512U;
    pll_p2 = 128U * b - (uint32_t)FRAC_C * f128;

    ms_p1 = 128U * div - 512U;

    debug_print_dec("ms5351: tune Hz", freq_hz);
    debug_print_dec("ms5351: divider", div);
    debug_print_hex32("ms5351: PLLB P1", pll_p1);
    debug_print_hex32("ms5351: PLLB P2", pll_p2);

    ok &= wr_ms(REG_MSNB_BASE, pll_p1, pll_p2, FRAC_C, "MSNB PLLB");

    if (div != s_last_div) {
        /* Divider (and therefore phase offset) changed: reprogram the
         * output Multisynths and phases, then reset PLLB to latch the
         * 90-degree relationship. Kept out of the common retune path
         * because the PLL reset produces an audible click. This is
         * ALSO the natural place to re-assert the output-enable mask
         * (added 01/09/2026, alongside ms5351_lo_disable() below,
         * fixing a real gap): s_last_div gets force-invalidated to 0
         * both by ms5351_lo_disable() and by the high-band/low-band
         * crossover paths precisely so the NEXT call always lands
         * here - but before this fix, nothing in this branch actually
         * touched REG_OE_CTRL, so a prior ms5351_lo_disable() (which
         * DOES mask the outputs off in reg3) was never actually
         * undone by a plain ms5351_set_lo_freq() call, contradicting
         * ms5351_lo_disable()'s own header comment ("no separate
         * re-enable call needed"). Writing it unconditionally here
         * every time the full reprogram runs makes that claim true
         * without adding any new state to track. */
        ok &= wr_ms(REG_MS0_BASE, ms_p1, 0UL, 1UL, "MS0");
        ok &= wr_reg(REG_CLK0_CTRL, CLKCTRL_QUAD_PLLB, "reg16 CLK0 on/PLLB");
        ok &= wr_reg(REG_CLK0_PHASE, (uint8_t)div, "reg165 CLK0 phase");

        ok &= wr_ms(REG_MS1_BASE, ms_p1, 0UL, 1UL, "MS1");
        ok &= wr_reg(REG_CLK0_CTRL + 1U, CLKCTRL_QUAD_PLLB, "reg17 CLK1 on/PLLB");
        ok &= wr_reg(REG_CLK1_PHASE, 0U, "reg166 CLK1 phase=0");

        ok &= wr_reg(REG_OE_CTRL, 0xFCU, "reg3 OE=CLK0|CLK1 (re-assert)");
        ok &= wr_reg(REG_PLL_RESET, PLL_RESET_B, "reg177 PLLB reset");

        if (ok) {
            s_last_div = div;
        }
    }

    if (ok) {
        /* Keep the front-end low-pass bank tracking the LO. */
        rf_lpf_select(freq_hz);
    }

    return ok;
}

/*
 * *** IMPLEMENTED 01/09/2026 - was declared in ms5351.h (13/08/2026)
 * but never actually written here until now *** - needed for real
 * this time by lo_gen_gd32.c: PA6/PA7 share the same physical nets as
 * CLK0/CLK1 (through 100-ohm series resistors - see main.c's boot-time
 * Hi-Z comment), so generating the LO via the GD32's own timer instead
 * (below LO_GEN_CROSSOVER_HZ - see lo_gen_gd32.h) requires the MS5351
 * side to be a GENUINE Hi-Z, not just quiet - two drivers on the same
 * node otherwise. Bench-test signal injection (this function's
 * original motivating use case, per its header comment) has the exact
 * same real requirement, which is presumably why it was already
 * documented even before anything called it.
 *
 * OE mask off (reg3) AND PDN bit set (reg16/17) - same "belt and
 * braces" pairing ms5351_init()'s own power-down sequence uses.
 * Invalidates BOTH s_last_div and s_last_lowf_zone (whichever path
 * the next ms5351_set_lo_freq() call takes - high-band or low-band -
 * is what actually re-asserts OE_CTRL again, per the gap-fix
 * described in both of their "div/zone changed" branches above) so
 * that next call is unconditionally a full reprogram, matching this
 * function's own header comment ("no separate re-enable call
 * needed").
 */
uint8_t ms5351_lo_disable(void)
{
    uint8_t ok = 1;

    ok &= wr_reg(REG_OE_CTRL, 0xFFU, "reg3 OE=all off (lo_disable)");
    ok &= wr_reg(REG_CLK0_CTRL, 0x80U, "reg16 CLK0 power down (lo_disable)");
    ok &= wr_reg(REG_CLK0_CTRL + 1U, 0x80U, "reg17 CLK1 power down (lo_disable)");

    s_last_div = 0U;
    s_last_lowf_zone = 0U;

    return ok;
}

/* ---- CLK2 auxiliary 8MHz ------------------------------------------ */

uint8_t ms5351_clk2_8mhz(uint8_t on)
{
    uint8_t ok = 1;

    if (on) {
        /* Captured value 0xCC with the PDN bit cleared, then open its
         * gate in the OE mask (clear bit 2). */
        ok &= wr_reg(REG_CLK2_CTRL, 0x4C, "reg18 CLK2 power up");
        ok &= wr_reg(REG_OE_CTRL, 0xF8, "reg3 OE=CLK0|CLK1|CLK2");
    } else {
        ok &= wr_reg(REG_OE_CTRL, 0xFC, "reg3 OE=CLK0|CLK1");
        ok &= wr_reg(REG_CLK2_CTRL, 0xCC, "reg18 CLK2 power down");
    }
    return ok;
}

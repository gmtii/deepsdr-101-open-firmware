#ifndef MS5351_H
#define MS5351_H

#include <stdint.h>

/*
 * MS5351M clock generator driver (Si5351A register-compatible clone).
 *
 * Role in this SDR: quadrature local oscillator for the QSD (quadrature
 * sampling detector). CLK0 and CLK1 both run at the LO frequency from
 * PLLB, with CLK0 phase-offset by exactly one output-divider count,
 * which at an even divider N means a delay of N/(4*fVCO) = 1/(4*fOUT)
 * = 90 degrees. Classic Si5351 quadrature-LO trick, no external
 * divide-by-4 Johnson counter needed. That trick only reaches down to
 * ~4.8MHz, though - below that, ms5351_set_lo_freq() switches to a
 * second technique entirely (a ported, community-vetted design, not a
 * capture from this board) extending coverage down to 100kHz - see
 * that function's comment below for which does what, and
 * ms5351_set_lo_freq_lowband()'s comment in ms5351.c for the low-band
 * technique's own derivation.
 *
 * Everything in ms5351_init() and ms5351_tune_captured() is a
 * byte-exact replay of an I2C capture taken from the original firmware
 * of this board (capture: captura_i2c_sdr.txt, device address 0x60).
 * Decoded facts from that capture:
 *
 *   - Reference: 26MHz (inferred: PLLA is set to x32 integer = 832MHz
 *     and MS2 to /104, which lands on exactly 8.000MHz only with a
 *     26MHz reference; a 25MHz one would give an implausible
 *     7.6923MHz).
 *   - PLLA = 832MHz fixed, integer mode. Only used by CLK2.
 *   - CLK2 = 8MHz from PLLA/MS2, fully configured but left POWERED
 *     DOWN (reg18 = 0xCC, PDN bit set) and not output-enabled. Likely
 *     a TX clock or auxiliary reference the original firmware only
 *     turns on when needed. We replicate it as-is; see
 *     ms5351_clk2_8mhz() to switch it on.
 *   - PLLB = fractional, retuned on every LO change. The captured tune
 *     is PLLB = 908.000MHz / MS0 = MS1 = /10 -> LO = 90.800MHz (an FM
 *     broadcast frequency - clearly a reception test). Note 908MHz is
 *     marginally above the 900MHz datasheet VCO max; MS5351 clones
 *     take this without complaint, and the capture proves this board
 *     does.
 *   - Quadrature: CLK0 phase offset (reg165) = output divider (10),
 *     CLK1 offset (reg166) = 0, then PLLB soft reset (reg177 = 0x80)
 *     to latch the new phase relationship.
 *
 * I2C: uses the shared bit-banged bus (i2c_bitbang). ms5351_init()
 * does NOT initialize the bus itself - call it after i2c_bitbang_init()
 * has already run (in main.c the AIC3204 phase-1 probe does that).
 */

#define MS5351_ADDR      0x60U
/* PPM-corrected (21/08/2026) - measured via the SAM PLL carrier-
 * frequency-offset meter (see sam_freq_offset.c), two known-frequency
 * stations:
 *   7220000 Hz -> +20Hz   (2.770 ppm)
 *   13855000 Hz -> +43Hz  (3.104 ppm)
 * Close enough between the two (~12% relative) to treat as one flat
 * crystal ppm error rather than something frequency-dependent, so
 * combined via a single-parameter least-squares fit (offset = k*freq,
 * forced through the origin) rather than a plain average of the two
 * ppm figures, weighting the higher (more precise, larger-denominator)
 * frequency appropriately: k = sum(offset_i*freq_i)/sum(freq_i^2) =
 * 3.032e-6.
 *
 * Direction check (worth keeping - easy to get backwards): a
 * DOWN-converted carrier appearing at a HIGHER-than-expected baseband
 * frequency (needing the PLL to shift UP, i.e. a POSITIVE reading)
 * means the LO's actual frequency was LOWER than commanded, which
 * happens when the real crystal runs SLOWER than the value this
 * driver assumes for all its PLL math - so a positive reading means
 * the assumed XTAL_HZ needs to be REDUCED, not increased.
 *
 *   ppm_true = -3.032 ppm
 *   corrected = 26000000 * (1 + ppm_true/1e6) = 26000000 - 78.8 = 25999921 Hz
 *
 * Two points now agree well enough to trust this - if a third,
 * well-separated measurement later disagrees noticeably, that would
 * point to a real frequency-dependent effect this flat-ppm model
 * doesn't capture, worth re-opening then.
 *
 * NOT a #define anymore (26/08/2026) - now just the BOOT-TIME DEFAULT
 * for a runtime variable (see ms5351_get_xtal_hz()/ms5351_set_xtal_hz()
 * below), so the CAL PPM tile (main.c, MENU_PAGE_HW) can correct it
 * on live hardware and persist the result to CONFIG.CSV
 * ("ms5351_xtal_hz" key - see settings.c) instead of requiring a
 * recompile every time the board's actual crystal error is
 * re-measured. */
#define MS5351_XTAL_HZ_DEFAULT   25999921UL

/* The LO frequency encoded in the captured tune block, for reference
 * and for validating ms5351_set_lo_freq() against known-good bytes:
 * ms5351_set_lo_freq(MS5351_CAPTURED_LO_HZ) with divider 10 reproduces
 * the capture bit-exactly. */
#define MS5351_CAPTURED_LO_HZ 90800000UL

/*
 * Boot-time base configuration, byte-exact replay of the captured
 * startup block: outputs off, CLK0-2 powered down, 8pF crystal load,
 * PLLA = x32 integer (832MHz), PLL reset, MS2 = /104 (8MHz, left
 * powered down), output-enable mask = CLK0|CLK1.
 *
 * Call once at startup, after the I2C bus is up, BEFORE the first
 * tune. Returns 1 if every write was ACKed, 0 otherwise.
 */
uint8_t ms5351_init(void);

/*
 * Byte-exact replay of the captured tune block: LO = 90.800MHz in
 * quadrature on CLK0/CLK1. Useful as ground truth during bring-up -
 * these exact bytes are hardware-proven. Returns 1 if all ACKed.
 */
uint8_t ms5351_tune_captured(void);

/*
 * General quadrature tune: puts freq_hz on CLK0 and CLK1. TWO
 * different techniques under the hood, picked automatically by
 * frequency, and - as of 01/09/2026 - they do NOT share the same
 * "which clock leads" convention, so don't assume one from the other:
 *
 *   >= 4.8MHz (the register-offset scheme below): CLK1 leads, CLK0
 *   lags - the Si5351/MS5351 datasheet defines CLKx_PHOFF as a time
 *   DELAY, and this scheme writes its nonzero value to CLK0, so CLK0
 *   is the one being delayed. Confirmed working on real hardware
 *   (correct SSB sideband sense, e.g. at 4.9MHz) - this path itself
 *   was never actually changed, only its OWN documentation was
 *   corrected (it used to wrongly claim "CLK0 leads" here too).
 *
 *   < 4.8MHz (the low-band phase-timing trick, see ms5351_set_lo_
 *   freq_lowband()): CLK0 leads, CLK1 lags - its OWN, unrelated
 *   mechanism (a timed divider-speedup race, not a phase-offset
 *   register), which briefly got swapped to match the high-band
 *   path's "CLK1 leads" convention above, then swapped BACK after
 *   the project owner found (via a real external generator, on a
 *   separate but analogous module - see lo_gen_gd32.c's own history)
 *   that assuming the two techniques must share one convention was
 *   itself the mistake. See ms5351_set_lo_freq_lowband()'s own
 *   step-3 comment for the fuller back-and-forth.
 *
 * See ms5351.c for both implementations:
 *
 *   >= 4.8MHz: the ORIGINAL phase-offset-register scheme (unchanged
 *   from before 31/07/2026) - PLLB fractional feedback + an EVEN
 *   integer output divider N (4-126) whose value is also written to
 *   the CLKx_PHOFF registers to get exactly 90 degrees. Divider
 *   selection: largest even N with N*freq inside the VCO window,
 *   capped at 126 (the 7-bit phase-offset register's limit).
 *
 *   < 4.8MHz: a genuinely FRACTIONAL output divider technique ported
 *   from a well-known, community-vetted SI5351 driver (JF3HZB /
 *   T.UEBO) - see ms5351_set_lo_freq_lowband()'s big comment in
 *   ms5351.c for the full derivation (a "photo finish" trick: run
 *   CLK1 a few Hz slow for a precisely-timed window, then snap it to
 *   the target frequency once it's accumulated exactly 90 degrees of
 *   lag behind CLK0). Extends the usable range down to 100kHz
 *   (LOWF_FLOOR_HZ in ms5351.c) - NOT bench-confirmed on this board
 *   yet at the low end, see that comment's PLL FEEDBACK MULTIPLIER
 *   WARNING before relying on frequencies much below ~1MHz. Crossing
 *   between the two techniques' zones (1.5MHz, 500kHz) costs a one-off
 *   ~62.5ms blocking delay - tuning within a zone is fully responsive.
 *
 * PLLB is soft-reset only when the divider (and therefore the phase
 * offset) changes with respect to the previous call - resetting the
 * PLL on every retune causes an audible click and is not needed when
 * only the fractional feedback moves. First call always resets.
 *
 * Returns 1 on success (all writes ACKed), 0 on I2C failure or if
 * freq_hz is outside the supported range (below LOWF_FLOOR_HZ or
 * above the high-band ceiling).
 */
uint8_t ms5351_set_lo_freq(uint32_t freq_hz);

/*
 * Puts CLK0/CLK1 (the quadrature LO pair feeding the QSD) into Hi-Z:
 * both output-enable-masked (reg3, same "1=disabled" mask
 * ms5351_init()/ms5351_clk2_8mhz() already use) AND powered down at
 * the CLK-control level (reg16/17 PDN bit, same 0x80 pattern
 * ms5351_init() uses while reconfiguring) - belt and braces, matching
 * every other place this driver disables a clock. CLK2 is untouched
 * either way (see ms5351_clk2_8mhz()'s header - it's an unrelated,
 * currently-unused auxiliary output, not the QSD LO).
 *
 * Added 13/08/2026 for bench testing: injecting a test signal
 * directly into the codec's IN2/IN3 pins (bypassing the antenna/QSD
 * front end) needs the QSD's own switches to stop toggling first, or
 * whatever the QSD is chopping (even just floating-input noise, with
 * no antenna connected) keeps landing on the same IN2/IN3 nodes and
 * fighting with the injected test signal.
 *
 * To resume normal reception afterward, just call
 * ms5351_set_lo_freq() again with the desired LO frequency - it
 * always does a full PLL-reset retune (this function forces that by
 * invalidating the last-tuned-divider cache), so there's no separate
 * "re-enable" call needed.
 *
 * Returns 1 if all writes were ACKed.
 */
uint8_t ms5351_lo_disable(void);

/*
 * CLK2 8MHz auxiliary output (PLLA/104), captured as configured but
 * powered down. on=1 powers it up and enables its output, on=0
 * returns it to the captured (off) state. Returns 1 if ACKed.
 */
uint8_t ms5351_clk2_8mhz(uint8_t on);

/*
 * Runtime crystal frequency (26/08/2026) - replaces the old
 * MS5351_XTAL_HZ #define everywhere frac_divide() needs the actual
 * reference frequency. Starts at MS5351_XTAL_HZ_DEFAULT every boot;
 * settings_load() overwrites it from CONFIG.CSV's "ms5351_xtal_hz"
 * key if present (same "caller applies it, settings.c just parses
 * it" split touch calibration doesn't use, since here there's no
 * ordering dependency on the rest of main()'s boot sequence the way
 * vfo_hz/mode have - safe to apply immediately from within
 * settings_load() itself, same as touch_set_calibration()).
 *
 * ms5351_get_xtal_hz() is also what settings.c's build_csv() reads to
 * persist the current value - see its own comment for why that's a
 * direct call instead of yet another settings_poll()/
 * settings_save_now() parameter (same pattern as
 * touch_get_calibration()).
 */
uint32_t ms5351_get_xtal_hz(void);

/* Does NOT retune the LO by itself - the caller (see main.c's CAL PPM
 * tile) must follow this with an ms5351_set_lo_freq() call (or
 * equivalently apply_lo_tune(s_tune_hz)) for the new reference to
 * actually take effect on the running LO. Kept separate rather than
 * retuning internally because this driver has no idea what frequency
 * is currently supposed to be on the air - only main.c does. */
void ms5351_set_xtal_hz(uint32_t xtal_hz);

#endif /* MS5351_H */

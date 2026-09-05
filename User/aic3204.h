#ifndef AIC3204_H
#define AIC3204_H

#include <stdint.h>

/*
 * TLV320AIC3204 codec driver.
 *
 * The AIC3204 organizes its registers into "pages": register 0x00 on
 * ANY page is the "Page Control Register" - writing it selects the
 * active page, and reading it always returns the currently selected
 * page. This gives a free closed-loop test to validate the I2C link
 * without touching any audio register: write a page, read it back,
 * and it must match.
 *
 * I2C address: defaults to 0x18 with the AIC3204's MODE pin tied to
 * ground (the most common breakout configuration), or 0x19 if tied to
 * VDD. If aic3204_init() doesn't detect 0x18, use aic3204_scan_bus()
 * to find the real address before assuming the chip isn't responding.
 */

#define AIC3204_ADDR_DEFAULT 0x18U

/* AIC3204 nRESET - PB11, active low (confirmed on real hardware).
 * aic3204_init() releases it before touching the I2C bus - without
 * this the chip never responds reliably. Defined inside aic3204.c,
 * not here, to avoid pulling gd32f4xx.h into this header. */

/* Call once at startup: initializes the I2C bus (i2c_bitbang) and
 * gets the driver ready to use the given address (normally
 * AIC3204_ADDR_DEFAULT). Does not touch the chip yet - that's
 * aic3204_probe_and_reset(). */
void aic3204_init(uint8_t i2c_addr);

/* Selects `page` and writes `value` to `reg` within that page.
 * Returns 1 if both writes (page select + register) were ACKed, 0 if
 * either failed. */
uint8_t aic3204_write_reg(uint8_t page, uint8_t reg, uint8_t value);

/* Selects `page` and reads `reg` within that page into `*value`.
 * Returns 1 if the whole operation (page write + register read) was
 * ACKed, 0 on failure (in which case *value is not valid). */
uint8_t aic3204_read_reg(uint8_t page, uint8_t reg, uint8_t *value);

/*
 * Full bring-up test, meant to run once at startup and dump its
 * result over UART (uses debug_print internally):
 *   1. Page Control Register closed-loop test (write page 1, read
 *      back, write page 0, read back) - confirms the I2C link works,
 *      without assuming anything about the rest of the chip.
 *   2. Software reset (page 0, register 0x01 = 0x01) - the chip
 *      returns to its reset values; there is no direct way to read
 *      back confirmation of this (the reset bit is self-clearing),
 *      but if the closed-loop test in step 1 already succeeded, the
 *      reset is essentially guaranteed to have worked too.
 * Returns 1 if step 1 (the only directly verifiable part) succeeded.
 */
uint8_t aic3204_probe_and_reset(void);

/* Scans the I2C bus (addresses 0x08-0x77) and prints over UART
 * (debug_print) every address that ACKs. Useful if
 * aic3204_probe_and_reset() fails and the real address needs
 * confirming instead of assuming 0x18. */
void aic3204_scan_bus(void);

/*
 * Phase 2 init: the AIC3204's clock/PLL, ADC input routing, and ADC
 * power-up, ported byte-for-byte (138 register writes, exact order)
 * from a REAL I2C bus capture of the original, known-working
 * firmware's own configuration of this same codec on this same board
 * - not derived, not extrapolated, not guessed.
 *
 * This capture revealed the actual clock architecture: the AIC3204's
 * own PLL is enabled, clocked from MCLK - NOT from BCLK, despite what
 * an earlier version of this comment claimed; corrected 01/09/2026
 * after the project owner pointed out that claim made no physical
 * sense (the codec is the I2S MASTER here, so BCLK is ITS OWN output -
 * a PLL can't sensibly derive its own reference from a signal
 * downstream of that same PLL). Re-decoded directly against the real
 * register bit fields (P0R4=0x43, P0R5=0x94 - see
 * aic3204_configure_rate()'s own comment for the full derivation):
 * PLL_CLKIN=MCLK, P=1, R=4, J=14, D=0 -> PLL_CLK = MCLK x R x J / P =
 * 1.536MHz x 4 x 14 / 1 = 86.016MHz = CODEC_CLKIN exactly, using this
 * project's own TIMER2_CH0/PC6-generated 1.536MHz MCLK signal (see
 * gd32_i2s_mclk_timer_start()) - which both the ADC (NADC=1/MADC=28/
 * AOSR=64) and DAC (NDAC=2/MDAC=7/DOSR=128) divider chains reduce to
 * exactly Fs=48kHz (was MADC=7/DOSR=32 -> 192kHz before 04/08/2026 -
 * see aic3204_phase2_init()'s header comment for the full story,
 * including the DAC processing-block change that had to come with
 * it). A same-day real-hardware experiment (disabling MCLK generation
 * on the theory that this comment's old, wrong claim made it
 * vestigial) directly confirmed MCLK is genuinely required - it broke
 * reception (off-frequency, degraded WFM, other odd behavior) - and
 * was restored.
 *
 * Call this after aic3204_probe_and_reset() AND after the I2S side is
 * already generating a real clock towards the codec
 * (gd32_i2s_init_slave_192k already executed) - phase 2 configures
 * clock registers that depend on BCLK/WCLK already running.
 *
 * `rate` parameterizes the cold-boot rate itself - added 01/09/2026
 * so main() can boot directly into AIC3204_RATE_192K when the saved
 * config's last mode was WFM, instead of always cold-booting at 96K
 * and immediately following up with a second, WARM apply_demod_mode()
 * switch into WFM. See main()'s boot sequence comment for the full
 * "double reset" reasoning this closes - every OTHER caller/scenario
 * (any non-WFM saved mode, or no saved mode at all) still passes
 * AIC3204_RATE_96K here, unchanged from this function's old
 * no-parameter behavior.
 */
/*
 * Which sample rate the codec's ADC/DAC divider chains are currently
 * configured for - see aic3204_set_rate()'s comment in aic3204.c for
 * exactly which registers each value touches. Declared here, ahead of
 * aic3204_phase2_init() below, since that function's own signature
 * now needs this type in scope.
 */
typedef enum {
    AIC3204_RATE_96K = 0,  /* AM/USB/LSB/NFM default - the default after aic3204_phase2_init() */
    AIC3204_RATE_192K,     /* WFM */
    AIC3204_RATE_48K       /* AM/USB/LSB/NFM alternate - added 01/09/2026, see
                             * aic3204_configure_rate()'s comment for why (a
                             * user-selectable escape hatch for a birdie tied
                             * to N*96kHz - moving to 48kHz relocates it) */
} aic3204_rate_t;

void aic3204_phase2_init(aic3204_rate_t rate);

/*
 * Live rate switch, split into SEVERAL calls - see main.c's
 * apply_demod_mode() for where this whole sequence is actually
 * enforced:
 *
 *   1. sdr_rx_stop() + gd32_i2s_stream_stop()
 *   2. aic3204_rate_switch_reset() - genuine hardware nRESET pulse,
 *      codec falls fully silent (no BCLK/WCLK at all).
 *   3. gd32_i2s_init_slave(rate) (gd32_i2s.h) - *** 05/08/2026, "full
 *      reinit instead of live resync" rewrite *** - a FULL teardown/
 *      rebuild of SPI1/I2S1_ADD for the new rate (spi_i2s_deinit(),
 *      PLLI2S reconfigure, i2s_init(), GPIO AF replay), the same
 *      sequence cold boot always used - not just a disable/re-enable
 *      of what was already configured. Also re-arms DMA0/CH4 with
 *      silence. See gd32_i2s.c's own comment for why this replaced the
 *      earlier partial-resync approach.
 *   4. aic3204_configure_rate(rate) - reprograms the PLL and the
 *      NDAC/MDAC/DOSR/NADC/MADC/AOSR divider chains, plus everything
 *      else aic3204_phase2_init() originally set (analog routing,
 *      biquad/DRC coefficient banks) - but does NOT touch R27/R30, so
 *      BCLK/WCLK stay silent even though the clock tree behind them is
 *      now fully configured for the new rate. ADC/DAC left powered
 *      DOWN.
 *   5. sdr_rx_bringup(block_samples) + gd32_i2s_stream_arm(block_samples)
 *      (sdr_rx.h/gd32_i2s.h) - arms BOTH DMA channels at the new block
 *      size. SPI1/I2S1_ADD are already freshly enabled from step 3, so
 *      no separate resync is needed here anymore.
 *   6. aic3204_start_bclk_wclk(rate) - NOW, and only now, BCLK/WCLK go
 *      live (R30 then R27) - the GD32 side is already listening at
 *      this point, so this is the first real edge it sees.
 *   7. aic3204_set_rate_power_up() - ADC/DAC start producing real
 *      samples at the new rate, straight into an already-armed DMA
 *      path.
 */
void aic3204_rate_switch_reset(void);
void aic3204_configure_rate(aic3204_rate_t rate);
void aic3204_start_bclk_wclk(aic3204_rate_t rate);
void aic3204_set_rate_power_up(void);


/*
 * Sets the DAC digital output volume, both L/R channels identically.
 * Range -63.5dB to +24dB in 0.5dB steps (silently clamped) - the
 * AIC3204's native DAC digital volume control (Page 0, R65/R66).
 * aic3204_phase2_init() leaves both at 0dB (unity, the byte-exact
 * captured baseline); call this afterwards to move away from that.
 * Does NOT touch the analog HPL/HPR headphone driver gain (Page 1,
 * R16/R17) - see the comment above this function's definition in
 * aic3204.c for why. Returns 1 if both L/R writes were ACKed.
 */
uint8_t aic3204_set_volume_db(float db);

/*
 * Sets the MIC_PGA analog input gain (Page 1, R59/R60), both L/R
 * channels identically - the stage BEFORE the ADC, unlike
 * aic3204_set_volume_db() above (which is after the DAC, on the
 * output side). Range 0dB to 47.5dB in 0.5dB steps (silently
 * clamped, unsigned - there's no attenuation direction, only gain).
 * aic3204_phase2_init() leaves both at 20dB (0x28, the byte-exact
 * captured baseline); call this afterwards to move away from that.
 * Returns 1 if both L/R writes were ACKed.
 */
uint8_t aic3204_set_pga_gain_db(float db);

/*
 * Input impedance / Rin selector for the MIC_PGA differential inputs
 * (Page 1, R52/R54/R55/R57) - see aic3204_set_input_impedance()'s
 * comment in aic3204.c for the full story, including the
 * *** IMPORTANT UNVERIFIED ASSUMPTION *** about the 20k/40k byte
 * values (inferred from the datasheet's standard field encoding, not
 * from a captured trace like everything else in this driver).
 *
 * 10k is the widest-gain option (0..47.5dB) and matches
 * aic3204_phase2_init()'s captured baseline - stay there for weak
 * signals. 20k/40k trade PGA gain range for extra fixed attenuation
 * (-6dB/-12dB), useful once aic3204_set_pga_gain_db(0.0f) alone still
 * isn't enough backoff for a very strong local signal. NOT
 * soft-stepped like the gain register - callers should mute around a
 * call to this (demod_am_reset_diag()/demod_wfm_reset_diag()) to
 * avoid an audible pop. Returns 1 if all four writes were ACKed.
 */
typedef enum {
    AIC3204_RIN_10K = 0,
    AIC3204_RIN_20K = 1,
    AIC3204_RIN_40K = 2
} aic3204_rin_t;

uint8_t aic3204_set_input_impedance(aic3204_rin_t level);

/*
 * TEMPORARY DIAGNOSTIC (28/07/2026): TI documents a digital "Audio Bus
 * Loopback" mode (Page 0 / Register 29, bit D5) that reflects
 * whatever comes in on DIN straight back out on DOUT, entirely
 * bypassing the ADC/DAC converters - designed specifically to verify
 * host<->codec digital bus communication in isolation from the analog
 * front-end. Set to 1 to enable it: if a known TX pattern comes back
 * correctly on RX, the whole digital I2S/DMA path is proven working
 * end-to-end and the remaining problem is isolated to the ADC analog
 * front-end. If RX is still stuck even in loopback, the bug is in the
 * digital bus framing/format itself. Set back to 0 for normal
 * operation. Also read by gd32_i2s.c, to automatically feed a real TX
 * pattern instead of silence while this is enabled (silence looped
 * back is still silence - not a useful test).
 */
#define AIC3204_TEST_LOOPBACK 0

#endif /* AIC3204_H */

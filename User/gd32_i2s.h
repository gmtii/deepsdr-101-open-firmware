#ifndef GD32_I2S_H
#define GD32_I2S_H

#include <stdint.h>
#include "aic3204.h" /* aic3204_rate_t, for gd32_i2s_init_slave(rate) */

/*
 * I2S1 (SPI1) peripheral configuration for the GD32F450, SLAVE mode,
 * full-duplex (main I2S1 block + I2S1_ADD extension block), for
 * 192kHz stereo I/Q with the AIC3204 codec.
 *
 * ARCHITECTURE (28/07/2026, corrected): a real I2C bus capture of the
 * original firmware's AIC3204 configuration shows register 0x1B=0x0C,
 * which sets BCLK and WCLK as OUTPUTS on the codec (confirmed against
 * a real TI forum example using this exact byte, and TI's SLAA404C
 * app note describing bits D3:D2 of this register as the BCLK/WCLK
 * direction control) - the codec is the I2S MASTER in this design,
 * not a slave. The GD32 must be configured as SLAVE to match. Running
 * both sides as master at the same time caused real, observed bus
 * contention on a real oscilloscope (BCLK/WCLK with irregular,
 * non-uniform pulse widths).
 *
 * STILL PENDING: MCLK is not generated at all right now. With the
 * codec expecting to be master, it needs a real MCLK to lock its own
 * PLL and generate a stable BCLK/WCLK - without it, expect BCLK/WCLK
 * to still be unstable even with the master/slave roles now correct.
 * This MCU's native I2S MCKOUT block can't produce the board's real
 * MCLK:BCLK:Fs ratio (8:32:1, fixed at 256x/32x instead) - the plan is
 * a general-purpose timer (TIMER2_CH0 on PC6 is available as an
 * alternate function on this pin) generating an independent 1.536MHz
 * square wave, pending confirmation of the exact AF number for that
 * function on this specific pin.
 *
 * Note: on this chip/board, the pins below belong to SPI1/I2S1, NOT to
 * SPI2/I2S2 - double-check this if porting from an STM32F4 reference,
 * since peripheral numbering on these specific pins differs.
 *
 * Pins (confirmed on real hardware):
 *   PB12 = WS   (I2S1_WS)        AF5 (now an INPUT - codec drives it)
 *   PB13 = BCLK (I2S1_CK)        AF5 (now an INPUT - codec drives it)
 *   PB15 = DOUT (I2S1_SD, TX)    AF5
 *   PB14 = DIN  (I2S1_ADD_SD,RX) AF6 (confirmed against the datasheet's
 *                                     Port B alternate function table -
 *                                     AF5 on this pin is SPI1_MISO, a
 *                                     different peripheral entirely)
 */
/*
 * NOTE (04/08/2026): despite the name, this now brings up the I2S
 * peripheral for 48kHz, not 192kHz - see gd32_i2s.c's own comment on
 * why the function itself needs almost no change for the Fs move (the
 * GD32 is the I2S SLAVE; the codec's own divider chain, in aic3204.c,
 * is what actually sets Fs). Left un-renamed today to keep this
 * change's diff contained to what actually needed to move, not
 * because the name is still accurate - a rename is a reasonable
 * follow-up, just a separate one.
 */
void gd32_i2s_init_slave_192k(void);

/*
 * *** 05/08/2026, added for the "full reinit instead of live resync"
 * rewrite *** - generalized version of gd32_i2s_init_slave_192k()
 * above (which is now a thin AIC3204_RATE_96K wrapper around this, for
 * the one cold-boot call site in main.c). Does the FULL teardown/
 * rebuild every time: spi_i2s_deinit(SPI1), a full PLLI2S off/
 * reconfigure/on/wait-for-lock, i2s_init()/i2s_full_duplex_mode_
 * config(), and GPIO AF replay - not just re-enabling what was already
 * there. See gd32_i2s.c's own comment on this function for why: this
 * is the only sequence real hardware testing has ever found to run
 * FERR-free after a rate change, cold boot or live.
 */
void gd32_i2s_init_slave(aic3204_rate_t rate);

/*
 * Diagnostic utility: toggles the five I2S pins as plain GPIO (no I2S
 * peripheral involved) to isolate whether a "no signal" symptom is a
 * peripheral/clock issue or a physical wiring issue. `cycles` = number
 * of toggles (~100Hz each, visible on a scope or AC-coupled meter).
 */
void gd32_i2s_pins_gpio_toggle_test(uint32_t cycles);

/*
 * Starts a circular DMA channel that continuously feeds SPI1/I2S1 with
 * silence (all-zero samples). Called automatically at the end of
 * gd32_i2s_init_slave_192k(). Historically this was required to keep
 * this MCU's MASTER-mode clock generator running (it would stop
 * without continuous TX data) - now that SPI1/I2S1 is a SLAVE that
 * concern no longer applies, but this is kept as a reasonable default
 * TX source for whatever DAC path exists on the codec side.
 */
/*
 * Starts MCLK generation on PC6 via TIMER2_CH0 (AF2, confirmed against
 * the datasheet), targeting 1.536MHz - independent of the I2S1
 * peripheral entirely, since its native MCKOUT block can't produce
 * this board's real MCLK:BCLK:Fs ratio. Call this BEFORE
 * gd32_i2s_init_slave_192k(), so the codec has a valid MCLK reference
 * from the moment its own I2S clocking comes up.
 */
void gd32_i2s_mclk_timer_start(void);

void gd32_i2s_dma_start_silence(void);

/*
 * Starts a circular DMA channel that continuously feeds SPI1/I2S1 with
 * a 1kHz test tone (does not stop, unlike a manual feed loop would).
 * Not called automatically - call it manually for TX/DAC-side bring-up
 * testing (replaces the silence fed by gd32_i2s_dma_start_silence()).
 */
void gd32_i2s_dma_start_test_tone(void);

/*
 * Re-arms DMA0/CH4 as a circular 2-half ping-pong TX stream (2 x
 * SDR_RX_BLOCK_SAMPLES stereo frames, starts silent - was a literal
 * "512" here and in gd32_i2s.c until 04/08/2026, when that hand-
 * copied number silently went stale after the 48kHz move and caused
 * a real out-of-bounds read bug - see STREAM_FRAMES_PER_HALF's
 * comment in gd32_i2s.c for the full story), replacing the silence/
 * test-tone feed. This is the DAC-side transport for demodulated
 * audio. Call after the codec is fully configured (phase 2).
 */
void gd32_i2s_dma_start_stream(void);

/*
 * Cleanly stops the TX stream DMA channel - call BEFORE tearing down
 * the I2S peripherals for a rate change (see main.c's apply_demod_
 * mode()).
 */
void gd32_i2s_stream_stop(void);

/*
 * *** 05/08/2026, added for the "full reinit instead of live resync"
 * rewrite - see gd32_i2s.c's own comment *** - arms the TX stream at
 * `frames_per_half` stereo frames (MUST be SDR_RX_BLOCK_SAMPLES or
 * SDR_RX_BLOCK_SAMPLES_WFM, sdr_rx.h - the two sides must always
 * agree, since RX and TX share the same codec-driven BCLK/WS at the
 * same rate). Call ONLY after gd32_i2s_init_slave(rate) has already
 * done a full teardown/rebuild of I2S1_ADD for that same rate -
 * supersedes the old gd32_i2s_stream_reconfigure()+gd32_i2s_stream_
 * start() pair, which used to separately resync I2S1_ADD; that's now
 * gd32_i2s_init_slave(rate)'s job, done once, the same way for cold
 * boot and every live switch.
 */
void gd32_i2s_stream_arm(uint32_t frames_per_half);

/*
 * Copies one half's worth of stereo frames (SDR_RX_BLOCK_SAMPLES
 * frames = SDR_RX_BLOCK_SAMPLES*2 int16, L/R interleaved - see
 * gd32_i2s_dma_start_stream()'s comment for why this is phrased in
 * terms of that constant now, not a literal count) into whichever
 * stream half is NOT currently being played, derived from the live
 * DMA position. Intended to be called once per RX block from the RX
 * DMA interrupt (see demod_am.c). The caller's buffer MUST be at
 * least that many stereo frames long - demod_am.c's s_audio_out[] is
 * sized exactly to match (SDR_RX_BLOCK_SAMPLES*2 int16), which is the
 * invariant that broke on 04/08/2026 (see above) when this file's own
 * copy of the frame count didn't move with it. As of 05/08/2026, WFM's
 * own demod_wfm_process_raw() calls this too, passing its own
 * SDR_RX_BLOCK_SAMPLES_WFM-sized s_wfm_audio_out - safe either way
 * since gd32_i2s_stream_reconfigure() above keeps this function's own
 * idea of "one half" in sync with whichever rate is actually active.
 */
void gd32_i2s_stream_write_half(const int16_t *stereo_frames);

/* TX-side FERR diagnostics (see gd32_i2s.c's own comments) - was
 * missing from this header (implicit-declaration warning fixed
 * 05/08/2026 alongside the R27/R30 clock-start reordering). */
uint32_t gd32_i2s_get_tx_ferr_count(void);

/*
 * EL MARGEN DE FASE DEL PING-PONG DE SALIDA - 06/10/2026.
 *
 * Ver el comentario largo dentro de gd32_i2s_stream_write_half(). En corto:
 * la eleccion de mitad no tiene memoria, y si la llamada cae cerca de la
 * frontera del buffer, deja de alternar - una mitad se escribe dos veces y
 * la otra repite 2,67 ms, que es un peine de armonicos a 375 Hz.
 *
 *   repes     veces que se ha repetido mitad. En regimen bueno: 0.
 *   min/max   donde cae la llamada DENTRO de la mitad, en palabras: lo mas
 *             bajo y lo mas alto vistos. Los dos bajos = la interrupcion va
 *             sobrada; los dos altos = esta llegando al final de su
 *             presupuesto. min a 0xFFFFFFFF = aun no se ha escrito ninguna.
 *   palabras  cuantas palabras tiene una mitad, para leerlo como fraccion
 *             (en WFM son 1024; en el resto, 512).
 */
uint32_t gd32_i2s_get_half_repes(void);
uint32_t gd32_i2s_get_half_min(void);
uint32_t gd32_i2s_get_half_max(void);
uint32_t gd32_i2s_get_half_palabras(void);
void     gd32_i2s_reset_half_diag(void);
void gd32_i2s_reset_tx_ferr_count(void);

#endif /* GD32_I2S_H */

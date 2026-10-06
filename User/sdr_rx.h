#ifndef SDR_RX_H
#define SDR_RX_H

#include <stdint.h>

/*
 * Captures RX samples from the AIC3204 ADC via the MAIN SPI1/I2S1
 * block (PB15=SD), using circular DMA.
 *
 * BLOCK ROLES SWAPPED (28/07/2026): this used to read from the
 * I2S1_ADD extension (PB14) - disassembly of the original firmware's
 * own I2S setup, combined with the GD32F4xx User Manual's DMA request
 * mapping table showing both directions exist as distinct
 * subperipheral options on the same DMA channels, showed the ORIGINAL
 * design uses the MAIN block for RX and the extension for TX - the
 * opposite of what this driver assumed for a long time. See the
 * comments in gd32_i2s.c's gd32_i2s_init_slave_192k() for the full
 * reasoning.
 *
 * DMA mapping: SPI1_RX -> DMA0, Channel 3, PERIEN[2:0]=000
 * (DMA_SUBPERI0). Confirmed against the GD32F4xx User Manual's DMA
 * request mapping table (Table 10-2, "Peripheral requests to DMA0").
 *
 * PING-PONG MODEL: a single circular buffer of 2*(active block size)
 * (stereo, 16-bit). When the DMA finishes the first half (HTF flag),
 * that half is complete and stable while the DMA writes into the
 * second half; when it completes the full circular round (FTF flag),
 * the second half is the one that's ready. sdr_rx_poll_block_iq()
 * tracks this and returns 0 if no new block is ready yet (non-blocking,
 * meant to be called from the main loop like the rest of this project).
 */

#define SDR_RX_BLOCK_SAMPLES     256U   /* AM/SSB/NFM's block size @ 96kHz -
                                          * was 128 @ 48kHz until AM/SSB/NFM moved
                                          * up to 96kHz for better RF coverage
                                          * (48kHz's Nyquist-adjacent span felt too
                                          * narrow) while keeping WFM's 192kHz path
                                          * untouched. Same 2.667ms/block either way
                                          * (256/96000 = 128/48000 exactly), so
                                          * everything timed off the BLOCK RATE
                                          * (squelch EMA, AGC-per-block reasoning,
                                          * display cadence) needed no changes -
                                          * only things timed off the raw SAMPLE
                                          * rate (DCB_R, AGC_RELEASE_*, CHF/ALPF/
                                          * NFM_CHF corner frequencies, the SSB
                                          * decimate/interpolate factor, DEMOD_IF_
                                          * OFFSET_HZ) needed recomputing - see
                                          * demod_am.c/demod_am.h for those. */
#define SDR_RX_BLOCK_SAMPLES_WFM 512U   /* WFM's block size @ 192kHz - reactivated
                                          * 05/08/2026 (was the ONLY rate this project
                                          * had before 04/08/2026's 48kHz move; see
                                          * demod_am.h's WFM note for why WFM alone
                                          * still needs the wider Nyquist window). */
#define SDR_RX_BLOCK_SAMPLES_MAX SDR_RX_BLOCK_SAMPLES_WFM /* 512 - the size any
                                          * SHARED buffer (this file's own raw
                                          * capture buffer, gd32_i2s.c's TX stream
                                          * buffer, the FFT/panadapter arrays in
                                          * fft.c/main.c) must be statically
                                          * allocated at, since whichever mode is
                                          * active determines how much of it is
                                          * actually used - see
                                          * sdr_rx_get_block_samples(). */

/* Starts the capture DMA at SDR_RX_BLOCK_SAMPLES (48kHz) - the boot
 * default, since AM is the default mode. Must be called AFTER
 * gd32_i2s_init_slave_192k() (needs SPI1/I2S1_ADD already initialized
 * and enabled). Runs the one-time RXORERR-clearing bring-up sequence
 * - NOT repeated by sdr_rx_reconfigure() below, which is a much
 * lighter "just change the block size" operation for an already-
 * running capture. */
void sdr_rx_init(void);

/*
 * Cleanly stops the capture DMA channel (disables, waits for CHEN to
 * actually drop) - call this BEFORE tearing down the I2S peripherals
 * for a rate change (see main.c's apply_demod_mode()). Safe to call
 * even if already stopped.
 */
void sdr_rx_stop(void);

/*
 * *** 05/08/2026, added for the "full reinit instead of live resync"
 * rewrite - see sdr_rx.c's own comment *** - the real bring-up logic:
 * sets the active block size (`block_samples` MUST be exactly
 * SDR_RX_BLOCK_SAMPLES or SDR_RX_BLOCK_SAMPLES_WFM), clears any stray
 * RXORERR, and arms DMA0/CH3. Call ONLY after gd32_i2s_init_slave(rate)
 * has already done a full teardown/rebuild of SPI1 for that same rate
 * - supersedes the old sdr_rx_reconfigure()+sdr_rx_start() pair, which
 * used to separately resync SPI1; that's now gd32_i2s_init_slave(rate)'s
 * job, done once, the same way for cold boot (sdr_rx_init(), which
 * just calls this with SDR_RX_BLOCK_SAMPLES) and every live switch.
 */
void sdr_rx_bringup(uint32_t block_samples);

/* Current active block size (SDR_RX_BLOCK_SAMPLES or
 * SDR_RX_BLOCK_SAMPLES_WFM) - what sdr_rx_poll_block_iq() and the
 * block hook actually deliver right now. Safe to poll anytime. */
uint32_t sdr_rx_get_block_samples(void);

/*
 * If a new complete block is available since the last call, copies
 * sdr_rx_get_block_samples() samples from the left ADC channel into
 * `i_out` and from the right ADC channel into `q_out`, already
 * de-interleaved, and returns 1. As of the byte-for-byte real capture
 * ported into aic3204_phase2_init(), left = I (IN2_L/IN2_R
 * differential) and right = Q (IN3_R/IN3_L differential) - see that
 * function's comments. If no new block is ready yet, returns 0 and
 * leaves both buffers untouched. Non-blocking. CALLER'S BUFFERS must
 * be at least SDR_RX_BLOCK_SAMPLES_MAX long - safe at either rate,
 * since sdr_rx_get_block_samples() tells you how much of that was
 * actually written this call.
 */
uint32_t sdr_rx_poll_block_iq(int16_t *i_out, int16_t *q_out);

/*
 * Registers a hook called from the RX DMA interrupt for EVERY
 * completed block, with a pointer to the raw interleaved half (L/R =
 * I/Q, sdr_rx_get_block_samples() frames). This is the real-time audio
 * path: unlike the polled display path, it never misses a block no
 * matter how long the main loop blocks. The hook runs in ISR context
 * - keep it bounded (the AM demodulator takes ~100us). Pass 0 to
 * unregister.
 */
void sdr_rx_set_block_hook(void (*hook)(const int16_t *raw_interleaved));

/* FERR diagnostics (see sdr_rx.c's own comments for each) - added
 * 05/08/2026 during the WFM FERR investigation, previously missing
 * from this header (called from main.c with no prototype visible -
 * fixed alongside the R27/R30 clock-start reordering). */
uint32_t sdr_rx_get_ferr_count(void);
void sdr_rx_reset_ferr_count(void);
uint8_t sdr_rx_last_block_corrupted(void);
uint8_t sdr_rx_get_ferr_snapshot(int16_t *i_out, int16_t *q_out, uint32_t n);

/*
 * El chequeo de bloque corrupto, encendido o apagado. Se APAGA en WFM: ahi
 * la I/Q va sin mezclar a 192 kHz y los saltos grandes entre muestras son
 * lo NORMAL, no una anomalia - ver la cuenta en sdr_rx.c. Encendido en
 * AM/SSB, donde la senal si va mezclada a banda base y es estrecha.
 */
void sdr_rx_set_chequeo_corrupcion(uint8_t on);

/* Cuantas veces la ISR del audio llego tan tarde que HTF y FTF estaban las
 * dos puestas y se perdio media captura. Antes esto no dejaba rastro. */
uint32_t sdr_rx_get_tarde(void);

/* Cuantos bloques se han dado por corruptos y se han descartado. Cada uno
 * repite 2,67 ms de audio ya sonado. */
uint32_t sdr_rx_get_descartes(void);

#endif /* SDR_RX_H */

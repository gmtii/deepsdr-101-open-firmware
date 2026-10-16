#ifndef FFT_H
#define FFT_H

#include <stdint.h>

/*
 * Real-input FFT for the spectrum/waterfall display, self-contained
 * and with NO libm dependency (this project links with nano.specs and
 * no -lm - same constraint that shaped the I2S test tone code).
 * Classic radix-2 Cooley-Tukey, Hann window, power converted to "dB"
 * via a fast bit-manipulation log2 approximation (not exact, but more
 * than good enough for coloring a waterfall/spectrum display - not
 * intended as a calibrated measurement).
 */

#define FFT_SIZE        256U   /* must be a power of two - was 512 @ 192kHz,
                                 * dropped to 128 @ 48kHz when AM/SSB/NFM first
                                 * moved off 192kHz, now 256 @ 96kHz - always in
                                 * lockstep with SDR_RX_BLOCK_SAMPLES (sdr_rx.h) -
                                 * keeps the SAME 375Hz/bin resolution, just over
                                 * a +/-48kHz span instead of 48kHz's +/-24kHz
                                 * (or the original 192kHz's +/-96kHz). */
#define FFT_BINS_USEFUL (FFT_SIZE / 2U)  /* real input: bins 0..Nyquist; the rest mirror these */
#define FFT_BINS_IQ     FFT_SIZE         /* complex I/Q input: every bin is meaningful */

/* Blank the DC bin (and its two Hann-spread neighbors) in the I/Q
 * spectrum, hiding the QSD/codec DC-offset spike that would otherwise
 * sit right on the VFO center line. Cosmetic only. */
#define FFT_IQ_DC_BLANK 1

/* Must be called once at startup (precomputes the twiddle table). */
void fft_init(void);

/*
 * Input: `samples` (FFT_SIZE real samples, e.g. int16_t already
 * de-interleaved from sdr_rx). Output: `db_out` (FFT_BINS_USEFUL
 * values, uncalibrated relative "dB" - higher means more energy).
 * Applies a Hann window internally before transforming.
 */
void fft_compute_db(const int16_t *samples, float *db_out);

/*
 * Complex I/Q transform for the panadapter display. Input: FFT_SIZE
 * samples of I and Q (same length, same block). Output: FFT_BINS_IQ
 * values in FFTSHIFT order - index 0 is -Fs/2 (24kHz below the VFO at
 * 48kHz sampling - was 96kHz @ 192kHz before 04/08/2026, see
 * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment), index FFT_SIZE/2 is DC
 * (the VFO itself), the last index is just under +Fs/2. Feeding this
 * straight to spectrum_draw() yields the classic centered panadapter:
 * VFO in the middle, lower frequencies left, higher right.
 */
void fft_compute_db_iq(const int16_t *i_samples, const int16_t *q_samples,
                        float *db_out);

#endif /* FFT_H */

#ifndef FT8_FFT1024_H
#define FT8_FFT1024_H

#include <stdint.h>

/*
 * Dedicated 1024-point real-input FFT for FT8's wider search window
 * (09/2026) - a full independent copy of fft.c's design (same radix-2
 * Cooley-Tukey, same Hann window, same libm-free fast log2 "dB"), NOT
 * a parameterized version of fft.c and NOT sharing its buffers.
 *
 * WHY A SEPARATE MODULE INSTEAD OF JUST BUMPING fft.h's FFT_SIZE:
 * fft.c's own FFT_SIZE is locked in step with the main spectrum's
 * SDR_RX_BLOCK_SAMPLES (currently 256, tuned for the live panadapter's
 * own real-time ISR-driven use) - fft.h's own history comment notes
 * it used to be 512 and was deliberately reduced, implying that size
 * mattered for the main spectrum's performance. Bumping it back up
 * just for FT8 risks that same regression for everyone, all the time,
 * not just in FT8 mode. A separate 1024-point instance, used only
 * while FT8 mode is active, isolates the cost entirely.
 *
 * RAM: this does NOT use fft.c's TCMRAM_BSS placement (TCM has no
 * room left, see the FT8 memory-budget history in
 * ft8_waterfall_adapter.h) - these buffers live in ordinary SRAM,
 * sized into the same "reuse the waterfall's own memory footprint
 * while FT8 mode is active" budget already established for
 * mag[]/the cascade. Approx size: re+im (1024*4*2=8192B) + twiddles
 * (512*4*2=4096B) + Hann (1024*4=4096B) + bit-reversal (1024*2=2048B)
 * = ~18.4KB.
 */
#define FT8_FFT_SIZE        1024U
#define FT8_FFT_BINS_USEFUL (FT8_FFT_SIZE / 2U) /* 512 - real input: bins 0..Nyquist, the rest mirror these */

/* Must be called once at startup (precomputes the twiddle/Hann/
 * bit-reversal tables) - alongside fft_init(), not instead of it. */
void ft8_fft1024_init(void);

/*
 * Input: `samples` (FT8_FFT_SIZE real samples). Output: `db_out`
 * (FT8_FFT_BINS_USEFUL values, uncalibrated relative "dB" - same
 * formula/scale as fft.c's fft_compute_db(), see
 * ft8_waterfall_adapter.h's FT8_ADAPTER_DB_OFFSET comment for why that
 * scale needs its own calibration offset regardless). Applies a Hann
 * window internally before transforming.
 */
void ft8_fft1024_compute_db(const int16_t *samples, float *db_out);

#endif /* FT8_FFT1024_H */

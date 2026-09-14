#ifndef FT8_WATERFALL_ADAPTER_H
#define FT8_WATERFALL_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "ft8/decode.h"

/*
 * Feeds our OWN ft8_fft1024.c (a dedicated 1024-point real-input FFT -
 * see that module's header for why it's separate from fft.c/the main
 * spectrum's own 256-point one) into ft8_lib's ftx_waterfall_t format,
 * without needing kiss_fft, monitor.c, or audio.c from ft8_lib at all
 * - decode.h's API expects a caller-filled magnitude buffer, nothing
 * more.
 *
 * ft8_lib decodes real, already-demodulated audio (like WSJT-X does
 * off a USB receiver's audio output), NOT raw I/Q - confirmed by
 * reading common/monitor.c and demo/decode_ft8.c.
 *
 * PARAMETER DERIVATION (09/2026 REVISION - widened from the original
 * 400Hz design, per the project owner, "vamos alla" on step 2 of the
 * memory-budget plan below):
 *
 *   sample_rate = 3200 Hz         (chosen - see ft8_decimator.h for why
 *                                  this specific rate needed a real
 *                                  rational resampler, not a simple
 *                                  integer decimator like the original)
 *   symbol_period = 0.16s         (FT8_SYMBOL_PERIOD, fixed by the protocol)
 *   block_size  = sample_rate * symbol_period = 512 samples/symbol
 *   time_osr    = 1               (A/B tested - see MEMORY BUDGET CONTEXT below)
 *   subblock_size = block_size / time_osr = 512 samples (=160ms) per feed
 *   freq_osr    = 2               (A/B tested, kept at ft8_lib's own default -
 *                                  see MEMORY BUDGET CONTEXT below for why this
 *                                  one specifically can't be reduced)
 *   nfft        = block_size * freq_osr = 1024  <-- == ft8_fft1024.h's FT8_FFT_SIZE, exactly
 *   native bin spacing at 3200Hz/1024pt = 3200/1024 = 3.125 Hz = 6.25Hz / freq_osr
 *     -> same ratio as the original 800Hz/256pt design (quadrupled both
 *        fs and nfft proportionally) - our own FFT's raw bins still sit
 *        exactly on ft8_lib's oversampled-bin grid, no extra interpolation
 *        needed (see ft8_waterfall_feed_subblock() and FT8_ADAPTER_RAW_BIN_STEP).
 *   max_blocks  = FT8_SLOT_TIME / symbol_period = 15.0/0.16 = 93 (truncated,
 *                 matches ft8_lib's own demo/monitor.c formula exactly - unchanged)
 *
 * FT8_ADAPTER_NUM_BINS = 256 (256 * 6.25Hz = 1600Hz search span) - the
 * ENTIRE useful output of a 1024-pt real FFT at 3200Hz
 * (FT8_FFT_BINS_USEFUL=512 raw bins / freq_osr(2) = 256 6.25Hz-bins),
 * same "cover everything available, not a narrow pre-tuned window"
 * choice as the original 400Hz design.
 */
#define FT8_ADAPTER_SAMPLE_RATE_HZ 3200 /* was 800 - see ft8_decimator.h for the resampler this now needs */
#define FT8_ADAPTER_TIME_OSR       1   /* A/B tested against the ft8_lib WAV corpus: TIME_OSR alone costs no measured decode sensitivity (still decodes the same reference signals as the original (2,2) design across 5 corpus files) */
#define FT8_ADAPTER_FREQ_OSR       2   /* kept at ft8_lib's own default - A/B testing showed THIS is the axis that actually matters: reducing it to 1 (alone or combined with TIME_OSR=1) lost a real decode (CT3IQ EI8GVB IO63, -9dB) that the (2,2) and (1,2) configs both still get */
#define FT8_ADAPTER_BLOCK_SIZE     512 /* = FT8_ADAPTER_SAMPLE_RATE_HZ * FT8_SYMBOL_PERIOD */
#define FT8_ADAPTER_SUBBLOCK_SIZE  512 /* = FT8_ADAPTER_BLOCK_SIZE / FT8_ADAPTER_TIME_OSR - one snapshot per full symbol period (160ms), since TIME_OSR=1 leaves no time-subdivision to fill */
#define FT8_ADAPTER_NFFT           1024 /* MUST equal ft8_fft1024.h's FT8_FFT_SIZE, enforced in the .c file */

/*
 * RAW_BIN_STEP: how many of ft8_fft1024_compute_db()'s raw 3.125Hz-
 * spaced output bins make up one ft8_lib "native" 6.25Hz bin - a fixed
 * property of THIS sample rate/FFT size pair (3200Hz / 1024pt: 3200/
 * 1024 = 3.125Hz/bin, same ratio the original 800Hz/256pt design had),
 * NOT of FT8_ADAPTER_FREQ_OSR - kept as its own constant (rather than
 * using FREQ_OSR directly in the encode formula) because this project
 * also tested FREQ_OSR=1 at one point (see MEMORY BUDGET CONTEXT
 * below), where the two concepts genuinely diverge; using RAW_BIN_STEP
 * explicitly is what made that test possible without corrupting the
 * frequency mapping. See ft8_waterfall_adapter.c's encode loop for how
 * this is actually used: src_bin = MIN_RAW_BIN + bin*RAW_BIN_STEP +
 * freq_sub*(RAW_BIN_STEP/FREQ_OSR).
 */
#define FT8_ADAPTER_RAW_BIN_STEP   2

/*
 * MEMORY BUDGET CONTEXT (09/2026) - two-step plan, per the project
 * owner, to widen the search window beyond the original 400Hz without
 * mag[] becoming impossibly large (block_stride = time_osr*freq_osr*
 * num_bins):
 *
 * STEP 1 (done first, fs still 800Hz/nfft still 256/window still
 * 400Hz) - A/B tested which oversampling axis actually costs decode
 * sensitivity if reduced from ft8_lib's own (2,2) default to 1:
 *
 *   (2,2), NUM_BINS=64  (400Hz): 93*2*2*64  = 23808 bytes (~23.2KB) - the ORIGINAL design
 *   (1,1), NUM_BINS=64  (400Hz): 93*1*1*64  =  5952 bytes (~5.8KB)  - LOST a real decode (CT3IQ EI8GVB IO63, -9dB)
 *   (2,1), NUM_BINS=64  (400Hz): 93*2*1*64  = 11904 bytes (~11.6KB) - ALSO lost that decode - FREQ_OSR is the sensitive axis
 *   (1,2), NUM_BINS=64  (400Hz): 93*1*2*64  = 11904 bytes (~11.6KB) - kept the decode, tested against 5 corpus files - the config actually shipped
 *
 * Conclusion: TIME_OSR is the "free" axis (halves mag[] with no
 * measured cost), FREQ_OSR is not.
 *
 * STEP 2 (this revision) - spends the RAM step 1 freed on an actually
 * wider window, via ft8_decimator.h's new rational resampler +
 * ft8_fft1024.h's bigger FFT:
 *
 *   (1,2), NUM_BINS=256 (1600Hz, fs=3200Hz/nfft=1024): 93*1*2*256 = 47616 bytes (~46.5KB)
 *
 * i.e. a 4x wider window (400Hz -> 1600Hz) for about double the
 * original (2,2)/400Hz design's RAM (23.2KB -> 46.5KB) - only possible
 * because step 1 already halved the per-bin cost first. Confirmed
 * against real hardware: two live decodes (9dB, 11dB - see this
 * session's own log) at the (1,2)/400Hz stage before committing to
 * this wider revision.
 */
#define FT8_ADAPTER_MAX_BLOCKS     93  /* = (int)(FT8_SLOT_TIME / FT8_SYMBOL_PERIOD) - unchanged */
#define FT8_ADAPTER_NUM_BINS       256 /* 1600Hz search span - see comment above */
#define FT8_ADAPTER_MIN_RAW_BIN    0   /* first ft8_fft1024_compute_db() output bin included in the search window - 0 = start right at DC-adjacent content */

/* ft8_fft1024_compute_db() uses the SAME formula as the original
 * fft.c-based design (10*log10(power) via a fast log2 approximation),
 * so the SAME calibration offset applies unchanged - see the session's
 * own dB-range measurements (real hardware: noise floor ~-104 to
 * -110dB, real signals topping out a few dB under 0dB after this
 * offset). If real decodes come up empty after any future hardware
 * change (gain staging, front-end revision, etc.), check the on-screen
 * "dB min/max" diagnostic (see main.c's FT8 slot-processing block) and
 * retune this - it's inherently an iterative, hardware-specific
 * calibration, not something derivable from first principles alone. */
#define FT8_ADAPTER_DB_OFFSET      -112.0f

/* Resets the sliding analysis window and the waterfall's block/time_sub
 * counters back to empty - call once at the START of each new 15s slot
 * capture, before the first ft8_waterfall_feed_subblock() call for
 * that slot. Does NOT need to re-zero the mag[] buffer itself (stale
 * old-slot data is harmless - it gets fully overwritten block by block
 * before num_blocks reaches it, and ftx_find_candidates()/
 * ftx_decode_candidate() only ever read blocks 0..num_blocks-1). */
void ft8_waterfall_reset(void);

/* Feed exactly FT8_ADAPTER_SUBBLOCK_SIZE new real audio samples,
 * captured at FT8_ADAPTER_SAMPLE_RATE_HZ (3200Hz, via
 * ft8_decimator.c's resampler) - call once per new subblock as audio
 * streams in (every 160ms), NOT once per full 15s slot: this is fully
 * incremental/streaming, so no separate raw-audio capture buffer is
 * needed anywhere upstream of this call - feed samples straight from
 * wherever the demod audio pipeline produces them at this rate.
 * Internally: shifts the new samples into a 1024-sample sliding
 * window, runs ft8_fft1024_compute_db() on it (Hann window applied
 * internally), and quantizes FT8_ADAPTER_NUM_BINS of the result into
 * the waterfall's mag[] array at the current (block, time_sub) slot,
 * using ft8_lib's own encoding convention (WF_ELEM_MAG_INT(x) = x,
 * decoded as x*0.5-120 - see decode.h) so ftx_find_candidates()/
 * ftx_decode_candidate() read it exactly as they would data from the
 * reference implementation.
 * A no-op past FT8_ADAPTER_MAX_BLOCKS blocks (the 15s slot is full) -
 * call ft8_waterfall_is_full() to know when to stop and decode. */
void ft8_waterfall_feed_subblock(const int16_t *audio_samples);

/* True once FT8_ADAPTER_MAX_BLOCKS blocks have been collected (the
 * full 15s slot's worth of analysis data is ready) - at that point,
 * pass ft8_waterfall_get() to ftx_find_candidates()/
 * ftx_decode_candidate() (see ft8/decode.h), then ft8_waterfall_reset()
 * before starting the next slot. */
bool ft8_waterfall_is_full(void);

/* Returns a pointer to the internal, statically-allocated
 * ftx_waterfall_t - valid for as long as no further
 * ft8_waterfall_feed_subblock()/ft8_waterfall_reset() call has been
 * made (there is exactly one instance, no dynamic allocation
 * anywhere in this adapter). */
const ftx_waterfall_t *ft8_waterfall_get(void);

/* Snapshot mag[] before resetting - see ft8_waterfall_adapter.c's own
 * comment for the full "why" (fixing the structural per-slot timing
 * overrun that no scheduling logic alone could solve). Call the
 * instant ft8_waterfall_is_full() is true, BEFORE ft8_waterfall_reset(). */
void ft8_waterfall_snapshot_mag(void);

/* Read-only pointer to the snapshot ft8_waterfall_snapshot_mag() made -
 * for building a scratch ftx_waterfall_t against, instead of the live
 * (already-reset-and-refilling) one ft8_waterfall_get() returns. */
const uint8_t *ft8_waterfall_get_mag_snapshot(void);

/* Calibration diagnostics for tuning FT8_ADAPTER_DB_OFFSET - see its
 * comment. Reports the raw (pre-offset, pre-quantization) dB range
 * ft8_fft1024_compute_db() actually produced across the whole slot
 * just captured. Meant for bring-up/tuning (print these once, adjust
 * the offset, rebuild), not a runtime feature. */
void ft8_waterfall_get_db_range(float *out_min, float *out_max);

/* Live spectrum readout - the most recent subblock's
 * ft8_fft1024_compute_db() output over our search window
 * (FT8_ADAPTER_NUM_BINS bins, one value per bin, offset-corrected dB,
 * same units/scale as what gets quantized into mag[]), plus a counter
 * that increments once per ft8_waterfall_feed_subblock() call so a
 * caller can tell when new data has actually arrived (roughly every
 * 160ms) without polling the data itself. */
void ft8_waterfall_get_live_spectrum(const float **out_db, int *out_n_bins);
uint32_t ft8_waterfall_get_update_counter(void);

/* Increments only when the cascade actually pushed a new row (every
 * FT8_WF_ROW_PERIOD subblocks, not every one - see that macro's
 * comment in ft8_waterfall_adapter.c) - poll THIS, not
 * ft8_waterfall_get_update_counter(), to know when to redraw the
 * cascade; using the general update counter would redraw far more
 * often than the data actually changes. */
uint32_t ft8_waterfall_get_row_counter(void);

/* Scrolling cascade history - out_buf[row*out_cols + col], row 0 =
 * newest (most recent subblock), increasing row = further in the
 * past, values are the SAME quantized bytes stored in mag[] (0-255,
 * see decode.h's WF_ELEM_MAG for the dB this represents). Pointer is
 * to the internal static buffer - valid until the next
 * ft8_waterfall_feed_subblock()/ft8_waterfall_reset() call, same
 * lifetime contract as ft8_waterfall_get(). */
void ft8_waterfall_get_history(const uint8_t **out_buf, int *out_rows, int *out_cols);

#endif /* FT8_WATERFALL_ADAPTER_H */

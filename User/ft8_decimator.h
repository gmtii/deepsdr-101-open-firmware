#ifndef FT8_DECIMATOR_H
#define FT8_DECIMATOR_H

#include <stdint.h>

/*
 * Rational resampler: 12kHz (demod_am.c's demodulated audio rate) ->
 * 3200Hz, feeding ft8_waterfall_adapter.c automatically. REPLACES the
 * original simple integer decimate-by-15 (12kHz->800Hz) - kept the
 * same public function names (ft8_decimator_reset/feed_sample) so
 * demod_am.c's tap point needed NO changes at all, only this module's
 * internals changed.
 *
 * WHY 3200Hz, AND WHY THIS NEEDED A REAL RESAMPLER (09/2026, per the
 * project owner, "vamos alla" on widening the search window beyond
 * 400Hz): 12000 has no integer decimation factor landing on a fs that
 * gives a power-of-two nfft together with FREQ_OSR=2 preserved (see
 * ft8_waterfall_adapter.h's memory-budget history for why FREQ_OSR=2
 * specifically must be kept - A/B tested, it costs real decode
 * sensitivity to drop it, unlike TIME_OSR). 3200Hz DOES: nfft =
 * (3200*0.16)*2 = 1024, a clean power of two (see ft8_fft1024.h) -
 * doubling the window from 400Hz to 1600Hz for a mag[] cost of ~46.5KB
 * (vs ~23.2KB for the ORIGINAL 400Hz/(2,2)-oversampled design - see
 * that header's own numbers).
 *
 * 12000/3200 = 15/4 in lowest terms - NOT an integer ratio, so a
 * simple decimate-only filter (like the old 800Hz design used) can't
 * reach it. This is a genuine rational resampler: interpolate by
 * L=4, then decimate by M=15, implemented as a direct polyphase
 * decomposition (not literally zero-stuffing and computing 48kHz's
 * worth of samples - that would be wasted CPU work for content that
 * gets thrown away on the way to decimating by 15 - see
 * ft8_decimator.c's own comment on the actual per-output-sample cost).
 *
 * Single prototype filter, 480 taps (120 per polyphase phase,
 * 480=4*120 exactly), Blackman-windowed-sinc, cutoff 1600Hz referenced
 * to the 48kHz virtual (post-interpolation) rate - simulated response
 * before committing to these coefficients: flat to ~12dB (=20*log10(4),
 * the L=4 gain compensating the interpolation zero-stuffing loss) up
 * to 1400Hz, -63dB by 2000Hz, better than -110dB from 4400Hz on
 * (comfortably covering every alias zone up to and beyond the
 * resampler's own output Nyquist).
 */
#define FT8_RESAMPLER_L          4    /* interpolation factor */
#define FT8_RESAMPLER_M          15   /* decimation factor - 12000*L/M = 3200Hz exactly */
#define FT8_DECIM_INPUT_RATE_HZ  12000 /* must match demod_am.c's post-decimation audio rate */

/* Call once when starting a fresh FT8 slot capture (alongside
 * ft8_waterfall_reset() - this does NOT call that itself, keep them
 * paired at the call site so it's obvious both halves of the capture
 * state reset together). Clears the resampler's input history and the
 * output-accumulation state. */
void ft8_decimator_reset(void);

/* Feed ONE new demodulated audio sample at FT8_DECIM_INPUT_RATE_HZ
 * (12kHz) - e.g. one element of demod_am.c's s_ssb_dec. Internally
 * runs the polyphase L=4/M=15 resampler, producing a new 3200Hz
 * output sample roughly every 3.75 input samples (not evenly - see
 * ft8_decimator.c for the exact bookkeeping), accumulates those into
 * a FT8_ADAPTER_SUBBLOCK_SIZE-sized buffer, and calls
 * ft8_waterfall_feed_subblock() automatically once that buffer fills -
 * the caller never needs to track any of these boundaries itself,
 * just keep feeding raw 12kHz samples one at a time, for as long as a
 * slot capture is running. */
void ft8_decimator_feed_sample(int16_t sample);

/* Convenience: feed N consecutive 12kHz samples in one call (e.g.
 * straight from demod_am.c's per-ISR audio block) instead of calling
 * ft8_decimator_feed_sample() N times yourself. */
void ft8_decimator_feed_block(const int16_t *samples, uint16_t n);

/* Call from the MAIN LOOP (NOT from an ISR) every iteration while an
 * FT8 slot capture is running - drains whichever resampled subblock(s)
 * ft8_decimator_feed_sample() has finished filling and runs the
 * actual FFT/encode/cascade work on them (ft8_waterfall_feed_subblock()).
 * This split exists because that work is too expensive to do
 * synchronously inside the audio DMA ISR ft8_decimator_feed_sample()
 * itself runs in - see this file's own comment on the double-buffer
 * handoff for why. Cheap to call when there's nothing ready (just
 * checks two flags), so calling it unconditionally every main-loop
 * iteration while FT8 mode is active is fine. */
void ft8_decimator_poll(void);

/* Total raw 12kHz input samples fed since the last ft8_decimator_reset()
 * - completely independent of the resampler's own internal
 * bookkeeping, added specifically to measure the true input delivery
 * rate directly (see ft8_decimator.c's declaration comment for the
 * full "why"): raw_sample_count / (elapsed real ms / 1000) should
 * read ~12000 if the audio pipeline genuinely delivers samples at the
 * rate this whole project assumes throughout. */
uint32_t ft8_decimator_get_raw_sample_count(void);

#endif /* FT8_DECIMATOR_H */

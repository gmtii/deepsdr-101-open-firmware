#ifndef RTTY_SCOPE_H
#define RTTY_SCOPE_H

#include <stdint.h>

/*
 * RTTY tuning scope - added 08/08/2026, per the project owner: the
 * main RF spectrum, even at its finest zoom (8X, Fs=12kHz, 256 IQ
 * bins -> 46.9Hz/bin), only gives ~3.6 bins of separation for a
 * standard 170Hz-shift RTTY signal - with the Hann window already in
 * fft.c (wider main lobe than a rectangular window, by design, to
 * control leakage), two comparable-amplitude tones that close
 * together visually merge into one blob rather than showing as two
 * distinct peaks. Confirmed against the project owner's own testing:
 * couldn't distinguish the two tones at 8X with a real 170Hz-shift
 * signal, could estimate ~850Hz would have been needed to see them
 * clearly with the RF spectrum as it stands.
 *
 * This is a SEPARATE, dedicated FFT - not a further RF zoom level -
 * on the AUDIO domain instead of RF: it reads s_ssb_dec, the exact
 * same already-12kHz SSB audio buffer rtty.c's decoder itself reads
 * (see demod_am.c's RTTY INTEGRATION comment) - so tuning against
 * this shows EXACTLY what the decoder sees, with no IF-offset or
 * RF-zoom-decimation approximation in between. At 512 points @
 * 12kHz = 23.4Hz/bin, a 170Hz shift is ~7.3 bins apart - comfortably
 * resolvable. Real input (not I/Q), so no need to distinguish +f
 * from -f the way the RF spectrum does - audio only has positive
 * frequencies 0..6kHz (Nyquist) to worry about.
 *
 * Own self-contained radix-2 FFT (same algorithm/style as fft.c,
 * deliberately NOT shared code - see fft.c's own local sin/cos/log2
 * approximations comment for why modules in this project stay
 * decoupled rather than share a common DSP utility file): different
 * size (512 vs fft.c's fixed 256), different input type (float audio
 * samples vs fft.c's int16 ADC samples), different consumer (a
 * one-off tuning aid, not the real-time waterfall) - similar enough
 * to look like duplication, different enough that sharing would mean
 * either bloating fft.c with a second size/input-type path or making
 * the real-time spectrum's hot code depend on this debug tool's
 * needs. Kept separate on purpose.
 *
 * *** REPLACES THE NORMAL RF SPECTRUM PANEL, DOESN'T COEXIST WITH IT
 * *** - see main.c's digi_panel_active()/rtty_scope_draw(): whenever
 * one of the RTTY-L/RTTY-U modes is selected (k_demod_modes[]/
 * menu_mode_preset_callback()), this panel takes over the same screen
 * area (SPEC_Y/SPEC_H/MAIN_W) the RF spectrum normally uses, and the
 * RF spectrum stops updating entirely for as long as that's the case
 * (main()'s main loop skips sdr_spectrum_waterfall_tick() while
 * active - see its own comment for why). Graduated from a
 * debug-build-only tool (the old RTTY_ENABLED build flag) to this
 * permanent mode-based integration on 08/08/2026, once the decoder +
 * scope concept were validated against a real signal.
 */

/*
 * MASTER SWITCH - previously RTTY_ENABLED (build flag); REMOVED
 * 08/08/2026, per the project owner, once the decoder/scope concept
 * was validated against a real signal (see rtty.c's/config.h's
 * comments for that story) and RTTY graduated from "debug-build-only
 * tool" to a real selectable mode - see main.c's k_demod_modes[]
 * (RTTY-L/RTTY-U entries) and menu_mode_preset_callback(). Always
 * compiled in now, exactly like rtty.c/rtty.h always were - see
 * this file's own top comment for the ~10KB static RAM this costs
 * (192KB SRAM total, still comfortable margin as of 08/08/2026, but
 * worth remembering next time RAM gets tight).
 */
#define RTTY_SCOPE_FFT_SIZE 512U
#define RTTY_SCOPE_BINS (RTTY_SCOPE_FFT_SIZE / 2U) /* real input: bins 0..Nyquist (6kHz) */

/*
 * Multi-window averaging - added 10/08/2026, per the project owner,
 * after noticing SPT (main.c's spatial line-smooth tile) barely
 * affected this panel: SPT smooths across NEIGHBORING COLUMNS within
 * one already-drawn frame, but the visible jitter here was mostly
 * TEMPORAL (frame to frame) - each rtty_scope_poll() used to display
 * a single raw 512-point window completely independent of the last,
 * with no frame-to-frame relationship at all (unlike the main RF
 * spectrum's own sdr_spectrum_waterfall_tick(), which already
 * averages up to SPECTRUM_MAX_FFT_PER_FRAME windows per displayed
 * frame for exactly this reason). Now does the same thing here:
 * rtty_scope_poll() accumulates RTTY_SCOPE_AVG_FRAMES raw windows'
 * power values before a frame is marked ready, so what actually
 * reaches the screen is a real statistical average, not just the
 * latest noisy sample.
 *
 * Trade-off, deliberately chosen with the project owner: this trades
 * away some responsiveness for a calmer trace. Each raw window is
 * RTTY_SCOPE_FFT_SIZE/12000Hz = ~43ms of audio - at
 * RTTY_SCOPE_AVG_FRAMES=3, a displayed frame only updates every
 * ~130ms instead of every ~43ms, so nudging SHIFT with the encoder
 * while watching the two peaks settle takes a little longer to
 * visually converge. 3 was picked as a middle ground (noticeably
 * calmer than 1, without turning the live encoder-tune feel sluggish)
 * - raise it for a calmer trace at the cost of more lag, or drop to 1
 * to fully restore the original every-window behavior.
 */
#define RTTY_SCOPE_AVG_FRAMES 1U

/* Precomputes twiddle/Hann/bit-reversal tables and resets state. Call
 * once at startup, same as fft_init()/rtty_init(). */
void rtty_scope_init(void);

/*
 * Feeds RTTY_BLOCK_SAMPLES(32, see rtty.h) of 12kHz audio - same
 * block size/rate/call site as rtty_process() (see demod_am.c's RTTY
 * INTEGRATION comment), called from the SAME place in the ISR. This
 * function itself does almost no work (just accumulates into a ring
 * until RTTY_SCOPE_FFT_SIZE samples are ready) - the actual FFT runs
 * from rtty_scope_poll() in the MAIN LOOP instead, never the ISR, same
 * "ISR sets minimal state, main loop does the heavy lifting" split as
 * everything else cross-context in this codebase (e.g. the RF-clip
 * flag) - a 512-point FFT is real work (roughly 4600 butterfly ops),
 * fine for a main-loop-paced debug tool, not something to risk adding
 * to the ISR's cycle budget for.
 */
void rtty_scope_feed(const float *audio, uint32_t n);

/*
 * Call from the main loop, same cadence as rtty_poll()/rf_agc_poll().
 * Runs the windowed FFT + magnitude computation IF a full window has
 * accumulated since the last call (see rtty_scope_feed()) - cheap
 * no-op otherwise. Sets the frame-ready flag rtty_scope_frame_ready()
 * reads.
 */
void rtty_scope_poll(void);

/* 1 if a new frame is ready to draw (cleared by rtty_scope_get_frame()). */
uint8_t rtty_scope_frame_ready(void);

/*
 * Returns a pointer to RTTY_SCOPE_BINS magnitude values (linear, NOT
 * dB - auto-normalized to this frame's own peak, i.e. always spans
 * the full display range regardless of actual signal strength; this
 * is a "where are the two peaks" tuning aid, not a calibrated
 * strength readout) and clears the ready flag. Bin 0 = DC, bin
 * RTTY_SCOPE_BINS-1 = Nyquist (6kHz) - see rtty_scope_hz_per_bin()
 * for converting a bin index to Hz.
 */
const float *rtty_scope_get_frame(void);

/* Hz per bin (12000.0f / RTTY_SCOPE_FFT_SIZE) - for the UI's Hz -> x
 * pixel mapping (e.g. drawing CONFIG_RTTY_MARK_HZ/SPACE_HZ markers). */
float rtty_scope_hz_per_bin(void);

/*
 * Discards any partially-accumulated average (see RTTY_SCOPE_AVG_
 * FRAMES's comment) without touching the FFT tables rtty_scope_init()
 * built - call this on a genuine "start fresh" transition (main.c
 * calls it alongside rtty_text_panel_reset(), only when actually
 * entering RTTY-L/RTTY-U from a different mode, NOT on every menu
 * open/close) so the first displayed frame of a new session is a
 * clean N-window average, not a blend that includes windows captured
 * before the transition.
 */
void rtty_scope_avg_reset(void);

#endif /* RTTY_SCOPE_H */

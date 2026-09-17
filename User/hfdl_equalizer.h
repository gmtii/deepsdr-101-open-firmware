#ifndef HFDL_EQUALIZER_H
#define HFDL_EQUALIZER_H

#include "arm_math.h"
#include <stdint.h>

/*
 * hfdl_equalizer - LMS adaptive equalizer (16/08/2026, "paso 2" round
 * 2 - see hfdl_data_segment.h's top comment for round 1's scope).
 * Ports dumphfdl's use of liquid-dsp's eqlms_cccf object (src/hfdl.c,
 * see this header's own SOURCE OF TRUTH section below for exact line
 * references from the project's own uploaded dumphfdl-master.zip).
 *
 * WHAT THIS IS: a short (HFDL_EQ_LEN=15 tap) complex adaptive FIR
 * filter, trained via the standard complex LMS (least mean-squares)
 * update rule against a KNOWN reference signal (HFDL's T_seq training
 * sequences - see hfdl_data_segment.h's EQ_TRAIN state), then applied
 * (taps frozen) to the DATA symbols in between training windows to
 * undo channel distortion (multipath/ISI) before those symbols are
 * demodulated.
 *
 * HONEST UNCERTAINTY (16/08/2026) - unlike every OTHER module in this
 * project (Costas, framer, deinterleaver, descrambler, CRC), this one
 * could NOT be verified against liquid-dsp's own source: eqlms_cccf is
 * a THIRD-PARTY library dumphfdl depends on, not part of the
 * dumphfdl-master.zip source tree itself, and only liquidsdr.org's API
 * reference page (interface signatures + one-line descriptions) was
 * reachable, not the actual eqlms_cccf.c implementation. What IS
 * confirmed from dumphfdl's real call sites (src/hfdl.c lines
 * ~495-496, ~717-732):
 *   - eqlms_cccf_create_lowpass(EQ_LEN, 0.45f) - EQ_LEN=15 taps,
 *     initialized as a lowpass filter (cutoff 0.45, normalized to
 *     sample rate) rather than starting from an identity/all-zero
 *     response.
 *   - eqlms_cccf_set_bw(0.1f) - a "learning rate"/adaptation-speed
 *     parameter, liquidsdr.org's own docs call it "bandwidth" (same
 *     naming convention this project's own hfdl_costas.h alpha/beta
 *     already uses for loop bandwidth parameters).
 *   - runs at 2 samples/symbol (fractionally-spaced - see
 *     hfdl_equalizer_push()'s comment), trains ONLY during EQ_TRAIN
 *     windows against the T_seq reference, but its output is APPLIED
 *     (not just trained) to every symbol, training or data alike.
 * What is NOT confirmed (liquidsdr.org's page documents the public
 * INTERFACE, not the internal math): the exact LMS update equation's
 * normalization (plain complex LMS vs a power-normalized variant),
 * and the exact lowpass-filter design (window function) used for
 * initialization. THIS implementation uses the textbook complex LMS
 * update (w[k] += mu * conj(x[k]) * error, unnormalized, mu = the
 * "bandwidth" parameter directly) and initializes taps as a unit
 * IMPULSE at the NEWEST-sample tap position (see
 * hfdl_equalizer_push()'s struct-field comment for the delay-line
 * indexing convention this depends on - fixed 16/08/2026 round 4
 * after real captures showed a center-tap impulse was actually a
 * 7-symbol-delayed pass-through, not a true one, given this module's
 * own indexing) rather than attempting to replicate an
 * unconfirmed specific lowpass design. Both choices are reasonable,
 * standard defaults, but are NOT bit-exact matches to dumphfdl's
 * actual liquid-dsp output - hfdl_equalizer_selftest() therefore
 * validates CONVERGENCE BEHAVIOR against a known synthetic channel
 * (does training measurably reduce equalization error), not bit-exact
 * agreement with a reference implementation, unlike this project's
 * other modules' self-tests.
 *
 * SOURCE OF TRUTH (dumphfdl real numbers, all confirmed from
 * src/hfdl.c): HFDL_EQ_LEN=15 (dumphfdl's EQ_LEN), mu=0.1 (dumphfdl's
 * eqlms_cccf_set_bw() argument) - see hfdl_data_segment.h for
 * M2_LEN/T_LEN/DATA_FRAME_LEN, already confirmed and wired there.
 *
 * NOT YET DONE this round (deferred, same "step by step" spirit as
 * the rest of this session):
 *   - wiring this into demod_am.c's post-LOCKED symbol loop (needs
 *     hfdl_data_segment.c extended to actually CALL this instead of
 *     just counting symbols).
 *   - the T_seq[bitmask & 1][...] global-180-degree-ambiguity
 *     selection dumphfdl does (see this header's WHAT THIS IS section)
 *     - this project's A1/A2/M1 framer resolves that ambiguity
 *     independently PER BLOCK now (non-coherent block combining, see
 *     hfdl_framer.h), which doesn't hand back a single global answer
 *     the way dumphfdl's original single-coherent-block correlator
 *     did. Needs its own design work, not a direct port - flagged
 *     here again, same as hfdl_data_segment.h, so it doesn't get lost.
 *   - the decision-directed Costas phase adjustment dumphfdl performs
 *     using modem_get_demodulator_phase_error() during EQ_TRAIN/DATA -
 *     a separate concern from equalization itself.
 */

#define HFDL_EQ_LEN 15u
#define HFDL_EQ_DEFAULT_MU 0.1f /* dumphfdl: eqlms_cccf_set_bw(c->eq, 0.1f) - see top comment */

/* MU SWEEP OVERRIDE (19/08/2026) - isolating the decision-directed
 * Costas coupling (HFDL_EQ_DISABLE_COSTAS_COUPLING, see demod_am.c)
 * reduced real-capture training error by ~25-40% across mean/median/
 * max, consistently, but did NOT resolve it - the immediate post-
 * first-window error jump persisted either way. mu=0.1 is dumphfdl's
 * OWN value (see top comment's SOURCE OF TRUTH), but the "HONEST
 * UNCERTAINTY" section already flags that this project's LMS update
 * form/normalization isn't confirmed bit-exact against liquid-dsp's
 * internal implementation - with the input-power normalization still
 * imperfect (measured RMS 0.72-0.94 across real captures, not exactly
 * 1.0) and this project running symbol-spaced instead of dumphfdl's
 * fractionally-spaced (see hfdl_equalizer_push()'s own comment), 0.1
 * is not guaranteed to be the right operating point here even if it's
 * the right one for dumphfdl's actual (unconfirmed) implementation.
 *
 * CONFIRMED (19/08/2026, same session, real capture, 11 LOCKED
 * segments/5 completed DONE, HFDL_EQ_DISABLE_COSTAS_COUPLING=1):
 * mu=0.03f (about 1/3 of dumphfdl's 0.1) ELIMINATES the divergence -
 * "eq MSE, all EQ_TRAIN symbols" dropped from ~2.5x the first-window
 * MSE (mu=0.1, both with and without Costas coupling) to ~1.08x
 * (1.127 vs 1.041) - within noise of the un-adapted baseline, not
 * still climbing. Divergence-trace max sample dropped 28.77 -> 3.61
 * over the same three runs. mu=0.1 was genuinely too aggressive for
 * this project's symbol-spaced/imperfectly-normalized real-signal
 * operating point - the LMS stability-margin concern in this file's
 * own SOURCE OF TRUTH section (2/HFDL_EQ_LEN=0.133, uncomfortably
 * close to 0.1 once real non-i.i.d. noise and imperfect normalization
 * are accounted for) was the right track. NOT yet updated as the new
 * HFDL_EQ_DEFAULT_MU (leaving that dumphfdl-sourced value alone,
 * still documented as such, HFDL_EQ_ACTIVE_MU is what actually gets
 * used) - one confirming real capture is a good signal, not yet
 * "swept and settled" the way hfdl_costas.c's own EMA lambda was (see
 * that file's history for what a properly swept parameter's
 * documentation looks like) - worth a couple more real captures at
 * 0.03f (and maybe a quick check whether the MSE floor near ~1.0 is
 * mostly Costas-coupling residual, mostly real channel SNR, or mostly
 * the still-imperfect RMS normalization, before calling it settled).
 *
 * Overridable at build time (`make HFDL_EQ_MU_OVERRIDE=0.03f` etc,
 * passed straight through as a float literal) purely to test whether
 * training error is sensitive to mu at all - a cheap, zero-RAM-cost
 * check (this only changes what value gets written into the ALREADY-
 * EXISTING mu field at init, not a new field) before assuming
 * something more structural. Default (no override) keeps
 * HFDL_EQ_DEFAULT_MU, current documented behavior. */
#ifdef HFDL_EQ_MU_OVERRIDE
#define HFDL_EQ_ACTIVE_MU HFDL_EQ_MU_OVERRIDE
#else
#define HFDL_EQ_ACTIVE_MU HFDL_EQ_DEFAULT_MU
#endif

typedef struct {
	/* Tap weights - real/imaginary pairs, index 0..HFDL_EQ_LEN-1.
	 * Adapted by hfdl_equalizer_step(), applied by
	 * hfdl_equalizer_execute(). */
	float32_t taps_i[HFDL_EQ_LEN];
	float32_t taps_q[HFDL_EQ_LEN];

	/* Delay line (the equalizer's own internal sample history) -
	 * index 0 is the OLDEST sample still in the window, HFDL_EQ_LEN-1
	 * is the MOST RECENT (the one hfdl_equalizer_push() just added).
	 * Shifted (not a circular buffer) on every push - HFDL_EQ_LEN=15
	 * is small enough that a plain shift is simpler to get right than
	 * a circular-index scheme, and this runs at 2 samples/symbol (see
	 * hfdl_equalizer_push()'s comment) which is nowhere near a real
	 * throughput concern on this hardware. */
	float32_t delay_i[HFDL_EQ_LEN];
	float32_t delay_q[HFDL_EQ_LEN];

	float32_t mu; /* LMS adaptation step size - see top comment's HONEST UNCERTAINTY section */

	/* INPUT AMPLITUDE NORMALIZATION (16/08/2026, round 6 - real
	 * captures confirmed AGC's delivered amplitude does NOT reliably
	 * sit at HFDL_T_SEQ's own +-1.0f scale - measured 0.42-0.59 across
	 * 3 real segments, not close to 1.0. Same root-cause family as
	 * hfdl_framer.c's own RMS-normalization fix (see that file's
	 * history) - AGC's actual settled level just isn't something this
	 * project should assume a fixed value for anywhere. Tracks a
	 * running EMA of input POWER (i^2+q^2), and hfdl_equalizer_push()
	 * divides incoming samples by sqrt(this) before storing them -
	 * keeps the equalizer's whole internal delay line (and therefore
	 * its output, and therefore the training-error/MSE comparison
	 * against T_seq) operating near unit RMS regardless of whatever
	 * absolute level AGC actually delivers, adaptively (handles level
	 * DRIFT too, not just one fixed scale factor measured once). */
	float32_t input_power_avg;
} hfdl_equalizer_t;

/* Resets state (delay line to all-zero) and sets taps to a unit
 * impulse at the NEWEST-sample tap position (index HFDL_EQ_LEN-1 -
 * genuine pass-through starting point, see hfdl_equalizer.c's
 * comment for the bug this fixed on 16/08/2026 round 4: a center-tap
 * impulse silently meant a 7-symbol-delayed pass-through instead,
 * given this module's push()/execute() indexing convention). mu: LMS
 * step size - pass HFDL_EQ_DEFAULT_MU unless there's a specific
 * reason not to. */
void hfdl_equalizer_init(hfdl_equalizer_t *eq, float32_t mu);

/* Pushes ONE new complex symbol into the equalizer's delay line.
 * CHANGED (16/08/2026, round 6) to normalize by the running input-
 * power estimate first - see hfdl_equalizer_t's input_power_avg field
 * comment for why. This means the VALUES stored in the delay line
 * (and therefore hfdl_equalizer_execute()'s output) are now on a
 * roughly-unit-RMS scale, NOT the raw i_in/q_in scale the caller
 * passed in - callers comparing against a KNOWN +-1.0f-scale
 * reference (like HFDL_T_SEQ) don't need to do any of their own
 * separate amplitude normalization anymore.
 *
 * DELIBERATE DEVIATION FROM DUMPHFDL - RESOLVED (25/08/2026,
 * "ecualizador T/2" round) - this project's own hfdl_symsync now
 * produces a genuine second sample per symbol at T/2 spacing (see
 * hfdl_symsync_step()'s own comment), Costas-corrected via
 * hfdl_costas_derotate_only() (see hfdl_demod_chain.c's own comment
 * on the exact call order), so this module now runs the SAME
 * fractionally-spaced push/execute pattern dumphfdl's real eqlms_cccf
 * does: hfdl_equalizer_push() is called TWICE per symbol (the T/2
 * sample first - it is chronologically earlier - then the main T
 * sample), hfdl_equalizer_execute()/hfdl_equalizer_step() still ONCE
 * per symbol, using whatever the delay line holds after both pushes
 * (see demod_am.c's real integration loop for the exact call order).
 * HFDL_EQ_LEN=15 (dumphfdl's own real number, see this header's
 * SOURCE OF TRUTH section) now spans ~7.5 symbols of channel memory,
 * not 15 - matching dumphfdl's actual design, not the longer-but-
 * symbol-spaced approximation this project used before this round.
 * The push()/execute()/step() functions below are UNCHANGED at the
 * API level - only how often and in what order the caller invokes
 * them changed - so nothing about the LMS update itself, the gain-
 * renormalization fix, or the input-power normalization needed to
 * change for this. NOT yet confirmed on real hardware. */
void hfdl_equalizer_push(hfdl_equalizer_t *eq, float32_t i_in, float32_t q_in);

/* Computes the equalizer's current output (dot product of taps against
 * the delay line, using the CURRENT tap values - does NOT push a new
 * sample, does NOT adapt taps) - same push/execute separation
 * dumphfdl's eqlms_cccf_push()/eqlms_cccf_execute() calls keep
 * separate (see top comment - push happens every sample, execute only
 * every OTHER sample after the symsync_out_idx&1 decimation). */
void hfdl_equalizer_execute(const hfdl_equalizer_t *eq, float32_t *i_out, float32_t *q_out);

/* Adapts the taps by ONE step of the standard complex LMS update,
 * given the KNOWN desired output `d` (e.g. a T_seq training symbol)
 * and the equalizer's own actual output `y` (from
 * hfdl_equalizer_execute(), computed with the taps BEFORE this
 * update). error = d - y; taps[k] += mu * conj(delay[k]) * error for
 * every k - see top comment's HONEST UNCERTAINTY section for why this
 * exact form (not necessarily liquid-dsp's own internal normalization)
 * is what's implemented. Only call this during EQ_TRAIN windows
 * (dumphfdl never calls eqlms_cccf_step() during DATA_1/DATA_2 - taps
 * stay frozen, only execute() runs, on data symbols). */
void hfdl_equalizer_step(hfdl_equalizer_t *eq, float32_t d_i, float32_t d_q, float32_t y_i, float32_t y_q);

/* Host-testable self-test (same "check before trusting" shape as
 * every other module in this project) - see top comment's HONEST
 * UNCERTAINTY section for why this validates CONVERGENCE BEHAVIOR
 * against a known synthetic 2-tap ISI channel (does training reduce
 * equalization error substantially over a realistic training-window
 * length) rather than bit-exact agreement with liquid-dsp, which
 * couldn't be confirmed this round. Returns 1 if the equalizer
 * measurably converges (post-training error well below pre-training
 * error against a distorted signal), 0 otherwise. */
uint8_t hfdl_equalizer_selftest(void);

#endif /* HFDL_EQUALIZER_H */

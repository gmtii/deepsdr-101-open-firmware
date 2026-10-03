#ifndef HFDL_COSTAS_H
#define HFDL_COSTAS_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL Costas loop (carrier phase/frequency tracking) - third DSP
 * block of the demod chain, conceptually feeding from
 * hfdl_matched_filter.c's output - but see the IMPORTANT rate note
 * below before wiring it in anywhere.
 *
 * IMPORTANT - OPERATING RATE: in dumphfdl's own pipeline (src/hfdl.c,
 * szpajder/dumphfdl, GPL-3.0-or-later), the Costas loop runs on
 * samples AFTER symbol timing recovery (the symsync stage) - i.e.
 * roughly once per SYMBOL (~1800/sec), NOT on every raw audio sample
 * (12000/sec). This module is written rate-agnostic (it just
 * processes whatever samples you feed it, one call per sample), but
 * it should NOT be wired to run on the full 12kHz audio stream - that
 * would be ~6.7x more sinf()/cosf() calls per second than necessary,
 * for no benefit (a Costas loop tracking residual carrier error only
 * needs to see one sample per symbol once timing is locked). This
 * project's own symbol timing recovery stage doesn't exist yet (see
 * the project's HFDL_fase2_arquitectura document, step 7 of the
 * implementation order) - THIS is the reason this module isn't wired
 * into anything yet, only self-tested standalone.
 *
 * ALGORITHM: classic decision-directed BPSK Costas loop (2nd order:
 * proportional + integral, i.e. a standard PI loop filter) - NOT a
 * generic M-PSK phase detector. dumphfdl uses liquid-dsp's generic
 * modem_get_demodulator_phase_error(), which handles BPSK/QPSK/8PSK
 * uniformly; this module deliberately starts BPSK-only, matching the
 * project's staged plan of validating against the sigidwiki.com
 * 300bps/600bps samples first (both BPSK per that page) before
 * attempting QPSK/8PSK. Per-sample algorithm:
 *   1. Derotate the input sample by the current phase estimate:
 *        y = x * e^{-j*phase}
 *   2. BPSK phase-error (decision-directed): err = sign(I_y) * Q_y
 *   3. Loop filter (2nd order PI): freq += beta*err; phase += freq + alpha*err
 * alpha/beta default to dumphfdl's own constants (alpha=0.1,
 * beta=0.047*alpha^2) - see HFDL_phy_layer_params_dumphfdl document,
 * section 8, for that provenance. These are a reasonable starting
 * point, not something guaranteed correct for this project's own
 * matched-filter/timing-recovery chain without re-tuning against a
 * real or sigidwiki.com signal once the full chain exists.
 */

/* Both overridable at build time (18/09/2026), see the Makefile's HFDL knobs. */
#ifndef HFDL_COSTAS_DEFAULT_ALPHA
#define HFDL_COSTAS_DEFAULT_ALPHA 0.1f
#endif
#ifndef HFDL_COSTAS_DEFAULT_BETA
#define HFDL_COSTAS_DEFAULT_BETA  (0.047f * HFDL_COSTAS_DEFAULT_ALPHA * HFDL_COSTAS_DEFAULT_ALPHA)
#endif

typedef struct {
	float32_t phase; /* current phase estimate, radians */
	float32_t freq;  /* current frequency estimate, radians/sample - the loop's integrator state */
	float32_t alpha; /* proportional gain */
	float32_t beta;  /* integral gain */

	/* Coarse frequency acquisition aid (16/08/2026) - see
	 * hfdl_costas_get_coarse_freq_estimate()'s comment for the
	 * why/how. Runs continuously alongside the fine loop above, on
	 * the RAW (pre-derotation) input - completely independent of
	 * phase/freq, so it isn't corrupted by whatever the fine loop's
	 * current (possibly still-wrong) state is. */
	float32_t coarse_prev_sq_i, coarse_prev_sq_q; /* previous sample, squared */
	float32_t coarse_acc_i, coarse_acc_q;         /* EMA of squared(k)*conj(squared(k-1)) */
} hfdl_costas_t;

/* Configures the loop's gains and resets phase/freq to zero. */
void hfdl_costas_init(hfdl_costas_t *c, float32_t alpha, float32_t beta);

/* Resets phase/freq to zero without touching alpha/beta - call at the
 * start of a new HFDL burst/frame (each burst's carrier phase is
 * unrelated to the previous one - no reason to carry state over).
 * Also resets the coarse-acquisition accumulator (see below) - a new
 * burst's residual carrier is unrelated to whatever the accumulator
 * saw during a previous one. */
void hfdl_costas_reset(hfdl_costas_t *c);

/* Processes one complex sample: derotates by the current phase
 * estimate (writing the result to i_out/q_out - safe to alias
 * i_out==&i_in-as-lvalue style in-place use at the call site, this
 * function itself just takes plain floats by value/pointer-out, no
 * buffer aliasing concerns), computes the BPSK phase error, and
 * updates phase/freq via the loop filter. See the module header
 * comment for the operating-rate note - call this once per SYMBOL,
 * not once per raw audio sample. Also feeds the coarse-acquisition
 * accumulator (see hfdl_costas_get_coarse_freq_estimate()) on the
 * same RAW i_in/q_in, every call, unconditionally - cheap (a
 * handful of multiplies, no trig), so no reason to gate it. */
void hfdl_costas_step_bpsk(hfdl_costas_t *c, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out);

/* DEROTATE-ONLY (25/08/2026, "ecualizador T/2" round, pieza 3) - the
 * SAME y = x * e^{-j*phase} rotation hfdl_costas_step_bpsk() applies
 * internally, using the loop's CURRENT phase estimate, but WITHOUT
 * computing a phase error, WITHOUT updating phase/freq, and WITHOUT
 * feeding the coarse-acquisition accumulator - `c` is only read, never
 * written (hence const).
 *
 * WHY THIS EXISTS: hfdl_symsync_step()'s T/2 tap (see that module's
 * own comment) produces a second sample per symbol, taken exactly
 * half a symbol EARLIER than the main (decision) sample - needed so
 * hfdl_equalizer.c's fractionally-spaced push/execute pattern (see
 * that module's own "DELIBERATE DEVIATION FROM DUMPHFDL" comment,
 * now resolved) gets both taps phase-corrected. It should NOT go
 * through hfdl_costas_step_bpsk() itself: that function's
 * decision-directed phase error is only meaningful for the ON-TIME
 * (main) sample - it's the one at the actual symbol decision instant,
 * where sign(I) is a real bit decision. The T/2 sample sits mid-
 * symbol (deliberately, by construction - see hfdl_symsync_step()'s
 * own comment), so "sign(I)*Q" computed on it would not be a genuine
 * decision-directed error, and calling step_bpsk() on it would (a)
 * update the loop filter twice per symbol on inputs of very different
 * reliability, and (b) corrupt the coarse-acquisition accumulator,
 * which assumes one sample per symbol.
 *
 * CALL ORDER PER SYMBOL: call this FIRST, on the T/2 sample, using
 * the loop's phase as carried over from the PREVIOUS symbol (correct,
 * since the T/2 sample is chronologically the EARLIER of the two -
 * see hfdl_symsync_step()'s comment) - THEN call
 * hfdl_costas_step_bpsk() as before on the main (T) sample, which
 * both derotates it AND advances the loop for the NEXT symbol. */
void hfdl_costas_derotate_only(const hfdl_costas_t *c, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out);

/* Coarse frequency acquisition aid. PROBLEM: the fine loop above
 * starts from freq=0 every burst and needs many symbols to integrate
 * its way to the true residual carrier frequency (beta is
 * deliberately small for steady-state tracking, not fast pull-in) -
 * real captures showed it still hadn't converged by the time
 * A2_SEARCH's ~127-symbol window ran out, capping A2's correlation
 * score well under threshold (see the project's session notes,
 * 16/08/2026, for the measured relationship between residual
 * frequency and A2 score - a synthetic test showed a sharp cliff
 * around 0.01-0.02 rad/symbol).
 *
 * FIX: a classic squaring estimator, cheap enough to run every
 * symbol unconditionally. For real BPSK data bits b(k)=+-1 on a
 * carrier with residual (radian/symbol) frequency error f:
 *   x(k) = b(k) * e^{j(phi + f*k)}
 *   x(k)^2 = b(k)^2 * e^{j*2(phi + f*k)} = e^{j*2(phi + f*k)}
 * (b(k)^2 = 1 always - squaring cancels the BPSK modulation
 * entirely, regardless of what the actual bits are, leaving a pure
 * tone at 2f). hfdl_costas_step_bpsk() accumulates an EMA of
 * x(k)^2 * conj(x(k-1)^2) - IN THEORY (no noise) is a phasor at
 * angle 2f, so freq_estimate = 0.5 * angle(accumulator). With noise
 * this estimate is itself noisy sample-to-sample, hence the EMA
 * (time-averages it out) rather than a single delay-multiply.
 *
 * USE: call this once, right when hfdl_preamble_sync transitions
 * A1_SEARCH -> A2_SEARCH (a real A1 hit - the accumulator has been
 * seeing real signal for the preceding ~127 symbols by then, not
 * burst-detector noise), then feed the result into
 * hfdl_costas_seed_freq() to "kick" the fine loop's integrator
 * straight to roughly the right value instead of leaving it to find
 * its own way there - the fine loop keeps running afterward exactly
 * as before, just starting from a much better initial guess. */
float32_t hfdl_costas_get_coarse_freq_estimate(const hfdl_costas_t *c);

/* DIAGNOSTIC (16/08/2026, round 4 - the wrap-ambiguity check) - the
 * accumulator's own magnitude, |coarse_acc| = sqrt(i^2+q^2). Real
 * captures showed hfdl_costas_get_coarse_freq_estimate() jumping
 * wildly (e.g. -1.22 -> +1.43 rad/symbol) between separate A1 hits
 * WITHIN the same burst (same physical signal, same true residual,
 * accumulator never reset between them) - several of those readings
 * sit right at +-pi/2 (~1.57), the edge of this estimator's
 * unambiguous range (0.5*atan2() folds the true 2f angle from
 * [-pi,pi) down to [-pi/2,pi/2), so a residual actually near that
 * edge can flip between the two representations with only a little
 * noise pushing the underlying angle across the pi wrap point) -
 * suspected wrap artifact, not necessarily genuine measurement noise.
 * This magnitude is the missing piece to tell the two apart: a LOW,
 * noisy magnitude means the accumulator itself hasn't really
 * converged (genuine noise, matches the existing EMA-lambda sweep's
 * framing) - a STEADY, reasonably large magnitude with the ANGLE
 * flipping is the wrap artifact instead, and points at a completely
 * different fix (unwrap using the previous reading as a continuity
 * reference, or use hfdl_costas_get_coarse_freq_estimate_raw_angle()
 * below and unwrap in radians/2-of-2 space before halving) rather
 * than "the estimator needs more averaging". */
float32_t hfdl_costas_get_coarse_freq_estimate_magnitude(const hfdl_costas_t *c);

/* DIAGNOSTIC (16/08/2026, same investigation) - the FULL, un-halved
 * angle atan2(coarse_acc_q, coarse_acc_i), i.e. the raw angle of 2f
 * before hfdl_costas_get_coarse_freq_estimate() divides it down to
 * f. Logging this alongside the magnitude above and the existing
 * halved estimate lets the next capture show directly whether a
 * seed-to-seed jump corresponds to this angle crossing +-pi (wrap)
 * or to the magnitude collapsing near zero (genuine noise/no lock). */
float32_t hfdl_costas_get_coarse_freq_estimate_raw_angle(const hfdl_costas_t *c);

/* Decision-directed / genie-aided external phase correction
 * (16/08/2026, "paso 2" round 5) - see hfdl_costas.c's comment for
 * the full reasoning, scope, and the important architectural caveat
 * about this stacking with (not replacing) hfdl_costas_step_bpsk()'s
 * own internal correction. phase_err: SIGNED error term in the SAME
 * style hfdl_costas_step_bpsk() already uses internally
 * (sign(reference) * quadrature_component) - positive means the loop
 * needs to advance phase further, negative means less. */
void hfdl_costas_adjust(hfdl_costas_t *c, float32_t phase_err);

/* Directly overwrites the fine loop's frequency integrator (does NOT
 * touch phase - the coarse estimator above has no phase information,
 * only frequency, so the fine loop's own phase tracking is left to
 * catch up normally). See hfdl_costas_get_coarse_freq_estimate()'s
 * comment for the intended call site/sequencing. */
void hfdl_costas_seed_freq(hfdl_costas_t *c, float32_t freq);

/* Runs the module's standalone self-test: a closed-loop convergence
 * simulation (a legitimate, standard way to validate a PLL/Costas
 * loop's correctness, unlike the earlier pure-digital modules which
 * needed external cross-checks - see the module header's Python
 * simulation reference this was validated against during
 * development). Feeds ~2000 synthetic BPSK symbols through a channel
 * with a known initial phase offset and a known constant frequency
 * offset, and checks: (1) the average phase-error magnitude in the
 * last 50 symbols is far smaller than in the first 50 (the loop
 * visibly converges), and (2) the loop's final frequency estimate is
 * close to the channel's true frequency offset (the loop correctly
 * identifies not just phase but frequency). Zero dependency on real
 * hardware or a real HFDL signal - like the other DSP-block
 * self-tests, this proves the loop mechanics are correct, not that
 * alpha/beta are definitely well-tuned for the real chain once it
 * exists. */
uint8_t hfdl_costas_selftest(void);

#endif /* HFDL_COSTAS_H */

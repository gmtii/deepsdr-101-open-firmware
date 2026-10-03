#ifndef HFDL_AGC_H
#define HFDL_AGC_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL automatic gain control - fills the "MISSING PIECES: AGC" gap
 * called out in hfdl_demod_chain.h, sitting between
 * hfdl_rational_resampler.c's output and hfdl_matched_filter.c's
 * input (same slot dumphfdl's agc_crcf occupies in its own pipeline,
 * per hfdl_demod_chain.h's block-diagram comment).
 *
 * PROVENANCE - READ THIS BEFORE ASSUMING IT MATCHES THE REST OF THE
 * CHAIN'S PEDIGREE: unlike hfdl_rational_resampler.c/hfdl_symsync.c/
 * hfdl_costas.c (each ported line-by-line from a real, compiled
 * liquid-dsp reference, per their own module headers), this is NOT a
 * port of liquid-dsp's real agc_crcf object. That object's actual
 * loop bandwidth/gain-limit constants as used by dumphfdl's own
 * src/hfdl.c were not available to verify at the time this was
 * written (11/08/2026) - rather than guess a number and present it as
 * if it were dumphfdl's real constant, this is a simpler, independent
 * design: a single-pole moving-average power estimator driving a
 * direct (non-integrating) gain computation, clamped to
 * [HFDL_AGC_MIN_GAIN, HFDL_AGC_MAX_GAIN]. Functionally it does the
 * same job (normalize amplitude into the matched filter regardless of
 * input signal strength), but it is NOT liquid's log-domain
 * proportional+integral loop with 6-state squelch - if that exact
 * behavior is ever needed (e.g. to match dumphfdl's squelch semantics
 * for burst detection), this module should be replaced, not assumed
 * equivalent.
 *
 * ALGORITHM:
 *   1. power[n]     = i[n]^2 + q[n]^2
 *   2. power_avg    = (1-bw)*power_avg + bw*power[n]   (single-pole IIR)
 *   3. gain         = target_amplitude / sqrt(power_avg + floor)
 *      clamped to [HFDL_AGC_MIN_GAIN, HFDL_AGC_MAX_GAIN]
 *   4. i_out, q_out = gain * i[n], gain * q[n]
 *
 * power_avg (pre-gain, i.e. the raw signal energy reaching this
 * stage) is exposed via hfdl_agc_get_power_avg() specifically so a
 * caller (e.g. demod_am.c's HFDL probe) can print it alongside
 * symsync/Costas state - a power_avg stuck at ~0 regardless of
 * frequency/antenna points at a front-end/wiring problem upstream of
 * this module, which is a different failure mode than "DSP chain
 * receives real energy but doesn't lock" and worth being able to
 * distinguish from a UART dump.
 */

/* Target output amplitude (RMS-ish, per the sqrt(power) normalization
 * above) this AGC drives towards. 1.0f chosen to match the scale the
 * downstream matched filter/symsync/Costas constants were informally
 * assumed at (see hfdl_costas.h's own note that alpha/beta are "not
 * guaranteed correct... without re-tuning against a real signal") -
 * changing this is equivalent to a global input scale change for
 * everything downstream. */
#define HFDL_AGC_TARGET_AMPLITUDE 1.0f

/* Loop bandwidth for the single-pole power estimator - OWN CHOICE,
 * not from any reference implementation (see module header). Picked
 * to average over roughly ~1-2 HFDL symbol periods at the resampler's
 * 5400Hz output rate (9 samples/call here) - fast enough to track a
 * burst turning on, slow enough not to fight the matched filter/
 * symsync loop underneath it. Revisit once real-signal captures are
 * available - see the project's HFDL_fase2 session notes. */
#ifndef HFDL_AGC_BW /* overridable at build time, see the Makefile (18/09/2026) */
#define HFDL_AGC_BW 0.05f
#endif

/* Gain clamps - prevent runaway gain during silence/near-zero input
 * (which would otherwise amplify quantization/thermal noise towards
 * infinity as power_avg -> 0) and prevent numerical blowup on an
 * unexpectedly hot input. */
#define HFDL_AGC_MIN_GAIN 0.01f
#define HFDL_AGC_MAX_GAIN 1000.0f

/* Floor added to power_avg before the sqrt/divide - avoids a literal
 * divide-by-zero on the very first call (power_avg starts at 0) and
 * bounds the gain computation the same way HFDL_AGC_MAX_GAIN does,
 * belt-and-suspenders. */
#define HFDL_AGC_POWER_FLOOR 1e-9f

typedef struct {
	float32_t power_avg; /* single-pole IIR estimate of i^2+q^2, PRE-gain */
	float32_t gain;      /* most recently computed/applied linear gain */
} hfdl_agc_t;

/* Resets power_avg to 0 and gain to 1.0 (unity - matches liquid's own
 * agc_crcf_reset() convention of not presupposing a signal level
 * before the first real sample arrives). Call once at init, or again
 * to reset state e.g. at the start of a new HFDL burst. */
void hfdl_agc_init(hfdl_agc_t *a);

/* Processes `n` complex samples in-place-safe (i_out==i_in and
 * q_out==q_in are both fine, matches this project's other DSP blocks'
 * convention). Safe to call with n==0 (no-op). */
void hfdl_agc_process(hfdl_agc_t *a,
		const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t n);

/* Returns the current pre-gain power_avg estimate - see module header
 * for why this is exposed (front-end/wiring sanity check, distinct
 * from lock/no-lock). */
float32_t hfdl_agc_get_power_avg(const hfdl_agc_t *a);

/* Returns the current linear gain being applied - a gain pinned at
 * HFDL_AGC_MAX_GAIN for an extended period is the AGC's own signature
 * of "there's essentially nothing here", same information as a
 * near-zero power_avg, just expressed the other way round. */
float32_t hfdl_agc_get_gain(const hfdl_agc_t *a);

/* Runs the module's standalone self-test:
 *   1) Constant-amplitude complex input converges gain towards
 *      TARGET_AMPLITUDE/input_amplitude within tolerance, and the
 *      output settles towards unit amplitude (structural check that
 *      the loop actually normalizes, not just that it doesn't crash).
 *   2) Zero input never produces NaN/Inf gain (floor + clamp both
 *      hold) and gain saturates at HFDL_AGC_MAX_GAIN, not beyond.
 *   3) A large-amplitude input pulls gain down towards
 *      HFDL_AGC_MIN_GAIN, not below.
 * Zero dependency on real hardware or a real HFDL signal - like this
 * project's other module self-tests, this proves the mechanics are
 * correct, not that HFDL_AGC_BW/TARGET_AMPLITUDE are the "right"
 * tuning for real on-air signal dynamics (see module header - that
 * needs a real capture, not a synthetic self-test). */
uint8_t hfdl_agc_selftest(void);

#endif /* HFDL_AGC_H */

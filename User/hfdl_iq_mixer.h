#ifndef HFDL_IQ_MIXER_H
#define HFDL_IQ_MIXER_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL subcarrier downconverter - first DSP block of the demod chain
 * proper (after CRC/PDU/descrambler/deinterleaver, which are all
 * pure digital logic with no DSP dependency - see those modules'
 * headers). This is a genuine floating-point signal processing block,
 * so unlike the earlier four it can only be partially validated on
 * this host (synthetic tone in, check it lands on DC) - full
 * validation needs either a real HFDL recording or the sigidwiki.com
 * samples mentioned in the project's session notes, run through this
 * on real hardware.
 *
 * WHAT THIS DOES: shifts HFDL's 1440Hz USB subcarrier down to
 * baseband via a recursive complex NCO (a unit-magnitude rotator
 * multiplied into itself each sample, NOT a per-sample sinf()/cosf()
 * call - cheap: one complex multiply, 4 real mults + 2 real adds per
 * sample, to update the oscillator, plus the same cost again to mix
 * the input). Runs on i_dec/q_dec - see the project's demod_am.c: the
 * decimated I/Q pair at 12kHz that ALREADY EXISTS there, computed
 * whenever demod mode is USB (which HFDL always forces, per
 * k_demod_modes[]'s own hfdl field comment: "always plain USB per
 * ARINC 635-3") - BEFORE the Hilbert-based sideband-selection combine
 * step that produces s_ssb_dec. Tapping i_dec/q_dec instead of
 * s_ssb_dec (same read-only tap pattern rtty_process() uses, and
 * hfdl_scope_feed() ALSO switched to as of 16/08/2026 - see
 * hfdl_scope.h) means this module needs NO
 * synthetic I/Q reconstruction from real audio - it's genuine
 * hardware-derived quadrature, both sidebands present, exactly what a
 * coherent PSK demod needs. This was a real correction mid-session:
 * an earlier design draft assumed a synthetic downconverter (a whole
 * extra Hilbert transform) would be needed - reusing i_dec/q_dec
 * instead eliminates that entirely.
 *
 * OPEN QUESTION, NOT YET RESOLVED (flagged honestly, not glossed
 * over): whether the subcarrier lands at +1440Hz or -1440Hz in this
 * complex representation depends on the front-end's I/Q sign
 * convention (which way the quadrature LO's phase is wired/generated
 * - see ms5351 "LO ... quadrature" in the project's boot log) and
 * isn't something derivable from reading demod_am.c alone. This
 * module defaults to shifting +1440Hz down to DC
 * (HFDL_MIXER_DEFAULT_SIGN = -1, i.e. multiplying by e^{-j*theta*n});
 * if the real signal turns out to be at -1440Hz instead, flip
 * HFDL_MIXER_DEFAULT_SIGN to +1 - this is a one-constant change, not
 * a redesign, and can only be confirmed by feeding it a real signal
 * (or the sigidwiki.com sample) and checking which sign locks the
 * Costas loop (a later module) once that exists.
 */

#define HFDL_MIXER_SUBCARRIER_HZ 1440.0f

/* +1: shift -1440Hz component down to DC. -1: shift +1440Hz component
 * down to DC. See "OPEN QUESTION" above - this is the project's best
 * guess pending a real-signal check, not a confirmed fact.
 *
 * OVERRIDABLE (21/08/2026) - see demod_am.c's HFDL_TEST_CONJUGATE_IQ
 * comment. Conjugating Q (negating it) fixed the burst-detection
 * blind spot (confirmed on real hardware, 21/08 session) but not
 * decode - conjugation mirrors frequency location AND reverses
 * rotational sense, so the sign that correctly derotates the
 * (unconjugated) reference WAV isn't necessarily still correct once
 * Q is conjugated too. `make HFDL_MIXER_SIGN_OVERRIDE=1` to flip it
 * for testing alongside HFDL_TEST_CONJUGATE_IQ=1. */
#ifndef HFDL_MIXER_SIGN_OVERRIDE
#define HFDL_MIXER_DEFAULT_SIGN (-1)
#else
#define HFDL_MIXER_DEFAULT_SIGN (HFDL_MIXER_SIGN_OVERRIDE)
#endif

/* Renormalize the oscillator's unit magnitude every this many samples
 * - cheap (one sqrtf + 2 divides), amortized to ~free. At 12kHz this
 * is well under once per 100ms, keeping float32 accumulation error
 * negligible (~1e-5 or better between renorms - see the module's
 * development notes / self-test for the measured drift rate) even
 * across an HFDL burst far longer than any real one is expected to
 * run. */
#define HFDL_MIXER_RENORM_PERIOD 1000u

typedef struct {
	float32_t cos_delta;  /* cos(2*pi*f/fs), fixed per init() */
	float32_t sin_delta;  /* sin(2*pi*f/fs), fixed per init() */
	float32_t cos_cur;    /* current oscillator state (real part) */
	float32_t sin_cur;    /* current oscillator state (imag part) */
	int8_t    sign;       /* +1 or -1, see HFDL_MIXER_DEFAULT_SIGN */
	uint32_t  sample_cnt; /* samples since last renormalization */
} hfdl_iq_mixer_t;

/* Configures `m` for a given subcarrier frequency and sample rate
 * (pass HFDL_MIXER_SUBCARRIER_HZ and 12000.0f for the real use case -
 * parameterized rather than hardcoded so the self-test can exercise
 * other frequencies too) and resets the oscillator phase to (1,0).
 * `sign` is normally HFDL_MIXER_DEFAULT_SIGN - see the header comment
 * above for why that might need flipping once tested against a real
 * signal. */
void hfdl_iq_mixer_init(hfdl_iq_mixer_t *m, float32_t freq_hz, float32_t fs_hz, int8_t sign);

/* Resets the oscillator's phase back to (1,0) without touching
 * cos_delta/sin_delta - call this whenever the mixer should restart
 * "from zero phase", e.g. at the start of a new HFDL burst (the
 * absolute phase doesn't matter for a Costas-loop-driven coherent
 * demod, which locks to whatever phase it finds, but starting from a
 * known state each burst keeps behaviour reproducible for testing). */
void hfdl_iq_mixer_reset_phase(hfdl_iq_mixer_t *m);

/* Mixes `n` samples of i_in/q_in (e.g. demod_am.c's s_i_dec/s_q_dec)
 * down by the configured frequency, writing i_out/q_out (may safely
 * alias i_in/q_out respectively for in-place use, i.e. i_out==i_in
 * and q_out==q_in is fine - each output sample only depends on the
 * same-index input sample and the running oscillator state). */
void hfdl_iq_mixer_process(hfdl_iq_mixer_t *m, const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t n);

/* Runs the module's standalone self-test: feeds a synthetic pure tone
 * at the configured frequency (generated the same way the reference
 * Python simulation during development did) and checks the output
 * lands on a near-constant DC value (i.e. the tone is fully shifted
 * to baseband), including after enough samples to exercise at least
 * one renormalization cycle. Zero dependency on real hardware or a
 * real HFDL signal - this only proves the NCO math itself is correct,
 * NOT that HFDL_MIXER_DEFAULT_SIGN is the right sign for the real
 * front-end (see the header's "OPEN QUESTION" note - that needs a
 * real signal to resolve, not something a self-test can determine in
 * isolation). */
uint8_t hfdl_iq_mixer_selftest(void);

#endif /* HFDL_IQ_MIXER_H */

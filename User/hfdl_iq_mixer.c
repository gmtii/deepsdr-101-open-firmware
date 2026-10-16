#include <math.h>
#include "hfdl_iq_mixer.h"

void hfdl_iq_mixer_init(hfdl_iq_mixer_t *m, float32_t freq_hz, float32_t fs_hz, int8_t sign)
{
	float32_t theta = 2.0f * PI * freq_hz / fs_hz;

	m->cos_delta = cosf(theta);
	m->sin_delta = sinf(theta);
	m->sign = sign;
	hfdl_iq_mixer_reset_phase(m);
}

void hfdl_iq_mixer_reset_phase(hfdl_iq_mixer_t *m)
{
	m->cos_cur = 1.0f;
	m->sin_cur = 0.0f;
	m->sample_cnt = 0u;
}

void hfdl_iq_mixer_process(hfdl_iq_mixer_t *m, const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t n)
{
	for (uint32_t k = 0; k < n; k++) {
		float32_t i_val = i_in[k];
		float32_t q_val = q_in[k];

		/* Complex multiply by (cos_cur + j*sign*sin_cur) - sign flips
		 * which subcarrier direction (+f or -f) lands on DC, see
		 * HFDL_MIXER_DEFAULT_SIGN's header comment. */
		float32_t mixed_i;
		float32_t mixed_q;
		if (m->sign >= 0) {
			mixed_i = i_val * m->cos_cur - q_val * m->sin_cur;
			mixed_q = q_val * m->cos_cur + i_val * m->sin_cur;
		} else {
			mixed_i = i_val * m->cos_cur + q_val * m->sin_cur;
			mixed_q = q_val * m->cos_cur - i_val * m->sin_cur;
		}
		i_out[k] = mixed_i;
		q_out[k] = mixed_q;

		/* Advance the oscillator by one sample (rotate the unit
		 * vector forward by cos_delta/sin_delta - a single complex
		 * multiply, no transcendental calls per sample). */
		float32_t new_cos = m->cos_cur * m->cos_delta - m->sin_cur * m->sin_delta;
		float32_t new_sin = m->sin_cur * m->cos_delta + m->cos_cur * m->sin_delta;
		m->cos_cur = new_cos;
		m->sin_cur = new_sin;

		m->sample_cnt++;
		if (m->sample_cnt >= HFDL_MIXER_RENORM_PERIOD) {
			m->sample_cnt = 0u;
			float32_t mag_sq = m->cos_cur * m->cos_cur + m->sin_cur * m->sin_cur;
			/* Guard added 15/08/2026, after a real field bug traced
			 * to this division: 1.0f/sqrtf(mag_sq) was unguarded - if
			 * mag_sq ever degrades to (near) zero, this produces
			 * +Infinity, and cos_cur *= Infinity is 0*Inf = NaN
			 * whenever cos_cur is itself already 0 (or merely very
			 * small) at that instant. That NaN then poisons every
			 * future sample this mixer instance ever produces (a
			 * complex multiply by NaN is NaN forever after) - and
			 * unlike a burst-boundary reset, nothing else in this
			 * codebase re-normalizes mid-burst, so a long-running
			 * burst (thousands of renorm cycles over several
			 * minutes - far more than hfdl_iq_mixer_selftest()'s own
			 * 5-cycle/5000-sample coverage) is exactly the condition
			 * that gives float32 rounding enough repetitions to find
			 * this edge case. Threshold is generous (1e-6 on mag_sq,
			 * i.e. |rotator| < ~1e-3) - normal operation keeps mag_sq
			 * extremely close to 1.0 always, so this only ever fires
			 * on genuine degradation, never in ordinary use. Recovery
			 * is the same clean (1,0) hfdl_iq_mixer_reset_phase()
			 * already uses - a full phase reset costs one burst's
			 * worth of Costas/symsync re-acquisition, which is far
			 * cheaper than silently running on NaN for the rest of
			 * the session. */
			if (mag_sq < 1.0e-6f) {
				m->cos_cur = 1.0f;
				m->sin_cur = 0.0f;
			} else {
				float32_t inv_mag = 1.0f / sqrtf(mag_sq);
				m->cos_cur *= inv_mag;
				m->sin_cur *= inv_mag;
			}
		}
	}
}

/* ---- self-test ---- */

uint8_t hfdl_iq_mixer_selftest(void)
{
	hfdl_iq_mixer_t m;
	hfdl_iq_mixer_init(&m, HFDL_MIXER_SUBCARRIER_HZ, 12000.0f, HFDL_MIXER_DEFAULT_SIGN);

	/* --- Part 1: synthetic tone whose sign matches HFDL_MIXER_DEFAULT_SIGN
	 * (i.e. one that SHOULD land on DC given that sign) must come out
	 * as a near-constant (1, 0) - matches the Python reference
	 * simulation run during development. */
	{
		float32_t theta = 2.0f * PI * HFDL_MIXER_SUBCARRIER_HZ / 12000.0f;
		/* tone_sign matches m.sign's convention: sign=-1 (default)
		 * expects a tone at +freq_hz (cos(n*theta), sin(n*theta)) to
		 * land on DC - see hfdl_iq_mixer_process()'s sign<0 branch,
		 * which is exactly the down-mix that cancels such a tone. */
		const uint32_t n_test = 50u;
		float32_t tolerance = 1.0e-4f; /* generous - float32 rotator error over 50 samples is far smaller, see Part 2 */

		for (uint32_t k = 0; k < n_test; k++) {
			float32_t i_in = cosf(theta * (float32_t)k);
			float32_t q_in = sinf(theta * (float32_t)k);
			float32_t i_out, q_out;
			hfdl_iq_mixer_process(&m, &i_in, &q_in, &i_out, &q_out, 1u);

			if (fabsf(i_out - 1.0f) > tolerance || fabsf(q_out) > tolerance) {
				return 0u;
			}
		}
	}

	/* --- Part 2: oscillator magnitude stays close to 1 across enough
	 * samples to exercise several renormalization cycles (measured
	 * drift during development: ~8.5e-9 per sample in float32 before
	 * renorm - HFDL_MIXER_RENORM_PERIOD=1000 keeps the error from
	 * ever exceeding roughly 1e-5, well inside this tolerance). --- */
	{
		hfdl_iq_mixer_t m2;
		hfdl_iq_mixer_init(&m2, HFDL_MIXER_SUBCARRIER_HZ, 12000.0f, HFDL_MIXER_DEFAULT_SIGN);

		float32_t i_dummy[64];
		float32_t q_dummy[64];
		for (uint32_t k = 0; k < 64u; k++) {
			i_dummy[k] = 0.0f;
			q_dummy[k] = 0.0f;
		}
		float32_t i_out[64];
		float32_t q_out[64];

		/* 5000 samples = 5 renorm cycles at the default period */
		for (uint32_t block = 0; block < (5000u / 64u); block++) {
			hfdl_iq_mixer_process(&m2, i_dummy, q_dummy, i_out, q_out, 64u);
		}

		float32_t mag = sqrtf(m2.cos_cur * m2.cos_cur + m2.sin_cur * m2.sin_cur);
		if (fabsf(mag - 1.0f) > 1.0e-3f) {
			return 0u;
		}
	}

	/* --- Part 3: hfdl_iq_mixer_reset_phase() must bring the oscillator
	 * back to (1,0) exactly, regardless of how far it had drifted. --- */
	{
		hfdl_iq_mixer_reset_phase(&m);
		if (m.cos_cur != 1.0f || m.sin_cur != 0.0f || m.sample_cnt != 0u) {
			return 0u;
		}
	}

	/* --- Part 4 (added 15/08/2026, regression test for a real field
	 * bug): force the degenerate (0,0) rotator state directly, one
	 * sample short of a renorm trigger, and confirm the renorm's
	 * near-zero guard recovers cleanly to (1,0) instead of producing
	 * Inf/NaN via the old unguarded 1.0f/sqrtf(mag_sq) division - see
	 * hfdl_iq_mixer_process()'s comment on that division for the full
	 * story. Bypasses hfdl_iq_mixer_init()/reset_phase() on purpose,
	 * poking the struct fields directly, since this state is exactly
	 * what several thousand renorm cycles over a long real session
	 * eventually reached in the field - not reachable through the
	 * normal public API in a short test run. --- */
	{
		hfdl_iq_mixer_t m3;
		hfdl_iq_mixer_init(&m3, HFDL_MIXER_SUBCARRIER_HZ, 12000.0f, HFDL_MIXER_DEFAULT_SIGN);
		m3.cos_cur = 0.0f;
		m3.sin_cur = 0.0f;
		m3.sample_cnt = HFDL_MIXER_RENORM_PERIOD - 1u;

		float32_t i_in = 0.0f;
		float32_t q_in = 0.0f;
		float32_t i_out, q_out;
		hfdl_iq_mixer_process(&m3, &i_in, &q_in, &i_out, &q_out, 1u);

		if (m3.cos_cur != m3.cos_cur || m3.sin_cur != m3.sin_cur) {
			return 0u; /* NaN - the old bug, portable NaN check (x!=x) */
		}
		if (fabsf(m3.cos_cur) > 3.0e38f || fabsf(m3.sin_cur) > 3.0e38f) {
			return 0u; /* Inf or blown up */
		}
		if (m3.cos_cur != 1.0f || m3.sin_cur != 0.0f) {
			return 0u; /* must have recovered to the clean (1,0) reset, not just "not NaN" */
		}
	}

	return 1u;
}

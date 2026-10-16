#include <math.h>
#include "hfdl_agc.h"

void hfdl_agc_init(hfdl_agc_t *a)
{
	a->power_avg = 0.0f;
	a->gain = 1.0f;
}

void hfdl_agc_process(hfdl_agc_t *a,
		const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t n)
{
	for (uint32_t k = 0; k < n; k++) {
		float32_t i_s = i_in[k];
		float32_t q_s = q_in[k];
		float32_t power = i_s * i_s + q_s * q_s;

		a->power_avg = (1.0f - HFDL_AGC_BW) * a->power_avg + HFDL_AGC_BW * power;

		float32_t gain = HFDL_AGC_TARGET_AMPLITUDE / sqrtf(a->power_avg + HFDL_AGC_POWER_FLOOR);
		if (gain > HFDL_AGC_MAX_GAIN) {
			gain = HFDL_AGC_MAX_GAIN;
		} else if (gain < HFDL_AGC_MIN_GAIN) {
			gain = HFDL_AGC_MIN_GAIN;
		}
		a->gain = gain;

		i_out[k] = gain * i_s;
		q_out[k] = gain * q_s;
	}
}

float32_t hfdl_agc_get_power_avg(const hfdl_agc_t *a)
{
	return a->power_avg;
}

float32_t hfdl_agc_get_gain(const hfdl_agc_t *a)
{
	return a->gain;
}

/* ---- self-test ---- */

uint8_t hfdl_agc_selftest(void)
{
	/* 1) Constant-amplitude input -> gain should converge towards
	 * TARGET/amplitude, output towards unit amplitude. */
	{
		hfdl_agc_t a;
		hfdl_agc_init(&a);
		const float32_t amplitude = 0.05f; /* deliberately small, well below target */
		float32_t i_out, q_out;

		for (uint32_t n = 0; n < 2000; n++) {
			float32_t i_in = amplitude;
			float32_t q_in = 0.0f;
			hfdl_agc_process(&a, &i_in, &q_in, &i_out, &q_out, 1u);
		}

		float32_t expected_gain = HFDL_AGC_TARGET_AMPLITUDE / amplitude;
		if (fabsf(a.gain - expected_gain) > 0.05f * expected_gain) {
			return 0u; /* gain did not converge close enough */
		}
		if (fabsf(i_out - HFDL_AGC_TARGET_AMPLITUDE) > 0.05f * HFDL_AGC_TARGET_AMPLITUDE) {
			return 0u; /* output amplitude did not normalize */
		}
	}

	/* 2) Zero input -> no NaN/Inf, gain saturates at MAX, not beyond. */
	{
		hfdl_agc_t a;
		hfdl_agc_init(&a);
		float32_t i_out, q_out;

		for (uint32_t n = 0; n < 2000; n++) {
			float32_t i_in = 0.0f;
			float32_t q_in = 0.0f;
			hfdl_agc_process(&a, &i_in, &q_in, &i_out, &q_out, 1u);
		}

		if (isnan(a.gain) || isinf(a.gain)) {
			return 0u;
		}
		if (a.gain > HFDL_AGC_MAX_GAIN + 1.0f) { /* small slack for float rounding */
			return 0u;
		}
		if (i_out != 0.0f || q_out != 0.0f) {
			return 0u; /* gain*0 must stay exactly 0 */
		}
	}

	/* 3) Large-amplitude input -> gain pulled down towards MIN, not below. */
	{
		hfdl_agc_t a;
		hfdl_agc_init(&a);
		const float32_t amplitude = 500.0f;
		float32_t i_out, q_out;

		for (uint32_t n = 0; n < 2000; n++) {
			float32_t i_in = amplitude;
			float32_t q_in = 0.0f;
			hfdl_agc_process(&a, &i_in, &q_in, &i_out, &q_out, 1u);
		}

		if (a.gain < HFDL_AGC_MIN_GAIN - 1e-6f) {
			return 0u;
		}
		if (isnan(a.gain) || isinf(a.gain)) {
			return 0u;
		}
	}

	return 1u;
}

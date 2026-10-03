#include "hfdl_rational_resampler.h"

/* Descomposicion polifasica real de rresamp_crcf_create_kaiser(9,20,8,-1,60):
 * P=9 sub-filtros de 16 taps cada uno (h_len=145, 1 coef. descartado
 * por division entera h_len/P - comportamiento real de firpfb_create(),
 * no un error de transcripcion). Cada fila YA esta invertida en el tiempo
 * (lista para dot-product directo), replicando firpfb.proto.c linea a linea. */
const float32_t hfdl_resamp_pfb[HFDL_RESAMP_NUM_PHASES][HFDL_RESAMP_SUBFILTER_LEN] = {
  { -3.6989431828e-03f, 1.7570829019e-02f, 3.3030308783e-02f, -5.2605185658e-02f, -1.4476141334e-01f, 9.2904202640e-02f, 6.7113006115e-01f, 1.0000000000e+00f, 6.7113006115e-01f, 9.2904202640e-02f, -1.4476141334e-01f, -5.2605185658e-02f, 3.3030308783e-02f, 1.7570829019e-02f, -3.6989431828e-03f, -1.7144660233e-03f },
  { -4.2025959119e-03f, 1.3955594040e-02f, 3.4951157868e-02f, -3.7940371782e-02f, -1.4471833408e-01f, 4.3723687530e-02f, 6.0578477383e-01f, 9.9540066719e-01f, 7.3319333792e-01f, 1.4711040258e-01f, -1.4036381245e-01f, -6.7726358771e-02f, 2.9645843431e-02f, 2.1241940558e-02f, -2.8533132281e-03f, -2.2259964608e-03f },
  { -4.4091851451e-03f, 1.0513247922e-02f, 3.5547848791e-02f, -2.4096572772e-02f, -1.4076770842e-01f, 3.9301728094e-08f, 5.3826177120e-01f, 9.8168796301e-01f, 7.9089891911e-01f, 2.0577889681e-01f, -1.3104918599e-01f, -8.2877121866e-02f, 2.4691112339e-02f, 2.4830128998e-02f, -1.6288526822e-03f, -2.7590647805e-03f },
  { -4.3686679564e-03f, 7.3375962675e-03f, 3.4984637052e-02f, -1.1372709647e-02f, -1.3348196447e-01f, -3.7970896810e-02f, 4.6967390180e-01f, 9.5911967754e-01f, 8.4322935343e-01f, 2.6822137833e-01f, -1.1641407013e-01f, -9.7574882209e-02f, 1.8100660294e-02f, 2.8177315369e-02f, 1.0869120715e-08f, -3.2814976294e-03f },
  { -4.1331737302e-03f, 4.4989539310e-03f, 3.3442188054e-02f, -2.0449871130e-08f, -1.2345588207e-01f, -7.0028565824e-02f, 4.0111413598e-01f, 9.2811387777e-01f, 8.8924646378e-01f, 3.3363428712e-01f, -9.6144363284e-02f, -1.1129002273e-01f, 9.8574860021e-03f, 3.1109629199e-02f, 2.0445787814e-03f, -3.7544211373e-03f },
  { -3.7544211373e-03f, 2.0445787814e-03f, 3.1109629199e-02f, 9.8574860021e-03f, -1.1129002273e-01f, -9.6144363284e-02f, 3.3363428712e-01f, 8.8924646378e-01f, 9.2811387777e-01f, 4.0111413598e-01f, -7.0028565824e-02f, -1.2345588207e-01f, -2.0449871130e-08f, 3.3442188054e-02f, 4.4989539310e-03f, -4.1331737302e-03f },
  { -3.2814976294e-03f, 1.0869120715e-08f, 2.8177315369e-02f, 1.8100660294e-02f, -9.7574882209e-02f, -1.1641407013e-01f, 2.6822137833e-01f, 8.4322935343e-01f, 9.5911967754e-01f, 4.6967390180e-01f, -3.7970896810e-02f, -1.3348196447e-01f, -1.1372709647e-02f, 3.4984637052e-02f, 7.3375962675e-03f, -4.3686679564e-03f },
  { -2.7590647805e-03f, -1.6288526822e-03f, 2.4830128998e-02f, 2.4691112339e-02f, -8.2877121866e-02f, -1.3104918599e-01f, 2.0577889681e-01f, 7.9089891911e-01f, 9.8168796301e-01f, 5.3826177120e-01f, 3.9301728094e-08f, -1.4076770842e-01f, -2.4096572772e-02f, 3.5547848791e-02f, 1.0513247922e-02f, -4.4091851451e-03f },
  { -2.2259964608e-03f, -2.8533132281e-03f, 2.1241940558e-02f, 2.9645843431e-02f, -6.7726358771e-02f, -1.4036381245e-01f, 1.4711040258e-01f, 7.3319333792e-01f, 9.9540066719e-01f, 6.0578477383e-01f, 4.3723687530e-02f, -1.4471833408e-01f, -3.7940371782e-02f, 3.4951157868e-02f, 1.3955594040e-02f, -4.2025959119e-03f },
};

void hfdl_rational_resampler_init(hfdl_rational_resampler_t *r)
{
	for (uint32_t i = 0; i < HFDL_RESAMP_SUBFILTER_LEN; i++) {
		r->hist_i[i] = 0.0f;
		r->hist_q[i] = 0.0f;
	}
}

/* Pushes one new sample into the shared history, discarding the
 * oldest (mirrors liquid's WINDOW push semantics: shift left, insert
 * newest at the end). */
static void push_sample(float32_t *hist, float32_t new_val)
{
	for (uint32_t i = 0; i + 1 < HFDL_RESAMP_SUBFILTER_LEN; i++) {
		hist[i] = hist[i + 1];
	}
	hist[HFDL_RESAMP_SUBFILTER_LEN - 1] = new_val;
}

static float32_t dot_phase(const float32_t *hist, uint32_t phase)
{
	float32_t acc = 0.0f;
	for (uint32_t i = 0; i < HFDL_RESAMP_SUBFILTER_LEN; i++) {
		acc += hist[i] * hfdl_resamp_pfb[phase][i];
	}
	return acc;
}

void hfdl_rational_resampler_process(hfdl_rational_resampler_t *r,
		const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out)
{
	/* Direct port of rresamp_crcf_execute_primitive() (see module
	 * header) - index bookkeeping kept identical on purpose. */
	uint32_t index = 0;
	uint32_t n = 0;

	for (uint32_t i = 0; i < HFDL_RESAMP_DECIM; i++) {
		push_sample(r->hist_i, i_in[i]);
		push_sample(r->hist_q, q_in[i]);

		while (index < HFDL_RESAMP_NUM_PHASES) {
			i_out[n] = dot_phase(r->hist_i, index) * HFDL_RESAMP_OUTPUT_SCALE;
			q_out[n] = dot_phase(r->hist_q, index) * HFDL_RESAMP_OUTPUT_SCALE;
			n++;
			index += HFDL_RESAMP_DECIM;
		}
		index -= HFDL_RESAMP_NUM_PHASES;
	}
	/* n must equal HFDL_RESAMP_NUM_PHASES (9) and index must be back
	 * to 0 here - guaranteed by construction for these fixed P/Q, same
	 * invariant liquid's own execute_primitive() asserts internally. */
}

/* ---- self-test ---- */

uint8_t hfdl_rational_resampler_selftest(void)
{
	/* --- Part 1: structural sanity check on the generated table -
	 * phase 0 (center-aligned) should have its peak tap very close to
	 * 1.0 (the hallmark of a windowed-sinc lowpass's center sample
	 * before any scaling) - catches gross transcription corruption. --- */
	{
		float32_t peak = 0.0f;
		for (uint32_t i = 0; i < HFDL_RESAMP_SUBFILTER_LEN; i++) {
			float32_t v = hfdl_resamp_pfb[0][i];
			if (v > peak) {
				peak = v;
			}
		}
		if (fabsf(peak - 1.0f) > 0.01f) {
			return 0u;
		}
	}

	/* --- Part 2: DC gain check --- */
	{
		hfdl_rational_resampler_t r;
		hfdl_rational_resampler_init(&r);

		float32_t i_in[HFDL_RESAMP_DECIM];
		float32_t q_in[HFDL_RESAMP_DECIM];
		for (uint32_t i = 0; i < HFDL_RESAMP_DECIM; i++) {
			i_in[i] = 1.0f;
			q_in[i] = 0.0f;
		}

		float32_t i_out[HFDL_RESAMP_NUM_PHASES];
		float32_t q_out[HFDL_RESAMP_NUM_PHASES];

		/* Run several primitive calls so history fully fills with the
		 * constant input (history length 16 samples, each primitive
		 * call consumes 20 - 2 calls is already more than enough to
		 * fully flush any zero-initialized history out). */
		for (uint32_t call = 0; call < 4u; call++) {
			hfdl_rational_resampler_process(&r, i_in, q_in, i_out, q_out);
		}

		/* Expected DC gain: sum of ANY phase's taps (a constant input
		 * makes every phase see the same all-ones history, so the
		 * output for phase p is just sum(hfdl_resamp_pfb[p]) *
		 * OUTPUT_SCALE - compute this independently from phase 0's
		 * taps and compare against the actual measured output rather
		 * than assuming a specific numeric value, so this test stays
		 * correct even if the design parameters (m, as) ever change). */
		float32_t expected_sum = 0.0f;
		for (uint32_t i = 0; i < HFDL_RESAMP_SUBFILTER_LEN; i++) {
			expected_sum += hfdl_resamp_pfb[0][i];
		}
		float32_t expected_dc = expected_sum * HFDL_RESAMP_OUTPUT_SCALE;

		float32_t tolerance = 1.0e-4f;
		for (uint32_t i = 0; i < HFDL_RESAMP_NUM_PHASES; i++) {
			if (fabsf(q_out[i]) > tolerance) {
				return 0u; /* Q was always fed zero, must stay zero */
			}
		}
		/* Only phase-0's own output is checked against expected_dc
		 * directly (other phases have different tap sums, by design -
		 * that's what makes them different interpolation points) -
		 * check that phase-0's output specifically (i_out[0], since
		 * index starts at 0 each primitive call) matches. */
		if (fabsf(i_out[0] - expected_dc) > tolerance) {
			return 0u;
		}
	}

	return 1u;
}

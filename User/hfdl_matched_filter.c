#include "hfdl_matched_filter.h"

/* Literal values from dumphfdl's src/hfdl.c hfdl_matched_filter[]
 * (szpajder/dumphfdl, GPL-3.0-or-later) - see hfdl_matched_filter.h
 * for the full provenance note. */
const float32_t hfdl_matched_filter_coeffs[HFDL_MF_TAPS_CNT] = {
	-0.0170974647427123f, 0.01148231492068473f, 0.03138375667422348f, 0.009454398851680437f,
	-0.04161644170893816f, -0.06451564801420356f, -0.005495792933327306f, 0.1316404671361545f,
	0.2759693160697777f, 0.3375901874933208f, 0.2759693160697777f, 0.1316404671361545f,
	-0.005495792933327306f, -0.06451564801420356f, -0.04161644170893816f, 0.009454398851680437f,
	0.03138375667422348f, 0.01148231492068473f, -0.0170974647427123f
};

void hfdl_matched_filter_init(hfdl_matched_filter_t *f)
{
	arm_fir_init_f32(&f->fir_i, HFDL_MF_TAPS_CNT, (float32_t *)hfdl_matched_filter_coeffs,
			f->state_i, HFDL_MF_MAX_BLOCK_SIZE);
	arm_fir_init_f32(&f->fir_q, HFDL_MF_TAPS_CNT, (float32_t *)hfdl_matched_filter_coeffs,
			f->state_q, HFDL_MF_MAX_BLOCK_SIZE);
}

void hfdl_matched_filter_process(hfdl_matched_filter_t *f, const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t block_size)
{
	arm_fir_f32(&f->fir_i, i_in, i_out, block_size);
	arm_fir_f32(&f->fir_q, q_in, q_out, block_size);
}

/* ---- self-test ---- */

uint8_t hfdl_matched_filter_selftest(void)
{
	/* --- Part 1: structural symmetry check (linear phase) --- */
	for (uint32_t i = 0; i < HFDL_MF_TAPS_CNT; i++) {
		if (hfdl_matched_filter_coeffs[i] != hfdl_matched_filter_coeffs[HFDL_MF_TAPS_CNT - 1u - i]) {
			return 0u;
		}
	}

	/* --- Part 2: impulse response identity --- */
	{
		hfdl_matched_filter_t f;
		hfdl_matched_filter_init(&f);

		/* Feed the impulse (and enough trailing zeros to flush the
		 * filter's full impulse response out) one block at a time,
		 * block_size = HFDL_MF_TAPS_CNT itself for simplicity - two
		 * blocks is enough since impulse response length ==
		 * HFDL_MF_TAPS_CNT exactly for an FIR filter. */
		float32_t i_in[HFDL_MF_TAPS_CNT];
		float32_t q_in[HFDL_MF_TAPS_CNT];
		float32_t i_out[HFDL_MF_TAPS_CNT];
		float32_t q_out[HFDL_MF_TAPS_CNT];

		for (uint32_t k = 0; k < HFDL_MF_TAPS_CNT; k++) {
			i_in[k] = 0.0f;
			q_in[k] = 0.0f;
		}
		i_in[0] = 1.0f; /* unit impulse on I, block 1 - Q stays all zero throughout both blocks */

		/* Block 1: impulse enters, output should be all zero (FIR
		 * with numTaps=19 hasn't produced any nonzero output yet
		 * until the impulse has propagated through - actually for a
		 * direct-form FIR the very first output IS h[0], not zero -
		 * see the check below, which handles this correctly by
		 * checking the full 19-sample impulse response across both
		 * blocks combined, not assuming block 1 is all-zero). */
		hfdl_matched_filter_process(&f, i_in, q_in, i_out, q_out, HFDL_MF_TAPS_CNT);

		float32_t tolerance = 1.0e-6f;
		for (uint32_t k = 0; k < HFDL_MF_TAPS_CNT; k++) {
			if (fabsf(i_out[k] - hfdl_matched_filter_coeffs[k]) > tolerance) {
				return 0u;
			}
			if (fabsf(q_out[k]) > tolerance) {
				return 0u; /* Q channel must stay exactly zero - it was never fed anything else */
			}
		}

		/* Block 2: all zeros in - output must also be all zero (the
		 * impulse response is exactly HFDL_MF_TAPS_CNT samples long,
		 * fully captured in block 1 - nothing should still be
		 * "ringing" in block 2). */
		for (uint32_t k = 0; k < HFDL_MF_TAPS_CNT; k++) {
			i_in[k] = 0.0f;
		}
		hfdl_matched_filter_process(&f, i_in, q_in, i_out, q_out, HFDL_MF_TAPS_CNT);
		for (uint32_t k = 0; k < HFDL_MF_TAPS_CNT; k++) {
			if (fabsf(i_out[k]) > tolerance || fabsf(q_out[k]) > tolerance) {
				return 0u;
			}
		}
	}

	return 1u;
}

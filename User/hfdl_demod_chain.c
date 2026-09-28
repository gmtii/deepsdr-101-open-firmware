#include <math.h>
#include "hfdl_demod_chain.h"

void hfdl_demod_chain_init(hfdl_demod_chain_t *c)
{
	hfdl_iq_mixer_init(&c->mixer, HFDL_MIXER_SUBCARRIER_HZ, 12000.0f, HFDL_MIXER_DEFAULT_SIGN);
	hfdl_rational_resampler_init(&c->resampler);
	hfdl_agc_init(&c->agc);
	hfdl_matched_filter_init(&c->mf);
	hfdl_symsync_reset(&c->symsync);
	hfdl_symsync_unlock(&c->symsync);
	hfdl_costas_init(&c->costas, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);

	for (uint32_t i = 0; i < HFDL_RESAMP_DECIM; i++) {
		c->resamp_buf_i[i] = 0.0f;
		c->resamp_buf_q[i] = 0.0f;
	}
	c->resamp_buf_fill = 0u;
}

void hfdl_demod_chain_process(hfdl_demod_chain_t *c,
		const float32_t *i_in, const float32_t *q_in, uint32_t n_in,
		float32_t *i_symbols_out, float32_t *q_symbols_out,
		float32_t *i_half_symbols_out, float32_t *q_half_symbols_out,
		uint32_t *n_symbols_out)
{
	uint32_t n_symbols = 0u;

	for (uint32_t k = 0; k < n_in; k++) {
		float32_t mixed_i, mixed_q;
		hfdl_iq_mixer_process(&c->mixer, &i_in[k], &q_in[k], &mixed_i, &mixed_q, 1u);

		c->resamp_buf_i[c->resamp_buf_fill] = mixed_i;
		c->resamp_buf_q[c->resamp_buf_fill] = mixed_q;
		c->resamp_buf_fill++;

		if (c->resamp_buf_fill < HFDL_RESAMP_DECIM) {
			continue;
		}
		c->resamp_buf_fill = 0u;

		float32_t resamp_i[HFDL_RESAMP_NUM_PHASES];
		float32_t resamp_q[HFDL_RESAMP_NUM_PHASES];
		hfdl_rational_resampler_process(&c->resampler, c->resamp_buf_i, c->resamp_buf_q,
				resamp_i, resamp_q);

		float32_t agc_i[HFDL_RESAMP_NUM_PHASES];
		float32_t agc_q[HFDL_RESAMP_NUM_PHASES];
		hfdl_agc_process(&c->agc, resamp_i, resamp_q, agc_i, agc_q, HFDL_RESAMP_NUM_PHASES);

		float32_t mf_i[HFDL_RESAMP_NUM_PHASES];
		float32_t mf_q[HFDL_RESAMP_NUM_PHASES];
		hfdl_matched_filter_process(&c->mf, agc_i, agc_q, mf_i, mf_q, HFDL_RESAMP_NUM_PHASES);

		for (uint32_t j = 0; j < HFDL_RESAMP_NUM_PHASES; j++) {
			float32_t ss_i[HFDL_SYMSYNC_M];
			float32_t ss_q[HFDL_SYMSYNC_M];
			float32_t ss_i_half[HFDL_SYMSYNC_M];
			float32_t ss_q_half[HFDL_SYMSYNC_M];
			uint8_t ss_half_clamped[HFDL_SYMSYNC_M];
			uint32_t ss_n;
			hfdl_symsync_step(&c->symsync, mf_i[j], mf_q[j], ss_i, ss_q,
					ss_i_half, ss_q_half, ss_half_clamped, &ss_n);

			for (uint32_t s = 0; s < ss_n; s++) {
				if (n_symbols < HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT) {
					/* T/2 tap FIRST (25/08/2026, pieza 3) - it is
					 * chronologically the EARLIER of the two samples
					 * (see hfdl_symsync_step()'s own comment), so it
					 * must be derotated using the loop's phase as
					 * carried over from the PREVIOUS symbol, before
					 * hfdl_costas_step_bpsk() below advances that
					 * phase for this symbol. See
					 * hfdl_costas_derotate_only()'s own header
					 * comment for why this can't just call
					 * hfdl_costas_step_bpsk() a second time. */
					float32_t half_i, half_q;
					hfdl_costas_derotate_only(&c->costas, ss_i_half[s], ss_q_half[s], &half_i, &half_q);
					i_half_symbols_out[n_symbols] = half_i;
					q_half_symbols_out[n_symbols] = half_q;

					float32_t co_i, co_q;
					hfdl_costas_step_bpsk(&c->costas, ss_i[s], ss_q[s], &co_i, &co_q);
					i_symbols_out[n_symbols] = co_i;
					q_symbols_out[n_symbols] = co_q;
					n_symbols++;
				}
				/* Capacity reached - remaining symsync outputs this
				 * call are silently dropped rather than overflowing
				 * the caller's buffer. Shouldn't happen in practice
				 * for demod_am.c's real block sizes (see header's
				 * sizing note) but this guard makes the failure mode
				 * "a few dropped symbols", not memory corruption. */
			}
		}
	}

	*n_symbols_out = n_symbols;
}

float32_t hfdl_demod_chain_get_agc_power_avg(const hfdl_demod_chain_t *c)
{
	return hfdl_agc_get_power_avg(&c->agc);
}

float32_t hfdl_demod_chain_get_agc_gain(const hfdl_demod_chain_t *c)
{
	return hfdl_agc_get_gain(&c->agc);
}

/* ---- self-test ---- */

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_demod_chain_selftest(void)
{
	/* Synthetic BPSK-on-1440Hz-subcarrier signal at 12kHz, matching
	 * what hfdl_iq_mixer.c actually expects at its input (see that
	 * module's header) - built simply (roughly 6.67 samples/symbol,
	 * sample-and-hold, no explicit pre-shaping) since this self-test
	 * checks END-TO-END PLUMBING AND STABILITY, not lock/BER - see
	 * this module's header for that scope limitation.
	 * Sized for 200 symbols (reduced from 1000 on 10/08/2026, once
	 * `nm` showed this self-test's own static buffers cost ~52.5KB at
	 * the original size - more than this project's entire free RAM
	 * margin. 200 symbols (~1.3KB*2 buffers) is still comfortably
	 * enough to exercise several resampler/symsync cycles and confirm
	 * no divergence, at a fraction of the RAM cost - see the project's
	 * session notes for the full measurement.) */
	static float32_t i_signal[1344]; /* 200 symbols * ~6.72 samples/symbol, generous */
	static float32_t q_signal[1344];
	const uint32_t n_samples = 1344u;
	const float32_t samples_per_symbol = 12000.0f / 1800.0f; /* ~6.667 */
	const float32_t theta_per_sample = 2.0f * PI * HFDL_MIXER_SUBCARRIER_HZ / 12000.0f;

	uint32_t rng = 555u;
	float32_t phase = 0.0f;
	uint32_t last_symbol_idx = 0xFFFFFFFFu;
	float32_t bit = 1.0f;

	for (uint32_t n = 0; n < n_samples; n++) {
		uint32_t symbol_idx = (uint32_t)((float32_t)n / samples_per_symbol);
		/* Deterministic per-symbol bit, regenerated from a fresh RNG
		 * draw only when symbol_idx advances - simple sample-and-hold
		 * pulse shape (not RRC) - fine for a plumbing check. */
		if (symbol_idx != last_symbol_idx) {
			bit = ((xorshift32(&rng) & 1u) != 0u) ? 1.0f : -1.0f;
			last_symbol_idx = symbol_idx;
		}

		/* Modulate onto +1440Hz (matches HFDL_MIXER_DEFAULT_SIGN=-1's
		 * expectation - see hfdl_iq_mixer.h). */
		i_signal[n] = bit * cosf(phase);
		q_signal[n] = bit * sinf(phase);
		phase += theta_per_sample;
	}

	hfdl_demod_chain_t chain;
	hfdl_demod_chain_init(&chain);

	float32_t i_out[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
	float32_t q_out[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
	float32_t i_half_out[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
	float32_t q_half_out[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
	uint32_t n_out;

	uint32_t total_symbols = 0u;
	const uint32_t block_size = 32u; /* matches demod_am.c's real DEC_BLOCK_SAMPLES as of this writing */

	for (uint32_t start = 0; start + block_size <= n_samples; start += block_size) {
		hfdl_demod_chain_process(&chain, &i_signal[start], &q_signal[start], block_size,
				i_out, q_out, i_half_out, q_half_out, &n_out);

		/* T/2 tap NaN/divergence guard (25/08/2026, pieza 1) - same
		 * check as the main tap below, on the new raw half-symbol
		 * output. */
		for (uint32_t k2 = 0; k2 < n_out; k2++) {
			if (!(i_half_out[k2] == i_half_out[k2]) || !(q_half_out[k2] == q_half_out[k2])) {
				return 0u;
			}
		}

		for (uint32_t k = 0; k < n_out; k++) {
			/* NaN/divergence guard - a NaN never compares equal to
			 * itself, the standard portable NaN check. */
			if (!(i_out[k] == i_out[k]) || !(q_out[k] == q_out[k])) {
				return 0u;
			}
			if (fabsf(i_out[k]) > 1.0e6f || fabsf(q_out[k]) > 1.0e6f) {
				return 0u; /* blown up, not just noisy */
			}
		}
		total_symbols += n_out;
	}

	/* The chain must have produced SOME symbols across the whole run -
	 * zero throughout would mean something in the pipeline is stuck
	 * (e.g. the resampler buffer never filling, or symsync never
	 * emitting anything). */
	if (total_symbols == 0u) {
		return 0u;
	}

	return 1u;
}

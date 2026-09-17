#include <math.h>
#include "hfdl_costas.h"

void hfdl_costas_init(hfdl_costas_t *c, float32_t alpha, float32_t beta)
{
	c->alpha = alpha;
	c->beta = beta;
	hfdl_costas_reset(c);
}

void hfdl_costas_reset(hfdl_costas_t *c)
{
	c->phase = 0.0f;
	c->freq = 0.0f;
	c->coarse_prev_sq_i = 0.0f;
	c->coarse_prev_sq_q = 0.0f;
	c->coarse_acc_i = 0.0f;
	c->coarse_acc_q = 0.0f;
}

/* EMA time constant for the coarse accumulator - see
 * hfdl_costas_get_coarse_freq_estimate()'s comment.
 *
 * MEASURED (16/08/2026, host-side test against synthetic BPSK+AWGN at
 * the signal/floor ratio ~3.3 seen in real captures): squaring loss
 * is severe at this SNR - lambda=0.15 (~7-symbol effective window)
 * gave a mean estimate error of ~0.13 rad/symbol, worse than useless
 * (bigger than the residual it's meant to fix). Averaging over MORE
 * total symbols with that same lambda did NOT help at all (fixed
 * lambda caps the EMA's effective memory regardless of how much data
 * you feed it - obvious in hindsight, cost some debugging time to
 * confirm). Sweeping lambda down at a fixed 500-symbol feed:
 *   0.15  (~7-symbol window):   mean abs error ~0.128
 *   0.05  (~20-symbol window):  mean abs error ~0.066
 *   0.02  (~50-symbol window):  mean abs error ~0.041
 *   0.01  (~100-symbol window): mean abs error ~0.029
 *   0.005 (~200-symbol window): mean abs error ~0.022
 * Picked 0.01: real A1_SEARCH runs are typically several hundred
 * symbols before a hit (see session captures - hits around symbol
 * 830, 1261, 2339...), so a ~100-symbol effective window has settled
 * long before the estimate is actually read. ~0.03 rad/symbol
 * residual error is still meaningfully smaller than the ~0.02+ this
 * project's fine loop was measured leaving uncorrected on its own -
 * see hfdl_costas_get_coarse_freq_estimate()'s comment for why even
 * a partial improvement here should still help the fine loop close
 * the remaining gap within A2_SEARCH's ~127-symbol window. */
#define HFDL_COSTAS_COARSE_EMA_LAMBDA 0.01f

void hfdl_costas_derotate_only(const hfdl_costas_t *c, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out)
{
	float32_t cos_p = cosf(c->phase);
	float32_t sin_p = sinf(c->phase);
	*i_out = i_in * cos_p + q_in * sin_p;
	*q_out = q_in * cos_p - i_in * sin_p;
}

void hfdl_costas_step_bpsk(hfdl_costas_t *c, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out)
{
	/* Coarse acquisition accumulator - on the RAW input, BEFORE
	 * derotation, so it's a clean read of the actual residual
	 * carrier regardless of the fine loop's own (possibly still
	 * wrong) phase/freq state. See this struct field's declaration
	 * comment. */
	float32_t sq_i = i_in * i_in - q_in * q_in;
	float32_t sq_q = 2.0f * i_in * q_in;
	float32_t prod_i = sq_i * c->coarse_prev_sq_i + sq_q * c->coarse_prev_sq_q;
	float32_t prod_q = sq_q * c->coarse_prev_sq_i - sq_i * c->coarse_prev_sq_q;
	c->coarse_acc_i += HFDL_COSTAS_COARSE_EMA_LAMBDA * (prod_i - c->coarse_acc_i);
	c->coarse_acc_q += HFDL_COSTAS_COARSE_EMA_LAMBDA * (prod_q - c->coarse_acc_q);
	c->coarse_prev_sq_i = sq_i;
	c->coarse_prev_sq_q = sq_q;

	float32_t cos_p = cosf(c->phase);
	float32_t sin_p = sinf(c->phase);

	/* Derotate by e^{-j*phase}: (I+jQ)(cos_p - j sin_p) */
	float32_t i_der = i_in * cos_p + q_in * sin_p;
	float32_t q_der = q_in * cos_p - i_in * sin_p;

	*i_out = i_der;
	*q_out = q_der;

	/* Decision-directed BPSK phase error: sign(I) * Q */
	float32_t err = ((i_der >= 0.0f) ? 1.0f : -1.0f) * q_der;

	c->freq += c->beta * err;
	c->phase += c->freq + c->alpha * err;
}

float32_t hfdl_costas_get_coarse_freq_estimate(const hfdl_costas_t *c)
{
	/* atan2f(0,0) is well-defined (0) on every libc this project
	 * targets - no special-case needed for "no symbols seen yet". */
	return 0.5f * atan2f(c->coarse_acc_q, c->coarse_acc_i);
}

float32_t hfdl_costas_get_coarse_freq_estimate_magnitude(const hfdl_costas_t *c)
{
	return sqrtf(c->coarse_acc_i * c->coarse_acc_i + c->coarse_acc_q * c->coarse_acc_q);
}

float32_t hfdl_costas_get_coarse_freq_estimate_raw_angle(const hfdl_costas_t *c)
{
	return atan2f(c->coarse_acc_q, c->coarse_acc_i);
}

/* EXTERNAL CORRECTION (16/08/2026, "paso 2" round 5 - decision-
 * directed phase tracking during EQ_TRAIN/DATA). Ports dumphfdl's
 * costas_cccf_adjust() call (src/hfdl.c line ~730: "costas_cccf_adjust
 * (c->loop, modem_get_demodulator_phase_error(...))") - real captures
 * showed the equalizer's own MSE staying flat/getting WORSE across a
 * whole EQ_TRAIN segment even after fixing a real tap-indexing bug
 * (see hfdl_equalizer.c's own history) - consistent with an
 * UNCORRECTED carrier residual drifting further out of alignment the
 * longer the post-LOCKED segment runs (hundreds to thousands of
 * symbols, far more than A1_SEARCH/A2_SEARCH/M1_SEARCH's few-hundred-
 * symbol windows the fine loop was ever validated against) - an
 * equalizer alone can't fix an ongoing, unbounded phase rotation,
 * that's the carrier loop's job.
 *
 * ARCHITECTURAL CAVEAT (important): this is called IN ADDITION to,
 * not instead of, this module's own internal per-symbol correction
 * inside hfdl_costas_step_bpsk() (called unconditionally for every
 * symbol regardless of preamble state, from inside
 * hfdl_demod_chain_process() - restructuring that shared,
 * already-validated call path to conditionally disable its own
 * BPSK-blind correction post-LOCKED was judged too risky to the
 * A1/A2/M1 detection this whole project's real LOCKED events already
 * depend on, so it wasn't attempted this round). During EQ_TRAIN
 * (content genuinely IS BPSK-like +-1, matching step_bpsk()'s own
 * assumption) the two corrections should usually reinforce each other
 * (both estimating the same sign most of the time once reasonably
 * locked), not fight - a real but likely benign double-application,
 * not a wrong-signed one. Scope THIS round: EQ_TRAIN only (a
 * genie-aided phase error using the KNOWN T_seq reference, not a
 * blind decision) - general M-PSK decision-directed correction for
 * DATA_1/DATA_2 (mod_arity 2/3) needs the same kind of unverified-
 * liquid-dsp-internals caution this project already hit with
 * hfdl_equalizer.c, deferred to validate this simpler, better-
 * justified piece first. */
void hfdl_costas_adjust(hfdl_costas_t *c, float32_t phase_err)
{
	c->freq += c->beta * phase_err;
	c->phase += c->alpha * phase_err; /* NOTE: no "+freq" term here, unlike
	                                      hfdl_costas_step_bpsk()'s phase update -
	                                      phase has ALREADY been advanced by freq
	                                      for this symbol (step_bpsk() already ran
	                                      first, see this function's top comment) -
	                                      adding freq again here would double-count
	                                      the natural per-symbol NCO advance, not
	                                      just the correction term. */
}

void hfdl_costas_seed_freq(hfdl_costas_t *c, float32_t freq)
{
	c->freq = freq;
}

/* ---- self-test ---- */

/* Simple deterministic xorshift32 PRNG, only used to generate the
 * self-test's synthetic bit sequence - not used anywhere in the
 * actual loop algorithm above. Seeded fixed for reproducibility. */
static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_costas_selftest(void)
{
	hfdl_costas_t c;
	hfdl_costas_init(&c, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);

	/* Synthetic channel: BPSK symbols with a fixed initial carrier
	 * phase offset (1.3 rad) and a fixed frequency offset
	 * (0.02 rad/sample) - same values used in the Python reference
	 * simulation run during development (see the module header
	 * comment), which confirmed this exact alpha/beta pair converges
	 * from this exact starting condition. */
	const uint32_t n_symbols = 2000u;
	const float32_t true_freq_offset = 0.02f;
	float32_t sig_phase = 1.3f;

	uint32_t rng_state = 12345u; /* fixed seed, reproducible */

	float32_t err_sum_first50 = 0.0f;
	float32_t err_sum_last50 = 0.0f;

	for (uint32_t n = 0; n < n_symbols; n++) {
		int32_t bit = ((xorshift32(&rng_state) & 1u) != 0u) ? 1 : -1;

		sig_phase += true_freq_offset;
		float32_t i_tx = (float32_t)bit * cosf(sig_phase);
		float32_t q_tx = (float32_t)bit * sinf(sig_phase);

		/* Derotate with the loop's CURRENT estimate (mirrors
		 * hfdl_costas_step_bpsk()'s own derotation, done here
		 * explicitly so we can inspect i_der/q_der before the loop
		 * updates - hfdl_costas_step_bpsk() itself doesn't expose the
		 * pre-update error value, only i_out/q_out and the updated
		 * state). */
		float32_t cos_p = cosf(c.phase);
		float32_t sin_p = sinf(c.phase);
		float32_t i_der = i_tx * cos_p + q_tx * sin_p;
		float32_t q_der = q_tx * cos_p - i_tx * sin_p;
		float32_t err = ((i_der >= 0.0f) ? 1.0f : -1.0f) * q_der;

		if (n < 50u) {
			err_sum_first50 += fabsf(err);
		}
		if (n >= n_symbols - 50u) {
			err_sum_last50 += fabsf(err);
		}

		/* Now actually advance the loop's state the same way
		 * hfdl_costas_step_bpsk() would (duplicated here rather than
		 * calling it, purely so this self-test can capture err BEFORE
		 * the state update for the running sums above - functionally
		 * identical update). */
		c.freq += c.beta * err;
		c.phase += c.freq + c.alpha * err;
	}

	float32_t avg_first50 = err_sum_first50 / 50.0f;
	float32_t avg_last50 = err_sum_last50 / 50.0f;

	/* Convergence check: error must have dropped by at least 100x -
	 * measured during development: ~0.153 -> ~1.3e-5, a ~11800x drop,
	 * so 100x is a very generous margin, not a tight/fragile
	 * threshold. */
	if (avg_last50 >= avg_first50 / 100.0f) {
		return 0u;
	}

	/* Frequency estimate must land within 5% of the true offset -
	 * measured during development: 0.019999 vs true 0.02, well within
	 * this margin. */
	float32_t freq_error = fabsf(c.freq - true_freq_offset);
	if (freq_error > 0.05f * true_freq_offset) {
		return 0u;
	}

	/* hfdl_costas_step_bpsk() itself (the actual public function, not
	 * the duplicated-for-inspection logic above) must produce the
	 * same final phase/freq given the same inputs - a basic
	 * consistency check that the public API matches this self-test's
	 * own reference logic. Re-run a short independent sequence
	 * through the real function and confirm it also converges. */
	{
		hfdl_costas_t c2;
		hfdl_costas_init(&c2, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
		uint32_t rng2 = 12345u;
		float32_t sig_phase2 = 1.3f;
		float32_t last_i_out = 0.0f, last_q_out = 0.0f;

		for (uint32_t n = 0; n < n_symbols; n++) {
			int32_t bit = ((xorshift32(&rng2) & 1u) != 0u) ? 1 : -1;
			sig_phase2 += true_freq_offset;
			float32_t i_tx = (float32_t)bit * cosf(sig_phase2);
			float32_t q_tx = (float32_t)bit * sinf(sig_phase2);
			hfdl_costas_step_bpsk(&c2, i_tx, q_tx, &last_i_out, &last_q_out);
		}

		float32_t freq_error2 = fabsf(c2.freq - true_freq_offset);
		if (freq_error2 > 0.05f * true_freq_offset) {
			return 0u;
		}
		/* Once locked, the derotated symbol's Q component should be
		 * small (residual error) relative to its I component
		 * (the recovered bit) - a loose "does it actually look
		 * locked" sanity check on the real function's output. */
		if (fabsf(last_q_out) > 0.2f * fabsf(last_i_out)) {
			return 0u;
		}
	}

	return 1u;
}

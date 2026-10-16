#include "hfdl_equalizer.h"
#include <string.h>
#include <math.h>

void hfdl_equalizer_init(hfdl_equalizer_t *eq, float32_t mu)
{
	memset(eq->taps_i, 0, sizeof(eq->taps_i));
	memset(eq->taps_q, 0, sizeof(eq->taps_q));
	memset(eq->delay_i, 0, sizeof(eq->delay_i));
	memset(eq->delay_q, 0, sizeof(eq->delay_q));
	/* BUG FOUND AND FIXED (16/08/2026, round 4 - real captures showed
	 * equalizer MSE stuck at ~1.17-1.23 - close to "d and y are
	 * uncorrelated" - regardless of training, worse than the FIRST
	 * window's MSE more often than not). This used to put the unit
	 * impulse at HFDL_EQ_LEN/2 (index 7, the CENTER tap) - but
	 * hfdl_equalizer_push()'s own delay-line convention (see header's
	 * struct comment) puts the NEWEST sample at index HFDL_EQ_LEN-1
	 * (14), not index 7. So the "identity/pass-through" starting
	 * point this comment claimed was actually a 7-symbol-DELAYED
	 * pass-through - hfdl_equalizer_execute() at t=0 returned
	 * delay[7], the sample from SEVEN pushes ago, not the current one.
	 * Comparing that against the CURRENT training symbol
	 * (HFDL_T_SEQ[bitmask][t_idx], no delay) in
	 * hfdl_equalizer_step() meant training started from a large,
	 * artificial misalignment that has nothing to do with the real
	 * channel - LMS had to first learn to undo a 7-symbol shift before
	 * it could even start addressing real ISI, which real (noisy,
	 * genuinely-ISI-distorted) captures didn't have the training
	 * budget/simplicity to fully escape within one real EQ_TRAIN
	 * segment (hfdl_equalizer_selftest()'s own synthetic 2-tap channel
	 * DID have that luxury - simple enough that 135 symbols of LMS was
	 * apparently enough to adapt around the bug, which is exactly why
	 * the self-test "passed" despite this being broken).
	 * FIX: unit impulse now at index HFDL_EQ_LEN-1 (the NEWEST sample
	 * position, matching hfdl_equalizer_push()'s actual convention) -
	 * genuine pass-through at t=0, no artificial delay to unlearn.
	 * This does give up some pre-cursor (future-symbol) ISI-correction
	 * capability a center-tap design would have (all HFDL_EQ_LEN-1=14
	 * remaining taps are now purely POST-cursor/past-symbol
	 * correction) - a real, deliberate trade-off, but post-cursor ISI
	 * is the dominant real-world mechanism for most HF channels, and a
	 * correct simple design beats a fancier one with a live bug in
	 * it. */
	eq->taps_i[HFDL_EQ_LEN - 1u] = 1.0f;
	eq->mu = mu;
	eq->input_power_avg = 1.0f; /* seed at 1.0 (not 0.0) - see push()'s guard comment on why starting
	                                at exactly 0 would be a problem, and 1.0 is a reasonable neutral
	                                starting assumption (no normalization at all) until real samples
	                                arrive and the EMA settles on the actual level */
}

void hfdl_equalizer_push(hfdl_equalizer_t *eq, float32_t i_in, float32_t q_in)
{
	/* INPUT AMPLITUDE NORMALIZATION (16/08/2026, round 6) - see
	 * hfdl_equalizer_t's input_power_avg field comment. EMA time
	 * constant (0.05, ~20-symbol effective window) picked as a
	 * reasonable middle ground - fast enough to track real AGC
	 * settling within roughly one EQ_TRAIN window (T_LEN=15 symbols),
	 * slow enough not to itself become noisy sample-to-sample. NOT
	 * independently verified/swept against real captures the way
	 * hfdl_costas.c's own EMA lambda was (see that file's own history
	 * for the kind of sweep this could get if it turns out to matter)
	 * - a reasonable first choice, not a measured-optimal one.
	 *
	 * OVERRIDABLE (20/08/2026) - that "if it turns out to matter"
	 * moment: the gain-collapse fix (see hfdl_equalizer_step()'s own
	 * comment) is now confirmed on real captures to bring tap energy
	 * into a tight, healthy band near 1.0 (leak_rate=0.15: mean 0.86-
	 * 0.87 across two separate captures) - but eq_i/eq_q's own OUTPUT
	 * MAGNITUDE stays wildly scattered (0.09 to 2.10 within the SAME
	 * captures) despite that healthy, stable tap energy. Stable
	 * filter gain but scattered output points at something upstream
	 * of the taps entirely - THIS normalization, applied before the
	 * taps ever see a sample. If real signal power is changing faster
	 * than this EMA's ~20-symbol window can track, every individual
	 * sample gets normalized against a LAGGING estimate of power, not
	 * its own true instantaneous level - exactly the kind of per-
	 * symbol scatter observed, independent of how well the taps
	 * themselves are behaving. `make HFDL_EQ_POWER_EMA_LAMBDA=0.2f`
	 * for a faster (more responsive, less smoothed) tracking window
	 * to test this directly. */
	#ifndef HFDL_EQ_POWER_EMA_LAMBDA
	#define HFDL_EQ_POWER_EMA_LAMBDA 0.05f
	#endif
	float32_t inst_power = i_in * i_in + q_in * q_in;
	eq->input_power_avg += HFDL_EQ_POWER_EMA_LAMBDA * (inst_power - eq->input_power_avg);
	if (eq->input_power_avg < 1.0e-6f) {
		eq->input_power_avg = 1.0e-6f; /* guard against dividing by ~0 during a silent stretch -
		                                   same "never divide by an unguarded near-zero" discipline
		                                   this project already applies elsewhere (e.g.
		                                   hfdl_framer.c's own RMS guard) */
	}
	float32_t norm = 1.0f / sqrtf(eq->input_power_avg);
	float32_t i_norm = i_in * norm;
	float32_t q_norm = q_in * norm;

	/* Shift everything down by one (index 0 = oldest, drops off the
	 * end), newest sample goes into the last slot - see header's
	 * struct comment for this indexing convention. */
	for (uint32_t k = 0; k < HFDL_EQ_LEN - 1u; k++) {
		eq->delay_i[k] = eq->delay_i[k + 1u];
		eq->delay_q[k] = eq->delay_q[k + 1u];
	}
	eq->delay_i[HFDL_EQ_LEN - 1u] = i_norm;
	eq->delay_q[HFDL_EQ_LEN - 1u] = q_norm;
}

void hfdl_equalizer_execute(const hfdl_equalizer_t *eq, float32_t *i_out, float32_t *q_out)
{
	/* Complex dot product: y = sum(taps[k] * delay[k]) */
	float32_t y_i = 0.0f;
	float32_t y_q = 0.0f;
	for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
		y_i += eq->taps_i[k] * eq->delay_i[k] - eq->taps_q[k] * eq->delay_q[k];
		y_q += eq->taps_i[k] * eq->delay_q[k] + eq->taps_q[k] * eq->delay_i[k];
	}
	*i_out = y_i;
	*q_out = y_q;
}

void hfdl_equalizer_step(hfdl_equalizer_t *eq, float32_t d_i, float32_t d_q, float32_t y_i, float32_t y_q)
{
	float32_t err_i = d_i - y_i;
	float32_t err_q = d_q - y_q;
	float32_t mu_eff = eq->mu;

	/* NORMALIZED LMS option (18/09/2026). liquid-dsp's eqlms_cccf_step() - the
	 * equalizer dumphfdl actually uses, with eqlms_cccf_set_bw(eq, 0.1) - is a
	 * NORMALIZED LMS: w += mu * conj(err) * x / x2_sum, where x2_sum is the
	 * energy of the whole tap window, sum |x|^2 (verified against liquid-dsp's
	 * src/equalization/src/eqlms.proto.c, EQLMS(_step): "w[n+1] = w[n] +
	 * mu*conj(d-d_hat)*x[n]/(x[n]' * conj(x[n]))"). This is the point the
	 * header's "HONEST UNCERTAINTY" section could not confirm at the time. The
	 * update below historically lacked that division. With the input
	 * normalized to unit power per sample, x2_sum is about HFDL_EQ_LEN (15), so
	 * the same "mu" gave a step ~15x LARGER than the reference's: mu=0.1 here
	 * behaves like an NLMS step of ~1.5 (stable only below 2, and any burst of
	 * input power above the running average pushes it over). That matches what
	 * this project measured on real captures - mu=0.1 diverged ("~2.5x the
	 * first-window MSE"), mu=0.03 did not, and huge training errors show up
	 * occasionally. Set HFDL_EQ_NLMS=1 to use the reference's rule; mu then has
	 * the reference's meaning (0.1 = dumphfdl's value). Default 0 keeps the old
	 * behaviour so results stay comparable until it has been A/B tested. */
#ifndef HFDL_EQ_NLMS
#define HFDL_EQ_NLMS 0
#endif
#if HFDL_EQ_NLMS
	{
		float32_t x2 = 0.0f;
		for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
			x2 += eq->delay_i[k] * eq->delay_i[k] + eq->delay_q[k] * eq->delay_q[k];
		}
		mu_eff = (x2 > 1.0e-6f) ? (eq->mu / x2) : 0.0f; /* liquid: no update when the window is empty */
	}
#endif

	/* taps[k] += mu * conj(delay[k]) * error - see header's top
	 * comment for why this exact (textbook, unnormalized) form was
	 * chosen. conj(delay[k]) = delay_i[k] - j*delay_q[k]; expanding
	 * (delay_i - j*delay_q) * (err_i + j*err_q):
	 *   real: delay_i*err_i + delay_q*err_q
	 *   imag: delay_i*err_q - delay_q*err_i */
	for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
		eq->taps_i[k] += mu_eff * (eq->delay_i[k] * err_i + eq->delay_q[k] * err_q);
		eq->taps_q[k] += mu_eff * (eq->delay_i[k] * err_q - eq->delay_q[k] * err_i);
	}

	/* GAIN-COLLAPSE FIX (20/08/2026) - real captures across many
	 * sessions all showed output magnitude stuck around ~0.43-0.48
	 * instead of the ~1.0 the T_seq reference (+-1.0) trains toward -
	 * and, critically, a NEW diagnostic added specifically to test
	 * this confirmed it directly: sum(|taps|^2), which starts at
	 * exactly 1.0 (see hfdl_equalizer_init()'s unit-gain identity
	 * init), was measured settling around 0.17-0.39 (mean ~0.29,
	 * sqrt~0.54) across 22 samples spanning many different real
	 * segments - a genuine, repeatable EQUILIBRIUM the unconstrained
	 * LMS update reaches, not noise or instability. This is a
	 * documented failure mode for error-minimizing-only LMS
	 * equalizers (no explicit gain constraint): under real noise, a
	 * lower-gain solution can locally reduce instantaneous error
	 * without reaching the true full-gain optimum - this project's
	 * own equalizer self-test uses a NOISELESS synthetic channel
	 * (see hfdl_equalizer_selftest()'s own CHANNEL_TAP0/TAP1 setup,
	 * no explicit noise term), which is exactly why this never
	 * showed up there: with zero noise, minimizing MSE and
	 * preserving full gain coincide, so there's no incentive for
	 * the collapse to happen at all in that specific test.
	 *
	 * FIX: after every LMS update, renormalize the WHOLE tap vector
	 * back to unit energy (sum(|taps|^2)==1.0) - a standard,
	 * well-established technique (a unit-norm projection step) for
	 * exactly this failure mode, applied without touching the LMS
	 * update itself (already verified textbook-correct this
	 * session). Overridable for A/B comparison against today's
	 * (collapsing) behavior - `make HFDL_EQ_DISABLE_GAIN_RENORM=1` -
	 * default ON since this is a well-motivated, directly-measured
	 * fix, not a speculative one, but not yet confirmed end-to-end
	 * on real hardware (does it actually get crc_ok=1 now?) the way
	 * mu=0.03 was before being trusted as the new default. */
#ifndef HFDL_EQ_DISABLE_GAIN_RENORM
#define HFDL_EQ_DISABLE_GAIN_RENORM 0
#endif
#if !HFDL_EQ_DISABLE_GAIN_RENORM
	{
		/* SOFT pull toward unit energy, NOT a hard renormalization
		 * every step (20/08/2026, found the hard way: an initial
		 * attempt that forced tap_energy to EXACTLY 1.0 after every
		 * single update broke this very self-test's own convergence
		 * check - mse_last only reached 0.098 vs the required <0.048,
		 * confirmed by adding temporary debug prints before trusting
		 * either version, not assumed). The TRUE equalizing solution
		 * for a real channel doesn't generally have EXACTLY unit tap
		 * energy - forcing it every step fights the LMS gradient
		 * itself, actively preventing convergence, not just
		 * constraining it. HFDL_EQ_GAIN_LEAK_RATE controls how much
		 * of the way toward unit energy each step corrects - 0.0
		 * would be no correction at all (today's collapsing
		 * behavior), 1.0 would be the hard version that just broke
		 * this test. Small and gentle, matching the module's own mu
		 * tuning philosophy (this project already knows small step
		 * sizes matter here - mu=0.03 vs mu=0.1 was the very first
		 * bug found and fixed this whole investigation). */
		#ifndef HFDL_EQ_GAIN_LEAK_RATE
		#define HFDL_EQ_GAIN_LEAK_RATE 0.02f
		#endif
		float32_t tap_energy = 0.0f;
		for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
			tap_energy += eq->taps_i[k] * eq->taps_i[k] + eq->taps_q[k] * eq->taps_q[k];
		}
		if (tap_energy > 1.0e-6f) { /* guard against dividing by ~0 - same discipline this project already applies elsewhere (e.g. input_power_avg's own guard) */
			float32_t full_scale = 1.0f / sqrtf(tap_energy);
			float32_t scale = 1.0f + (float32_t)HFDL_EQ_GAIN_LEAK_RATE * (full_scale - 1.0f);
			for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
				eq->taps_i[k] *= scale;
				eq->taps_q[k] *= scale;
			}
		}
	}
#endif
}

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_equalizer_selftest(void)
{
	/* Synthetic 2-tap ISI channel (16/08/2026) - deliberately NOT
	 * dumphfdl's real HF channel model (this project has no access to
	 * one, and doesn't need one for THIS test - see header's top
	 * comment, this validates convergence behavior generically, not
	 * agreement with a specific real channel). y[n] = 1.0*x[n] +
	 * 0.5*x[n-1]: a textbook, deliberately non-trivial ISI channel
	 * (the 0.5 echo is a substantial fraction of the main tap, enough
	 * that an unequalized receiver would see real, measurable
	 * intersymbol interference, but not so extreme the LMS update
	 * used here couldn't plausibly converge on it within a realistic
	 * training length). */
	#define CHANNEL_TAP0 1.0f
	#define CHANNEL_TAP1 0.5f

	uint32_t rng = 0xC0FFEEu;
	hfdl_equalizer_t eq;
	hfdl_equalizer_init(&eq, HFDL_EQ_DEFAULT_MU);

	float32_t prev_tx_i = 0.0f, prev_tx_q = 0.0f; /* x[n-1], for the channel model */

	/* TRAINING PHASE - same length as one real HFDL EQ_TRAIN window
	 * (HFDL_T_LEN=15 symbols - see hfdl_data_segment.h) repeated 9
	 * times, matching the real initial training burst's total length
	 * (9*15=135 symbols) so this test reflects a realistic amount of
	 * adaptation opportunity, not an arbitrarily generous one. */
	#define TRAIN_SYMBOLS (9u * 15u)
	float32_t train_err_sq_first10 = 0.0f;
	float32_t train_err_sq_last10 = 0.0f;

	for (uint32_t n = 0; n < TRAIN_SYMBOLS; n++) {
		float32_t tx_i = (xorshift32(&rng) & 1u) ? 1.0f : -1.0f; /* known +-1 "T_seq" stand-in */
		float32_t tx_q = 0.0f; /* BPSK-like real-only reference, keeps this test's arithmetic simple
		                          without losing any generality for a COMPLEX equalizer - the LMS
		                          update itself is fully complex regardless */

		float32_t rx_i = CHANNEL_TAP0 * tx_i + CHANNEL_TAP1 * prev_tx_i; /* channel distortion */
		float32_t rx_q = CHANNEL_TAP0 * tx_q + CHANNEL_TAP1 * prev_tx_q;
		prev_tx_i = tx_i;
		prev_tx_q = tx_q;

		hfdl_equalizer_push(&eq, rx_i, rx_q);
		float32_t y_i, y_q;
		hfdl_equalizer_execute(&eq, &y_i, &y_q);

		float32_t err_sq = (tx_i - y_i) * (tx_i - y_i) + (tx_q - y_q) * (tx_q - y_q);
		if (n < 10u) {
			train_err_sq_first10 += err_sq;
		}
		if (n >= TRAIN_SYMBOLS - 10u) {
			train_err_sq_last10 += err_sq;
		}

		hfdl_equalizer_step(&eq, tx_i, tx_q, y_i, y_q);
	}

	/* CONVERGENCE CHECK: mean squared error over the LAST 10 training
	 * symbols should be substantially lower than over the FIRST 10 -
	 * proof the equalizer is actually learning to undo the channel,
	 * not just sitting at its unit-impulse starting point (which
	 * would leave the 0.5-tap echo's error fully present throughout,
	 * with no first-vs-last difference at all). */
	float32_t mse_first = train_err_sq_first10 / 10.0f;
	float32_t mse_last = train_err_sq_last10 / 10.0f;
	if (mse_last >= mse_first * 0.3f) {
		return 0u; /* not a strong enough improvement - see comment above */
	}

	/* GAIN-RENORMALIZATION CHECK (20/08/2026) - see
	 * hfdl_equalizer_step()'s own comment for the full story (real
	 * captures directly measured tap energy settling around 0.29
	 * instead of the 1.0 identity-init value, a documented LMS gain-
	 * collapse failure mode). This synthetic test's own channel is
	 * NOISELESS (see this function's own CHANNEL_TAP0/TAP1 comment),
	 * so collapse wouldn't naturally occur here even without the fix -
	 * this check isn't proving the fix PREVENTS collapse (this test
	 * can't exercise that), it's confirming the renormalization CODE
	 * ITSELF is correct (produces a sane, near-1.0 tap energy, not a
	 * broken value from e.g. a formula error) - complementary to,
	 * not a replacement for, checking real captures directly. Skipped
	 * entirely when HFDL_EQ_DISABLE_GAIN_RENORM=1 (nothing meaningful
	 * to check - tap energy is expected to drift then, that's the
	 * whole point of disabling it for comparison). */
#ifndef HFDL_EQ_DISABLE_GAIN_RENORM
#define HFDL_EQ_DISABLE_GAIN_RENORM 0
#endif
#if !HFDL_EQ_DISABLE_GAIN_RENORM
	{
		float32_t tap_energy = 0.0f;
		for (uint32_t k = 0; k < HFDL_EQ_LEN; k++) {
			tap_energy += eq.taps_i[k] * eq.taps_i[k] + eq.taps_q[k] * eq.taps_q[k];
		}
		/* Soft correction (see hfdl_equalizer_step()'s own comment on
		 * why this isn't pinned to exactly 1.0) - just confirms it's
		 * meaningfully closer to 1.0 than the ~0.29 collapse this fix
		 * exists to counteract, not exactly at 1.0. */
		if (tap_energy < 0.5f) {
			return 0u; /* gain-renorm should keep this well above the ~0.29 collapse value seen on real captures without the fix */
		}
	}
#endif

	/* FROZEN-TAPS DATA CHECK: after training, freeze the taps (no more
	 * hfdl_equalizer_step() calls - same as dumphfdl during DATA_1/
	 * DATA_2, see header's top comment) and confirm the SAME channel,
	 * fed FRESH random symbols, still equalizes well - proves the
	 * converged taps generalize beyond the exact training symbols,
	 * not just memorized them somehow (LMS doesn't memorize - this is
	 * mostly a sanity check the taps found a genuinely channel-
	 * matching solution). */
	float32_t data_err_sq_sum = 0.0f;
	#define DATA_TEST_SYMBOLS 30u /* one real HFDL_DATA_FRAME_LEN - see hfdl_data_segment.h */
	for (uint32_t n = 0; n < DATA_TEST_SYMBOLS; n++) {
		float32_t tx_i = (xorshift32(&rng) & 1u) ? 1.0f : -1.0f;
		float32_t tx_q = 0.0f;

		float32_t rx_i = CHANNEL_TAP0 * tx_i + CHANNEL_TAP1 * prev_tx_i;
		float32_t rx_q = CHANNEL_TAP0 * tx_q + CHANNEL_TAP1 * prev_tx_q;
		prev_tx_i = tx_i;
		prev_tx_q = tx_q;

		hfdl_equalizer_push(&eq, rx_i, rx_q);
		float32_t y_i, y_q;
		hfdl_equalizer_execute(&eq, &y_i, &y_q);
		/* NO hfdl_equalizer_step() call here - taps stay frozen, matching dumphfdl's DATA_1/DATA_2 */

		data_err_sq_sum += (tx_i - y_i) * (tx_i - y_i) + (tx_q - y_q) * (tx_q - y_q);
	}
	float32_t data_mse = data_err_sq_sum / (float32_t)DATA_TEST_SYMBOLS;
	if (data_mse >= mse_first * 0.3f) {
		return 0u; /* frozen taps should still perform close to the trained level on fresh data */
	}

	return 1u;
}

#ifndef HFDL_SYMSYNC_H
#define HFDL_SYMSYNC_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL symbol timing recovery - ported line-by-line from liquid-dsp's
 * real symsync_crcf (jgaeddert/liquid-dsp, MIT), specifically the
 * exact parameters dumphfdl uses (szpajder/dumphfdl, GPL-3.0-or-later,
 * src/hfdl.c): symsync_crcf_create_kaiser(k=3, m=3, beta=0.9f, M=16)
 * followed by symsync_crcf_set_lf_bw(0.001f). Runs on
 * hfdl_rational_resampler.c's 5400Hz output.
 *
 * SESSION HISTORY (worth keeping, not just for flavor - it explains
 * a real bug that was caught and fixed, not assumed away): an initial
 * hand-derived Python port of this algorithm used rate=1/M as the
 * loop's initial condition, which produced a symbol-index sequence
 * that never converged at all (flat variance ~0.08 even after
 * 384,000 samples). Rather than assume this was just "a very slow
 * loop", a small standalone C program was written linking against a
 * REAL, locally-compiled libliquid.a to generate a ground-truth trace
 * from the actual symsync_crcf object (see the project's session
 * notes for the generator source and the resulting reference trace,
 * hfdl_symsync_reference_trace.csv) - which showed symsync_reset()'s
 * true initial condition is rate = k/k_out (3.0 for k=3, k_out=1),
 * NOT 1/M. Correcting this made the Python port's qualitative
 * behaviour (smooth, monotonic post-transient convergence) match the
 * real object's trace closely - the remaining differences are
 * explained by the two using different PRNGs for the test bit
 * sequence (Python's random vs C's rand()), not a further bug.
 *
 * This module's own self-test therefore does NOT attempt exact
 * numerical convergence-to-a-value (bt=0.001 is a deliberately narrow
 * loop bandwidth - the real reference trace above was still slowly
 * drifting even after 3000 symbols, nowhere near numerically settled)
 * - it checks structural table sanity plus loop STABILITY (bounded,
 * smoothly-varying tau/rate, no divergence/NaN) over a moderate run.
 * Full validation - does this actually lock within HFDL's real
 * preamble budget (531 symbols, per the project's phy-layer document)
 * - needs a real or realistic signal (the sigidwiki.com samples
 * mentioned in the project's session notes), not something a
 * boot-time self-test can practically establish for a loop this slow.
 *
 * hfdl_symsync_mf_pfb[][]/hfdl_symsync_dmf_pfb[][]: the 16-phase,
 * 18-tap-per-phase interpolation filterbank and its derivative,
 * generated the same way as hfdl_rational_resampler.c's table (real
 * liquid_firdes_kaiser() call, then the derivative computed via the
 * exact finite-difference + normalization formula from
 * symsync_create() - see that function for the 0.06/hdh_max
 * normalization constant, replicated exactly, not approximated).
 * NOTE: these are NOT the same as hfdl_matched_filter.c's 19-tap
 * filter - that one (applied upstream, before this module) is HFDL's
 * actual RRC-like pulse-matched filter; these are symsync's own
 * internal interpolation filter, a separate, generic Nyquist lowpass
 * used purely for fractional-delay interpolation and timing-error
 * detection, per liquid-dsp's own naming (confusingly reusing "mf" /
 * "matched filter" for a different purpose - flagged here so nobody
 * conflates the two later).
 */

#define HFDL_SYMSYNC_K            3   /* input samples/symbol (5400Hz / 1800baud) */
#define HFDL_SYMSYNC_M            16  /* number of polyphase filter bank phases */
#define HFDL_SYMSYNC_K_OUT        1   /* output samples/symbol - one interpolated sample per symbol */
#define HFDL_SYMSYNC_SUBFILTER_LEN 18

/* T/2 FRACTIONALLY-SPACED TAP (25/08/2026, "ecualizador T/2" round,
 * pieza 1) - see hfdl_symsync_step()'s own comment below for the full
 * rationale. HFDL_SYMSYNC_K=3, so one symbol period is ~3 raw input
 * samples (del~=3.0); a HALF-symbol offset is therefore ~1.5 raw
 * samples earlier than the main (T) tap's interpolation point - i.e.
 * it needs the polyphase filterbank evaluated against a filter
 * history window that is between 0 and roughly ceil(HFDL_SYMSYNC_K/2)
 * raw samples "older" than the window the main tap uses (exactly how
 * many depends on the loop's CURRENT del/bf, not a fixed constant,
 * since del tracks the adaptively-estimated rate, not always exactly
 * 3.0). 2 extra raw-sample slots of history comfortably covers this
 * for any del in HFDL's realistic operating range (del would have to
 * drift to >4.0 raw samples/symbol - already well outside anything
 * this loop's own rate-adjustment range produces - before 2 stopped
 * being enough); hfdl_symsync_step() clamps and flags (via
 * *half_lag_clamped_out) the pathological case rather than reading
 * out-of-window history silently wrong. */
#define HFDL_SYMSYNC_HIST_EXTRA    2
#define HFDL_SYMSYNC_HIST_LEN     (HFDL_SYMSYNC_SUBFILTER_LEN + HFDL_SYMSYNC_HIST_EXTRA)

extern const float32_t hfdl_symsync_mf_pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN];
extern const float32_t hfdl_symsync_dmf_pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN];

typedef struct {
	/* Shared input history (chronological, oldest-to-newest),
	 * mirrors liquid's single shared WINDOW inside firpfb - same
	 * pattern as hfdl_rational_resampler.c. Extended by
	 * HFDL_SYMSYNC_HIST_EXTRA raw samples beyond the 18-tap
	 * subfilter's own window so the T/2 tap (see
	 * HFDL_SYMSYNC_HIST_EXTRA's own comment) can be computed from a
	 * slightly-older windowed view of the SAME shared history,
	 * without adding any new state to the timing loop itself. */
	float32_t hist_i[HFDL_SYMSYNC_HIST_LEN];
	float32_t hist_q[HFDL_SYMSYNC_HIST_LEN];

	float32_t tau;    /* instantaneous fractional timing offset */
	float32_t tau_decim; /* tau captured once per symbol (when decim_counter resets) - mirrors liquid's own tau_decim, the meaningful once-per-symbol timing estimate (see symsync_crcf_get_tau()) - raw per-call tau can legitimately jump by several units in a single hfdl_symsync_step() call when rate>>1 (as it is here, rate=k/k_out=3), so tau_decim, not raw tau, is what should be checked for smoothness/lock behaviour */
	float32_t rate;   /* tracked samples/symbol ratio - nominal k/k_out */
	float32_t del;    /* per-input-sample tau increment (rate + loop correction) */
	int32_t   b;      /* current filterbank phase index (rounded) */
	float32_t bf;     /* soft (unrounded) filterbank phase index */
	uint32_t  decim_counter;
	float32_t loop_y_prev; /* loop filter's y[n-1] state (see module header - IIR biquad collapses to 1st order for HFDL's bt=0.001) */
	uint8_t   is_locked;   /* mirrors symsync_lock()/unlock() - while locked, the loop filter stops updating (timing estimate frozen) */
} hfdl_symsync_t;

/* Resets `q` to its initial state (mirrors symsync_reset(), including
 * the rate=k/k_out initial condition - see the module header for why
 * this specific value matters). Call once at init, or again at the
 * start of a new HFDL burst. Does not touch is_locked - call
 * hfdl_symsync_unlock() explicitly if a fresh acquisition is wanted. */
void hfdl_symsync_reset(hfdl_symsync_t *q);

void hfdl_symsync_lock(hfdl_symsync_t *q);
void hfdl_symsync_unlock(hfdl_symsync_t *q);

/* Processes one input I/Q sample, writing up to
 * HFDL_SYMSYNC_M interpolated output samples to i_out/q_out
 * (capacity must be at least HFDL_SYMSYNC_M - matches liquid's own
 * worst-case output count per input sample) and reporting how many
 * were actually written via *n_out (0, 1, or more - variable by
 * design, since this block adapts the output rate to track the
 * incoming symbol timing).
 *
 * T/2 TAP (25/08/2026, "ecualizador T/2" round, pieza 1) -
 * i_half_out/q_half_out (same capacity/indexing as i_out/q_out -
 * i_half_out[n] is the half-symbol-early companion of i_out[n], for
 * every n in [0, *n_out)) carry a SECOND interpolated sample per
 * symbol, taken exactly half a symbol period EARLIER than the main
 * (T-spaced) output, computed from the SAME shared filter history and
 * the SAME already-converged tau/rate the main tap uses - this
 * function's own timing-error detector and loop filter (the part with
 * real LOCKED events behind it) are completely unmodified, this is a
 * pure additional read of state that already exists. Feeding both
 * outputs into an equalizer that pushes at 2 samples/symbol (see
 * hfdl_equalizer.h's own "DELIBERATE DEVIATION FROM DUMPHFDL" comment,
 * which flagged exactly this as the deferred next step) reproduces
 * dumphfdl's actual fractionally-spaced push/execute pattern.
 *
 * half_lag_clamped_out (non-NULL, one flag per n_out slot laid out
 * the same way): 0 normally; 1 if that symbol's required history lag
 * exceeded HFDL_SYMSYNC_HIST_EXTRA and got clamped (see that macro's
 * own comment on when this can happen - not expected in normal
 * operation, but surfaced rather than silently producing a
 * wrong-offset sample). Pass NULL to skip this check (i_half_out/
 * q_half_out are still computed and clamped internally either way). */
void hfdl_symsync_step(hfdl_symsync_t *q, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out,
		float32_t *i_half_out, float32_t *q_half_out,
		uint8_t *half_lag_clamped_out, uint32_t *n_out);

/* Returns the loop's current tau_decim - the once-per-symbol timing
 * estimate (see hfdl_symsync_t's own field comment for why tau_decim,
 * not raw tau, is the meaningful value to read). Added 21/08/2026 to
 * give real visibility into whether the timing loop is actually
 * converging within a real segment's length, instead of inferring it
 * blindly from crc_ok alone (see this module's HFDL_SYMSYNC_LOOP_B0
 * comment for the investigation this supports). */
float32_t hfdl_symsync_get_tau_decim(const hfdl_symsync_t *q);

/* Runs the module's standalone self-test - see the module header for
 * why this checks stability/sanity rather than numerical convergence.
 * Zero dependency on real hardware or a real HFDL signal. */
uint8_t hfdl_symsync_selftest(void);

/* Loop filter coefficients, precomputed from set_lf_bw(0.001f)'s formula
 * (see symsync_crcf_set_lf_bw() - alpha=1-bt, beta=0.220*bt, a=0.5, b=0.495,
 * B=[beta,0,0], A=[1-a*alpha, -b*alpha, 0]) then normalized by A[0] the
 * same way iirfiltsos_rrrf_set_coefficients() does - collapses to a
 * simple 1st-order IIR since B[1]=B[2]=A[2]=0 for these parameters.
 * Declared in the HEADER (not hfdl_symsync.c, where the algorithm that
 * actually uses them lives) specifically so main.c's own boot-time
 * build-config print can see these names too, the same way it already
 * prints HFDL_EQ_ACTIVE_MU etc - an earlier real capture session had
 * NO visibility into which bt these constants corresponded to for
 * that specific log, the exact ambiguity this project already hit
 * once before with HFDL_EQ_MU_OVERRIDE and fixed the same way.
 *
 * OVERRIDABLE (20/08/2026) - this module's own top comment already
 * documented, from this project's OWN earlier testing, that bt=0.001
 * "was still slowly drifting even after 3000 symbols, nowhere near
 * numerically settled" - and every real HFDL segment (2160-5040+
 * symbols) is the SAME order of magnitude or less. Several real
 * captures, across BPSK/PSK4/PSK8 and both weak AND confirmed-good
 * SNR, all showed the same crc_ok=0/low-magnitude failure signature -
 * after ruling out Costas correction (both its EQ_TRAIN and DATA-
 * state forms were tested, including a check that symsync's OWN loop
 * is not accidentally frozen - hfdl_symsync_lock() is confirmed never
 * called anywhere in the real pipeline, only at module init to force
 * UNLOCKED, so the loop is always actively adapting) this narrow-
 * bandwidth timing loop possibly never converging WITHIN one
 * segment's length is the next real candidate. The C preprocessor
 * cannot do floating-point arithmetic, so this can't derive B0/
 * NEG_A1/RATE_ADJ from a single `bt` override the way
 * HFDL_EQ_MU_OVERRIDE derives one value from itself - each of the 3
 * constants below needs its own override, computed OFFLINE from
 * set_lf_bw()'s exact formula above (verified this session to
 * reproduce today's own bt=0.001 constants bit-for-bit before trusting
 * it for anything else) for whatever candidate bt is being tried.
 * Some already-computed candidates (same formula, just a different
 * bt) for a quick A/B test without needing to redo the derivation:
 *   bt=0.005: B0=0.0021890547263681594f NEG_A1=0.9801492537313433f RATE_ADJ=0.0025f
 *   bt=0.01:  B0=0.004356435643564357f  NEG_A1=0.9703960396039604f RATE_ADJ=0.005f
 *   bt=0.02:  B0=0.008627450980392156f  NEG_A1=0.9511764705882352f RATE_ADJ=0.01f
 *   bt=0.05:  B0=0.020952380952380955f  NEG_A1=0.8957142857142857f RATE_ADJ=0.025f
 * Default (no overrides) keeps today's bt=0.001 behavior exactly -
 * this is a diagnostic knob, not a claim that a wider bt is correct;
 * it may turn out to destabilize the loop the way mu=0.1 destabilized
 * the equalizer, needing its own "how far can this go" sweep rather
 * than assuming wider is simply better. */
#ifndef HFDL_SYMSYNC_LOOP_B0
#define HFDL_SYMSYNC_LOOP_B0     0.0004395604395604396f
#endif
#ifndef HFDL_SYMSYNC_LOOP_NEG_A1
#define HFDL_SYMSYNC_LOOP_NEG_A1 0.9880219780219781f
#endif
#ifndef HFDL_SYMSYNC_RATE_ADJ
#define HFDL_SYMSYNC_RATE_ADJ    0.0005f /* 0.5 * bt(0.001) */
#endif

#endif /* HFDL_SYMSYNC_H */

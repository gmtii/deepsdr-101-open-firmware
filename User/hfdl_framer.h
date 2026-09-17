#ifndef HFDL_FRAMER_H
#define HFDL_FRAMER_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL frame synchronizer (preamble correlator) - Phase 2, next step
 * after hfdl_demod_chain.c's Costas-corrected symbol output (see that
 * module's "MISSING PIECES" note - this is the piece that fills the
 * "no framer state machine yet" gap).
 *
 * WHAT THIS MODULE IS: a GENERIC sliding-window BPSK sequence
 * correlator plus a small state machine (SEARCHING / PREAMBLE_FOUND /
 * LOCKED) built around it. It knows nothing HFDL-specific by itself -
 * it is handed a reference bit sequence (±1 per bit) at init time and
 * looks for that exact sequence in the incoming BPSK-decided symbol
 * stream. This split is deliberate: the correlator engine (this file)
 * could be built, self-tested, and verified structurally WITHOUT
 * having the real HFDL A2/M1 bit patterns in hand yet.
 *
 * WHAT THIS MODULE IS NOT (YET): it does NOT contain HFDL's actual A2
 * or M1 preamble sequences. Per dumphfdl's own stats naming
 * (szpajder/dumphfdl, doc/STATSD_METRICS.md - the one piece of
 * dumphfdl documentation actually consulted for this module, since
 * GitHub's raw source view was not reachable in the environment this
 * was written in): "A2 is a pseudo-random bit sequence in the
 * preamble of a HFDL frame ... occurs twice in every preamble.
 * Finding a second occurrence ... is a good indication that a HFDL
 * frame has been found." and "M1 ... indicates the modulation and
 * interleaver type used to encode the frame." Both are almost
 * certainly liquid-dsp msequence_*() outputs, the same family this
 * project's hfdl_descrambler.c already ported bit-for-bit from
 * dumphfdl's src/hfdl.c + liquid-dsp's src/sequence/src/msequence.c -
 * but THIS module was not written against that same direct source
 * read, and the actual A2/M1 generator polynomial/init-state/length
 * were not re-derived or guessed here. See HFDL_FRAMER_REF_BITS_TODO
 * below - filling that in (from the project's own
 * HFDL_phy_layer_params_dumphfdl document, referenced in
 * hfdl_costas.h/hfdl_symsync.h, or from a direct read of dumphfdl's
 * hfdl.c the way hfdl_descrambler.c's own header documents doing) is
 * a prerequisite for this module to detect a REAL HFDL preamble.
 * Until then, hfdl_framer_selftest() below exercises the correlator
 * engine against a SYNTHETIC test pattern only (clearly marked as
 * such) - it proves the state machine and correlation math work, not
 * that HFDL_FRAMER_REF_BITS matches anything transmitted over the
 * air.
 *
 * Preamble budget: per this project's own phy-layer document (see
 * hfdl_symsync.h's header), HFDL's preamble is 531 symbols - so a
 * SEARCHING->LOCKED transition should happen well within that many
 * incoming symbols once locked onto a real signal; a correlator that
 * hasn't fired by symbol ~531 into a burst (see hfdl_scope's
 * burst_active gate in demod_am.c) is a reasonable "give up, go back
 * to SEARCHING" bound for the caller to apply, though this module
 * doesn't enforce that timeout itself (kept caller's responsibility,
 * same "small orthogonal pieces" shape as the rest of this chain).
 *
 * ALGORITHM: maintains a circular buffer of the last
 * HFDL_FRAMER_MAX_REF_LEN symbols. CHANGED (16/08/2026, A2-score-
 * ceiling investigation - see hfdl_framer.c's top-of-function comment
 * on hfdl_framer_process_symbol() for the full mechanism/evidence):
 * used to hard-decide each symbol to +-1.0f (sign of the Costas-
 * corrected I) before pushing; now pushes the SOFT (analog) I value
 * directly - this header's own note used to call that "a future
 * soft-decision version", turned out to be the actual fix for a real
 * ceiling, not a nice-to-have. Each new symbol: push into the ring,
 * then compute normalized correlation (dot product / ref_len) between
 * the last ref_len symbols and the reference sequence. Score is no
 * longer hard-bounded to [-1,+1] the way it was under pure +-1
 * hard-decision (that bound assumed both correlation factors were
 * exactly +-1) - now it's "1.0 at a perfect noiseless match given
 * AGC's target amplitude of ~1.0", so on_thresh/off_thresh (and
 * HFDL_CORR_THRESHOLD_A1/A2/M1) may need re-tuning against real
 * captures rather than assuming the old hard-decision-era values
 * still apply as-is. BOTH +ref and -ref are checked (max of the two)
 * - HFDL is coherent PSK with a genuine 180-degree phase ambiguity out
 * of the Costas loop (see hfdl_costas.h's header, and
 * hfdl_descrambler.h's note on why: ambiguity is resolved by preamble
 * correlation, not per-symbol differential encoding) - so the
 * correlator itself is what resolves that ambiguity, not something
 * upstream. Hysteresis (ON/OFF thresholds) on the correlation score,
 * same shape as hfdl_scope.c's burst detector, avoids chattering
 * right at the threshold.
 *
 * NON-COHERENT BLOCK COMBINING (16/08/2026, round 5 - the "maybe it's
 * the 127-symbol window itself" investigation): a single 127-symbol
 * coherent dot product (n_blocks=1, the ONLY mode available before
 * this round) requires the residual carrier phase to stay consistent
 * across the WHOLE window - real captures showed the fine Costas
 * loop's residual (whatever it actually is; the coarse estimator
 * turned out to be too noise-dominated at real SNR to even measure it
 * reliably, see hfdl_costas.h/.c's own history) is enough to cap
 * real-world scores around ~0.35-0.40 regardless of every seeding/
 * loop-tuning approach tried so far. Setting n_blocks > 1 splits the
 * ref_len window into n_blocks equal-length sub-blocks (127 is PRIME,
 * so an uneven split always leaves 127 % n_blocks leftover symbols at
 * the END of the window unused by the correlation - accepted as a
 * minor, deliberate loss rather than added complexity for an uneven
 * last block), each correlated COHERENTLY on its own (own local +-ref
 * sign resolution, own local RMS normalization), and the block scores
 * combined NON-COHERENTLY (mean of |block score|) into the overall
 * score. This trades away some coherent processing gain (shorter
 * blocks integrate less noise-averaging each) for tolerance to phase
 * drift ACROSS blocks - the same technique GPS/spread-spectrum
 * acquisition uses when frequency uncertainty precludes full-window
 * coherent integration. n_blocks=1 (the default, unless a caller
 * opts in) is EXACTLY the pre-existing single-coherent-block
 * behavior, byte-for-byte - existing callers/thresholds are
 * unaffected unless they explicitly ask for more blocks.
 */

/* Upper bound on the reference sequence length this module can be
 * configured with - sizes the internal ring buffer statically (no
 * dynamic allocation, project convention). 531 (the full preamble
 * length) is a safe generous ceiling for A2 (which per dumphfdl's
 * stats naming occurs TWICE within the preamble, so is necessarily
 * shorter than the whole preamble) or M1. Bump if the real sequence
 * turns out longer once sourced. */
#define HFDL_FRAMER_MAX_REF_LEN 256u

/* HFDL's preamble length in symbols, per this project's own phy-layer
 * document (see hfdl_symsync.h's header) - not re-derived here, just
 * reused as a documented constant callers can use for a "give up
 * searching after this many symbols into a burst" timeout. */
#define HFDL_FRAMER_PREAMBLE_SYMBOLS 531u

typedef enum {
	HFDL_FRAMER_SEARCHING = 0,  /* no correlation peak seen yet */
	HFDL_FRAMER_PREAMBLE_FOUND, /* correlation crossed ON threshold at least once - tentative */
	HFDL_FRAMER_LOCKED         /* caller-confirmed lock (see hfdl_framer_confirm_lock()) - correlator keeps running but state won't drop back to SEARCHING on a single low sample, only via hfdl_framer_reset() */
} hfdl_framer_state_t;

typedef struct {
	/* Reference sequence, as handed to hfdl_framer_init() - NOT
	 * owned/copied, caller's array must outlive this struct. Each
	 * element must be exactly +1.0f or -1.0f. */
	const float32_t *ref_bits;
	uint32_t ref_len; /* <= HFDL_FRAMER_MAX_REF_LEN, enforced in hfdl_framer_init() */

	/* NON-COHERENT BLOCK COMBINING (16/08/2026) - see this file's top
	 * comment. 1 = old single-coherent-block behavior (default).
	 * >1 = split ref_len into this many equal sub-blocks (dropping
	 * ref_len % n_blocks leftover trailing symbols), correlate each
	 * independently, combine non-coherently. Clamped to
	 * [1, ref_len] in hfdl_framer_init() - n_blocks > ref_len makes
	 * no sense (blocks would be zero-length). */
	uint32_t n_blocks;

	float32_t on_thresh;  /* normalized correlation to DECLARE preamble found (e.g. 0.8f) */
	float32_t off_thresh; /* lower than on_thresh (hysteresis) - unused for state drop today, kept for a future soft-lock-loss check */

	/* Ring buffer of the last HFDL_FRAMER_MAX_REF_LEN symbols' HARD
	 * SIGN (+1/-1, packed as int8_t - not float32_t anymore).
	 *
	 * HISTORY: was hard-decision +1.0f/-1.0f originally, changed
	 * 16/08/2026 to store the SOFT (analog, Costas-derotated) I
	 * value instead, to fix a real "A2 ceiling" problem (residual
	 * carrier frequency error hard-flipping borderline symbols
	 * across the +-1 decision boundary - see this file's own
	 * framer_correlate_block() history for the fully story).
	 *
	 * Changed BACK to hard-decision storage 21/08/2026, after that
	 * same day's session found - and confirmed on real captured
	 * hardware data (host-harness cross-check against a working
	 * dumphfdl reference) - that framer_correlate_block() itself
	 * needed to switch to a hard-decision AGREEMENT score (matching
	 * dumphfdl's own real algorithm) regardless of what the ring
	 * stores, since the soft dot-product score was systematically
	 * weaker than its own noise floor on real signals (see that
	 * function's own comment for the measured numbers). Once the
	 * correlator itself hard-slices at READ time, storing the full
	 * analog value here is pure waste - int8_t (+-1) is exactly
	 * equivalent for what's actually read, at 1/4 the size. This
	 * project's RAM is at its structural limit (see this session's
	 * own repeated single-byte .bss fights) and this ring exists in
	 * 9 separate instances (1 a_framer + 8 m1_framer[]) - the switch
	 * back recovers 768 bytes/instance x 9 = 6912 bytes total. */
	int8_t ring[HFDL_FRAMER_MAX_REF_LEN];
	uint32_t ring_head;   /* next write index into ring[] */
	uint32_t n_pushed;    /* total bits pushed since last reset - used to know when the ring is "full enough" to correlate at all */

	hfdl_framer_state_t state;
	float32_t last_score;     /* most recent normalized correlation score. With n_blocks==1 (default), |value| <= 1.0f-ish and sign indicates which phase (+ref vs -ref) matched, same as always. With n_blocks>1, this is a NON-COHERENT combined score - always >= 0.0f (block-level sign ambiguity is resolved independently per block and thrown away by the combining step, so there's no single overall "which phase matched" answer anymore), and hfdl_framer_process_symbol()'s threshold check is on the value directly, not on +-score. */
	uint32_t symbols_since_reset; /* for the caller's own HFDL_FRAMER_PREAMBLE_SYMBOLS timeout - this module doesn't act on it itself */
} hfdl_framer_t;

/* Configures the correlator with a reference sequence (see struct
 * comment - not copied, must outlive `f`) and hysteresis thresholds,
 * and clears all state. ref_len must be <= HFDL_FRAMER_MAX_REF_LEN
 * (silently clamped if not, so this can't overflow the ring - but a
 * clamped length means the correlator is running against a truncated,
 * WRONG reference, so callers should treat a clamp as a configuration
 * bug to fix, not something to rely on). n_blocks: 1 for the original
 * single-coherent-block correlator, >1 for non-coherent block
 * combining (see this file's top comment) - clamped to [1, ref_len]. */
void hfdl_framer_init(hfdl_framer_t *f, const float32_t *ref_bits, uint32_t ref_len,
		uint32_t n_blocks, float32_t on_thresh, float32_t off_thresh);

/* Resets ring/state/counters without touching the configured
 * reference/thresholds - call at the start of a new search (e.g. on
 * hfdl_scope_burst_active()'s rising edge, same spot
 * hfdl_demod_chain_init() is already called from in demod_am.c's
 * probe). */
void hfdl_framer_reset(hfdl_framer_t *f);

/* Feeds ONE Costas-corrected complex symbol (one call per symbol, NOT
 * per raw sample - same rate note as hfdl_costas.h). CHANGED
 * (16/08/2026): pushes the SOFT i_sym value (no more hard sign()
 * decision - see this file's top comment), then re-correlates.
 * Returns 1 if this call's correlation just crossed on_thresh (a NEW
 * tentative preamble candidate at THIS symbol - state becomes
 * HFDL_FRAMER_PREAMBLE_FOUND if it wasn't already past SEARCHING),
 * else 0. The q_sym argument is accepted for API symmetry with the
 * rest of the chain (Costas outputs complex symbols) but is NOT used
 * - see hfdl_framer.c's comment on why folding it in isn't the right
 * fix even now that this is soft-decision. */
uint8_t hfdl_framer_process_symbol(hfdl_framer_t *f, float32_t i_sym, float32_t q_sym);

/* Caller-driven transition PREAMBLE_FOUND -> LOCKED, e.g. once a
 * SECOND correlation peak (the A2-occurs-twice signature dumphfdl's
 * own stats rely on, see this file's header) has also been seen, or
 * once M1 has additionally been matched - deliberately left as a
 * caller decision rather than something this generic correlator
 * enforces itself, since "what counts as confirmed" is an HFDL-
 * specific policy (A2 twice, optionally +M1), not a correlator-engine
 * concern. No-op if state is still SEARCHING (nothing to confirm). */
void hfdl_framer_confirm_lock(hfdl_framer_t *f);

/* Feeds one symbol WITHOUT correlating/checking - just pushes the
 * soft i_sym into the ring (CHANGED 16/08/2026, see this file's top
 * comment - was a hard-decision push before), same as the push half
 * of hfdl_framer_process_symbol() but skipping the correlate+
 * threshold step entirely. For the "skip N symbols before the next
 * check" pattern real HFDL framing needs (see hfdl_preamble_sync.h -
 * HFDL's real symbols_wanted mechanism keeps the sliding register
 * current across skipped symbols, it just doesn't bother correlating
 * every single one). */
void hfdl_framer_push_only(hfdl_framer_t *f, float32_t i_sym, float32_t q_sym);

/* Re-arms detection (state -> HFDL_FRAMER_SEARCHING) WITHOUT touching
 * the ring buffer or push/symbol counters - use this to look for a
 * SECOND occurrence of the same reference sequence with the same
 * running history (e.g. HFDL's A1->A2 transition, or a retry after a
 * miss), as opposed to hfdl_framer_reset() which also wipes the ring
 * and would force waiting for ref_len fresh symbols again. No-op on
 * last_score (left as whatever it last was, purely informational). */
void hfdl_framer_rearm(hfdl_framer_t *f);

hfdl_framer_state_t hfdl_framer_get_state(const hfdl_framer_t *f);
float32_t hfdl_framer_get_last_score(const hfdl_framer_t *f);

/* GLOBAL POLARITY (16/08/2026, "paso 2" round 2 - see
 * hfdl_equalizer.h's top comment for why this exists: dumphfdl tracks
 * a single global 180-degree-ambiguity bitmask from A1's correlation
 * sign, used to pick which T_seq[] variant to train the equalizer
 * against, and to XOR-correct every subsequent hard-decision bit).
 * This project's non-coherent block combining (see this file's top
 * comment) resolves that same ambiguity independently PER BLOCK now,
 * which doesn't hand back a single global answer the way dumphfdl's
 * original single-coherent-block correlator did.
 *
 * This getter recovers a global answer WITHOUT changing how detection
 * itself works: at the moment of a match, f->ring[] holds EXACTLY the
 * winning ref_len-symbol window (hfdl_framer_process_symbol() only
 * just returned 1 because THIS window's correlation crossed
 * threshold) - so a single, ordinary COHERENT dot product of that
 * whole window against the reference (n_blocks=1 style, ignoring this
 * particular f's own configured n_blocks) gives a clean, cheap,
 * well-defined signed answer: positive means the window matched
 * +ref_bits, negative means it matched -ref_bits. Call this right
 * after a match (hfdl_framer_process_symbol() returned 1, or
 * equivalently hfdl_preamble_sync_get_state() just became
 * HFDL_PREAMBLE_LOCKED) - the ring's contents are only guaranteed to
 * still BE that winning window until the next symbol pushes something
 * new in. Returns the raw signed coherent correlation (same RMS-
 * normalized scale as this file's other scores) - callers that only
 * need the polarity should check its SIGN, not its magnitude (a
 * single coherent 127-symbol dot product is exactly the case this
 * project moved AWAY from for detection robustness - see this file's
 * top comment - so this number can legitimately be a weak, noisy
 * correlation even on a real match; it's only being used here to pick
 * a sign, not to re-decide whether the match was real). */
float32_t hfdl_framer_get_global_polarity(const hfdl_framer_t *f);

/* DIAGNOSTIC (15/08/2026) - see hfdl_framer.c's comment. Read-only
 * peek at the correlation score for whatever's currently in the ring,
 * regardless of push_only()/process_symbol() state - safe to call
 * every symbol, no side effects. */
float32_t hfdl_framer_peek_score(const hfdl_framer_t *f);

/* Self-test: exercises the correlator engine ONLY, against a
 * SYNTHETIC reference pattern generated in this test (NOT HFDL's real
 * A2/M1 - see this file's header for why those aren't in this module
 * yet). Embeds the test pattern at a known symbol offset inside a
 * stream of random +1/-1 "noise" bits (both phases tested, to check
 * the +ref/-ref ambiguity-resolving max works), and checks: (a) no
 * false HFDL_FRAMER_PREAMBLE_FOUND transition anywhere in the pure-
 * noise region before the embedded pattern, (b) a transition DOES
 * happen at the expected symbol once the full pattern has been
 * pushed, for BOTH polarities. This is a PLUMBING/CORRELATOR-MATH
 * check only - it says nothing about real HFDL preamble detection
 * until HFDL_FRAMER_REF_BITS_TODO is replaced with the real sequence
 * and re-tested against it (ideally against the sigidwiki.com HFDL
 * samples the project's other self-tests reference, same as
 * everywhere else in this chain). */
uint8_t hfdl_framer_selftest(void);

/* SELF-TEST (16/08/2026, round 5 - non-coherent block combining).
 * Builds a synthetic 127-symbol reference, then feeds it back
 * corrupted by a LINEAR PHASE RAMP across the window (simulating a
 * small constant residual carrier frequency error, same failure mode
 * documented at length in hfdl_framer.h's top comment and this
 * project's own session history) at a ramp rate chosen to be
 * moderate - enough to meaningfully hurt a single coherent 127-symbol
 * block, not so extreme that even short sub-blocks would fail too
 * (that extreme case wouldn't demonstrate anything useful either
 * way). Checks that hfdl_framer_init()'d with n_blocks=4 crosses a
 * reasonable threshold on this corrupted stream while n_blocks=1
 * does NOT - i.e. actually demonstrates the tolerance improvement
 * this round's change is meant to provide, on the host, before
 * trusting it enough to burn another real-hardware capture cycle on
 * it. Returns 1 if the multi-block advantage is confirmed, 0
 * otherwise (either mode behaved unexpectedly - see
 * hfdl_framer.c's implementation for exactly what's checked). */
uint8_t hfdl_framer_selftest_block_combining(void);

#endif /* HFDL_FRAMER_H */

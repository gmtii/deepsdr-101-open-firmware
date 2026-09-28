#ifndef HFDL_VITERBI_H
#define HFDL_VITERBI_H

#include <stdint.h>
#include <stdbool.h>

/*
 * hfdl_viterbi - K=7 r=1/2 convolutional (Viterbi) decoder, the FEC
 * layer HFDL's PDU payload actually rides on (19/08/2026, "paso 2",
 * next real piece after the equalizer per hfdl_data_segment.h's own
 * top comment - hfdl_pdu.c/hfdl_deinterleaver.c/hfdl_descrambler.c
 * are already built with self-tests but have no real data to process
 * without this).
 *
 * SOURCE OF TRUTH - this is a from-scratch, line-by-line PORT of
 * libfec's viterbi27_port.c (Phil Karn, KA9Q, LGPL, Copyright Feb
 * 2004 - "K=7 r=1/2 Viterbi decoder in portable C"), the exact file
 * dumphfdl vendors and calls (szpajder/dumphfdl, GPL-3.0-or-later,
 * confirmed by downloading the actual repo this session, not
 * remembered/assumed):
 *   - src/hfdl.c #include "libfec/fec.h", decode_user_data():
 *     create_viterbi27(user_data_bits_cnt) once per channel at setup,
 *     then per data segment: init_viterbi27(v, 0) (KNOWN start state
 *     0 - not tail-biting), update_viterbi27_blk(v, viterbi_input,
 *     viterbi_output_len), chainback_viterbi27(v, viterbi_output,
 *     viterbi_output_len, 0) (KNOWN end state 0 too - HFDL's
 *     convolutional code is TERMINATED/flushed at both ends, not a
 *     tail-biting code).
 *   - src/libfec/fec.h: V27POLYA=0x6d, V27POLYB=0x4f (generator
 *     polynomials - this project's HFDL_VITERBI_POLYA/POLYB below).
 *   - src/libfec/viterbi27_port.c: the actual butterfly/traceback
 *     algorithm this module ports - same branch-metric table
 *     construction (set_viterbi27_polynomial(), see
 *     HFDL_VITERBI_BRANCHTAB_0/1 below - PRECOMPUTED at Python/build
 *     time from that exact function rather than replicated as
 *     runtime init code, see those tables' own comment for why), same
 *     BFLY() butterfly update, same chainback bit-shift-through-
 *     endstate traceback.
 *
 * CODE RATE HANDLING (confirmed from hfdl.c's decode_user_data(),
 * NOT part of this module - the CALLER's job, see below): the
 * project's "code_rate" field (2 or 4, from hfdl_data_segment.h's
 * hfdl_frame_params_t/HFDL_FRAME_PARAMS) is NOT this decoder's own
 * rate - the underlying convolutional code is ALWAYS rate 1/2 K=7.
 * code_rate==4 means dumphfdl's overall link runs an EXTRA outer
 * repetition (every post-encoder chip transmitted twice) on top of
 * the rate-1/2 code, and the receiver averages consecutive soft-bit
 * PAIRS (branchless average: (a&b)+((a^b)>>1), see hfdl.c line ~1032)
 * before this decoder ever sees them - so by the time
 * hfdl_viterbi_update_block() is called, the input is ALWAYS rate-1/2
 * soft bits, regardless of code_rate. The averaging step itself
 * belongs in the caller (the eventual demod_am.c wiring, alongside
 * the deinterleaver pop loop), not here - this module has zero
 * knowledge of HFDL's code_rate field, same "zero dependency on the
 * rest of the pipeline" discipline as hfdl_crc.c/hfdl_pdu.c/
 * hfdl_deinterleaver.c/hfdl_descrambler.c already established.
 *
 * SOFT BIT CONVENTION: dumphfdl's modem_demodulate_soft() (liquid-dsp)
 * produces one uint8_t per encoded bit, 0..255, where 0 means "very
 * confidently a 0" and 255 means "very confidently a 1" (matching
 * libfec's own BFLY() which XORs the branch table's {0,255} against
 * the soft sample and takes 510-metric for the complementary branch -
 * see viterbi27_port.c's BFLY macro, ported unchanged below). This
 * decoder does NOT do its own hard-decision thresholding or soft-bit
 * generation - that's the caller's job (feeding it whatever
 * modem_demodulate_soft-equivalent produces), consistent with the
 * "confirm against real source, don't assume" approach the rest of
 * this project already takes.
 *
 * ZERO NEW PERSISTENT RAM (19/08/2026) - this project's build was
 * confirmed (same session, readelf) to sit EXACTLY at its 192KB RAM
 * boundary with 0 bytes of headroom before this module existed. A
 * real decode needs a "decisions" traceback buffer sized to the
 * NUMBER OF DECODED BITS (up to ~60KB for HFDL's largest real
 * combination - PSK8 double-slot, see HFDL_VITERBI_COMBO_TABLE_NOTE
 * below) - far too big for a new static array. Every buffer this
 * module touches (metrics, decisions) is therefore CALLER-PROVIDED
 * (pointers passed into hfdl_viterbi_init(), same "caller-owned
 * buffer" pattern hfdl_deinterleaver.h already uses for its table) -
 * this module itself adds NOTHING to .bss, not even the small
 * (512-byte) metrics arrays libfec keeps embedded in its own struct.
 * The intended real caller (not yet wired - see hfdl_viterbi.c's
 * bottom comment) is waterfall.h's existing, currently-UNUSED
 * waterfall_ram_borrow_for_hfdl()/waterfall_ram_return_from_hfdl()
 * pair (WATERFALL_RAM_BORROW_CAPACITY = 43008 bytes) - built ahead of
 * time, apparently for exactly this kind of "one big scratch buffer,
 * needed briefly, not while the waterfall display is active" need.
 *
 * HFDL_VITERBI_COMBO_TABLE_NOTE - required decisions-buffer size per
 * HFDL_FRAME_PARAMS M1 index (computed 19/08/2026, all formulas
 * derived from hfdl_data_segment.h's already-confirmed
 * data_segment_cnt/DATA_FRAME_LEN and this module's own
 * HFDL_VITERBI_DECISION_BYTES() macro below):
 *   M1=0 BPSK  72-seg rate1/4 (300bps single)  ->  4368 bytes - fits
 *   M1=1 BPSK  72-seg rate1/2 (600bps single)  ->  8688 bytes - fits
 *   M1=2 PSK4  72-seg rate1/2 (1200bps single) -> 17328 bytes - fits
 *   M1=3 PSK8  72-seg rate1/2 (1800bps single) -> 25968 bytes - fits
 *   M1=4 BPSK 168-seg rate1/4 (300bps double)  -> 10128 bytes - fits
 *   M1=5 BPSK 168-seg rate1/2 (600bps double)  -> 20208 bytes - fits
 *   M1=6 PSK4 168-seg rate1/2 (1200bps double) -> 40368 bytes - fits,
 *        but only 2640 bytes spare in the 43008-byte borrowed buffer
 *        - fine if nothing else needs that space at the same moment,
 *        worth remembering if the deinterleaver table (ALSO a
 *        candidate for the same borrowed buffer, see hfdl_deinterleaver.h)
 *        ends up needing to coexist with the decisions buffer rather
 *        than being freed first.
 *   M1=7 PSK8 168-seg rate1/2 (1800bps double) -> 60528 bytes -
 *        DOES NOT FIT (43008 available). Real, valid HFDL combination
 *        (1800bps is a real bitrate, double-slot frames are already
 *        confirmed in this project's own real captures) - NOT a
 *        hypothetical edge case to wave away. This module's
 *        hfdl_viterbi_init() DETECTS this (returns false, does
 *        nothing further) rather than silently overrunning the
 *        caller's buffer - see its own comment. FIXING it for real
 *        needs a traceback-DEPTH-LIMITED (sliding-window) decoder
 *        instead of full block-mode chainback - bounded, small,
 *        constant-size decision memory regardless of block length,
 *        the standard technique real hardware Viterbi decoders use -
 *        but that's a genuinely different algorithm (continuous
 *        decode-with-delay, not "buffer everything, chainback once"),
 *        deliberately NOT attempted this round - flagged here so it
 *        doesn't get lost, matching this project's own "flag it,
 *        don't guess" discipline elsewhere (see hfdl_equalizer.h's
 *        own deferred-items list for the same pattern).
 */

#define HFDL_VITERBI_K            7u   /* constraint length */
#define HFDL_VITERBI_NUM_STATES   64u  /* 2^(K-1) */
#define HFDL_VITERBI_POLYA        0x6du /* dumphfdl/libfec: V27POLYA - see top comment */
#define HFDL_VITERBI_POLYB        0x4fu /* dumphfdl/libfec: V27POLYB - see top comment */

/* One decoded output bit per decision_t per BUTTERFLY (32 of them,
 * packed 2 decisions/butterfly = 64 decision bits/w[2]) - same layout
 * as libfec's own `union { unsigned long w[2]; } decision_t` (here
 * pinned to explicit uint32_t rather than `unsigned long` - this
 * project's target is a 32-bit Cortex-M4 where the two are the same
 * size, but explicit is safer than relying on that across host-test
 * vs target builds). One decision_t is produced per PAIR of encoded
 * (rate-1/2) input bits, i.e. per ONE decoded output bit. */
typedef struct {
    uint32_t w[2];
} hfdl_viterbi_decision_t;

/* Path metrics for all 64 trellis states - one of these per "old" and
 * "new" metric buffer, swapped every decoded bit (see
 * hfdl_viterbi_t's old_metrics/new_metrics below). Caller-provided,
 * see top comment's ZERO NEW PERSISTENT RAM section - 256 bytes each. */
typedef struct {
    uint32_t w[HFDL_VITERBI_NUM_STATES];
} hfdl_viterbi_metric_t;

/* Decoder instance state. ALL buffer members are CALLER-OWNED
 * (pointers into memory the caller provides at hfdl_viterbi_init()
 * time, e.g. waterfall.h's borrowed scratch buffer) - see top
 * comment. Nothing here is itself an array. */
typedef struct {
    hfdl_viterbi_metric_t *metrics_a;   /* caller-owned, HFDL_VITERBI_NUM_STATES uint32_t */
    hfdl_viterbi_metric_t *metrics_b;   /* caller-owned, same size - old/new alternate between these two */
    hfdl_viterbi_metric_t *old_metrics; /* internal - points at metrics_a or metrics_b */
    hfdl_viterbi_metric_t *new_metrics; /* internal - points at the other one */

    hfdl_viterbi_decision_t *decisions;          /* caller-owned, decisions_capacity entries */
    uint32_t                 decisions_capacity; /* in hfdl_viterbi_decision_t units, NOT bytes */
    uint32_t                 decisions_used;     /* how many have been written so far this block - bounds-checked, see update_block() */
} hfdl_viterbi_t;

/* Bytes needed for a decisions buffer that can hold `nbits` decoded
 * output bits (nbits = viterbi_output_len in dumphfdl's own naming -
 * see top comment's CODE RATE HANDLING section for how a caller
 * computes this from num_encoded_bits/code_rate). +6 matches libfec's
 * own "room beyond the end of the encoder register" chainback margin
 * (viterbi27_port.c's create_viterbi27(): calloc(len+6, ...)) - NOT
 * an arbitrary safety margin, the chainback algorithm's own d+=6
 * "look past tail" step needs it. */
#define HFDL_VITERBI_DECISION_BYTES(nbits) (((nbits) + 6u) * sizeof(hfdl_viterbi_decision_t))

/* Resets decoder state for a new block. starting_state: which trellis
 * state the encoder is known to have started in (dumphfdl always
 * passes 0 - HFDL's convolutional code is flushed/terminated, not
 * tail-biting, see top comment). decisions_capacity is in
 * hfdl_viterbi_decision_t units (use HFDL_VITERBI_DECISION_BYTES() to
 * size the caller's buffer in bytes first, then pass
 * bytes/sizeof(hfdl_viterbi_decision_t) here, or just track the count
 * directly). Returns true if the given decisions_capacity is enough
 * for AT LEAST one output bit (a basic sanity floor - the real
 * per-nbits bounds check happens in hfdl_viterbi_update_block(), see
 * its own comment, since capacity vs actual-nbits-needed is a
 * relationship the caller controls by how it sized the buffer, not
 * something this function can fully validate up front without also
 * being told nbits). */
bool hfdl_viterbi_init(hfdl_viterbi_t *v,
        hfdl_viterbi_metric_t *metrics_a, hfdl_viterbi_metric_t *metrics_b,
        hfdl_viterbi_decision_t *decisions, uint32_t decisions_capacity,
        uint32_t starting_state);

/* Feeds a block of soft-decision rate-1/2 symbol pairs (nbits pairs,
 * so 2*nbits bytes of `syms`) and advances the trellis by nbits
 * decoded-output-bit steps. Returns false WITHOUT writing anything
 * past the caller's buffer if nbits would exceed decisions_capacity
 * (see top comment's M1=7/PSK8-double-slot case - this is the guard
 * that turns "silently overruns memory" into "cleanly refuses, caller
 * decides what to do" for that combination). Can be called more than
 * once per block (decisions_used accumulates) as long as the running
 * total stays within capacity - dumphfdl's own call pattern is a
 * single call per segment, but nothing here requires that. */
bool hfdl_viterbi_update_block(hfdl_viterbi_t *v, const uint8_t *syms, uint32_t nbits);

/* Traces back through the LAST nbits decisions written (via one or
 * more hfdl_viterbi_update_block() calls) and writes the decoded bits
 * into data_out, MSB-first within each byte, matching libfec's own
 * chainback_viterbi27() bit order exactly (needed for
 * hfdl_pdu.h's REVERSE_BYTE convention to line up correctly - see
 * that header's own note on why the caller must NOT skip that step).
 * data_out must be at least (nbits+7)/8 bytes. endstate: the trellis
 * state the encoder is known to have ENDED in (dumphfdl always passes
 * 0, same termination reasoning as starting_state above). Does not
 * modify decoder state - callable once after the matching
 * update_block() call(s), not incremental/streaming. */
void hfdl_viterbi_chainback(const hfdl_viterbi_t *v, uint8_t *data_out, uint32_t nbits, uint32_t endstate);

/* Diagnostic-only path-metric getter (added 19/08/2026) - see
 * hfdl_viterbi.c's own comment. Valid any time after at least one
 * hfdl_viterbi_update_block() call; not needed for normal decode
 * (chainback() doesn't use it), purely for comparing decode
 * "confidence" across real-capture attempts when crc_ok keeps coming
 * back false and there's no other way to tell noise apart from a
 * wiring bug. */
uint32_t hfdl_viterbi_get_final_metric(const hfdl_viterbi_t *v, uint32_t state);

/* Host-testable self-test (same "check before trusting" shape as
 * every other module in this project). Builds its own small K=7 r=1/2
 * reference ENCODER internally (not exposed as public API - this
 * decoder never needs to encode for real, only a self-test does) to
 * generate a KNOWN bit sequence's encoded soft-bit stream, decodes it
 * with THIS module, and checks for an exact match - first with clean
 * (0/255) soft bits, then again with several bits corrupted (soft
 * values nudged toward the wrong decision, and a couple of full hard
 * errors) to confirm actual error correction happens, not just clean
 * pass-through. Uses small on-stack buffers only (test block sizes
 * are tiny, nowhere near the real ~10-60KB captures need) - does NOT
 * exercise waterfall.h's borrow mechanism, that's the eventual real
 * caller's job once this is wired into demod_am.c (NOT done yet -
 * this round only builds and validates the decoder itself, see
 * hfdl_viterbi.c's bottom comment). Returns true if every check
 * passes. */
bool hfdl_viterbi_selftest(void);

#endif /* HFDL_VITERBI_H */

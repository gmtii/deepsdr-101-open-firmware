#ifndef HFDL_PAYLOAD_DECODE_H
#define HFDL_PAYLOAD_DECODE_H

#include "arm_math.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * hfdl_payload_decode - orchestrates the full DATA_1/DATA_2 symbol ->
 * PDU pipeline (19/08/2026, "integración completa" round): soft demod
 * (hfdl_soft_demod.c) -> descrambler phase-flip (hfdl_descrambler.c,
 * already built) -> deinterleaver (hfdl_deinterleaver.c, already
 * built) -> code-rate averaging -> Viterbi (hfdl_viterbi.c) ->
 * REVERSE_BYTE -> MPDU/LPDU parse (hfdl_pdu.c, already built). This
 * is the piece the resumen's own roadmap called out as missing:
 * "sin eso, hfdl_pdu.c/hfdl_deinterleaver.c/hfdl_descrambler.c... no
 * tienen datos reales que procesar" - all 3 of those already had
 * self-tests before this round, this module is what actually feeds
 * them real data for the first time.
 *
 * SOURCE OF TRUTH for the call ORDER (not any one algorithm - each
 * piece already cites its own): dumphfdl's decode_user_data()
 * (src/hfdl.c, confirmed by downloading the real repo this session):
 * per DATA symbol, in this exact order - descrambler_advance() first,
 * THEN phase-flip the symbol (by descrambler bit AND by the
 * preamble's own polarity bitmask - see hfdl_soft_demod.h's own note
 * on i/q scale), THEN soft-demod, THEN push each bit into the
 * deinterleaver. All of a segment's symbols get pushed before ANY
 * pop - see hfdl_deinterleaver.h's own "push phase, then pop phase,
 * same cursor" structural note, already correct in that module.
 *
 * MEMORY - this module needs 3 real buffers per segment (deinterleaver
 * table, Viterbi metrics, Viterbi decisions) plus small persistent
 * state (descrambler/deinterleaver/viterbi CONTEXT structs, which
 * must survive across many hfdl_payload_decode_feed_symbol() calls as
 * a segment's symbols arrive one at a time from demod_am.c's live
 * per-symbol loop). This project's build sits at EXACTLY 0 bytes of
 * RAM headroom (confirmed earlier this session, readelf) - so NONE of
 * that, not even the small context structs, can be a new module-level
 * static. Everything (small persistent state AND the big per-segment
 * buffers) is carved out of ONE borrowed region instead - see
 * hfdl_payload_decode_begin_segment()'s own comment for the exact
 * layout, and hfdl_scope.c's s_hfdl_ram/hfdl_scope_get_hfdl_ram() for
 * where that region comes from (the waterfall buffer, borrowed for
 * the whole duration of HFDL mode, same pattern
 * hfdl_deinterleaver.h's table already anticipated - see waterfall.h's
 * own "REUTILIZACION DE RAM PARA HFDL FASE 2" comment).
 *
 * COMBO COVERAGE (computed 19/08/2026, see hfdl_viterbi.h's own note
 * on the 8 combos' Viterbi-only sizes for the earlier, simpler
 * analysis) - requiring the deinterleaver table AND Viterbi's
 * metrics+decisions to be SIMULTANEOUSLY resident (this module does
 * NOT attempt to free/reuse the table's memory mid-segment for
 * Viterbi - see hfdl_payload_decode_finish()'s own comment for why
 * that reuse was deliberately NOT attempted this round) changes which
 * combos fit within the 43008-byte borrowed capacity:
 *   M1=0..5 (everything up to 600bps double-slot, PLUS both
 *     single-slot PSK4/PSK8 rates): fits, most with comfortable
 *     margin. This covers EVERY M1 index actually seen in this
 *     project's real captures so far (1, 3, 4).
 *   M1=6 (1200bps/PSK4 double-slot): table(10080) + viterbi(40880) =
 *     50960 bytes - does NOT fit simultaneously (even though Viterbi
 *     ALONE would, per hfdl_viterbi.h's table - the combined
 *     requirement is the new, tighter constraint this module adds).
 *   M1=7 (1800bps/PSK8 double-slot): already known not to fit from
 *     hfdl_viterbi.h alone, still doesn't.
 *   Both M1=6 and M1=7 are refused cleanly (see begin_segment()'s
 *   return value) rather than silently mishandled - fixing them for
 *   real needs either a genuine table/decisions-region-reuse scheme
 *   (deliberately not attempted this round - see finish()'s comment)
 *   or the same traceback-windowed Viterbi redesign hfdl_viterbi.h
 *   already flags for M1=7 alone. Flagged here, not guessed at.
 */

/* Call once when a new data segment starts (right after
 * HFDL_PREAMBLE_LOCKED, same point hfdl_equalizer_init()/
 * hfdl_data_segment_init() already get called - see demod_am.c).
 * `m1_index` is the M1 index dumphfdl/this project's own preamble
 * correlator already determines (hfdl_preamble_sync_get_m1_index()),
 * indexing hfdl_deinterleaver.h's existing hfdl_rate_params[8] table.
 * `ram`/`ram_capacity` are normally hfdl_scope_get_hfdl_ram()'s
 * output (see hfdl_payload_decode_begin_segment() below, the real
 * entry point demod_am.c calls) - exposed as an explicit parameter
 * here so a host-side self-test can supply its own buffer instead of
 * depending on hardware/waterfall.c.
 *
 * Layout carved from `ram` (in order, all computed from `m1_index`'s
 * hfdl_rate_params_t entry, nothing hardcoded):
 *   [0                                   .. sizeof(internal state))  - descrambler/deinterleaver/viterbi context structs
 *   [sizeof(state)                       .. + table_size)            - deinterleaver table
 *   [sizeof(state)+table_size            .. + 512)                   - Viterbi metrics_a (256B) + metrics_b (256B)
 *   [sizeof(state)+table_size+512        .. + decisions_bytes)       - Viterbi decisions
 * Returns false (touches nothing else) if ram is NULL, m1_index is
 * out of range, or the total needed exceeds ram_capacity (the M1=6/7
 * case - see top comment). */
bool hfdl_payload_decode_begin_segment_ex(uint32_t m1_index, uint8_t *ram, uint32_t ram_capacity);

/* Real entry point - forwards to the _ex version above using
 * hfdl_scope_get_hfdl_ram()'s buffer (hfdl_scope.h). Returns false
 * (and logs why, if DEBUG_UART_ENABLED) under the same conditions as
 * the _ex version, PLUS if HFDL mode isn't currently on (no RAM
 * borrowed at all). */
bool hfdl_payload_decode_begin_segment(uint32_t m1_index);

/* Call once per DATA_1/DATA_2-state symbol, in original arrival
 * order, with that symbol's EQUALIZED (hfdl_equalizer_execute()'s
 * output) i/q - same scale hfdl_soft_demod.h expects. `polarity_bitmask`
 * is the same bit this project's own EQ_TRAIN-derived polarity
 * decision already produces (demod_am.c's s_hfdl_eq_bitmask, 0 or 1 -
 * see hfdl_data_segment.h's HFDL_T_SEQ[bitmask][...] usage for the
 * other place that same value already gets used) - passed explicitly
 * rather than this module reaching into demod_am.c's own state, to
 * keep this module's only dependencies its declared #includes.
 * `ram` is the SAME pointer begin_segment_ex() was given - this
 * module has NO module-level static of its own, not even a single
 * cached pointer (see hfdl_payload_decode.c's own comment on why:
 * this project's build has 0 bytes of RAM headroom, and even one
 * 4-byte static pointer overflowed it during this round's first
 * build attempt) - every call recomputes where its state lives from
 * `ram` directly. Does nothing (safe no-op) if `ram` is NULL or no
 * segment is active (rate == NULL inside the state that lives at the
 * start of `ram` - see begin_segment_ex()). */
void hfdl_payload_decode_feed_symbol_ex(uint8_t *ram, float32_t eq_i, float32_t eq_q, uint32_t polarity_bitmask);

/* Real entry point - forwards to the _ex version using
 * hfdl_scope_get_hfdl_ram()'s buffer, recomputed on every call (see
 * the _ex version's comment on why there's no cached pointer). */
void hfdl_payload_decode_feed_symbol(float32_t eq_i, float32_t eq_q, uint32_t polarity_bitmask);

/* Call once when hfdl_data_segment_get_state() reports HFDL_DSEG_DONE
 * - pops everything from the deinterleaver, applies code-rate
 * averaging (see hfdl_viterbi.h's own note on why this belongs at the
 * integration layer, not inside the Viterbi module itself), runs the
 * Viterbi decode, reverses every output byte's bit order
 * (hfdl_pdu.h's REVERSE_BYTE convention), and hands the result to
 * hfdl_mpdu_parse_header()/hfdl_mpdu_extract_lpdus() (hfdl_pdu.c) -
 * printing a summary via debug_print_always() either way (decode
 * success/failure, CRC result, LPDU kinds found) so a real capture's
 * log shows the outcome without needing extra wiring per call site.
 * `ram` - see hfdl_payload_decode_feed_symbol_ex()'s comment on why
 * this is always explicit, never cached. `decoded_out`/`decoded_len_out`
 * are OPTIONAL (NULL is fine, and is what the production wrapper
 * below passes) - when non-NULL, written with the post-Viterbi,
 * post-REVERSE_BYTE PDU buffer's address/length (still valid at the
 * point this function returns, since the underlying storage is only
 * overwritten by the NEXT begin_segment_ex() call, not by anything
 * this function itself does afterward) - purely so
 * hfdl_payload_decode_selftest() can check the decoded bytes directly
 * against a known-good message, independent of hfdl_pdu.c's own
 * (already separately tested) parsing. Safe no-op if no segment is
 * active. */
void hfdl_payload_decode_finish_ex(uint8_t *ram, const uint8_t **decoded_out, uint32_t *decoded_len_out);

/* Real entry point - forwards to the _ex version using
 * hfdl_scope_get_hfdl_ram()'s buffer, with decoded_out/decoded_len_out
 * both NULL (production callers use the debug_print_always() summary
 * finish_ex() already prints, not this - see that function). */
void hfdl_payload_decode_finish(void);

/* On-screen CRC status (21/08/2026) - see hfdl_payload_decode.c's own
 * comment at the state declaration. Any pointer may be NULL if that
 * particular value isn't needed. last_valid_out is 0 until the first
 * decode attempt ever completes (finish_ex reaching either the "too
 * short" early-out or a real hfdl_mpdu_parse_header() call) - check it
 * before trusting last_ok_out. */
void hfdl_payload_decode_get_crc_status(uint8_t *last_ok_out, uint8_t *last_valid_out,
        uint8_t *attempts_out, uint8_t *good_out);

/* Host-testable self-test: builds a synthetic HFDL data segment from
 * scratch (known message bits -> CRC-appended -> conv-encoded ->
 * descrambled -> mapped to symbols -> deinterleaved - i.e. the exact
 * INVERSE of this module's own pipeline, built independently so the
 * test doesn't just check the code against itself) for a couple of
 * real rate/slot combinations, feeds it through
 * begin_segment_ex()/feed_symbol()/finish() using a host-side buffer,
 * and confirms the decoded MPDU/LPDU matches what was encoded.
 * Depends on hfdl_soft_demod/hfdl_descrambler/hfdl_deinterleaver/
 * hfdl_viterbi/hfdl_pdu/hfdl_crc, all already self-tested
 * independently - this test is specifically about the WIRING between
 * them (call order, bit order, phase-flip sign), the class of bug an
 * individually-correct-module can't catch on its own. */
bool hfdl_payload_decode_selftest(void);

#endif /* HFDL_PAYLOAD_DECODE_H */

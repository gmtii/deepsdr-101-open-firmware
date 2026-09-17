#include "hfdl_payload_decode.h"
#include "hfdl_descrambler.h"
#include "hfdl_deinterleaver.h"
#include "hfdl_viterbi.h"
#include "hfdl_soft_demod.h"
#include "hfdl_pdu.h"
#include "hfdl_crc.h" /* hfdl_crc16_x25() - used by this file's own selftest to build a realistic MPDU+FCS, see hfdl_pd_test_one_combo()'s "STRUCTURED MPDU HEADER OVERLAY" comment */
#include "hfdl_scope.h" /* hfdl_scope_get_hfdl_ram() */
#include "debug_uart.h"
#include <string.h>
#include <math.h>

/* Same bit-reversal trick hfdl_pdu.c's own (file-local) reverse_byte()
 * uses - see that file's comment. Duplicated here rather than
 * exposed/shared since it's a trivial 3-line helper and this module
 * has its own separate reason to need it (REVERSE_BYTE on the raw
 * Viterbi chainback output, see hfdl_pdu.h's module comment on why
 * that step is the CALLER's job, not hfdl_pdu.c's). */
static uint8_t hfdl_payload_reverse_byte(uint8_t x)
{
    x = (uint8_t)((x & 0xF0u) >> 4 | (x & 0x0Fu) << 4);
    x = (uint8_t)((x & 0xCCu) >> 2 | (x & 0x33u) << 2);
    x = (uint8_t)((x & 0xAAu) >> 1 | (x & 0x55u) << 1);
    return x;
}

/* Persistent state, carved from the START of the borrowed RAM region
 * - see hfdl_payload_decode.h's top comment for the full layout and
 * why this can't be a module-level static (0 bytes of RAM headroom).
 * Survives across an entire segment's worth of feed_symbol() calls,
 * living inside memory that's itself alive for the whole HFDL-mode
 * session (see hfdl_scope.c's s_hfdl_ram). */
typedef struct {
    hfdl_descrambler_t   descrambler;
    hfdl_deinterleaver_t deinterleaver;
    hfdl_viterbi_t        viterbi;
    const hfdl_rate_params_t *rate;     /* NULL = no segment active/begin_segment failed */
    uint8_t              *viterbi_metrics_a_bytes; /* byte offset into ram, cast at use */
    uint8_t              *viterbi_metrics_b_bytes;
    uint8_t              *viterbi_decisions_bytes;
    uint32_t              viterbi_decisions_capacity; /* in hfdl_viterbi_decision_t units */
    uint8_t               mod_arity;
    float32_t             quant_power_avg; /* per-symbol amplitude tracker feeding the
                                               PRE-QUANTIZATION normalization below - see
                                               hfdl_payload_decode_feed_symbol_ex()'s own
                                               comment on why this lives HERE, separate from
                                               hfdl_equalizer_t's own input_power_avg */
} hfdl_payload_decode_state_t;


/* NO module-level static state at all - not even a cached pointer
 * (see hfdl_payload_decode.h's own comment on the _ex functions for
 * why: a single 4-byte static pointer was enough to overflow this
 * project's 0-headroom RAM budget on this round's first build
 * attempt). Every function that needs the persistent state re-derives
 * its address from the `ram` pointer it's given (or, for the
 * convenience wrappers, from hfdl_scope_get_hfdl_ram() called fresh
 * each time) - a few pointer-arithmetic instructions' worth of CPU
 * cost per call, in exchange for zero new bytes of RAM outside the
 * borrowed region. */
static inline hfdl_payload_decode_state_t *hfdl_payload_state_at(uint8_t *ram)
{
    return (hfdl_payload_decode_state_t *)(void *)ram;
}

bool hfdl_payload_decode_begin_segment_ex(uint32_t m1_index, uint8_t *ram, uint32_t ram_capacity)
{
    if (ram == (void *)0) {
        return false;
    }

    /* CRITICAL FIX (19/08/2026, found from a real capture hang) -
     * invalidate any PRIOR segment's state FIRST, unconditionally
     * (as soon as we know `ram` itself is non-NULL and safe to
     * dereference), before ANY other check - including the m1_index
     * range check and the capacity check below, either of which might
     * make THIS call return false. The bug this fixes: if a previous
     * segment's begin_segment_ex() succeeded but its finish() was
     * never called (burst ended mid-segment - a normal, observed
     * real-world case, see this project's own capture logs) OR its
     * own begin_segment_ex() call failed for a DIFFERENT reason,
     * st->rate at the start of `ram` could still hold a valid-looking
     * pointer to that OLD segment's (smaller, or just different) rate
     * params. If THIS call then also fails a check below and returns
     * early "having written nothing" (as the old comment there
     * claimed), that stale st->rate would still read as non-NULL - so
     * feed_symbol_ex()/finish_ex() would treat the NEW, larger
     * segment's DATA symbols as belonging to the OLD, smaller-sized
     * deinterleaver table configured for a completely different rate
     * - writing past that table's actual allocated size on every
     * push() once the new segment's symbol count exceeds what the
     * old table was sized for. That's a real out-of-bounds write into
     * adjacent RAM, not a hypothetical - confirmed by a real capture
     * hanging immediately after exactly this sequence (a completed-
     * or-incomplete prior segment, then an M1=6 LOCKED that failed
     * this function's own capacity check). Clearing st->rate here,
     * before every other check, means EVERY `return false` below
     * leaves the state definitively inactive - no path can carry
     * stale sizing into a mismatched future segment. */
    hfdl_payload_decode_state_t *st = hfdl_payload_state_at(ram);
    st->rate = (void *)0;

    if (m1_index >= 8u) {
        return false;
    }
    const hfdl_rate_params_t *rate = &hfdl_rate_params[m1_index];

    uint32_t state_size = (uint32_t)sizeof(hfdl_payload_decode_state_t);
    uint32_t table_size = (uint32_t)hfdl_deinterleaver_column_cnt(rate) * HFDL_DEINTERLEAVER_ROW_CNT;

    uint32_t num_symbols = (uint32_t)rate->data_segment_cnt * 30u; /* HFDL_DATA_FRAME_LEN, see hfdl_deinterleaver.c */
    uint32_t num_encoded_bits = num_symbols * (uint32_t)rate->scheme_bits;
    uint32_t viterbi_input_len = (rate->code_rate_denom == 4u) ? (num_encoded_bits / 2u) : num_encoded_bits;
    uint32_t viterbi_output_len = viterbi_input_len / 2u; /* CONV_CODE_RATE=2, always - see hfdl_viterbi.h */
    uint32_t decisions_bytes = (uint32_t)HFDL_VITERBI_DECISION_BYTES(viterbi_output_len);

    uint32_t total_needed = state_size + table_size + 512u /* 2x metric_t */ + decisions_bytes;
    if (total_needed > ram_capacity) {
        /* M1=6/7 case - see hfdl_payload_decode.h's top comment. st->rate
         * is ALREADY NULL (set above, before this check) - clean
         * refusal, state left definitively inactive. */
        debug_print_dec_always("hfdl_payload_decode: segment too big for borrowed RAM, m1_index", m1_index);
        debug_print_dec_always("  total_needed", total_needed);
        debug_print_dec_always("  ram_capacity", ram_capacity);
        return false;
    }

    st->rate = rate;
    st->mod_arity = rate->scheme_bits;
    st->quant_power_avg = 1.0f; /* seed at 1.0, same neutral-start convention as
                                    hfdl_equalizer_init()'s own input_power_avg - see
                                    feed_symbol_ex()'s comment for why this needs its
                                    own tracker rather than reusing the equalizer's */

    uint8_t *table_buf = ram + state_size;
    hfdl_deinterleaver_configure(&st->deinterleaver, table_buf, rate);
    hfdl_deinterleaver_reset(&st->deinterleaver);

    hfdl_descrambler_init(&st->descrambler);

    st->viterbi_metrics_a_bytes = table_buf + table_size;
    st->viterbi_metrics_b_bytes = st->viterbi_metrics_a_bytes + sizeof(hfdl_viterbi_metric_t);
    st->viterbi_decisions_bytes = st->viterbi_metrics_b_bytes + sizeof(hfdl_viterbi_metric_t);
    st->viterbi_decisions_capacity = viterbi_output_len + 6u; /* matches HFDL_VITERBI_DECISION_BYTES()'s own +6 */

    /* Viterbi itself isn't init'd until finish() (that's when its
     * buffers actually start getting used - see that function). Only
     * the layout/pointers are fixed here. */
    return true;
}

bool hfdl_payload_decode_begin_segment(uint32_t m1_index)
{
    uint32_t cap = 0u;
    uint8_t *ram = hfdl_scope_get_hfdl_ram(&cap);
    if (ram == (void *)0) {
        debug_print_always("hfdl_payload_decode_begin_segment: no borrowed RAM (HFDL mode off?)\n");
        return false;
    }
    return hfdl_payload_decode_begin_segment_ex(m1_index, ram, cap);
}

void hfdl_payload_decode_feed_symbol_ex(uint8_t *ram, float32_t eq_i, float32_t eq_q, uint32_t polarity_bitmask)
{
    if (ram == (void *)0) {
        return;
    }
    hfdl_payload_decode_state_t *st = hfdl_payload_state_at(ram);
    if (st->rate == (void *)0) {
        return; /* no active segment - safe no-op, see header comment */
    }

    /* Order confirmed against dumphfdl's decode_user_data() (see
     * this module's header comment): descrambler advances FIRST, its
     * bit (plus the preamble's own polarity bitmask) flips the
     * symbol's phase BEFORE soft-demod, THEN each resulting bit gets
     * pushed into the deinterleaver in order. */
    uint32_t descrambler_bit = hfdl_descrambler_advance(&st->descrambler);
    float32_t sign = (descrambler_bit ^ (polarity_bitmask & 1u)) ? -1.0f : 1.0f;
    float32_t i = eq_i * sign;
    float32_t q = eq_q * sign;

    /* PRE-QUANTIZATION AMPLITUDE NORMALIZATION (25/08/2026 session,
     * "cuantificador no funciona" investigation) - hfdl_soft_demod.h's
     * own contract says this function receives eq_i/eq_q "same scale
     * hfdl_soft_demod.h expects" (see hfdl_payload_decode.h's
     * feed_symbol_ex() comment) - i.e. hfdl_soft_demod_bpsk()'s
     * gamma=4.0 and hfdl_soft_demod_psk8()'s gamma=1.2*M constants
     * (ported bit-exact from liquid-dsp, see hfdl_soft_demod.h) are
     * only correct LLR scaling when the input constellation actually
     * sits near unit radius, the same assumption liquid-dsp itself
     * makes of ITS caller.
     *
     * hfdl_equalizer.c's own history (see hfdl_equalizer_push()'s
     * "OVERRIDABLE (20/08/2026)" comment) directly measured, on real
     * captures, that eq_i/eq_q's OUTPUT magnitude scatters from 0.09
     * to 2.10 WITHIN THE SAME capture, despite tap energy sitting in a
     * healthy, stable band near 1.0 - i.e. the equalizer's own input
     * normalization (a slow, ~20-symbol EMA applied BEFORE the taps)
     * isn't enough to keep its OUTPUT at the scale this module's
     * contract promises downstream. A soft-demod call that's correct
     * for unit-radius input will silently misbehave in both
     * directions on that scattered input: at 0.09x, BPSK's
     * -128*i_in+127 barely moves off 127 (near-zero-confidence soft
     * bits even for a clean symbol, starving the Viterbi decoder of
     * real LLR magnitude); at 2.10x, it saturates hard at 0/255
     * (false full-confidence on a symbol that may carry ordinary
     * noise) - both look exactly like "the quantizer doesn't work"
     * from the Viterbi/CRC end, without any bug in hfdl_soft_demod.c's
     * own (self-test-verified, liquid-exact) math.
     *
     * FIX: track this module's OWN fast EMA of instantaneous power on
     * the (already sign-corrected) symbol actually about to be
     * quantized - deliberately separate from, and faster than,
     * hfdl_equalizer_t's own input_power_avg (lambda=0.05, ~20-symbol
     * window - see that file's comment flagging that window as
     * possibly too slow relative to real signal-power changes). This
     * normalizes at the LAST possible point before quantization, so
     * it corrects for scatter introduced anywhere upstream (input
     * normalization lag, tap-energy micro-fluctuations, residual
     * front-end AGC settling) rather than trying to fix any one of
     * those sources individually. lambda=0.2 chosen as a first,
     * reasoned-not-measured value (~5-symbol effective window, fast
     * enough to track within-segment scatter without becoming noisy
     * sample-to-sample) - same "reasonable first choice, not a
     * measured-optimal one" status hfdl_equalizer.c's own EMA lambda
     * had before ITS override knob got added; override with
     * `make HFDL_QUANT_POWER_EMA_LAMBDA=<value>` for an A/B sweep
     * against real captures. NOT yet confirmed end-to-end on real
     * hardware (does it actually move crc_ok off 0?) - same "flagged,
     * not assumed" discipline as every other fix in this project. */
#ifndef HFDL_QUANT_POWER_EMA_LAMBDA
#define HFDL_QUANT_POWER_EMA_LAMBDA 0.2f
#endif
    float32_t inst_power = i * i + q * q;
    st->quant_power_avg += (float32_t)HFDL_QUANT_POWER_EMA_LAMBDA * (inst_power - st->quant_power_avg);
    if (st->quant_power_avg < 1.0e-6f) {
        st->quant_power_avg = 1.0e-6f; /* same near-zero guard discipline as
                                           hfdl_equalizer_push()'s own input_power_avg */
    }
    float32_t quant_norm = 1.0f / sqrtf(st->quant_power_avg);
    float32_t i_q_norm = i * quant_norm;
    float32_t q_q_norm = q * quant_norm;

    uint8_t soft_bits[HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL];
    hfdl_soft_demod(st->mod_arity, i_q_norm, q_q_norm, soft_bits);
    for (uint32_t k = 0u; k < st->mod_arity; k++) {
        hfdl_deinterleaver_push(&st->deinterleaver, soft_bits[k]);
    }
}

void hfdl_payload_decode_feed_symbol(float32_t eq_i, float32_t eq_q, uint32_t polarity_bitmask)
{
    uint8_t *ram = hfdl_scope_get_hfdl_ram((void *)0);
    hfdl_payload_decode_feed_symbol_ex(ram, eq_i, eq_q, polarity_bitmask);
}

/* On-screen CRC status (21/08/2026) - Jorge's own request: a simple
 * running counter + last-result flag, so he can walk the receiver to
 * an RFI-quiet spot and see decode results directly on the device's
 * own screen, without a laptop/UART cable nearby (which may itself be
 * a noise source - see this session's impulsive-interference finding
 * on the real front-end capture). uint8_t, not uint16_t/uint32_t (RAM
 * is at zero headroom - see today's two linker overflows - and 255
 * decode attempts in one power-on session is still far beyond
 * realistic use before someone checks the screen or reboots). last_ok
 * and last_valid packed into one byte (bit 0 = valid, bit 1 = ok) to
 * save the other byte a separate uint8_t would cost. Read via
 * hfdl_payload_decode_get_crc_status() below. */
static uint8_t s_hfdl_crc_attempts = 0u;
static uint8_t s_hfdl_crc_good = 0u;
#define HFDL_CRC_STATUS_VALID_BIT 0x01u
#define HFDL_CRC_STATUS_OK_BIT    0x02u
static uint8_t s_hfdl_crc_status_bits = 0u; /* 0 until the first decode ever completes */

void hfdl_payload_decode_get_crc_status(uint8_t *last_ok_out, uint8_t *last_valid_out,
        uint8_t *attempts_out, uint8_t *good_out)
{
    if (last_ok_out) { *last_ok_out = (s_hfdl_crc_status_bits & HFDL_CRC_STATUS_OK_BIT) ? 1u : 0u; }
    if (last_valid_out) { *last_valid_out = (s_hfdl_crc_status_bits & HFDL_CRC_STATUS_VALID_BIT) ? 1u : 0u; }
    if (attempts_out) { *attempts_out = s_hfdl_crc_attempts; }
    if (good_out) { *good_out = s_hfdl_crc_good; }
}

void hfdl_payload_decode_finish_ex(uint8_t *ram, const uint8_t **decoded_out, uint32_t *decoded_len_out)
{
    if (decoded_out != (void *)0) { *decoded_out = (void *)0; }
    if (decoded_len_out != (void *)0) { *decoded_len_out = 0u; }
    if (ram == (void *)0) {
        return;
    }
    hfdl_payload_decode_state_t *st = hfdl_payload_state_at(ram);
    if (st->rate == (void *)0) {
        return;
    }
    const hfdl_rate_params_t *rate = st->rate;

    uint32_t num_symbols = (uint32_t)rate->data_segment_cnt * 30u;
    uint32_t num_encoded_bits = num_symbols * (uint32_t)rate->scheme_bits;
    uint32_t total_pop = num_encoded_bits; /* every pushed bit gets popped exactly once, see hfdl_deinterleaver.h */

    hfdl_viterbi_metric_t *ma = (hfdl_viterbi_metric_t *)(void *)st->viterbi_metrics_a_bytes;
    hfdl_viterbi_metric_t *mb = (hfdl_viterbi_metric_t *)(void *)st->viterbi_metrics_b_bytes;
    hfdl_viterbi_decision_t *dec = (hfdl_viterbi_decision_t *)(void *)st->viterbi_decisions_bytes;
    if (!hfdl_viterbi_init(&st->viterbi, ma, mb, dec, st->viterbi_decisions_capacity, 0u)) {
        debug_print_always("hfdl_payload_decode_finish: hfdl_viterbi_init failed\n");
        st->rate = (void *)0;
        return;
    }

    /* Pop everything, applying code-rate averaging inline (see
     * hfdl_viterbi.h's own note on why this belongs here, not inside
     * the Viterbi module) and feeding the Viterbi decoder ONE decoded
     * bit's worth (2 rate-1/2 soft symbols) at a time - deliberately
     * not materializing a separate full-size "viterbi_input" array
     * (would be another up-to-~15KB buffer with nowhere cheap to
     * live) - a few thousand tiny update_block() calls is negligible
     * CPU cost for something that only runs once per completed
     * segment, not a hot per-symbol path. */
    uint32_t viterbi_input_len = (rate->code_rate_denom == 4u) ? (total_pop / 2u) : total_pop;
    uint32_t viterbi_output_len_loop = viterbi_input_len / 2u; /* each loop iteration consumes 2 rate-1/2 symbols = exactly 1 decoded bit - was WRONGLY looping viterbi_input_len times (found via hfdl_payload_decode_selftest(), first real run: hfdl_viterbi_update_block() correctly refused once past capacity rather than overrunning memory - the capacity guard did its job, but the bug was still real) */
    uint8_t pair[2];
    bool ok = true;
    for (uint32_t i = 0u; i < viterbi_output_len_loop && ok; i++) {
        if (rate->code_rate_denom == 4u) {
            uint8_t a = hfdl_deinterleaver_pop(&st->deinterleaver);
            uint8_t b = hfdl_deinterleaver_pop(&st->deinterleaver);
            /* Average without overflow - same formula dumphfdl's own
             * decode_user_data() uses (hfdl.c line ~1032), see
             * hfdl_viterbi.h's CODE RATE HANDLING note. */
            pair[0] = (uint8_t)((a & b) + ((a ^ b) >> 1));
        } else {
            pair[0] = hfdl_deinterleaver_pop(&st->deinterleaver);
        }
        /* Every decoded bit needs 2 rate-1/2 soft symbols
         * (hfdl_viterbi_update_block()'s own convention) - the SECOND
         * one still comes from a fresh pop (code_rate==2 case) or a
         * fresh averaged pair (code_rate==4 case), never reused. */
        if (rate->code_rate_denom == 4u) {
            uint8_t a2 = hfdl_deinterleaver_pop(&st->deinterleaver);
            uint8_t b2 = hfdl_deinterleaver_pop(&st->deinterleaver);
            pair[1] = (uint8_t)((a2 & b2) + ((a2 ^ b2) >> 1));
        } else {
            pair[1] = hfdl_deinterleaver_pop(&st->deinterleaver);
        }
        ok = hfdl_viterbi_update_block(&st->viterbi, pair, 1u);
    }
    if (!ok) {
        debug_print_always("hfdl_payload_decode_finish: hfdl_viterbi_update_block refused (capacity) - shouldn't happen, begin_segment already checked\n");
        st->rate = (void *)0;
        return;
    }

    uint32_t viterbi_output_len = viterbi_input_len / 2u;
    uint32_t out_octets = viterbi_output_len / 8u + (viterbi_output_len % 8u != 0u ? 1u : 0u);

    /* Deinterleaver's table is fully drained by now (every cell
     * popped exactly once, per hfdl_deinterleaver.h's own contract) -
     * its memory is free to reuse for this much smaller output buffer.
     * Genuinely safe (not the M1=6/7 kind of risky reuse this module
     * deliberately avoided elsewhere) because there's no overlap in
     * TIME of use: the pop loop above has completely finished before
     * this line runs, and chainback() below only READS from
     * st->viterbi (the decisions buffer, a completely different
     * region) - it never touches the table's old memory except to
     * WRITE this output into it. */
    uint8_t *out_buf = ram + (uint32_t)sizeof(hfdl_payload_decode_state_t);
    hfdl_viterbi_chainback(&st->viterbi, out_buf, viterbi_output_len, 0u);

    for (uint32_t i = 0u; i < out_octets; i++) {
        out_buf[i] = hfdl_payload_reverse_byte(out_buf[i]);
    }

    if (decoded_out != (void *)0) { *decoded_out = out_buf; }
    if (decoded_len_out != (void *)0) { *decoded_len_out = out_octets; }

    debug_print_dec_always("hfdl_payload_decode: PDU bytes", out_octets);
    debug_print_dec_always("  Viterbi final metric at state 0 (lower = cleaner path)", hfdl_viterbi_get_final_metric(&st->viterbi, 0u));
    debug_print_dec_always("  Viterbi decoded bits (for the metric-per-bit ratio above)", viterbi_output_len);

    /* RAW BYTE HEX DUMP (19/08/2026) - added after several real
     * capture sessions all showed crc_ok=0 with no other visibility
     * into what was actually being decoded. Every check so far
     * (soft-demod formulas vs liquid-dsp, CRC vs a known-answer test
     * vector, MPDU structure vs dumphfdl's mpdu.c, a synthetic
     * end-to-end self-test including a real MPDU+FCS round trip) has
     * come back clean, and costas.freq (see demod_am.c's own LOCKED/
     * DONE prints) stays small and stable rather than diverging - but
     * none of that tells us whether the ACTUAL decoded bytes on a
     * real capture look "structured but shifted/scaled" (would point
     * to a smaller alignment/timing bug) or "pure noise" (would point
     * to something more fundamental, or a genuinely bad channel).
     * First 16 bytes (or fewer if the PDU is shorter), printed
     * unconditionally - not gated behind crc_ok, that's the whole
     * point. Uses debug_print_hex32_always 4 bytes at a time (no
     * dedicated byte-array hex dump helper exists in debug_uart.h) -
     * bounded to 16 bytes regardless of out_octets (up to 405 for
     * M1=3) so this stays a handful of lines, not a per-decode wall
     * of text. */
    {
        uint32_t dump_bytes = out_octets < 16u ? out_octets : 16u;
        for (uint32_t i = 0u; i < dump_bytes; i += 4u) {
            uint32_t word = 0u;
            uint32_t n = (dump_bytes - i < 4u) ? (dump_bytes - i) : 4u;
            for (uint32_t k = 0u; k < n; k++) {
                word |= (uint32_t)out_buf[i + k] << (8u * (3u - k));
            }
            debug_print_hex32_always("  decoded bytes", word);
        }
    }

    hfdl_mpdu_header_t hdr;
    if (!hfdl_mpdu_parse_header(out_buf, out_octets, &hdr)) {
        debug_print_always("  MPDU header: too short, not decoded\n");
        s_hfdl_crc_attempts++;
        s_hfdl_crc_status_bits = HFDL_CRC_STATUS_VALID_BIT; /* valid, not ok - bit 1 stays 0 */
        st->rate = (void *)0;
        return;
    }
    debug_print_dec_always("  MPDU crc_ok", hdr.crc_ok ? 1u : 0u);
    s_hfdl_crc_attempts++;
    s_hfdl_crc_status_bits = HFDL_CRC_STATUS_VALID_BIT | (hdr.crc_ok ? HFDL_CRC_STATUS_OK_BIT : 0u);
    if (hdr.crc_ok) { s_hfdl_crc_good++; }
    debug_print_dec_always("  MPDU direction (0=down,1=up)", (uint32_t)hdr.direction);

    if (hdr.crc_ok) {
        hfdl_lpdu_result_t lpdus[HFDL_MAX_LPDUS_TOTAL];
        uint32_t n = hfdl_mpdu_extract_lpdus(out_buf, out_octets, &hdr, lpdus, HFDL_MAX_LPDUS_TOTAL);
        debug_print_dec_always("  LPDU count", n);
        for (uint32_t i = 0u; i < n; i++) {
            debug_print_dec_always("    LPDU kind", (uint32_t)lpdus[i].kind);
            debug_print_dec_always("    LPDU crc_ok", lpdus[i].crc_ok ? 1u : 0u);
        }
    }

    st->rate = (void *)0; /* segment consumed - next begin_segment() starts fresh */
}

void hfdl_payload_decode_finish(void)
{
    uint8_t *ram = hfdl_scope_get_hfdl_ram((void *)0);
    hfdl_payload_decode_finish_ex(ram, (void *)0, (void *)0);
}

/* ---- self-test ---- */

/* HOST-ONLY (19/08/2026) - unlike hfdl_soft_demod_selftest()/
 * hfdl_viterbi_selftest() (small enough, ~64-200 bit test messages,
 * to run fine even on the embedded target), this test needs a FULL
 * real-sized data segment (thousands of bits) to actually exercise
 * the deinterleaver+Viterbi combination meaningfully - nowhere near
 * fitting this project's 0-byte RAM headroom. Uses malloc() freely
 * (fine on a host gcc build, never linked into the embedded firmware
 * - same "host_test_harness" pattern this project's DSP-chain modules
 * already use, just not wired into that specific harness_main.c since
 * this module has no CMSIS dependency to isolate). NOT called from
 * any embedded boot/debug self-test sequence - build and run
 * standalone with gcc, see this project's session notes for the
 * exact command used to validate this during development. */

#include <stdlib.h>

static uint32_t hfdl_pd_test_parity32(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return x & 1u;
}

/* Same K=7 r=1/2 reference encoder as hfdl_viterbi.c's own selftest -
 * duplicated (not shared) since that one's `static` and this test has
 * its own independent reason to need it; see that file's own comment
 * for the exact algorithm/convention this replicates. */
static void hfdl_pd_test_conv_encode(const uint8_t *bits_in, uint32_t n, uint8_t *soft_out)
{
    uint32_t reg = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        uint32_t reg7 = (reg << 1) | bits_in[i];
        uint32_t out0 = hfdl_pd_test_parity32(reg7 & HFDL_VITERBI_POLYA);
        uint32_t out1 = hfdl_pd_test_parity32(reg7 & HFDL_VITERBI_POLYB);
        soft_out[2u * i] = out0 ? 255u : 0u;
        soft_out[2u * i + 1u] = out1 ? 255u : 0u;
        reg = reg7 & 0x3Fu;
    }
}

static uint32_t hfdl_pd_test_gray_decode(uint32_t s)
{
    uint32_t mask = s, out = s;
    for (uint32_t i = 0u; i < 4u; i++) {
        out ^= (mask >> 1); out ^= (mask >> 2); out ^= (mask >> 3); out ^= (mask >> 4);
        mask >>= 4;
    }
    return out;
}

/* Inverse of hfdl_soft_demod's hard-decision mapping - given
 * `mod_arity` MSB-first CLEAN bits (no noise, this test is about
 * wiring correctness, not error correction - that's already covered
 * by hfdl_soft_demod_selftest()/hfdl_viterbi_selftest() individually),
 * produces the exact constellation point hfdl_soft_demod would map
 * BACK to those same bits with zero error. */
static void hfdl_pd_test_modulate(uint32_t mod_arity, const uint8_t *bits, float32_t *i_out, float32_t *q_out)
{
    if (mod_arity == 1u) {
        *i_out = bits[0] ? -1.0f : 1.0f;
        *q_out = 0.0f;
        return;
    }
    uint32_t sym = 0u;
    for (uint32_t k = 0u; k < mod_arity; k++) {
        sym = (sym << 1) | bits[k];
    }
    uint32_t M = 1u << mod_arity;
    float32_t alpha = 3.14159265358979f / (float32_t)M;
    float32_t phase = (float32_t)hfdl_pd_test_gray_decode(sym) * 2.0f * alpha;
    *i_out = cosf(phase);
    *q_out = sinf(phase);
}

/* Computes inv_perm[push_step] = pop_step such that the bit pushed at
 * push_step is the one popped at pop_step - built using the REAL
 * hfdl_deinterleaver_push()/pop() (not a duplicated/assumed cursor
 * algorithm), exploiting the bijection property
 * hfdl_deinterleaver_selftest() already independently validates for
 * real rate params. Two passes (low byte, high byte of the index)
 * since push/pop only carry one uint8_t per call and `total` can
 * exceed 256 for real HFDL segment sizes. `scratch` must be >= the
 * rate's table size (caller's responsibility, same as
 * hfdl_deinterleaver_configure()'s own contract). */
static void hfdl_pd_test_build_inv_perm(const hfdl_rate_params_t *rate, uint8_t *scratch, uint32_t total, uint32_t *inv_perm_out)
{
    hfdl_deinterleaver_t d;
    uint8_t *low = malloc(total);
    uint8_t *high = malloc(total);
    uint32_t *perm = malloc(total * sizeof(uint32_t));

    hfdl_deinterleaver_configure(&d, scratch, rate);
    hfdl_deinterleaver_reset(&d);
    for (uint32_t i = 0u; i < total; i++) { hfdl_deinterleaver_push(&d, (uint8_t)(i & 0xFFu)); }
    for (uint32_t i = 0u; i < total; i++) { low[i] = hfdl_deinterleaver_pop(&d); }

    hfdl_deinterleaver_reset(&d);
    for (uint32_t i = 0u; i < total; i++) { hfdl_deinterleaver_push(&d, (uint8_t)((i >> 8) & 0xFFu)); }
    for (uint32_t i = 0u; i < total; i++) { high[i] = hfdl_deinterleaver_pop(&d); }

    for (uint32_t i = 0u; i < total; i++) {
        perm[i] = ((uint32_t)high[i] << 8) | (uint32_t)low[i]; /* perm[pop_step] = push_step */
    }
    for (uint32_t i = 0u; i < total; i++) {
        inv_perm_out[perm[i]] = i; /* inv_perm[push_step] = pop_step */
    }

    free(low); free(high); free(perm);
}

/* Runs one full encode->feed->decode cycle for one rate combo and
 * checks the decoded bytes match exactly. Returns true on match. */
static bool hfdl_pd_test_one_combo(uint32_t m1_index)
{
    const hfdl_rate_params_t *rate = &hfdl_rate_params[m1_index];
    uint32_t num_symbols = (uint32_t)rate->data_segment_cnt * 30u;
    uint32_t num_encoded_bits = num_symbols * (uint32_t)rate->scheme_bits;
    uint32_t viterbi_input_len = (rate->code_rate_denom == 4u) ? (num_encoded_bits / 2u) : num_encoded_bits;
    uint32_t viterbi_output_len = viterbi_input_len / 2u;
    uint32_t msg_bits = viterbi_output_len; /* real HFDL combos are byte-aligned here (confirmed: 1080/8=135 exactly for M1=1) - use the WHOLE output length as the message, with its own last (K-1) bits forced to zero as the flush tail below. No separate "round up to a byte, then add tail on top" needed - that was this test's own earlier bug (added tail bits ON TOP of a byte-rounded message instead of carving them FROM the existing budget), not a production-code bug. */
    if (msg_bits <= (HFDL_VITERBI_K - 1u)) {
        return false; /* combo doesn't fit this test's assumptions - not expected for any real HFDL rate */
    }

    uint8_t *msg_bits_buf = malloc(msg_bits);
    uint32_t rng = 0xA5A5A5A5u ^ m1_index;
    for (uint32_t i = 0u; i < msg_bits - (HFDL_VITERBI_K - 1u); i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        msg_bits_buf[i] = (uint8_t)(rng & 1u);
    }
    for (uint32_t i = msg_bits - (HFDL_VITERBI_K - 1u); i < msg_bits; i++) {
        msg_bits_buf[i] = 0u; /* zero-flush tail, matching dumphfdl's terminated-code convention */
    }

    /* STRUCTURED MPDU HEADER OVERLAY (19/08/2026, added after a real
     * capture session ran 40+ decodes with 0 crc_ok successes despite
     * good MSE and the Costas fix - everything checked so far
     * (hfdl_crc.c's own known-answer test vector, hfdl_pdu.c's
     * structure hand-compared against dumphfdl's mpdu.c) looked
     * correct in isolation, but this module's own self-test had NEVER
     * actually exercised hfdl_crc_check()/hfdl_mpdu_parse_header() at
     * all - only compared raw decoded bytes, bypassing the one layer
     * most likely to hide an integration-only bug. Overwrites the
     * first 8 message bytes with a REALISTIC minimal downlink MPDU
     * (direction bit set, lpdu_cnt=0, arbitrary dst/src id, hdr_len=6)
     * plus its own correctly-computed FCS, so the full round trip
     * through hfdl_mpdu_parse_header() can be checked below, not just
     * the raw Viterbi output. Every combo has room for this (smallest
     * real payload is 68 bytes, M1=0) since msg_bits is always
     * data_segment_cnt-derived, comfortably >= 8 bytes for every M1
     * this project decodes. */
    {
        uint8_t hdr_bytes[8]; /* FINAL, post-REVERSE_BYTE intended values - what decoded[] should show, matching how a real MPDU header's direction/lpdu_cnt/dst_id/src_id bits are actually tested in hfdl_pdu.c (buf[0]&0x02 etc, same convention dumphfdl's mpdu.c uses on ITS OWN already-reversed viterbi_output, confirmed from src/hfdl.c:1052) */
        hdr_bytes[0] = 0x02u; /* direction bit (0x02) set = downlink, lpdu_cnt (bits 2-5) = 0 -> hdr_len = 6+0 = 6 */
        hdr_bytes[1] = 0x15u; /* dst_id (bit7 must be clear, hfdl_mpdu_parse_header masks with 0x7F anyway) */
        hdr_bytes[2] = 0xABu; /* src_id */
        hdr_bytes[3] = 0x00u; /* reserved/unused by this test's minimal header */
        hdr_bytes[4] = 0x00u;
        hdr_bytes[5] = 0x00u;
        uint16_t fcs = hfdl_crc16_x25(hdr_bytes, 6u) ^ 0xFFFFu; /* same formula hfdl_crc_append() uses - computed over the FINAL (post-reversal) bytes, since that's what hfdl_crc_check() will actually see and verify against in production */
        hdr_bytes[6] = (uint8_t)(fcs & 0xFFu);
        hdr_bytes[7] = (uint8_t)((fcs >> 8) & 0xFFu);
        /* PRE-reverse each byte before packing into msg_bits_buf - the
         * pipeline (this test's own conv-encode/symbol/decode path,
         * matching hfdl_payload_decode_finish_ex()'s own single
         * REVERSE_BYTE call in production) will reverse every byte
         * exactly once between here and `decoded[]`, so msg_bits_buf
         * needs to hold the INVERSE of what we want to see land in
         * decoded[] - same bug class this test already got right
         * for the raw-byte comparison below (which reverses its OWN
         * `expected` value before comparing) - missed here on the
         * first pass of adding this MPDU check, caught immediately
         * by the very check it was added to make possible. */
        for (uint32_t byte_i = 0u; byte_i < 8u; byte_i++) {
            uint8_t final_val = hdr_bytes[byte_i];
            uint8_t pre_reversal = hfdl_payload_reverse_byte(final_val);
            for (uint32_t bit_i = 0u; bit_i < 8u; bit_i++) {
                msg_bits_buf[byte_i * 8u + bit_i] = (uint8_t)((pre_reversal >> (7u - bit_i)) & 1u);
            }
        }
    }

    /* conv-encode -> rate-1/2 soft bits (clean, 0/255 only) */
    uint8_t *conv_soft = malloc(2u * msg_bits);
    hfdl_pd_test_conv_encode(msg_bits_buf, msg_bits, conv_soft);
    /* conv_soft, taken 2 bytes at a time, is exactly viterbi_input_len
     * bytes when code_rate==2. For code_rate==4, each pair needs
     * DUPLICATING (not averaging - that's the RECEIVER's job) to
     * simulate the "every chip transmitted twice" repetition - see
     * hfdl_viterbi.h's CODE RATE HANDLING note. */
    uint8_t *encoded_bits; /* one bit-value per BYTE here (0 or 1), NOT packed - matches what gets pushed into the deinterleaver */
    uint32_t encoded_bits_len = num_encoded_bits;
    encoded_bits = malloc(encoded_bits_len);
    if (rate->code_rate_denom == 4u) {
        /* conv_soft has 2*msg_bits entries (=viterbi_input_len =
         * num_encoded_bits/2 for this combo) - each duplicated once
         * to reach num_encoded_bits. */
        for (uint32_t i = 0u; i < 2u * msg_bits; i++) {
            uint8_t bit = conv_soft[i] ? 1u : 0u;
            encoded_bits[2u * i] = bit;
            encoded_bits[2u * i + 1u] = bit;
        }
    } else {
        for (uint32_t i = 0u; i < 2u * msg_bits; i++) {
            encoded_bits[i] = conv_soft[i] ? 1u : 0u;
        }
    }
    free(conv_soft);

    /* Build the deinterleaver permutation and reorder encoded_bits
     * (currently in POP/transmission order) into PUSH/symbol-arrival
     * order. */
    uint32_t table_size = (uint32_t)hfdl_deinterleaver_column_cnt(rate) * HFDL_DEINTERLEAVER_ROW_CNT;
    uint8_t *perm_scratch = malloc(table_size);
    uint32_t *inv_perm = malloc(encoded_bits_len * sizeof(uint32_t));
    hfdl_pd_test_build_inv_perm(rate, perm_scratch, encoded_bits_len, inv_perm);

    uint8_t *push_order_bits = malloc(encoded_bits_len);
    for (uint32_t push_step = 0u; push_step < encoded_bits_len; push_step++) {
        uint32_t pop_step = inv_perm[push_step];
        push_order_bits[push_step] = encoded_bits[pop_step];
    }
    free(encoded_bits);
    free(inv_perm);
    free(perm_scratch);

    /* Group push_order_bits into symbols (mod_arity bits each,
     * MSB-first - matching hfdl_payload_decode_feed_symbol_ex()'s own
     * push order), modulate each to a clean constellation point,
     * apply the SAME phase-flip the real feed function will UNDO
     * (descrambler bit XOR polarity bitmask) so it cancels back to
     * clean on the decode side. */
    uint32_t table_size2 = table_size; /* reuse for the real begin_segment_ex() RAM */
    uint32_t needed = (uint32_t)sizeof(hfdl_payload_decode_state_t) + table_size2 + 512u
            + (uint32_t)HFDL_VITERBI_DECISION_BYTES(viterbi_output_len);
    uint8_t *ram = malloc(needed);

    const uint32_t polarity_bitmask = 1u; /* arbitrary fixed choice for this test - exercises the non-zero path */
    if (!hfdl_payload_decode_begin_segment_ex(m1_index, ram, needed)) {
        free(ram); free(push_order_bits);
        return false;
    }

    hfdl_descrambler_t tx_descrambler;
    hfdl_descrambler_init(&tx_descrambler);

    for (uint32_t sym_idx = 0u; sym_idx < num_symbols; sym_idx++) {
        uint8_t bits[HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL];
        for (uint32_t k = 0u; k < rate->scheme_bits; k++) {
            bits[k] = push_order_bits[sym_idx * rate->scheme_bits + k];
        }
        float32_t i_clean, q_clean;
        hfdl_pd_test_modulate(rate->scheme_bits, bits, &i_clean, &q_clean);

        uint32_t descrambler_bit = hfdl_descrambler_advance(&tx_descrambler);
        float32_t sign = (descrambler_bit ^ (polarity_bitmask & 1u)) ? -1.0f : 1.0f;
        /* Apply the SAME sign here (not its inverse) - feed_symbol_ex()
         * will multiply by `sign` again, and sign*sign=1, so the clean
         * point survives the round trip exactly. */
        hfdl_payload_decode_feed_symbol_ex(ram, i_clean * sign, q_clean * sign, polarity_bitmask);
    }
    free(push_order_bits);

    const uint8_t *decoded = (void *)0;
    uint32_t decoded_len = 0u;
    hfdl_payload_decode_finish_ex(ram, &decoded, &decoded_len);

    bool match = (decoded != (void *)0) && (decoded_len == (msg_bits / 8u) + (msg_bits % 8u != 0u ? 1u : 0u));
    if (match) {
        /* Only compare the FULLY-covered bytes (floor(msg_bits/8)) -
         * a non-byte-aligned msg_bits (real for code_rate==4 combos,
         * e.g. M1=0: viterbi_output_len=540, not a multiple of 8 -
         * dumphfdl's own user_data_bits_cnt formula gives this exact
         * value, confirmed not a bug) means the LAST output byte's
         * low bits come from padding/shift-register leftover, not
         * real message content on either side of this round trip -
         * not meaningful to compare. */
        uint32_t full_bytes = msg_bits / 8u;
        for (uint32_t i = 0u; i < full_bytes && match; i++) {
            /* decoded is REVERSE_BYTE'd + bit-packed MSB-first per
             * hfdl_viterbi_chainback()'s own convention - pack
             * msg_bits_buf the same way (MSB-first) and reverse each
             * byte to compare on equal footing, since finish_ex()'s
             * output already had REVERSE_BYTE applied. */
            uint8_t expected = 0u;
            for (uint32_t b = 0u; b < 8u; b++) {
                expected = (uint8_t)((expected << 1) | msg_bits_buf[i * 8u + b]);
            }
            uint8_t expected_rev = hfdl_payload_reverse_byte(expected);
            if (decoded[i] != expected_rev) {
                match = false;
            }
        }
    }

    /* CRC/HEADER ROUND TRIP CHECK (19/08/2026) - see the "STRUCTURED
     * MPDU HEADER OVERLAY" comment above for why this exists: the
     * raw-byte match above already confirms Viterbi/deinterleaver/
     * descrambler/soft-demod wiring, but NEVER previously exercised
     * hfdl_crc_check()/hfdl_mpdu_parse_header() at all. `decoded` is
     * the exact same buffer production code hands to
     * hfdl_mpdu_parse_header() (post-REVERSE_BYTE, see
     * hfdl_payload_decode_finish_ex()) - if the raw bytes already
     * matched, this SHOULD trivially pass; if it doesn't, the bug is
     * specifically in hfdl_crc.c/hfdl_pdu.c's own bit-level
     * conventions, not in anything upstream of them. */
    if (match) {
        hfdl_mpdu_header_t hdr;
        if (!hfdl_mpdu_parse_header(decoded, decoded_len, &hdr)) {
            match = false;
        } else if (!hdr.crc_ok) {
            match = false;
        } else if (hdr.direction != HFDL_PDU_DIR_DOWNLINK || hdr.dst_id != 0x15u || hdr.src_id != 0xABu) {
            match = false; /* parsed, CRC ok, but wrong FIELDS - a different, more specific bug than a bad CRC */
        }
    }

    free(msg_bits_buf);
    free(ram);
    return match;
}

bool hfdl_payload_decode_selftest(void)
{
    /* M1=1 (BPSK, single-slot, rate 1/2) - simplest real combo, no
     * code-rate repetition to worry about, exercises the core wiring. */
    if (!hfdl_pd_test_one_combo(1u)) { return false; }

    /* M1=0 (BPSK, single-slot, rate 1/4) - same modulation, but
     * exercises the code_rate==4 repeat/average path specifically -
     * untested production logic otherwise (no real capture has hit
     * this branch's correctness, only its RAM sizing, this session). */
    if (!hfdl_pd_test_one_combo(0u)) { return false; }

    /* M1=3 (PSK8, single-slot, rate 1/2) - exercises multi-bit-per-
     * symbol soft demod and gray coding end to end, the combo this
     * project's own real captures have actually LOCKED on with a
     * payload-carrying rate. */
    if (!hfdl_pd_test_one_combo(3u)) { return false; }

    /* M1=2 (PSK4, single-slot, rate 1/2) - the hard-decision-only
     * soft demod path (see hfdl_soft_demod.h's own note on why PSK4
     * gets no real soft information) - worth confirming the WIRING
     * still works even though the soft values themselves are trivial. */
    if (!hfdl_pd_test_one_combo(2u)) { return false; }

    return true;
}

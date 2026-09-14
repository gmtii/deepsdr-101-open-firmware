#ifndef FT8_DECODER_H
#define FT8_DECODER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Orchestrates a real decode pass over a captured slot: calls
 * ft8_lib's ftx_find_candidates()/ftx_decode_candidate()/
 * ftx_message_decode() directly (the pieces the host WAV test
 * (test/host/ft8_host_test.c) already exercised against real off-air
 * recordings), and queues the results as ready-to-display text lines.
 *
 * Deliberately NOT slot-timing aware (same scope limit as
 * ft8_waterfall_adapter.h/ft8_decimator.h) - call
 * ft8_decoder_process_slot() whenever ft8_waterfall_is_full() says a
 * capture is ready; arming/rearming the next capture is the caller's
 * job (see main.c's minimal bring-up hook).
 */

#define FT8_DECODER_MAX_CANDIDATES 140 /* matches ft8_lib's own demo default (kMax_candidates) */
#define FT8_DECODER_MIN_SCORE      10  /* matches ft8_lib's own demo default (kMin_score) */
#define FT8_DECODER_LDPC_ITERATIONS 25 /* matches ft8_lib's own demo default (kLDPC_iterations) */
#define FT8_DECODER_MSG_QUEUE_SIZE 20  /* generous for our 400Hz search window - a busy full-band capture might see more, ours won't */
#define FT8_DECODER_LINE_LEN       56  /* "HH:MM " (6) + "+168.8Hz -16dB " (15) + message (up to 35) + NUL, rounded up */

/* One decoded message, already formatted as a display-ready line
 * ("freq snr ~ message") - see ft8_decoder_process_slot()'s comment
 * for the exact format. */
typedef struct
{
    char line[FT8_DECODER_LINE_LEN];
} ft8_decoded_msg_t;

/* Call once, at startup (alongside ft8_decimator_reset()/
 * ft8_waterfall_reset()) - clears the message queue. */
void ft8_decoder_init(void);

/* Runs ftx_find_candidates() + ftx_decode_candidate() +
 * ftx_message_decode() over whatever ft8_waterfall_get() currently
 * holds, and pushes every successfully decoded, non-duplicate message
 * into the output queue. Call this ONCE right after
 * ft8_waterfall_is_full() becomes true (NOT from an ISR - this walks
 * up to FT8_DECODER_MAX_CANDIDATES candidates, each running a
 * bp_decode() LDPC pass costing ~4.4KB of stack momentarily - see this
 * session's stack_watermark.h measurement for why that's fine from
 * the main loop but would NOT be fine from inside an ISR). Duplicate
 * suppression is a simple exact-match-within-this-slot check, not
 * ft8_lib's own hash table (this deliberately skips the
 * ftx_callsign_hash_interface_t plumbing for now - compound/hashed
 * callsigns may decode less completely than a full WSJT-X-style
 * multi-pass would give you, but ordinary messages - the large
 * majority in practice - decode in full without it, same tradeoff
 * already accepted in test/host/ft8_host_test.c's own hash stub). */
void ft8_decoder_process_slot(void);

/* Pulls one queued decoded-message line. Returns true and fills *out
 * if one was pending, false (leaves *out untouched) if the queue is
 * empty. Call from the main loop to drain into the UI - same
 * "background sets state, main loop drains/acts on it" split as every
 * other cross-context field in this codebase (e.g. rtty_get_char()). */
bool ft8_decoder_get_message(ft8_decoded_msg_t *out);

/* Running total of real decoded messages since ft8_decoder_init()
 * (i.e. since boot) - counts every successful, non-duplicate decode
 * even if the display queue was full and that particular message got
 * dropped before ever reaching ft8_decoder_get_message(). Added
 * (09/2026) per the project owner's request: a persistent counter,
 * shown continuously in the UI, to help tell at a glance how
 * productive a given band/time/tuning is while experimenting, since
 * the scrolling text panel alone doesn't make totals easy to track. */
uint32_t ft8_decoder_get_total_count(void);

/* Candidate count from the last ftx_find_candidates() call - see its
 * own declaration comment in ft8_decoder.c for why. */
int ft8_decoder_get_last_num_candidates(void);

#endif /* FT8_DECODER_H */

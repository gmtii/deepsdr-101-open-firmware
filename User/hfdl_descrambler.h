#ifndef HFDL_DESCRAMBLER_H
#define HFDL_DESCRAMBLER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HFDL data-symbol descrambler - step 3 of Phase 2's staged plan (see
 * hfdl_crc.h/hfdl_pdu.h for steps 1-2 and the project's
 * HFDL_fase2_arquitectura document for the full plan). Zero
 * dependency on the rest of the pipeline, same as those two modules -
 * this is a pure bit generator, built/tested standalone first.
 *
 * WHAT THIS ACTUALLY IS: a 15-bit Fibonacci LFSR (maximal-length
 * sequence generator), whose output bit each data symbol flips that
 * symbol's phase by pi (see hfdl_pdu.h's earlier note: HFDL is
 * coherent PSK with phase ambiguity resolved by preamble correlation,
 * NOT differential-per-symbol encoding - this LFSR is the piece that
 * actually earns HFDL's "scrambled"/pseudo-random look on the air).
 *
 * dumphfdl (szpajder/dumphfdl, GPL-3.0-or-later) delegates this to
 * liquid-dsp's generic msequence_*() API (src/hfdl.c's
 * hfdl_descrambler_create()/descrambler_advance()) rather than
 * implementing the LFSR itself - so the exact bit-level algorithm
 * isn't in dumphfdl's own source, it's in liquid-dsp
 * (jgaeddert/liquid-dsp, MIT license), specifically
 * src/sequence/src/msequence.c's msequence_advance() combined with
 * src/utility/src/byte_utilities.c's liquid_bdotprod(). Both were
 * read directly to confirm the bit-level algorithm below - this is
 * not a guess or a reconstruction from liquid-dsp's public API docs.
 *
 * Confirmed parameters (dumphfdl's hfdl_descrambler_create(), the
 * "liquid-dsp >= 1.6.0" branch - that's the version in current use,
 * the older-version branch with genpoly=0x8002/init=0x6959 exists
 * only for backwards compatibility with liquid-dsp < 1.6.0 and is
 * NOT what modern dumphfdl actually runs):
 *   - Shift register width (m):    15 bits
 *   - Generator polynomial (g):    0x4001 (bits 0 and 14 set - i.e.
 *                                   feedback = state_bit0 XOR state_bit14)
 *   - Initial state (a):           0x4d4b
 *   - Reset period (seq_len):      120 symbols - the LFSR state resets
 *                                   back to 0x4d4b every 120 data
 *                                   symbols (dumphfdl's descrambler_advance())
 *
 * Algorithm (liquid-dsp's msequence_advance(), read literally):
 *   b            = parity(state & g)     // XOR of state's bits 0 and 14 only, since g=0x4001
 *   state        = ((state << 1) | b) & 0x7FFF   // 0x7FFF = (1<<15)-1, the 15-bit mask
 *   output bit   = b
 * This is applied BEFORE the state register updates for next time -
 * i.e. b is computed from the CURRENT state, then folded back in.
 *
 * Verified with an independent Python re-implementation: the output
 * bit sequence exactly repeats every 120 symbols (as the seq_len
 * reset should produce), and the first several output bits were
 * cross-checked bit-by-bit against that Python run - see
 * hfdl_descrambler_selftest() in the .c file for the exact expected
 * values used there.
 */

typedef struct {
	uint32_t state;  /* current 15-bit LFSR state */
	uint32_t pos;     /* symbols advanced since the last periodic reset */
} hfdl_descrambler_t;

/* Resets `d` to the LFSR's initial state (0x4d4b) and position 0 -
 * call once before the first hfdl_descrambler_advance() of a new
 * frame (dumphfdl re-creates its descrambler per HFDL channel/frame,
 * this is the equivalent reset point). */
void hfdl_descrambler_init(hfdl_descrambler_t *d);

/* Advances the LFSR by one data symbol and returns its output bit (0
 * or 1). Per HFDL's descrambler_advance() behaviour, automatically
 * resets back to the initial state every 120 calls (i.e. every 120
 * data symbols) - the caller does NOT need to track that separately,
 * just call this once per data symbol and multiply that symbol's
 * phase by -1 when the returned bit is 1 (phase unchanged when 0). */
uint32_t hfdl_descrambler_advance(hfdl_descrambler_t *d);

/* Runs the module's standalone self-test against the Python
 * cross-check mentioned above. Returns true if the implementation's
 * bit sequence matches exactly, including the periodic reset at
 * symbol 120. Zero dependency on the rest of Phase 2. */
bool hfdl_descrambler_selftest(void);

#endif /* HFDL_DESCRAMBLER_H */

#include "hfdl_descrambler.h"

/* See hfdl_descrambler.h for the full derivation. Constants confirmed
 * directly against liquid-dsp's src/sequence/src/msequence.c +
 * src/utility/src/byte_utilities.c (jgaeddert/liquid-dsp, MIT), as
 * used by dumphfdl's hfdl_descrambler_create() (szpajder/dumphfdl,
 * GPL-3.0-or-later, "liquid-dsp >= 1.6.0" branch). */
#define HFDL_DESCRAMBLER_GENPOLY 0x4001u
#define HFDL_DESCRAMBLER_INIT    0x4d4bu
#define HFDL_DESCRAMBLER_MASK    0x7FFFu /* (1 << 15) - 1 */
#define HFDL_DESCRAMBLER_RESET_PERIOD 120u

/* XOR of every set bit in x - this is liquid_bdotprod(x, y) with the
 * AND already folded into the caller (liquid_bdotprod(state, g) ==
 * parity(state & g)). A lookup-free bit-count loop is fine here: this
 * runs once per DATA symbol only (max a few hundred/sec at 1800 baud
 * worst case), nowhere near the per-audio-sample hot path. */
static uint32_t parity_u32(uint32_t x)
{
	uint32_t p = 0u;
	while (x) {
		p ^= (x & 1u);
		x >>= 1;
	}
	return p;
}

void hfdl_descrambler_init(hfdl_descrambler_t *d)
{
	d->state = HFDL_DESCRAMBLER_INIT;
	d->pos = 0u;
}

uint32_t hfdl_descrambler_advance(hfdl_descrambler_t *d)
{
	if (d->pos == HFDL_DESCRAMBLER_RESET_PERIOD) {
		d->state = HFDL_DESCRAMBLER_INIT;
		d->pos = 0u;
	}
	d->pos++;

	uint32_t b = parity_u32(d->state & HFDL_DESCRAMBLER_GENPOLY);
	d->state = ((d->state << 1) | b) & HFDL_DESCRAMBLER_MASK;
	return b;
}

bool hfdl_descrambler_selftest(void)
{
	/* First 25 output bits from state=0x4d4b, cross-checked against
	 * an independent Python re-implementation of the same
	 * liquid-dsp algorithm (parity(state & 0x4001), shift, mask
	 * 0x7FFF) - see hfdl_descrambler.h for the derivation. */
	static const uint8_t expected_first_25[25] = {
		0,0,0,1,0,0,1,1,0,0,0,1,1,0,1,1,1,1,0,0,0,1,0,0,0
	};

	hfdl_descrambler_t d;
	hfdl_descrambler_init(&d);

	for (uint32_t i = 0; i < 25u; i++) {
		if (hfdl_descrambler_advance(&d) != expected_first_25[i]) {
			return false;
		}
	}

	/* Drive it up to symbol 120 (already consumed 25, need 95 more)
	 * and confirm the periodic reset makes bit[120] == bit[0] and
	 * bit[121] == bit[1], i.e. the sequence exactly restarts. */
	for (uint32_t i = 25; i < 120u; i++) {
		hfdl_descrambler_advance(&d);
	}
	if (hfdl_descrambler_advance(&d) != expected_first_25[0]) { /* symbol 120 */
		return false;
	}
	if (hfdl_descrambler_advance(&d) != expected_first_25[1]) { /* symbol 121 */
		return false;
	}

	/* A second, independently-initialized descrambler must reproduce
	 * the exact same sequence from scratch - guards against any
	 * hidden global/static state leaking between uses. */
	hfdl_descrambler_t d2;
	hfdl_descrambler_init(&d2);
	for (uint32_t i = 0; i < 25u; i++) {
		if (hfdl_descrambler_advance(&d2) != expected_first_25[i]) {
			return false;
		}
	}

	return true;
}

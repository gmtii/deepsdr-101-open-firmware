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
	/*
	 * LOS 120 BITS DE LA NORMA, LITERALES - 14/10/2026.
	 *
	 * Pedido por el usuario.
	 *
	 * Aqui habia 25 bits "cross-checked against an independent Python
	 * re-implementation of the same liquid-dsp algorithm". O sea: se
	 * comprobaba el algoritmo contra el mismo algoritmo escrito otra vez.
	 * Si yo hubiera entendido mal el polinomio o el estado inicial, las
	 * dos versiones se habrian equivocado igual y la autoprueba habria
	 * salido verde. No era una prueba, era un eco.
	 *
	 * La norma da el patron ENTERO escrito en hexadecimal, que es una
	 * verdad que no depende ni de liquid-dsp ni de dumphfdl ni de mi:
	 *
	 *     131BC4250F8C15EFCD6AEC996E2368
	 *
	 *     "where the most significant bit of the left-most hexadecimal
	 *      number is the first bit of the scrambling pattern"
	 *
	 * Comprobado el 14/10/2026: el LFSR de este fichero (polinomio
	 * 0x4001, estado inicial 0x4d4b) reproduce los 120 bits EXACTOS. Asi
	 * que ahora se comprueba contra eso, los 120, y no contra 25 de una
	 * reimplementacion.
	 *
	 * La norma ademas explica de donde sale -"15 stage shift register
	 * with coefficient generator polynomial 1 + X + X15 ... initial
	 * state 6959 hexadecimal"-, que es la misma secuencia expresada con
	 * el registro al reves; por eso 0x6959 y 0x4d4b son los dos
	 * correctos segun como se numeren los bits, y por eso no hay que
	 * tocar nada: lo que manda es la tira de 120, y sale.
	 */
	static const uint8_t PATRON_NORMA[30] = {
		0x13u, 0x1Bu, 0xC4u, 0x25u, 0x0Fu, 0x8Cu, 0x15u, 0xEFu, 0xCDu, 0x6Au,
		0xECu, 0x99u, 0x6Eu, 0x23u, 0x68u, 0x13u, 0x1Bu, 0xC4u, 0x25u, 0x0Fu,
		0x8Cu, 0x15u, 0xEFu, 0xCDu, 0x6Au, 0xECu, 0x99u, 0x6Eu, 0x23u, 0x68u
	}; /* los 15 bytes = 120 bits, repetidos para poder cruzar el reinicio */

	#define BIT_NORMA(i) ((PATRON_NORMA[(i) >> 3] >> (7u - ((i) & 7u))) & 1u)

	static const uint8_t expected_first_25[25] = {
		0,0,0,1,0,0,1,1,0,0,0,1,1,0,1,1,1,1,0,0,0,1,0,0,0
	}; /* los 25 primeros, que son los mismos - se dejan por legibilidad */

	hfdl_descrambler_t d;
	hfdl_descrambler_init(&d);

	for (uint32_t i = 0; i < 120u; i++) {
		if (hfdl_descrambler_advance(&d) != BIT_NORMA(i)) {
			return false;
		}
	}

	/* Y el reinicio periodico: el bit 120 tiene que ser otra vez el 0.
	 * Se comprueba contra la norma tambien, no contra lo que salio. */
	for (uint32_t i = 120u; i < 240u; i++) {
		if (hfdl_descrambler_advance(&d) != BIT_NORMA(i - 120u)) {
			return false;
		}
	}

	hfdl_descrambler_init(&d);
	for (uint32_t i = 0; i < 25u; i++) {
		if (hfdl_descrambler_advance(&d) != expected_first_25[i]) {
			return false;
		}
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

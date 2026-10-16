/* Ver armonico.h para el porque. Aqui solo esta el como. */
#include "armonico.h"

uint8_t arm_busca(uint32_t lo_hz, uint32_t span_hz,
                  arm_rango_fn rangos, uint16_t n_rangos,
                  arm_hit_t *out, uint8_t max)
{
    uint8_t n, puestos = 0U;
    uint32_t medio;

    if (lo_hz == 0UL || rangos == 0 || out == 0 || max == 0U) { return 0U; }
    medio = span_hz / 2UL;

    for (n = ARM_N_MIN; n <= ARM_N_MAX; n++) {
        /*
         * n*LO en 32 bits. Con el LO en 180 MHz y n=7 son 1.260 MHz, que
         * cabe de sobra; aun asi se comprueba, porque el dia que alguien
         * suba ARM_N_MAX o el margen de sintonia esto se desbordaria en
         * silencio y empezaria a enseñar bandas al azar.
         */
        uint32_t f;
        uint16_t i;

        if (lo_hz > (0xFFFFFFFFUL / (uint32_t)n)) { break; }
        f = lo_hz * (uint32_t)n;

        for (i = 0U; i < n_rangos; i++) {
            uint32_t lo = 0UL, hi = 0UL;
            const char *nom = 0;
            uint32_t a, b;

            if (!rangos(i, &lo, &hi, &nom)) { continue; }
            if (nom == 0 || lo == 0UL) { continue; }
            if (hi < lo) { continue; }

            /*
             * Se solapan el rango [lo,hi] y el tramo [f-medio, f+medio]?
             * Con restas y no con sumas: f+medio se puede desbordar arriba
             * del todo, y f-medio por abajo cuando f es pequeño.
             */
            a = (f > medio) ? (f - medio) : 0UL;
            b = (medio > (0xFFFFFFFFUL - f)) ? 0xFFFFFFFFUL : (f + medio);
            if (hi < a || lo > b) { continue; }

            if (puestos < max) {
                uint32_t centro = lo + ((hi - lo) / 2UL);
                out[puestos].n      = n;
                out[puestos].f_hz   = f;
                out[puestos].nombre = nom;
                out[puestos].off_hz = (centro >= f)
                                      ? (int32_t)(centro - f)
                                      : -(int32_t)(f - centro);
                /*
                 * El desvio se mide al trozo del rango que de verdad CAE
                 * dentro, no al centro del rango entero: una banda de
                 * radiodifusion de 300 kHz tiene su centro fuera del tramo
                 * de 96, y decir "a -180 kHz" de algo que se esta oyendo es
                 * mentira. Se recorta primero.
                 */
                {
                    uint32_t cl = (lo > a) ? lo : a;
                    uint32_t ch = (hi < b) ? hi : b;
                    uint32_t c  = cl + ((ch - cl) / 2UL);
                    out[puestos].off_hz = (c >= f) ? (int32_t)(c - f)
                                                   : -(int32_t)(f - c);
                }
            }
            puestos++;
            if (puestos >= 250U) { return puestos; }  /* no desbordar el contador */
        }
    }
    return puestos;
}

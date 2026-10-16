/*
 * CUANTO GIRO AGUANTA EL DECODIFICADOR - 14/10/2026.
 *
 * Pedido por el usuario.
 *
 * La autoprueba que ya existia demuestra que el decodificador va bien a
 * las CUATRO velocidades -M1 0, 1, 2 y 3, o sea BPSK, 4-PSK y 8-PSK- con
 * simbolos limpios. Asi que el fallo esta ANTES, en lo que le llega.
 *
 * Esto mide una cosa concreta: cuanto giro de constelacion aguanta. Si
 * medio paso ya lo tumba, entonces el giro absoluto -que hasta la V4.84 no
 * se resolvia mas alla de +-180- explica por si solo el cero de 25, y no
 * hace falta buscar mas lejos. Y si lo aguanta, el culpable es otro y esto
 * me ahorra el viaje.
 */
#include "hfdl_payload_decode.h"
#include "hfdl_costas.h"
#include <stdio.h>
#include <math.h>

static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-46s %s  %s\n", que, cond ? "ok " : "MAL", como ? como : "");
    if (!cond) { fallos++; }
}

int main(void)
{
    static const struct { uint32_t m1; const char *nom; uint32_t m; } k[] = {
        { 1u, "BPSK", 2u }, { 2u, "4PSK", 4u }, { 3u, "8PSK", 8u },
    };
    uint32_t c;

    printf("\nCUANTO GIRO AGUANTA EL DECODIFICADOR\n");
    printf("====================================\n\n");

    printf("  1. sin girar, las tres velocidades decodifican\n");
    for (c = 0u; c < 3u; c++) {
        di(hfdl_payload_decode_selftest_fase(k[c].m1, 0.0f) ? 1 : 0, k[c].nom, "");
    }

    printf("\n  2. girado UN PASO de constelacion, ninguna\n");
    for (c = 0u; c < 3u; c++) {
        float paso = 6.28318530718f / (float)k[c].m;
        char d[64];
        snprintf(d, sizeof d, "un paso son %.1f grados", paso * 57.29578f);
        di(hfdl_payload_decode_selftest_fase(k[c].m1, paso) ? 0 : 1, k[c].nom, d);
    }

    printf("\n  3. y deshaciendo el giro con hfdl_costas_giro_absoluto()\n");
    for (c = 1u; c < 3u; c++) {   /* BPSK no pasa por ahi, ver la funcion */
        uint32_t bits = (k[c].m == 4u) ? 2u : 3u;
        float paso = 6.28318530718f / (float)k[c].m;
        float gc, gs, resto;
        char d[96];
        /* Una correlacion girada ese paso, como la que mediria la radio */
        hfdl_costas_giro_absoluto(135.0f * cosf(paso), 135.0f * sinf(paso),
                                  (uint8_t)bits, &gc, &gs);
        /* Lo que queda despues de deshacerlo: el angulo de (gc,gs) sumado
         * al giro de verdad tiene que dar cero. */
        resto = paso + atan2f(gs, gc);
        while (resto >  3.14159265f) { resto -= 6.28318530718f; }
        while (resto < -3.14159265f) { resto += 6.28318530718f; }
        snprintf(d, sizeof d, "queda %.4f rad", resto);
        di(resto < 0.001f && resto > -0.001f, k[c].nom, d);
        /* Y el decodificador, con el giro ya deshecho, vuelve a decodificar */
        di(hfdl_payload_decode_selftest_fase(k[c].m1, paso + atan2f(gs, gc)) ? 1 : 0,
           "   y con el giro deshecho, decodifica", "");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

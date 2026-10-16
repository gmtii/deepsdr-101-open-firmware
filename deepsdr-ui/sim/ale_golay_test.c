/*
 * EL GOLAY DE ALE, PROBADO ENTERO Y NO POR MUESTREO.
 *
 * Y "entero" aqui quiere decir DEMOSTRADO, no "muchas veces". El codigo es
 * LINEAL, y en un codigo lineal decodificar la palabra recibida r = c + e
 * depende solo del error e, no de la palabra c que se mando. O sea que
 * probar los 2.325 patrones de error de peso <= 3 sobre la palabra CERO
 * cubre los 4.096 * 2.325 casos posibles. No es una muestra: es todos.
 *
 * Eso es lo que hace este banco en un parpadeo y lo que una prueba
 * aleatoria no da por mucho que corra.
 *
 * Y de paso mide lo que pasa con CUATRO errores, que es lo interesante: la
 * distancia minima del Golay extendido es 8, asi que cuatro errores caen
 * justo a mitad de camino entre dos palabras y el decodificador NO puede
 * arreglarlos. Lo que si puede es DARSE CUENTA - y cuantas veces se da
 * cuenta en vez de inventarse una palabra es un numero que conviene tener
 * escrito, porque es el que dice cuanta basura se cuela en la pantalla
 * cuando la propagacion esta mala.
 */
#include "ale_golay.h"
#include <stdio.h>

static int fallos;

static void bien(int ok, const char *que)
{
    printf("    %-54s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

int main(void)
{
    uint32_t c0 = ale_golay_codifica(0U);
    unsigned i, j, k, n = 0U, mal = 0U, mal_peso = 0U;

    printf("\nEL GOLAY (24,12) DE ALE\n\n");

    bien(c0 == 0U, "la palabra cero se codifica a cero");
    bien(ale_golay_codifica(0x800U) == 0x800AE3UL,
         "y el bit 11 da el propio generador (0xAE3)");

    /* --- todos los errores de peso 0, 1, 2 y 3, sobre 24 bits --- */
    for (i = 0U; i <= 24U; i++) {
        for (j = i; j <= 24U; j++) {
            for (k = j; k <= 24U; k++) {
                uint32_t e = 0U;
                uint8_t  err = 0U, peso = 0U;
                uint16_t d;

                if (i < 24U) { e ^= 1UL << i; }
                if ((j < 24U) && (j != i)) { e ^= 1UL << j; }
                if ((k < 24U) && (k != j) && (k != i)) { e ^= 1UL << k; }
                peso = 0U;
                { uint32_t t = e; while (t) { t &= t - 1U; peso++; } }

                d = ale_golay_decodifica(c0 ^ e, &err);
                n++;
                if (d != 0U) { mal++; }
                if (err != peso) { mal_peso++; }
            }
        }
    }
    printf("    %u combinaciones de hasta tres errores probadas\n", n);
    bien(mal == 0U, "las corrige TODAS (demostrado, no muestreado)");
    bien(mal_peso == 0U, "y dice exactamente cuantas ha corregido");

    /* --- cuatro errores: no se pueden arreglar; que al menos se noten --- */
    {
        unsigned tot = 0U, visto = 0U, inventado = 0U;

        for (i = 0U; i < 24U; i++) {
            for (j = i + 1U; j < 24U; j++) {
                for (k = j + 1U; k < 24U; k++) {
                    unsigned l;

                    for (l = k + 1U; l < 24U; l++) {
                        uint32_t e = (1UL << i) | (1UL << j) |
                                     (1UL << k) | (1UL << l);
                        uint8_t  err = 0U;
                        uint16_t d = ale_golay_decodifica(c0 ^ e, &err);

                        tot++;
                        if (err == ALE_GOLAY_NO) { visto++; }
                        else if (d != 0U)        { inventado++; }
                    }
                }
            }
        }
        printf("\n    con CUATRO errores (%u casos, ninguno corregible):\n", tot);
        printf("      %u  se dan por imposibles  (%.1f%%)\n",
               visto, 100.0 * visto / tot);
        printf("      %u  se corrigen a una palabra equivocada  (%.1f%%)\n",
               inventado, 100.0 * inventado / tot);
        bien(visto + inventado == tot, "o lo detecta o se equivoca, no hay tercera");
        /*
         * El numero de arriba NO es un fallo: con d=8 y cuatro errores el
         * codigo esta en su limite teorico. Lo que importa es que ALE exige
         * que las DOS mitades decodifiquen y ademas pasa una votacion 2-de-3
         * antes, asi que una palabra inventada tiene que colarse por tres
         * sitios a la vez. Por eso el sincronismo de ale_rx.c pide ademas
         * <= 1 error corregido para enganchar.
         */
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

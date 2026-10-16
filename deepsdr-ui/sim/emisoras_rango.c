/*
 * LOS NOMBRES QUE ENTRAN EN EL ESPECTRO - 15/10/2026
 *
 * Pedido por el usuario.
 *
 * Ver User/emisoras.h. Este banco mide y comprueba emisoras_rango() sobre
 * el EMISORAS.BIN DE VERDAD -206 kB, 1.898 frecuencias-, no sobre uno
 * inventado: un fichero de juguete no tiene las ventanas apretadas de los
 * 7 MHz, que son las que deciden el diseño.
 *
 * LAS DOS COSAS QUE COMPRUEBA, y la segunda es la que importa:
 *
 *   1. Que lo que devuelve es lo MISMO que preguntando frecuencia a
 *      frecuencia con emisoras_busca(), que es la funcion que ya estaba
 *      probada. Si las dos discrepan, la nueva miente.
 *   2. Que cuesta mucho menos. Si un dia alguien la reescribe y vuelve a
 *      recorrer la tabla entera, aqui se ve.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "emisoras.h"

static int fallos;
static FILE *g_f;
static long  g_tam, g_lect, g_bytes;

static void di(int cond, const char *que, const char *como)
{
    printf("    %-54s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

static void lee(uint32_t a, uint8_t *b, uint32_t n)
{
    g_lect++; g_bytes += (long)n;
    memset(b, 0, n);
    if ((long)a >= g_tam) { return; }
    fseek(g_f, (long)a, SEEK_SET);
    if (fread(b, 1, n, g_f) != n) { /* el relleno de ceros ya esta puesto */ }
}

/* Lo mismo, a la antigua: una busqueda por cada kilohercio del rango. Es
 * la referencia contra la que se compara, no una propuesta. */
static int a_la_antigua(uint32_t lo, uint32_t hi, uint16_t min, uint8_t dia,
                        uint32_t *hz_out, int max)
{
    uint32_t hz; int n = 0;
    for (hz = lo; hz <= hi && n < max; hz += 1000UL) {
        emisoras_r_t r[1];
        if (emisoras_busca(hz, 400UL, min, dia, r, 1U) && r[0].ahora) {
            /* emisoras_busca() redondea a la mas cercana: puede repetir */
            uint32_t v = hz;  /* se compara por kHz, que es la rejilla */
            if (n == 0 || hz_out[n - 1] != v) { hz_out[n++] = v; }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "datos/EMISORAS.BIN";
    static const uint32_t k_centro[] = { 7473000UL, 9600000UL, 13525000UL,
                                         15200000UL, 6100000UL, 11800000UL };
    static const uint32_t k_span[]   = { 96000UL, 48000UL, 24000UL, 12000UL, 6000UL };
    emisoras_rango_r_t r[16];
    uint16_t hubo;
    unsigned si, ci;
    long peor_lect = 0, peor_viejo = 0;

    printf("\n== LOS NOMBRES QUE ENTRAN EN EL ESPECTRO ==\n\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) {
        printf("  (no esta %s: me salto el banco)\n", ruta);
        return 0;
    }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    if (!emisoras_abre(0UL, lee)) { printf("  no abre la base\n"); return 1; }

    printf("  1. LO QUE CUESTA, por el mismo bus SPI que la radio\n\n");
    printf("     %-8s %-12s %7s %9s %12s\n",
           "span", "centro", "ahora", "lecturas", "a la antigua");
    for (si = 0; si < sizeof k_span / sizeof k_span[0]; si++) {
        for (ci = 0; ci < 2U; ci++) {
            uint32_t lo = k_centro[ci] - k_span[si] / 2U;
            uint32_t hi = k_centro[ci] + k_span[si] / 2U;
            long l_nuevo, l_viejo;
            uint32_t v[64];
            uint8_t n;

            g_lect = 0; g_bytes = 0;
            n = emisoras_rango(lo, hi, 720U, EMISORAS_DIA_CUALQUIERA,
                               r, (uint8_t)(sizeof r / sizeof r[0]), &hubo);
            l_nuevo = g_lect;
            g_lect = 0;
            (void)a_la_antigua(lo, hi, 720U, EMISORAS_DIA_CUALQUIERA, v, 64);
            l_viejo = g_lect;

            if (l_nuevo > peor_lect)  { peor_lect = l_nuevo; }
            if (l_viejo > peor_viejo) { peor_viejo = l_viejo; }

            printf("     %3u kHz  %7.3f MHz %4u/%-2u %9ld %12ld\n",
                   (unsigned)(k_span[si] / 1000U), k_centro[ci] / 1e6,
                   (unsigned)n, (unsigned)hubo, l_nuevo, l_viejo);
        }
    }

    printf("\n  2. Y QUE DIGA LO MISMO QUE LA FUNCION QUE YA ESTABA PROBADA\n\n");
    {
        int malos = 0, miradas = 0;
        for (si = 0; si < sizeof k_span / sizeof k_span[0]; si++) {
            for (ci = 0; ci < sizeof k_centro / sizeof k_centro[0]; ci++) {
                uint32_t lo = k_centro[ci] - k_span[si] / 2U;
                uint32_t hi = k_centro[ci] + k_span[si] / 2U;
                uint32_t v[64];
                uint8_t n, i;
                int nv;

                n  = emisoras_rango(lo, hi, 720U, EMISORAS_DIA_CUALQUIERA,
                                    r, (uint8_t)(sizeof r / sizeof r[0]), &hubo);
                nv = a_la_antigua(lo, hi, 720U, EMISORAS_DIA_CUALQUIERA, v, 64);
                miradas++;
                /* La de siempre redondea a kHz y puede juntar dos que el
                 * rango separa, asi que NO se exige el mismo recuento: se
                 * exige que cada una que devuelve el rango este tambien
                 * ahi, con 500 Hz de holgura. Lo que se comprueba es que
                 * no se INVENTA ninguna. */
                for (i = 0; i < n; i++) {
                    int j, visto = 0;
                    for (j = 0; j < nv; j++) {
                        uint32_t d = (v[j] > r[i].hz) ? (v[j] - r[i].hz)
                                                      : (r[i].hz - v[j]);
                        if (d <= 500UL) { visto = 1; break; }
                    }
                    if (!visto) {
                        printf("        se inventa %lu Hz en %.3f MHz +-%u kHz\n",
                               (unsigned long)r[i].hz, k_centro[ci] / 1e6,
                               (unsigned)(k_span[si] / 2000U));
                        malos++;
                    }
                }
            }
        }
        printf("     %d ventanas comparadas\n", miradas);
        di(malos == 0, "ninguna emisora inventada", "");
    }

    printf("\n  3. LAS PUERTAS\n");
    {
        uint8_t n;
        n = emisoras_rango(9600000UL, 9500000UL, 720U, EMISORAS_DIA_CUALQUIERA,
                           r, 16U, &hubo);
        di(n == 0U, "un rango del reves no devuelve nada", "");
        n = emisoras_rango(9500000UL, 9600000UL, 720U, EMISORAS_DIA_CUALQUIERA,
                           r, 0U, &hubo);
        di(n == 0U, "con sitio para cero, cero", "");
        n = emisoras_rango(1UL, 2UL, 720U, EMISORAS_DIA_CUALQUIERA, r, 16U, &hubo);
        di(n == 0U && hubo == 0U, "un rango donde no hay nada, vacio", "");
        /* Y el que de verdad importa: que `hubo` siga contando cuando no
         * caben. Es el numero que dice si la pantalla esconde emisoras. */
        n = emisoras_rango(7425000UL, 7521000UL, 720U, EMISORAS_DIA_CUALQUIERA,
                           r, 3U, &hubo);
        di(n == 3U && hubo >= 3U,
           "con sitio para tres, dice cuantas habia de verdad",
           "sin eso la pantalla esconde emisoras sin avisar");
        printf("        (en 7,473 MHz +-48 kHz caben 3 y hay %u)\n", (unsigned)hubo);
    }

    printf("\n  4. Y QUE SIGA SIENDO BARATO\n");
    /*
     * EL LISTON SALE DE LA MEDIDA, NO DE MI CABEZA - 15/10/2026.
     *
     * Aqui puse 120 de primeras, calculado a ojo: "una binaria son once
     * lecturas, mas una por frecuencia, mas tres por nombre". El banco
     * dijo 160 y era cierto: en la peor ventana hay 32 frecuencias y de
     * muchas de ellas hay que leer VARIAS entradas antes de encontrar una
     * que emita ahora -o todas, si ninguna emite-. Eso son ~4,7 lecturas
     * por frecuencia, no 1.
     *
     * Se pone 200: lo medido mas un tercio. No se pone 160 clavado porque
     * un liston pegado a la medida salta con cualquier lista nueva y
     * entonces se sube sin mirar, que es como un liston deja de medir
     * nada. Y no se pone 500 porque entonces no cazaria el fallo que
     * existe de verdad: que alguien lo reescriba recorriendo la tabla
     * entera, y eso son 1.400.
     *
     * SE PODRIA BAJAR A LA MITAD leyendo los registros de frecuencia de
     * golpe -son contiguos y de 8 bytes, 32 de ellos caben en una lectura
     * de 256-. No se hace hoy: son 32 lecturas de las 160 y complica el
     * recorrido. Queda escrito por si algun dia el bus aprieta.
     */
    di(peor_lect <= 200, "la peor ventana no pasa de 200 lecturas",
       "alguien ha vuelto a recorrer la tabla entera");
    printf("        peor medido: %ld lecturas (a la antigua eran %ld)\n",
           peor_lect, peor_viejo);

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

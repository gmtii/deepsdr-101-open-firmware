/*
 * EL 4415, CONTRA UN AUDIO DE VERDAD, Y PARA UNA PREGUNTA CONCRETA.
 *
 * s4415.c lleva escrito desde que se escribio que el orden de los dos bits
 * dentro del dibit es UNA SUPOSICION (ver BIT_ALTO_PRIMERO): la tabla XI del
 * estandar dice que a 75 bps se sacan dos bits del entrelazador por simbolo y
 * la XIV indexa por "00, 01, 10, 11", pero no dice con todas las letras cual
 * de los dos sale primero. Lo razonable es el de la izquierda y es lo que esta
 * puesto, con su interruptor a mano.
 *
 * Y el comentario dice tambien COMO SE RESUELVE: "con una grabacion que lleve
 * trafico de verdad se resuelve en un minuto: el orden bueno saca texto y el
 * otro saca basura". Lo que faltaba no era un razonamiento, era la grabacion.
 *
 * Esto es lo que mira si la tenemos. No decide el orden: decide si se PUEDE
 * decidir, que es la pregunta de antes y la que nadie habia contestado. Lo
 * primero que hay que saber de una grabacion es cuanto trafico lleva:
 *
 *   - si el flujo de dibits va casi todo a cero, es relleno de inactividad y
 *     los dos ordenes dan LO MISMO, asi que con esa grabacion no se puede
 *     decidir y decir lo contrario seria inventarse una confirmacion;
 *   - si lleva trafico, se compila con cada orden y se mira cual saca algo
 *     que se parezca a texto.
 *
 *   ffmpeg -i loquesea.mp3 -ac 1 -ar 12000 -f f32le salida.f32
 *   ./out/s4415aire salida.f32
 */
#include "s4415.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float g_trabajo[S4415_FLOATS];

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    float *x;
    uint32_t i;
    s4415_est_t e;
    uint32_t cuenta[4] = {0U, 0U, 0U, 0U};
    uint32_t total = 0U;
    uint8_t buf[256];

    if (argc < 2) { printf("uso: s4415aire fichero.f32 mono 12 kHz\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("no puedo abrir %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f) / 4L; fseek(f, 0, SEEK_SET);
    x = (float *)malloc((size_t)n * sizeof(float));
    if (!x || fread(x, sizeof(float), (size_t)n, f) != (size_t)n) { return 2; }
    fclose(f);

    if (!s4415_init(g_trabajo, S4415_FLOATS)) {
        printf("el 4415 no arranca\n"); return 2;
    }

    printf("\n%s  (%.1f s)\n\n", argv[1], (double)n / 12000.0);

    for (i = 0U; (long)(i + 256U) <= n; i += 256U) {
        uint16_t k, m;

        s4415_mete(&x[i], 256U);
        while (s4415_paso()) { }

        /* Los dibits que haya sacado, vaciando la cola. */
        m = s4415_saca_dibits(buf, (uint16_t)sizeof buf);
        for (k = 0U; k < m; k++) {
            cuenta[buf[k] & 3U]++;
            total++;
        }
    }

    s4415_estado(&e);

    printf("  ENGANCHE\n");
    printf("    segmentos de preambulo   %u\n", (unsigned)e.segmentos);
    printf("    calidad del enganche     %u %%\n", (unsigned)e.corr);
    printf("    velocidad                %s%s\n", s4415_bps_txt(e.bps_idx),
           e.largo ? "  entrelazado largo" : "");
    printf("    bloques por Viterbi      %u\n", (unsigned)e.bloques);
    printf("    bits de datos            %u\n", (unsigned)e.bits);
    printf("    EOM del estandar         %u\n", (unsigned)e.eom);

    printf("\n  EL FLUJO DE DIBITS  (%u en total)\n", (unsigned)total);
    if (total == 0U) {
        printf("    no ha salido ni uno: sin enganche no hay nada que mirar\n");
    } else {
        const char *k_n[4] = { "00", "01", "10", "11" };
        uint32_t d;
        uint32_t ceros = cuenta[0];

        for (d = 0U; d < 4U; d++) {
            printf("    %s   %6u   %5.1f %%\n", k_n[d], (unsigned)cuenta[d],
                   100.0 * (double)cuenta[d] / (double)total);
        }
        printf("\n");
        printf("  SE PUEDE DECIDIR EL ORDEN CON ESTA GRABACION?\n");
        /*
         * El criterio: los dos ordenes solo se distinguen en los dibits
         * ASIMETRICOS -01 y 10-, porque 00 y 11 son iguales del reves. Si
         * esos dos juntos no llegan a una parte razonable del flujo, dar la
         * vuelta al orden no cambia casi nada y la grabacion no sirve de
         * prueba.
         */
        {
            uint32_t asim = cuenta[1] + cuenta[2];
            double pc = 100.0 * (double)asim / (double)total;

            printf("    ceros (relleno de inactividad)  %5.1f %%\n",
                   100.0 * (double)ceros / (double)total);
            printf("    dibits asimetricos (01 y 10)    %5.1f %%"
                   "   <- los unicos que cambian al dar la vuelta\n", pc);
            if (pc < 10.0) {
                printf("\n    NO. Con %.1f %% de dibits asimetricos, los dos\n"
                       "    ordenes sacan practicamente lo mismo. Esta\n"
                       "    grabacion no puede confirmar nada, y decir que\n"
                       "    si seria inventarse una confirmacion.\n", pc);
            } else {
                printf("\n    SI. Hay trafico de sobra. Compila con\n"
                       "    BIT_ALTO_PRIMERO a 1 y a 0 y mira cual saca algo\n"
                       "    que se parezca a texto.\n");
            }
        }
    }
    printf("\n");
    free(x);
    s4415_fin();
    return 0;
}

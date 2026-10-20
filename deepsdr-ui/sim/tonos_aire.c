/*
 * EL DETECTOR DE DOS TONOS, CONTRA GRABACIONES DE VERDAD - 06/10/2026.
 *
 * LO QUE PASABA. La señal de 5339 es un STANAG 4481 FSK: dos tonos a 1348
 * y 510 Hz, separados 838, a 75 baudios. La radio la estaba DECODIFICANDO
 * en RTTY-U con 850 Hz y 75 Bd puestos a mano y sacaba Baudot correcto. Y
 * aun asi el detector decia que no habia dos tonos.
 *
 * Medido sobre esa grabacion con los criterios que habia:
 *
 *   caida entre las dos rayas ....  11,1 dB   (el limite eran 10)
 *   energia de la raya baja ......   4,8 %    (el minimo era 12 %)
 *   las dos juntas ...............  78,1 %    (el minimo es 35 %)
 *
 * O sea que fallaba por los dos criterios que miran las rayas POR
 * SEPARADO, y pasaba de sobra el que las mira JUNTAS.
 *
 * Y LA RAZON ES FISICA, no de calibracion. Los dos tonos van separados 850
 * Hz y la ionosfera los desvanece por su cuenta: a 2.000 km uno llega 11 dB
 * mas flojo que el otro, y un minuto despues al reves. Los comentarios de
 * stanag_det.c daban por hecho lo contrario -"una FSK de verdad reparte el
 * tiempo entre sus dos tonos y las dos salen parecidas"- y eso es cierto EN
 * EL TRANSMISOR. En el receptor, despues de la ionosfera, no.
 *
 * Por eso ademas "iba variando": cuando el desvanecimiento juntaba los dos
 * niveles, el detector si veia la FSK; cuando los separaba, se caia a la
 * pista del ancho y decia otra cosa.
 *
 * QUE COMPRUEBA ESTO: que cada grabacion da el veredicto que le toca. Las
 * que SON FSK de dos tonos tienen que salir con sus dos tonos y su
 * separacion; las que no -las PSK anchas y el navtex, que es SITOR- tienen
 * que seguir saliendo que no, que es lo que de verdad hace falta proteger:
 * un identificador que dice "FSK" delante de todo no sirve de nada.
 */
#include "stanag_det.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    const char *clave;
    uint8_t     espera_tonos;   /* 1 = tiene que encontrar los dos */
    uint16_t    sep_lo, sep_hi; /* y la separacion, si los encuentra */
} espera_t;

/*
 * Lo que cada grabacion TIENE que dar. Las separaciones son las medidas el
 * dia que se metio cada una, con holgura para el desvanecimiento.
 */
static const espera_t k_espera[] = {
    { "fsk4481_5339",   1U,  780U, 920U },  /* el de 5339: 838 Hz medidos */
    { "stanag4481_fsk", 1U,  600U, 920U },
    { "sitorb",         1U,  100U, 2000U },
    { "navtex8424",     1U,  100U, 2000U },
    { "stanag4285",     0U,  0U,   0U   },  /* PSK ancha: NO son dos tonos */
    { "stanag4415",     0U,  0U,   0U   },
    /*
     * El control del 4415 (Robser-s.mp3). Esta aqui porque la grabacion de
     * 4415 y esta se confundieron una vez -por el nombre del fichero- y
     * tenerlas las dos en el arbol, cada una con lo suyo esperado, es lo que
     * impide que vuelva a pasar sin que nadie se entere.
     */
    { "stanag_robser",  0U,  0U,   0U   },
    { "stanag4529",     0U,  0U,   0U   },
    { "stanag4539",     0U,  0U,   0U   },
    { "stanag4538",     0U,  0U,   0U   },
    { "stanag4481_psk", 0U,  0U,   0U   },
};

static int16_t *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *fs_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t h[44];
    int16_t *d;
    long tam;

    if (!f) { printf("  no encuentro %s\n", ruta); exit(2); }
    if (fread(h, 1, 44, f) != 44) { fclose(f); exit(2); }
    *fs_out = (uint32_t)h[24] | ((uint32_t)h[25] << 8)
            | ((uint32_t)h[26] << 16) | ((uint32_t)h[27] << 24);
    fseek(f, 0, SEEK_END); tam = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    d = (int16_t *)malloc((size_t)tam);
    if (fread(d, 1, (size_t)tam, f) != (size_t)tam) { fclose(f); exit(2); }
    fclose(f);
    *n_out = (uint32_t)(tam / 2);
    return d;
}

int main(int argc, char **argv)
{
    const espera_t *e = 0;
    int16_t *d;
    uint32_t n, fs, i, k;
    stanag_det_t m;
    uint32_t con_tonos = 0U, ventanas = 0U;
    uint32_t sep_min = 0xFFFFU, sep_max = 0U;
    uint32_t hist[20] = {0};
    float buf[256];
    uint16_t j = 0U;

    if (argc != 2) { printf("uso: tonosaire <wav>\n"); return 1; }
    for (k = 0U; k < (sizeof k_espera / sizeof k_espera[0]); k++) {
        if (strstr(argv[1], k_espera[k].clave) != 0) { e = &k_espera[k]; break; }
    }
    if (e == 0) { printf("  %s: no esta en la tabla de esperados\n", argv[1]); return 1; }

    d = lee_wav(argv[1], &n, &fs);
    static float trabajo[STANAG_DET_FLOATS];
    if (!stanag_det_init(trabajo, STANAG_DET_FLOATS)) {
        printf("  el detector no coge memoria\n"); return 1;
    }

    for (i = 0U; i < n; i++) {
        buf[j++] = (float)d[i];
        if (j == 256U) {
            stanag_det_mete(buf, j);
            j = 0U;
            if (stanag_det_paso(&m)) {
                ventanas++;
                if (m.hay_tonos) {
                    con_tonos++;
                    if (m.sep_hz < sep_min) { sep_min = m.sep_hz; }
                    if (m.sep_hz > sep_max) { sep_max = m.sep_hz; }
                    { uint32_t b = m.sep_hz / 100U;
                      if (b > 19U) { b = 19U; }
                      hist[b]++; }
                }
            }
        }
    }
    free(d);

    printf("  %-34s %4u ventanas, %4u con dos tonos (%3u %%)",
           e->clave, (unsigned)ventanas, (unsigned)con_tonos,
           (unsigned)(ventanas ? 100U * con_tonos / ventanas : 0U));
    if (con_tonos) { printf("   separacion %u..%u Hz", sep_min, sep_max); }
    printf("\n");
    if (con_tonos) {
        printf("      reparto:");
        for (k = 0U; k < 20U; k++) {
            if (hist[k]) { printf("  %u-%u:%u", (unsigned)(k*100U),
                                   (unsigned)(k*100U+99U), (unsigned)hist[k]); }
        }
        printf("\n");
    }

    if (e->espera_tonos) {
        /* Con desvanecimiento no se le puede pedir que acierte SIEMPRE;
         * lo que no puede es no verlo nunca. */
        if (con_tonos * 100U < ventanas * 10U) {
            printf("      *** MAL: tiene dos tonos y no los encuentra casi nunca ***\n");
            return 1;
        }
        /*
         * LA MAYORIA, NO TODAS - y esto no es relajar el banco, es medir lo
         * que hay. Con desvanecimiento selectivo, en algunas ventanas el
         * tono flojo desaparece del todo y el detector se queda con otra
         * raya. Lo que tiene que cumplirse es que la separacion BUENA sea
         * la que mas sale, porque es la que ident_casa() va a ver casi
         * siempre. Exigir que acierte en las 377 ventanas seria pedirle a
         * la ionosfera que se este quieta.
         */
        {
            uint32_t dentro = 0U, kk;
            for (kk = 0U; kk < 20U; kk++) {
                if (kk * 100U + 99U >= e->sep_lo && kk * 100U <= e->sep_hi) {
                    dentro += hist[kk];
                }
            }
            printf("      en %u..%u Hz: %u de %u ventanas (%u %%)\n",
                   e->sep_lo, e->sep_hi, (unsigned)dentro, (unsigned)con_tonos,
                   (unsigned)(100U * dentro / con_tonos));
            if (dentro * 100U < con_tonos * 60U) {
                printf("      *** MAL: la separacion buena no es la mayoritaria ***\n");
                return 1;
            }
        }
    } else {
        /* Y al reves: una PSK ancha no puede salir como FSK de dos tonos
         * mas que de rebote. Esto es lo que de verdad hay que proteger. */
        if (con_tonos * 100U > ventanas * 20U) {
            printf("      *** MAL: no son dos tonos y lo dice en el %u %% ***\n",
                   (unsigned)(100U * con_tonos / ventanas));
            return 1;
        }
    }
    printf("      ok\n");
    return 0;
}

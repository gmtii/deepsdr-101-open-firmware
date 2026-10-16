/*
 * EL DECODIFICADOR DE CW, CONTRA UN AUDIO DE VERDAD.
 *
 * Mismo oficio que sim/identaire.c y sim/sstvaire.c: mete un fichero de
 * muestras crudas -f32, mono, 12 kHz, lo mismo que le llega a cw_process()
 * dentro de la radio- por el MISMO cw.c que se compila para el GD32, y
 * enseña lo que la radio diria.
 *
 *   ffmpeg -i loquesea.ogg -ac 1 -ar 12000 -f f32le salida.f32
 *   ./out/cwaire salida.f32 [tono_hz]
 *
 * No lleva valores esperados: es una herramienta de medir, no una
 * comprobacion, y por eso no entra en "make comprueba".
 */
#include "cw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOQUE ((uint32_t)CW_BLOCK_SAMPLES)

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    float *x;
    uint32_t i;
    char c;
    char texto[4096];
    uint32_t nt = 0U;

    if (argc < 2) { printf("uso: cwaire fichero.f32 [tono_hz]\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("no puedo abrir %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f) / 4L; fseek(f, 0, SEEK_SET);
    x = (float *)malloc((size_t)n * sizeof(float));
    if (!x || fread(x, sizeof(float), (size_t)n, f) != (size_t)n) { return 2; }
    fclose(f);

    cw_init();
    cw_set_enabled(1U);
    if (argc > 2) { cw_set_pitch_hz((float)atof(argv[2])); }
    /* tercer argumento: 0 apaga el autoenganche de tono */
    if (argc > 3) { cw_set_autotune((uint8_t)(atoi(argv[3]) != 0)); }

    printf("  %s: %.1f s\n", argv[1], (double)n / 12000.0);
    printf("  tono puesto %.1f Hz, autoenganche %s\n",
           (double)cw_get_pitch_hz(), cw_get_autotune() ? "si" : "no");

    for (i = 0UL; i + BLOQUE <= (uint32_t)n; i += BLOQUE) {
        cw_process(&x[i], BLOQUE);
        while (cw_get_char(&c)) {
            if (nt < sizeof texto - 1U) { texto[nt++] = c; }
        }
    }
    texto[nt] = '\0';

    printf("  --------------------------------------------------\n");
    printf("  tono detectado   %.1f Hz   (desvio %.1f)\n",
           (double)cw_get_detect_hz(), (double)cw_get_offset_hz());
    printf("  velocidad        %.1f ppm\n", (double)cw_get_wpm());
    printf("  nivel            %.4f   ruido %.4f   pico %.4f\n",
           (double)cw_get_level(), (double)cw_get_floor(), (double)cw_get_peak());
    printf("  enganchado       %u   calidad %.0f%%\n",
           (unsigned)cw_get_decode_ok(), 100.0 * (double)cw_get_quality());
    printf("  caracteres       %lu\n", (unsigned long)nt);
    printf("  --------------------------------------------------\n");
    printf("%s\n", texto);
    free(x);
    return 0;
}

/*
 * BANCO DEL ANALIZADOR DE AUDIO (User/analizador.c). 23/09/2026.
 *
 * Un analizador de espectro es un instrumento: si miente, miente con pinta
 * de dato. Por eso aqui no se comprueba "que funciona" sino las cuatro cosas
 * concretas que lo harian mentir:
 *
 *   1. QUE LA FRECUENCIA SEA LA FRECUENCIA. Se meten tonos de frecuencia
 *      conocida y se mira en que bin caen. Un error de escala se veria como
 *      "la subportadora esta en 118 Hz", y uno se lo creeria.
 *   2. QUE LA BANDA SEA PLANA. Se barre un tono de amplitud constante por
 *      toda la banda util y se mira cuanto cambia el nivel. Una caida en los
 *      bordes se lee como "esa emisora manda menos ahi".
 *   3. QUE NO SE INVENTE RAYAS. Se mete un tono FUERA de la banda util y se
 *      comprueba que su alias no aparece dentro. Esto es lo que justifica
 *      que la banda util acabe en 0,8 de Nyquist y no en 1,0.
 *   4. QUE SEPARE 100 DE 125 Hz, que es la pregunta que motivo escribir esto.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "analizador.h"

#define FS 96000.0f
#define BLOQUE 256

static float s_t = 0.0f;

/* Mete `seg` segundos de un tono y devuelve los bins del ultimo analisis. */
static int corre(float hz, float amp, float *db, float ruido)
{
    float b[BLOQUE];
    int i, k, hechos = 0;
    unsigned long r = 12345UL;
    int bloques = (int)(FS * 2.0f / BLOQUE);

    for (k = 0; k < bloques; k++) {
        for (i = 0; i < BLOQUE; i++) {
            float x = amp * sinf(6.2831853f * hz * s_t);
            if (ruido > 0.0f) {
                r = r * 1103515245UL + 12345UL;
                x += ruido * (((float)((r >> 8) & 0xFFFFUL) / 32768.0f) - 1.0f);
            }
            b[i] = x;
            s_t += 1.0f / FS;
            if (s_t > 10.0f) { s_t -= 10.0f; }
        }
        analiz_feed(b, BLOQUE);
        if (analiz_listo()) { analiz_bins(db); hechos++; }
    }
    return hechos;
}

/* El bin con mas nivel dentro de la banda util, y su nivel. */
static int pico(const float *db, float *nivel)
{
    int k, mejor = 0;
    int n = (int)analiz_bins_utiles();
    for (k = 1; k < n; k++) { if (db[k] > db[mejor]) { mejor = k; } }
    *nivel = db[mejor];
    return mejor;
}

/* El nivel mas alto de la banda util EXCLUYENDO un entorno de `evitar`. */
static float pico_fuera(const float *db, int evitar, int margen)
{
    int k, n = (int)analiz_bins_utiles();
    float mx = -1e9f;
    for (k = 1; k < n; k++) {
        if (k >= evitar - margen && k <= evitar + margen) { continue; }
        if (db[k] > mx) { mx = db[k]; }
    }
    return mx;
}

int main(void)
{
    static float db[FFT_BINS_USEFUL];
    int fallos = 0;
    static const char *k_nombres[ANALIZ_N] = { "2400", "1200", "600", "300", "150" };
    int z;

    fft_init();
    printf("Analizador de audio: FFT de %d puntos, %d bins utiles de %d\n",
           FFT_SIZE, (int)(FFT_BINS_USEFUL * 0.8), FFT_BINS_USEFUL);

    /* --- 1. La frecuencia es la frecuencia ---------------------------- */
    printf("\n  un tono conocido cae donde debe:\n");
    for (z = 0; z < (int)ANALIZ_N; z++) {
        float hzbin, tope, nivel;
        int k;
        analiz_start(FS);
        analiz_zoom((analiz_zoom_t)z);
        hzbin = analiz_hz_por_bin();
        tope  = analiz_tope_hz();
        printf("    zoom %-5s  tope %7.1f Hz   %5.2f Hz por bin\n",
               k_nombres[z], (double)tope, (double)hzbin);
        for (k = 0; k < 3; k++) {
            float hz = tope * (0.25f + 0.25f * (float)k);
            int b, esperado;
            analiz_start(FS);
            analiz_zoom((analiz_zoom_t)z);
            s_t = 0.0f;
            corre(hz, 8000.0f, db, 0.0f);
            b = pico(db, &nivel);
            esperado = (int)(hz / hzbin + 0.5f);
            printf("      %8.2f Hz -> bin %3d (esperado %3d), error %5.2f Hz",
                   (double)hz, b, esperado, (double)((b - hz / hzbin) * hzbin));
            if (b < esperado - 1 || b > esperado + 1) {
                printf("   MAL");
                fallos++;
            }
            printf("\n");
        }
    }

    /* --- 2. La banda util es plana ------------------------------------ */
    printf("\n  barrido de nivel por la banda util (tono de amplitud constante):\n");
    for (z = 0; z < (int)ANALIZ_N; z++) {
        float tope, mn = 1e9f, mx = -1e9f;
        int k;
        analiz_start(FS);
        analiz_zoom((analiz_zoom_t)z);
        tope = analiz_tope_hz();
        for (k = 1; k <= 9; k++) {
            float hz = tope * (float)k / 10.0f;
            float nivel;
            analiz_start(FS);
            analiz_zoom((analiz_zoom_t)z);
            s_t = 0.0f;
            corre(hz, 8000.0f, db, 0.0f);
            (void)pico(db, &nivel);
            if (nivel < mn) { mn = nivel; }
            if (nivel > mx) { mx = nivel; }
        }
        printf("    zoom %-5s  variacion %5.2f dB en toda la banda", k_nombres[z], (double)(mx - mn));
        if (mx - mn > 2.0f) { printf("   MAL (mas de 2 dB)"); fallos++; }
        printf("\n");
    }

    /* --- 3. No se inventa rayas --------------------------------------- */
    printf("\n  un tono FUERA de la banda util no deja alias dentro:\n");
    for (z = 0; z < (int)ANALIZ_N; z++) {
        float tope, nivel_dentro, nivel_ref;
        int b;
        float hz_fuera;

        /* referencia: el mismo tono pero dentro, para saber cuanto sube un
         * tono de verdad y poder comparar */
        analiz_start(FS); analiz_zoom((analiz_zoom_t)z); s_t = 0.0f;
        tope = analiz_tope_hz();
        corre(tope * 0.5f, 8000.0f, db, 0.0f);
        b = pico(db, &nivel_ref);

        /* el de fuera: a 1,4 veces el tope, o sea pasado Nyquist, donde su
         * alias caeria dentro si el filtro no lo parara */
        hz_fuera = tope * 1.40f;
        analiz_start(FS); analiz_zoom((analiz_zoom_t)z); s_t = 0.0f;
        corre(hz_fuera, 8000.0f, db, 0.0f);
        b = pico(db, &nivel_dentro);
        (void)b;
        printf("    zoom %-5s  tono en %8.1f Hz -> lo mas alto dentro esta %5.1f dB"
               " por debajo de un tono de verdad",
               k_nombres[z], (double)hz_fuera, (double)(nivel_ref - nivel_dentro));
        if (nivel_ref - nivel_dentro < 30.0f) {
            printf("   MAL (menos de 30 dB)");
            fallos++;
        }
        printf("\n");
    }

    /* --- 4. Separa 100 de 125 Hz -------------------------------------- */
    printf("\n  100 Hz contra 125 Hz, que es para lo que se hizo:\n");
    {
        float hzbin, n100, n125, valle;
        int b100, b125;
        analiz_start(FS); analiz_zoom(ANALIZ_300); s_t = 0.0f;
        hzbin = analiz_hz_por_bin();
        corre(100.0f, 8000.0f, db, 0.0f);
        b100 = pico(db, &n100);
        analiz_start(FS); analiz_zoom(ANALIZ_300); s_t = 0.0f;
        corre(125.0f, 8000.0f, db, 0.0f);
        b125 = pico(db, &n125);
        printf("    100 Hz -> bin %d, 125 Hz -> bin %d: %d bins de separacion (%.2f Hz por bin)\n",
               b100, b125, b125 - b100, (double)hzbin);
        if (b125 - b100 < 5) {
            printf("    MAL: menos de cinco bins, no se distinguirian en pantalla\n");
            fallos++;
        }
        /* y las dos a la vez: el valle entre ellas tiene que hundirse */
        analiz_start(FS); analiz_zoom(ANALIZ_300); s_t = 0.0f;
        {
            float b[BLOQUE]; int i, k;
            int bloques = (int)(FS * 2.0f / BLOQUE);
            for (k = 0; k < bloques; k++) {
                for (i = 0; i < BLOQUE; i++) {
                    b[i] = 8000.0f * sinf(6.2831853f * 100.0f * s_t)
                         + 8000.0f * sinf(6.2831853f * 125.0f * s_t);
                    s_t += 1.0f / FS;
                    if (s_t > 10.0f) { s_t -= 10.0f; }
                }
                analiz_feed(b, BLOQUE);
                if (analiz_listo()) { analiz_bins(db); }
            }
        }
        valle = db[(b100 + b125) / 2];
        printf("    las dos a la vez: pico %.1f dB, valle entre ellas %.1f dB, hondura %.1f dB\n",
               (double)n100, (double)valle, (double)(n100 - valle));
        if (n100 - valle < 10.0f) {
            printf("    MAL: el valle no se hunde, en pantalla saldria una sola raya gorda\n");
            fallos++;
        }
        (void)pico_fuera;
    }

    printf("\n%s\n", fallos ? "  HAY FALLOS" : "  TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

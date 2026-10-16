/*
 * Comprueba la autoescala del espectro con cuadros sinteticos.
 *
 * Existe porque la autoescala "no hacia nada" en la radio y el motivo no
 * se veia leyendo el codigo: la logica era correcta, lo que fallaba era
 * de que dos numeros se fiaba. Aqui se le dan cuadros donde se sabe cual
 * es la respuesta buena y se comprueba que sale.
 *
 * El caso que importa es el tercero: ruido, una senal, y DOS bins
 * disparados por el pico de continua. Con extremos, esos dos bins mandan
 * sobre los otros quinientos diez.
 */
#include "spec_agc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define N 512

static uint32_t rng = 1u;
static float uni(void) { rng = rng*1664525u+1013904223u; return (float)((rng>>8)&0xFFFFFFu)/16777216.0f; }

/* La version VIEJA, con minimo y maximo absolutos, para comparar. */
static void viejo(const float *db, uint32_t n, float *mn, float *mx)
{
    float fmin = db[0], fmax = db[0], tmin, tmax;
    uint32_t k;
    for (k = 1; k < n; k++) {
        if (db[k] < fmin) fmin = db[k];
        if (db[k] > fmax) fmax = db[k];
    }
    tmin = fmin - SPEC_AGC_FLOOR_MARGIN_DB;
    tmax = tmin + SPEC_AGC_MIN_SPAN_DB;
    if (fmax + SPEC_AGC_CEIL_MARGIN_DB > tmax) tmax = fmax + SPEC_AGC_CEIL_MARGIN_DB;
    if (tmin < SPEC_AGC_DB_FLOOR) tmin = SPEC_AGC_DB_FLOOR;
    if (tmax > SPEC_AGC_DB_CEIL)  tmax = SPEC_AGC_DB_CEIL;
    *mn = tmin; *mx = tmax;
}

static void caso(const char *nombre, const float *db, float util_lo, float util_hi, int *fallos)
{
    float vn, vx, nn, nx;
    viejo(db, N, &vn, &vx);
    spec_agc_target(db, N, &nn, &nx);
    printf("  %-38s antes %6.0f..%-4.0f (%3.0f dB)   ahora %6.0f..%-4.0f (%3.0f dB)\n",
           nombre, (double)vn, (double)vx, (double)(vx-vn),
           (double)nn, (double)nx, (double)(nx-nn));
    /* La ventana tiene que CONTENER lo que interesa ver... */
    if (nn > util_lo || nx < util_hi) {
        printf("      *** FALLA: se deja fuera lo util (%.0f..%.0f)\n", (double)util_lo, (double)util_hi);
        (*fallos)++;
    }
    /* ...y no ser mucho mayor que eso, que es de lo que se trata. */
    if ((nx - nn) > (util_hi - util_lo) + 60.0f) {
        printf("      *** FALLA: la ventana es %.0f dB para %.0f dB de contenido\n",
               (double)(nx-nn), (double)(util_hi-util_lo));
        (*fallos)++;
    }
}

int main(void)
{
    static float db[N];
    int i, fallos = 0;

    printf("\n=== Autoescala del espectro: extremos frente a percentiles ===\n\n");

    for (i = 0; i < N; i++) db[i] = 70.0f + (uni()-0.5f)*6.0f;
    caso("ruido plano a 70 dB", db, 67.0f, 73.0f, &fallos);

    for (i = 0; i < N; i++) db[i] = 70.0f + (uni()-0.5f)*6.0f;
    for (i = 250; i < 262; i++) db[i] = 100.0f;
    caso("ruido + senal de 30 dB", db, 67.0f, 100.0f, &fallos);

    for (i = 0; i < N; i++) db[i] = 70.0f + (uni()-0.5f)*6.0f;
    for (i = 250; i < 262; i++) db[i] = 100.0f;
    db[255] = 119.0f; db[256] = 118.0f;              /* pico de continua */
    for (i = 0; i < 4; i++) db[i] = 2.0f;            /* bordes hundidos */
    for (i = N-4; i < N; i++) db[i] = 2.0f;
    caso("lo mismo + pico de continua y bordes", db, 67.0f, 100.0f, &fallos);

    for (i = 0; i < N; i++) db[i] = 40.0f + (uni()-0.5f)*4.0f;
    for (i = 100; i < 140; i++) db[i] = 95.0f;       /* senal ancha de verdad */
    caso("senal ancha: NO debe recortarse", db, 38.0f, 95.0f, &fallos);

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "todo correcto");
    return fallos ? 1 : 0;
}

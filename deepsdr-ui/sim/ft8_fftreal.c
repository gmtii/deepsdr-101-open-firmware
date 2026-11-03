/*
 * QUE LA FFT DE ENTRADA REAL DIGA LO MISMO QUE LA COMPLEJA.
 *
 * ft8_fft.c hacia una transformada compleja de 2.048 puntos con la parte
 * imaginaria a cero y tiraba la mitad de la salida. Desde el 02/11/2026
 * hace la de 1.024 con las muestras repartidas entre real e imaginaria y
 * desenreda al final: 8.192 bytes menos y la mitad de mariposas.
 *
 * Esto corre LAS DOS -la nueva y la vieja, que se conserva detras de
 * FT8_FFT_COMPLEJA- contra las mismas muestras y compara bin a bin.
 *
 * NO se exige igualdad al bit: son dos ordenes de operaciones distintos
 * sobre floats de 32 bits y eso no puede dar lo mismo. Lo que se exige es
 * que la diferencia no llegue a mover un escalon de la escalera con la
 * que se guarda (FTX_MAG_PASO_DB), porque por debajo de eso el
 * decodificador no se entera.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ft8_fft.h"

/* Las dos versiones, cada una con su memoria: el fichero de verdad las
 * tiene en la union prestada y aqui no hay union. */
#define FT8_FFT_SIN_RAM_COMPARTIDA 1

static int fallos;

static void ok(const char *que, int bien, const char *det)
{
    printf("    %-48s %s   %s\n", que, bien ? "ok" : "*** MAL ***", det);
    if (!bien) { fallos++; }
}

/*
 * LAS DOS DEVUELVEN CUARTOS DE DECIBELIO desde el 02/11/2026, no
 * decibelios en flotante: ver FT8_FFT_DB_ESCALA. Este banco compara en
 * las unidades que de verdad se guardan, que es lo que importa.
 */
extern void ft8_fft_compute_db_rango(const int16_t *s, int16_t *o,
                                     uint32_t base, uint32_t paso, uint32_t n);
extern void vieja_compute_db_rango(const int16_t *s, int16_t *o,
                                   uint32_t base, uint32_t paso, uint32_t n);

static int16_t s_muestras[FT8_FFT_SIZE];
static int16_t s_nueva[FT8_FFT_SIZE / 2];
static int16_t s_vieja[FT8_FFT_SIZE / 2];

static uint32_t vai = 123456789u;
static float uni(void) { vai = vai * 1103515245u + 12345u;
                         return (float)((vai >> 8) & 0xFFFFFFu) / 16777216.0f - 0.5f; }

int main(void)
{
    int caso;
    float peor = 0.0f;
    const char *k_nom[4] = { "ruido blanco", "un tono en medio de un bin",
                             "un tono a medio bin", "tono fuerte + ruido" };

    printf("\nLA FFT DE ENTRADA REAL CONTRA LA COMPLEJA\n\n");

    for (caso = 0; caso < 4; caso++) {
        int n;
        float peor_caso = 0.0f;
        char det[80];

        for (n = 0; n < FT8_FFT_SIZE; n++) {
            double v = 0.0;
            if (caso == 0 || caso == 3) { v = 3000.0 * uni(); }
            if (caso == 1) { v += 8000.0 * sin(2.0 * M_PI * 200.0 * n / FT8_FFT_SIZE); }
            if (caso == 2) { v += 8000.0 * sin(2.0 * M_PI * 200.5 * n / FT8_FFT_SIZE); }
            if (caso == 3) { v += 12000.0 * sin(2.0 * M_PI * 333.25 * n / FT8_FFT_SIZE); }
            if (v >  32767.0) { v =  32767.0; }
            if (v < -32768.0) { v = -32768.0; }
            s_muestras[n] = (int16_t)v;
        }

        ft8_fft_compute_db_rango(s_muestras, s_nueva, 0U, 1U, FT8_FFT_SIZE / 2U);
        vieja_compute_db_rango(s_muestras, s_vieja, 0U, 1U, FT8_FFT_SIZE / 2U);

        for (n = 0; n < (int)(FT8_FFT_SIZE / 2U); n++) {
            float d = fabsf((float)(s_nueva[n] - s_vieja[n]))
                      / FT8_FFT_DB_ESCALA;
            if (d > peor_caso) { peor_caso = d; }
        }
        if (peor_caso > peor) { peor = peor_caso; }
        snprintf(det, sizeof det, "el peor bin se mueve %.3f dB", (double)peor_caso);
        ok(k_nom[caso], peor_caso < 0.5f, det);
    }

    {
        char det[80];
        snprintf(det, sizeof det, "%.3f dB contra el escalon de 6,8", (double)peor);
        ok("y nunca llega a mover un escalon de la escalera de 3 bits",
           peor < 6.8f, det);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

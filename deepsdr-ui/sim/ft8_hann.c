/*
 * ======================================================================
 * LA VENTANA DE HANN DE FT8, QUE YA NO ES UNA TABLA - 28/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE: porque al quitar la tabla se quito tambien lo unico
 * contra lo que se podia comparar. ***
 *
 * Hasta hoy la ventana eran 1.024 floats en flash, generados por
 * tools/gen_ft8_tablas.py. Ahora la calcula ft8_fft_hann() con la misma
 * aproximacion de Bhaskara-I, y se comprobo entrada por entrada que salen
 * **las 1.024 identicas bit a bit** antes de borrar la tabla.
 *
 * Pero esa comprobacion solo se pudo hacer UNA VEZ, mientras las dos
 * cosas existian a la vez. A partir de ahora, si alguien toca la
 * aproximacion -o el signo, o el (N-1) del denominador- no hay tabla que
 * lo contradiga: la radio seguiria compilando y decodificando PEOR, que
 * es la clase de averia que no se ve.
 *
 * ASI QUE ESTO COMPARA CONTRA LA DEFINICION, no contra una copia:
 *
 *   w[n] = 0,5 - 0,5 * cos(2*pi*n / (N-1))
 *
 * calculada con el coseno de la biblioteca en doble precision. La
 * aproximacion de Bhaskara-I no da eso exacto -ni tiene que darlo- pero
 * tiene un error acotado y conocido, y lo que se exige es que siga dentro
 * de el. Si alguien la cambia por sinf() de verdad, esto NO se pone rojo
 * (seria mejor), y eso es a proposito; lo que se pone rojo es que se
 * tuerza.
 *
 * Y ADEMAS LAS PROPIEDADES QUE TIENE QUE CUMPLIR UNA VENTANA DE HANN, que
 * no dependen de como se calcule: vale cero en los dos extremos, uno en
 * el centro, es simetrica y no se sale de [0,1]. Un fallo de signo o un
 * (N) en vez de (N-1) rompe alguna de las cuatro.
 */
#include <stdio.h>
#include <math.h>
#include "ft8_fft.h"
#include "ft8_shared_ram.h"

/* El fichero de la FFT vive dentro de la memoria compartida; aqui solo se
 * le da un sitio donde estar. */
ft8_shared_ram_t g_ft8_shared_ram;

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

int main(void)
{
    uint32_t n;
    double peor = 0.0;
    uint32_t peor_n = 0;
    float fuera = 0.0f;
    double sim = 0.0;
    char t[96];

    printf("\nLA VENTANA DE HANN DE FT8 (NFFT %u)\n\n", (unsigned)FT8_FFT_SIZE);

    /* --------------------------------------------------------------
     * 1. CONTRA LA DEFINICION
     * -------------------------------------------------------------- */
    for (n = 0; n < FT8_FFT_SIZE; n++) {
        double exacta = 0.5 - 0.5 * cos(2.0 * M_PI * (double)n
                                        / (double)(FT8_FFT_SIZE - 1U));
        double d = fabs((double)ft8_fft_hann(n) - exacta);

        if (d > peor) { peor = d; peor_n = n; }
    }
    printf("  1. CONTRA 0,5 - 0,5*cos(2*pi*n/(N-1))\n");
    printf("      el peor error esta en n=%u y vale %.6f\n", peor_n, peor);
    /*
     * El tope es 0,0021 y no un numero redondo: es lo que de verdad da la
     * aproximacion de Bhaskara-I hoy (0,000816), con mas del doble de
     * holgura.
     * Apretarlo mas seria escribir el valor de hoy y llamarlo requisito;
     * aflojarlo mas dejaria pasar una ventana torcida.
     */
    pasa("se queda dentro del error de Bhaskara-I", peor < 0.0021);

    /* --------------------------------------------------------------
     * 2. Y LAS PROPIEDADES DE UNA VENTANA DE HANN
     * -------------------------------------------------------------- */
    printf("\n  2. LAS PROPIEDADES QUE NO DEPENDEN DE COMO SE CALCULE\n");
    printf("      w[0] = %.9f   w[N-1] = %.9f   w[N/2] = %.9f\n",
           (double)ft8_fft_hann(0),
           (double)ft8_fft_hann(FT8_FFT_SIZE - 1U),
           (double)ft8_fft_hann(FT8_FFT_SIZE / 2U));
    pasa("vale cero en el primer extremo",
         fabsf(ft8_fft_hann(0)) < 0.0025f);
    pasa("y en el ultimo",
         fabsf(ft8_fft_hann(FT8_FFT_SIZE - 1U)) < 0.0025f);
    pasa("y uno en el centro",
         fabsf(ft8_fft_hann(FT8_FFT_SIZE / 2U) - 1.0f) < 0.0025f);

    for (n = 0; n < FT8_FFT_SIZE; n++) {
        float w = ft8_fft_hann(n);
        float e = ft8_fft_hann(FT8_FFT_SIZE - 1U - n);
        float d = (w > e) ? (w - e) : (e - w);

        if (w < -0.0025f || w > 1.0025f) { fuera += 1.0f; }
        if ((double)d > sim) { sim = (double)d; }
    }
    snprintf(t, sizeof t, "es simetrica (la peor pareja difiere %.6f)", sim);
    pasa(t, sim < 0.0025);
    pasa("y ninguna entrada se sale de [0,1]", fuera == 0.0f);

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

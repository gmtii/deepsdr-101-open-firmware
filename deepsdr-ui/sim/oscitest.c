/*
 * EL OSCILOSCOPIO, MIRADO DE FRENTE.
 *
 * POR QUE EXISTE - 24/10/2026
 * ---------------------------
 * *** Preguntado por el usuario, y es la pregunta correcta: "cuando
 * acabes, puede que a cw le pase lo mismo que a rtty?". ***
 *
 * Le pasaba. Hoy rtty_scope.c dejo de tener su propia FFT -usa la de
 * fft.c, ver el bloque del principio de aquel fichero- y en la primera
 * version di por hecho que fft_init() la habria llamado main() antes. En
 * la radio era verdad; en los bancos no, y la transformada corrio con las
 * tablas a CERO.
 *
 * Lo cazo sim/rtty_solo.c, pero lo cazo DE REBOTE: lo que fallaba era el
 * ajuste automatico de RTTY, y el mensaje decia "acaba en 50 baudios" y
 * "los tonos encima de la señal". Eso es un sintoma a tres saltos de la
 * causa. Y sobre todo: el osciloscopio NO ES DE RTTY. Lo alimentan NUEVE
 * modos -ver la lista de demod_am.c-:
 *
 *     RTTY, CW, NAVTEX, WEFAX, SSTV, FT8, PSK31, DSC y APRS
 *
 * O sea que un fallo ahi deja ciegos a los nueve, y el unico banco que lo
 * tocaba era el de uno de ellos, por un camino indirecto. Esto lo mira de
 * frente: un tono conocido entra, y el pico tiene que salir en su bin.
 *
 * Y LO PRIMERO QUE COMPRUEBA ES LA TRAMPA EN LA QUE CAI: que funcione sin
 * que NADIE haya llamado a fft_init() antes. Si alguien vuelve a dar por
 * hecho un orden de arranque, esto se pone rojo y lo dice con su nombre.
 */
#include "rtty_scope.h"
#include "fft.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-52s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* La tasa a la que el demodulador le da el audio. Ver rtty_scope.h: los
 * Hz por bin son 12000/RTTY_SCOPE_FFT_SIZE. */
#define FS   12000.0

/* Mete un tono de f Hz hasta que el osciloscopio tenga un cuadro. */
static const float *cuadro_con_tono(double f)
{
    static float lote[64];
    double fase = 0.0;
    int vueltas;

    for (vueltas = 0; vueltas < 200; vueltas++) {
        int k;
        for (k = 0; k < 64; k++) {
            lote[k] = (float)(8000.0 * sin(fase));
            fase += 2.0 * M_PI * f / FS;
        }
        rtty_scope_feed(lote, 64U);
        rtty_scope_poll();
        if (rtty_scope_frame_ready()) {
            return rtty_scope_get_frame();
        }
    }
    return 0;
}

static int bin_del_pico(const float *m, int *fuera)
{
    int k, mejor = 0;
    float pico = -1.0f, suma = 0.0f;

    for (k = 0; k < (int)RTTY_SCOPE_BINS; k++) {
        suma += m[k];
        if (m[k] > pico) { pico = m[k]; mejor = k; }
    }
    /* Cuanto sobresale el pico sobre la media, en veces. Con las tablas a
     * cero esto sale 1 -todo igual- y con un tono de verdad, cientos. */
    if (fuera != 0) {
        float media = suma / (float)RTTY_SCOPE_BINS;
        *fuera = (media > 0.0f) ? (int)(pico / media) : 0;
    }
    return mejor;
}

int main(void)
{
    char t[120];
    float hz_bin;

    printf("\n== El osciloscopio de los nueve modos ==\n");

    /*
     * 1. SIN LLAMAR A fft_init() A MANO. Esta es la prueba que habria
     *    ahorrado el rodeo de hoy: rtty_scope_init() tiene que dejar la
     *    transformada utilizable el solo.
     */
    printf("\n  arrancando solo con rtty_scope_init()\n");
    rtty_scope_init();
    hz_bin = rtty_scope_hz_per_bin();
    {
        const float *m = cuadro_con_tono(1500.0);
        int fuera = 0, bin, esperado;

        di("da un cuadro", m != 0, 0);
        if (m == 0) {
            printf("\n%s (%d fallos)\n\n", "HAY FALLOS", ++fallos);
            return 1;
        }
        bin = bin_del_pico(m, &fuera);
        esperado = (int)((1500.0 / (double)hz_bin) + 0.5);
        snprintf(t, sizeof t, "bin %d, esperado %d (%.1f Hz por bin)",
                 bin, esperado, (double)hz_bin);
        di("el pico cae en el bin del tono", (bin >= esperado - 1)
                                             && (bin <= esperado + 1), t);
        snprintf(t, sizeof t, "%d veces la media", fuera);
        di("y sobresale de verdad, no es un espectro plano", fuera > 20, t);
    }

    /*
     * 2. Y ahora en el otro orden, que es el de la radio. Tiene que dar
     *    lo mismo: fft_init() solo rellena tablas a partir de constantes.
     */
    printf("\n  y con fft_init() llamada antes, como en la radio\n");
    fft_init();
    rtty_scope_init();
    {
        const float *m = cuadro_con_tono(1500.0);
        int fuera = 0, bin, esperado;

        di("da un cuadro", m != 0, 0);
        if (m != 0) {
            bin = bin_del_pico(m, &fuera);
            esperado = (int)((1500.0 / (double)hz_bin) + 0.5);
            snprintf(t, sizeof t, "bin %d, esperado %d", bin, esperado);
            di("el pico sigue donde tiene que estar",
               (bin >= esperado - 1) && (bin <= esperado + 1), t);
        }
    }

    /*
     * 3. Tres tonos a lo largo de la banda, que es donde se ve si el eje
     *    esta bien y no solo si hay pico. 500 Hz es la zona baja, 2.125 es
     *    la marca de RTTY de siempre y 3.000 es la alta.
     */
    printf("\n  el eje entero, no solo un punto\n");
    {
        static const double tonos[3] = { 500.0, 2125.0, 3000.0 };
        int k;

        for (k = 0; k < 3; k++) {
            const float *m;
            int bin, esperado;

            rtty_scope_init();
            m = cuadro_con_tono(tonos[k]);
            if (m == 0) {
                di("da un cuadro", 0, 0);
                continue;
            }
            bin = bin_del_pico(m, 0);
            esperado = (int)((tonos[k] / (double)hz_bin) + 0.5);
            snprintf(t, sizeof t, "%.0f Hz -> bin %d, esperado %d",
                     tonos[k], bin, esperado);
            di("el pico cae donde dice el eje",
               (bin >= esperado - 1) && (bin <= esperado + 1), t);
        }
    }

    /*
     * 4. Y el tamaño, que es de donde salio todo esto. Si alguien separa
     *    el osciloscopio de fft.c otra vez, que no compile es mejor que
     *    que se vea raro.
     */
    printf("\n  los dos tamaños siguen siendo el mismo\n");
    {
        snprintf(t, sizeof t, "osciloscopio %u, fft %u",
                 (unsigned)RTTY_SCOPE_FFT_SIZE, (unsigned)FFT_SIZE);
        di("la ventana del osciloscopio es la de fft.c",
           RTTY_SCOPE_FFT_SIZE == FFT_SIZE, t);
        snprintf(t, sizeof t, "osciloscopio %u, fft %u",
                 (unsigned)RTTY_SCOPE_BINS, (unsigned)FFT_BINS_USEFUL);
        di("y los bins tambien", RTTY_SCOPE_BINS == FFT_BINS_USEFUL, t);
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

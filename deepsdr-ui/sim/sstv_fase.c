/*
 * CUANTO BAILA EL BORDE IZQUIERDO: LA CALIDAD DEL SINCRONISMO DE LINEA,
 * MEDIDA EN COLUMNAS.
 *
 * POR QUE EXISTE.
 *
 * "El sincronismo podria ir mejor" no es una medida. Lo que se ve en la foto
 * cuando el sincronismo baila es que las lineas no quedan alineadas: un
 * borde vertical de la imagen sale como una escalera. Eso SI se puede
 * contar.
 *
 * El patron del generador tiene un borde vertical limpio en x=60 - la franja
 * roja acaba y empieza la verde -. Como la señal se fabrica con el mismo
 * plan con el que se lee, ese borde TIENE que caer en la misma columna en
 * todas las lineas. Lo que se desvie es, exactamente, el error de fase del
 * sincronismo de esa linea convertido a columnas.
 *
 * Se mide por el cruce por cero de (verde - rojo) entre dos columnas, con
 * interpolacion lineal, asi que la resolucion no es de una columna entera:
 * un desplazamiento de un tercio de columna se ve.
 *
 * LO QUE SE PUBLICA ES LA DISPERSION, NO EL DESVIO MEDIO. Que la imagen
 * entera salga corrida media columna no es un fallo de sincronismo sino una
 * constante del muestreador; lo que estropea la foto es que cada linea salga
 * corrida DISTINTO. Por eso el numero que manda es la desviacion respecto a
 * la media, y el maximo.
 *
 * NO LLEVA VALORES ESPERADOS: es una herramienta de medir, no una
 * comprobacion, y por eso no cuelga de `make comprueba`.
 *
 *   make fase
 */
#include "sstv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS 12000.0
#include "sstv_genera.h"

/* El pozo de RAM prestada: copiado de hfdl_ram.h, no escrito de memoria.
 * Ver el comentario de sim/sstvaire.c. */
static unsigned char g_ram_pozo[0xD4A0];
void           hfdl_ram_coge(void) { }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { }

/*
 * Donde cae el borde rojo->verde de la linea `y`, en columnas, o -1 si en
 * esa linea no hay borde que medir.
 *
 * Las lineas blancas del patron (y % 32 < 4) no tienen borde: son blancas
 * enteras. Hay que saltarlas o se mide ruido.
 */
static double borde(int y)
{
    int x, mejor = -1;
    double dmax = 0.0;

    if ((y % 32) < 4)            { return -1.0; }
    if (y >= 120 && y < 136)     { return -1.0; }   /* la banda de grises */

    /* El salto mas grande de (verde - rojo) en la zona del borde. */
    for (x = 40; x < 85; x++) {
        double a = (double)g_img[y][x][1]     - (double)g_img[y][x][0];
        double b = (double)g_img[y][x + 1][1] - (double)g_img[y][x + 1][0];
        if ((b - a) > dmax) { dmax = b - a; mejor = x; }
    }
    if (mejor < 0 || dmax < 60.0) { return -1.0; }

    /* Y el cruce por cero entre esas dos columnas, interpolado. */
    {
        double a = (double)g_img[y][mejor][1]     - (double)g_img[y][mejor][0];
        double b = (double)g_img[y][mejor + 1][1] - (double)g_img[y][mejor + 1][0];
        if (b == a) { return (double)mejor + 0.5; }
        return (double)mejor + (0.0 - a) / (b - a);
    }
}

static void mide(uint8_t modo, int lineas, double sigma, double ppm)
{
    tx_t t;
    sstv_info_t in;
    sstv_plan_t pl;
    double v[SSTV_ALTO];
    int n = 0, y;
    double media = 0.0, rms = 0.0, peor = 0.0;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL; t.ppm = ppm; t.sigma = sigma;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1;
    g_n = 0; g_t = 0.0;

    emite_reinicia();
    sstv_start((float)FS);
    cabecera_vis(&t, sstv_modo_vis(modo));
    sstv_plan(modo, &pl);
    for (y = 0; y < lineas; y += pl.lineas_img) { linea(&t, modo, y); }
    sstv_info(&in);

    for (y = 2; y <= g_max_y; y++) {
        double b = borde(y * (int)pl.vdec);
        if (b > 0.0) { v[n++] = b; }
    }
    sstv_stop();

    if (n < 8) {
        printf("    %-10s ruido %.2f  ppm %+5.0f   NO HAY LINEAS QUE MEDIR (%d)\n",
               sstv_modo_nombre(modo), sigma, ppm, n);
        return;
    }

    for (y = 0; y < n; y++) { media += v[y]; }
    media /= (double)n;
    for (y = 0; y < n; y++) {
        double d = v[y] - media;
        rms += d * d;
        if (fabs(d) > peor) { peor = fabs(d); }
    }
    rms = sqrt(rms / (double)n);

    printf("    %-10s ruido %.2f  ppm %+5.0f   lineas %3d   borde medio %6.2f   "
           "baile %.3f col (peor %.2f)   sincs malos %u\n",
           sstv_modo_nombre(modo), sigma, ppm, n, media, rms, peor,
           (unsigned)in.sincs_malos);
}

int main(void)
{
    static const double k_sig[] = { 0.0, 0.10, 0.25, 0.50, 1.00 };
    unsigned i;

    printf("BAILE DEL BORDE IZQUIERDO (columnas de 320). Menos es mejor.\n\n");

    printf("1. Martin 1, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(0U, 128, k_sig[i], 0.0); }

    printf("\n2. Scottie 1, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(2U, 128, k_sig[i], 0.0); }

    printf("\n3. PD120, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(9U, 128, k_sig[i], 0.0); }

    printf("\n4. Martin 1 con el reloj del transmisor desviado\n");
    mide(0U, 128, 0.25, -500.0);
    mide(0U, 128, 0.25,  500.0);

    return 0;
}

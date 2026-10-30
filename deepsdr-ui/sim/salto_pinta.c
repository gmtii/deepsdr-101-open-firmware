/*
 * ======================================================================
 * LO QUE CUESTA PINTAR UN SALTO - 15/10/2026
 * ======================================================================
 *
 * Asi que el coste tiene que estar en otro sitio, y la sospecha es el
 * REPINTADO. spectrum_draw() se salta las bandas de 16 filas cuya traza no
 * ha cambiado (s_parcial_hay / s_saltadas). Con un paso de sintonia de
 * 1 kHz la traza casi no se mueve y se salta casi todo; con un salto de
 * emisora el dial se va kilohercios de golpe, la traza cambia entera y no
 * se salta nada.
 *
 * Esto lo MIDE, con el User/spectrum.c de verdad: pinta un cuadro, luego
 * otro con la traza movida poco, y otro con la traza movida mucho, y
 * pregunta cuantas bandas se ha saltado cada uno.
 *
 * Y LA SOSPECHA ERA FALSA. El banco la mato a la primera, y el porque esta
 * en el propio spectrum.c: una banda se salta cuando NINGUN tramo pinta
 * nada en sus filas, o sea solo el cielo que queda POR ENCIMA del pico mas
 * alto. No se compara la traza de este cuadro con la del anterior en
 * ningun sitio. Asi que lo que se ahorra depende de lo ALTAS que sean las
 * señales, no de cuanto se haya movido el dial: un salto de veinte
 * kilohercios cuesta de pintar exactamente lo mismo que un paso de uno.
 *
 * Esto se queda escrito y midiendose porque un negativo medido vale: la
 * proxima vez que alguien -yo el primero- culpe al repintado de que la
 * cascada va lenta al sintonizar, aqui esta el numero que dice que no.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-52s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

#define BINS  1024
#define ANCHO 756
#define ALTO  208

static float g_db[BINS];

/* Un panorama con unas cuantas emisoras, desplazado `corr` bins. */
static void panorama(int corr)
{
    int i, k;
    for (i = 0; i < BINS; i++) { g_db[i] = -110.0f + 3.0f * sinf((float)i * 0.11f); }
    for (k = 0; k < 9; k++) {
        int c = ((k * 113 + 37) + corr) % BINS;
        int w;
        for (w = -3; w <= 3; w++) {
            int x = c + w;
            if (x < 0 || x >= BINS) { continue; }
            g_db[x] = -40.0f + 4.0f * (float)(w < 0 ? -w : w);
        }
    }
}

static uint32_t pinta(void)
{
    uint32_t salt = 0U, tot = 0U;
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);
    spectrum_parcial_diag(&salt, &tot);
    (void)tot;
    return salt;
}

int main(void)
{
    uint32_t s_igual, s_poco, s_mucho, tot = 0U;

    printf("\n== LO QUE CUESTA PINTAR UN SALTO ==\n\n");
    lcd_sim_reset(0x0000);

    /* Dos cuadros iguales para que el parcial se active (hace falta un
     * cuadro anterior con el que comparar). */
    panorama(0); (void)pinta();
    panorama(0); s_igual = pinta();
    spectrum_parcial_diag(0, &tot);

    /* Un paso de sintonia de 1 kHz a 96 kHz de ventana son ~11 bins de
     * 1024... pero lo que mueve la traza es la SEÑAL, no la ventana: con
     * 1 kHz las emisoras se corren un pelin. */
    panorama(0); (void)pinta();
    panorama(1); s_poco = pinta();

    /* Un salto de emisora son kilohercios: la traza cambia entera. */
    panorama(0); (void)pinta();
    panorama(60); s_mucho = pinta();

    printf("  bandas de 16 filas en la traza: %u\n\n", (unsigned)tot);
    printf("    %-40s %u de %u\n", "cuadro igual, bandas saltadas",
           (unsigned)s_igual, (unsigned)tot);
    printf("    %-40s %u de %u\n", "la traza se mueve un poco (1 kHz)",
           (unsigned)s_poco, (unsigned)tot);
    printf("    %-40s %u de %u\n", "la traza se mueve mucho (un salto)",
           (unsigned)s_mucho, (unsigned)tot);
    printf("\n");

    di(s_igual > 0U && s_igual < tot,
       "se salta el cielo de encima de las señales, y solo eso",
       "si se saltara todo o nada, el parcial no estaria haciendo su trabajo");
    /*
     * ==================================================================
     * AQUI SE EXIGIA IGUALDAD EXACTA, Y SE AFLOJA A UNA BANDA - 30/10/2026.
     * ==================================================================
     *
     * `s_poco == s_mucho` se cumplia -9 y 9 de 52- mientras el contorno
     * venia de fabrica con TRES pasadas de suavizado: tres pasadas
     * ensanchan los picos lo bastante como para que el borde del cielo
     * caiga en la misma banda de 16 filas con la traza movida un bin o
     * movida sesenta. Bajado el contorno a UNA pasada -30/10/2026, pedido
     * por el usuario- la traza es mas afilada y el borde del cielo cae una
     * banda mas arriba en uno de los dos casos: 8 y 7.
     *
     * LO QUE ESTE BANCO DEFIENDE no es la igualdad al bit, es que el
     * ahorro NO SEA PROPORCIONAL al salto del dial, porque esa era la
     * hipotesis que mato: si lo fuera, el repintado explicaria que la
     * cascada vaya lenta al saltar de emisora. Y sigue muerta, con el
     * numero delante: mover la traza SESENTA veces mas cuesta UNA banda
     * mas de 52, o sea un 2 %. Proporcional seria sesenta veces.
     *
     * Por eso lo que se exige ahora es eso mismo escrito como numero, y no
     * un "casi igual" a ojo.
     */
    {
        int dif = (int)s_poco - (int)s_mucho;
        char d[120];
        if (dif < 0) { dif = -dif; }
        snprintf(d, sizeof d,
                 "mover la traza 60 veces mas cuesta %d banda(s) de %u, "
                 "no 60 veces mas", dif, (unsigned)tot);
        printf("    %-52s %s\n", "cuanto depende del salto del dial", d);
        di(dif <= 1,
           "y saltarse bandas NO depende de cuanto se movio el dial",
           "si dependiera, el repintado si explicaria la cascada lenta al saltar");
    }
    {
        char d[110];
        snprintf(d, sizeof d,
                 "un salto repinta %u bandas y un paso %u",
                 (unsigned)(tot - s_mucho), (unsigned)(tot - s_poco));
        printf("    %-52s %s\n", "lo que cuesta de pintar cada uno", d);
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

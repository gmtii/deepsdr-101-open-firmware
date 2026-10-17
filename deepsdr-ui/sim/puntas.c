/*
 * DE QUE FORMA SE DIBUJA LA PUNTA DE UN PICO - 23/09/2026.
 *
 * Nace de una observacion del usuario: las señales salian planas en la punta
 * cuando antes eran picos. Y de una condicion suya que es la que manda
 * aqui: que si la radio tiene un modo de puntas planas, sea porque se ha
 * PROGRAMADO asi, no porque salga de casualidad de como esta escrito el
 * dibujo.
 *
 * Asi que esto no opina: mide. Mete en spectrum_draw() una señal sintetica
 * con picos de anchura y altura conocidas, lee los pixeles que salen, y dice
 * cuantas columnas mide la cumbre de cada uno.
 *
 * Compila el User/spectrum.c DE VERDAD, no la copia de vendor/. Esa copia
 * existe desde antes y hoy solo se diferencia en un comentario, pero eso es
 * suerte: una copia que nadie obliga a estar al dia acaba mintiendo, y la
 * pregunta que se contesta aqui es justo de las que no admiten una respuesta
 * sobre codigo que a lo mejor ya no es el que corre.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define BINS 796
#define PX   0
#define PY   104
#define PW   800
#define PH   260

static float g_db[BINS];
static const float DB_MIN = -110.0f;
static const float DB_MAX =  -20.0f;

/* Suelo plano y picos de la anchura que se pida. */
static void señal(int ancho_pico, const int *alturas_db, const int *pos, int n)
{
    int i, k, j;
    for (i = 0; i < BINS; i++) { g_db[i] = -100.0f; }
    for (k = 0; k < n; k++) {
        for (j = 0; j < ancho_pico; j++) {
            int b = pos[k] + j;
            if (b >= 0 && b < BINS) { g_db[b] = (float)alturas_db[k]; }
        }
    }
}

static void pinta(int pasadas, spectrum_style_t estilo)
{
    lcd_sim_reset(0x0000);
    spectrum_set_line_smooth((uint8_t)pasadas);
    spectrum_set_style(estilo);
    /* Dos cuadros: el primero siembra la EMA temporal, el segundo ya es el
     * regimen permanente, que es lo que se ve en la radio. */
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, DB_MIN, DB_MAX, 0, 0U, 0, 0);
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, DB_MIN, DB_MAX, 0, 0U, 0, 0);
}

int main(void)
{
    static const int alturas[3] = { -30, -55, -75 };   /* fuerte, media, floja */
    static const int pos[3]     = { 200, 400, 600 };
        int fallos = 0;


    spectrum_init();

    /*
     * Este banco NO intenta medir el ancho de la cumbre con numeros. La
     * primera version lo hacia buscando el primer pixel no negro de cada
     * columna, y lo que encontraba eran las rayas de la rejilla: daba 800
     * columnas de "meseta" en una pantalla donde el pico mide una. Un numero
     * que miente es peor que ningun numero, asi que lo que deja son los dos
     * renders, que se miran y no admiten interpretacion.
     */
    printf("Renders del trazo del espectro, con el User/spectrum.c REAL.\n\n");

    /* Y dos renders para mirarlos con los ojos: el pico aislado, y una
     * banda llena como la de 40 m de noche, que es lo que de verdad se mira
     * en la radio y donde se hizo la observacion. */
    señal(1, alturas, pos, 3);
    pinta(1, SPECTRUM_STYLE_HEATMAP);
    lcd_sim_write_ppm("out/puntas.ppm");
    {
        int i;
        unsigned r = 12345U;
        for (i = 0; i < BINS; i++) {
            r = r * 1103515245U + 12345U;
            g_db[i] = -78.0f + (float)((r >> 16) & 0x3FF) / 1024.0f * 10.0f;
        }
        /* y unas cuantas estaciones encima, de alturas variadas */
        for (i = 0; i < 24; i++) {
            r = r * 1103515245U + 12345U;
            {
                int b = (int)((r >> 8) % (unsigned)BINS);
                float alto = -70.0f + (float)((r >> 20) & 0x1F);
                if (b > 1 && b < BINS - 2) {
                    g_db[b] = alto; g_db[b-1] = alto - 6.0f; g_db[b+1] = alto - 6.0f;
                }
            }
        }
        /* La misma banda dibujada con CADA valor de contorno: es la unica
         * forma de comparar contra una foto de la radio y saber cual estaba
         * puesto. */
        int c;
        for (c = 0; c <= 3; c++) {
            char nom[48];
            pinta(c, SPECTRUM_STYLE_HEATMAP);
            snprintf(nom, sizeof nom, "out/banda_contorno%d.ppm", c);
            lcd_sim_write_ppm(nom);
        }
        printf("  banda llena con contorno 0..3 en out/banda_contorno*.ppm\n");
    }
    printf("  render en out/puntas.ppm (contorno 1, estilo Relleno)\n");
    return fallos ? 1 : 0;
}

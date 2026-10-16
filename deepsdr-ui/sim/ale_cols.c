/*
 * QUE LA TABLA DE ALE CABE EN SUS COLUMNAS.
 *
 * Aqui el peor caso SI se puede acotar de verdad, al reves que en AIS: una
 * direccion de ALE son como mucho quince caracteres -cinco palabras de
 * tres- y el alfabeto no tiene minusculas, asi que quince "W" es el maximo
 * absoluto y no una exageracion defensiva.
 */
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

static int16_t hueco(uint8_t k)
{
    int16_t a = ui_digi_cols_ale[k];
    int16_t b = ui_digi_cols_ale[k + 1U];

    if (b < 0) { return (int16_t)((-b) - a); }
    return (int16_t)(b - a);
}

static void cabe(uint8_t col, const char *txt, const char *que)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);
    int16_t h = hueco(col);
    int ok = (w <= h);

    printf("    col %u  %-18s %4d px en %4d   %s   %s\n",
           col, txt, w, h, ok ? "ok " : "MAL", que);
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\nanchos de la tabla de ALE\n\n");

    cabe(0U, "Indicativo", "titulo");
    cabe(1U, "Tipo",       "titulo");
    cabe(2U, "kHz",        "titulo");
    cabe(3U, "Cal.",       "titulo");

    printf("\n");

    cabe(0U, "WWWWWWWWWWWWWWW", "quince caracteres, el tope del estandar");
    cabe(0U, "EA4XYZ",          "y uno normal, para comparar");
    /* Los cuatro preambulos que abren direccion, mas los de extension. */
    cabe(1U, "TWAS",            "el tipo mas ancho");
    cabe(1U, "THRU",            "otro de cuatro");
    /* La onda corta llega a 30 MHz. */
    cabe(2U, "29.999",          "la frecuencia mas ancha de HF");
    /* La calidad son los votos unanimes: de 0 a 48. */
    cabe(3U, "48",              "la calidad mas alta");

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

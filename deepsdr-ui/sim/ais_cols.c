/*
 * QUE LA TABLA DE AIS CABE EN SUS COLUMNAS.
 *
 * El mismo banco que sim/hfdl_cols.c y sim/wspr_cols.c. Aqui lo que hay que
 * vigilar es el NOMBRE DEL BARCO: son veinte caracteres libres, elegidos
 * por quien registro el barco, y no hay forma de saber cual va a pasar por
 * delante. Se dimensiona al peor caso ABSOLUTO -veinte "W"- y no a uno
 * plausible, que es lo contrario de lo que hubo que hacer en HFDL: alli no
 * sobraba sitio y hubo que ir a medir los 356 nombres reales del fichero.
 * Aqui sobra, asi que se reserva de mas y no se vuelve a pensar en ello.
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
    int16_t a = ui_digi_cols_ais[k];
    int16_t b = ui_digi_cols_ais[k + 1U];

    if (b < 0) { return (int16_t)((-b) - a); }
    return (int16_t)(b - a);
}

static void cabe(uint8_t col, const char *txt, const char *que)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);
    int16_t h = hueco(col);
    int ok = (w <= h);

    printf("    col %u  %-22s %4d px en %4d   %s   %s\n",
           col, txt, w, h, ok ? "ok " : "MAL", que);
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\nanchos de la tabla de AIS\n\n");

    cabe(0U, "MMSI",     "titulo");
    cabe(1U, "Nombre",   "titulo");
    cabe(2U, "Posición", "titulo");
    cabe(3U, "Vel",      "titulo");
    cabe(4U, "Rumbo",    "titulo");

    printf("\n");

    /* Un MMSI son NUEVE cifras siempre: los tres primeros son el pais. */
    cabe(0U, "224123456",            "el MMSI, nueve cifras");
    /* El peor nombre posible, no el mas probable. */
    cabe(1U, "WWWWWWWWWWWWWWWWWWWW", "veinte caracteres anchos");
    cabe(1U, "MAERSK ESSEX",         "y uno de verdad, para comparar");
    /* Latitud hasta 90 y longitud hasta 180, con tres decimales y letra. */
    cabe(2U, "43,371N 179,999W",     "la posicion mas ancha");
    /* La velocidad de AIS llega a 102,2 nudos (1022 = el tope del campo). */
    cabe(3U, "102,2",                "la velocidad mas alta");
    /* El rumbo, hasta 359 grados. */
    cabe(4U, "359o",                 "el rumbo mas ancho");

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

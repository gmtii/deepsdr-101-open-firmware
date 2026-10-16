/*
 * QUE LA TABLA DE HFDL CABE EN SUS COLUMNAS.
 *
 * POR QUE EXISTE (25/09/2026). Porque los anchos de columna se ponen a
 * ojo y se comprueban mirando la pantalla, y mirando la pantalla solo se
 * ve el caso que haya pasado ese dia. El nombre mas largo que genera
 * tools/icao24.py es "B77W Boeing 777-300ER", y ese solo sale cuando se
 * cruza un 777 - o sea que la columna puede estar corta durante semanas
 * sin que se note.
 *
 * Aqui se mide con gfx2_text_w() y la MISMA fuente con la que se dibuja,
 * asi que lo que diga esto es lo que va a pasar en el cristal.
 *
 * El caso que lo motivo: Pedido por el usuario.. Al darle columna propia al modelo hubo que
 * recolocar las cinco que ya habia, y recolocar a ojo cinco columnas es
 * exactamente como se rompe la de al lado sin enterarse.
 */
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

/* El ancho util de la columna k: hasta donde empieza la siguiente. La
 * ultima es negativa -pegada a la derecha- y se mide al reves. */
static int16_t hueco(uint8_t k)
{
    int16_t a = ui_digi_cols_hfdl[k];
    int16_t b = ui_digi_cols_hfdl[k + 1U];

    if (b < 0) { return (int16_t)((-b) - a); }
    return (int16_t)(b - a);
}

static void cabe(uint8_t col, const char *txt, const char *que)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);
    int16_t h = hueco(col);
    int ok = (w <= h);

    printf("    col %u  %-24s %4d px en %4d   %s   %s\n",
           col, txt, w, h, ok ? "ok " : "MAL", que);
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\n  COLUMNAS DE LA TABLA DE AVIONES (HFDL)\n\n");

    /* Los titulos, que tambien ocupan. */
    cabe(0U, "OACI",     "titulo");
    cabe(1U, "Modelo",   "titulo");
    cabe(2U, "Vuelo",    "titulo");
    cabe(3U, "Posición", "titulo");

    /* Y el peor contenido de cada una. */
    cabe(0U, "717C9C", "una direccion OACI");
    cabe(0U, "ID 255", "o el identificador, cuando no se sabe");

    /* Los nombres mas largos que puede escribir tools/icao24.py: la
     * entrada mide 24 bytes con el NUL, o sea 23 caracteres de tope. */
    /*
     * EL PEOR DE LOS 356 DE VERDAD, no el peor que se me ocurra. Salieron
     * midiendo uno a uno los nombres del ICAO24.BIN generado del CSV real
     * de OpenSky. La primera version de este banco ponia
     * "B77W Boeing 777-300ER" (185 px) como el peor y la columna se
     * dimensiono para el; el fichero tenia dentro uno de 218 px. Habria
     * salido cortado el dia que pasara un Nomad por encima, o sea nunca
     * hasta que pasara.
     */
    cabe(1U, "NOMA Government Nomad N", "el mas ancho de los 356 del fichero");
    cabe(1U, "B77W Boeing 777-300ER",   "uno de los que de verdad se cruzan");
    cabe(1U, "A359 Airbus A350-941",    "otro de los gordos");
    cabe(1U, "B734 Boeing 737-4Q8(SF)", "el de mas caracteres: 23, el tope");
    /*
     * Y NO se exige que quepa cualquier cadena de 23 caracteres: "WWWWW
     * WWWWWWWWWWWWWWWWW" mide 335 px y no cabria en ninguna columna
     * razonable. No hace falta que quepa, porque ui_digi.c RECORTA cada
     * campo a su columna -ver el comentario "RECORTADO A SU COLUMNA"-, asi
     * que un nombre largo sale cortado y nunca se come la columna de al
     * lado. Lo que se mide aqui es lo otro: que los nombres que de verdad
     * escribe tools/icao24.py quepan ENTEROS, que es lo que decide si el
     * usuario ve "Boeing 777-300ER" o "Boeing 777-3".
     */

    cabe(2U, "ABY259", "un distintivo de vuelo");

    cabe(3U, "50,5N 4,4E",   "una posicion corriente");
    /* La mas larga de verdad. El signo NO va delante: fila_avion() lo
     * pone como letra detras (N/S, E/W), asi que "89,9S 179,9W" es el
     * tope y no "-89,9S -179,9W". */
    cabe(3U, "89,9S 179,9W", "la posicion mas larga posible");

    /* La del evento se queda con el resto, pero la estacion se pinta en
     * su titulo y esa puede ser larga. */
    cabe(4U, "> LOGON DLS",     "el evento mas largo");
    cabe(4U, "> SAN FRANCISCO", "la estacion de nombre mas largo");

    /*
     * EL BOTON DE "LA SIGUIENTE FRECUENCIA" (25/09/2026).
     *
     * Pedido por el usuario.
     *
     * Su rotulo es la frecuencia en la que estas, y la mas ancha de las 21
     * de la lista es "21.955". El boton se topa a 150 px y pinta_uno() se
     * queda 34 de aire, asi que lo que le queda al texto son 116.
     */
    {
        int16_t w = gfx2_text_w("21.955", &font_ui_14b);
        int ok = (w <= 150 - 34);

        printf("\n    boton \"la siguiente\": \"21.955\" mide %d px de los %d "
               "que le quedan   %s\n", w, 150 - 34, ok ? "ok" : "MAL");
        if (!ok) { fallos++; }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

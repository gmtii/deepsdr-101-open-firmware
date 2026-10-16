/*
 * QUE LA TABLA DE WSPR CABE EN SUS COLUMNAS.
 *
 * El mismo banco que sim/hfdl_cols.c y por la misma razon: los anchos se
 * ponen a ojo y se comprueban mirando la pantalla, y mirando la pantalla
 * solo se ve el caso que haya pasado ese dia. En WSPR eso es peor todavia
 * que en HFDL, porque aqui llega UNA linea cada dos minutos como mucho: una
 * columna corta puede estar corta durante horas sin que salga el dato que
 * la desborda.
 *
 * Se mide con gfx2_text_w() y la MISMA fuente con la que se dibuja, asi que
 * lo que diga esto es lo que va a pasar en el cristal.
 *
 * Y LOS TITULOS TAMBIEN SE MIDEN, no solo los datos. En esta tabla los
 * datos son cortos -"60", "RR99", "100"- y los titulos no: "Indicativo"
 * mide 76 px y "Desvío" mide mas que cualquier desvio que pueda salir. La
 * columna la manda el mas ancho de los dos, y dar por hecho que es el dato
 * es justo como se corta una cabecera.
 *
 * EL CASO QUE LO MOTIVO: el titulo del desvio iba a ser "ΔHz". Mide 24 px
 * y "Hz" mide 19, o sea que la delta ocupa CINCO: no esta en la fuente y lo
 * que se habria dibujado no es una delta. Eso no lo caza un banco de
 * anchos -cabe de sobra- pero si lo caza MEDIR en vez de suponer, que es lo
 * que hay que hacer aqui siempre.
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
    int16_t a = ui_digi_cols_wspr[k];
    int16_t b = ui_digi_cols_wspr[k + 1U];

    if (b < 0) { return (int16_t)((-b) - a); }
    return (int16_t)(b - a);
}

static void cabe(uint8_t col, const char *txt, const char *que)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);
    int16_t h = hueco(col);
    int ok = (w <= h);

    printf("    col %u  %-12s %4d px en %4d   %s   %s\n",
           col, txt, w, h, ok ? "ok " : "MAL", que);
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\nanchos de la tabla de WSPR\n\n");

    /* Los titulos, que en esta tabla son casi siempre lo mas ancho. */
    cabe(0U, "Hora",       "titulo");
    cabe(1U, "Indicativo", "titulo");
    cabe(2U, "Loc.",       "titulo");
    cabe(3U, "dBm",        "titulo");
    cabe(4U, "Desvío",     "titulo");

    printf("\n");

    /* Y los datos, en su peor caso. */
    cabe(0U, "23:58",      "la hora mas ancha");
    /*
     * Siete caracteres anchos. wspr_msg_t guarda ocho bytes de indicativo
     * y los mensajes de tipo 2 llevan prefijo o sufijo ("F/EA4XYZ"), asi
     * que seis no es el tope: siete si.
     */
    cabe(1U, "MM0WWWW",    "indicativo de 7, todo ancho");
    cabe(1U, "F/EA4XYZ",   "con prefijo de pais");
    /* Localizador: dos letras (A-R) y dos cifras. Las mas anchas, RR. */
    cabe(2U, "RR99",       "el localizador mas ancho");
    /* La potencia va en dBm y el tope del formato de WSPR es 60. */
    cabe(3U, "60",         "la potencia mas alta");
    /*
     * El desvio respecto del centro de la ventana. La ventana son 200 Hz,
     * o sea +-100 desde el centro; se prueba con 150 para que sobre.
     */
    cabe(4U, "-150,0",     "el desvio mas ancho");

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

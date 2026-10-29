/*
 * QUE EL RENGLON DE DETALLE DE LA HORA POR RADIO QUEPA EN LOS DOS IDIOMAS.
 *
 * *** El usuario, 29/10/2026: que revise que todas las palabras cambien de
 * castellano a ingles al cambiar el idioma. ***
 *
 * Al traducir los rotulos de esa pantalla -"marca", "ciclo", "raras",
 * "mal", "codigo" y los siete motivos de MSF- habia que contestar a una
 * pregunta que el comentario de main.c deja planteada: esos rotulos son
 * CORTOS A PROPOSITO, porque tienen que caber al lado de dos numeros. Una
 * traduccion mas larga los sacaria del hueco.
 *
 * La respuesta se midio una vez y salio bien -el ingles es mas estrecho en
 * los cuatro peores casos- pero una medida que se hace una vez y se escribe
 * en un documento se pudre igual que un comentario. Esta tarde ya ha
 * costado una version: tema_aplicar() llamaba a freq_display_draw() con
 * "la cabecera entera" al lado, y esa funcion habia dejado de pintarla
 * entera veinticuatro dias antes.
 *
 * Asi que la medida vive aqui, se rehace en cada tanda, y falla si alguien
 * alarga una traduccion.
 *
 * LA FUENTE ES LA DE VERDAD: font_ui_14, que es con la que ui_hora.c pinta
 * el renglon de detalle. Medir con otra seria medir otra cosa.
 *
 * EL LIMITE. El renglon se pinta centrado en los 800 px de ancho de la
 * pantalla (ver ui_hora.c, gfx2_text_in con ancho 800). Se deja un margen
 * de 20 px a cada lado, que es lo que hace que el texto no bese el borde:
 * 760 px utiles.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

#define UTIL 760

static int fallos;

static void mide(const char *que, const char *es, const char *en)
{
    int wes = gfx2_text_w(es, &font_ui_14);
    int wen = gfx2_text_w(en, &font_ui_14);

    printf("    %-26s  es %4d px   en %4d px   %s\n", que, wes, wen,
           (wen <= UTIL) ? "cabe" : "*** NO CABE ***");
    if (wen > UTIL) { fallos++; }
    if (wes > UTIL) {
        printf("      *** y el castellano TAMPOCO cabe ***\n");
        fallos++;
    }
}

int main(void)
{
    printf("\n  EL RENGLON DE DETALLE DE LA HORA POR RADIO\n");
    printf("  hueco util: %d px de los 800 (20 de margen a cada lado)\n\n",
           UTIL);

    /*
     * Los peores casos de cada rama de hora_poll(), armados igual que los
     * arma ella: dos cifras, barra, 59, y los rotulos con sus numeros al
     * maximo. El motivo mas largo de los siete va el ultimo.
     */
    mide("LEYENDO, con motivo",
         "59/59 marca 200 ciclo 1000 mal 999 par-anno",
         "59/59 mark 200 cycle 1000 bad 999 par-year");
    mide("LEYENDO, con motivo largo",
         "59/59 marca 200 ciclo 1000 mal 999 patron",
         "59/59 mark 200 cycle 1000 bad 999 pattern");
    mide("LEYENDO, sin ninguna mala",
         "59/59 marca 200 ciclo 1000 raras 999",
         "59/59 mark 200 cycle 1000 odd 999");
    mide("BUSCANDO",
         "9,99 de marca",
         "9,99 of mark");
    mide("RDS sin nombre de cadena",
         "codigo E2A1 - la hora se manda una vez por minuto",
         "code E2A1 - the time is sent once a minute");

    printf("\n");
    if (fallos) {
        printf("  *** %d renglon(es) se salen del hueco ***\n", fallos);
        printf("  Acorta la traduccion: esos rotulos van al lado de dos\n");
        printf("  numeros y el hueco no crece.\n\n");
        return 1;
    }
    printf("  los cinco renglones caben en los dos idiomas  ok\n\n");
    return 0;
}

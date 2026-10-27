/*
 * CUANTO SE SALE CADA GLIFO DE SU RENGLON.
 *
 * POR QUE EXISTE - 24/10/2026
 * ---------------------------
 * En la revision a fondo pedida por el usuario se le puso a gfx2_text_in()
 * la guarda de banda que ya tenian las demas primitivas de gfx2: si la
 * banda que se esta componiendo no toca el renglon, se vuelve sin medir el
 * texto. Eso ahorra recorrer la cadena entera en las treinta y pico bandas
 * que no la tocan.
 *
 * Pero una guarda de recorte es una afirmacion sobre GEOMETRIA: que ningun
 * glifo pinta fuera de [y - 2, y + line_h + 2). Si algun dia entra una
 * fuente con un descendente mas largo -una 'g' o una 'j' que baje mas- o
 * con tilde alta, la guarda empezaria a cortar letras en el borde de banda
 * y NADIE SE ENTERARIA: lo que se ve es media letra una vez cada ocho
 * filas, que parece un fallo de la pantalla.
 *
 * Asi que el margen se mide sobre las fuentes de verdad en vez de darlo por
 * bueno. Lo medido hoy, con las siete del arbol:
 *
 *     fuente         line_h   by minimo   by+h maximo
 *     font_num_20      25          4           23
 *     font_num_44      54          8           50
 *     font_rain_16     18          0           17
 *     font_ui_11       14          0           14
 *     font_ui_14       18         -1           18
 *     font_ui_14b      18         -1           18
 *     font_ui_18b      23          0           23
 *
 * O sea que lo mas que se sale cualquier glifo es UN pixel por arriba
 * -font_ui_14 y su negrita- y ninguno por abajo. La guarda deja dos por
 * cada lado, asi que sobra uno arriba y dos abajo.
 *
 * LO QUE ESTE BANCO EXIGE: ese margen. No que las fuentes no cambien.
 */
#include "gfx2.h"
#include <stdio.h>

/* El aire que gfx2_text_in() deja por cada lado del renglon. Si se toca
 * alli, se toca aqui, y entonces este banco dice si sigue cabiendo. */
#define AIRE 2

extern const gfx2_font_t font_num_20, font_num_44, font_rain_16,
                         font_ui_11, font_ui_14, font_ui_14b, font_ui_18b;

static int fallos = 0;

static void mira(const char *nom, const gfx2_font_t *f)
{
    int i, n = 0, arriba = 9999, abajo = -9999;
    int ok;

    for (i = 0; i < (int)f->n_ranges; i++) {
        unsigned cp;
        for (cp = f->ranges[i].lo; cp <= f->ranges[i].hi; cp++) {
            const gfx2_glyph_t *g =
                &f->glyphs[f->ranges[i].first + (cp - f->ranges[i].lo)];
            if (g->h == 0U) { continue; }    /* el espacio no pinta */
            if (g->by < arriba) { arriba = g->by; }
            if (g->by + (int)g->h > abajo) { abajo = g->by + (int)g->h; }
            n++;
        }
    }
    if (n == 0) {
        printf("    %-14s *** MAL: no tiene glifos con tinta ***\n", nom);
        fallos++;
        return;
    }
    ok = (arriba >= -AIRE) && (abajo <= (int)f->line_h + AIRE);
    printf("    %-14s line_h %2d  %4d glifos   se sale %d arriba, %d abajo"
           "   %s\n",
           nom, (int)f->line_h, n,
           (arriba < 0) ? -arriba : 0,
           (abajo > (int)f->line_h) ? abajo - (int)f->line_h : 0,
           ok ? "ok" : "*** MAL ***");
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\n== El renglon de las fuentes, con %d px de aire por lado ==\n\n",
           AIRE);
    mira("font_num_20",  &font_num_20);
    mira("font_num_44",  &font_num_44);
    mira("font_rain_16", &font_rain_16);
    mira("font_ui_11",   &font_ui_11);
    mira("font_ui_14",   &font_ui_14);
    mira("font_ui_14b",  &font_ui_14b);
    mira("font_ui_18b",  &font_ui_18b);

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS: la guarda de banda de"
                                  " gfx2_text_in() cortaria texto ***"
                                : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

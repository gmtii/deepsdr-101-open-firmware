/*
 * ======================================================================
 * LA PASTILLA DE BANDA, CUANDO LA ETIQUETA CAMBIA DE ANCHO
 * ======================================================================
 *
 * ui_top_draw_freq() -el repintado barato, el que corre en cada clic del
 * mando- recorta en rds_x1(), y rds_x1() sale de s_band_x2, que es una
 * variable que draw_header() rellena MIENTRAS PINTA. Antes de pintar vale
 * lo de la vez anterior, asi que al pasar a una etiqueta mas ancha la
 * pastilla nueva no cabia en la ventana calculada con la vieja.
 *
 * ESTE BANCO MIRA LOS PIXELES, no las cuentas: pinta la cabecera con una
 * etiqueta corta, cambia a una larga, repinta SOLO la frecuencia -que es
 * lo que hace la radio- y mide hasta donde llega lo dibujado. Si lo que
 * se ve es mas estrecho que lo que la pastilla pide, esta cortada.
 *
 * Y mide los dos sentidos, porque el que encoge tiene su propio riesgo:
 * una pastilla que se estrecha puede dejar COLA a la derecha -trozos de
 * la anterior que ya no los borra nadie-, que es exactamente el fallo que
 * ui_digi.c documenta con su chapa.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void ver(const char *q, int ok)
{
    printf("    %-54s %s\n", q, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

/*
 * Hasta donde llega EL BORDE DE LA PASTILLA, buscando su color.
 *
 * No vale medir "lo que no es fondo": la cabecera lleva un degradado de
 * 800 px de ancho, asi que con ese criterio todo esta dibujado siempre y
 * la medida sale 799 pase lo que pase. Hay que buscar el color del borde,
 * que es el unico que esta ahi por la pastilla y por nada mas.
 */
static int borde_derecho(uint32_t color)
{
    const uint16_t *fb = lcd_sim_fb();
    gfx2_rgba_t c = gfx2_rgb(color);
    uint16_t buscado = gfx2_to565(c.r, c.g, c.b);
    int x, y, ult = -1;

    for (x = 0; x < GFX2_W; x++) {
        for (y = 14; y <= 45; y++) {
            if (fb[(size_t)y * GFX2_W + x] == buscado) { ult = x; break; }
        }
    }
    return ult;
}

static void pinta(ui_top_state_t *st, const char *banda, uint8_t fam, int solo_frec)
{
    st->band = banda; st->en_banda = 1U; st->fam_banda = fam;
    if (solo_frec) { ui_top_draw_freq(st); } else { ui_top_draw(st); }
}

int main(void)
{
    ui_top_state_t st;
    int ancho_corta, ancho_larga, ancho_tras_cambio, ancho_tras_encoger;

    puts("");
    puts("LA PASTILLA DE BANDA AL CAMBIAR DE ANCHO LA ETIQUETA");
    puts("");

    memset(&st, 0, sizeof st);
    st.freq = "14.230,00"; st.mode = "USB";

    /* Cuanto ocupa cada una cuando se pinta la cabecera ENTERA, que es la
     * referencia: asi es como tienen que verse. */
    pinta(&st, "20 m", 0U, 0);     ancho_corta = borde_derecho(PAL_OK);
    pinta(&st, "SSTV 20", 2U, 0);  ancho_larga = borde_derecho(PAL_ACCENT);
    printf("  con la cabecera entera:  \"20 m\" llega a x=%d, \"SSTV 20\" a x=%d\n\n",
           ancho_corta, ancho_larga);
    ver("la etiqueta larga ocupa mas que la corta", ancho_larga > ancho_corta);

    /* AHORA EL CASO DE VERDAD: estando en la corta, se cambia a la larga y
     * se repinta solo la frecuencia. */
    pinta(&st, "20 m", 0U, 0);
    pinta(&st, "SSTV 20", 2U, 1);
    ancho_tras_cambio = borde_derecho(PAL_ACCENT);
    printf("\n  cambiando a \"SSTV 20\" y repintando solo la frecuencia:\n");
    printf("    se dibuja hasta x=%d   (tendria que ser %d)\n",
           ancho_tras_cambio, ancho_larga);
    ver("la pastilla NO sale cortada al ensancharse",
        ancho_tras_cambio >= ancho_larga);

    /* Y el sentido contrario: de la larga a la corta, sin dejar cola. */
    pinta(&st, "SSTV 20", 2U, 0);
    pinta(&st, "20 m", 0U, 1);
    ancho_tras_encoger = borde_derecho(PAL_OK);
    printf("\n  volviendo a \"20 m\" y repintando solo la frecuencia:\n");
    printf("    se dibuja hasta x=%d   (tendria que ser %d)\n",
           ancho_tras_encoger, ancho_corta);
    ver("al encoger no deja cola de la anterior",
        ancho_tras_encoger <= ancho_corta);

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

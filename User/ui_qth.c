#include "ui_qth.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"

/*
 * NI UNA CADENA EN ESTE FICHERO. El titulo, el pie, los rotulos de los
 * cuatro botones y las pistas de cada casilla llegan en ui_qth_state_t,
 * ya elegidos por main.c con tr(). Es la misma regla que ui_grid.c y
 * ui_cfg.c: el widget dibuja, no decide - y asi la pantalla sale en el
 * idioma puesto sin que este fichero sepa que hay dos.
 *
 * LA FUENTE DE LOS CARACTERES: font_ui_18b, y no es por gusto.
 *
 * No es font_num_44 porque la de numeros SOLO tiene cifras: las cuatro
 * letras del localizador salian como hueco en blanco. glyph_for() en
 * gfx2.c devuelve el espacio para cualquier codepoint fuera de rango, sin
 * fallar ni avisar, asi que en la radio habria sido "las letras no se
 * ven" sin ninguna pista de por que. Se vio al hacer la maqueta.
 *
 * Y no es font_ui_24b, que es lo que pedia el diseno, porque esa fuente
 * NO ESTABA EN EL BINARIO: --gc-sections la tiraba entera por no usarla
 * nadie. Al usarla aqui volvia, y su mapa de bits son 16.956 bytes que
 * caen en el desvan (.arriba, ver el script de enlazado) - donde solo
 * quedaban unos 9 kB. El enlazado fallaba por 7.862 bytes exactos.
 * Diecisiete kilobytes de flash por seis caracteres no salen a cuenta;
 * 18b ya esta dentro y se lee perfectamente en una caja de 96x88.
 */

static int16_t btn_x(uint8_t i)
{
    switch (i) {
    case UIQ_HIT_MENOS:   return UIQ_PAD;
    case UIQ_HIT_MAS:     return (int16_t)(UIQ_PAD + UIQ_BTN_W + UIQ_BTN_GAP);
    case UIQ_HIT_APLICAR: return (int16_t)(GFX2_W - UIQ_PAD - 2 * UIQ_BTN_W - UIQ_BTN_GAP);
    default:              return (int16_t)(GFX2_W - UIQ_PAD - UIQ_BTN_W);
    }
}

static int16_t caja_x(uint8_t i)
{
    return (int16_t)(UIQ_CAJA_X0 + (int16_t)i * (UIQ_CAJA_W + UIQ_CAJA_GAP));
}

static void boton(gfx2_surf_t *s, uint8_t i, const char *rot,
                  uint8_t vivo, uint8_t acento, uint8_t hundido)
{
    int16_t x = btn_x(i);
    uint32_t fondo = hundido ? PAL_SURF_3 : (vivo ? PAL_SURF_1 : PAL_SURF_0);

    gfx2_rrect(s, x, UIQ_BTN_Y, UIQ_BTN_W, UIQ_BTN_H, 10, gfx2_rgb(fondo));
    gfx2_rrect_outline(s, x, UIQ_BTN_Y, UIQ_BTN_W, UIQ_BTN_H, 10, 2,
                       gfx2_rgb((vivo && acento) ? PAL_ACCENT : PAL_LINE));
    gfx2_text_in(s, x, (int16_t)(UIQ_BTN_Y + 18), UIQ_BTN_W, rot, &font_ui_18b,
                 gfx2_rgb(vivo ? PAL_INK : PAL_INK_MUTE), GFX2_ALIGN_C);
}

static void pinta(gfx2_surf_t *s, void *ctx)
{
    const ui_qth_state_t *st = (const ui_qth_state_t *)ctx;
    uint8_t i;

    gfx2_fill(s, 0, UIQ_Y, GFX2_W, UIQ_H, gfx2_rgb(PAL_SURF_0));

    gfx2_text(s, UIQ_PAD, (int16_t)(UIQ_Y + 12), st->titulo, &font_ui_18b,
              gfx2_rgb(PAL_INK));
    if (st->pie != 0) {
        gfx2_text_in(s, 0, (int16_t)(UIQ_Y + 20), (int16_t)(GFX2_W - UIQ_PAD),
                     st->pie, &font_ui_14, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_R);
    }

    for (i = 0U; i < UIQ_N; i++) {
        int16_t x = caja_x(i);
        uint8_t sel = (uint8_t)(i == st->sel);
        char c[2];

        gfx2_rrect(s, x, UIQ_CAJA_Y, UIQ_CAJA_W, UIQ_CAJA_H, 12,
                   gfx2_rgb(sel ? PAL_SURF_2 : PAL_SURF_1));
        gfx2_rrect_outline(s, x, UIQ_CAJA_Y, UIQ_CAJA_W, UIQ_CAJA_H, 12,
                           (int16_t)(sel ? 3 : 2),
                           gfx2_rgb(sel ? PAL_ACCENT : PAL_LINE));

        c[0] = st->loc[i];
        c[1] = '\0';
        gfx2_text_in(s, x, (int16_t)(UIQ_CAJA_Y + 28), UIQ_CAJA_W, c,
                     &font_ui_18b, gfx2_rgb(sel ? PAL_ACCENT : PAL_INK),
                     GFX2_ALIGN_C);
        if (st->pista[i] != 0) {
            gfx2_text_in(s, x, (int16_t)(UIQ_CAJA_Y + UIQ_CAJA_H - 26),
                         UIQ_CAJA_W, st->pista[i], &font_ui_14,
                         gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);
        }
    }

    if (st->coord != 0) {
        gfx2_text_in(s, 0, (int16_t)(UIQ_CAJA_Y + UIQ_CAJA_H + 16), GFX2_W,
                     st->coord, &font_ui_18b, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);
    }

    for (i = 0U; i < UIQ_BTN_N; i++) {
        /* "Aplicar" solo esta vivo si hay algo que aplicar: escribir la
         * flash para guardar lo mismo que ya hay es trabajo y desgaste
         * para nada, y un boton que siempre se puede pulsar no dice si has
         * cambiado algo. */
        uint8_t vivo   = (uint8_t)((i != UIQ_HIT_APLICAR) || st->cambiado);
        uint8_t acento = (uint8_t)(i == UIQ_HIT_APLICAR);
        boton(s, i, st->rot[i], vivo, acento,
              (uint8_t)(st->pressed == (int8_t)i));
    }
}

void ui_qth_draw(const ui_qth_state_t *st)
{
    gfx2_render(0, UIQ_Y, GFX2_W, UIQ_H, pinta, (void *)st);
}

int8_t ui_qth_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    /*
     * Las casillas primero. Se llevan una franja mas alta que la caja -18
     * px por arriba y por abajo- porque la caja mide 88 de alto pero el
     * objetivo comodo de dedo son 47 y aqui hay sitio de sobra; lo que
     * cuesta un toque fallado es tener que volver a apuntar.
     */
    if ((int16_t)y >= UIQ_CAJA_Y - 18 && (int16_t)y < UIQ_CAJA_Y + UIQ_CAJA_H + 18) {
        for (i = 0U; i < UIQ_N; i++) {
            if ((int16_t)x >= caja_x(i) && (int16_t)x < caja_x(i) + UIQ_CAJA_W) {
                return (int8_t)(UIQ_HIT_CAJA + i);
            }
        }
        return UIQ_HIT_NONE;
    }

    if ((int16_t)y < UIQ_BTN_Y - 16 || (int16_t)y >= UIQ_BTN_Y + UIQ_BTN_H + 16) {
        return UIQ_HIT_NONE;
    }
    for (i = 0U; i < UIQ_BTN_N; i++) {
        if ((int16_t)x >= btn_x(i) && (int16_t)x < btn_x(i) + UIQ_BTN_W) {
            return (int8_t)i;
        }
    }
    return UIQ_HIT_NONE;
}

#include "ui_hora.h"
#include "idioma.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_num_44.h"

/* Ver ui_hora.h para por que esta pantalla ensena dos barras y no un
 * "sincronizando..." a secas. */

#define BARRA_X    60
#define BARRA_W   (800 - 2 * BARRA_X)
#define BARRA_H    22
#define BARRA1_Y  (UIH_Y + 96)
#define BARRA2_Y  (BARRA1_Y + BARRA_H + 34)
#define ROT_W      68                 /* hueco del rotulo a la izquierda */

static void barra(gfx2_surf_t *s, int16_t y, const char *rotulo,
                  uint8_t pct, uint8_t encendida)
{
    int16_t x = (int16_t)(BARRA_X + ROT_W);
    int16_t w = (int16_t)(BARRA_W - ROT_W);
    int16_t llena;

    if (pct > 100U) { pct = 100U; }
    llena = (int16_t)(((int32_t)w * (int32_t)pct) / 100);

    gfx2_text_in(s, BARRA_X, (int16_t)(y + 3), ROT_W - 8, rotulo,
                 &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_R);

    gfx2_rrect(s, x, y, w, BARRA_H, 6, gfx2_rgb(PAL_SURF_2));
    if (llena > 4) {
        /* Encendida = el acento; apagada = un gris que se ve pero no llama.
         * Esa diferencia es el latido: con señal buena, la barra de marca se
         * enciende una vez por segundo y eso se reconoce de un vistazo desde
         * el otro lado de la mesa. */
        gfx2_rrect(s, x, y, llena, BARRA_H, 6,
                   encendida ? gfx2_rgb(PAL_ACCENT) : gfx2_rgba(PAL_ACCENT, 90));
    }
    gfx2_rrect_outline(s, x, y, w, BARRA_H, 6, 1, gfx2_rgb(PAL_LINE));
}

static void boton(gfx2_surf_t *s, int16_t x, const char *txt,
                  uint8_t vivo, uint8_t pulsado)
{
    gfx2_rgba_t fondo, borde, tinta;

    if (!vivo) {
        fondo = gfx2_rgb(PAL_SURF_1);
        borde = gfx2_rgba(PAL_LINE, 120);
        tinta = gfx2_rgba(PAL_INK_MUTE, 110);
    } else if (pulsado) {
        fondo = gfx2_rgb(PAL_ACCENT);
        borde = gfx2_rgb(PAL_ACCENT);
        tinta = gfx2_rgb(PAL_SURF_0);
    } else {
        fondo = gfx2_rgb(PAL_SURF_2);
        borde = gfx2_rgb(PAL_LINE);
        tinta = gfx2_rgb(PAL_INK);
    }
    gfx2_rrect(s, x, UIH_BTN_Y, UIH_BTN_W, UIH_BTN_H, 8, fondo);
    gfx2_rrect_outline(s, x, UIH_BTN_Y, UIH_BTN_W, UIH_BTN_H, 8, 1, borde);
    /* La fuente se elige por lo que MIDE el texto, no por el boton: el
     * rotulo de la emisora ("DCF77 - 77,5 kHz, Alemania") no cabe en 18b y
     * los otros dos si. Medir y decidir evita tener que acordarse. */
    {
        const gfx2_font_t *f = &font_ui_18b;
        if (gfx2_text_w(txt, f) > UIH_BTN_W - 12) { f = &font_ui_14b; }
        gfx2_text_in(s, x, (int16_t)(UIH_BTN_Y + (UIH_BTN_H - f->line_h) / 2),
                     UIH_BTN_W, txt, f, tinta, GFX2_ALIGN_C);
    }
}

static int16_t btn_x(uint8_t i)
{
    /* Tres repartidos a lo ancho: 2*10 + 3*240 + 2*35 = 810... no cabe, asi
     * que el hueco sale de la cuenta. 800 - 2*margen - 3*ancho, repartido en
     * los dos huecos. */
    const int16_t margen = 10;
    const int16_t hueco  = (int16_t)((800 - 2 * margen - UIH_BTN_N * UIH_BTN_W)
                                     / (UIH_BTN_N - 1));
    return (int16_t)(margen + i * (UIH_BTN_W + hueco));
}

static void draw_all(gfx2_surf_t *s, void *ctx)
{
    const ui_hora_state_t *st = (const ui_hora_state_t *)ctx;

    gfx2_fill(s, 0, UIH_Y, 800, UIH_H, gfx2_rgb(PAL_SURF_0));

    gfx2_text_in(s, 0, (int16_t)(UIH_Y + 14), 800, st->titulo,
                 &font_ui_18b, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);

    if (st->estado) {
        gfx2_text_in(s, 0, (int16_t)(UIH_Y + 48), 800, st->estado,
                     &font_ui_14b, gfx2_rgb(PAL_ACCENT), GFX2_ALIGN_C);
    }
    if (st->detalle) {
        gfx2_text_in(s, 0, (int16_t)(UIH_Y + 68), 800, st->detalle,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);
    }

    barra(s, BARRA1_Y, tr("Nivel", "Level"), st->nivel, 1U);
    barra(s, BARRA2_Y, tr("Marca", "Mark"), st->marca, st->latido);

    /*
     * La hora en grande y la fecha debajo, CENTRADAS en el hueco que queda
     * entre la segunda barra y los botones.
     *
     * *** Por el dueño, con una foto: "la fecha se superpone un poco con
     * el boton, subir un poco para arriba el conjunto hora fecha lo
     * arreglaria". *** Y se superponia, por cuatro pixeles:
     *
     *     hueco           278 .. 354   (76 px)
     *     hora  en +14    292 .. 346   (54 de alto)
     *     fecha en +62    340 .. 358   <- se metia 4 px en los botones
     *                                     y 6 px dentro de la propia hora
     *
     * Los dos desplazamientos eran numeros escritos a mano que cuadraban
     * con las fuentes de entonces. Ahora se calcula: el bloque mide lo que
     * midan las dos fuentes, y se centra en el hueco que haya. Si mañana
     * cambia una fuente, el tamaño de la pantalla o la altura de los
     * botones, esto sigue cuadrando solo en vez de volver a pisarse.
     *
     * El -2 de la fecha la junta un poco a la hora: font_num_44 lleva once
     * pixeles de descenso que los digitos no usan, y sin eso el hueco
     * entre las dos lineas se ve como un despiste.
     */
    if (st->hora) {
        int16_t y0 = (int16_t)(BARRA2_Y + BARRA_H);
        int16_t hueco = (int16_t)(UIH_BTN_Y - y0);
        int16_t alto  = (int16_t)(font_num_44.line_h
                                  + (st->fecha ? (font_ui_14.line_h - 2) : 0));

        if (hueco > alto) { y0 = (int16_t)(y0 + ((hueco - alto) / 2)); }

        gfx2_text_in(s, 0, y0, 800, st->hora,
                     &font_num_44, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        if (st->fecha) {
            gfx2_text_in(s, 0, (int16_t)(y0 + font_num_44.line_h - 2), 800,
                         st->fecha,
                         &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);
        }
    }

    boton(s, btn_x(0), st->emisora, 1U,
          (uint8_t)(st->pressed == UIH_HIT_EMISORA));
    boton(s, btn_x(1), tr("Aplicar", "Apply"), st->puede_aplicar,
          (uint8_t)(st->pressed == UIH_HIT_APLICAR));
    boton(s, btn_x(2), tr("Salir", "Exit"), 1U,
          (uint8_t)(st->pressed == UIH_HIT_SALIR));
}

/*
 * LA MISMA CAPA, EN UNA BANDA QUE LE DAN - 05/10/2026.
 *
 * *** El dueño: "quiero que las capturas funcionen tambien en los modos
 * digitales y en las pantallas de opciones". ***
 *
 * Es la MISMA funcion de dibujo que pinta en la pantalla, no una copia: lo
 * unico que cambia es de donde sale la banda. En la pantalla la pide
 * gfx2_render(); aqui la trae la captura, que va montando la pantalla
 * entera franja por franja porque no hay sitio en RAM para una copia.
 *
 * El recorte lo pone gfx2_sub_y(): la funcion de dibujo pinta su ventana
 * entera dando por hecho que quien le llama la ha recortado -eso es lo que
 * hace gfx2_render()-, asi que aqui hay que darselo igual. Sin esto se
 * sale por arriba y por abajo de su sitio.
 */
void ui_hora_pinta_en(gfx2_surf_t *s, const ui_hora_state_t *st)
{
    gfx2_surf_t sub = gfx2_sub_y(s, UIH_Y, UIH_H);
    if (sub.h > 0) { draw_all(&sub, (void *)st); }
}

void ui_hora_draw(const ui_hora_state_t *st)
{
    gfx2_render(0, UIH_Y, 800, UIH_H, draw_all, (void *)st);
}

int8_t ui_hora_hit(uint16_t x, uint16_t y)
{
    /* Los botones se llevan tambien el aire de arriba y de abajo: en esa
     * franja no hay nada mas que tocar. */
    uint8_t i;

    if (y < UIH_BTN_Y - 16 || y >= UIH_BTN_Y + UIH_BTN_H + 16) { return UIH_HIT_NONE; }
    for (i = 0U; i < UIH_BTN_N; i++) {
        if ((int16_t)x >= btn_x(i) && (int16_t)x < btn_x(i) + UIH_BTN_W) {
            return (int8_t)i;
        }
    }
    return UIH_HIT_NONE;
}

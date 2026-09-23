#include "ui_digi.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"

/* 18 px de renglon con una fuente de 14 deja 4 de aire: ocho renglones en
 * 144 px exactos, mas los 30 de la cabecera. UDG_LINE_H vive en la cabecera
 * porque main.c necesita cuadrar el alto del panel con el. */
#define UDG_PAD_X   6

/* La cabecera: el boton a la izquierda, la chapa a la derecha. */
static int16_t btn_y(const ui_digi_state_t *st) { return (int16_t)(st->y + 3); }
static int16_t chip_w(const ui_digi_state_t *st)
{
    return (int16_t)(gfx2_text_w(st->chip, &font_ui_14b) + 34);
}
static int16_t chip_x(const ui_digi_state_t *st)
{
    return (int16_t)(GFX2_W - UDG_PAD_X - chip_w(st));
}

/* --- la cabecera: boton a la izquierda, chapa a la derecha ---------------- */

static void pinta_boton(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    gfx2_rgba_t fondo = st->btn_press ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_SURF_2);
    gfx2_rgba_t borde = st->btn_press ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_LINE);
    gfx2_rgba_t tinta = st->btn_press ? gfx2_rgb(PAL_SURF_0) : gfx2_rgb(PAL_INK_DIM);

    if (!st->btn) { return; }
    gfx2_rrect(s, UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, 6, fondo);
    gfx2_rrect_outline(s, UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, 6, 1, borde);
    gfx2_text_in(s, UDG_BTN_X, (int16_t)(btn_y(st) + 4), UDG_BTN_W, st->btn,
                 &font_ui_14b, tinta, GFX2_ALIGN_C);
}

static void pinta_chip(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    int16_t x, w;

    if (!st->chip) { return; }
    w = chip_w(st);
    x = chip_x(st);
    gfx2_rrect(s, x, btn_y(st), w, UDG_BTN_H, 6, gfx2_rgb(PAL_SURF_2));
    gfx2_rrect_outline(s, x, btn_y(st), w, UDG_BTN_H, 6, 1, gfx2_rgb(PAL_LINE));
    gfx2_text(s, (int16_t)(x + 10), (int16_t)(btn_y(st) + 4),
              st->chip, &font_ui_14b, gfx2_rgb(PAL_INK));
    /* El punto: verde cuando lo que llega tiene forma de Morse, apagado
     * cuando no. No es adorno - es la diferencia entre "esta leyendo" y
     * "esta inventando letras a partir de ruido", que desde fuera no se
     * distingue mirando el texto. */
    gfx2_rrect(s, (int16_t)(x + w - 16), (int16_t)(btn_y(st) + UDG_BTN_H / 2 - 4), 8, 8, 4,
               st->enganchado ? gfx2_rgb(PAL_OK) : gfx2_rgb(PAL_LINE));
}

static void cabecera(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    pinta_boton(s, st);
    pinta_chip(s, st);
    /* Raya fina que separa la cabecera de los renglones: sin ella el primer
     * renglon parece parte de la fila de botones. */
    gfx2_hline(s, 0, (int16_t)(st->y + UDG_HDR_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));
}

static void texto(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_rgba_t tinta = gfx2_rgb(PAL_INK);
    gfx2_rgba_t viejo = gfx2_rgb(PAL_INK_MUTE);
    uint8_t i;

    gfx2_fill(s, 0, st->y, GFX2_W, st->h, gfx2_rgb(PAL_SURF_0));
    cabecera(s, st);

    for (i = 0U; i < UDG_ROWS; i++) {
        if (!st->fila[i] || st->fila[i][0] == '\0') { continue; }
        /* Los renglones viejos, mas apagados. Lo que acaba de llegar esta
         * abajo del todo y es lo que se esta leyendo; lo de arriba es
         * contexto. Sin esta diferencia los ocho pesan igual y hay que
         * buscar con la vista cual es el ultimo. */
        gfx2_text(s, UDG_PAD_X, (int16_t)(st->y + UDG_HDR_H + i * UDG_LINE_H),
                  st->fila[i], &font_ui_14b,
                  (i + 1U == UDG_ROWS) ? tinta : viejo);
    }
}

void ui_digi_draw_texto(const ui_digi_state_t *st)
{
    gfx2_render(0, st->y, GFX2_W, st->h, texto, (void *)st);
}

/*
 * UN solo renglon.
 *
 * Es lo que permite que llegar un caracter no cueste repintar el panel
 * entero: al teclear el decodificador solo cambia el renglon de abajo. La
 * version anterior afinaba mas -dibujaba UN glifo en su hueco- porque con
 * ancho fijo sabia exactamente donde caia; con tipografia proporcional no se
 * sabe sin medir la linea entera, asi que se redibuja la linea. Son 800x18
 * px por caracter, y a 20 palabras por minuto eso son diez caracteres por
 * segundo: nada al lado del espectro, que repinta 800x208 treinta veces.
 */
static int8_t s_fila = 0;

static void una_fila(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    uint8_t i = (uint8_t)s_fila;

    gfx2_fill(s, 0, (int16_t)(st->y + UDG_HDR_H + i * UDG_LINE_H), GFX2_W, UDG_LINE_H,
              gfx2_rgb(PAL_SURF_0));
    if (st->fila[i] && st->fila[i][0] != '\0') {
        gfx2_text(s, UDG_PAD_X, (int16_t)(st->y + UDG_HDR_H + i * UDG_LINE_H),
                  st->fila[i], &font_ui_14b,
                  (i + 1U == UDG_ROWS) ? gfx2_rgb(PAL_INK) : gfx2_rgb(PAL_INK_MUTE));
    }
}

void ui_digi_draw_fila(const ui_digi_state_t *st, uint8_t i)
{
    if (i >= UDG_ROWS) { return; }
    s_fila = (int8_t)i;
    gfx2_render(0, (int16_t)(st->y + UDG_HDR_H + i * UDG_LINE_H), GFX2_W, UDG_LINE_H,
                una_fila, (void *)st);
}

/* Repintados sueltos. Los dos caen FUERA del trazo, asi que repintarlos no
 * pelea con spectrum_draw() y no parpadean. */
static void solo_chip(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_fill(s, chip_x(st), btn_y(st), chip_w(st), UDG_BTN_H, gfx2_rgb(PAL_SURF_0));
    pinta_chip(s, st);
}

void ui_digi_draw_chip(const ui_digi_state_t *st)
{
    if (!st->chip) { return; }
    gfx2_render(chip_x(st), btn_y(st), chip_w(st), UDG_BTN_H, solo_chip, (void *)st);
}

static void solo_boton(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_fill(s, UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton(s, st);
}

void ui_digi_draw_boton(const ui_digi_state_t *st)
{
    if (!st->btn) { return; }
    gfx2_render(UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, solo_boton, (void *)st);
}

uint8_t ui_digi_boton_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    if (!st->btn) { return 0U; }
    /* Margen de 4 px, igual que el resto de zonas: este tactil pide fuerza y
     * un toque que cae justo en el borde y no hace nada se vive como que no
     * ha entrado. */
    return (uint8_t)((int16_t)x >= UDG_BTN_X - 4 &&
                     (int16_t)x <  UDG_BTN_X + UDG_BTN_W + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

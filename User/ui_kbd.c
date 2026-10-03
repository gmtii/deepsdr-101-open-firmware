#include "ui_kbd.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"
#include "font_num_44.h"

static int16_t cel_x(uint8_t i) { return (int16_t)(UIK_MARGIN + (i % UIK_COLS) * (UIK_CELL_W + UIK_GAP)); }
static int16_t cel_y(uint8_t i) { return (int16_t)(UIK_GRID_Y + (i / UIK_COLS) * (UIK_CELL_H + UIK_GAP)); }

int8_t ui_kbd_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    /* Los huecos entre teclas se reparten entre las vecinas, igual que en la
     * rejilla: en un tactil que pide fuerza, un toque que cae en la juntura
     * y no hace nada se vive como que no ha entrado. */
    for (i = 0U; i < UIK_KEYS; i++) {
        int16_t cx = cel_x(i), cy = cel_y(i);
        if ((int16_t)x >= cx - UIK_GAP / 2 && (int16_t)x < cx + UIK_CELL_W + UIK_GAP / 2 &&
            (int16_t)y >= cy - UIK_GAP / 2 && (int16_t)y < cy + UIK_CELL_H + UIK_GAP / 2) {
            return (int8_t)i;
        }
    }
    return UIK_HIT_NONE;
}

/* Colores de una tecla segun lo que hace y como este. */
static void colores(const ui_kbd_state_t *st, uint8_t i,
                    gfx2_rgba_t *fondo, gfx2_rgba_t *borde, gfx2_rgba_t *tinta)
{
    uint8_t press = (uint8_t)(st->pressed == (int8_t)i);

    if (press) {
        /* Pulsada: se invierte, igual que en la rejilla. Es el unico aviso
         * de que ha entrado el toque, porque un digito no cambia de aspecto
         * despues de pulsarlo - lo que cambia es la lectura de arriba. */
        *fondo = gfx2_rgb(PAL_ACCENT);
        *borde = gfx2_rgb(PAL_ACCENT);
        *tinta = gfx2_rgb(PAL_SURF_0);
        return;
    }

    switch (st->tipo[i]) {
    case UIK_ACEPTA:
        /* La unica con relleno de acento: en una rejilla de dieciseis, la
         * tecla que APLICA tiene que encontrarse sin leerlas todas. */
        *fondo = gfx2_rgba(PAL_ACCENT, 44);
        *borde = gfx2_rgba(PAL_ACCENT, 170);
        *tinta = gfx2_rgb(PAL_ACCENT);
        break;
    case UIK_BORRA:
        *fondo = gfx2_rgb(PAL_SURF_2);
        *borde = gfx2_rgba(PAL_WARN, 150);
        *tinta = gfx2_rgb(PAL_WARN);
        break;
    case UIK_VUELVE:
        *fondo = gfx2_rgb(PAL_SURF_1);
        *borde = gfx2_rgb(PAL_LINE);
        *tinta = gfx2_rgb(PAL_INK_DIM);
        break;
    default:
        *fondo = gfx2_rgb(PAL_SURF_2);
        *borde = gfx2_rgb(PAL_LINE);
        *tinta = gfx2_rgb(PAL_INK);
        break;
    }
}

static void tecla(gfx2_surf_t *s, const ui_kbd_state_t *st, uint8_t i)
{
    int16_t x = cel_x(i), y = cel_y(i);
    gfx2_rgba_t fondo, borde, tinta;

    if (st->tipo[i] == (uint8_t)UIK_NADA || st->tecla[i] == 0) {
        return;   /* hueco: se deja el fondo, sin marco que invite a tocarlo */
    }

    colores(st, i, &fondo, &borde, &tinta);

    gfx2_rrect(s, x, y, UIK_CELL_W, UIK_CELL_H, 8, fondo);
    gfx2_rrect_outline(s, x, y, UIK_CELL_W, UIK_CELL_H, 8, 1, borde);
    if (st->cursor == (int8_t)i && st->pressed != (int8_t)i) {
        gfx2_rrect_outline(s, x, y, UIK_CELL_W, UIK_CELL_H, 8, 2, gfx2_rgb(PAL_ACCENT));
    }

    /* Los digitos van en la MISMA tipografia que la lectura de arriba y que
     * la frecuencia de la cabecera: se teclea un numero y se ve el mismo
     * numero, con la misma cara, sin tener que mirar dos veces.
     *
     * Y ademas sale gratis. La alternativa era font_ui_24b, que no la usa
     * ninguna otra pantalla: meterla habria costado 17 KB de flash -media
     * holgura de la que queda- por unas cifras mas.
     *
     * y+4 con ascent=43 deja las cifras entre 4 y 47 de los 59 de alto de la
     * tecla; las cifras no tienen rasgos por debajo de la linea base, asi que
     * el descent de la fuente no cuenta aqui. */
    if (st->tipo[i] == (uint8_t)UIK_DIGITO) {
        gfx2_text_in(s, x, (int16_t)(y + 4), UIK_CELL_W, st->tecla[i],
                     &font_num_44, tinta, GFX2_ALIGN_C);
    } else {
        gfx2_text_in(s, x, (int16_t)(y + 19), UIK_CELL_W, st->tecla[i],
                     &font_ui_18b, tinta, GFX2_ALIGN_C);
    }
}

/* La franja de arriba: titulo, pista y lo que se lleva escrito. */
static void lectura(gfx2_surf_t *s, const ui_kbd_state_t *st)
{
    int16_t w;

    gfx2_fill(s, 0, UIK_Y, GFX2_W, UIK_READ_H, gfx2_rgb(PAL_SURF_0));

    if (st->titulo) {
        gfx2_text(s, UIK_MARGIN + 4, (int16_t)(UIK_Y + 6), st->titulo,
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
    }
    if (st->pista) {
        gfx2_text(s, UIK_MARGIN + 4, (int16_t)(UIK_Y + 28), st->pista,
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
    }

    /* Lo escrito va a la DERECHA y en la tipografia de la frecuencia de la
     * cabecera: es el mismo dato, y verlo con la misma cara mientras lo
     * tecleas es lo que hace que no haya que comprobarlo dos veces. */
    if (st->lectura && st->lectura[0] != '\0') {
        w = gfx2_text_w(st->lectura, &font_num_44);
        if (st->unidad) {
            int16_t wu = gfx2_text_w(st->unidad, &font_ui_18b);
            gfx2_text(s, (int16_t)(GFX2_W - UIK_MARGIN - 4 - wu),
                      (int16_t)(UIK_Y + 26), st->unidad,
                      &font_ui_18b, gfx2_rgb(PAL_INK_MUTE));
            w = (int16_t)(w + wu + 10);
        }
        gfx2_text(s, (int16_t)(GFX2_W - UIK_MARGIN - 4 - w),
                  (int16_t)(UIK_Y + 2), st->lectura,
                  &font_num_44, gfx2_rgb(PAL_ACCENT));
    } else {
        /* Aun no hay nada escrito. Un hueco vacio no dice si la pantalla
         * esta lista o colgada; una raya si, y ademas no se confunde con un
         * cero tecleado. */
        /* Rayas, no un guion largo: font_num_44 solo lleva cifras, signos y
         * dos puntos (ver sus rangos en fonts_data.c), asi que cualquier
         * otra cosa saldria en blanco. */
        gfx2_text(s, (int16_t)(GFX2_W - UIK_MARGIN - 4 - gfx2_text_w("---", &font_num_44)),
                  (int16_t)(UIK_Y + 2), "---",
                  &font_num_44, gfx2_rgb(PAL_INK_MUTE));
    }

    gfx2_hline(s, 0, (int16_t)(UIK_Y + UIK_READ_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));
}

/* --- entradas del modulo, con el mismo reparto por bandas que ui_grid --- */

static void draw_all(gfx2_surf_t *s, void *ctx)
{
    const ui_kbd_state_t *st = (const ui_kbd_state_t *)ctx;
    uint8_t i;

    gfx2_fill(s, 0, UIK_Y, GFX2_W, UIK_H, gfx2_rgb(PAL_SURF_0));
    lectura(s, st);
    for (i = 0U; i < UIK_KEYS; i++) { tecla(s, st, i); }
}

void ui_kbd_draw(const ui_kbd_state_t *st)
{
    gfx2_render(0, UIK_Y, GFX2_W, UIK_H, draw_all, (void *)st);
}

/* Cual repintar en ui_kbd_draw_one(): gfx2_render() solo pasa un puntero de
 * contexto y ese ya lleva el estado, asi que el indice viaja por aqui. Es
 * local al fichero y solo vive entre la asignacion y la llamada de justo
 * debajo, que son sincronas. */
static int8_t s_una = 0;

static void draw_one(gfx2_surf_t *s, void *ctx)
{
    const ui_kbd_state_t *st = (const ui_kbd_state_t *)ctx;
    uint8_t i = (uint8_t)s_una;

    gfx2_fill(s, cel_x(i), cel_y(i), UIK_CELL_W, UIK_CELL_H, gfx2_rgb(PAL_SURF_0));
    tecla(s, st, i);
}

void ui_kbd_draw_one(const ui_kbd_state_t *st, int8_t i)
{
    if (i < 0 || i >= (int8_t)UIK_KEYS) { return; }
    s_una = i;
    gfx2_render(cel_x((uint8_t)i), cel_y((uint8_t)i), UIK_CELL_W, UIK_CELL_H,
                draw_one, (void *)st);
}

static void draw_read(gfx2_surf_t *s, void *ctx)
{
    lectura(s, (const ui_kbd_state_t *)ctx);
}

void ui_kbd_draw_lectura(const ui_kbd_state_t *st)
{
    gfx2_render(0, UIK_Y, GFX2_W, UIK_READ_H, draw_read, (void *)st);
}

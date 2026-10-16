/*
 * Ajustes, rediseño 2: mismos nombres y valores a la vista que la versión de
 * lista, pero con objetivos táctiles del tamaño que pide un dedo sobre un
 * panel resistivo.
 *
 * QUÉ CAMBIA Y POR QUÉ
 * --------------------
 * La versión de lista tenía filas de 40 px = 4,7 mm de alto, la mitad de los
 * ~9 mm que pide cualquier guía de táctil (44 pt de Apple, 48 dp de Material),
 * y en resistivo sin háptica conviene MÁS margen, no menos. Los tiles de
 * 95 px del firmware original eran 11,1 mm: bien dimensionados. El fallo fue
 * tirar los tiles en vez de quedarme con su tamaño y arreglarles el contenido.
 *
 * Aquí la rejilla vuelve, pero cada celda lleva NOMBRE REAL, VALOR ACTUAL y
 * una línea de ayuda - lo que antes obligaba a entrar en cada tile para saber
 * en qué estaba.
 *
 * Y el mando entra como alternativa: girar mueve el foco, pulsar entra. Con
 * eso la precisión del dedo deja de ser obligatoria para nada.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_ui_18b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

#define HDR_H   56
#define FOOT_H  26
#define SEC_W  176
#define PAD      8

/*
 * Altura disponible, calculada una vez y usada por todos, en vez de fijar
 * alturas a ojo y descubrir en el render que la última fila se come el pie -
 * que es justo lo que pasó en el primer intento.
 *
 *   480 - 56 (cabecera) - 26 (pie) - 8 arriba - 8 abajo = 382
 *
 * Tiles:     3 filas + 2 huecos de 8  ->  (382 - 16) / 3 = 122 px = 14,3 mm
 * Secciones: 4 filas + 3 huecos de 8  ->  (382 - 24) / 4 =  89 px = 10,4 mm
 */
#define CONT_Y  (HDR_H + PAD)
#define CONT_H  (GFX2_H - FOOT_H - CONT_Y - PAD)
#define TILE_H  ((CONT_H - 2 * PAD) / 3)
#define SEC_H   ((CONT_H - 3 * PAD) / 4)

typedef struct { const char *name; const char *value; const char *hint; } opt_t;

static const char *k_secs[4] = { "Recepción", "Filtros", "Pantalla", "Digitales" };

static const opt_t k_opts[6] = {
    { "AGC",                "Media",       "velocidad del control de ganancia" },
    { "Silenciador",        "12",          "umbral en AM y FM estrecha" },
    { "Ganancia",           "24,0 dB",     "PGA del códec, 0 a 47,5" },
    { "Atenuador",          "-6 dB",       "fijo: 0 / -6 / -12 dB" },
    { "AGC de RF",          "Activado",    "atenúa solo si se satura" },
    { "Reducción de ruido", "Desactivada", "sustracción espectral" },
};

static void draw(gfx2_surf_t *s, void *ctx)
{
    int16_t i;
    (void)ctx;

    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));

    /* --- cabecera fina: sitio para el botón de cerrar y poco más --- */
    gfx2_vgrad(s, 0, 0, GFX2_W, HDR_H, gfx2_rgb(PAL_HDR_TOP), gfx2_rgb(PAL_SURF_1));
    gfx2_hline(s, 0, (int16_t)(HDR_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));
    gfx2_text(s, 16, 14, "Ajustes", &font_ui_18b, gfx2_rgb(PAL_INK));
    gfx2_text(s, 150, 22, "·  Recepción", &font_ui_18b, gfx2_rgb(PAL_INK_DIM));
    {
        /* 132 x 44 px = 15,4 x 5,1 mm. Ancho de sobra; la altura es la que
         * manda, y en la cabecera no hay más. Se compensa con que es la
         * esquina, que es fácil de acertar. */
        int16_t bx = (int16_t)(GFX2_W - 132 - 12);
        gfx2_rrect(s, bx, 6, 132, 44, 8, gfx2_rgb(PAL_SURF_2));
        gfx2_rrect_outline(s, bx, 6, 132, 44, 8, 1, gfx2_rgb(PAL_LINE));
        gfx2_text_in(s, bx, 18, 132, "Cerrar", &font_ui_18b, gfx2_rgb(PAL_INK),
                     GFX2_ALIGN_C);
    }

    /* --- columna de secciones: 4 objetivos de 176 x 98 px = 20,6 x 11,4 mm --- */
    for (i = 0; i < 4; i++) {
        int16_t ty = (int16_t)(CONT_Y + i * (SEC_H + PAD));
        int on = (i == 0);
        gfx2_rrect(s, PAD, ty, SEC_W, SEC_H, 9,
                   gfx2_rgb(on ? PAL_SURF_2 : PAL_SURF_0));
        gfx2_rrect_outline(s, PAD, ty, SEC_W, SEC_H, 9, 1,
                           on ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_LINE));
        if (on) {
            /* barra de acento: la sección activa no depende solo del color */
            gfx2_rrect(s, (int16_t)(PAD + 8), (int16_t)(ty + SEC_H / 2 - 22), 5, 44, 2,
                       gfx2_rgb(PAL_ACCENT));
        }
        gfx2_text_in(s, (int16_t)(PAD + 16), (int16_t)(ty + SEC_H / 2 - 12),
                     (int16_t)(SEC_W - 24), k_secs[i], &font_ui_18b,
                     gfx2_rgb(on ? PAL_INK : PAL_INK_DIM), GFX2_ALIGN_C);
    }

    /* --- rejilla 2 x 3: celdas de 296 x 130 px = 34,6 x 15,2 mm --- */
    for (i = 0; i < 6; i++) {
        int16_t col = (int16_t)(i % 2), row = (int16_t)(i / 2);
        int16_t cx = (int16_t)(SEC_W + PAD * 2 + col * 304);
        int16_t cy = (int16_t)(CONT_Y + row * (TILE_H + PAD));
        int16_t cw = 296, ch = TILE_H;
        int focus = (i == 2);   /* el que tendría el mando ahora mismo */

        gfx2_rrect(s, cx, cy, cw, ch, 10, gfx2_rgb(PAL_SURF_2));
        gfx2_rrect_outline(s, cx, cy, cw, ch, 10, focus ? 2 : 1,
                           focus ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_LINE));

        gfx2_text(s, (int16_t)(cx + 16), (int16_t)(cy + 12), k_opts[i].name,
                  &font_ui_18b, gfx2_rgb(PAL_INK));
        gfx2_text(s, (int16_t)(cx + 16), (int16_t)(cy + 38), k_opts[i].hint,
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));

        /* el VALOR es lo grande: es lo que se quiere leer de un vistazo */
        gfx2_text(s, (int16_t)(cx + 16), (int16_t)(cy + ch - 42), k_opts[i].value,
                  &font_ui_18b, gfx2_rgb(focus ? PAL_ACCENT : PAL_INK));

        if (focus) {
            /* icono de mando: esta opción está bajo el control del encoder */
            int16_t ky = (int16_t)(cy + ch - 40);
            gfx2_rrect(s, (int16_t)(cx + cw - 42), ky, 26, 26, 13,
                       gfx2_rgba(PAL_ACCENT, 60));
            gfx2_rrect_outline(s, (int16_t)(cx + cw - 42), ky, 26, 26, 13, 2,
                               gfx2_rgb(PAL_ACCENT));
            gfx2_fill(s, (int16_t)(cx + cw - 30), (int16_t)(ky + 2), 2, 7,
                      gfx2_rgb(PAL_ACCENT));
        }
    }

    /* --- pie: el mando como alternativa al dedo --- */
    {
        int16_t fy = (int16_t)(GFX2_H - FOOT_H);
        gfx2_fill(s, 0, fy, GFX2_W, FOOT_H, gfx2_rgb(PAL_SURF_1));
        gfx2_hline(s, 0, fy, GFX2_W, gfx2_rgb(PAL_LINE));
        gfx2_text(s, 12, (int16_t)(fy + 4),
                  "El mando es la vía principal: girar mueve el foco, pulsar entra a ajustar. El dedo, opcional.",
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
    }
}

int main(void)
{
    lcd_sim_reset(0);
    gfx2_render_screen(draw, 0);
    lcd_sim_write_ppm("out/08_settings2.ppm");
    printf("ok\n");
    return 0;
}

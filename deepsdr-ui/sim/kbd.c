/*
 * Render del teclado nuevo (etapa 19) con el ui_kbd.c que se copia a User/.
 *
 * Ademas comprueba lo que solo se ve midiendo: que ningun rotulo se sale de
 * su tecla, que la lectura mas larga posible cabe en su franja, y que el
 * mapa de toques devuelve cada celda donde se dibuja y nada fuera.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_kbd.h"
#include "font_ui_18b.h"

#include "font_num_44.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

/* El teclado de frecuencia, tal cual lo va a montar main.c. */
static void kbd_freq(ui_kbd_state_t *st, const char *lectura)
{
    static const char *const rot[UIK_KEYS] = {
        "1","2","3","Borrar",
        "4","5","6","Vaciar",
        "7","8","9","Volver",
        ",","0","kHz","MHz",
    };
    static const uint8_t tip[UIK_KEYS] = {
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_BORRA,
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_BORRA,
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_VUELVE,
        UIK_DIGITO,UIK_DIGITO,UIK_ACEPTA,UIK_ACEPTA,
    };
    uint8_t i;
    memset(st, 0, sizeof *st);
    st->titulo  = "Frecuencia";
    st->pista   = "teclea y elige la unidad";
    st->lectura = lectura;
    st->pressed = -1;
    st->cursor  = -1;
    for (i = 0U; i < UIK_KEYS; i++) { st->tecla[i] = rot[i]; st->tipo[i] = tip[i]; }
}

static void kbd_hora(ui_kbd_state_t *st, const char *lectura)
{
    static const char *const rot[UIK_KEYS] = {
        "1","2","3","Borrar",
        "4","5","6","Vaciar",
        "7","8","9","Volver",
        0,  "0","Poner en hora", 0,
    };
    static const uint8_t tip[UIK_KEYS] = {
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_BORRA,
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_BORRA,
        UIK_DIGITO,UIK_DIGITO,UIK_DIGITO,UIK_VUELVE,
        UIK_NADA,  UIK_DIGITO,UIK_ACEPTA, UIK_NADA,
    };
    uint8_t i;
    memset(st, 0, sizeof *st);
    st->titulo  = "Hora";
    st->pista   = "cuatro cifras, HHMM";
    st->lectura = lectura;
    st->pressed = -1;
    st->cursor  = -1;
    for (i = 0U; i < UIK_KEYS; i++) { st->tecla[i] = rot[i]; st->tipo[i] = tip[i]; }
}

static void mide(const ui_kbd_state_t *st)
{
    uint8_t i;
    const int16_t hueco = UIK_CELL_W - 16;

    for (i = 0U; i < UIK_KEYS; i++) {
        int16_t w;
        if (st->tipo[i] == UIK_NADA || !st->tecla[i]) { continue; }
        w = gfx2_text_w(st->tecla[i],
                        st->tipo[i] == UIK_DIGITO ? &font_num_44 : &font_ui_18b);
        if (w > hueco) {
            printf("    \"%s\" mide %d px y la tecla da %d  NO CABE\n",
                   st->tecla[i], w, hueco);
            fallos++;
        }
    }
}

int main(int argc, char **argv)
{
    ui_kbd_state_t st = {0};
    int cual = (argc > 1) ? atoi(argv[1]) : 0;
    uint8_t i;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);

    if (cual == 1)      { kbd_hora(&st, "07:4"); }
    else if (cual == 2) { kbd_freq(&st, ""); }
    else                { kbd_freq(&st, "14.074,0"); st.pressed = 6; }

    ui_kbd_draw(&st);

    printf("teclado %s\n", cual == 1 ? "de hora" : "de frecuencia");
    printf("  rejilla y=%d..%d, ultima fila acaba en %d (la zona llega a %d)\n",
           UIK_GRID_Y,
           UIK_GRID_Y + UIK_ROWS * UIK_CELL_H + (UIK_ROWS - 1) * UIK_GAP,
           UIK_GRID_Y + UIK_ROWS * UIK_CELL_H + (UIK_ROWS - 1) * UIK_GAP,
           UIK_Y + UIK_H);
    if (UIK_GRID_Y + UIK_ROWS * UIK_CELL_H + (UIK_ROWS - 1) * UIK_GAP > UIK_Y + UIK_H) {
        printf("  la rejilla se sale de la zona de menu  MAL\n");
        fallos++;
    }
    /* Las columnas tienen que coincidir con las de la rejilla de ajustes:
     * si no, al cambiar de pantalla las cosas bailan. */
    if (UIK_MARGIN * 2 + UIK_COLS * UIK_CELL_W + (UIK_COLS - 1) * UIK_GAP != GFX2_W) {
        printf("  las columnas no suman 800  MAL\n");
        fallos++;
    }

    mide(&st);
    { ui_kbd_state_t o; kbd_freq(&o, ""); mide(&o); kbd_hora(&o, ""); mide(&o); }

    /* La lectura mas larga que puede salir: 9 cifras, coma y unidad. */
    {
        int16_t w = gfx2_text_w("999.999.999", &font_num_44)
                  + gfx2_text_w("MHz", &font_ui_18b) + 10;
        int16_t sitio = GFX2_W - UIK_MARGIN - 4 - gfx2_text_w("Frecuencia", &font_ui_18b) - 20;
        printf("  lectura mas larga %d px, sitio %d  %s\n", w, sitio, w <= sitio ? "OK" : "NO CABE");
        if (w > sitio) { fallos++; }
    }

    /* Mapa de toques: el centro de cada tecla devuelve esa tecla. */
    for (i = 0U; i < UIK_KEYS; i++) {
        int16_t cx = (int16_t)(UIK_MARGIN + (i % UIK_COLS) * (UIK_CELL_W + UIK_GAP) + UIK_CELL_W / 2);
        int16_t cy = (int16_t)(UIK_GRID_Y + (i / UIK_COLS) * (UIK_CELL_H + UIK_GAP) + UIK_CELL_H / 2);
        if (ui_kbd_hit((uint16_t)cx, (uint16_t)cy) != (int8_t)i) {
            printf("    la tecla %u no responde en su centro  MAL\n", i);
            fallos++;
        }
    }
    if (ui_kbd_hit(400U, (uint16_t)(UIK_Y + 10)) != UIK_HIT_NONE) {
        printf("    la franja de la lectura responde como tecla  MAL\n");
        fallos++;
    }
    if (ui_kbd_hit(400U, 50U) != UIK_HIT_NONE) {
        printf("    la cabecera responde como tecla  MAL\n");
        fallos++;
    }

    {
        char nombre[64];
        snprintf(nombre, sizeof nombre, "out/kbd%d.ppm", cual);
        lcd_sim_write_ppm(nombre);
    }
    printf("  %s\n", fallos ? "HAY FALLOS" : "todo cabe y los toques caen donde deben");
    return fallos ? 1 : 0;
}

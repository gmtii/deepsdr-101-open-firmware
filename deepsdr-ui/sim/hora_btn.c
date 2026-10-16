/*
 * ======================================================================
 * EL BOTON DE EMISORA DE LA PANTALLA DE LA HORA - 13/10/2026
 * ======================================================================
 *
 * Cinco de los diez rotulos de emisora salian CORTADOS, incluido el que
 * sale al abrir la pantalla: "DCF77 - 77,5 kHz, Alemania" se leia
 * "DCF77 - 77,5 kHz, Alem".
 *
 * El dibujo bajaba de font_ui_18b a font_ui_14b si no cabia... y daba el
 * asunto por resuelto sin volver a medir. gfx2_text_in() recorta por la
 * derecha SIN AVISAR, asi que un rotulo que tampoco cabe en la pequena no
 * se nota como fallo: simplemente le falta el final. Y al recortar se
 * alinea a la izquierda, asi que ademas salia descentrado respecto a los
 * otros tres botones.
 *
 * Por que no lo veia nadie: sim/captura_menus.c rellena
 * hora.emisora = "DCF77" -un marcador corto, no el nombre de verdad- y es
 * el unico banco que toca ui_hora.c.
 *
 * ESTE BANCO LEE PIXELES, no repite la cuenta del dibujo: pinta la
 * pantalla con CADA nombre de k_emisoras[] en los dos idiomas y mira
 * donde acaba la tinta dentro del boton. Si la tinta llega al borde, el
 * texto esta cortado.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_hora.h"
#include "dcf77.h"
#include "idioma.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

/*
 * Los cabos sueltos de dcf77.c. Aqui no se decodifica nada: lo unico que
 * se le pide a ese fichero son los NOMBRES de k_emisoras[], y se enlaza
 * entero a proposito para que los nombres salgan del sitio de verdad y no
 * de una copia escrita a mano que se quede vieja el dia que se anada una
 * emisora.
 */
void rbu_start(float fs) { (void)fs; }
void wwv_start(float fs) { (void)fs; }
void rbu_feed(const float *a, uint32_t n) { (void)a; (void)n; }
void wwv_feed(const float *a, uint32_t n) { (void)a; (void)n; }
void rbu_info(void *o) { (void)o; }
void wwv_info(void *o) { (void)o; }

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(void)
{
    ui_hora_state_t st;
    uint8_t e;
    int idi;

    printf("\n== EL BOTON DE EMISORA DE LA PANTALLA DE LA HORA ==\n");
    printf("\n  el boton mide %d x %d, en x = %d\n\n",
           (int)UIH_BTN_W, (int)UIH_BTN_H, (int)ui_hora_btn_x_dbg(0));

    for (idi = 0; idi < 2; idi++) {
        idioma_pon(idi ? IDIOMA_EN : IDIOMA_ES);
        for (e = 0U; e < (uint8_t)HORA_N; e++) {
            const char *nom = dcf77_nombre((hora_emisora_t)e);
            int16_t bx = ui_hora_btn_x_dbg(0);
            int16_t x, y, x0 = 9999, x1 = -1;
            uint16_t fondo565;

            memset(&st, 0, sizeof st);
            st.emisora = nom;
            st.hora    = "12:34:56";
            st.estado  = "";

            lcd_sim_reset(0x0000);
            gfx2_render_screen(fondo, 0);
            ui_hora_draw(&st);

            /* El color del interior del boton, leido de una esquina suya
             * que el texto no alcanza nunca. */
            fondo565 = lcd_sim_fb()[(UIH_BTN_Y + 3) * LCD_W + (bx + 3)];

            for (y = (int16_t)(UIH_BTN_Y + 3);
                 y < (int16_t)(UIH_BTN_Y + UIH_BTN_H - 3); y++) {
                for (x = (int16_t)(bx + 2);
                     x < (int16_t)(bx + UIH_BTN_W - 2); x++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo565) {
                        if (x < x0) { x0 = x; }
                        if (x > x1) { x1 = x; }
                    }
                }
            }
            {
                int izq = (x1 >= 0) ? (int)(x0 - bx) : -1;
                int der = (x1 >= 0) ? (int)(bx + UIH_BTN_W - 1 - x1) : -1;
                int bien = (x1 >= 0) && izq >= 3 && der >= 3;

                printf("    %-28s tinta %3d..%3d del boton   aire %2d/%2d  %s\n",
                       nom, izq, (int)(x1 - bx), izq, der,
                       bien ? "ok" : "*** MAL: cortado o pegado al borde ***");
                if (!bien) { fallos++; }
            }
        }
    }
    idioma_pon(IDIOMA_ES);

    /* Y una foto con el de por omision, para mirarla. */
    memset(&st, 0, sizeof st);
    st.emisora = dcf77_nombre(HORA_DCF77);
    st.hora = "12:34:56"; st.estado = "";
    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_hora_draw(&st);
    lcd_sim_write_ppm("out/hora_boton.ppm");
    printf("\n  foto en out/hora_boton.ppm\n");

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

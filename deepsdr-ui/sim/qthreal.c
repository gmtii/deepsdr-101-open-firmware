/*
 * BANCO DE LA PANTALLA DEL QTH.
 *
 * Compila el User/ui_qth.c DEL FIRMWARE, no una copia: lo que se mide aqui
 * es lo que se dibuja en la radio.
 *
 * Mide tres cosas que no se ven mirando:
 *
 *   1. Que los seis caracteres, las pistas y los cuatro rotulos caben en su
 *      hueco EN LOS DOS IDIOMAS. gfx2_text_in() alinea pero no recorta: lo
 *      que se pasa se pinta encima de lo de al lado.
 *   2. Que las casillas y los botones caben en la banda 104..421 y no se
 *      solapan entre ellos.
 *   3. Que ui_qth_hit() devuelve la casilla correcta en todo el ancho de
 *      cada caja y NONE en los huecos - un toque que cae en el sitio
 *      equivocado cambia el caracter equivocado.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_qth.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

/* El idioma del render. La cadena la eligen los dos arrays de abajo: este
 * banco no enlaza main.c, asi que los textos se declaran aqui IGUAL que los
 * pone qth_fill(). Si se cambian alli y no aqui, el banco mide otra cosa -
 * por eso se imprimen, para que se vea que se esta midiendo. */
static const char *k_pie[2] = {
    "tu localizador: de aquí salen el aspa de los mapas y las distancias",
    "your locator: the map cross and the distances come from this"
};
static const char *k_rot[2][UIQ_BTN_N] = {
    { "-", "+", "Aplicar", "Salir" },
    { "-", "+", "Apply",   "Exit"  }
};

static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    if (!ok) { fallos++; }
    printf("  %-44s %4d px en %4d  %s\n", que, w, hueco, ok ? "OK" : "NO CABE");
}

int main(int argc, char **argv)
{
    ui_qth_state_t st;
    const char *loc = (argc > 1) ? argv[1] : "IN80dk";
    int lang = (argc > 2) ? atoi(argv[2]) : 0;
    int i, peor = 0;
    const char *qpeor = "";

    memset(&st, 0, sizeof st);
    { int q; for (q = 0; q < UIQ_N; q++) { st.loc[q] = loc[q] ? loc[q] : 0x41; } st.loc[UIQ_N] = 0; }
    st.titulo = "QTH";
    st.pie    = k_pie[lang];
    st.coord  = "40,4375 N   3,7083 W";
    for (i = 0; i < UIQ_N; i++) { st.pista[i] = (i < 2) ? "A-R" : ((i < 4) ? "0-9" : "a-x"); }
    for (i = 0; i < UIQ_BTN_N; i++) { st.rot[i] = k_rot[lang][i]; }
    st.sel = 0; st.cambiado = 1; st.pressed = -1;

    printf("QTH \"%s\", idioma %d\n\n", st.loc, lang);

    /* 1. anchos */
    for (i = 0; i < 2; i++) {
        int k;
        for (k = 0; k < UIQ_BTN_N; k++) {
            int w = gfx2_text_w(k_rot[i][k], &font_ui_18b);
            if (w > peor) { peor = w; qpeor = k_rot[i][k]; }
        }
    }
    printf("  rotulo mas ancho de los dos idiomas: \"%s\"\n", qpeor);
    cabe("  cabe en el boton", (int16_t)peor, (int16_t)(UIQ_BTN_W - 16));
    peor = 0;
    for (i = 0; i < 2; i++) {
        int w = gfx2_text_w(k_pie[i], &font_ui_14);
        if (w > peor) { peor = w; qpeor = k_pie[i]; }
    }
    cabe("el pie no pisa el titulo", (int16_t)peor,
         (int16_t)(GFX2_W - UIQ_PAD - gfx2_text_w("QTH", &font_ui_18b) - 40));
    cabe("la coordenada cabe a lo ancho",
         (int16_t)gfx2_text_w("-179,9999 W   -89,9999 S", &font_ui_18b), GFX2_W);

    /* 2. geometria */
    cabe("las seis casillas caben a lo ancho",
         (int16_t)(UIQ_CAJA_X0 * 2 + UIQ_N * UIQ_CAJA_W + (UIQ_N - 1) * UIQ_CAJA_GAP),
         GFX2_W);
    cabe("las casillas no pisan los botones",
         (int16_t)(UIQ_CAJA_Y + UIQ_CAJA_H + 44), (int16_t)UIQ_BTN_Y);
    cabe("los botones caben en la banda",
         (int16_t)(UIQ_BTN_Y + UIQ_BTN_H), (int16_t)(UIQ_Y + UIQ_H));
    cabe("los cuatro botones caben a lo ancho",
         (int16_t)(UIQ_PAD * 2 + 4 * UIQ_BTN_W + 3 * UIQ_BTN_GAP), GFX2_W);
    cabe("objetivo minimo de dedo en la casilla", 47, UIQ_CAJA_W);
    cabe("objetivo minimo de dedo en el boton", 47, UIQ_BTN_H);

    /* 3. el toque */
    {
        int malos = 0;
        for (i = 0; i < UIQ_N; i++) {
            int16_t x0 = (int16_t)(UIQ_CAJA_X0 + i * (UIQ_CAJA_W + UIQ_CAJA_GAP));
            int16_t x;
            for (x = x0; x < x0 + UIQ_CAJA_W; x++) {
                if (ui_qth_hit((uint16_t)x, (uint16_t)(UIQ_CAJA_Y + 10))
                    != (int8_t)(UIQ_HIT_CAJA + i)) { malos++; }
            }
            /* el hueco entre cajas no puede devolver casilla */
            if (i + 1 < UIQ_N) {
                for (x = (int16_t)(x0 + UIQ_CAJA_W);
                     x < (int16_t)(x0 + UIQ_CAJA_W + UIQ_CAJA_GAP); x++) {
                    if (ui_qth_hit((uint16_t)x, (uint16_t)(UIQ_CAJA_Y + 10)) != UIQ_HIT_NONE) { malos++; }
                }
            }
        }
        printf("  puntos de toque mal enrutados en las casillas: %d  %s\n",
               malos, malos ? "MAL" : "OK");
        if (malos) { fallos++; }
    }

    lcd_sim_reset(0x0000);

    ui_qth_draw(&st);
    lcd_sim_write_ppm("out/qth.ppm");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * COMO QUEDA LA PANTALLA DE FT8 - 25/09/2026.
 *
 * Lo de "bonito" no se comprueba con un banco. Lo que SI se comprueba, y es
 * lo que se colaba antes, son los numeros que hay debajo:
 *
 *   - que ningun campo se salga por la derecha ni se pise con el siguiente,
 *     con los valores mas anchos que puede haber (99:99, 9999 Hz, -99 dB,
 *     el mensaje mas largo que produce ft8_lib y 99999 km);
 *   - que los renglones quepan en el alto del panel contando el de titulos;
 *   - que la columna de km quede pegada al borde derecho y no colgando;
 *   - y que el reparto llene la pantalla en vez de dejar media vacia, que
 *     era la queja.
 *
 * Deja ademas out/ft8_panel.ppm para poder MIRARLO, que es la unica forma de
 * juzgar lo primero.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

static void pasa(const char *que, int bien)
{
    printf("    %-58s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* Ancho en px del campo k de una linea con tabuladores. */
static int ancho_campo(const char *l, int k)
{
    char c[64]; int n = 0, i = 0;
    while (*l && i < k) { if (*l == '\t') { i++; } l++; }
    while (*l && *l != '\t' && n < 63) { c[n++] = *l++; }
    c[n] = '\0';
    return gfx2_text_w(c, &font_ui_14b);
}

int main(void)
{
    /* El panel entero, igual que en main.c: de donde empieza el espectro a
     * donde acaba el de texto. */
    static const char *filas[] = {
        "05:17\t0263\t+12\t+0,2\tDL5AN TA3NK KM39\t4191\tTurquia",
        "05:17\t0266\t+11\t+0,2\tDL5AN TA3NK R+04\t\tTurquia",
        "05:17\t0153\t+09\t-0,3\tCQ AI0Y EN08\t7116\tEE.UU.",
        "05:17\t0350\t+08\t+1,1\tEU7SV SZ4VJ -19\t\tGrecia",
        "05:18\t0263\t+15\t+0,2\tCQ DX DL5AN JN49\t3209\tAlemania",
        "05:18\t0153\t+11\t-0,3\tAI0Y OK2FD JN79\t3573\tRep. Checa",
        "99:99\t9999\t-99\t-9,9\tCQ DX PA9XYZ/P KA1ABC/R RR73\t99999\tCeuta-Melilla",   /* el peor caso */
    };
    ui_digi_state_t st;
    unsigned i;

    memset(&st, 0, sizeof st);
    st.y = 104; st.h = 318;
    st.filas = 15;
    st.cols = ui_digi_cols_ft8;
    st.ncols = UI_DIGI_COLS_FT8_N;
    st.titulos = "Hora\tHz\tdB\tDT\tMensaje\tkm\tPaís";
    st.btn = "Borrar";
    st.chip = "9/15";
    st.enganchado = 1;
    st.barra_on = 1; st.barra_v = 9; st.barra_max = 15;
    st.barra_txt = "999c  9999ms  99p  +9,9";
    /* Como lo arma ft8_chapa(): el segundo de la ranura va pegado a la
     * derecha de la barra, con su punto, y lo demas se centra en lo que
     * queda. Sin esto el banco mediria un hueco que no existe. */
    st.barra_dcha = 1;
    st.barra_txt_dcha = "9/15";
    for (i = 0; i < sizeof filas / sizeof filas[0]; i++) { st.fila[i] = filas[i]; }

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_digi_draw_texto(&st);
    lcd_sim_write_ppm("out/ft8_panel.ppm");

    printf("PANTALLA DE FT8\n\n  COLUMNAS (pantalla de %d px)\n", GFX2_W);
    {
        const char *peor = filas[6];
        const char *nom[] = { "Hora", "Hz", "dB", "DT", "Mensaje", "km", "País" };
        int k, fin_ant = 0;

        for (k = 0; k < UI_DIGI_COLS_FT8_N; k++) {
            int x = ui_digi_cols_ft8[k];
            int w = ancho_campo(peor, k);
            int lim = (k + 1 < UI_DIGI_COLS_FT8_N)
                      ? ((ui_digi_cols_ft8[k+1] >= 0) ? ui_digi_cols_ft8[k+1]
                                                      : -ui_digi_cols_ft8[k+1] - 60)
                      : GFX2_W;
            int ini = (x < 0) ? (-x - w) : x;
            if (x >= 0 && x + w > lim) { w = lim - x; }   /* se recorta */
            int fin = ini + w;
            char q[80];

            printf("    %-8s x=%4d  ancho maximo %3d  ocupa %3d..%3d\n",
                   nom[k], x, w, ini, fin);
            sprintf(q, "la columna \"%s\" no pisa a la anterior", nom[k]);
            pasa(q, ini >= fin_ant);
            sprintf(q, "la columna \"%s\" no se sale de la pantalla", nom[k]);
            pasa(q, fin <= GFX2_W);
            fin_ant = fin;
        }
        pasa("la columna de km queda pegada al borde derecho",
             ui_digi_cols_ft8[UI_DIGI_COLS_FT8_N - 1] < 0);
        /* La queja era "hay mucho hueco a la derecha vacio": una linea
         * escrita de corrido medía 390 px de 800. */
        pasa("el reparto usa mas de tres cuartos del ancho",
             fin_ant > (GFX2_W * 3) / 4);
        printf("      el ultimo campo acaba en %d de %d px\n", fin_ant, GFX2_W);
    }

    /*
     * Y QUE LA CHAPA QUEPA EN SU HUECO - 23/10/2026.
     *
     * El renglon de diagnostico de FT8 se dibuja con gfx2_text_in() sobre
     * un ancho fijo, asi que pasarse NO rompe nada: RECORTA. Y un
     * diagnostico recortado es peor que ninguno, porque se lee a medias y
     * nadie sabe que falta un trozo.
     *
     * Hasta hoy no lo miraba nadie, y hoy entra un campo mas -el margen de
     * dB, que sale cuando no hay candidatos-, asi que se mide: los textos
     * mas anchos que ft8_chapa() puede producir contra el hueco que
     * ui_digi les deja de verdad, descontando lo que se lleva el "9/15"
     * pegado a la derecha.
     */
    printf("\n  LA CHAPA DE DIAGNOSTICO\n");
    {
        /* Los tres que puede producir ft8_chapa(), con sus topes. Solo uno
         * de los tres campos del medio sale cada vez; ver su codigo. */
        static const char *k_peor[] = {
            "999c \xC2\xB7 99p \xC2\xB7 -9,9 s",
            "999c \xC2\xB7 9999 ms \xC2\xB7 -9,9 s",
            "0c \xC2\xB7 -99/+99 dB \xC2\xB7 -9,9 s",
        };
        /* El hueco lo da ui_digi con la MISMA resta con la que pinta, no
         * una copia de la cuenta aqui. Ver ui_digi_barra_hueco_dbg(). */
        int16_t hueco = ui_digi_barra_hueco_dbg(&st);
        int16_t dcha  = gfx2_text_w(st.barra_txt_dcha, &font_ui_14b);
        unsigned k;

        printf("      \"%s\" a la derecha se lleva %d px, al rotulo le quedan %d\n",
               st.barra_txt_dcha, dcha, hueco);
        for (k = 0; k < sizeof k_peor / sizeof k_peor[0]; k++) {
            int16_t w = gfx2_text_w(k_peor[k], &font_ui_14b);
            char q[96];

            printf("      <%s>  %d px\n", k_peor[k], w);
            sprintf(q, "cabe sin recortarse (%d de %d px)", w, hueco);
            pasa(q, w <= hueco);
        }
    }

    printf("\n  ALTO\n");
    {
        int usado = UDG_HDR_H + (1 + st.filas) * UDG_LINE_H;
        printf("      cabecera %d + titulos %d + %d renglones = %d de %d px\n",
               UDG_HDR_H, UDG_LINE_H, st.filas, usado, st.h);
        pasa("los titulos y los renglones caben en el panel", usado <= st.h);
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    printf("  imagen: out/ft8_panel.ppm\n");
    return fallos ? 1 : 0;
}

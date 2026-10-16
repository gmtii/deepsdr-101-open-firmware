/*
 * Render y comprobacion de la pantalla de bandas con columna de familias.
 *
 * La lista sale de User/main.c (tools/bandas_dump.py), asi que el render no
 * puede enseñar una y la placa otra. Comprueba ademas que ninguna familia
 * pase de las celdas que hay y que ningun texto se salga de la suya.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_cfg.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "bandas_tabla.h"
#include <stdio.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    if (!ok) { fallos++; }
    printf("  %-40s %4d px en %4d  %s\n", que, w, hueco, ok ? "OK" : "NO CABE");
}
static void fondo(gfx2_surf_t *s, void *c){ (void)c; gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }

int main(int argc, char **argv)
{
    ui_cfg_state_t st = {0};
    int sel = (argc > 1) ? atoi(argv[1]) : 0;
    int i, n = 0, first = -1, cats = 0;
    int pn = 0, pr = 0, pf = 0;
    const char *qn = "", *qr = "", *qf = "";
    const int16_t hueco = UIC_DCELL_W - 10;
    const int celdas = UIC_DCOLS * UIC_DROWS;

    for (i = 0; i < N_BANDAS; i++) { if (banda[i].familia + 1 > cats) cats = banda[i].familia + 1; }
    printf("Bandas: %d en %d familias, %d celdas por familia\n\n", N_BANDAS, cats, celdas);

    for (i = 0; i < N_BANDAS; i++) {
        /*
         * LOS DOS NOMBRES, no solo el español - 30/09/2026. Casi todas las
         * bandas se llaman igual en los dos idiomas, pero no todas ("OL" es
         * "LW"), y medir solo uno es medir medio problema. La tabla la saca
         * tools/bandas_dump.py, que desde hoy emite los dos.
         */
        int wn = gfx2_text_w(banda[i].nombre, &font_ui_18b);
        int we = gfx2_text_w(banda[i].nombre_en, &font_ui_18b);
        int wr = gfx2_text_w(banda[i].rango,  &font_ui_14);
        if (wn > pn) { pn = wn; qn = banda[i].nombre; }
        if (we > pn) { pn = we; qn = banda[i].nombre_en; }
        if (wr > pr) { pr = wr; qr = banda[i].rango; }
        if (wn > hueco) { printf("  NOMBRE DESBORDA: \"%s\" %d px\n", banda[i].nombre, wn); fallos++; }
        if (we > hueco) { printf("  NOMBRE DESBORDA (en): \"%s\" %d px\n", banda[i].nombre_en, we); fallos++; }
        if (wr > hueco) { printf("  RANGO DESBORDA:  \"%s\" %d px\n", banda[i].rango,  wr); fallos++; }
    }
    for (i = 0; i < cats; i++) {
        int w = gfx2_text_w(familia_nombre[i], &font_ui_18b);
        if (w > pf) { pf = w; qf = familia_nombre[i]; }
    }
    printf("  nombre mas ancho: \"%s\"\n", qn);
    cabe("  cabe en la celda", (int16_t)pn, hueco);
    printf("  rango mas ancho:  \"%s\"\n", qr);
    cabe("  cabe en la celda", (int16_t)pr, hueco);
    printf("  familia mas ancha: \"%s\"\n", qf);
    cabe("  cabe en la columna", (int16_t)pf, (int16_t)(UIC_SIDE_W - 26));

    for (i = 0; i < cats; i++) {
        int c = 0, k;
        for (k = 0; k < N_BANDAS; k++) { if (banda[k].familia == i) c++; }
        if (c > celdas) { printf("  la familia %s tiene %d bandas\n", familia_nombre[i], c); fallos++; }
        printf("  %-15s %2d bandas\n", familia_nombre[i], c);
    }

    cabe("la rejilla densa cabe a lo ancho",
         (int16_t)(UIC_GRID_X + UIC_DCOLS * UIC_DCELL_W + (UIC_DCOLS - 1) * UIC_DGAP), GFX2_W);
    cabe("la rejilla densa cabe a lo alto",
         (int16_t)(UIC_GRID_Y + UIC_DROWS * UIC_DCELL_H + (UIC_DROWS - 1) * UIC_DGAP),
         (int16_t)(UIG_Y + UIG_H));
    cabe("objetivo minimo de dedo (47 px)", 47, UIC_DCELL_H);
    for (i = 0; i < N_BANDAS; i++) {
        /*
         * El techo era 180 MHz y APRS 70 esta en 432,5 (paquete de la banda
         * de 70 cm, anadido a proposito). Se sube a 500 MHz: esto esta aqui
         * para cazar un cero de mas o de menos al teclear una frecuencia, no
         * para opinar sobre hasta donde llega el sintonizador.
         */
        if (banda[i].hz < 30000UL || banda[i].hz > 500000000UL) {
            printf("  FUERA DE RANGO: %s\n", banda[i].nombre); fallos++;
        }
    }

    for (i = 0; i < N_BANDAS; i++) { if (banda[i].familia == sel) { if (first < 0) first = i; n++; } }
    for (i = 0; i < UIC_CATS; i++) { st.cat[i] = (i < cats) ? familia_nombre[i] : ""; }
    st.cats = (uint8_t)cats; st.denso = 1; st.cat_sel = (uint8_t)sel;
    st.n = (uint8_t)n; st.cursor = 1; st.pressed = -1; st.marcada = 4;
    for (i = 0; i < UIC_CELLS; i++) {
        if (i < n) { st.cel[i].nombre = banda[first + i].nombre;
                     st.cel[i].valor  = banda[first + i].rango;
                     st.cel[i].extra  = banda[first + i].modo; }
        else       { st.cel[i].nombre = ""; st.cel[i].valor = ""; st.cel[i].extra = ""; }
    }

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_cfg_draw(&st);
    lcd_sim_write_ppm("out/bandas2.ppm");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

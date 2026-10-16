/*
 * Render y comprobacion de la pantalla de ajustes con columna de categorias.
 *
 * Comprueba lo que no se ve mirando: que ningun nombre ni ningun valor se
 * salga de su celda con su peor caso, que ninguna categoria pase de
 * UIC_CELLS ajustes, y que la columna y la rejilla no se solapen.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_cfg.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;
/* Idioma del RENDER (el segundo argumento). Las comprobaciones de ancho
 * miden siempre los dos; esto solo elige cual se dibuja en el .ppm. */
static int s_idioma_render = 0;

/*
 * La lista de ajustes y de paginas NO se escribe aqui: la genera
 * tools/gen_ajustes.py leyendo k_ajustes[] y k_aj_paginas[] de main.c. Lo
 * que si es cosa de este banco es el PEOR CASO de cada valor -la cadena mas
 * larga que esa celda puede llegar a enseñar-, que el firmware no declara en
 * ningun sitio porque la construye al vuelo.
 *
 * Un ajuste nuevo sin peor caso declarado hace fallar el banco. Es a
 * proposito: lo contrario seria medir una celda menos y no enterarse.
 */
#include "ajustes_tabla.h"

#include "peores.h"

static const peor_t *peor_de(const char *nombre)
{
    unsigned k;
    for (k = 0; k < sizeof(k_peores) / sizeof(k_peores[0]); k++) {
        if (strcmp(k_peores[k].nombre, nombre) == 0) { return &k_peores[k]; }
    }
    return 0;
}

typedef struct { const char *nombre[AJ_TABLA_IDIOMAS];
                 const char *peor[AJ_TABLA_IDIOMAS]; int cat; } aj_t;
static aj_t aj[AJ_TABLA_N];
static int N_AJ = 0;

static void arma_lista(void)
{
    int i;
    for (i = 0; i < AJ_TABLA_N; i++) {
        /* El peor caso se busca SIEMPRE por el nombre espanol: es la clave
         * de k_peores[], y asi traducir un ajuste no le deja sin medir. */
        const peor_t *p = peor_de(k_aj_tabla[i].nombre[0]);
        int l;
        if (p == 0) {
            printf("  *** el ajuste \"%s\" no tiene peor caso declarado en este banco ***\n",
                   k_aj_tabla[i].nombre[0]);
            fallos++;
        }
        for (l = 0; l < AJ_TABLA_IDIOMAS; l++) {
            aj[N_AJ].nombre[l] = k_aj_tabla[i].nombre[l];
            /* Una traduccion vacia seria un hueco en blanco en la pantalla
             * que este banco mediria como 0 px y aprobaria tan contento. */
            if (aj[N_AJ].nombre[l] == 0 || aj[N_AJ].nombre[l][0] == '\0') {
                printf("  *** el ajuste \"%s\" no tiene texto en el idioma %d ***\n",
                       k_aj_tabla[i].nombre[0], l);
                fallos++;
            }
        }
        aj[N_AJ].peor[0] = (p != 0) ? p->peor    : "";
        aj[N_AJ].peor[1] = (p != 0) ? p->peor_en : "";
        aj[N_AJ].cat    = k_aj_tabla[i].pagina;
        N_AJ++;
    }
}


static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    if (!ok) { fallos++; }
    printf("  %-38s %4d px en %4d  %s\n", que, w, hueco, ok ? "OK" : "NO CABE");
}
static void fondo(gfx2_surf_t *s, void *c){ (void)c; gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }

int main(int argc, char **argv)
{
    ui_cfg_state_t st = {0};
    int sel = (argc > 1) ? atoi(argv[1]) : 0;
    s_idioma_render = (argc > 2) ? atoi(argv[2]) : 0;
    if (s_idioma_render < 0 || s_idioma_render >= AJ_TABLA_IDIOMAS) { s_idioma_render = 0; }
    int i, n = 0, first = -1;
    int pn = 0, pv = 0, pc = 0;
    const char *qn = "", *qv = "", *qc = "";
    const int16_t hueco = UIC_CELL_W - 28;

    arma_lista();
    printf("Ajustes: %d en %d categorias, %d celdas por categoria\n\n",
           N_AJ, AJ_TABLA_PAGINAS, UIC_CELLS);

    /*
     * LOS DOS IDIOMAS, no solo el espanol. gfx2_text_in() alinea dentro del
     * ancho pero NO recorta -ver gfx2.c-, asi que una traduccion inglesa
     * mas larga que su celda no se corta: se pinta encima de la celda de al
     * lado. Medir solo el espanol seria aprobar la mitad de la pantalla.
     */
    for (i = 0; i < N_AJ; i++) {
        int l;
        for (l = 0; l < AJ_TABLA_IDIOMAS; l++) {
            int wn = gfx2_text_w(aj[i].nombre[l], &font_ui_14);
            if (wn > pn) { pn = wn; qn = aj[i].nombre[l]; }
        }
        for (l = 0; l < AJ_TABLA_IDIOMAS; l++) {
            int wv = gfx2_text_w(aj[i].peor[l], &font_ui_18b);
            if (wv > pv) { pv = wv; qv = aj[i].peor[l]; }
        }
    }
    for (i = 0; i < AJ_TABLA_PAGINAS; i++) {
        int l;
        for (l = 0; l < AJ_TABLA_IDIOMAS; l++) {
            int w = gfx2_text_w(k_aj_pag_nombres[i][l], &font_ui_18b);
            if (w > pc) { pc = w; qc = k_aj_pag_nombres[i][l]; }
        }
    }
    printf("  nombre mas ancho:    \"%s\"\n", qn);
    cabe("  cabe en la celda", (int16_t)pn, hueco);
    printf("  valor mas ancho:     \"%s\"\n", qv);
    cabe("  cabe en la celda", (int16_t)pv, hueco);
    printf("  categoria mas ancha: \"%s\"\n", qc);
    cabe("  cabe en la columna", (int16_t)pc, (int16_t)(UIC_SIDE_W - 26));

    for (i = 0; i < AJ_TABLA_PAGINAS; i++) {
        int c = 0, k;
        for (k = 0; k < N_AJ; k++) { if (aj[k].cat == i) c++; }
        /*
         * UIC_CELLS son 16, pero la rejilla NO densa solo dibuja
         * UIC_COLS*UIC_ROWS = 9: ui_cfg.c corta con `if (i >= st->n)
         * return;` y cfg_fill() llena hasta 16. Una decima fila de una
         * pagina se rellenaria en el estado, no se dibujaria y no seria
         * tocable, sin que nada avisara. Ese es el limite que hay que
         * vigilar, no el del buffer.
         */
        if (c > UIC_COLS * UIC_ROWS) {
            printf("  *** la categoria %s tiene %d ajustes y solo se dibujan %d ***\n",
                   k_aj_pag_nombres[i][0], c, UIC_COLS * UIC_ROWS);
            fallos++;
        }
        printf("  %-9s %d de %d ajustes\n", k_aj_pag_nombres[i][0], c, UIC_COLS * UIC_ROWS);
    }

    cabe("la columna no toca la rejilla",
         (int16_t)(UIC_SIDE_X + UIC_SIDE_W), (int16_t)(UIC_GRID_X - 4));
    cabe("la rejilla cabe a lo ancho",
         (int16_t)(UIC_GRID_X + UIC_COLS * UIC_CELL_W + (UIC_COLS - 1) * UIC_GAP), GFX2_W);
    cabe("la rejilla cabe a lo alto",
         (int16_t)(UIC_GRID_Y + UIC_ROWS * UIC_CELL_H + (UIC_ROWS - 1) * UIC_GAP),
         (int16_t)(UIG_Y + UIG_H));
    cabe("las categorias caben a lo alto",
         (int16_t)(UIC_CAT_Y + AJ_TABLA_PAGINAS * UIC_CAT_H_DE(AJ_TABLA_PAGINAS) + (AJ_TABLA_PAGINAS - 1) * UIC_CAT_GAP),
         (int16_t)(UIG_Y + UIG_H));
    cabe("objetivo minimo de dedo en la categoria", 47, UIC_CAT_H_DE(AJ_TABLA_PAGINAS));
    cabe("objetivo minimo de dedo en la celda (47 px)", 47, UIC_CELL_H);

    for (i = 0; i < N_AJ; i++) { if (aj[i].cat == sel) { if (first < 0) first = i; n++; } }
    for (i = 0; i < AJ_TABLA_PAGINAS; i++) { st.cat[i] = k_aj_pag_nombres[i][s_idioma_render]; }
    st.cat_sel = (uint8_t)sel; st.n = (uint8_t)n;
    st.cats = AJ_TABLA_PAGINAS; st.denso = 0; st.marcada = 0xFF;
    st.cursor = 2; st.pressed = -1;
    for (i = 0; i < UIC_CELLS; i++) {
        if (i < n) { st.cel[i].nombre = aj[first + i].nombre[s_idioma_render];
                     st.cel[i].valor  = aj[first + i].peor[s_idioma_render];
                     st.cel[i].extra = 0; }
        else       { st.cel[i].nombre = ""; st.cel[i].valor = ""; st.cel[i].extra = 0; }
    }

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_cfg_draw(&st);
    lcd_sim_write_ppm("out/config.ppm");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

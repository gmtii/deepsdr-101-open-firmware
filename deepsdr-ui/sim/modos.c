/*
 * Render y comprobacion de la pantalla de MODO con columna de categorias.
 *
 * La pantalla de modo se paso a la columna el 24/09/2026, pedido por el
 * usuario: igual que la de bandas, en categorias de analogico y digital.
 * Este banco comprueba lo que no se ve mirando el codigo:
 *
 *   - que ninguna categoria pase de UIC_CELLS modos, porque pasarse no
 *     rompe nada visible al instante -no hay paginacion en esta pantalla-
 *     sino que ESCONDE los modos de mas, que es la peor forma de fallar;
 *   - que ninguna etiqueta ni ninguna descripcion se salga de su celda, que
 *     en el reparto denso son 150 px y las descripciones son frases;
 *   - que los nombres de las dos categorias quepan en la columna.
 *
 * Y pinta las dos categorias en out/modos_*.ppm para poder MIRARLAS, que en
 * una pantalla es media medida.
 *
 * La tabla no se escribe aqui: la genera tools/gen_modos.py leyendo
 * k_demod_modes[] y k_modo_familias[] de main.c. Ver su comentario.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_cfg.h"
#include "ui_grid.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <string.h>

#include "modos_tabla.h"

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

/* El demodulador sobre el que va el modo i, o "" si su etiqueta ya lo
 * dice. Copia de modo_base_nombre() de main.c, sobre la tabla generada:
 * si las dos dejaran de coincidir, el ancho medido aqui dejaria de ser el
 * que se pinta alli, asi que se comprueba abajo que ninguna se salga. */
static const char *base_de(int k)
{
    int m;
    for (m = 0; m < MODO_TABLA_N; m++) {
        if (k_modo_tabla[m].fam != 0) { continue; }
        if (strcmp(k_modo_tabla[m].demod, k_modo_tabla[k].demod) != 0) { continue; }
        return (m == k) ? "" : k_modo_tabla[m].label;
    }
    return "";
}

static void cabe(const char *que, int16_t mide, int16_t hueco)
{
    int ok = (mide <= hueco);
    printf("  %-44s %4d px en %4d  %s\n", que, mide, hueco, ok ? "OK" : "SE SALE");
    if (!ok) { fallos++; }
}

static void fondo(gfx2_surf_t *s, void *c){ (void)c; gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }

int main(void)
{
    ui_cfg_state_t st = {0};
    int i, f;

    printf("pantalla de MODO: %d modos en %d categorias, %d celdas por categoria\n\n",
           MODO_TABLA_N, MODO_TABLA_FAMS, UIC_CELLS);

    /* 1. cuantos caben. */
    for (f = 0; f < MODO_TABLA_FAMS; f++) {
        int n = 0;
        for (i = 0; i < MODO_TABLA_N; i++) { if (k_modo_tabla[i].fam == f) { n++; } }
        printf("  %-12s %d modos", k_modo_fam_nombres[f][0], n);
        if (n > UIC_CELLS) { printf("   *** SE PASA DE %d: LOS DE MAS NO SE VEN ***\n", (int)UIC_CELLS); fallos++; }
        printf("\n");
    }
    printf("\n");

    /* 2. los anchos, en el reparto denso (el que usa esta pantalla). */
    {
        int pe = 0, pd = 0, pc = 0;
        const char *qe = "", *qd = "", *qc = "";
        for (i = 0; i < MODO_TABLA_N; i++) {
            int we = gfx2_text_w(k_modo_tabla[i].label, &font_ui_18b);
            int l;
            if (we > pe) { pe = we; qe = k_modo_tabla[i].label; }
            /* Los DOS idiomas: gfx2_text_in() alinea pero no recorta, asi
             * que una descripcion inglesa mas larga se derrama sobre la
             * celda de al lado en vez de cortarse. */
            for (l = 0; l < MODO_TABLA_IDIOMAS; l++) {
                int wd = gfx2_text_w(k_modo_tabla[i].desc[l], &font_ui_14);
                if (wd > pd) { pd = wd; qd = k_modo_tabla[i].desc[l]; }
            }
        }
        for (f = 0; f < MODO_TABLA_FAMS; f++) {
            int l2;
            for (l2 = 0; l2 < MODO_TABLA_IDIOMAS; l2++) {
                int w = gfx2_text_w(k_modo_fam_nombres[f][l2], &font_ui_18b);
                if (w > pc) { pc = w; qc = k_modo_fam_nombres[f][l2]; }
            }
        }
        printf("  etiqueta mas ancha:    \"%s\"\n", qe);
        cabe("  cabe en la celda densa", (int16_t)pe, UIC_DCELL_W - 8);
        printf("  descripcion mas ancha: \"%s\"\n", qd);
        cabe("  cabe en la celda densa", (int16_t)pd, UIC_DCELL_W - 8);
        {
            int pb = 0; const char *qb = "";
            for (i = 0; i < MODO_TABLA_N; i++) {
                int wb = gfx2_text_w(base_de(i), &font_ui_14b);
                if (wb > pb) { pb = wb; qb = base_de(i); }
            }
            printf("  tercer renglon mas ancho: \"%s\"\n", qb);
            cabe("  cabe en la celda densa", (int16_t)pb, UIC_DCELL_W - 8);
        }
        printf("  categoria mas ancha:   \"%s\"\n", qc);
        cabe("  cabe en la columna", (int16_t)pc, (int16_t)(UIC_SIDE_W - 26));
    }

    /* 3. pintarlas. */
    for (f = 0; f < MODO_TABLA_FAMS; f++) {
        char ruta[64];
        int n = 0;

        /*
         * [i][0], no [i]. La tabla es BILINGUE -dos cadenas por familia- asi
         * que k_modo_fam_nombres[i] no es una cadena, es el par. Esto
         * asignaba un const char *const* a un const char * y el compilador
         * lo decia desde hace tiempo ("incompatible pointer type"): las
         * pestanas de las capturas salian con lo que hubiera en esa
         * direccion. Se arregla en la revision del 11/10/2026, que es
         * cuando por fin alguien leyo el aviso.
         */
        for (i = 0; i < MODO_TABLA_FAMS; i++) { st.cat[i] = k_modo_fam_nombres[i][0]; }
        st.cats = MODO_TABLA_FAMS;
        st.cat_sel = (uint8_t)f;
        st.denso = 1;
        st.cursor = 1;
        st.pressed = -1;
        st.marcada = 0xFF;

        for (i = 0; i < MODO_TABLA_N && n < UIC_CELLS; i++) {
            if (k_modo_tabla[i].fam != f) { continue; }
            st.cel[n].nombre = k_modo_tabla[i].label;
            st.cel[n].valor  = k_modo_tabla[i].desc[0];
            /* El tercer renglon: el demodulador de debajo, y solo cuando
             * no es evidente - la misma regla que modo_base_nombre() en
             * main.c, sacada de la misma tabla generada. */
            st.cel[n].extra = base_de(i);
            if (n == 0) { st.marcada = (uint8_t)n; }
            n++;
        }
        st.n = (uint8_t)n;
        for (i = n; i < UIC_CELLS; i++) {
            st.cel[i].nombre = ""; st.cel[i].valor = ""; st.cel[i].extra = "";
        }

        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_cfg_draw(&st);
        snprintf(ruta, sizeof ruta, "out/modos_%d.ppm", f);
        lcd_sim_write_ppm(ruta);
        printf("  %s pintada en %s\n", k_modo_fam_nombres[f][0], ruta);
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

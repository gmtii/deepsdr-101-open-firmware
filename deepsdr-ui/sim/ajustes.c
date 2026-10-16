/*
 * Render y comprobacion de anchos de las pantallas de rejilla.
 *
 * Mide CADA nombre de ajuste y su valor mas largo con la fuente real contra
 * el ancho real de la celda. Un nombre que no cabe no se ve cortado: se ve
 * desbordado sobre el de al lado, que es peor.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_grid.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

typedef struct { const char *nombre, *peor; int pagina; } aj_t;

/* Los 27 ajustes con su valor MAS LARGO posible, no uno cualquiera. */
static const aj_t aj[] = {
    { "AGC",            "Sin AGC",       0 },
    { "Ancho (BW)",     "4,0 kHz",       0 },
    { "Volumen",        "-47,5 dB",      0 },
    { "Silenciador",    "-120 dB",       0 },
    { "Reducción",      "100",           0 },
    { "Ganancia",       "-47,5 dB",      0 },
    { "AGC de RF",      "Apagado",       0 },
    { "Atenuador",      "-12 dB",        0 },

    { "Brillo",         "100 %",         1 },
    { "Escala",         "-110 / -20",    1 },
    { "Autoescala",     "Apagada",       1 },
    { "Zoom",           "8x",            1 },
    { "Paleta",         "Clásica verde", 1 },
    { "Estilo",         "Contorno",      1 },
    { "Traza",          "Del color",     1 },
    { "Suavizado",      "100 %",         1 },
    { "Contorno",       "3",             1 },

    { "Altavoz",        "Activo",        2 },
    { "Táctil",         "Sensible",      2 },
    { "Muestreo",       "96 kHz",        2 },
    { "Filtro FM",      "96 kHz",        2 },
    { "Reposo",         "",              2 },
    { "Calibrar táctil","",              2 },
    { "Calibrar PPM",   "",              2 },

    { "Desplazamiento", "1000 Hz",       3 },
    { "Baudios",        "45.45",         3 },
    { "Inversión",      "Invertida",     3 },
};
#define N_AJ ((int)(sizeof aj / sizeof aj[0]))
static const char *const pag[4] = { "Radio", "Pantalla", "Equipo", "Digital" };

static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    if (!ok) { fallos++; }
    printf("  %-34s %4d px en %4d  %s\n", que, w, hueco, ok ? "OK" : "NO CABE");
}

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(int argc, char **argv)
{
    ui_grid_state_t st = {0};
    int pagina = (argc > 1) ? atoi(argv[1]) : 0;
    int i, n = 0, first = -1;
    int peor_n = 0, peor_v = 0;
    const char *qn = "", *qv = "";
    const int16_t hueco = UIG_CELL_W - 10;

    printf("Ajustes: %d en %d paginas de hasta %d\n\n", N_AJ, 4, UIG_CELLS);

    for (i = 0; i < N_AJ; i++) {
        int wn = gfx2_text_w(aj[i].nombre, &font_ui_18b);
        int wv = gfx2_text_w(aj[i].peor,   &font_ui_14b);
        if (wn > peor_n) { peor_n = wn; qn = aj[i].nombre; }
        if (wv > peor_v) { peor_v = wv; qv = aj[i].peor; }
        if (wn > hueco) { printf("  NOMBRE DESBORDA: \"%s\" %d px\n", aj[i].nombre, wn); fallos++; }
        if (wv > hueco) { printf("  VALOR DESBORDA:  \"%s\" %d px\n", aj[i].peor,   wv); fallos++; }
    }
    printf("  nombre mas ancho: \"%s\"\n", qn);
    cabe("  cabe en la celda", (int16_t)peor_n, hueco);
    printf("  valor mas ancho:  \"%s\"\n", qv);
    cabe("  cabe en la celda", (int16_t)peor_v, hueco);

    /* ninguna pagina puede pasar de UIG_CELLS */
    for (i = 0; i < 4; i++) {
        int c = 0, k;
        for (k = 0; k < N_AJ; k++) { if (aj[k].pagina == i) c++; }
        if (c > UIG_CELLS) { printf("  la pagina %s tiene %d celdas\n", pag[i], c); fallos++; }
        printf("  pagina %-9s %2d ajustes\n", pag[i], c);
    }

    /* --- render --- */
    for (i = 0; i < N_AJ; i++) {
        if (aj[i].pagina == pagina) { if (first < 0) first = i; n++; }
    }
    st.titulo = pag[pagina];
    st.pagina = (uint8_t)pagina; st.paginas = 4; st.n = (uint8_t)n;
    st.pressed = -1; st.cursor = 1; st.marcada = 0xFF;
    st.nav[0] = "Anterior";  st.nav_on[0] = (uint8_t)(pagina > 0);
    st.nav[1] = "Siguiente"; st.nav_on[1] = (uint8_t)(pagina < 3);
    /* 22/09/2026: aqui habia un tercer boton "Cerrar" que ya no existe (ver
     * NAV_N en ui_grid.c). Seguia escribiendose en nav[2], o sea FUERA del
     * array de dos: escritura fuera de limites en el propio banco de pruebas. */
    for (i = 0; i < UIG_CELLS; i++) {
        if (i < n) { st.cel[i].l1 = aj[first + i].nombre;
                     st.cel[i].l2 = aj[first + i].peor;
                     st.cel[i].l3 = 0; }
        else       { st.cel[i].l1 = ""; st.cel[i].l2 = ""; st.cel[i].l3 = 0; }
    }

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_grid_draw(&st);
    lcd_sim_write_ppm("out/ajustes.ppm");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * PANTALLA PRINCIPAL COMPLETA, con la geometria real del firmware.
 *
 * Todo lo que sale aqui es el mismo codigo que corre en la placa:
 *   - cabecera y barra de estado : User/ui_top.c
 *   - traza del espectro         : User/spectrum.c (el de verdad, con su EMA,
 *                                  su peak-hold y su suavizado)
 *   - ejes y leyenda             : User/spec_chrome.c
 *   - barra de acciones          : User/ui_act.c
 *
 * Lo unico inventado son los datos de la radio.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "ui_top.h"
#include "ui_act.h"
#include "specdata.h"
#include "temas_tabla.h"
#include <stdio.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;

/* Tema por argumento (23/09/2026): el 5o argumento es el INDICE en la tabla
 * de temas, que se genera de main.c (ver tools/gen_temas.py). Poner la
 * paleta de interfaz y la del waterfall a la vez es lo unico que permite
 * mirar un tema y ver lo que se va a ver en la radio: la mitad de la
 * pantalla es waterfall, y una interfaz clara con un waterfall oscuro no se
 * parece en nada a la misma interfaz con uno claro. */
static void tema_sim(int i)
{
    if (i < 0 || i >= SIM_TEMA_COUNT) { return; }
    g_pal = k_sim_temas[i].pal;
    spectrum_set_palette(k_sim_temas[i].wf);
    fprintf(stderr, "  tema %d: %s\n", i, k_sim_temas[i].nombre);
}

#define FFT_BINS 796
static float   g_db[FFT_BINS];
static uint8_t g_wf[SPC_WF_ROWS * SPC_TRACE_W];

static float DB_MIN = -110.0f;
static float DB_MAX =  -20.0f;

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(int argc, char **argv)
{
    /*
     * = {0} y no a secas, 23/09/2026. Estaba SIN inicializar, y el dia que
     * ui_top_state_t gano el campo "band" (la pastilla de banda de la
     * cabecera) este fichero se quedo sin ponerlo: basura en la pila, un
     * puntero a 0x2 y segmentation fault en cuanto se compilaba con -O2.
     * Con -O0 la pila salia a cero y no pasaba nada, que es la peor version
     * del fallo: funciona hasta que cambias una opcion del compilador.
     *
     * Es otra vez el mismo fallo de siempre -un dato nuevo en un sitio y un
     * sitio viejo que no se entera, sin error de compilacion-, y la unica
     * defensa barata es que un campo nuevo salga a cero en vez de a basura:
     * ui_top.c ya trata el NULL como "esto no se pinta".
     */
    ui_top_state_t  top = {0};
    ui_act_state_t  act = {0};
    spec_chrome_t   ch = {0};
    int16_t demod_px = (int16_t)(SPC_TRACE_W / 4);
    uint32_t span = 96000U, bw_hz = 4000U;
    int pulsado = (argc > 1) ? atoi(argv[1]) : -1;
    int r, i;

    if (argc > 3) { DB_MIN = (float)atof(argv[2]); DB_MAX = (float)atof(argv[3]); }
    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    spectrum_init();
    spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
    /* Paleta por argumento (22/09/2026): la demo original no llamaba a
     * spectrum_set_palette(), o sea que salia con la de por defecto de
     * spectrum.c, CLASSIC. Poder pedirla por linea de ordenes es lo que
     * permite renderizar las candidatas y comparar en vez de recordar. */
    if (argc > 4) { spectrum_set_palette((spectrum_palette_t)atoi(argv[4])); }
    if (argc > 5) { tema_sim(atoi(argv[5])); gfx2_render_screen(fondo, 0); }

    /* --- espectro --- */
    {
        int16_t bw_px = (int16_t)((uint32_t)SPC_TRACE_W * bw_hz / span);
        int pass;
        for (pass = 0; pass < 24; pass++) {
            specdata_fill(g_db, FFT_BINS, 1u);
            spectrum_draw(g_db, FFT_BINS, SPC_TRACE_X, SPC_TRACE_Y,
                          SPC_TRACE_W, SPC_TRACE_H, DB_MIN, DB_MAX, demod_px,
                          1U, (int16_t)(demod_px - bw_px), (int16_t)(demod_px + bw_px));
        }
    }

    /* --- waterfall --- */
    for (r = 0; r < SPC_WF_ROWS; r++) {
        specdata_fill(g_db, FFT_BINS, (unsigned)(r * 7 + 1));
        for (i = 0; i < SPC_TRACE_W; i++) {
            int src = (int)((long)i * FFT_BINS / SPC_TRACE_W);
            g_wf[r * SPC_TRACE_W + i] =
                spectrum_colormap_index(g_db[src], DB_MIN, DB_MAX);
        }
    }
    gfx2_wf_blit(SPC_TRACE_X, SPC_WF_Y, SPC_TRACE_W, SPC_WF_ROWS,
                 g_wf, SPC_TRACE_W, 0, spectrum_colormap_lut());

    /* --- ejes --- */
    ch.db_min = DB_MIN; ch.db_max = DB_MAX;
    ch.center_hz = 7200000U - span / 4U;   /* el panel va Fs/4 por debajo del VFO */
    ch.span_hz = span; ch.demod_px = demod_px;
    ch.band = SPC_BAND_BOTH; ch.band_hz = bw_hz;
    ch.cmap = spectrum_colormap_lut();
    /* Maqueta 10/10/2026: con DENTRO_DE_BANDA=0 se pinta el caso "te has
     * salido". Va ANTES de spec_chrome_draw(), que es donde se pinta la
     * raya: puesto despues no se veia nada y la maqueta mentia. */
#ifndef DENTRO_DE_BANDA
#define DENTRO_DE_BANDA 1
#endif
    ch.en_banda = (DENTRO_DE_BANDA != 9);
    /* 0..3 = las cuatro familias, 9 = fuera de banda. Ver spec_chrome_t. */
    ch.fam_banda = (uint8_t)DENTRO_DE_BANDA;
    spec_chrome_draw(&ch);

    /* --- cabecera y barra de estado --- */
    top.freq = "7.200,00";   /* lo que produce tune_freq_format_khz() */
    top.freq_digit = 4;            /* el digito de 1 kHz */
    top.mode = "AM";
    top.band = (DENTRO_DE_BANDA == 0) ? "40 m"
             : ((DENTRO_DE_BANDA == 1) ? "41 m"
             : ((DENTRO_DE_BANDA == 2) ? "NAVTEX"
             : ((DENTRO_DE_BANDA == 3) ? "2 m" : "Bandas")));
    top.en_banda  = (DENTRO_DE_BANDA != 9);
    top.fam_banda = (uint8_t)DENTRO_DE_BANDA;
    top.clock = "23:41";
    top.batt_pct = 72;

    top.sam_ppm = 0;
    top.s_units = 7;
    top.over_db = 0;
    top.dbm = -79;
    top.dbm_valid = 0;
    top.knob = UI_KNOB_TUNE;
    top.knob_value = "1 kHz";
    top.agc = "MED";
    top.bw = "4,0 k";
    top.att = "-06";
    top.nco_on = 1;
    top.nco_off = "+40k";
    top.ovr = 0;
    top.spk_muted = 0;
    ui_top_draw(&top);

    /* --- barra de acciones --- */
    {
        /* Mismo orden que k_act_slots[] en main.c */
        static const char *nom[UI_ACT_N] = { "Bandas", "Modo", "Paso",
                                             "Volumen", "Ruido", "Ajustes" };
        static const char *val[UI_ACT_N] = { 0, "AM", "1 kHz",
                                             "-12,0 dB", "activo", 0 };
        for (i = 0; i < UI_ACT_N; i++) {
            act.name[i] = nom[i];
            act.value[i] = val[i];
            act.active[i] = (uint8_t)(i == 4);
        }
        act.pressed = (int8_t)pulsado;
        ui_act_draw(&act);
    }

    {
        lcd_cost_t c = lcd_sim_cost();
        printf("pantalla principal completa\n");
        printf("  cabecera   y=  0..%d\n", UI_TOP_H - 1);
        printf("  estado     y=%3d..%d\n", UI_STATUS_Y, UI_STATUS_Y + UI_STATUS_H - 1);
        printf("  espectro   y=%3d..%d   traza x=%d..%d\n", SPC_Y, SPC_RULER_Y - 1,
               SPC_TRACE_X, SPC_TRACE_X + SPC_TRACE_W - 1);
        printf("  regla      y=%3d..%d\n", SPC_RULER_Y, SPC_RULER_Y + SPC_RULER_H - 1);
        printf("  waterfall  y=%3d..%d\n", SPC_WF_Y, SPC_WF_Y + SPC_WF_ROWS - 1);
        printf("  acciones   y=%3d..%d\n", UI_ACT_Y, UI_ACT_Y + UI_ACT_H - 1);
        printf("  ventanas EXMC=%llu  accesos=%llu\n",
               (unsigned long long)c.windows, (unsigned long long)c.bus_writes);
    }
    lcd_sim_write_ppm("out/principal.ppm");
    return 0;
}

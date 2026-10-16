/*
 * ETAPA 3b - render del panel de espectro con la geometria REAL del
 * firmware.
 *
 * Lo que hace este banco distinto de sim/after.c: la traza NO esta
 * reimplementada con gfx2. Se compila el User/spectrum.c del firmware tal
 * cual -EMA asimetrica, peak-hold, suavizado espacial, puentes entre
 * columnas, estilos, paletas- contra el panel simulado. Lo que sale en el
 * PNG es lo que va a salir en el panel, no algo parecido.
 *
 * Encima va el chrome de la etapa 3b (gfx2/spec_chrome.c), que es el mismo
 * fichero que se copia a User/.
 */

#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "wf_cmap.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "font_ui_14.h"
#include "specdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define FFT_BINS 796

static float  g_db[FFT_BINS];
static uint8_t g_wf[SPC_WF_ROWS * SPC_TRACE_W];

static const float DB_MIN = -110.0f;
static const float DB_MAX =  -20.0f;

/* ------------------------------------------------------------------ */
/* Waterfall: mismo camino que el firmware - indices de 8 bits + LUT.   */
static void wf_fill(void)
{
    int r, i;
    for (r = 0; r < SPC_WF_ROWS; r++) {
        specdata_fill(g_db, FFT_BINS, (unsigned)(r * 7 + 1));
        for (i = 0; i < SPC_TRACE_W; i++) {
            int src = (int)((long)i * FFT_BINS / SPC_TRACE_W);
            g_wf[r * SPC_TRACE_W + i] =
                spectrum_colormap_index(g_db[src], DB_MIN, DB_MAX);
        }
    }
}

/* Fondo de los paneles, por fuera de lo que dibujan spectrum.c y el
 * chrome: el hueco entre la traza y la regla, y el borde del waterfall. */
static void draw_bg(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(int argc, char **argv)
{
    spec_chrome_t st = {0};
    int modo = (argc > 1) ? atoi(argv[1]) : 0;   /* 0=AM 1=USB 2=LSB */
    int16_t demod_px;
    uint8_t band_active;
    int16_t band_lo, band_hi;
    uint32_t bw_hz = 4000U;
    uint32_t span  = 96000U;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(draw_bg, 0);

    spectrum_init();
    spectrum_set_style((argc > 2) ? (spectrum_style_t)atoi(argv[2]) : SPECTRUM_STYLE_HEATMAP);
    if (argc > 3) { spectrum_set_bridge((uint8_t)atoi(argv[3])); }

    /* El punto de demodulacion con low-IF cae en +Fs/4 = +W/4 exacto. */
    demod_px = (int16_t)(SPC_TRACE_W / 4);

    {
        int16_t bw_px = (int16_t)((uint32_t)SPC_TRACE_W * bw_hz / span);
        band_active = 1U;
        if (modo == 1) { band_lo = demod_px;             band_hi = (int16_t)(demod_px + bw_px); }
        else if (modo == 2) { band_lo = (int16_t)(demod_px - bw_px); band_hi = demod_px; }
        else { band_lo = (int16_t)(demod_px - bw_px);    band_hi = (int16_t)(demod_px + bw_px); }
    }

    /* Varias pasadas para que la EMA y el peak-hold lleguen a regimen:
     * una sola pasada dibujaria un transitorio, no lo que se ve de verdad. */
    {
        int pass;
        for (pass = 0; pass < 24; pass++) {
            specdata_fill(g_db, FFT_BINS, 1u);
            spectrum_draw(g_db, FFT_BINS,
                          SPC_TRACE_X, SPC_TRACE_Y, SPC_TRACE_W, SPC_TRACE_H,
                          DB_MIN, DB_MAX, demod_px,
                          band_active, band_lo, band_hi);
        }
    }

    wf_fill();
    gfx2_wf_blit(SPC_TRACE_X, SPC_WF_Y, SPC_TRACE_W, SPC_WF_ROWS,
                 g_wf, SPC_TRACE_W, 0, spectrum_colormap_lut());

    st.db_min    = DB_MIN;
    st.db_max    = DB_MAX;
    st.center_hz = 7200000U;
    st.span_hz   = span;
    st.demod_px  = demod_px;
    st.band      = (modo == 1) ? SPC_BAND_UPPER
                 : (modo == 2) ? SPC_BAND_LOWER : SPC_BAND_BOTH;
    st.band_hz   = bw_hz;
    st.cmap      = spectrum_colormap_lut();
    spec_chrome_draw(&st);

    {
        lcd_cost_t c = lcd_sim_cost();
        printf("etapa 3b, modo %s\n", modo == 1 ? "USB" : modo == 2 ? "LSB" : "AM");
        printf("  traza      x=%d..%d  (%d px)\n", SPC_TRACE_X,
               SPC_TRACE_X + SPC_TRACE_W - 1, SPC_TRACE_W);
        printf("  canaleta   x=0..%d\n", SPC_GUT_W - 1);
        printf("  regla      y=%d..%d\n", SPC_RULER_Y, SPC_RULER_Y + SPC_RULER_H - 1);
        printf("  ventanas EXMC=%llu  accesos=%llu\n",
               (unsigned long long)c.windows, (unsigned long long)c.bus_writes);
    }

    { char nm[64]; snprintf(nm, sizeof nm, "out/spec3b_e%d_p%d.ppm",
        (argc > 2) ? atoi(argv[2]) : 0, (argc > 3) ? atoi(argv[3]) : 1);
      lcd_sim_write_ppm(nm); }
    return 0;
}

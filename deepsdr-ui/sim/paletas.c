/*
 * BANCO DE PALETAS - 22/09/2026
 *
 * Renderiza la MISMA escena de espectro + waterfall (la geometria real
 * del firmware, compilando vendor/spectrum.c tal cual) una vez por cada
 * paleta de spectrum_palette_t, y escribe un PPM por paleta.
 *
 * Para que: el usuario recuerda una demo antigua en la que se notaban muy
 * bien los valles entre señales, y que esa paleta tenia puntitos.
 * En vez de adivinar cual era, se dibujan las 16 con los mismos datos
 * y se miran.
 *
 * Ademas mide, por paleta, el CONTRASTE DE VALLE: cuantos escalones de
 * luminancia hay entre el suelo de ruido y un valle 6 dB por encima.
 * Esa cifra es la que explica "se ven los valles", no la impresion.
 */

#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "font_ui_14.h"
#include "specdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define FFT_BINS 796

static float   g_db[FFT_BINS];
static uint8_t g_wf[SPC_WF_ROWS * SPC_TRACE_W];

static float DB_MIN = -110.0f;
static float DB_MAX =  -20.0f;

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

static void draw_bg(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

/* luminancia perceptual aproximada de un RGB565 */
static float lum565(uint16_t c)
{
    float r = (float)((c >> 11) & 0x1FU) / 31.0f;
    float g = (float)((c >>  5) & 0x3FU) / 63.0f;
    float b = (float)( c        & 0x1FU) / 31.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

int main(int argc, char **argv)
{
    int pal;
    int solo = (argc > 1) ? atoi(argv[1]) : -1;
    uint32_t span = 96000U;

    if (argc > 3) { DB_MIN = (float)atof(argv[2]); DB_MAX = (float)atof(argv[3]); }

    printf("paletas: rango %.0f..%.0f dB (%.0f dB de span)\n",
           (double)DB_MIN, (double)DB_MAX, (double)(DB_MAX - DB_MIN));
    printf("  n  contraste de valle (delta luminancia por 6 dB cerca del suelo)\n");

    for (pal = 0; pal < 16; pal++) {
        char nombre[64];
        spec_chrome_t st = {0};
        int16_t demod_px = (int16_t)(SPC_TRACE_W / 4);
        int16_t bw_px = (int16_t)((uint32_t)SPC_TRACE_W * 4000U / span);

        if (solo >= 0 && pal != solo) continue;

        lcd_sim_reset(0x0000);
        gfx2_render_screen(draw_bg, 0);

        spectrum_init();
        spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
        spectrum_set_palette((spectrum_palette_t)pal);

        {
            int pass;
            for (pass = 0; pass < 24; pass++) {
                specdata_fill(g_db, FFT_BINS, 1u);
                spectrum_draw(g_db, FFT_BINS,
                              SPC_TRACE_X, SPC_TRACE_Y, SPC_TRACE_W, SPC_TRACE_H,
                              DB_MIN, DB_MAX, demod_px,
                              1U, (int16_t)(demod_px - bw_px), (int16_t)(demod_px + bw_px));
            }
        }

        wf_fill();
        gfx2_wf_blit(SPC_TRACE_X, SPC_WF_Y, SPC_TRACE_W, SPC_WF_ROWS,
                     g_wf, SPC_TRACE_W, 0, spectrum_colormap_lut());

        st.db_min = DB_MIN; st.db_max = DB_MAX;
        st.center_hz = 7200000U; st.span_hz = span;
        st.demod_px = demod_px; st.band = SPC_BAND_BOTH; st.band_hz = 4000U;
        st.cmap = spectrum_colormap_lut();
        spec_chrome_draw(&st);

        /* Contraste de valle: el suelo sintetico esta en -98 dBFS y las
         * ondulaciones entre senales suben unos 6 dB. Lo que decide si
         * "se ven los valles" es cuanta luminancia cambia en ese tramo. */
        {
            float l0 = lum565(spectrum_colormap(-98.0f, DB_MIN, DB_MAX));
            float l1 = lum565(spectrum_colormap(-92.0f, DB_MIN, DB_MAX));
            float d  = l1 - l0;
            if (d < 0.0f) d = -d;
            printf("  %2d  %.4f\n", pal, (double)d);
        }

        snprintf(nombre, sizeof nombre, "out/pal%02d.ppm", pal);
        lcd_sim_write_ppm(nombre);
    }
    return 0;
}

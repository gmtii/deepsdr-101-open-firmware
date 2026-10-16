/*
 * Render del rediseno, sobre el mismo simulador y los mismos datos de
 * espectro que sim/before.c, para que la comparacion sea justa.
 *
 * Layout (800x480), pensado por frecuencia de uso y no por "donde habia
 * hueco":
 *
 *   y   0..71   CABECERA     lo que se mira siempre: frecuencia, modo, banda
 *   y  72..113  ESTADO       S-meter, destino del mando, chips de estado
 *   y 114..327  ESPECTRO     con banda de paso visible y ejes etiquetados
 *   y 328..417  WATERFALL    con leyenda de escala
 *   y 418..479  ACCIONES     seis botones, lo que se toca a menudo
 */

#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "wf_cmap.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_ui_18b.h"
#include "font_num_44.h"
#include "font_num_20.h"
#include "specdata.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define HDR_Y     0
#define HDR_H    72
#define ST_Y     72
#define ST_H     42
#define SP_Y    114
#define SP_H    214
#define WF_Y    328
#define WF_H     90
#define ACT_Y   418
#define ACT_H    62

#define WF_ROWS 84
#define WF_W    740

/* --- estado de la radio que se pinta ------------------------------------ */
typedef struct {
    const char *mhz;        /* parte entera + decimales, agrupada */
    int         digit_hi;   /* indice del caracter que mueve el mando */
    const char *mode;
    const char *band;
    const char *clock;
    int         volume;
    float       dbm;
    int         s_units;    /* 0..9 = S1..S9, >9 = S9+dB */
    int         over_db;
    const char *agc;
    int         audio_bw_hz;
    int         nr_on;
    int         att_db;
    const char *knob;       /* destino del mando */
    const char *knob_val;
} radio_t;

/* --- espectro compartido ------------------------------------------------ */
const palette_t *g_pal = &k_pal_oscura;

static float g_spec[SPEC_BINS];
static uint8_t g_wf[WF_ROWS * WF_W];

static const float DB_LO = -100.0f, DB_HI = -22.0f;

/* snprintf escribe el punto como separador decimal; en la UI en espanol
 * toca coma. Se cambia in situ, que es mas simple que tocar la locale. */
static char *dec_comma(char *s)
{
    char *p;
    for (p = s; *p; p++) { if (*p == '.') *p = ','; }
    return s;
}

static void build_data(void)
{
    int r, i;
    specdata_fill(g_spec, SPEC_BINS, 7);

    for (r = 0; r < WF_ROWS; r++) {
        static float line[SPEC_BINS];
        specdata_fill(line, SPEC_BINS, (unsigned)(7 + r * 13));
        for (i = 0; i < WF_W; i++) {
            /* remuestreo simple de SPEC_BINS a WF_W */
            int src = (int)((int64_t)i * SPEC_BINS / WF_W);
            float t = (line[src] - DB_LO) / (DB_HI - DB_LO);
            int v;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            v = (int)(t * 255.0f + 0.5f);
            g_wf[r * WF_W + i] = (uint8_t)v;
        }
    }
}

/* ======================================================================
 * CABECERA
 * ====================================================================== */
static void draw_header(gfx2_surf_t *s, const radio_t *R)
{
    gfx2_rgba_t ink   = gfx2_rgb(PAL_INK);
    gfx2_rgba_t dim   = gfx2_rgb(PAL_INK_DIM);
    gfx2_rgba_t acc   = gfx2_rgb(PAL_ACCENT);
    int16_t x, i;
    const char *p;

    gfx2_vgrad(s, 0, HDR_Y, GFX2_W, HDR_H,
               gfx2_rgb(0x1B2027), gfx2_rgb(PAL_SURF_1));
    gfx2_hline(s, 0, (int16_t)(HDR_Y + HDR_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));

    /* --- lector de frecuencia, cifras tabulares ---
     * El digito que mueve el mando va SUBRAYADO en ambar. Asi el paso de
     * sintonia se lee en el propio numero y no hace falta un "STEP 1kHz"
     * aparte que obliga a traducir mentalmente. */
    x = 18;
    for (p = R->mhz, i = 0; *p; p++, i++) {
        char buf[2] = { *p, 0 };
        int16_t aw = gfx2_text_w(buf, &font_num_44);
        int hi = (i == R->digit_hi);
        gfx2_text(s, x, 8, buf, &font_num_44, hi ? acc : ink);
        if (hi) {
            gfx2_fill(s, x, 56, (int16_t)(aw - 2), 3, acc);
        }
        x = (int16_t)(x + aw);
    }
    gfx2_text(s, (int16_t)(x + 8), 30, "MHz", &font_ui_18b, dim);

    /* --- modo y banda: dos chips, el modo con acento --- */
    {
        int16_t cx = 470;
        int16_t w1 = (int16_t)(gfx2_text_w(R->mode, &font_ui_18b) + 26);
        gfx2_rrect(s, cx, 14, w1, 32, 6, gfx2_rgba(PAL_ACCENT, 42));
        gfx2_rrect_outline(s, cx, 14, w1, 32, 6, 1, gfx2_rgba(PAL_ACCENT, 170));
        gfx2_text_in(s, cx, 20, w1, R->mode, &font_ui_18b, acc, GFX2_ALIGN_C);

        cx = (int16_t)(cx + w1 + 8);
        {
            int16_t w2 = (int16_t)(gfx2_text_w(R->band, &font_ui_14b) + 22);
            gfx2_rrect(s, cx, 16, w2, 28, 6, gfx2_rgb(PAL_SURF_2));
            gfx2_text_in(s, cx, 21, w2, R->band, &font_ui_14b, dim, GFX2_ALIGN_C);
        }
    }

    /* --- reloj y bateria, alineados a la derecha --- */
    gfx2_text_in(s, 600, 14, 120, R->clock, &font_num_20, ink, GFX2_ALIGN_R);
    {
        int16_t bx = 730, by = 18, bw = 46, bh = 20;
        gfx2_rrect_outline(s, bx, by, bw, bh, 4, 2, gfx2_rgb(PAL_INK_MUTE));
        gfx2_rrect(s, (int16_t)(bx + bw), (int16_t)(by + 6), 4, 8, 2,
                   gfx2_rgb(PAL_INK_MUTE));
        gfx2_rrect(s, (int16_t)(bx + 4), (int16_t)(by + 4), 28, 12, 2,
                   gfx2_rgb(PAL_OK));
        gfx2_text_in(s, 700, 44, 92, "3,94 V", &font_ui_14, dim, GFX2_ALIGN_R);
    }
}

/* ======================================================================
 * BARRA DE ESTADO
 * ====================================================================== */
static void chip(gfx2_surf_t *s, int16_t x, int16_t y, const char *label,
                 const char *value, int active)
{
    int16_t lw = gfx2_text_w(label, &font_ui_14);
    int16_t vw = value ? gfx2_text_w(value, &font_ui_14b) : 0;
    int16_t w  = (int16_t)(lw + (value ? vw + 7 : 0) + 20);

    /* Activo = RELLENO, inactivo = contorno. El estado no depende del color:
     * asi sigue leyendose con cualquier tipo de daltonismo. */
    if (active) {
        gfx2_rrect(s, x, y, w, 26, 6, gfx2_rgb(PAL_SURF_3));
        gfx2_rrect_outline(s, x, y, w, 26, 6, 1, gfx2_rgb(PAL_LINE));
    } else {
        gfx2_rrect_outline(s, x, y, w, 26, 6, 1, gfx2_rgb(PAL_LINE));
    }
    gfx2_text(s, (int16_t)(x + 10), (int16_t)(y + 5), label, &font_ui_14,
              gfx2_rgb(active ? PAL_INK_DIM : PAL_INK_MUTE));
    if (value) {
        gfx2_text(s, (int16_t)(x + 10 + lw + 7), (int16_t)(y + 5), value,
                  &font_ui_14b, gfx2_rgb(active ? PAL_INK : PAL_INK_MUTE));
    }
}

static int16_t chip_w(const char *label, const char *value)
{
    return (int16_t)(gfx2_text_w(label, &font_ui_14)
                     + (value ? gfx2_text_w(value, &font_ui_14b) + 7 : 0) + 20);
}

static void draw_status(gfx2_surf_t *s, const radio_t *R)
{
    char buf[48];
    int16_t x;

    gfx2_fill(s, 0, ST_Y, GFX2_W, ST_H, gfx2_rgb(PAL_SURF_0));

    /* --- S-meter: barra continua con degradado y marcas de unidad S ---
     * El de 12 segmentos actual da 7dB por segmento, tan grueso que el
     * numero de al lado hacia todo el trabajo. Una barra continua con
     * escala marcada se lee de un vistazo Y conserva la precision. */
    {
        int16_t mx = 16, my = (int16_t)(ST_Y + 13), mw = 232, mh = 16;
        float t;
        int16_t fill_w, i;

        gfx2_rrect(s, mx, my, mw, mh, 4, gfx2_rgb(PAL_SURF_2));

        /* S1..S9 ocupan el 62% de la barra, S9+ el resto: es la escala
         * real de un S-meter (6dB por unidad S hasta S9, luego dB sueltos) */
        if (R->s_units > 9) {
            t = 0.62f + (float)R->over_db / 60.0f * 0.38f;
        } else {
            t = (float)R->s_units / 9.0f * 0.62f;
        }
        if (t > 1.0f) t = 1.0f;
        fill_w = (int16_t)(t * (float)mw);

        if (fill_w > 4) {
            gfx2_rrect(s, mx, my, fill_w, mh, 4, gfx2_rgb(PAL_TRACE));
            /* el tramo por encima de S9 en ambar: sobrecarga inminente */
            if (t > 0.62f) {
                int16_t x0 = (int16_t)(mx + 0.62f * mw);
                gfx2_fill(s, x0, my, (int16_t)(mx + fill_w - x0), mh,
                          gfx2_rgb(PAL_ACCENT));
            }
        }
        /* marcas de escala por encima de la barra */
        for (i = 1; i <= 9; i += 2) {
            int16_t tx = (int16_t)(mx + (float)i / 9.0f * 0.62f * mw);
            gfx2_vline(s, tx, (int16_t)(my + mh + 2), 3, gfx2_rgb(PAL_INK_MUTE));
        }

        /* lectura numerica: unidades S grandes, dBm como dato secundario */
        if (R->s_units > 9) snprintf(buf, sizeof buf, "S9+%d", R->over_db);
        else                snprintf(buf, sizeof buf, "S%d", R->s_units);
        gfx2_text(s, (int16_t)(mx + mw + 12), (int16_t)(ST_Y + 9), buf,
                  &font_ui_18b, gfx2_rgb(PAL_INK));
        snprintf(buf, sizeof buf, "%.0f dBm", R->dbm);
        gfx2_text(s, (int16_t)(mx + mw + 104), (int16_t)(ST_Y + 13), buf,
                  &font_ui_14, gfx2_rgb(PAL_INK_DIM));
    }

    /* --- destino del mando, siempre en el mismo sitio ---
     * El firmware actual deja el mando apuntando a "lo ultimo que abriste
     * en un DETAIL view", sin nada en pantalla que lo diga. Este indicador
     * es la respuesta a "que hace el mando ahora mismo". */
    {
        int16_t kx = 452, kw;
        char kb[64];
        snprintf(kb, sizeof kb, "%s  %s", R->knob, R->knob_val);
        kw = (int16_t)(gfx2_text_w(kb, &font_ui_14b) + 46);
        gfx2_rrect(s, kx, (int16_t)(ST_Y + 8), kw, 26, 13,
                   gfx2_rgba(PAL_ACCENT, 38));
        gfx2_rrect_outline(s, kx, (int16_t)(ST_Y + 8), kw, 26, 13, 1,
                           gfx2_rgba(PAL_ACCENT, 150));
        /* iconito de mando: circulo con muesca */
        gfx2_rrect(s, (int16_t)(kx + 9), (int16_t)(ST_Y + 15), 12, 12, 6,
                   gfx2_rgb(PAL_ACCENT));
        gfx2_fill(s, (int16_t)(kx + 14), (int16_t)(ST_Y + 15), 2, 4,
                  gfx2_rgb(PAL_SURF_0));
        gfx2_text(s, (int16_t)(kx + 30), (int16_t)(ST_Y + 13), kb,
                  &font_ui_14b, gfx2_rgb(PAL_ACCENT));
    }

    /* --- chips de estado, a la derecha --- */
    {
        const char *nr = R->nr_on ? "sí" : "no";
        int16_t w1 = chip_w("RNR", nr);
        int16_t w2 = chip_w("ATT", "-6 dB");
        /* se colocan desde el borde derecho hacia dentro, para que el
         * ultimo chip no se salga de pantalla al cambiar de texto */
        x = (int16_t)(GFX2_W - 14 - w2);
        chip(s, x, (int16_t)(ST_Y + 8), "ATT", "-6 dB", R->att_db != 0);
        x = (int16_t)(x - 6 - w1);
        chip(s, x, (int16_t)(ST_Y + 8), "RNR", nr, R->nr_on);
    }
}

/* ======================================================================
 * ESPECTRO
 * ====================================================================== */
static void draw_spectrum(gfx2_surf_t *s, const radio_t *R)
{
    const int16_t px0 = 10, pw = WF_W, ptop = (int16_t)(SP_Y + 6);
    const int16_t ph = (int16_t)(SP_H - 30);
    int16_t i;
    char buf[32];

    gfx2_fill(s, 0, SP_Y, GFX2_W, SP_H, gfx2_rgb(PAL_SURF_0));

    /* rejilla recesiva: esta para poder medir, no para mirarla */
    for (i = 0; i <= 4; i++) {
        int16_t gy = (int16_t)(ptop + i * ph / 4);
        gfx2_hline(s, px0, gy, pw, gfx2_rgb(PAL_GRID));
    }
    for (i = 0; i <= 8; i++) {
        int16_t gx = (int16_t)(px0 + i * pw / 8);
        gfx2_vline(s, gx, ptop, ph, gfx2_rgb(PAL_GRID));
    }

    /* --- BANDA DE PASO ---
     * Rectangulo translucido centrado en el VFO con el ancho del filtro de
     * audio activo. Esto es lo que de verdad se esta escuchando. El
     * firmware actual solo lo dice como numero ("BW 4K0") en un tile, asi
     * que no habia forma de ver si una emisora vecina caia dentro. */
    {
        float hz_per_px = 192000.0f / (float)pw;
        int16_t half = (int16_t)((float)R->audio_bw_hz / hz_per_px / 2.0f);
        int16_t cx = (int16_t)(px0 + pw / 2);
        if (half < 2) half = 2;
        gfx2_fill(s, (int16_t)(cx - half), ptop, (int16_t)(half * 2), ph,
                  gfx2_rgba(PAL_PASSBAND, 46));
        gfx2_vline(s, (int16_t)(cx - half), ptop, ph, gfx2_rgba(PAL_PASSBAND, 190));
        gfx2_vline(s, (int16_t)(cx + half), ptop, ph, gfx2_rgba(PAL_PASSBAND, 190));
        snprintf(buf, sizeof buf, "%.1f kHz", R->audio_bw_hz / 1000.0f);
        dec_comma(buf);
        gfx2_text(s, (int16_t)(cx + half + 6), (int16_t)(ptop + 3), buf,
                  &font_ui_14, gfx2_rgb(PAL_PASSBAND));
    }

    /* --- traza: area rellena con degradado + linea de cresta ---
     * El area da sensacion de nivel de un vistazo; la linea de 2px
     * mantiene los picos estrechos legibles (un birdie de 1 bin no
     * desaparece). */
    for (i = 0; i < pw; i++) {
        int src = (int)((int64_t)i * SPEC_BINS / pw);
        float t = (g_spec[src] - DB_LO) / (DB_HI - DB_LO);
        int16_t h, top;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        h = (int16_t)(t * (float)ph);
        if (h < 1) h = 1;
        top = (int16_t)(ptop + ph - h);
        gfx2_vgrad(s, (int16_t)(px0 + i), top, 1, h,
                   gfx2_rgba(PAL_TRACE, 150), gfx2_rgba(PAL_TRACE, 26));
        gfx2_fill(s, (int16_t)(px0 + i), top, 1, 2, gfx2_rgb(PAL_TRACE));
    }

    /* rotulo de la paleta activa, en el hueco libre de arriba a la derecha */
    {
        int16_t tw = (int16_t)(gfx2_text_w("paleta: ", &font_ui_14)
                               + gfx2_text_w(g_pal->name, &font_ui_14b) + 20);
        int16_t tx = (int16_t)(px0 + pw - tw - 6);
        gfx2_rrect(s, tx, (int16_t)(ptop + 4), tw, 22, 5,
                   gfx2_rgba(PAL_SURF_2, 210));
        gfx2_rrect_outline(s, tx, (int16_t)(ptop + 4), tw, 22, 5, 1,
                           gfx2_rgb(PAL_LINE));
        gfx2_text(s, (int16_t)(tx + 10), (int16_t)(ptop + 7), "paleta: ",
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
        gfx2_text(s, (int16_t)(tx + 10 + gfx2_text_w("paleta: ", &font_ui_14)),
                  (int16_t)(ptop + 7), g_pal->name, &font_ui_14b,
                  gfx2_rgb(PAL_ACCENT));
    }

    /* --- cursor de VFO: linea + cabeza triangular, geometria propia --- */
    {
        int16_t cx = (int16_t)(px0 + pw / 2);
        int16_t k;
        gfx2_vline(s, cx, ptop, ph, gfx2_rgba(PAL_ACCENT, 210));
        for (k = 0; k < 7; k++) {
            gfx2_hline(s, (int16_t)(cx - 6 + k), (int16_t)(ptop + k),
                       (int16_t)(13 - 2 * k), gfx2_rgb(PAL_ACCENT));
        }
    }

    /* --- eje de dB ---
     * Solo las tres divisiones interiores: la de i=4 cae sobre el eje de
     * frecuencia y se solapaba con su etiqueta. La unidad se escribe una
     * sola vez arriba, no repetida en cada marca. */
    for (i = 1; i <= 3; i++) {
        int16_t gy = (int16_t)(ptop + i * ph / 4);
        float db = DB_HI - (DB_HI - DB_LO) * (float)i / 4.0f;
        snprintf(buf, sizeof buf, "%.0f", db);
        gfx2_text_in(s, (int16_t)(px0 + 2), (int16_t)(gy + 2), 34, buf,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_R);
    }
    gfx2_text(s, (int16_t)(px0 + 4), (int16_t)(ptop + 2), "dBFS",
              &font_ui_14, gfx2_rgb(PAL_INK_MUTE));

    /* --- eje de frecuencia bajo el espectro, en kHz relativos --- */
    for (i = 0; i <= 8; i++) {
        int16_t gx = (int16_t)(px0 + i * pw / 8);
        int khz = -96 + i * 24;
        int16_t lx;
        if (khz == 0)      snprintf(buf, sizeof buf, "VFO");
        else if (i == 0)   snprintf(buf, sizeof buf, "-96 kHz");
        else if (i == 8)   snprintf(buf, sizeof buf, "+96 kHz");
        else               snprintf(buf, sizeof buf, "%+d", khz);
        /* las etiquetas de los extremos se pegan al borde en vez de
         * centrarse sobre su marca, que las sacaba de la pantalla */
        lx = (int16_t)(gx - 30);
        if (i == 0) lx = px0;
        if (i == 8) lx = (int16_t)(gx - 60);
        gfx2_text_in(s, lx, (int16_t)(ptop + ph + 5), 60, buf,
                     &font_ui_14, gfx2_rgb(khz == 0 ? PAL_ACCENT : PAL_INK_MUTE),
                     i == 0 ? GFX2_ALIGN_L : (i == 8 ? GFX2_ALIGN_R : GFX2_ALIGN_C));
    }
}

/* ======================================================================
 * WATERFALL + leyenda de escala
 * ====================================================================== */
static void draw_wf_frame(gfx2_surf_t *s)
{
    char buf[16];
    int16_t i;

    gfx2_fill(s, 0, WF_Y, GFX2_W, WF_H, gfx2_rgb(PAL_SURF_0));

    /* Leyenda de la escala de color: obligatoria cuando el color codifica
     * magnitud, y el firmware actual no la tiene - los colores del waterfall
     * no se podian traducir a dB de ninguna forma.
     * La barra va en la canaleta de la derecha, con el extremo fuerte arriba
     * para que coincida con la direccion del eje de dB del espectro. */
    {
        const int16_t bx = 752, bw = 9;
        const int16_t by = (int16_t)(WF_Y + 18), bh = 56;
        for (i = 0; i < bh; i++) {
            uint16_t c = wf_cmap[(int)((float)(bh - 1 - i) / (float)(bh - 1) * 255.0f)];
            gfx2_rgba_t cc;
            uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
            uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
            uint8_t b5 = (uint8_t)(c & 0x1F);
            cc.r = (uint8_t)((r5 << 3) | (r5 >> 2));
            cc.g = (uint8_t)((g6 << 2) | (g6 >> 4));
            cc.b = (uint8_t)((b5 << 3) | (b5 >> 2));
            cc.a = 255;
            gfx2_fill(s, bx, (int16_t)(by + i), bw, 1, cc);
        }
        gfx2_rrect_outline(s, bx, by, bw, bh, 0, 1, gfx2_rgb(PAL_LINE));
        snprintf(buf, sizeof buf, "%.0f", DB_HI);
        gfx2_text_in(s, (int16_t)(bx + bw + 3), (int16_t)(by - 1), 26, buf,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_L);
        snprintf(buf, sizeof buf, "%.0f", DB_LO);
        gfx2_text_in(s, (int16_t)(bx + bw + 3), (int16_t)(by + bh - 14), 26, buf,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_L);
        gfx2_text(s, bx, (int16_t)(WF_Y + 2), "dBFS", &font_ui_14,
                  gfx2_rgb(PAL_INK_MUTE));
    }
}

/* ======================================================================
 * BARRA DE ACCIONES
 * ====================================================================== */
static void draw_actions(gfx2_surf_t *s, int pressed)
{
    /* Nombres reales, no siglas de tres letras. Caben porque la fuente es
     * proporcional: "Filtros" ocupa menos que "IFBW" en la 5x7 escalada. */
    static const char *lab[6] = { "Modo", "Bandas", "Filtros",
                                  "AGC", "Ruido", "Ajustes" };
    static const char *sub[6] = { "AM",   "41 m",   "4,0 kHz",
                                  "Media", "no",    NULL };
    int16_t i;

    gfx2_vgrad(s, 0, ACT_Y, GFX2_W, ACT_H,
               gfx2_rgb(PAL_SURF_1), gfx2_rgb(0x11141A));
    gfx2_hline(s, 0, ACT_Y, GFX2_W, gfx2_rgb(PAL_LINE));

    for (i = 0; i < 6; i++) {
        int16_t bw = 126, gap = 6;
        int16_t bx = (int16_t)(10 + i * (bw + gap));
        int16_t by = (int16_t)(ACT_Y + 7);
        int16_t bh = 48;
        int on = (i == pressed);

        gfx2_rrect(s, bx, by, bw, bh, 8,
                   gfx2_rgb(on ? PAL_SURF_3 : PAL_SURF_2));
        gfx2_rrect_outline(s, bx, by, bw, bh, 8, 1,
                           on ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_LINE));
        if (sub[i]) {
            gfx2_text_in(s, bx, (int16_t)(by + 6), bw, lab[i], &font_ui_14b,
                         gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
            gfx2_text_in(s, bx, (int16_t)(by + 25), bw, sub[i], &font_ui_14,
                         gfx2_rgb(on ? PAL_ACCENT : PAL_INK_DIM), GFX2_ALIGN_C);
        } else {
            gfx2_text_in(s, bx, (int16_t)(by + 14), bw, lab[i], &font_ui_18b,
                         gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        }
    }
}

/* ======================================================================
 * Composicion de la pantalla principal
 * ====================================================================== */
static radio_t g_R;

void sim_init_state(void);

void draw_all(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    draw_header(s, &g_R);
    draw_status(s, &g_R);
    draw_spectrum(s, &g_R);
    draw_wf_frame(s);
    draw_actions(s, -1);
}

/* ======================================================================
 * Pantalla de ajustes: secciones + filas con valor, en vez de 36 tiles
 * planos de tres letras
 * ====================================================================== */
typedef struct { const char *name; const char *value; const char *hint; } row_t;

void draw_settings(gfx2_surf_t *s, void *ctx)
{
    static const char *secs[4] = { "Recepción", "Filtros", "Pantalla", "Digitales" };
    static const row_t rows[6] = {
        { "AGC",                  "Media",    "Rápida / Media / Lenta / Desactivada" },
        { "Silenciador",          "12",       "Umbral en AM y FM estrecha" },
        { "Ganancia de entrada",  "24,0 dB",  "PGA del códec, 0 a 47,5 dB" },
        { "Atenuador",            "-6 dB",    "Fijo: 0 / -6 / -12 dB" },
        { "AGC de radiofrecuencia","Activado","Atenúa solo si la entrada se satura" },
        { "Reducción de ruido",   "Desactivada", "Sustracción espectral, solo AM y BLU" },
    };
    int16_t i;
    (void)ctx;

    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    draw_header(s, &g_R);
    draw_status(s, &g_R);

    /* barra de titulo de la hoja */
    gfx2_fill(s, 0, SP_Y, GFX2_W, 38, gfx2_rgb(PAL_SURF_1));
    gfx2_text(s, 18, (int16_t)(SP_Y + 8), "Ajustes", &font_ui_18b,
              gfx2_rgb(PAL_INK));
    {
        int16_t bw = 92;
        int16_t bx = (int16_t)(GFX2_W - bw - 12);
        gfx2_rrect(s, bx, (int16_t)(SP_Y + 5), bw, 28, 6, gfx2_rgb(PAL_SURF_3));
        gfx2_rrect_outline(s, bx, (int16_t)(SP_Y + 5), bw, 28, 6, 1,
                           gfx2_rgb(PAL_LINE));
        gfx2_text_in(s, bx, (int16_t)(SP_Y + 10), bw, "Cerrar", &font_ui_14b,
                     gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
    }
    gfx2_hline(s, 0, (int16_t)(SP_Y + 38), GFX2_W, gfx2_rgb(PAL_LINE));

    /* --- columna de secciones: 4 objetivos altos, comodos para tactil
     * resistivo, con la activa marcada por relleno Y por una barra de
     * acento a la izquierda (no solo por color) --- */
    for (i = 0; i < 4; i++) {
        int16_t ty = (int16_t)(SP_Y + 48 + i * 50);
        int on = (i == 0);
        if (on) {
            gfx2_rrect(s, 10, ty, 186, 44, 8, gfx2_rgb(PAL_SURF_2));
            gfx2_fill(s, 10, (int16_t)(ty + 8), 4, 28, gfx2_rgb(PAL_ACCENT));
        }
        gfx2_text(s, 26, (int16_t)(ty + 12), secs[i], &font_ui_18b,
                  gfx2_rgb(on ? PAL_INK : PAL_INK_DIM));
    }

    /* --- filas de la seccion activa: nombre, ayuda y valor a la derecha ---
     * Una lista crece sin limite; la rejilla de 3x3 tiles se llenaba y
     * forzaba a meter controles en la pagina equivocada. */
    for (i = 0; i < 6; i++) {
        int16_t ry = (int16_t)(SP_Y + 46 + i * 42);
        int16_t rx = 206, rw = (int16_t)(GFX2_W - rx - 12);
        if (i & 1) {
            gfx2_fill(s, rx, ry, rw, 40, gfx2_rgba(0xFFFFFF, 6));
        }
        gfx2_text(s, (int16_t)(rx + 14), (int16_t)(ry + 4), rows[i].name,
                  &font_ui_14b, gfx2_rgb(PAL_INK));
        gfx2_text(s, (int16_t)(rx + 14), (int16_t)(ry + 21), rows[i].hint,
                  &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
        {
            int16_t vw = (int16_t)(gfx2_text_w(rows[i].value, &font_ui_14b) + 24);
            int16_t vx = (int16_t)(rx + rw - vw - 10);
            gfx2_rrect(s, vx, (int16_t)(ry + 7), vw, 26, 6, gfx2_rgb(PAL_SURF_2));
            gfx2_rrect_outline(s, vx, (int16_t)(ry + 7), vw, 26, 6, 1,
                               gfx2_rgb(PAL_LINE));
            gfx2_text_in(s, vx, (int16_t)(ry + 12), vw, rows[i].value,
                         &font_ui_14b, gfx2_rgb(PAL_ACCENT), GFX2_ALIGN_C);
        }
        if (i < 5) {
            gfx2_hline(s, (int16_t)(rx + 14), (int16_t)(ry + 40),
                       (int16_t)(rw - 28), gfx2_rgb(0x1A1F26));
        }
    }
}

/* ====================================================================== */
void sim_init_state(void)
{
    g_R.mhz = "7.200,00";
    g_R.digit_hi = 4;          /* el digito de 1 kHz en "7.200,00" */
    g_R.mode = "AM";
    g_R.band = "41 m";
    g_R.clock = "21:47";
    g_R.volume = 18;
    g_R.dbm = -53.0f;
    g_R.s_units = 10;
    g_R.over_db = 14;
    g_R.agc = "Media";
    g_R.audio_bw_hz = 4000;
    g_R.nr_on = 0;
    g_R.att_db = -6;
    g_R.knob = "Sintonía";
    g_R.knob_val = "1 kHz";

    build_data();
}

#ifndef NO_MAIN
int main(void)
{
    lcd_cost_t c;

    sim_init_state();

    /* --- pantalla principal --- */
    lcd_sim_reset(0x0000);
    lcd_sim_cost_reset();
    gfx2_render_screen(draw_all, NULL);
    gfx2_wf_blit(10, WF_Y + 4, WF_W, WF_ROWS, g_wf, WF_W, 0, wf_cmap);
    c = lcd_sim_cost();
    lcd_sim_write_ppm("out/03_after_radio.ppm");
    printf("DESPUES pantalla radio: ventanas=%llu accesos_bus=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes);
    printf("  timings actuales  (21 ciclos @200MHz): %6.1f ms -> %5.1f fps\n",
           lcd_sim_est_us(21, 200.0) / 1000.0,
           1000.0 / (lcd_sim_est_us(21, 200.0) / 1000.0));
    printf("  timings ajustados ( 6 ciclos @200MHz): %6.1f ms -> %5.1f fps\n",
           lcd_sim_est_us(6, 200.0) / 1000.0,
           1000.0 / (lcd_sim_est_us(6, 200.0) / 1000.0));

    /* --- pantalla de ajustes --- */
    lcd_sim_reset(0x0000);
    lcd_sim_cost_reset();
    gfx2_render_screen(draw_settings, NULL);
    c = lcd_sim_cost();
    lcd_sim_write_ppm("out/04_after_settings.ppm");
    printf("DESPUES pantalla ajustes: ventanas=%llu accesos_bus=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes);

    return 0;
}
#endif /* NO_MAIN */

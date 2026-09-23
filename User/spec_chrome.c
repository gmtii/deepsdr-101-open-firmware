#include "spec_chrome.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"

/* ---------------------------------------------------------------------
 * Formateo sin printf
 * ---------------------------------------------------------------------
 * Ni snprintf ni %f: newlib-nano sin soporte de float en printf devuelve
 * literalmente la cadena "f" en lugar del numero, y arrastrar el soporte
 * completo son ~10 KB de flash por tres etiquetas. Todo a mano.
 */

/* Entero con signo, con coma decimal opcional. dec = cifras decimales.
 * Devuelve el numero de caracteres escritos. */
static uint8_t fmt_dec(char *buf, int32_t v, uint8_t dec, uint8_t force_sign)
{
    char tmp[12];
    uint8_t n = 0, i = 0;
    uint32_t mag;

    if (v < 0) { buf[i++] = '-'; mag = (uint32_t)(-v); }
    else       { if (force_sign) { buf[i++] = '+'; } mag = (uint32_t)v; }

    do { tmp[n++] = (char)('0' + (mag % 10U)); mag /= 10U; } while (mag > 0U);
    /* rellenar con ceros si hacen falta para la parte decimal */
    while (n <= dec) { tmp[n++] = '0'; }

    while (n > 0U) {
        n--;
        buf[i++] = tmp[n];
        /* la coma decimal va en la convencion espanola, no el punto */
        if (dec != 0U && n == dec) { buf[i++] = ','; }
    }
    buf[i] = '\0';
    return i;
}

/* Frecuencia en Hz -> "7.200" en kHz ENTEROS, con separador de miles.
 *
 * Sin decimales a proposito. Las marcas de la regla estan separadas
 * span/4, que en el caso mas estrecho son 3 kHz: un decimal ahi no
 * distingue nada y las etiquetas se pisaban unas a otras en los bordes.
 * La frecuencia exacta al Hz ya esta en la cabecera, que es donde se mira
 * para sintonizar; la regla esta para situar lo que se ve, no para leer
 * el dial. */
static void fmt_khz(char *buf, uint32_t hz)
{
    uint32_t khz = (hz + 500U) / 1000U;   /* redondeo, no truncado */
    char num[16];
    uint8_t n = fmt_dec(num, (int32_t)khz, 0, 0);
    uint8_t i = 0, j;

    for (j = 0; j < n; j++) {
        if (j != 0U && ((n - j) % 3U) == 0U) { buf[i++] = '.'; }
        buf[i++] = num[j];
    }
    buf[i] = '\0';
}

/* ---------------------------------------------------------------------
 * Canaleta izquierda del espectro: numeros de dB sobre las divisiones
 * ---------------------------------------------------------------------
 * Las divisiones son las mismas que dibuja spectrum.c dentro de la traza
 * (SPECTRUM_GRID_ROWS lineas que parten la altura en ROWS+1 bandas), asi
 * que el numero cae EXACTAMENTE sobre su linea. Si esas dos cuentas se
 * separan, el eje miente, que es peor que no tenerlo.
 */
#define SPC_GRID_ROWS 3

static void draw_db_gutter(gfx2_surf_t *s, void *ctx)
{
    const spec_chrome_t *st = (const spec_chrome_t *)ctx;
    char buf[12];
    uint8_t gi;

    if (!gfx2_band_hits(s, SPC_Y, (int16_t)(SPC_RULER_Y - SPC_Y))) { return; }

    gfx2_fill(s, 0, SPC_Y, SPC_GUT_W, (int16_t)(SPC_RULER_Y - SPC_Y),
              gfx2_rgb(PAL_SURF_0));

    /* Sin rotulo de unidad. Lo llevaba ("dBFS") y sobra: una columna de
     * numeros negativos al lado de un espectro no se confunde con otra cosa,
     * y el rotulo ocupaba la esquina de arriba, que es justo donde se mete la
     * traza cuando hay senal fuerte. */

    for (gi = 1U; gi <= SPC_GRID_ROWS; gi++) {
        /* misma formula que spectrum.c: row_pos = h*gi/(ROWS+1), medido
         * desde ARRIBA de la traza */
        int16_t row = (int16_t)(((int32_t)SPC_TRACE_H * gi) / (SPC_GRID_ROWS + 1));
        int16_t gy  = (int16_t)(SPC_TRACE_Y + row);
        /* el nivel que representa esa fila: arriba = db_max */
        float   db  = st->db_max - (st->db_max - st->db_min)
                                    * (float)gi / (float)(SPC_GRID_ROWS + 1);
        int32_t dbi = (int32_t)((db < 0.0f) ? (db - 0.5f) : (db + 0.5f));

        (void)fmt_dec(buf, dbi, 0, 0);
        /* La linea de guia entra 4 px en la canaleta para que el numero no
         * quede flotando lejos de su division. */
        gfx2_hline(s, (int16_t)(SPC_GUT_W - 4), gy, 4, gfx2_rgb(PAL_GRID));
        /* INK_DIM y no INK_MUTE: el signo menos es un guion de 5x2 px y en
         * el panel real, a oscuras, se perdia - los numeros se leian como si
         * fueran positivos, que en un eje de dBFS es decir lo contrario de lo
         * que pasa. Con el tono de texto secundario el guion aguanta. */
        gfx2_text_in(s, 0, (int16_t)(gy - 7), (int16_t)(SPC_GUT_W - 6), buf,
                     &font_ui_14, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_R);
    }
}

/* ---------------------------------------------------------------------
 * Regla de frecuencia
 * ---------------------------------------------------------------------
 * Cinco marcas: bordes, cuartos y centro. Las de los bordes van pegadas
 * al borde en vez de centradas sobre su marca, porque centradas se salen
 * de la pantalla.
 *
 * El punto de DEMODULACION va en ambar y con su propia marca mas alta.
 * No siempre cae en el centro: con low-IF activo esta a +Fs/4 del centro
 * del panel, que es justo el cuarto de la derecha.
 */
static void draw_ruler(gfx2_surf_t *s, void *ctx)
{
    const spec_chrome_t *st = (const spec_chrome_t *)ctx;
    char buf[20];
    int8_t k;

    if (!gfx2_band_hits(s, SPC_RULER_Y, SPC_RULER_H)) { return; }

    gfx2_fill(s, 0, SPC_RULER_Y, GFX2_W, SPC_RULER_H, gfx2_rgb(PAL_SURF_0));
    gfx2_hline(s, SPC_TRACE_X, SPC_RULER_Y, SPC_TRACE_W, gfx2_rgb(PAL_LINE));

    /* Ancho de campo de una etiqueta: el peor caso es el borde, algo como
     * "7.296,00". Se mide de verdad en vez de estimarlo. */
    for (k = -2; k <= 2; k++) {
        int32_t  off_hz = (int32_t)k * (int32_t)(st->span_hz / 4U);
        int32_t  px     = (int32_t)(((int64_t)SPC_TRACE_W * off_hz)
                                     / (int64_t)st->span_hz);
        int16_t  cx     = (int16_t)(SPC_TRACE_X + SPC_TRACE_W / 2 + px);
        int64_t  f      = (int64_t)st->center_hz + (int64_t)off_hz;
        int16_t  tw, tx;
        /* k del punto de demodulacion: demod_px esta en px, y las marcas
         * estan en multiplos de SPC_TRACE_W/4, asi que se compara en px
         * con media division de tolerancia. */
        uint8_t  is_demod = (uint8_t)((px - (int32_t)st->demod_px < SPC_TRACE_W / 8) &&
                                      ((int32_t)st->demod_px - px < SPC_TRACE_W / 8));
        gfx2_rgba_t col = is_demod ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_INK_MUTE);

        if (f < 0) { f = 0; }
        fmt_khz(buf, (uint32_t)f);

        gfx2_vline(s, cx, SPC_RULER_Y, is_demod ? 7 : 4, col);

        tw = gfx2_text_w(buf, &font_ui_14);
        tx = (int16_t)(cx - tw / 2);
        /* Los extremos se pegan al borde de la TRAZA, no al de la pantalla:
         * a la izquierda el "kHz" de la canaleta ocupa hasta SPC_GUT_W. */
        if (tx < SPC_TRACE_X + 2) { tx = (int16_t)(SPC_TRACE_X + 2); }
        if (tx > (int16_t)(GFX2_W - tw - 2)) { tx = (int16_t)(GFX2_W - tw - 2); }
        gfx2_text(s, tx, (int16_t)(SPC_RULER_Y + 8), buf, &font_ui_14, col);
    }

    /* La unidad, una vez, en la canaleta. Aqui SI cabe: en esta fila la
     * canaleta esta vacia (los numeros de dB acaban arriba, en SPC_RULER_Y).
     *
     * El ancho del filtro NO se escribe aqui aunque sea lo que explica el
     * rectangulo sombreado: ya esta en el chip "BW" de la barra de estado,
     * y puesto ademas al final de la regla se comia la etiqueta del borde
     * derecho. Un dato, un sitio. */
    gfx2_text_in(s, 0, (int16_t)(SPC_RULER_Y + 8), (int16_t)(SPC_GUT_W - 6),
                 "kHz", &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_R);
}

/* ---------------------------------------------------------------------
 * Canaleta del waterfall: la escala de color
 * ---------------------------------------------------------------------
 * Obligatoria en cuanto el color codifica una magnitud. El firmware
 * actual no la tiene: los colores del waterfall no se podian traducir a
 * dB de ninguna manera.
 *
 * El extremo fuerte va ARRIBA, igual que en el eje de dB del espectro,
 * para que las dos escalas se lean en el mismo sentido.
 */
static void draw_wf_legend(gfx2_surf_t *s, void *ctx)
{
    const spec_chrome_t *st = (const spec_chrome_t *)ctx;
    /* Los numeros van ENCIMA y DEBAJO de la barra, no a su lado: al lado
     * solo quedaban 12 px libres y "-110" se cortaba a "-1". Apilados, cada
     * uno dispone de la canaleta entera. */
    const int16_t bx = 6, bw = 26;
    const int16_t by = (int16_t)(SPC_WF_Y + 15);
    const int16_t bh = (int16_t)(SPC_WF_ROWS - 30);
    char buf[12];
    int16_t i;

    if (!gfx2_band_hits(s, SPC_WF_PANEL_Y,
                        (int16_t)(SPC_WF_Y + SPC_WF_ROWS - SPC_WF_PANEL_Y))) {
        return;
    }

    gfx2_fill(s, 0, SPC_WF_PANEL_Y, SPC_GUT_W,
              (int16_t)(SPC_WF_Y + SPC_WF_ROWS - SPC_WF_PANEL_Y),
              gfx2_rgb(PAL_SURF_0));

    for (i = 0; i < bh; i++) {
        uint16_t c565 = st->cmap[(int)((int32_t)(bh - 1 - i) * 255 / (bh - 1))];
        uint8_t r5 = (uint8_t)((c565 >> 11) & 0x1F);
        uint8_t g6 = (uint8_t)((c565 >> 5) & 0x3F);
        uint8_t b5 = (uint8_t)(c565 & 0x1F);
        gfx2_rgba_t cc;
        cc.r = (uint8_t)((r5 << 3) | (r5 >> 2));
        cc.g = (uint8_t)((g6 << 2) | (g6 >> 4));
        cc.b = (uint8_t)((b5 << 3) | (b5 >> 2));
        cc.a = 255;
        gfx2_fill(s, bx, (int16_t)(by + i), bw, 1, cc);
    }
    /* Sin contorno. Lo llevaba, y en la placa se veia a trozos: la rampa de
     * color ya es una forma cerrada y no necesita que nadie la enmarque. */

    (void)fmt_dec(buf, (int32_t)((st->db_max < 0.0f) ? (st->db_max - 0.5f)
                                                     : (st->db_max + 0.5f)), 0, 0);
    gfx2_text_in(s, 0, (int16_t)(by - 15), (int16_t)(SPC_GUT_W - 2), buf,
                 &font_ui_14, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);

    (void)fmt_dec(buf, (int32_t)((st->db_min < 0.0f) ? (st->db_min - 0.5f)
                                                     : (st->db_min + 0.5f)), 0, 0);
    gfx2_text_in(s, 0, (int16_t)(by + bh + 1), (int16_t)(SPC_GUT_W - 2), buf,
                 &font_ui_14, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);
}

void spec_chrome_draw_ruler(const spec_chrome_t *st)
{
    gfx2_render(0, SPC_RULER_Y, GFX2_W, SPC_RULER_H, draw_ruler, (void *)st);
}

void spec_chrome_draw_axis(const spec_chrome_t *st)
{
    gfx2_render(0, SPC_Y, SPC_GUT_W, (int16_t)(SPC_RULER_Y - SPC_Y),
                draw_db_gutter, (void *)st);
    gfx2_render(0, SPC_WF_PANEL_Y, SPC_GUT_W,
                (int16_t)(SPC_WF_Y + SPC_WF_ROWS - SPC_WF_PANEL_Y),
                draw_wf_legend, (void *)st);
}

void spec_chrome_draw(const spec_chrome_t *st)
{
    spec_chrome_draw_axis(st);
    spec_chrome_draw_ruler(st);
}

/* Comparacion campo a campo, no memcmp: la estructura tiene huecos de
 * relleno entre campos de distinto tamano y su contenido es indefinido, asi
 * que un memcmp daria "ha cambiado" al azar y repintaria 30 veces por
 * segundo algo que no se ha movido. */
uint8_t spec_chrome_ruler_changed(const spec_chrome_t *a, const spec_chrome_t *b)
{
    return (uint8_t)(a->center_hz != b->center_hz ||
                     a->span_hz   != b->span_hz   ||
                     a->demod_px  != b->demod_px);
}

uint8_t spec_chrome_axis_changed(const spec_chrome_t *a, const spec_chrome_t *b)
{
    return (uint8_t)(a->db_min != b->db_min ||
                     a->db_max != b->db_max ||
                     a->cmap   != b->cmap);
}

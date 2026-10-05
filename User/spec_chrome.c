#include "spectrum.h"   /* SPECTRUM_GRID_ROWS: las lineas que dibuja de verdad el espectro */
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
static void fmt_eje(char *buf, uint32_t hz, uint8_t en_hz)
{
    uint32_t v = en_hz ? hz : ((hz + 500U) / 1000U);   /* redondeo, no truncado */
    char num[16];
    uint8_t n = fmt_dec(num, (int32_t)v, 0, 0);
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
/*
 * EL NUMERO DE DIVISIONES SALE DE spectrum.h, NO DE AQUI - 05/10/2026.
 *
 * Aqui ponia 3 escrito a mano, y spectrum.c dibuja SPECTRUM_GRID_ROWS = 4.
 * El comentario de aqui arriba decia que si las dos cuentas se separaban el
 * eje mentiria. Se habian separado: spectrum.c pone sus lineas en h*gi/5
 * -0,20, 0,40, 0,60 y 0,80 de la altura- y esta canaleta ponia sus numeros
 * en h*gi/4 -0,25, 0,50 y 0,75-. Ni uno solo de los tres numeros caia sobre
 * una linea, y las cuatro lineas de verdad no tenian numero.
 *
 * No se arregla poniendo un 4: se arregla quitando el numero de aqui, que es
 * lo unico que impide que vuelvan a separarse.
 */
#define SPC_GRID_ROWS SPECTRUM_GRID_ROWS

/*
 * EL REDONDEO, EN UN SOLO SITIO, Y NO ES COSMETICA - 05/10/2026.
 *
 * Esta canaleta se repintaba EN CADA FOTOGRAMA, y la culpa era de dos lineas
 * que parecian no tener nada que ver:
 *
 *   - la autoescala del espectro (spec_agc_step) mueve db_min y db_max con
 *     una media exponencial de coma flotante, o sea que cambian en el ultimo
 *     bit SIEMPRE, para siempre;
 *   - spec_chrome_axis_changed() los comparaba con !=, igualdad exacta de
 *     floats, asi que contestaba "ha cambiado" en todos los fotogramas.
 *
 * Y repintar este eje no es gratis: son 11.843 accesos al bus medidos
 * (sim/chromecost.c: 32.677 el cromo entero, 20.834 solo la regla), y
 * ademas por el camino de la CPU, que no se solapa con nada.
 *
 * Lo que se dibuja aqui no son los floats: son sus REDONDEOS a dB enteros.
 * Asi que comparar los redondeos no es "una tolerancia a ojo", es comparar
 * exactamente lo que se ve. Y por eso el redondeo vive aqui, en una funcion
 * que usan el dibujo Y la comparacion: si algun dia cambia la precision de
 * lo que se imprime, la comparacion se entera sola.
 */
static int32_t redondea(float v)
{
    return (int32_t)((v < 0.0f) ? (v - 0.5f) : (v + 0.5f));
}

/* El dB que le toca a la division `gi`, ya redondeado. Misma formula que
 * spectrum.c usa para colocar la linea. */
static int32_t fila_db(const spec_chrome_t *st, uint8_t gi)
{
    float db = st->db_max - (st->db_max - st->db_min)
                            * (float)gi / (float)(SPC_GRID_ROWS + 1);
    return redondea(db);
}

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

        (void)fmt_dec(buf, fila_db(st, gi), 0, 0);
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
        fmt_eje(buf, (uint32_t)f, st->eje_en_hz);

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
                 st->eje_en_hz ? "Hz" : "kHz",
                 &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_R);
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

    (void)fmt_dec(buf, redondea(st->db_max), 0, 0);
    gfx2_text_in(s, 0, (int16_t)(by - 15), (int16_t)(SPC_GUT_W - 2), buf,
                 &font_ui_14, gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);

    (void)fmt_dec(buf, redondea(st->db_min), 0, 0);
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

/* El cromo entero en una banda que trae otro - para la captura de pantalla.
 * Las mismas tres funciones de dibujo que usa spec_chrome_draw(). */
/*
 * Cada capa RECORTADA A SU HUECO, igual que se recorta sola cuando se pinta
 * por gfx2_render(): ahi la ventana que se pide es el recorte. Montando la
 * pantalla entera en una banda no hay ventana, asi que hay que darsela.
 *
 * No es teorico: el numero de abajo de la leyenda del color se sale UNA FILA
 * por debajo de su hueco y en la pantalla se corta. Sin este recorte, la
 * captura lo enseñaba entero y no coincidia con lo que se ve. Lo encontro
 * sim/captura.c comparando pixel a pixel - dos pixeles de 384.000.
 */
void spec_chrome_pinta_en(gfx2_surf_t *s, const spec_chrome_t *st)
{
    gfx2_surf_t sub;

    sub = gfx2_sub_y(s, SPC_Y, (int16_t)(SPC_RULER_Y - SPC_Y));
    if (sub.h > 0) { draw_db_gutter(&sub, (void *)st); }

    sub = gfx2_sub_y(s, SPC_WF_PANEL_Y,
                     (int16_t)(SPC_WF_Y + SPC_WF_ROWS - SPC_WF_PANEL_Y));
    if (sub.h > 0) { draw_wf_legend(&sub, (void *)st); }

    sub = gfx2_sub_y(s, SPC_RULER_Y, SPC_RULER_H);
    if (sub.h > 0) { draw_ruler(&sub, (void *)st); }
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
/* Para el banco: cuantas divisiones usa esta canaleta de verdad. Existe
 * para que sim/cromorepinta.c pueda comprobar que es el mismo numero que
 * dibuja spectrum.c, en vez de fiarse de que alguien se acuerde. */
int spec_chrome_divisiones(void)
{
    return (int)SPC_GRID_ROWS;
}

uint8_t spec_chrome_ruler_changed(const spec_chrome_t *a, const spec_chrome_t *b)
{
    return (uint8_t)(a->center_hz != b->center_hz ||
                     a->span_hz   != b->span_hz   ||
                     a->demod_px  != b->demod_px);
}

/*
 * SE COMPARA LO QUE SE DIBUJA, NO LOS FLOATS DE DONDE SALE - 05/10/2026.
 *
 * Ver el comentario de redondea(): con != de floats y una autoescala que
 * mueve db_min/db_max con una media exponencial, esto contestaba "si" en
 * TODOS los fotogramas y repintaba 11.843 accesos al bus para dejar la
 * pantalla exactamente igual.
 *
 * Ahora se comparan los numeros que se imprimen -ya redondeados a dB
 * enteros- y el mapa de color. Sigue siendo exacto: si algo de lo que se ve
 * cambia, esto lo dice; si no cambia nada de lo que se ve, no se repinta.
 */
uint8_t spec_chrome_axis_changed(const spec_chrome_t *a, const spec_chrome_t *b)
{
    uint8_t gi;

    if (a->cmap != b->cmap) { return 1U; }
    if (redondea(a->db_max) != redondea(b->db_max)) { return 1U; }
    if (redondea(a->db_min) != redondea(b->db_min)) { return 1U; }
    for (gi = 1U; gi <= SPC_GRID_ROWS; gi++) {
        if (fila_db(a, gi) != fila_db(b, gi)) { return 1U; }
    }
    return 0U;
}

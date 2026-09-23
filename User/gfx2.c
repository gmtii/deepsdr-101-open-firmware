#include "gfx2.h"
#include "rm68120_exmc.h"
#include <string.h>

#ifdef GFX2_PROFILE
unsigned long g_blend_calls = 0, g_rrcov_calls = 0, g_draw_calls = 0;
#define PROF_BLEND() (g_blend_calls++)
#define PROF_RRCOV() (g_rrcov_calls++)
#define PROF_DRAW()  (g_draw_calls++)
#else
#define PROF_BLEND() ((void)0)
#define PROF_RRCOV() ((void)0)
#define PROF_DRAW()  ((void)0)
#endif

/* La banda compositora. Unica reserva grande de este modulo. */
static uint16_t s_band[GFX2_W * GFX2_BAND_H];

/* ------------------------------------------------------------------------
 * Mezcla
 * ------------------------------------------------------------------------ */

/* Mezcla c sobre el pixel RGB565 dst con cobertura cov (0..255).
 * Se expande el destino de 565 a 888 replicando bits altos - la misma
 * expansion que hace el panel - se mezcla en 888 y se vuelve a 565. Hacerlo
 * en 565 directamente acumularia error de cuantizacion en cada capa. */
static inline uint16_t blend(uint16_t dst, gfx2_rgba_t c, uint16_t cov)
{
    uint32_t a = ((uint32_t)c.a * cov) / 255u;
    uint32_t dr, dg, db;

    PROF_BLEND();

    if (a == 0) return dst;

    if (a >= 255) {
        return gfx2_to565(c.r, c.g, c.b);
    }

    dr = (dst >> 11) & 0x1F; dr = (dr << 3) | (dr >> 2);
    dg = (dst >> 5)  & 0x3F; dg = (dg << 2) | (dg >> 4);
    db =  dst        & 0x1F; db = (db << 3) | (db >> 2);

    dr = (c.r * a + dr * (255u - a)) / 255u;
    dg = (c.g * a + dg * (255u - a)) / 255u;
    db = (c.b * a + db * (255u - a)) / 255u;

    return gfx2_to565((uint8_t)dr, (uint8_t)dg, (uint8_t)db);
}

static inline void px_blend(gfx2_surf_t *s, int16_t x, int16_t y,
                            gfx2_rgba_t c, uint16_t cov)
{
    int16_t lx = (int16_t)(x - s->x);
    int16_t ly = (int16_t)(y - s->y);
    uint16_t *p;

    if (lx < 0 || ly < 0 || lx >= s->w || ly >= s->h) return;

    p = &s->px[(int32_t)ly * s->w + lx];
    *p = blend(*p, c, cov);
}

/* ------------------------------------------------------------------------
 * Render por bandas
 * ------------------------------------------------------------------------ */

/* Gancho que se llama entre bandas. Sirve para muestrear el tactil durante un
 * repintado: sin esto, la entrada solo se lee entre repintados enteros, y si
 * uno tarda un segundo hay que mantener el dedo puesto todo ese tiempo para
 * que se note. */
static void (*s_pump)(void) = 0;

void gfx2_set_pump(void (*fn)(void))
{
    s_pump = fn;
}

void gfx2_render(int16_t x, int16_t y, int16_t w, int16_t h,
                 void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx)
{
    int16_t by;

    if (w <= 0 || h <= 0) return;
    if (w > GFX2_W) w = GFX2_W;

    for (by = y; by < (int16_t)(y + h); by = (int16_t)(by + GFX2_BAND_H)) {
        gfx2_surf_t s;
        int16_t bh = (int16_t)((y + h) - by);
        int32_t n, i;

        if (bh > GFX2_BAND_H) bh = GFX2_BAND_H;

        s.px = s_band;
        s.x = x; s.y = by; s.w = w; s.h = bh;

        /* la banda arranca opaca en negro; el codigo de dibujo pinta el
         * fondo que corresponda como primera capa */
        n = (int32_t)w * bh;
        for (i = 0; i < n; i++) s_band[i] = 0;

        PROF_DRAW();
        draw(&s, ctx);

        /* una ventana, un volcado */
        rm68120_set_window((uint16_t)x, (uint16_t)by,
                           (uint16_t)(x + w - 1), (uint16_t)(by + bh - 1));
        for (i = 0; i < n; i++) {
            rm68120_write_data(s_band[i]);
        }

        if (s_pump) { s_pump(); }
    }
}

void gfx2_render_screen(void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx)
{
    gfx2_render(0, 0, GFX2_W, GFX2_H, draw, ctx);
}

/* ------------------------------------------------------------------------
 * Primitivas
 * ------------------------------------------------------------------------ */

void gfx2_fill(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
               gfx2_rgba_t c)
{
    int16_t x0 = (int16_t)(x - s->x), y0 = (int16_t)(y - s->y);
    int16_t x1 = (int16_t)(x0 + w),   y1 = (int16_t)(y0 + h);
    int16_t iy, ix;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;

    if (c.a >= 255) {
        uint16_t v = gfx2_to565(c.r, c.g, c.b);
        for (iy = y0; iy < y1; iy++) {
            uint16_t *row = &s->px[(int32_t)iy * s->w];
            for (ix = x0; ix < x1; ix++) row[ix] = v;
        }
    } else {
        for (iy = y0; iy < y1; iy++) {
            uint16_t *row = &s->px[(int32_t)iy * s->w];
            for (ix = x0; ix < x1; ix++) row[ix] = blend(row[ix], c, 255);
        }
    }
}

void gfx2_hline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, gfx2_rgba_t c)
{
    gfx2_fill(s, x, y, w, 1, c);
}

void gfx2_vline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t h, gfx2_rgba_t c)
{
    gfx2_fill(s, x, y, 1, h, c);
}

/* matriz de dither ordenado 4x4, valores 0..15 */
static const uint8_t k_dither[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

void gfx2_vgrad(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                gfx2_rgba_t c0, gfx2_rgba_t c1)
{
    int16_t iy, ix, y0, y1;

    if (h <= 0) return;
    /* Corte temprano: sin esto se recorrian las h filas del degradado en CADA
     * banda, descartando casi todas una por una. */
    if (!gfx2_band_hits(s, y, h)) return;

    /* solo las filas del degradado que caen dentro de esta banda */
    y0 = (int16_t)(s->y - y);
    y1 = (int16_t)(y0 + s->h);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;

    for (iy = y0; iy < y1; iy++) {
        int16_t sy = (int16_t)(y + iy);
        int16_t ly = (int16_t)(sy - s->y);
        uint32_t t;
        uint16_t *row;

        if (ly < 0 || ly >= s->h) continue;

        t = (h == 1) ? 0 : ((uint32_t)iy * 255u) / (uint32_t)(h - 1);
        row = &s->px[(int32_t)ly * s->w];

        for (ix = 0; ix < w; ix++) {
            int16_t lx = (int16_t)((x + ix) - s->x);
            gfx2_rgba_t c;
            /* el dither se aplica en 888 antes de cuantizar: sumamos una
             * fraccion de LSB de 565 (8 para R/B, 4 para G) segun la
             * posicion, lo que rompe los escalones del degradado */
            int32_t d = (int32_t)k_dither[sy & 3][(x + ix) & 3] - 8;

            if (lx < 0 || lx >= s->w) continue;

            c.r = (uint8_t)((c0.r * (255u - t) + c1.r * t) / 255u);
            c.g = (uint8_t)((c0.g * (255u - t) + c1.g * t) / 255u);
            c.b = (uint8_t)((c0.b * (255u - t) + c1.b * t) / 255u);
            c.a = (uint8_t)((c0.a * (255u - t) + c1.a * t) / 255u);

            {
                int32_t r = c.r + d / 2, g = c.g + d / 4, b = c.b + d / 2;
                if (r < 0)   { r = 0; }
                if (r > 255) { r = 255; }
                if (g < 0)   { g = 0; }
                if (g > 255) { g = 255; }
                if (b < 0)   { b = 0; }
                if (b > 255) { b = 255; }
                c.r = (uint8_t)r; c.g = (uint8_t)g; c.b = (uint8_t)b;
            }

            row[lx] = blend(row[lx], c, 255);
        }
    }
}

/* Cobertura antialiasada de un pixel dentro de un rectangulo redondeado.
 * Supermuestreo 4x4: barato, suficiente para radios de 4-12px, y no hace
 * falta ni sqrt ni float. */
static uint16_t rr_cov(int16_t px, int16_t py, int16_t x, int16_t y,
                       int16_t w, int16_t h, int16_t r)
{
    int16_t sx, sy;
    uint16_t hit = 0;

    PROF_RRCOV();

    for (sy = 0; sy < 4; sy++) {
        for (sx = 0; sx < 4; sx++) {
            /* centro de la submuestra, en 1/8 de pixel */
            int32_t fx = (int32_t)px * 8 + sx * 2 + 1;
            int32_t fy = (int32_t)py * 8 + sy * 2 + 1;
            int32_t X0 = (int32_t)x * 8, Y0 = (int32_t)y * 8;
            int32_t X1 = (int32_t)(x + w) * 8, Y1 = (int32_t)(y + h) * 8;
            int32_t R = (int32_t)r * 8;
            int32_t cx, cy, dx, dy;

            if (fx < X0 || fx >= X1 || fy < Y0 || fy >= Y1) continue;

            /* centro de la esquina mas cercana, si estamos en una */
            cx = fx; cy = fy;
            if (fx < X0 + R) cx = X0 + R;
            else if (fx > X1 - R) cx = X1 - R;
            if (fy < Y0 + R) cy = Y0 + R;
            else if (fy > Y1 - R) cy = Y1 - R;

            dx = fx - cx; dy = fy - cy;
            if (dx == 0 && dy == 0) { hit++; continue; }
            if (dx * dx + dy * dy <= R * R) hit++;
        }
    }
    return (uint16_t)((hit * 255u) / 16u);
}

void gfx2_rrect(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                int16_t r, gfx2_rgba_t c)
{
    int16_t iy, ix;

    if (w <= 0 || h <= 0) return;
    if (!gfx2_band_hits(s, y, h)) return;
    if (r <= 0) { gfx2_fill(s, x, y, w, h, c); return; }
    if (r > w / 2) r = (int16_t)(w / 2);
    if (r > h / 2) r = (int16_t)(h / 2);

    /* interior recto de una sola pasada, los bordes con cobertura */
    gfx2_fill(s, x, (int16_t)(y + r), w, (int16_t)(h - 2 * r), c);
    gfx2_fill(s, (int16_t)(x + r), y, (int16_t)(w - 2 * r), r, c);
    gfx2_fill(s, (int16_t)(x + r), (int16_t)(y + h - r), (int16_t)(w - 2 * r), r, c);

    for (iy = 0; iy < r; iy++) {
        for (ix = 0; ix < r; ix++) {
            int16_t xs[2], ys[2];
            int k, l;
            xs[0] = (int16_t)(x + ix);          xs[1] = (int16_t)(x + w - 1 - ix);
            ys[0] = (int16_t)(y + iy);          ys[1] = (int16_t)(y + h - 1 - iy);
            for (k = 0; k < 2; k++) {
                for (l = 0; l < 2; l++) {
                    uint16_t cov;
                    /* la comprobacion de banda va ANTES del supermuestreo:
                     * rr_cov son 16 submuestras que no se deben pagar para
                     * un pixel que ni siquiera esta en esta banda */
                    if (ys[l] < s->y || ys[l] >= (int16_t)(s->y + s->h)) continue;
                    cov = rr_cov(xs[k], ys[l], x, y, w, h, r);
                    if (cov) px_blend(s, xs[k], ys[l], c, cov);
                }
            }
        }
    }
}

void gfx2_rrect_outline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, int16_t t, gfx2_rgba_t c)
{
    int16_t iy, ix, k;

    if (w <= 0 || h <= 0 || t <= 0) return;
    if (!gfx2_band_hits(s, y, h)) return;

    if (r > w / 2) r = (int16_t)(w / 2);
    if (r > h / 2) r = (int16_t)(h / 2);

    /*
     * Solo se recorre el ANILLO del borde.
     *
     * La version anterior barria el rectangulo COMPLETO llamando a rr_cov()
     * dos veces por pixel (32 submuestras cada pixel) y descartaba el
     * interior. Para un boton de 126x48 eso eran ~194.000 operaciones cuando
     * los pixeles de borde que importan son ~350. Medido en el simulador:
     * una pantalla entera pasaba de 1.972.960 llamadas a rr_cov(), es decir
     * 31,5 millones de submuestras, que en el Cortex-M4 son segundos.
     *
     * Ahora: los cuatro tramos rectos son rellenos planos (sin antialiasing,
     * que en una recta no hace nada) y solo las cuatro esquinas de r x r
     * pasan por rr_cov.
     */
    if (r > 0) {
        /* tramos rectos */
        gfx2_fill(s, (int16_t)(x + r), y, (int16_t)(w - 2 * r), t, c);
        gfx2_fill(s, (int16_t)(x + r), (int16_t)(y + h - t), (int16_t)(w - 2 * r), t, c);
        gfx2_fill(s, x, (int16_t)(y + r), t, (int16_t)(h - 2 * r), c);
        gfx2_fill(s, (int16_t)(x + w - t), (int16_t)(y + r), t, (int16_t)(h - 2 * r), c);

        /* cuatro cajas de esquina, r x r */
        for (iy = 0; iy < r; iy++) {
            for (ix = 0; ix < r; ix++) {
                int16_t xs[2], ys[2];
                xs[0] = (int16_t)(x + ix);         xs[1] = (int16_t)(x + w - 1 - ix);
                ys[0] = (int16_t)(y + iy);         ys[1] = (int16_t)(y + h - 1 - iy);
                for (k = 0; k < 4; k++) {
                    int16_t sx = xs[k & 1], sy = ys[(k >> 1) & 1];
                    uint16_t outer, inner;
                    int16_t ri = (int16_t)(r - t);
                    if (sy < s->y || sy >= (int16_t)(s->y + s->h)) continue;
                    outer = rr_cov(sx, sy, x, y, w, h, r);
                    if (!outer) continue;
                    if (ri < 0) ri = 0;
                    inner = rr_cov(sx, sy, (int16_t)(x + t), (int16_t)(y + t),
                                   (int16_t)(w - 2 * t), (int16_t)(h - 2 * t), ri);
                    if (outer > inner) {
                        px_blend(s, sx, sy, c, (uint16_t)(outer - inner));
                    }
                }
            }
        }
    } else {
        /* sin radio: cuatro rectangulos, nada de supermuestreo */
        gfx2_fill(s, x, y, w, t, c);
        gfx2_fill(s, x, (int16_t)(y + h - t), w, t, c);
        gfx2_fill(s, x, y, t, h, c);
        gfx2_fill(s, (int16_t)(x + w - t), y, t, h, c);
    }
}

/* ------------------------------------------------------------------------
 * Texto
 * ------------------------------------------------------------------------ */

static const gfx2_glyph_t *glyph_for(const gfx2_font_t *f, uint16_t cp)
{
    uint8_t i;
    for (i = 0; i < f->n_ranges; i++) {
        if (cp >= f->ranges[i].lo && cp <= f->ranges[i].hi) {
            return &f->glyphs[f->ranges[i].first + (cp - f->ranges[i].lo)];
        }
    }
    /* sin glyph: caemos al espacio, que siempre esta en el primer rango */
    if (' ' >= f->ranges[0].lo && ' ' <= f->ranges[0].hi) {
        return &f->glyphs[f->ranges[0].first + (' ' - f->ranges[0].lo)];
    }
    return &f->glyphs[0];
}

/* Decodifica UTF-8 de 1, 2 y 3 bytes. */
static const char *next_cp(const char *p, uint16_t *cp)
{
    uint8_t c = (uint8_t)*p;
    if (c < 0x80) { *cp = c; return p + 1; }
    /* Tres bytes: hace falta desde que la pantalla de arranque escribe
     * katakana (U+30A2 y vecinos). Sin esto, cada katakana se leia como tres
     * bytes sueltos y salian tres caracteres de basura por celda. */
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cp = (uint16_t)(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        return p + 3;
    }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cp = (uint16_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
        return p + 2;
    }
    *cp = c; /* byte suelto: lo tratamos como Latin-1 */
    return p + 1;
}

int16_t gfx2_text_w(const char *str, const gfx2_font_t *f)
{
    int16_t wsum = 0;
    uint16_t cp;
    while (*str) {
        str = next_cp(str, &cp);
        wsum = (int16_t)(wsum + glyph_for(f, cp)->adv);
    }
    return wsum;
}

int16_t gfx2_text(gfx2_surf_t *s, int16_t x, int16_t y, const char *str,
                  const gfx2_font_t *f, gfx2_rgba_t c)
{
    int16_t cx = x;
    uint16_t cp;

    while (*str) {
        const gfx2_glyph_t *g;
        int16_t gy, gx;

        str = next_cp(str, &cp);
        g = glyph_for(f, cp);

        if (g->w && g->h) {
            /* Una sola pasada sobre la tinta del glyph, mezclando la
             * cobertura de 4 bpp. Comparar con gfx_char() del firmware
             * actual, que abria una ventana del panel por cada pixel. */
            const uint8_t *bm = &f->bitmap[g->off];
            int16_t row_bytes = (int16_t)((g->w + 1) / 2);

            for (gy = 0; gy < g->h; gy++) {
                int16_t sy = (int16_t)(y + g->by + gy);
                if (sy < s->y || sy >= (int16_t)(s->y + s->h)) continue;
                for (gx = 0; gx < g->w; gx++) {
                    uint8_t byte = bm[gy * row_bytes + (gx >> 1)];
                    uint8_t nib = (gx & 1) ? (uint8_t)(byte & 0x0F)
                                           : (uint8_t)(byte >> 4);
                    if (nib) {
                        /* 0..15 -> 0..255 sin perder el extremo */
                        uint16_t cov = (uint16_t)((nib * 255u) / 15u);
                        px_blend(s, (int16_t)(cx + g->bx + gx), sy, c, cov);
                    }
                }
            }
        }
        cx = (int16_t)(cx + g->adv);
    }
    return (int16_t)(cx - x);
}

void gfx2_text_in(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w,
                  const char *str, const gfx2_font_t *f, gfx2_rgba_t c,
                  gfx2_align_t al)
{
    int16_t tw = gfx2_text_w(str, f);
    int16_t tx = x;
    if (al == GFX2_ALIGN_C) tx = (int16_t)(x + (w - tw) / 2);
    else if (al == GFX2_ALIGN_R) tx = (int16_t)(x + w - tw);
    gfx2_text(s, tx, y, str, f, c);
}

/* ------------------------------------------------------------------------
 * Waterfall
 * ------------------------------------------------------------------------ */

void gfx2_wf_blit(int16_t x, int16_t y, int16_t w, int16_t rows,
                  const uint8_t *idx, int16_t stride, int16_t head,
                  const uint16_t *cmap)
{
    /* Se vuelca por trozos de GFX2_BAND_H filas reutilizando la banda, de
     * forma que solo hace falta UNA ventana por trozo y el buffer de
     * historial puede seguir siendo de 8 bits. */
    int16_t done = 0;

    while (done < rows) {
        int16_t chunk = (int16_t)(rows - done);
        int16_t r, i;
        int32_t n;

        if (chunk > GFX2_BAND_H) chunk = GFX2_BAND_H;

        for (r = 0; r < chunk; r++) {
            int16_t src = (int16_t)((head + done + r) % rows);
            const uint8_t *sp = &idx[(int32_t)src * stride];
            uint16_t *dp = &s_band[(int32_t)r * w];
            for (i = 0; i < w; i++) dp[i] = cmap[sp[i]];
        }

        rm68120_set_window((uint16_t)x, (uint16_t)(y + done),
                           (uint16_t)(x + w - 1), (uint16_t)(y + done + chunk - 1));
        n = (int32_t)w * chunk;
        for (i = 0; i < n; i++) rm68120_write_data(s_band[i]);

        done = (int16_t)(done + chunk);
    }
}

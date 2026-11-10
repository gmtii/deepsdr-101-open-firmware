/*
 * QUE PINTAR TEXTO SIGA PINTANDO LOS MISMOS PIXELES.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * gfx2_text_uno() es por donde pasa TODA la letra de la radio: el dial, la
 * franja de arriba, la marquesina de RDS, el panel digital, los menus, los
 * avisos. Medido en deepsdr-ui/m4/m4_dibujo.c, era tambien la partida mas
 * gorda de dibujar: 4.752 instrucciones por letra de catorce puntos, y la
 * franja de arriba entera 4,25 millones -21 ms-.
 *
 * El bucle llamaba a px_blend() punto por punto, y px_blend() vuelve a
 * preguntarse cada vez si esa x cae dentro de la ventana de recorte y
 * dentro de la banda. La respuesta es la misma para toda la fila. Se
 * calcularon los dos limites una vez por glifo y el bucle pasó a recorrer
 * solo lo que se escribe: la letra bajo a 2.723 instrucciones -un 43 %
 * menos- y la franja de arriba a 3,74 millones.
 *
 * PERO EL RECORTE NO ES UN DETALLE. Si los limites salen mal por uno, lo
 * que aparece es una columna de pixeles de una letra metida en el panel de
 * al lado, o media cifra del dial comida. Y eso no se ve en un banco que
 * mire contadores: hay que mirar LOS PIXELES.
 *
 * Asi que aqui esta el bucle de ANTES, copiado, y se exige que las dos
 * versiones dejen el buffer IDENTICO -memcmp, no "parecido"- en los casos
 * que de verdad pasan:
 *
 *   - el texto entero dentro de la banda, que es lo normal;
 *   - asomando por la izquierda y por la derecha, que es la marquesina;
 *   - con la ventana de recorte en X puesta, que es lo que usa la
 *     cabecera para no pisar al vecino;
 *   - con el origen MUY a la izquierda -un desplazamiento de marquesina de
 *     treinta mil- que es donde los 16 bits con signo se desbordan;
 *   - y en bandas de ocho filas, que es como compone la radio de verdad,
 *     con el texto cortado por arriba y por abajo.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "gfx2.c"
#include "font_ui_11.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_num_44.h"

/* ---------------------------------------------------------------------
 * EL BUCLE DE ANTES, TAL CUAL ESTABA. No se toca ni para arreglarle el
 * estilo: si se toca, deja de ser la referencia.
 * --------------------------------------------------------------------- */
static int16_t texto_uno_viejo(gfx2_surf_t *s, int16_t cx, int16_t y,
                               uint16_t cp, const gfx2_font_t *f, gfx2_rgba_t c)
{
    const gfx2_glyph_t *g = glyph_for(f, cp);
    int16_t gy, gx;

    if ((int16_t)(cx + g->bx + g->w) <= s->x
        || (int16_t)(cx + g->bx) >= (int16_t)(s->x + s->w)) {
        return g->adv;
    }
    if (!gfx2_band_hits(s, (int16_t)(y + g->by), (int16_t)g->h)) {
        return g->adv;
    }

    if (g->w && g->h) {
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
                    uint16_t cov = (uint16_t)((nib * 255u) / 15u);
                    px_blend(s, (int16_t)(cx + g->bx + gx), sy, c, cov);
                }
            }
        }
    }
    return g->adv;
}

static int16_t texto_viejo(gfx2_surf_t *s, int16_t x, int16_t y,
                           const char *str, const gfx2_font_t *f,
                           gfx2_rgba_t c)
{
    int16_t cx = x;
    uint16_t cp;

    if (str == 0) { return cx; }
    while (*str) {
        str = next_cp(str, &cp);
        cx = (int16_t)(cx + texto_uno_viejo(s, cx, y, cp, f, c));
    }
    return (int16_t)(cx - x);
}

/* --------------------------------------------------------------------- */

#define ANCHO  800
#define ALTO   64

static uint16_t g_a[ANCHO * ALTO];
static uint16_t g_b[ANCHO * ALTO];

static int g_fallos;

/* Un fondo que no sea liso: si lo fuera, una mezcla mal hecha podria dar
 * el mismo color por casualidad. */
static void fondo(uint16_t *p, int16_t w, int16_t h)
{
    int32_t i;
    for (i = 0; i < (int32_t)w * h; i++) {
        p[i] = (uint16_t)((i * 2654435761u) >> 16);
    }
}

static const char *const k_txt[] = {
    "RADIO ES  14.230.0 kHz  USB",
    "EA4HEW IN80 37",
    "Wj|()[]_,.;:  0123456789",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
};
#define N_TXT ((int)(sizeof k_txt / sizeof k_txt[0]))

static void caso(const char *que, int16_t x, int16_t y,
                 int16_t bx, int16_t by, int16_t bw, int16_t bh,
                 int16_t rec0, int16_t rec1, const gfx2_font_t *f)
{
    gfx2_surf_t sa, sb;
    int t, malos = 0;
    int32_t primero = -1;

    for (t = 0; t < N_TXT; t++) {
        int32_t i;

        fondo(g_a, bw, bh);
        fondo(g_b, bw, bh);

        sa.px = g_a; sa.x = bx; sa.y = by; sa.w = bw; sa.h = bh;
        sb.px = g_b; sb.x = bx; sb.y = by; sb.w = bw; sb.h = bh;

        if (rec1 > rec0) { gfx2_recorte_x(rec0, rec1); }
        else             { gfx2_recorte_quita(); }
        (void)texto_viejo(&sa, x, y, k_txt[t], f, gfx2_rgb(0xE8ECF2));

        if (rec1 > rec0) { gfx2_recorte_x(rec0, rec1); }
        else             { gfx2_recorte_quita(); }
        (void)gfx2_text(&sb, x, y, k_txt[t], f, gfx2_rgb(0xE8ECF2));

        gfx2_recorte_quita();

        for (i = 0; i < (int32_t)bw * bh; i++) {
            if (g_a[i] != g_b[i]) {
                if (primero < 0) { primero = i; }
                malos++;
            }
        }
    }

    printf("    %-46s %s", que, malos ? "MAL " : "igual");
    if (malos) {
        printf("  %d pixeles distintos, el primero en x=%d y=%d\n",
               malos, (int)(primero % bw), (int)(primero / bw));
        g_fallos++;
    } else {
        printf("\n");
    }
}

int main(void)
{
    printf("== Pintar texto: el bucle de antes y el de ahora, pixel a pixel ==\n\n");

    printf("  la banda entera (800x64)\n");
    caso("el texto dentro, en su sitio",        40, 20,   0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);
    caso("asomando por la izquierda",         -120, 20,   0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);
    caso("asomando por la derecha",            740, 20,   0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);
    caso("entero fuera por la izquierda",    -2000, 20,   0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);
    caso("el origen a treinta mil (marquesina)", -30000, 20, 0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);
    caso("y a treinta mil por la derecha",   30000, 20,   0, 0, ANCHO, ALTO,
         0, 0, &font_ui_14);

    printf("\n  con la ventana de recorte en X puesta\n");
    caso("recorte 200..600, texto dentro",      40, 20,   0, 0, ANCHO, ALTO,
         200, 600, &font_ui_14);
    caso("recorte 200..600, texto a caballo",  180, 20,   0, 0, ANCHO, ALTO,
         200, 600, &font_ui_14);
    caso("recorte 200..600, texto fuera",      620, 20,   0, 0, ANCHO, ALTO,
         200, 600, &font_ui_14);
    caso("recorte de un solo pixel",           100, 20,   0, 0, ANCHO, ALTO,
         300, 301, &font_ui_14);

    printf("\n  por bandas de ocho filas, como compone la radio\n");
    {
        int16_t by;
        char n[64];
        for (by = 0; by < 40; by += 8) {
            sprintf(n, "banda y=%d..%d", (int)by, (int)(by + 7));
            caso(n, 40, 20, 0, by, ANCHO, 8, 0, 0, &font_ui_14);
        }
    }

    printf("\n  y una banda estrecha, que no empieza en x=0\n");
    caso("banda x=300 ancho 200",              260, 20, 300, 16, 200, 24,
         0, 0, &font_ui_14);
    caso("banda x=300 ancho 200, con recorte",  260, 20, 300, 16, 200, 24,
         320, 420, &font_ui_14);

    printf("\n  las otras fuentes\n");
    caso("ui_11",       40, 20, 0, 0, ANCHO, ALTO, 0, 0, &font_ui_11);
    caso("ui_14b",      40, 20, 0, 0, ANCHO, ALTO, 0, 0, &font_ui_14b);
    caso("ui_18b",      40, 10, 0, 0, ANCHO, ALTO, 0, 0, &font_ui_18b);
    caso("num_44 (el dial)", 40, 2, 0, 0, ANCHO, ALTO, 0, 0, &font_num_44);

    printf("\n");
    if (g_fallos) {
        printf("  Por aqui pasa toda la letra de la radio. Un limite que se\n");
        printf("  pasa por uno es media cifra del dial comida o una columna\n");
        printf("  de pixeles metida en el panel de al lado.\n");
        printf("TEXTO IGUAL: HAY FALLOS (%d)\n", g_fallos);
        return 1;
    }
    printf("TODO CORRECTO (0 fallos)\n");
    return 0;
}

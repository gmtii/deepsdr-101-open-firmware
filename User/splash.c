#include "splash.h"
#include "gfx2.h"
#include "palette.h"
#include "font_title_60.h"
#include "font_rain_16.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"

/* --- rejilla de la lluvia ------------------------------------------------ */
#define CELL_W   18
#define CELL_H   20
#define COLS     (GFX2_W / CELL_W)        /* 44 */
#define ROWS     (GFX2_H / CELL_H)        /* 24 */

/* Los colores salen de la paleta de la interfaz - los mismos grises y el
 * mismo ambar de la pantalla principal, no una escala verde aparte. La
 * cabeza es la tinta mas clara, que es lo que hace que se lea como "aqui
 * esta cayendo ahora" sin necesidad de brillo. */
#define VERDE_CABEZA  PAL_SP_HEAD
#define VERDE_VIVO    PAL_SP_HOT
#define VERDE_MEDIO   PAL_SP_MID
#define VERDE_OSCURO  PAL_SP_DIM
#define NEGRO         PAL_SP_BG

/* --- tiempos ------------------------------------------------------------- */
#define T_TITULO   1500U   /* cuando aparece el titulo */
#define T_FIN      3600U   /* cuando termina todo */
#define PASO_MS      55U   /* periodo de un fotograma */

/* Banda del titulo: la lluvia sigue cayendo por encima y por debajo. */
#define TIT_Y       168
#define TIT_H       150
#define CRED_H       26   /* franja de creditos, pegada abajo */
#define CRED_Y      (GFX2_H - CRED_H)

typedef struct {
    int8_t   cabeza;      /* fila de la cabeza; puede ir negativa al entrar */
    uint8_t  periodo;     /* fotogramas por paso: la velocidad de la columna */
    uint8_t  ctr;
    uint8_t  largo;       /* longitud de la estela */
    uint8_t  glifo[5];    /* los ultimos cinco caracteres, para repintarlos
                           * mas apagados sin tener que guardar la rejilla */
} col_t;

static col_t   s_col[COLS];
static uint32_t s_rnd = 0x1234567u;
static uint32_t s_prox;        /* ms del siguiente fotograma */
static uint8_t  s_titulo_hecho;

/* Congruencial lineal: no hace falta nada mejor para decidir que katakana
 * cae, y no arrastra rand() de la libreria. */
static uint32_t rnd(void)
{
    s_rnd = s_rnd * 1103515245u + 12345u;
    return (s_rnd >> 16) & 0x7FFFu;
}

static uint8_t n_glifos(void)
{
    /* Cuantos caracteres tiene la fuente de lluvia, contando sus rangos: asi
     * si manana se le anaden o quitan katakana, esto no se queda corto ni se
     * sale de la tabla. */
    uint8_t i, n = 0;
    for (i = 0; i < font_rain_16.n_ranges; i++) {
        n = (uint8_t)(n + (font_rain_16.ranges[i].hi - font_rain_16.ranges[i].lo + 1));
    }
    return n;
}

/* Codepoint del glifo numero `k` de la fuente de lluvia. */
static uint16_t glifo_cp(uint8_t k)
{
    uint8_t i;
    for (i = 0; i < font_rain_16.n_ranges; i++) {
        uint16_t n = (uint16_t)(font_rain_16.ranges[i].hi - font_rain_16.ranges[i].lo + 1);
        if (k < n) { return (uint16_t)(font_rain_16.ranges[i].lo + k); }
        k = (uint8_t)(k - n);
    }
    return font_rain_16.ranges[0].lo;
}

/* --- dibujo de una celda ------------------------------------------------- */
typedef struct { int16_t x, y; uint16_t cp; uint32_t color; } celda_t;

static void utf8(uint16_t cp, char *b)
{
    /* gfx2_text lee UTF-8; los katakana caen en el tramo de tres bytes. */
    if (cp < 0x80u) { b[0] = (char)cp; b[1] = 0; }
    else if (cp < 0x800u) {
        b[0] = (char)(0xC0u | (cp >> 6)); b[1] = (char)(0x80u | (cp & 0x3Fu)); b[2] = 0;
    } else {
        b[0] = (char)(0xE0u | (cp >> 12));
        b[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        b[2] = (char)(0x80u | (cp & 0x3Fu));
        b[3] = 0;
    }
}

static void pinta_celda(gfx2_surf_t *s, void *ctx)
{
    const celda_t *c = (const celda_t *)ctx;
    char b[5];

    gfx2_fill(s, c->x, c->y, CELL_W, CELL_H, gfx2_rgb(NEGRO));
    if (c->color == PAL_SP_BG) { return; }
    utf8(c->cp, b);
    gfx2_text_in(s, c->x, (int16_t)(c->y + 1), CELL_W, b, &font_rain_16,
                 gfx2_rgb(c->color), GFX2_ALIGN_C);
}

static void celda(uint8_t col, int16_t fila, uint16_t cp, uint32_t color)
{
    celda_t c;

    if (fila < 0 || fila >= ROWS) { return; }
    c.x = (int16_t)(col * CELL_W);
    c.y = (int16_t)(fila * CELL_H);
    /* La banda del titulo, una vez puesta, no se repinta: la lluvia que
     * cruzaria por ahi se salta. Asi el titulo no parpadea ni hace falta
     * volver a dibujarlo en cada fotograma. */
    if (s_titulo_hecho &&
        ((c.y + CELL_H > TIT_Y && c.y < TIT_Y + TIT_H) || c.y + CELL_H > CRED_Y)) {
        return;
    }
    c.cp = cp;
    c.color = color;
    gfx2_render(c.x, c.y, CELL_W, CELL_H, pinta_celda, &c);
}

/* --- titulo -------------------------------------------------------------- */
static void pinta_titulo(gfx2_surf_t *s, void *ctx)
{
    int16_t w;
    (void)ctx;

    if (!gfx2_band_hits(s, TIT_Y, TIT_H)) { return; }

    gfx2_fill(s, 0, TIT_Y, GFX2_W, TIT_H, gfx2_rgb(NEGRO));
    gfx2_hline(s, 0, TIT_Y, GFX2_W, gfx2_rgb(PAL_LINE));
    gfx2_hline(s, 0, (int16_t)(TIT_Y + TIT_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));

    /* "DeepSDR" centrado, y debajo "reload" separado con espaciado amplio:
     * dos pesos distintos leen mejor que la misma palabra dos veces. */
    w = gfx2_text_w("DeepSDR", &font_title_60);
    gfx2_text(s, (int16_t)((GFX2_W - w) / 2), (int16_t)(TIT_Y + 16), "DeepSDR",
              &font_title_60, gfx2_rgb(PAL_SP_TITLE));

    {
        /* "r e l o a d" letra a letra para poder separarlas: la fuente no
         * tiene espaciado ajustable y juntas se leen como una palabra mas,
         * no como un subtitulo. */
        const char *p = "reload";
        int16_t sep = 10, total = 0, x;
        const char *q;

        for (q = p; *q; q++) {
            char c[2]; c[0] = *q; c[1] = 0;
            total = (int16_t)(total + gfx2_text_w(c, &font_ui_18b) + sep);
        }
        total = (int16_t)(total - sep);
        x = (int16_t)((GFX2_W - total) / 2);
        for (q = p; *q; q++) {
            char c[2]; c[0] = *q; c[1] = 0;
            x = (int16_t)(x + gfx2_text(s, x, (int16_t)(TIT_Y + 100), c,
                                        &font_ui_18b, gfx2_rgb(PAL_SP_SUB)) + sep);
        }
    }
}

/* Creditos abajo: esto es firmware de otros con una interfaz nueva encima, y
 * la pantalla de arranque es donde eso se dice. */
static void pinta_creditos(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    if (!gfx2_band_hits(s, CRED_Y, CRED_H)) { return; }
    gfx2_fill(s, 0, CRED_Y, GFX2_W, CRED_H, gfx2_rgb(NEGRO));
    gfx2_text_in(s, 0, (int16_t)(CRED_Y + 6), GFX2_W,
                 "sobre el firmware de EA8DGL · UA6YKK · EA7GIB  ·  CC BY-NC-SA",
                 &font_ui_14, gfx2_rgb(PAL_SP_DIM), GFX2_ALIGN_C);
}

/* --- animacion ----------------------------------------------------------- */
static void limpia(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(NEGRO));
}

void splash_reinicia(void)
{
    uint8_t i, j;

    s_rnd = 0x1234567u;
    s_titulo_hecho = 0U;
    s_prox = 0U;

    for (i = 0; i < COLS; i++) {
        /* Cada columna arranca a una altura y a una velocidad distintas: si
         * todas empiezan arriba y a la vez, se ve una cortina bajando, no
         * lluvia. */
        /* Arranque escalonado pero CORTO: con cabezas hasta 30 filas por
         * encima, a medio segundo apenas habia entrado un tercio de las
         * columnas y la pantalla se veia vacia justo cuando mas mira uno. */
        s_col[i].cabeza  = (int8_t)(-(int8_t)(rnd() % 12u));
        s_col[i].periodo = (uint8_t)(1u + rnd() % 3u);
        s_col[i].ctr     = (uint8_t)(rnd() % 3u);
        s_col[i].largo   = (uint8_t)(8u + rnd() % 14u);
        for (j = 0; j < 5; j++) { s_col[i].glifo[j] = (uint8_t)(rnd() % n_glifos()); }
    }
    gfx2_render_screen(limpia, 0);
}

uint8_t splash_paso(uint32_t t)
{
    uint8_t i;

    if (t >= T_FIN) { return 0U; }

    if (t < s_prox) { return 1U; }
    s_prox = t + PASO_MS;

    if (!s_titulo_hecho && t >= T_TITULO) {
        s_titulo_hecho = 1U;
        gfx2_render(0, TIT_Y, GFX2_W, TIT_H, pinta_titulo, 0);
        gfx2_render(0, CRED_Y, GFX2_W, CRED_H, pinta_creditos, 0);
    }

    for (i = 0; i < COLS; i++) {
        col_t *c = &s_col[i];

        if (++c->ctr < c->periodo) { continue; }
        c->ctr = 0U;

        /* Se desplaza la memoria de los ultimos glifos y se sortea el nuevo. */
        c->glifo[4] = c->glifo[3];
        c->glifo[3] = c->glifo[2];
        c->glifo[2] = c->glifo[1];
        c->glifo[1] = c->glifo[0];
        c->glifo[0] = (uint8_t)(rnd() % n_glifos());
        c->cabeza++;

        /* Cinco celdas por paso, y el degradado de la estela sale solo: cada
         * celda se pinta una vez de cabeza, otra de vivo, otra de medio y
         * otra de oscuro segun la cabeza se aleja, y ahi se queda hasta que
         * la cola la borra. No hace falta guardar la rejilla entera, solo
         * los ultimos cinco caracteres de cada columna. */
        celda(i, c->cabeza,                    glifo_cp(c->glifo[0]), VERDE_CABEZA);
        celda(i, (int16_t)(c->cabeza - 1),     glifo_cp(c->glifo[1]), VERDE_VIVO);
        celda(i, (int16_t)(c->cabeza - 2),     glifo_cp(c->glifo[2]), VERDE_MEDIO);
        celda(i, (int16_t)(c->cabeza - 4),     glifo_cp(c->glifo[4]), VERDE_OSCURO);
        celda(i, (int16_t)(c->cabeza - c->largo), 0u, PAL_SP_BG);

        if (c->cabeza - (int16_t)c->largo > ROWS) {
            /* Fuera de pantalla: vuelve a caer desde arriba, con velocidad y
             * largo nuevos para que no se note el ciclo. */
            c->cabeza  = (int8_t)(-(int8_t)(rnd() % 10u));
            c->periodo = (uint8_t)(1u + rnd() % 3u);
            c->largo   = (uint8_t)(8u + rnd() % 14u);
        }
    }
    return 1U;
}

void splash_run(splash_ms_fn ms)
{
    uint32_t t0 = ms();

    splash_reinicia();
    while (splash_paso(ms() - t0)) {
        /* nada que hacer: el arranque esta parado aqui a proposito */
    }
}

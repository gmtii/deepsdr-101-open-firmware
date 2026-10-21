/*
 * COMO SE VERIA EL CARGADOR CON OTRA FUENTE - 17/10/2026.
 *
 * El cargador de arranque gasta 11.314 de sus 41.212 bytes -el 27%- en
 * font_ui_18b, la negrita de 19 px. Si baja de 32.768 cabe en dos
 * sectores en vez de tres y la aplicacion gana 16.384 bytes, que es mas
 * de seis veces lo que le queda libre hoy.
 *
 * ANTES DE DECIDIR ESO HAY QUE VERLO. Por eso este banco no calcula
 * tamaños: PINTA las tres pantallas del cargador -la de actualizacion,
 * la de grabando y la de no arranca- con cada fuente candidata, con los
 * mismos textos y las mismas coordenadas, y guarda un PPM de cada una.
 *
 * Y MIDE LO QUE NO SE VE EN UNA CAPTURA: el ancho de cada rotulo. Eso
 * importa porque texto_sobre() CORTA en la columna 790, y ya ha pasado:
 * el aviso de los 512 kB medía 919 px con esta fuente y se quedaba a
 * medias (ver ficha() en boot_main.c). Una fuente mas pequeña no solo
 * ahorra flash, tambien deja de cortar.
 *
 * DE DONDE SALE EL CODIGO DE DIBUJO. letra(), texto_sobre() y
 * ancho_texto() estan COPIADAS de boot/boot_main.c, sin tocar una coma.
 * Se pueden copiar porque no dependen de nada del cargador: hablan con
 * rm68120_set_window()/rm68120_write_data(), que es justo lo que
 * sim/lcd_sim.c implementa. Si algun dia el cargador cambia su forma de
 * pintar y esto no, las capturas dejan de valer: por eso, si este
 * cambio sale adelante, lo primero es sacar esas tres funciones a su
 * propio fichero y que las compilen los dos.
 */

#include "lcd_sim.h"
#include "gfx2.h"
#include "gfx2_font.h"
#include "font_ui_18b.h"
#include "font_ui_14b.h"
#include "font_ui_14.h"
#include "font_ui_11.h"

/* Las tres del panel que implementa sim/lcd_sim.c. */
void rm68120_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void rm68120_write_data(uint16_t data);
void rm68120_fill_screen(uint16_t color);

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Los colores del cargador, tal cual estan en boot_main.c. */
#define BLANCO    0xFFFFU
#define VERDE     0x07E0U
#define ROJO      0xF800U
#define AMBAR     0xFD20U
#define GRIS      0x8410U
#define FONDO     0x0861U
#define BARRA     0x18C3U
#define ACENTO    0x3D7FU
#define ETIQUETA  0xA534U
#define VALOR     0xE73CU
#define COL_VALOR 150U

/* La fuente en curso: en el cargador es un #define, aqui una variable
 * para poder pintar las cuatro seguidas. */
static const gfx2_font_t *s_f;
static uint16_t           s_alto_lin;

/* --- COPIADO DE boot_main.c ----------------------------------------- */
static const gfx2_glyph_t *glifo(const gfx2_font_t *f, uint16_t cp)
{
    uint8_t i;
    for (i = 0U; i < f->n_ranges; i++) {
        if (cp >= f->ranges[i].lo && cp <= f->ranges[i].hi) {
            return &f->glyphs[f->ranges[i].first + (cp - f->ranges[i].lo)];
        }
    }
    return (const gfx2_glyph_t *)0;
}

static uint16_t mezcla(uint16_t fondo, uint16_t tinta, uint8_t a)
{
    uint32_t rf = (fondo >> 11) & 0x1FU, gf = (fondo >> 5) & 0x3FU, bf = fondo & 0x1FU;
    uint32_t rt = (tinta >> 11) & 0x1FU, gt = (tinta >> 5) & 0x3FU, bt = tinta & 0x1FU;
    uint32_t r = (rf * (15U - a) + rt * a) / 15U;
    uint32_t g = (gf * (15U - a) + gt * a) / 15U;
    uint32_t b = (bf * (15U - a) + bt * a) / 15U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t letra(uint16_t x, uint16_t y, char c, uint16_t tinta, uint16_t fondo)
{
    const gfx2_font_t  *f = s_f;
    const gfx2_glyph_t *g = glifo(f, (uint16_t)(unsigned char)c);
    uint16_t fil, col, x0, y0;
    uint32_t fuera;

    if (g == (const gfx2_glyph_t *)0) { g = glifo(f, (uint16_t)'?'); }
    if (g == (const gfx2_glyph_t *)0) { return x; }
    if (g->w == 0U || g->h == 0U)     { return (uint16_t)(x + g->adv); }

    x0 = (uint16_t)((int32_t)x + g->bx);
    y0 = (uint16_t)((int32_t)y + g->by);

    rm68120_set_window(x0, y0, (uint16_t)(x0 + g->w - 1U), (uint16_t)(y0 + g->h - 1U));

    fuera = ((uint32_t)g->w + 1U) >> 1;
    for (fil = 0U; fil < g->h; fil++) {
        const uint8_t *r = &f->bitmap[g->off + (uint32_t)fil * fuera];
        for (col = 0U; col < g->w; col++) {
            uint8_t a = (col & 1U) ? (uint8_t)(r[col >> 1] & 0x0FU)
                                   : (uint8_t)(r[col >> 1] >> 4);
            rm68120_write_data(a == 0U ? fondo
                             : (a == 15U ? tinta : mezcla(fondo, tinta, a)));
        }
    }
    return (uint16_t)(x + g->adv);
}

static void texto_sobre(uint16_t x, uint16_t y, const char *s,
                        uint16_t tinta, uint16_t fondo)
{
    while (*s != '\0' && x < 790U) {
        x = letra(x, y, *s++, tinta, fondo);
    }
}

static void texto(uint16_t x, uint16_t y, const char *s, uint16_t tinta)
{
    texto_sobre(x, y, s, tinta, FONDO);
}

static uint16_t ancho_texto(const char *s)
{
    const gfx2_font_t *f = s_f;
    uint16_t w = 0U;
    while (*s != '\0') {
        const gfx2_glyph_t *g = glifo(f, (uint16_t)(unsigned char)*s++);
        if (g != (const gfx2_glyph_t *)0) { w = (uint16_t)(w + g->adv); }
    }
    return w;
}

static void texto_derecha(uint16_t borde, uint16_t y, const char *s,
                          uint16_t tinta, uint16_t fondo)
{
    uint16_t w = ancho_texto(s);
    texto_sobre((uint16_t)((borde > w) ? (borde - w) : 0U), y, s, tinta, fondo);
}

static void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint16_t i, j;
    rm68120_set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    for (j = 0U; j < h; j++) { for (i = 0U; i < w; i++) { rm68120_write_data(c); } }
}

static void fila(uint16_t y, const char *etiq, const char *valor)
{
    uint16_t x = 24U;
    while (*etiq != '\0' && x < (COL_VALOR - 10U)) {
        x = letra(x, y, *etiq++, ETIQUETA, FONDO);
    }
    texto(COL_VALOR, y, valor, VALOR);
}

static void cabecera(const char *titulo, const char *pie)
{
    rect(0U, 0U, 800U, 46U, BARRA);
    rect(0U, 46U, 800U, 2U, ACENTO);
    texto_sobre(24U, 12U, titulo, ACENTO, BARRA);
    texto_derecha(776U, 12U, "v2.16  17/10", ETIQUETA, BARRA);
    if (pie != (const char *)0) { texto(24U, 58U, pie, ETIQUETA); }
}
/* --- fin de lo copiado ---------------------------------------------- */

/*
 * LA FICHA, con los valores que de verdad salen en una radio: los de un
 * GD32F450VET6 con el W25Q16 de fabrica. No son de adorno: lo que se
 * mide aqui es si esas lineas CABEN, y una linea de ejemplo mas corta
 * que la de verdad diria que si cuando no.
 */
static const char *k_etiq[6] = { "Chip", "Serie", "Reloj", "Panel",
                                 "Flash SPI", "Volumen" };
static const char *k_valor[6] = {
    "flash 512 kB     SRAM 192 kB",
    "0034003A 31375116 20393759",
    "199 MHz     depuracion 00000000",
    "rama B firme  0x000A=0000  0x3A00=0080",
    "EF 40 15     2048 kB",
    "1 MB  (disco 1008 kB)"
};

static uint16_t ficha(void)
{
    uint16_t y = 100U, i;
    for (i = 0U; i < 6U; i++) {
        fila(y, k_etiq[i], k_valor[i]);
        y = (uint16_t)(y + (uint16_t)(s_alto_lin + 5U));
    }
    return (y > 296U) ? y : 296U;
}

/* Las tres pantallas, con los textos exactos de boot_main.c. */
static void pant_actualizacion(void)
{
    uint16_t y;
    rm68120_fill_screen(FONDO);
    cabecera("MODO ACTUALIZACION",
             "La radio es un disco USB. Copia update.bin y reinicia.");
    y = ficha();
    texto(16U, y, "Disco  2016 bloques de 512  = 1008 kB", BLANCO);
    y = (uint16_t)(y + s_alto_lin + 11U);
    texto(16U, y, "DFU de fabrica: al encender, manten el mando pulsado Y GIRALO.", GRIS);
    texto(16U, 360U, "USB: listo. Copia update.bin y reinicia.", VERDE);
    texto(16U, 390U, "Para arrancar la radio: apaga y enciende SIN pulsar.", GRIS);
}

static void pant_grabando(void)
{
    uint16_t y;
    rm68120_fill_screen(FONDO);
    cabecera("DEEPSDR 101  -  actualizando",
             "No apagues la radio hasta que termine.");
    y = ficha();
    texto(24U, (uint16_t)(y + 4U), "Grabando update.bin en la flash interna...", AMBAR);
    rect(24U, (uint16_t)(y + 44U), 752U, 28U, BARRA);
    rect(24U, (uint16_t)(y + 44U), 500U, 28U, ACENTO);
}

static void pant_no_arranca(void)
{
    uint16_t y;
    rm68120_fill_screen(FONDO);
    cabecera("DEEPSDR 101  -  no se puede arrancar", (const char *)0);
    y = ficha();
    texto(24U, (uint16_t)(y + 4U), "la cabecera no cuadra con el contenido", ROJO);
    texto(24U, (uint16_t)(y + s_alto_lin + 11U),
          "Copia un update.bin valido al disco USB:", ETIQUETA);
    texto(24U, (uint16_t)(y + 2U * s_alto_lin + 18U),
          "enciende con el mando pulsado.", ETIQUETA);
}

/*
 * LO QUE MAS LARGO SE PINTA EN EL CARGADOR. El aviso de los 512 kB es el
 * que ya se salio una vez; los demas estan aqui para que si alguno se
 * acerca al borde se vea antes y no en una radio.
 */
static const char *k_largas[] = {
    "DFU de fabrica: al encender, manten el mando pulsado Y GIRALO.",
    "AVISO: mas de 512 kB de flash; parte de la aplicacion",
    "La radio es un disco USB. Copia update.bin y reinicia.",
    "Para arrancar la radio: apaga y enciende SIN pulsar.",
    "Grabando update.bin en la flash interna...",
    "DEEPSDR 101  -  no se puede arrancar",
    "rama B firme  0x000A=0000  0x3A00=0080",
    "199 MHz     depuracion 00000000",
};

static void guarda(const char *nombre)
{
    const uint16_t *fb = lcd_sim_fb();
    FILE *f = fopen(nombre, "wb");
    int i;
    if (f == 0) { return; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (i = 0; i < LCD_W * LCD_H; i++) {
        uint16_t c = fb[i];
        unsigned char p[3];
        p[0] = (unsigned char)((((c >> 11) & 0x1FU) * 255U) / 31U);
        p[1] = (unsigned char)((((c >> 5)  & 0x3FU) * 255U) / 63U);
        p[2] = (unsigned char)(((c & 0x1FU) * 255U) / 31U);
        fwrite(p, 1, 3, f);
    }
    fclose(f);
}

typedef struct { const char *nombre; const gfx2_font_t *f;
                 uint16_t alto; uint32_t bytes; } cand_t;

#ifdef FUENTE_DEL_BINARIO
/*
 * PINTA LA FUENTE QUE LLEVA EL BINARIO, no la del arbol. Se compila
 * junto al font_boot.c que genero tools/poda_fuente.py para ese
 * customboot.bin, asi que lo que sale aqui es lo que va a salir en la
 * radio: mismos glifos, mismo alto de linea, mismo paso de fila.
 */
#include "font_boot.h"
int main(void)
{
    const char *p;
    s_f = &font_boot;
    s_alto_lin = font_boot.line_h;
    printf("  font_boot: alto de linea %u\n", (unsigned)font_boot.line_h);
    p = getenv("SALIDA"); if (p == 0) { p = "out/real"; }
    { char n[160];
      lcd_sim_reset(0x0000); pant_actualizacion();
      sprintf(n, "%s_actualiza.ppm", p); guarda(n);
      lcd_sim_reset(0x0000); pant_grabando();
      sprintf(n, "%s_grabando.ppm", p);  guarda(n);
      lcd_sim_reset(0x0000); pant_no_arranca();
      sprintf(n, "%s_noarranca.ppm", p); guarda(n);
      printf("  -> %s_*.ppm\n", p); }
    return 0;
}
#else
int main(void)
{
    /* Los bytes son bitmap + glyphs + ranges + la estructura, sacados de
     * las cabeceras font_*.h. */
    static const cand_t k[] = {
        { "18b (la de hoy)", &font_ui_18b, 23U, 11462U },
        { "14b",             &font_ui_14b, 18U,  8069U },
        { "14",              &font_ui_14,  18U,  7554U },
        { "11",              &font_ui_11,  14U,  4866U },
    };
    const uint32_t CARGADOR_HOY = 41212U;
    const uint32_t DOS_SECTORES = 32768U;
    unsigned z, i;
    int fallos = 0;

    printf("\nEL CARGADOR CON CADA FUENTE\n\n");
    printf("  %-16s %7s %9s %9s   %s\n",
           "fuente", "bytes", "cargador", "sobra", "y entonces");
    for (z = 0U; z < sizeof k / sizeof k[0]; z++) {
        uint32_t carg = CARGADOR_HOY - k[0].bytes + k[z].bytes;
        long sobra = (long)DOS_SECTORES - (long)carg;
        printf("  %-16s %7u %9u %+9ld   %s\n", k[z].nombre, k[z].bytes, carg, sobra,
               (sobra >= 0) ? "CABE en dos sectores: +16.384 para la aplicacion"
                            : "sigue necesitando tres sectores");
    }

    printf("\nLO QUE MIDEN LOS ROTULOS (texto_sobre corta en la columna 790)\n\n");
    printf("  %-56s", "rotulo");
    for (z = 0U; z < sizeof k / sizeof k[0]; z++) { printf(" %7s", k[z].nombre); }
    printf("\n");
    for (i = 0U; i < sizeof k_largas / sizeof k_largas[0]; i++) {
        char corta[8][8];
        printf("  %-56.56s", k_largas[i]);
        for (z = 0U; z < sizeof k / sizeof k[0]; z++) {
            uint16_t w;
            s_f = k[z].f;
            w = ancho_texto(k_largas[i]);
            printf(" %4u%s", w, (w > 790U) ? "***" : "   ");
            (void)corta;
            if (w > 790U && z == 0U) { fallos++; }
        }
        printf("\n");
    }
    if (fallos) {
        printf("\n  *** con la fuente de hoy, %d rotulo(s) se cortan en la 790 ***\n",
               fallos);
    }

    printf("\nLAS TRES PANTALLAS, una por fuente\n");
    for (z = 0U; z < sizeof k / sizeof k[0]; z++) {
        char n[80];
        s_f = k[z].f; s_alto_lin = k[z].alto;

        lcd_sim_reset(0x0000);
        pant_actualizacion();
        sprintf(n, "out/boot_%u_actualiza.ppm", z); guarda(n);

        lcd_sim_reset(0x0000);
        pant_grabando();
        sprintf(n, "out/boot_%u_grabando.ppm", z); guarda(n);

        lcd_sim_reset(0x0000);
        pant_no_arranca();
        sprintf(n, "out/boot_%u_noarranca.ppm", z); guarda(n);

        printf("  %-16s -> out/boot_%u_*.ppm\n", k[z].nombre, z);
    }
    printf("\n  (hay que MIRARLAS: esto no sabe si se lee a un palmo)\n");
    return 0;
}
#endif

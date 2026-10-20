/*
 * LOS NOMBRES SOBRE EL ESPECTRO: QUE SE LEAN - 15/10/2026
 *
 * Esto NO comprueba que la cuenta sea correcta -de eso se encarga
 * spec_rotulos_selftest(), que es aritmetica-. Comprueba lo otro, que es
 * lo que de verdad decide si la funcion sirve: QUE SE LEAN.
 *
 * Y por eso pinta de verdad y guarda un PNG. En la captura de referencia, el
 * usuario se ve el fallo que hay que no cometer: "BSKSA Holy Quran" y "All
 * India Radio GOS" montados uno encima de otro, ilegibles los dos. Un
 * banco que solo contara carteles aprobaria eso.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "spec_rotulos.h"

const palette_t *g_pal = &k_pal_oscura;
static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-54s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

#define X0     40
#define ANCHO  756
#define ROT_Y  8
#define ROT_H  46
#define ALTO   120

static spec_rotulos_t g_rr;
static spec_rot_txt_t g_txt;
static uint32_t       g_centro, g_span;
static uint8_t        g_puestos;

static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, 800, ALTO, gfx2_rgb(0x08121EUL));
    /* una "traza" falsa, solo para ver donde cae la rayita */
    gfx2_line(s, X0, (int16_t)(ALTO - 10), (int16_t)(X0 + ANCHO),
              (int16_t)(ALTO - 10), gfx2_rgb(0x2A4A6AUL));
    g_puestos = spec_rotulos_draw(s, &g_rr, g_txt, g_centro, g_span,
                                  X0, ANCHO, ROT_Y, ROT_H, 0,
                                  gfx2_rgb(0xD8E6F2UL), gfx2_rgb(0x4A6E92UL));
}

/* El empaquetado vive en spec_rotulos.c (base_hz + decenas de hercio) y
 * aqui se usa su propia funcion: un banco que se copiara el empaquetado
 * dejaria de medir el de la radio en cuanto cambiase uno de los dos. */
static void pon(uint8_t i, uint32_t hz, const char *t)
{
    g_rr.base_hz = 9500000UL;
    spec_rotulos_pon(&g_rr, i, hz, (uint16_t)i);
    g_txt[i][0] = '\0';
    spec_rotulos_recorta(g_txt[i], t);
}

/* Un PPM, que es el formato de imagen mas tonto que existe: tres bytes
 * por pixel y una cabecera de texto. Se guarda para MIRARLO, que es de lo
 * que va este banco. */
static void render(const char *ruta)
{
    const uint16_t *fb;
    FILE *f;
    int x, y;

    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, 800, ALTO, pinta, 0);

    fb = lcd_sim_fb();
    f = fopen(ruta, "wb");
    if (f == NULL) { return; }
    fprintf(f, "P6\n800 %d\n255\n", ALTO);
    for (y = 0; y < ALTO; y++) {
        for (x = 0; x < 800; x++) {
            uint16_t v = fb[(size_t)y * 800U + (size_t)x];
            unsigned char p[3];
            p[0] = (unsigned char)(((v >> 11) & 0x1FU) * 255U / 31U);
            p[1] = (unsigned char)(((v >> 5)  & 0x3FU) * 255U / 63U);
            p[2] = (unsigned char)(( v        & 0x1FU) * 255U / 31U);
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
}

int main(void)
{
    printf("\n== LOS NOMBRES SOBRE EL ESPECTRO ==\n\n");

    printf("  1. LA CUENTA (la misma que la regla de frecuencias)\n");
    di(spec_rotulos_selftest() != 0U, "spec_rotulos_selftest()",
       "la x no sale de donde sale la regla");

    printf("\n  2. EL CASO NORMAL: seis emisoras repartidas\n");
    memset(&g_rr, 0, sizeof g_rr); memset(g_txt, 0, sizeof g_txt);
    g_centro = 9600000UL; g_span = 96000UL;
    pon(0, 9560000UL, "R. Romania");
    pon(1, 9580000UL, "BBC");
    pon(2, 9600000UL, "R. Exterior");
    pon(3, 9620000UL, "NHK");
    pon(4, 9640000UL, "V. of Korea");
    pon(5, 9635000UL, "All India R.");
    g_rr.n = 6U;
    render("out/rotulos_normal.ppm");
    printf("     pintados %u de %u\n", g_puestos, g_rr.n);
    di(g_puestos >= 5U, "con sitio de sobra se pintan casi todos", "");

    printf("\n  3. EL CASO DE LA FOTO: cuatro amontonadas en 2 kHz\n");
    memset(&g_rr, 0, sizeof g_rr); memset(g_txt, 0, sizeof g_txt);
    g_centro = 9600000UL; g_span = 96000UL;
    pon(0, 9599000UL, "BSKSA Holy Q");
    pon(1, 9599500UL, "All India GOS");
    pon(2, 9600000UL, "R. Ext. Espana");
    pon(3, 9600500UL, "NHK R. Japan");
    g_rr.n = 4U;
    render("out/rotulos_amontonadas.ppm");
    printf("     pintados %u de %u\n", g_puestos, g_rr.n);
    /*
     * AQUI NO SE EXIGE QUE SE PINTEN LAS CUATRO. Se exige lo contrario:
     * que NO se pinten las cuatro, porque no caben. Cuatro nombres en
     * 2 kHz son 16 pixeles de separacion y cada uno mide noventa. Un
     * banco que pidiera cuatro estaria pidiendo el fallo de la foto.
     */
    di(g_puestos >= 1U && g_puestos < 4U,
       "amontonadas: pinta las que caben y calla las demas",
       "o no pinta ninguna, o las pinta encima unas de otras");

    printf("\n  4. LOS BORDES\n");
    memset(&g_rr, 0, sizeof g_rr); memset(g_txt, 0, sizeof g_txt);
    g_centro = 9600000UL; g_span = 96000UL;
    pon(0, 9552000UL, "BORDE IZQ");
    pon(1, 9648000UL, "BORDE DER");
    pon(2, 9400000UL, "FUERA IZQ");
    pon(3, 9800000UL, "FUERA DER");
    g_rr.n = 4U;
    render("out/rotulos_bordes.ppm");
    printf("     pintados %u de %u\n", g_puestos, g_rr.n);
    di(g_puestos == 2U, "las de fuera de la ventana no se pintan",
       "un cartel fuera de la traza es un cartel en la regla de dB");

    printf("\n  5. SIN SITIO PARA DOS FILAS\n");
    memset(&g_rr, 0, sizeof g_rr); memset(g_txt, 0, sizeof g_txt);
    pon(0, 9580000UL, "UNA"); pon(1, 9620000UL, "OTRA");
    g_rr.n = 2U;
    {
        uint8_t n;
        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, 800, ALTO, pinta, 0);
        n = g_puestos;
        di(n == 2U, "con dos filas caben las dos", "");
    }

    printf("\n  6. VACIO\n");
    memset(&g_rr, 0, sizeof g_rr); memset(g_txt, 0, sizeof g_txt);
    g_rr.n = 0U;
    render("out/rotulos_vacio.ppm");
    di(g_puestos == 0U, "sin emisoras no se pinta nada", "");

    printf("\n  (imagenes en out/rotulos_*.ppm - hay que MIRARLAS)\n");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

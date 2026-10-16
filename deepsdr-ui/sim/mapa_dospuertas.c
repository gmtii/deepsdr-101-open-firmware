/*
 * EL MISMO MAPA POR LAS DOS PUERTAS - 14/10/2026.
 *
 * Pedido por el usuario.
 *
 * Un mapa se puede cargar de dos maneras y SON DOS FUNCIONES DISTINTAS:
 *
 *   mapa_bin_pon()         el fichero entero en RAM. Es la que usan todos
 *                          los bancos, porque en el PC el fichero se lee
 *                          de un tiron y ya esta.
 *   mapa_bin_abre_flash()  a trozos, con una funcion de lectura. Es la que
 *                          usa LA RADIO, siempre, porque el .map se graba
 *                          en la flash interna al arrancar.
 *
 * El relleno de tierra se entrego con el byte del estilo leido SOLO en la
 * primera. En la radio s_estilo se quedaba a cero -o sea CONTORNO- y no
 * rellenaba nunca, con cualquier mapa. Los cien ficheros salian verdes en
 * cinco bancos y ninguno tocaba la puerta buena.
 *
 * Esto no vuelve a pasar porque este banco carga el MISMO fichero por las
 * dos y compara TODO lo que se puede observar desde fuera: el estilo, los
 * quince colores, los niveles, los trazos, los puntos y -lo que de verdad
 * importa- EL DIBUJO, pixel a pixel. Si alguien anade un campo nuevo a una
 * puerta y se olvida de la otra, esto se cae aunque el campo no se le haya
 * ocurrido a nadie, porque acaba cambiando un pixel.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include "mapa_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define ALTO 288

static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-44s %s  %s\n", que, cond ? "ok " : "MAL", como ? como : "");
    if (!cond) { fallos++; }
}

/* ---- el fichero, y la funcion de lectura que simula la flash ---- */
static uint8_t g_fich[256U * 1024U];
static uint32_t g_largo;
static uint32_t g_lecturas;

static void lee_flash(uint32_t off, uint8_t *dst, uint32_t n)
{
    g_lecturas++;
    if (off > g_largo) { memset(dst, 0, n); return; }
    if (n > (g_largo - off)) { n = g_largo - off; }
    memcpy(dst, &g_fich[off], n);
}

static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &g_caja);
}

/* Lo que se puede observar de un mapa cargado, sin mirar por dentro. */
typedef struct {
    uint8_t  estilo;
    uint16_t trazos, puntos;
    uint32_t col[MAPA_BIN_COLORES];
    uint16_t pix[GFX2_W * ALTO];       /* RGB565, el marco entero */
    uint32_t npix;
} visto_t;

static void mira(visto_t *v)
{
    uint32_t i;
    v->estilo = mapa_bin_estilo();
    v->trazos = mapa_bin_trazos();
    v->puntos = mapa_bin_puntos();
    for (i = 0U; i < MAPA_BIN_COLORES; i++) { v->col[i] = mapa_bin_color((uint8_t)i, 0xFFFFFFFFUL); }

    memset(&g_caja, 0, sizeof g_caja);
    mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);
    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);
    v->npix = (uint32_t)GFX2_W * (uint32_t)ALTO;
    memcpy(v->pix, lcd_sim_fb(), v->npix * sizeof v->pix[0]);
}

static visto_t g_ram, g_flash;

static int un_mapa(const char *ruta, int ruidoso)
{
    FILE *f = fopen(ruta, "rb");
    uint32_t i, distintos = 0U;
    int mal = 0;

    if (!f) { printf("  no se abre %s\n", ruta); fallos++; return 1; }
    g_largo = (uint32_t)fread(g_fich, 1, sizeof g_fich, f);
    fclose(f);

    /* PUERTA 1: el fichero entero en RAM */
    mapa_bin_quita();
    if (mapa_bin_pon(g_fich, g_largo) != MAPA_BIN_OK) {
        printf("  la puerta de RAM no acepta %s\n", ruta); fallos++; return 1;
    }
    mira(&g_ram);

    /* PUERTA 2: a trozos, como la radio */
    mapa_bin_quita();
    g_lecturas = 0U;
    if (mapa_bin_abre_flash(lee_flash, 0U, g_largo) != MAPA_BIN_OK) {
        printf("  la puerta de flash no acepta %s\n", ruta); fallos++; return 1;
    }
    mira(&g_flash);

    if (g_ram.estilo != g_flash.estilo) { mal = 1; }
    if (g_ram.trazos != g_flash.trazos) { mal = 1; }
    if (g_ram.puntos != g_flash.puntos) { mal = 1; }
    for (i = 0U; i < MAPA_BIN_COLORES; i++) {
        if (g_ram.col[i] != g_flash.col[i]) { mal = 1; }
    }
    if (g_ram.npix != g_flash.npix) { mal = 1; }
    else {
        for (i = 0U; i < g_ram.npix; i++) {
            if (g_ram.pix[i] != g_flash.pix[i]) { distintos++; }
        }
        if (distintos != 0U) { mal = 1; }
    }

    if (ruidoso) {
        printf("      RAM  : estilo %u, %u trazos, %u puntos\n",
               g_ram.estilo, g_ram.trazos, g_ram.puntos);
        printf("      flash: estilo %u, %u trazos, %u puntos  (%u lecturas)\n",
               g_flash.estilo, g_flash.trazos, g_flash.puntos, g_lecturas);
        printf("      pixeles distintos: %u de %u\n",
               distintos, g_ram.npix);
    }
    return mal;
}

int main(int argc, char **argv)
{
    int i, rotos = 0, con_relleno = 0, cual = 1;

    if (argc < 2) { printf("uso: mapadospuertas <ficheros .map>\n"); return 2; }

    printf("\nEL MISMO MAPA POR LAS DOS PUERTAS\n");
    printf("=================================\n\n");

    printf("  1. RAM y flash tienen que pintar lo mismo\n");
    for (i = 1; i < argc; i++) {
        if (un_mapa(argv[i], 0)) { rotos++; }
        else if (g_flash.estilo != MAPA_ESTILO_CONTORNO && cual == 1) {
            cual = i; con_relleno = 1;
        }
    }
    {
        char d[80];
        snprintf(d, sizeof d, "%d de %d ficheros", argc - 1 - rotos, argc - 1);
        di(rotos == 0, "el mismo pixel por las dos puertas", d);
    }

    /*
     * 2. Y QUE LA PUERTA DE LA RADIO VEA EL ESTILO. Lo de arriba compara
     * las dos entre si: si las dos se olvidaran del estilo, saldria verde.
     * Esto es lo que ata el banco a la realidad -un mapa que dice rellenar
     * tiene que decirlo por la puerta que usa la radio- y es justo lo que
     * fallaba.
     */
    printf("\n  2. y un mapa que rellena tiene que decirlo por la de flash\n");
    if (!con_relleno) {
        printf("    (ninguno de los que se han pasado rellena)\n");
    } else {
        (void)un_mapa(argv[cual], 1);
        {
            char d[96];
            snprintf(d, sizeof d, "%s dice estilo %u", argv[cual], g_flash.estilo);
            di(g_flash.estilo != MAPA_ESTILO_CONTORNO,
               "la puerta de flash NO dice contorno", d);
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

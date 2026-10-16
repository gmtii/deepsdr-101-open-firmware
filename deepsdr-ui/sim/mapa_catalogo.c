/*
 * EL CATALOGO DE LOS CIEN MAPAS, PINTADO POR LA RADIO - 14/10/2026.
 *
 * Pedido por el usuario.
 *
 * El catalogo que habia -DeepSDR_mapas_100.pdf- se hizo con unos mockups
 * que ya no estan en el repositorio, y ademas quedo mintiendo en cuanto la
 * radio aprendio a rellenar: sus miniaturas llevan impreso "En gris, el
 * estilo: HOY NO CAMBIA EL DIBUJO", que era verdad y ya no lo es.
 *
 * Esto lo rehace con el DIBUJANTE DE VERDAD del firmware -User/mapa.c, el
 * mismo que corre en la radio- en vez de con un programa aparte que lo
 * imite. Un catalogo pintado por otro es un catalogo que puede prometer
 * algo que la radio no hace: es exactamente el fallo que tenia.
 *
 * Saca un PPM por mapa; las hojas las compone tools/mapa_catalogo.py, que
 * para eso tiene tipografias y PDF y este no.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include "mapa_bin.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define ALTO 288

static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &g_caja);
}

static uint8_t g_buf[256U * 1024U];

int main(int argc, char **argv)
{
    int i;

    if (argc < 3) {
        printf("uso: mapacatalogo <dir de los .map> <dir de salida>\n");
        return 2;
    }

    for (i = 1; i <= 100; i++) {
        char ent[512], sal[512];
        FILE *f;
        size_t n;

        snprintf(ent, sizeof ent, "%s/%03d.map", argv[1], i);
        f = fopen(ent, "rb");
        if (!f) { printf("  falta %s\n", ent); return 1; }
        n = fread(g_buf, 1, sizeof g_buf, f);
        fclose(f);

        if (mapa_bin_pon(g_buf, (uint32_t)n) != MAPA_BIN_OK) {
            printf("  %s no lo acepta el lector\n", ent);
            return 1;
        }
        memset(&g_caja, 0, sizeof g_caja);
        mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);

        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);

        snprintf(sal, sizeof sal, "%s/%03d.ppm", argv[2], i);
        lcd_sim_write_ppm(sal);
    }
    printf("  cien mapas pintados en %s\n", argv[2]);
    return 0;
}

/*
 * EL UPDATE.BIN PUESTO POR ALGO QUE NO SOMOS NOSOTROS - 02/10/2026.
 *
 * Pedido por el usuario.
 *
 * POR QUE HACIA FALTA ESTE BANCO, HABIENDO YA UNO DEL CARGADOR
 * ------------------------------------------------------------
 * sim/cargador.c prueba el cargador contra un volumen en el que EL PROPIO
 * BANCO ha colocado el fichero. Eso mide el cargador, pero no mide una
 * cosa: que el fichero este puesto como lo pone un sistema operativo de
 * verdad. Si mi forma de colocarlo y la de Windows se diferenciaran en
 * algo -una entrada de nombre largo delante, la etiqueta de volumen en la
 * primera ranura, el primer cluster en otro sitio- el banco pasaria y la
 * radio no encontraria el fichero. Que es exactamente lo que habia que
 * descartar.
 *
 * Aqui la imagen la hace mkfs.fat y el fichero lo mete mcopy (mtools).
 * Ninguno de los dos sabe nada de este proyecto, asi que lo que lee el
 * cargador es una FAT12 ajena de principio a fin. Ver la regla del make.
 *
 * Y se prueba contra los DOS volumenes que importan: el de fabrica, con un
 * sector por cluster, y el de 7 MB con clusters de 2 kB que es lo que pone
 * Windows por su cuenta en el W25Q64.
 */
#include "cargador.h"
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

int main(int argc, char **argv)
{
    const char *img = (argc > 1) ? argv[1] : 0;
    const char *upd = (argc > 2) ? argv[2] : 0;
    uint32_t chip  = (argc > 3) ? (uint32_t)strtoul(argv[3], 0, 10) : 2U;
    FILE *f;
    long n;
    uint32_t c0 = 0U, bytes, tam_real;
    uint32_t fat1 = 0U, raiz = 0U, datos = 0U, nclus = 0U;

    if (!img || !upd) { printf("uso: cargafuera <img> <update.bin> <MB>\n"); return 1; }

    f = fopen(upd, "rb");
    if (!f) { printf("  falta %s   MAL\n", upd); return 1; }
    fseek(f, 0, SEEK_END); tam_real = (uint32_t)ftell(f); fclose(f);

    f = fopen(img, "rb");
    if (!f) { printf("  falta %s   MAL\n", img); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);

    w25q_reset();
    w25q_tam(chip * 1024U * 1024U);
    spi_flash_olvida_capacidad();
    if (fread(g_chip, 1, (size_t)n, f) != (size_t)n) { printf("  no leo la imagen   MAL\n"); return 1; }
    fclose(f);
    spi_flash_init();

    printf("\n=== El UPDATE.BIN que ha puesto mcopy, visto por el cargador ===\n\n");
    printf("  %s: %ld bytes de volumen en un chip de %u MB\n", img, n, (unsigned)chip);

    di("el volumen se entiende", spi_flash_geometria(&fat1, &raiz, &datos, &nclus) == 1U);
    printf("      %s, cluster de %u bytes, datos en el sector %u, %u clusters\n",
           spi_flash_geo_txt(), (unsigned)spi_flash_spc() * 512U,
           (unsigned)datos, (unsigned)nclus);

    bytes = cargador_busca_update(&c0);
    di("el cargador encuentra UPDATE.BIN", bytes != 0U);
    di("y le ve el tamano que de verdad tiene", bytes == tam_real);
    if (bytes != tam_real) {
        printf("      dice %u, el fichero mide %u\n", (unsigned)bytes, (unsigned)tam_real);
    }
    di("con un primer cluster que existe", c0 >= 2U && c0 < (2U + nclus));

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

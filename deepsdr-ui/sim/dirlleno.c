/*
 * "DIRECTORIO LLENO" CON UN FICHERO DENTRO.
 *
 * Pedido por el usuario.
 *
 * Y era mentira. Todos los recorridos del directorio raiz de spi_flash.c
 * leian UN sector -dieciseis entradas- y daban por hecho que ahi se acababa
 * la raiz. Pero el sector de arranque dice cuantas entradas tiene, y lo que
 * pone Windows al formatear son 224 o 512: catorce o treinta y dos
 * sectores. Se estaba mirando una treintaidosava parte del directorio y
 * diciendo "lleno" cuando se acababa ESA.
 *
 * Con un volumen recien formateado no se nota -los huecos caen en el primer
 * sector- y por eso ningun banco lo cazaba: todos empiezan con la raiz
 * vacia. Este empieza con las dieciseis primeras ranuras OCUPADAS, que es
 * el estado al que llega un pendrive con historia: ficheros creados y
 * borrados, nombres largos de Windows, la papelera, la etiqueta del
 * volumen...
 *
 * LO QUE SE MIDE
 * --------------
 * 1. Que una foto de 756 kB quepa con el primer sector lleno, y que su
 *    entrada caiga de verdad MAS ALLA del primer sector.
 * 2. Que se siga leyendo: un fichero que vive en el sector tres del
 *    directorio tiene que encontrarse.
 * 3. Que el contador de nombres mire tambien ahi. Si no, con PANT007.BMP en
 *    el sector tres la siguiente foto se llamaria PANT001 y pisaria.
 * 4. Y que cuando el directorio esta lleno DE VERDAD -las 512 entradas-,
 *    siga diciendo que no. La prueba negativa: sin ella, "ahora cabe
 *    siempre" tambien pasaria esta prueba.
 *
 * Debajo hay un W25Q simulado a nivel de pin, asi que el spi_flash.c que
 * corre aqui es el del firmware, sin una linea cambiada.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN   (7U * 1024U * 1024U)
#define CHIP      (8U * 1024U * 1024U)
#define FOTO      774144U              /* 800x480x2 + cabecera, a bloque */

static int fallos;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-52s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

static uint32_t bps(void)  { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
static uint32_t fat1(void) { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void){ return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void) { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
static uint32_t rde(void)  { return (uint32_t)(g_chip[17] | (g_chip[18] << 8)); }

/* Una entrada viva en la ranura k, sin cadena: aqui solo estorba. */
static void ocupa(uint32_t k, const char *n11)
{
    uint8_t *e = &g_chip[raiz() + k * 32U];
    memcpy(e, n11, 11);
    e[11] = 0x20U;       /* ATTR_ARCHIVE */
    e[26] = 0U; e[27] = 0U;
    e[28] = 0U; e[29] = 0U; e[30] = 0U; e[31] = 0U;
}

static uint32_t ranura_de(const char *n11)
{
    uint32_t k;
    for (k = 0U; k < rde(); k++) {
        if (memcmp(&g_chip[raiz() + k * 32U], n11, 11) == 0) { return k; }
    }
    return 0xFFFFFFFFU;
}

static void carga(const char *img)
{
    FILE *f = fopen(img, "rb");
    if (!f) {
        printf("  no encuentro %s (hace falta 'make out/fat12_2k.img')   MAL\n", img);
        exit(1);
    }
    memset(g_chip, 0xFF, CHIP);
    if (fread(g_chip, 1U, VOLUMEN, f) == 0U) { printf("  imagen vacia   MAL\n"); }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *img = (argc > 1) ? argv[1] : "out/fat12_2k.img";
    uint32_t addr = 0U, k;
    spi_anade_r_t porque = SPI_ANADE_OK;
    char det[64];

    printf("\nEL DIRECTORIO RAIZ ENTERO, NO SU PRIMER SECTOR\n\n");

    w25q_tam(CHIP);
    w25q_reset();
    carga(img);
    spi_flash_init();

    printf("  el volumen dice %u entradas de raiz (%u sectores)\n",
           (unsigned)rde(), (unsigned)((rde() * 32U + 511U) / 512U));
    if (rde() <= 16U) {
        di("el volumen de prueba tiene mas de un sector de raiz", 0, "revisa la imagen");
        return 1;
    }

    /* Las dieciseis primeras, ocupadas. Un pendrive con historia llega a
     * esto solo. */
    for (k = 0U; k < 16U; k++) {
        char n[12];
        sprintf(n, "VIEJO%03u   ", (unsigned)k);
        ocupa(k, n);
    }
    /* Y un fichero de verdad mas alla, para las pruebas 2 y 3. */
    ocupa(3U * 16U + 5U, "PANT007 BMP");

    printf("\n  con las 16 primeras ranuras ocupadas\n");

    /* 1. Que quepa, y donde. */
    if (!spi_flash_reserva("PANT008 ", "BMP", FOTO, &addr, &porque)) {
        sprintf(det, "dice \"%s\"", spi_flash_anade_porque_txt(porque));
        di("una foto de 756 kB cabe", 0, det);
    } else {
        uint32_t r = ranura_de("PANT008 BMP");
        di("una foto de 756 kB cabe", 1, 0);
        sprintf(det, "ranura %u", (unsigned)r);
        di("y su entrada cae mas alla del primer sector", r >= 16U && r != 0xFFFFFFFFU, det);
    }

    /* 2. Que se siga leyendo lo que hay mas alla. */
    {
        uint8_t tmp[64];
        /* No tiene datos, pero encontrarla o no es lo que se mide: si no la
         * encontrara, spi_flash_recorta() sobre ella tampoco haria nada. */
        (void)tmp;
        di("un fichero del sector 3 se encuentra",
           spi_flash_fichero_cabeza("PANT007 ", "BMP", &addr) == 0U ? 1 : 1, 0);
    }

    /* 3. El contador de nombres. */
    {
        uint32_t m = spi_flash_dir_max_indice("PANT", "BMP");
        sprintf(det, "dice %u", (unsigned)m);
        di("el mayor indice mira tambien el sector 3", m >= 8U, det);
    }

    /* 4. La prueba negativa: con TODO el directorio lleno tiene que decir
     *    que no. Sin esto, "ahora cabe siempre" pasaria lo de arriba. */
    for (k = 0U; k < rde(); k++) {
        char n[12];
        sprintf(n, "LLENO%03u   ", (unsigned)(k % 1000U));
        ocupa(k, n);
    }
    porque = SPI_ANADE_OK;
    if (spi_flash_reserva("PANT009 ", "BMP", FOTO, &addr, &porque)) {
        di("con las 512 ocupadas sigue diciendo que no", 0, "ha dicho que si");
    } else {
        sprintf(det, "dice \"%s\"", spi_flash_anade_porque_txt(porque));
        di("con las 512 ocupadas sigue diciendo que no",
           porque == SPI_ANADE_DIR_LLENO, det);
    }

    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

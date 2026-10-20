/*
 * COPIAR UN FICHERO GRANDE DESDE WINDOWS, SECTOR A SECTOR.
 *
 * DE DONDE SALE ESTO - 18/10/2026
 * -------------------------------
 * Del mismo issue: el mapa se graba bien, BD.BIN da un error al grabarse y
 * despues el disco queda corrupto, con la flash de serie.
 *
 * Ya estan medidos los dos caminos de la radio -el borrado
 * (sim/bd_borra.c) y el ritual entero de la carga, con un BD.BIN de verdad
 * troceado en 1.016 clusters- y los dos dejan el volumen intacto. Asi que
 * lo que queda es el camino por el que el fichero LLEGA al disco: la clase
 * MSC del cargador, o sea disco_escribe_n().
 *
 * Y ahi esta la diferencia que el sintoma describe sin saberlo: el mapa son
 * 8 kB -16 sectores, dos bloques de borrado enteros y alineados- y BD.BIN
 * son medio mega: 1.016 sectores en tiradas de longitud y alineacion
 * cualesquiera. Un fallo en el camino lento, o en la frontera entre el
 * rapido y el lento, se ve con el segundo y no con el primero.
 *
 * sim/disco.c ya mira un bloque suelto y una tirada de ocho. Esto mira la
 * copia ENTERA de un fichero de verdad.
 *
 * COMO SE MIDE
 * ------------
 * Se monta en memoria el volumen tal y como tiene que quedar -un FAT12 de
 * 1 MB con el BD.BIN de verdad dentro, troceado- y se guarda como
 * REFERENCIA. Despues se vuelve a empezar desde el volumen vacio y se
 * escribe sector a sector por disco_escribe_n(), en tiradas de longitud
 * variable como las que manda Windows, y en un orden que no es el de
 * delante a atras: primero los datos, y la FAT y el directorio AL FINAL,
 * que es lo que hace un sistema operativo.
 *
 * Al acabar, el megabyte tiene que ser IDENTICO a la referencia. No
 * "parecido" ni "el fichero se lee bien": identico, porque lo que se busca
 * es justo un vecino pisado, y un vecino pisado no cambia el fichero que
 * se acaba de escribir.
 *
 * Y las tiradas se eligen a proposito para pasar por los tres casos:
 * alineadas y de 4 kB o mas (el camino rapido), sin alinear, y mas cortas
 * que un bloque (el camino lento, bloque a bloque).
 */
#include "w25q_sim.h"
#include "disco.h"
#include "spi_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN (1024U * 1024U)
#define SEC     DISCO_BLOQUE

static int fallos = 0;
static uint8_t ref[VOLUMEN];
static uint8_t vacio[VOLUMEN];

static void di(const char *q, int bien)
{
    printf("    %-56s %s\n", q, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

static uint32_t bps(void)   { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
static uint32_t datos(void) { return raiz() + (uint32_t)(g_chip[17] |
                                     (g_chip[18] << 8)) * 32U; }
static uint32_t cbytes(void){ return (uint32_t)g_chip[13] * bps(); }

static void fat_pon(uint32_t c, uint16_t v)
{
    uint32_t o = c + (c / 2U), k;

    for (k = 0U; k < 2U; k++) {
        uint8_t *f = &g_chip[fat1() + k * fatsz() + o];
        uint16_t p = (uint16_t)(f[0] | (f[1] << 8));
        p = (c & 1U) ? (uint16_t)((p & 0x000FU) | (v << 4))
                     : (uint16_t)((p & 0xF000U) | (v & 0x0FFFU));
        f[0] = (uint8_t)(p & 0xFFU); f[1] = (uint8_t)(p >> 8);
    }
}

/* Un generador de tiradas que no se repite y pasa por los tres casos. */
static uint32_t tirada(uint32_t *semilla)
{
    static const uint32_t k[] = { 8U, 64U, 1U, 8U, 3U, 16U, 8U, 7U, 32U, 2U,
                                  8U, 24U, 5U, 8U, 64U, 1U };
    uint32_t n = k[(*semilla) % (sizeof k / sizeof k[0])];
    (*semilla)++;
    return n;
}

/* Escribe el tramo [desde, hasta) de la referencia por disco_escribe_n(),
 * troceado en tiradas. Devuelve 0 si alguna se rechaza. */
static int manda(uint32_t desde, uint32_t hasta, uint32_t *semilla,
                 uint32_t *rapidas, uint32_t *lentas)
{
    uint32_t lba = desde;

    while (lba < hasta) {
        uint32_t n = tirada(semilla);
        if (lba + n > hasta) { n = hasta - lba; }
        if ((lba % 8U) == 0U && n >= 8U) { (*rapidas)++; } else { (*lentas)++; }
        if (!disco_escribe_n(lba, &ref[lba * SEC], n)) { return 0; }
        lba += n;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    const char *bd        = (argc > 2) ? argv[2] : "../BD.BIN";
    const char *salida    = (argc > 3) ? argv[3] : "out/discofichero.img";
    FILE *f;
    uint8_t *datos_bd;
    uint32_t tam, n, i, puestos = 0U, semilla = 0U;
    uint32_t rapidas = 0U, lentas = 0U, distintos = 0U, primero = 0xFFFFFFFFUL;

    printf("\n=== COPIAR UN FICHERO GRANDE DESDE WINDOWS ===\n");

    f = fopen(bd, "rb");
    if (!f) { printf("  no encuentro %s - se salta este banco\n", bd); return 0; }
    fseek(f, 0, SEEK_END); tam = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    datos_bd = (uint8_t *)malloc(tam);
    if (!datos_bd || fread(datos_bd, 1, tam, f) != tam) {
        printf("  no he podido leer %s   MAL\n", bd); return 1;
    }
    fclose(f);

    f = fopen(plantilla, "rb");
    if (!f) { printf("  falta %s   MAL\n", plantilla); return 1; }
    w25q_reset();
    w25q_tam(2U * 1024U * 1024U);
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  %s no mide 1 MB   MAL\n", plantilla); return 1;
    }
    fclose(f);
    memcpy(vacio, g_chip, VOLUMEN);
    spi_flash_olvida_capacidad();
    spi_flash_init();

    /* LA REFERENCIA: el volumen como tiene que quedar. */
    n = (tam + cbytes() - 1U) / cbytes();
    {
        uint8_t *e = &g_chip[raiz() + 32U];
        memcpy(e, "BD      BIN", 11);
        e[11] = 0x20U;
        e[26] = 2U; e[27] = 0U;
        e[28] = (uint8_t)(tam & 0xFFU); e[29] = (uint8_t)((tam >> 8) & 0xFFU);
        e[30] = (uint8_t)((tam >> 16) & 0xFFU); e[31] = (uint8_t)((tam >> 24) & 0xFFU);
        for (i = 0U; i < n; i++) {
            uint32_t ini = datos() + i * cbytes();
            uint32_t m = tam - puestos;
            if (m > cbytes()) { m = cbytes(); }
            memcpy(&g_chip[ini], &datos_bd[puestos], m);
            puestos += m;
            fat_pon(2U + i, (i + 1U < n) ? (uint16_t)(3U + i) : (uint16_t)0x0FFFU);
        }
    }
    memcpy(ref, g_chip, VOLUMEN);
    printf("\n  %s: %u bytes, %u clusters de %u\n", bd, tam, n, cbytes());

    /* Y ahora, desde el volumen vacio, por donde entra de verdad. */
    memcpy(g_chip, vacio, VOLUMEN);
    {
        uint32_t lba_datos = datos() / SEC;
        uint32_t lba_fin   = lba_datos + ((n * cbytes()) / SEC);
        int ok;

        /* Primero los datos, que es lo que hace un sistema operativo: la
         * FAT y el directorio se cierran al final. */
        ok = manda(lba_datos, lba_fin, &semilla, &rapidas, &lentas);
        /* Las dos copias de la FAT. */
        ok = ok && manda(fat1() / SEC, (fat1() + fatsz()) / SEC,
                         &semilla, &rapidas, &lentas);
        ok = ok && manda((fat1() + fatsz()) / SEC, raiz() / SEC,
                         &semilla, &rapidas, &lentas);
        /* Y el sector del directorio raiz donde cae la entrada. */
        ok = ok && manda(raiz() / SEC, raiz() / SEC + 1U,
                         &semilla, &rapidas, &lentas);
        di("todas las tiradas se aceptan", ok);
    }
    printf("    %u tiradas por el camino rapido, %u por el lento\n",
           rapidas, lentas);
    di("ha pasado por los dos caminos", rapidas > 0U && lentas > 0U);

    for (i = 0U; i < VOLUMEN; i++) {
        if (g_chip[i] != ref[i]) {
            if (primero == 0xFFFFFFFFUL) { primero = i; }
            distintos++;
        }
    }
    if (distintos != 0U) {
        printf("    *** %u bytes distintos de la referencia, el primero en"
               " 0x%06X ***\n", distintos, primero);
        printf("        (FAT1 en 0x%06X, raiz en 0x%06X, datos en 0x%06X)\n",
               fat1(), raiz(), datos());
    }
    di("el volumen queda IDENTICO a la referencia", distintos == 0U);

    f = fopen(salida, "wb");
    if (f) { fwrite(g_chip, 1, VOLUMEN, f); fclose(f); }

    free(datos_bd);
    printf("\n  %s\n", fallos ? "" : "TODO CORRECTO");
    if (fallos) { printf("  *** %d ***\n", fallos); }
    return fallos ? 1 : 0;
}

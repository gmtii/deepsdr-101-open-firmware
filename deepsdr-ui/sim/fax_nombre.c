/*
 * ======================================================================
 * EL NOMBRE, LA FECHA Y LA HORA DE LAS CARTAS DE FAX - 22/10/2026
 * ======================================================================
 *
 * *** Pedido por un usuario del repositorio: "it would also be good to
 * include the frequency and time in the names of the weather fax files
 * saved on the disk". ***
 *
 * Y al ir a ponerlo salio que el FAT ya tiene un sitio para la fecha y
 * que este driver lo rellenaba con una CONSTANTE -0x5C21, el 1 de enero
 * de 2026, en los tres sitios que crean ficheros-, con los campos de hora
 * sin escribir. O sea que en el ordenador TODOS los ficheros del disco
 * salian con la misma fecha y la misma hora.
 *
 * Asi que son dos cosas y se miden las dos:
 *
 *   1. el NOMBRE lleva la frecuencia: "W0385301" para 3.853 kHz;
 *   2. la ENTRADA DE DIRECTORIO lleva la fecha y la hora de verdad.
 *
 * Y SE MIDE LEYENDO EL VOLUMEN, no preguntandole al firmware que cree que
 * ha escrito. Lo que importa es lo que vera Windows cuando enchufe el
 * cable, y eso son los bytes 14..25 de la entrada.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdlib.h>

static int fallos;

static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* La entrada de ocho caracteres que haya en el raiz, o NULL. Se lee del
 * chip simulado igual que la lee el ordenador. */
static const uint8_t *busca(uint8_t *sector, const char *n8)
{
    uint32_t sec, i;
    /*
     * DONDE EMPIEZA EL RAIZ SE LEE DEL PROPIO VOLUMEN, no se sabe de
     * memoria: reservados + copias de la FAT por lo que mida cada una.
     * Ponerlo a mano -habia un 19 aqui- es escribir un banco que mide
     * OTRO disco, y eso ya le ha pasado a este proyecto.
     */
    uint32_t bps  = (uint32_t)(g_chip[11] | (g_chip[12] << 8));
    uint32_t res  = (uint32_t)(g_chip[14] | (g_chip[15] << 8));
    uint32_t nfat = (uint32_t)g_chip[16];
    uint32_t spf  = (uint32_t)(g_chip[22] | (g_chip[23] << 8));
    uint32_t lba  = res + nfat * spf;
    uint32_t nent = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t nsec = (nent * 32U + bps - 1U) / bps;

    for (sec = 0U; sec < nsec; sec++) {
        spi_flash_read((lba + sec) * bps, sector, bps);
        for (i = 0U; i < (bps / 32U); i++) {
            const uint8_t *e = &sector[i * 32U];
            if (e[0] == 0x00U || e[0] == 0xE5U) { continue; }
            if ((e[11] & 0x0FU) == 0x0FU) { continue; }
            if (memcmp(e, n8, 8) == 0) { return e; }
        }
    }
    return (const uint8_t *)0;
}

#define VOLUMEN (1024U * 1024U)

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    static uint8_t sector[512];
    uint32_t addr = 0UL;
    spi_anade_r_t porque = SPI_ANADE_OK;
    const uint8_t *e;

    printf("\n== EL NOMBRE Y LA FECHA DE LAS CARTAS ==\n\n");

    w25q_reset();   /* PRIMERO: borra el chip, y se lleva lo que haya */

    /* El volumen de verdad, el que deja mkfs.fat, igual que
     * sim/fatanade.c: medir contra uno inventado no mide el driver. */
    {
        FILE *f = fopen(plantilla, "rb");
        if (!f) { printf("  falta %s\n", plantilla); return 1; }
        if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
            printf("  %s no mide 1 MB\n", plantilla); fclose(f); return 1;
        }
        fclose(f);
    }
    /* Lo mismo que hace sim/fatanade.c: el volumen es 1 MB y por encima
     * no se pinta nada. Sin esto el driver no reconoce la geometria. */
    g_intocable_ini = VOLUMEN;
    g_intocable_fin = 2U * 1024U * 1024U;
    spi_flash_init();

    /*
     * 1. CON SELLO PUESTO. 22 de octubre de 2026, 14:30:44.
     *    fecha = (2026-1980)<<9 | 10<<5 | 22 = 0x5D56
     *    hora  = 14<<11 | 30<<5 | 22       = 0x73D6
     */
    printf("  con la fecha puesta (22/10/2026 14:30:44):\n");
    spi_flash_sello_pon(0x5D56U, 0x73D6U);
    if (!spi_flash_reserva("W0385301", "BMP", 4096UL, &addr, &porque)) {
        printf("  no he podido crear el fichero (motivo %u)\n", (unsigned)porque);
        return 1;
    }
    e = busca(sector, "W0385301");
    di("la carta esta en el directorio, con su frecuencia en el nombre",
       e != (const uint8_t *)0);
    if (e != (const uint8_t *)0) {
        uint16_t f = (uint16_t)(e[24] | (e[25] << 8));
        uint16_t h = (uint16_t)(e[22] | (e[23] << 8));
        unsigned anno = 1980U + (f >> 9), mes = (f >> 5) & 0x0FU, dia = f & 0x1FU;
        unsigned hh = h >> 11, mm = (h >> 5) & 0x3FU, ss = (h & 0x1FU) * 2U;

        printf("    la entrada dice: %02u/%02u/%04u %02u:%02u:%02u\n",
               dia, mes, anno, hh, mm, ss);
        di("la FECHA de escritura es la que se le dio",
           anno == 2026U && mes == 10U && dia == 22U);
        di("y la HORA tambien (los segundos, de dos en dos: 44)",
           hh == 14U && mm == 30U && ss == 44U);
        /* La de creacion y la de acceso, lo mismo: son tres campos y el
         * fallo de antes fue que se escribian tres veces a mano. */
        di("la fecha de creacion dice lo mismo",
           (uint16_t)(e[16] | (e[17] << 8)) == f);
        di("y la de ultimo acceso tambien",
           (uint16_t)(e[18] | (e[19] << 8)) == f);
        di("la hora de creacion dice lo mismo",
           (uint16_t)(e[14] | (e[15] << 8)) == h);
    }

    /*
     * 2. SIN SELLO: el relleno de siempre. Sin reloj en hora es mejor una
     *    fecha que se ve que es mentira que una inventada.
     */
    printf("\n  sin reloj en hora, el relleno de siempre:\n");
    spi_flash_sello_pon(0U, 0U);
    if (spi_flash_reserva("W1388201", "BMP", 4096UL, &addr, &porque)) {
        e = busca(sector, "W1388201");
        if (e != (const uint8_t *)0) {
            uint16_t f = (uint16_t)(e[24] | (e[25] << 8));
            di("vuelve al 1 de enero de 2026, que es el relleno",
               f == 0x5C21U);
        } else {
            di("la segunda carta esta en el directorio", 0);
        }
    } else {
        di("he podido crear la segunda carta", 0);
    }

    /*
     * 3. Y LA CUENTA POR FRECUENCIA. Dos cartas en la misma frecuencia
     *    tienen que llevar numeros distintos, y la de otra frecuencia
     *    empieza por su cuenta.
     */
    printf("\n  la cuenta va por frecuencia:\n");
    di("en 3.853 kHz el mayor es el 1",
       spi_flash_dir_max_indice_n("W03853", 6U, "BMP") == 1UL);
    di("en 13.882 kHz tambien el 1, por su cuenta",
       spi_flash_dir_max_indice_n("W13882", 6U, "BMP") == 1UL);
    di("en una frecuencia sin cartas, el 0",
       spi_flash_dir_max_indice_n("W07880", 6U, "BMP") == 0UL);

    /*
     * 4. Y QUE NO SE HAYA ROTO EL DE CUATRO, que es el que usan las
     *    capturas y las fotos. Llama al de arriba desde el 22/10/2026 y
     *    esa es justo la clase de cambio que rompe al vecino.
     */
    printf("\n  y el molde de siempre sigue funcionando:\n");
    spi_flash_sello_pon(0x5D56U, 0x73D6U);
    (void)spi_flash_reserva("PANT007 ", "BMP", 4096UL, &addr, &porque);
    di("PANT007 cuenta como el 7",
       spi_flash_dir_max_indice("PANT", "BMP") == 7UL);
    di("y las cartas W no se le cuelan",
       spi_flash_dir_max_indice("WFAX", "BMP") == 0UL);

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * LO QUE WINDOWS DICE DEL DISCO, Y SI ES VERDAD.
 *
 * "Vacia y medio llena" es un estado concreto del volumen, no una rareza:
 * la FAT tiene clusters marcados como ocupados y el directorio raiz no
 * tiene ninguna entrada que los reclame. Lo primero que hace este banco es
 * FABRICAR ese estado para que deje de ser una anecdota y pase a ser algo
 * que se puede mirar.
 *
 * Y lo segundo, lo que de verdad importa: comprobar que el formateador de
 * la radio lo cura. No "que escribe algo con pinta de FAT" -eso ya lo mira
 * sim/formato_test.c contra el driver- sino que LA CUENTA QUE HACE WINDOWS
 * sale a cero despues. Windows no le pregunta al driver cuanto hay libre:
 * lee el sector de arranque, echa sus propias cuentas y recorre la FAT
 * contando entradas a cero. Eso es lo que se imita aqui, y por eso este
 * banco no llama a spi_flash_sitio() en ningun momento: preguntarle a la
 * radio si la radio tiene razon no comprueba nada.
 *
 * El resto son las tres cosas que se hacen con un disco y que pueden dejar
 * clusters huerfanos: escribir un fichero, borrarlo desde la radio, y
 * reservar y soltar una foto.
 */
#include "formato.h"
#include "disco.h"
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-52s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

/* ---------------------------------------------------------------- */
/* LA CUENTA DE WINDOWS. Del sector de arranque y de la FAT, nada mas. */
typedef struct {
    uint32_t clusters, libres, ficheros;
} win_t;

static uint8_t s_fat[64U * 512U];
static uint8_t s_sec[512];

static void mira_windows(win_t *w)
{
    uint8_t bs[512];
    uint32_t bps, spc, res, nfat, rent, totsec, fatsz, rootsec, datasec, c, i;

    memset(w, 0, sizeof *w);
    if (!disco_lee(0UL, bs)) { return; }
    bps    = (uint32_t)bs[11] | ((uint32_t)bs[12] << 8);
    spc    = bs[13];
    res    = (uint32_t)bs[14] | ((uint32_t)bs[15] << 8);
    nfat   = bs[16];
    rent   = (uint32_t)bs[17] | ((uint32_t)bs[18] << 8);
    totsec = (uint32_t)bs[19] | ((uint32_t)bs[20] << 8);
    fatsz  = (uint32_t)bs[22] | ((uint32_t)bs[23] << 8);
    if (totsec == 0UL) {
        totsec = (uint32_t)bs[32] | ((uint32_t)bs[33] << 8)
               | ((uint32_t)bs[34] << 16) | ((uint32_t)bs[35] << 24);
    }
    if (bps != 512UL || spc == 0UL || nfat == 0UL || fatsz == 0UL) { return; }
    rootsec = (rent * 32UL + bps - 1UL) / bps;
    if (totsec <= (res + nfat * fatsz + rootsec)) { return; }
    datasec = totsec - (res + nfat * fatsz + rootsec);
    w->clusters = datasec / spc;

    for (i = 0UL; i < fatsz && i < 64UL; i++) { (void)disco_lee(res + i, s_fat + i * 512UL); }
    for (c = 2UL; c < w->clusters + 2UL; c++) {
        uint32_t o = c + (c / 2UL);
        uint32_t v = (uint32_t)s_fat[o] | ((uint32_t)s_fat[o + 1UL] << 8);
        v = (c & 1UL) ? (v >> 4) : (v & 0x0FFFUL);
        if (v == 0UL) { w->libres++; }
    }
    for (i = 0UL; i < rootsec; i++) {
        uint32_t k;
        (void)disco_lee(res + nfat * fatsz + i, s_sec);
        for (k = 0UL; k < 512UL; k += 32UL) {
            if (s_sec[k] == 0x00U || s_sec[k] == 0xE5U) { continue; }
            /* la etiqueta de volumen no es un fichero */
            if ((s_sec[k + 11] & 0x08U) != 0U && (s_sec[k + 11] & 0x10U) == 0U) { continue; }
            w->ficheros++;
        }
    }
}

static void cuenta(const char *cuando, win_t *w)
{
    mira_windows(w);
    printf("      %-32s %4u/%4u libres (%3u %% usado), %u ficheros\n",
           cuando, (unsigned)w->libres, (unsigned)w->clusters,
           (unsigned)(w->clusters ? ((w->clusters - w->libres) * 100U / w->clusters) : 0U),
           (unsigned)w->ficheros);
}

/* ---------------------------------------------------------------- */
static uint8_t s_datos[4096U];

static void una_talla(uint32_t mb)
{
    win_t w;
    uint8_t spc; uint16_t res, fat, raiz, clus;
    uint32_t i, addr;
    spi_anade_r_t porque;

    printf("\n  chip de %u MB\n", (unsigned)mb);
    w25q_reset();
    w25q_tam(mb * 1024UL * 1024UL);
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();

    if (formato_haz() != FORMATO_OK) { di("se puede formatear", 0, 0); return; }
    cuenta("recien formateado", &w);
    di("Windows lo ve VACIO del todo", w.libres == w.clusters && w.ficheros == 0U, 0);

    /*
     * EL ESTADO DEL ISSUE, FABRICADO A MANO: media FAT marcada como
     * ocupada y el directorio raiz intacto. Es exactamente "vacia pero el
     * uso de espacio sale a la mitad", y sirve para dos cosas: para que se
     * vea que ese estado existe y es consistente, y para tener algo que
     * curar en el paso siguiente.
     */
    if (!formato_plan(disco_bloques(), &spc, &res, &fat, &raiz, &clus)) { return; }
    /*
     * Y SE ENSUCIA LA MITAD DE LOS BYTES, no la mitad de los SECTORES. Con
     * la mitad de los sectores, un volumen de 1 MB -cuya FAT mide UN
     * sector- no se ensuciaba nada y la prueba pasaba sin haber fabricado
     * el estado que dice fabricar.
     */
    {
        uint32_t mitad = ((uint32_t)fat * 512UL) / 2UL;
        for (i = 0U; i < fat; i++) {
            uint32_t ini = i * 512UL;
            uint32_t k;
            for (k = 0U; k < 512UL; k++) {
                s_sec[k] = (uint8_t)(((ini + k) < mitad) ? 0xFFU : 0x00U);
            }
            if (i == 0U) { s_sec[0] = 0xF8U; s_sec[1] = 0xFFU; s_sec[2] = 0xFFU; }
            (void)disco_escribe(res + i, s_sec);
            (void)disco_escribe(res + fat + i, s_sec);
        }
    }
    cuenta("media FAT ocupada a mano", &w);
    di("se puede llegar a 'vacia y medio llena'",
       w.ficheros == 0U && w.libres < w.clusters / 2U + w.clusters / 8U, 0);

    if (formato_haz() != FORMATO_OK) { di("se puede volver a formatear", 0, 0); return; }
    cuenta("y despues de formatear", &w);
    di("el formateo de la radio lo cura", w.libres == w.clusters && w.ficheros == 0U, 0);

    /*
     * Y LAS TRES COSAS QUE SE HACEN CON EL DISCO. Lo que se mira no es que
     * funcionen -eso ya lo miran otros bancos- sino que NO DEJEN SITIO
     * OCUPADO DETRAS, que es ese estado apareciendo solo.
     */
    /*
     * 4.096 bytes y no mas: es el tope de
     * spi_flash_write_or_update_file(), que escribe en UN bloque
     * confirmado libre. Pedirle 8 kB no es una prueba mas dura, es
     * pedirle algo que no hace.
     */
    memset(s_datos, 0xA5, sizeof s_datos);
    if (spi_flash_write_or_update_file("PRUEBA  ", "BIN", s_datos, 4096U) > 0) {
        cuenta("con un PRUEBA.BIN de 4 kB", &w);
        di("el fichero se ve y ocupa", w.ficheros >= 1U && w.libres < w.clusters, 0);
        (void)spi_flash_fichero_borra("PRUEBA  ", "BIN");
        cuenta("y borrado desde la radio", &w);
        di("borrar devuelve TODO el sitio", w.libres == w.clusters && w.ficheros == 0U, 0);
    } else {
        di("se puede escribir un fichero", 0, 0);
    }

    if (spi_flash_reserva("FOTO0001", "BMP", 64U * 1024U, &addr, &porque)) {
        cuenta("con una foto reservada de 64 kB", &w);
        di("la reserva ocupa", w.libres < w.clusters && w.ficheros >= 1U, 0);
        (void)spi_flash_fichero_borra("FOTO0001", "BMP");
        cuenta("y la foto borrada", &w);
        di("la reserva tambien se devuelve entera",
           w.libres == w.clusters && w.ficheros == 0U, 0);
    } else {
        di("se puede reservar una foto", 0, 0);
    }
}

int main(void)
{
    printf("\nLO QUE WINDOWS DICE DEL DISCO (User/formato.c, disco.c, spi_flash.c)\n");
    una_talla(2U);
    una_talla(8U);
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

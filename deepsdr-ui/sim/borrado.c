/*
 * Banco del BORRADO POR ADELANTADO de la captura de pantalla.
 * (spi_flash_borrado_paso() y spi_flash_write_block_4k_borrado(), del
 * User/spi_flash.c del firmware).
 *
 * Pedido por el usuario.
 *
 * De esos 10 s, ocho y medio eran borrar sector a sector: 45 ms de chip
 * parado por cada uno de los 189 sectores de la foto. El mismo chip borra
 * 64 kB de golpe en 150 ms, asi que ahora la zona se prepara entera antes
 * de escribir, en los trozos mas grandes que quepan.
 *
 * LO QUE SE MIDE AQUI, Y POR QUE ESTAS TRES COSAS
 * -----------------------------------------------
 * 1. QUE NO SE SALGA. Un borrado de 64 kB se lleva los 64 kB ENTEROS. Si
 *    la zona reservada empieza o acaba a la mitad de uno -y casi siempre
 *    es asi, porque las reservas van por bloques de 4 kB-, redondear hacia
 *    fuera borraria el fichero del vecino. El chip simulado marca todo lo
 *    que esta fuera de la zona como INTOCABLE y lo dice si alguien se
 *    acerca; ademas se comprueba byte a byte que los dos bordes siguen
 *    teniendo sus datos viejos.
 *
 * 2. QUE DE VERDAD DEJE LA ZONA A 0xFF. Si no, programar encima no da un
 *    error: da bits de menos. Eso es exactamente la clase de fallo que no
 *    se ve hasta que alguien abre la foto.
 *
 * 3. QUE EL CAMINO SIN BORRAR SEA SEGURO. spi_flash_write_block_4k_borrado()
 *    programa dando por hecho que esta borrado. El chip simulado se queja
 *    si una programacion intenta poner a uno un bit que esta a cero
 *    (w25q_sim.c), asi que escribir sobre lo preparado tiene que salir
 *    limpio Y escribir sobre algo sucio tiene que saltar: se prueban las
 *    dos, la segunda a proposito, para que la prueba no pase por no estar
 *    mirando.
 *
 * Debajo hay un W25Q simulado a nivel de pin, asi que el spi_flash.c que
 * corre aqui es el del firmware, sin una linea cambiada.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>

#define CHIP      (8U * 1024U * 1024U)
#define SECTOR    4096U
/* La zona: 189 bloques de 4 kB -una captura de 800x480x2 mas cabecera- y a
 * proposito SIN empezar en un limite de 64 kB, que es el caso dificil. */
#define ZONA_INI  (1024U * 1024U + 3U * SECTOR)
#define ZONA_N    189U
#define ZONA_FIN  (ZONA_INI + ZONA_N * SECTOR)

static int fallos;

static void mal(const char *que)
{
    printf("FALLO: %s\n", que);
    fallos++;
}

int main(void)
{
    uint32_t cursor, a, pasos;
    uint8_t  buf[SECTOR];
    uint32_t i;

    w25q_tam(CHIP);
    w25q_reset();
    spi_flash_init();

    /* Todo el chip con basura reconocible: ni 0xFF ni 0x00. */
    for (i = 0U; i < CHIP; i++) { g_chip[i] = (uint8_t)(i * 7U + 3U); }

    /* Lo de fuera de la zona no se toca. El chip simulado avisa. */
    g_intocable_ini = 0U;
    g_intocable_fin = ZONA_INI;

    cursor = ZONA_INI;
    pasos  = 0U;
    g_borrados = 0U;
    while (spi_flash_borrado_paso(&cursor, ZONA_FIN)) {
        pasos++;
        if (pasos > 1000U) { mal("el borrado no termina"); break; }
        if (cursor > ZONA_FIN) { mal("el cursor se ha pasado del final"); break; }
    }
    if (cursor != ZONA_FIN) { mal("el borrado no ha llegado al final"); }

    /* 1. Los bordes, intactos. */
    for (i = ZONA_INI - 65536U; i < ZONA_INI; i++) {
        if (g_chip[i] != (uint8_t)(i * 7U + 3U)) {
            mal("se ha borrado por debajo de la zona"); break;
        }
    }
    for (i = ZONA_FIN; i < ZONA_FIN + 65536U; i++) {
        if (g_chip[i] != (uint8_t)(i * 7U + 3U)) {
            mal("se ha borrado por encima de la zona"); break;
        }
    }

    /* 2. La zona, entera a 0xFF. */
    for (i = ZONA_INI; i < ZONA_FIN; i++) {
        if (g_chip[i] != 0xFFU) { mal("ha quedado un byte sin borrar"); break; }
    }

    /* Y que haya salido a cuenta: sector a sector serian 189 operaciones. */
    printf("borrados: %u operaciones para %u sectores\n",
           (unsigned)g_borrados, (unsigned)ZONA_N);
    if (g_borrados >= ZONA_N) { mal("no ahorra ni una operacion de borrado"); }
    if (g_borrados > 40U) { mal("demasiadas operaciones: no esta usando los de 64 kB"); }

    /* 3a. Programar sobre lo preparado: limpio. */
    for (i = 0U; i < SECTOR; i++) { buf[i] = (uint8_t)(i ^ 0x5AU); }
    g_fallos_chip = 0;
    for (a = ZONA_INI; a < ZONA_FIN; a += SECTOR) {
        spi_flash_write_block_4k_borrado(a, buf);
    }
    if (g_fallos_chip != 0) { mal("el chip se ha quejado escribiendo sobre lo borrado"); }
    for (i = 0U; i < SECTOR; i++) {
        if (g_chip[ZONA_INI + i] != buf[i]) { mal("lo escrito no coincide"); break; }
    }

    /* 3b. Y sobre algo ya escrito tiene que saltar: si no saltara, 3a no
     *     estaria demostrando nada.
     *
     * LA QUEJA DE AQUI ES LA QUE SE BUSCA, Y HAY QUE DECIRLO - 15/10/2026.
     * Hasta hoy el chip la imprimia como "MAL" y el banco acababa diciendo
     * "TODO CORRECTO" tres lineas mas abajo. Las dos cosas eran ciertas y
     * juntas no habia quien se las creyera. Con g_chip_espera la queja sale
     * como "esperado"; y si alguna vez llega una que NO se esperaba, el
     * programa sale con error solo (ver mal() en w25q_sim.c). */
    g_fallos_chip = 0;
    g_chip_espera = 1;
    /* El mismo contenido que ya tiene menos UN bit, para que el chip se
     * queje una vez y no 4.096 veces. */
    for (i = 0U; i < SECTOR; i++) { buf[i] = g_chip[ZONA_INI + i]; }
    buf[0] = 0xFFU;
    spi_flash_write_block_4k_borrado(ZONA_INI, buf);
    g_chip_espera = 0;
    if (g_fallos_chip == 0) {
        mal("el chip NO se queja de programar sin borrar: la prueba no vale");
    }
    if (g_chip_sueltas != 0U) {
        mal("ha llegado una queja del chip que nadie esperaba");
    }

    if (fallos == 0) { printf("TODO CORRECTO: borrado grande dentro de su sitio\n"); }
    return fallos ? 1 : 0;
}

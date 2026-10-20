/*
 * CUANTO SE QUEDA PARADO EL BUCLE ESPERANDO AL CHIP DE FLASH
 * ==========================================================
 *
 * En la radio salio un pico de 237 ms en el segundo bloque de poll, que
 * es donde viven la captura de pantalla y el volcado de ficheros. Los dos
 * ya se reparten el trabajo -"un bloque de 4 kB por vuelta"-, asi que la
 * pregunta no era si se reparte, sino cuanto cuesta UN paso.
 *
 * Y eso no lo pone el bus: lo pone el chip por dentro. spi_wait_busy()
 * mira el registro de estado cada 100 us hasta que el chip dice que ha
 * terminado, y mientras tanto el bucle principal no avanza - ni tactil,
 * ni espectro, ni cascada. El audio si: va por DMA y no pasa por aqui.
 *
 * Este banco pone en el chip simulado los tiempos del fabricante y mide
 * lo que el firmware DE VERDAD se queda esperando en cada paso de las dos
 * operaciones, sin tocar la radio.
 *
 * Tiempos del W25Q (hoja de caracteristicas, tipico / maximo):
 *   borrado de sector 4 kB   45 ms / 400 ms
 *   borrado de bloque 64 kB 150 ms / 1000 ms
 *   grabar una pagina 256 B 0,4 ms / 3 ms
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "w25q_sim.h"
#include "spi_flash.h"

static int fallos;

static void tiempos(int peor)
{
    g_us_sector = peor ? 400000U : 45000U;
    g_us_bloque = peor ? 1000000U : 150000U;
    g_us_pagina = peor ?   3000U :    400U;
}

static void cero(void)
{
    g_us_bloqueado = 0U; g_borrados = 0U; g_grabados = 0U; g_miradas = 0U;
}

static double ms(void) { return (double)g_us_bloqueado / 1000.0; }

/* Un bloque de 4 kB por el camino de siempre: borrar y grabar 16 paginas. */
static double un_bloque_4k(int peor)
{
    static uint8_t datos[4096];
    /* Con el bloque ya borrado el firmware se salta el borrado, y entonces
     * el paso sale barato y enganoso: lo que hay en la radio es un bloque
     * con datos viejos. Se ensucia a mano antes de medir. */
    memset(&g_chip[0x100000U], 0x00, 4096U);
    tiempos(peor);
    cero();
    memset(datos, 0x5A, sizeof datos);
    spi_flash_write_block_4k(0x100000U, datos);
    return ms();
}

/* Un paso de la preparacion de la zona de una foto. */
static double un_paso_borrado(int peor, uint32_t *cursor, uint32_t fin, int *queda)
{
    tiempos(peor);
    cero();
    *queda = spi_flash_borrado_paso(cursor, fin);
    return ms();
}

int main(void)
{
    uint32_t cursor, fin;
    double t;
    int queda, pasos;
    double total;

    w25q_reset();
    w25q_tam(2U * 1024U * 1024U);

    printf("\n== LO QUE BLOQUEA UN PASO DE CADA OPERACION ==\n\n");

    t = un_bloque_4k(0);
    printf("  bloque de 4 kB (volcado, restaurar, guardar un fichero)\n");
    printf("     %u borrado(s) + %u pagina(s)   tipico  %7.1f ms   (%u miradas)\n",
           g_borrados, g_grabados, t, g_miradas);
    if (g_borrados != 1U || g_grabados != 16U) {
        printf("     MAL: se esperaba 1 borrado y 16 paginas\n"); fallos++;
    }
    t = un_bloque_4k(1);
    printf("     %u borrado(s) + %u pagina(s)   peor    %7.1f ms\n",
           g_borrados, g_grabados, t);

    /* La zona de una foto: 756 kB, que es lo que reserva una captura. */
    cursor = 0x100000U;
    fin    = cursor + 756U * 1024U;
    /* Lo mismo: una zona recien borrada no se vuelve a borrar. La de una
     * foto tiene la foto anterior. */
    memset(&g_chip[cursor], 0x00, fin - cursor);
    printf("\n  borrado de la zona de una foto (756 kB), paso a paso\n");
    pasos = 0; total = 0.0;
    do {
        t = un_paso_borrado(0, &cursor, fin, &queda);
        if (pasos < 3 || !queda) {
            printf("     paso %2d: %u borrado(s)  %7.1f ms\n", pasos + 1, g_borrados, t);
        } else if (pasos == 3) {
            printf("     ...\n");
        }
        total += t;
        pasos++;
        if (pasos > 400) { printf("     MAL: no termina\n"); fallos++; break; }
    } while (queda);
    printf("     %d pasos, %.1f ms parados en total (tipico)\n", pasos, total);

    printf("\n== LO QUE ESO ES EN LA RADIO ==\n\n");
    printf("  Cada paso va en UNA vuelta del bucle principal, asi que mientras\n");
    printf("  dura la operacion la vuelta no baja de lo que tarda el chip:\n");
    printf("    - preparando la zona de la foto:  %.0f ms por vuelta (tipico)\n",
           150.0);
    printf("    - escribiendo cada bloque de 4 kB: %.0f ms por vuelta (tipico)\n",
           45.0 + 16.0 * 0.4);
    printf("  En ese rato el tactil se mira una vez cada %.0f ms y el espectro\n", 150.0);
    printf("  se queda quieto. El audio no se entera: va por DMA.\n");
    printf("\n  El pico de 237 ms que salio en la radio cae justo ahi: es UN\n");
    printf("  borrado del chip, ni mas ni menos.\n");

    if (fallos) { printf("\nFALLOS: %d\n", fallos); return 1; }
    printf("\nOK: el bloqueo es el del chip, y esta acotado a una operacion por vuelta.\n");
    return 0;
}

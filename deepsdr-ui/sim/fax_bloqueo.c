/*
 * "EL WEFAX SE QUEDA FRITO EN CUANTO PONE GUARDANDO"
 * ==================================================
 *
 * Dicho por el usuario, y la cuenta le da la razon. Una carta de fax son
 * 756 bytes por linea, o sea un bloque de 4 kB cada 5,4 lineas, y a 120
 * lineas por minuto eso es un bloque cada 2,7 segundos. Lo que faltaba por
 * medir era cuanto cuesta ESE bloque.
 *
 * Y no lo pone el bus: lo pone el chip. Escribir 4 kB sobre flash SIN
 * BORRAR obliga a borrar el sector -45 ms tipicos, 400 en el peor caso- y a
 * releer y reprogramar. Sobre flash YA BORRADA son dieciseis paginas y
 * nada mas.
 *
 * Este banco corre el User/spi_flash.c de verdad contra el chip simulado
 * con los tiempos del fabricante, y mide los microsegundos que el firmware
 * se queda mirando el registro de estado -que es exactamente el tiempo que
 * el bucle principal no avanza, o sea que el tacto no responde-.
 *
 * LAS DOS MANERAS DE ESCRIBIR UNA CARTA:
 *
 *   antes   cada bloque sobre flash sin borrar
 *   ahora   se borra la zona por delante, un borrado por vuelta del bucle,
 *           y los bloques se programan sobre lo ya borrado
 *
 * Lo que se compara es el PEOR PARON de una vuelta, porque es lo que se
 * nota, y el tiempo total parado durante la carta.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "w25q_sim.h"
#include "spi_flash.h"

static int fallos;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-54s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

#define BLOQUE   4096U
/* 756 x 600 en gris de 8 bits mas la cabecera, redondeado a bloque. */
#define CARTA_BLOQUES  112U
#define BASE     0x00040000UL        /* donde cae la reserva, alineada a 64 kB */

static void tiempos(void)
{
    g_us_sector = 45000U;    /* tipicos de la hoja de caracteristicas */
    g_us_bloque = 150000U;
    g_us_pagina = 400U;
    g_us_espera = 100U;      /* spi_wait_busy() mira cada 100 us */
}

int main(void)
{
    static uint8_t buf[BLOQUE];
    char t[120];
    uint32_t i;
    uint64_t antes_total = 0ULL, ahora_total = 0ULL;
    uint64_t antes_peor  = 0ULL, ahora_peor  = 0ULL;
    uint64_t prep_total  = 0ULL, prep_peor   = 0ULL;
    uint32_t prep_pasos  = 0UL;

    w25q_reset();
    w25q_tam(2U * 1024U * 1024U);
    tiempos();
    for (i = 0U; i < BLOQUE; i++) { buf[i] = (uint8_t)i; }

    printf("\n== Lo que se para el bucle guardando una carta de fax ==\n");

    /* ---------------------------------------------------------------
     * COMO SE HACIA ANTES: cada bloque sobre flash sin borrar.
     * --------------------------------------------------------------- */
    /*
     * Y LA ZONA SE ENSUCIA PRIMERO, QUE ES EL CASO DE VERDAD.
     *
     * Un chip recien borrado no vale para medir esto: spi_flash_write_block_4k()
     * ya se salta el borrado cuando el bloque esta a 0xFF, asi que sobre un
     * disco nuevo los dos caminos cuestan lo mismo. Lo que se paga es
     * escribir encima de lo que dejo un fichero anterior, que es lo que hay
     * en un disco con uso.
     */
    for (i = 0U; i < CARTA_BLOQUES; i++) {
        spi_flash_write_block_4k(BASE + i * BLOQUE, buf);
    }
    g_us_bloqueado = 0ULL;

    printf("\n  antes: cada bloque sobre flash sin borrar\n");
    for (i = 0U; i < CARTA_BLOQUES; i++) {
        uint64_t t0 = g_us_bloqueado;
        uint64_t d;

        spi_flash_write_block_4k(BASE + i * BLOQUE, buf);
        d = g_us_bloqueado - t0;
        antes_total += d;
        if (d > antes_peor) { antes_peor = d; }
    }
    printf("      peor vuelta %llu ms, total %llu ms en los %u bloques\n",
           (unsigned long long)(antes_peor / 1000ULL),
           (unsigned long long)(antes_total / 1000ULL), CARTA_BLOQUES);

    /* ---------------------------------------------------------------
     * COMO SE HACE AHORA: borrar por delante y programar encima.
     * ---------------------------------------------------------------
     * El cursor arranca detras de la cabecera, igual que en main.c.
     * --------------------------------------------------------------- */
    printf("\n  ahora: borrando por delante, un borrado por vuelta\n");
    {
        uint32_t cur = BASE + BLOQUE;
        uint32_t fin = BASE + CARTA_BLOQUES * BLOQUE;

        while (cur < fin) {
            uint64_t t0 = g_us_bloqueado;
            uint64_t d;

            (void)spi_flash_borrado_paso(&cur, fin);
            d = g_us_bloqueado - t0;
            prep_total += d;
            prep_pasos++;
            if (d > prep_peor) { prep_peor = d; }
        }
    }
    printf("      preparar: %u vueltas, peor %llu ms, total %llu ms\n",
           prep_pasos, (unsigned long long)(prep_peor / 1000ULL),
           (unsigned long long)(prep_total / 1000ULL));

    for (i = 1U; i < CARTA_BLOQUES; i++) {
        uint64_t t0 = g_us_bloqueado;
        uint64_t d;

        spi_flash_write_block_4k_borrado(BASE + i * BLOQUE, buf);
        d = g_us_bloqueado - t0;
        ahora_total += d;
        if (d > ahora_peor) { ahora_peor = d; }
    }
    printf("      escribir: peor vuelta %llu ms, total %llu ms\n",
           (unsigned long long)(ahora_peor / 1000ULL),
           (unsigned long long)(ahora_total / 1000ULL));

    /* ---------------------------------------------------------------
     * LO QUE SE EXIGE.
     * ---------------------------------------------------------------
     * A 120 lpm entra un bloque cada 2,7 s. Un paron de 800 ms de cada
     * 2.700 es un tercio del tiempo con la radio muerta; uno de 10 ms no
     * se ve. El limite se pone en 50 ms, que es lo que ya cuesta una
     * vuelta larga del bucle con el espectro pintando.
     * --------------------------------------------------------------- */
    printf("\n  lo que se exige\n");
    snprintf(t, sizeof t, "%llu ms por bloque",
             (unsigned long long)(ahora_peor / 1000ULL));
    di("escribir un bloque ya no para el bucle ni 50 ms",
       ahora_peor < 50000ULL, t);

    snprintf(t, sizeof t, "%llu ms -> %llu ms",
             (unsigned long long)(antes_peor / 1000ULL),
             (unsigned long long)(ahora_peor / 1000ULL));
    di("y al menos cinco veces menos que antes",
       ahora_peor * 5ULL < antes_peor, t);

    /* Preparar cuesta, pero se paga una vez y repartido. Un borrado de
     * 64 kB son 150 ms tipicos, y ese es el peor paso que puede salir. */
    snprintf(t, sizeof t, "peor paso %llu ms", (unsigned long long)(prep_peor / 1000ULL));
    di("ningun paso de preparacion pasa de 160 ms", prep_peor <= 160000ULL, t);
    snprintf(t, sizeof t, "%llu ms en %u vueltas",
             (unsigned long long)(prep_total / 1000ULL), prep_pasos);
    di("y preparar la carta entera cuesta menos de 2 s", prep_total < 2000000ULL, t);

    /*
     * Y SUMANDO TODO, QUE ES EL NUMERO HONESTO: preparar no es gratis.
     *
     * Lo que cambia no es solo el total, es COMO esta repartido. Antes
     * eran 112 parones de 51 ms salpicados por toda la carta, uno cada 2,7
     * segundos, durante diez minutos. Ahora son 21 parones al principio y
     * luego nada que se note. El total baja ademas a menos de la mitad.
     */
    snprintf(t, sizeof t, "%llu ms -> %llu ms",
             (unsigned long long)(antes_total / 1000ULL),
             (unsigned long long)((prep_total + ahora_total) / 1000ULL));
    di("la carta entera para el bucle menos de la mitad",
       (prep_total + ahora_total) * 2ULL < antes_total, t);

    /* ---------------------------------------------------------------
     * Y LO QUE DE VERDAD IMPORTABA: QUE NO BORRE LO QUE NO ES SUYO.
     * ---------------------------------------------------------------
     *
     * spi_flash_borrado_paso() redondeaba el cursor HACIA ABAJO al limite
     * de sector. Con una zona alineada a 4 kB eso no mueve nada, y todo lo
     * que lo llamaba hasta entonces lo estaba. Pero una reserva de fichero
     * empieza donde empiece su CLUSTER, y la zona de datos de un volumen
     * formateado por Windows no tiene por que caer en un limite de 4 kB:
     * con el cursor desalineado, el redondeo se salia POR EL PRINCIPIO.
     *
     * Aqui se le da justo eso -un cursor desalineado- y se marca todo lo
     * que hay debajo como intocable. El chip simulado se queja solo si se
     * toca.
     * --------------------------------------------------------------- */
    printf("\n  borrar por delante, con la zona desalineada\n");
    {
        uint32_t cur, fin;
        int fallos_antes = g_fallos_chip;

        /* Zona "reservada" que empieza a medio sector, como un cluster de
         * un volumen de Windows. */
        cur = BASE + BLOQUE + 512U;
        fin = BASE + 16U * BLOQUE;

        /* Todo lo que hay por debajo es de otro. */
        g_intocable_ini = 0UL;
        g_intocable_fin = cur;

        while (cur < fin) {
            if (!spi_flash_borrado_paso(&cur, fin)) { break; }
        }

        g_intocable_ini = g_intocable_fin = 0xFFFFFFFFU;

        snprintf(t, sizeof t, "%d avisos del chip",
                 g_fallos_chip - fallos_antes);
        di("no toca ni un byte por debajo del cursor",
           g_fallos_chip == fallos_antes, t);
        snprintf(t, sizeof t, "acabo en 0x%06X de 0x%06X", (unsigned)cur,
                 (unsigned)fin);
        di("y llega hasta el final de lo suyo", cur >= fin, t);
    }

    /* ---------------------------------------------------------------
     * Y ESCRIBIR UN BLOQUE EN UNA DIRECCION NO ALINEADA.
     * ---------------------------------------------------------------
     * spi_flash_write_block_4k() redondeaba la direccion HACIA ABAJO y
     * borraba y reescribia ese sector entero. Con una direccion no alineada
     * eso se lleva hasta 4 kB del vecino de abajo y ademas deja los datos
     * corridos.
     *
     * Y pasa de verdad: la cabecera de una foto se reescribe en la
     * direccion del PRIMER CLUSTER del fichero, y la zona de datos de un
     * volumen formateado por Windows no tiene por que empezar en un limite
     * de 4 kB.
     *
     * Aqui se escribe a medio sector con todo lo de alrededor marcado
     * intocable, y despues se lee para comprobar que lo escrito esta donde
     * se pidio y no corrido.
     * --------------------------------------------------------------- */
    printf("\n  escribir 4 kB en una direccion no alineada\n");
    {
        uint32_t donde = BASE + 7U * BLOQUE + 1536U;   /* a medio sector */
        static uint8_t lee[BLOQUE];
        static uint8_t antes[4U * BLOQUE];
        static uint8_t ahora[4U * BLOQUE];
        uint32_t zona = BASE + 6U * BLOQUE;
        uint32_t d, malos = 0U;

        for (i = 0U; i < BLOQUE; i++) { buf[i] = (uint8_t)(i * 3U + 7U); }

        /*
         * Lo que se exige NO es que el chip no se toque -el camino seguro
         * lee, empalma y reescribe el sector entero, asi que lo toca por
         * definicion- sino que LOS DATOS DEL VECINO SIGAN AHI. Que es lo
         * que importa y lo que no pasaba.
         */
        spi_flash_read(zona, antes, sizeof antes);
        spi_flash_write_block_4k(donde, buf);
        spi_flash_read(zona, ahora, sizeof ahora);

        for (d = 0U; d < sizeof antes; d++) {
            uint32_t a = zona + d;
            if (a >= donde && a < donde + BLOQUE) { continue; }  /* lo escrito */
            if (antes[d] != ahora[d]) { malos++; }
        }
        snprintf(t, sizeof t, "%u bytes del vecino cambiados", (unsigned)malos);
        di("no se lleva por delante nada de alrededor", malos == 0U, t);

        spi_flash_read(donde, lee, sizeof lee);
        for (d = 0U; d < BLOQUE; d++) { if (lee[d] != buf[d]) { break; } }
        snprintf(t, sizeof t, "difieren en el byte %u", (unsigned)d);
        di("y lo escrito esta donde se pidio, sin correrse", d == BLOQUE, t);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

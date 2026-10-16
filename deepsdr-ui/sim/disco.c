/*
 * EL DISCO DE WINDOWS: QUE ESCRIBIR UN BLOQUE NO SE LLEVE A LOS VECINOS.
 *
 * Pedido por el usuario. Pues eso es la clase
 * MSC, y la clase MSC escribe bloques de 512 bytes sueltos, en el orden que
 * le da la gana. La flash SPI borra de 4 kB en 4 kB.
 *
 * LO QUE SE MIDE: que despues de escribir UN bloque, los otros SIETE de su
 * mismo sector de 4 kB siguen exactamente igual. Ese es el fallo que deja
 * el volumen corrupto, y ya nos mordio una vez con CONFIG.CSV.
 *
 * Y se mide en los OCHO desplazamientos posibles dentro del sector, no solo
 * en el primero: el fallo de septiembre solo aparecia cuando el bloque NO
 * era el primero de los ocho.
 */
#include "w25q_sim.h"
#include "disco.h"
#include "spi_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN (1024U*1024U)
static int fallos = 0;
static uint8_t *copia;

static void di(const char *q, int bien)
{
    printf("    %-56s %s\n", q, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

int main(int argc, char **argv)
{
    const char *img = (argc > 1) ? argv[1] : "out/volumen_con_config.img";
    static uint8_t buf[DISCO_BLOQUE], leido[DISCO_BLOQUE];
    FILE *f;
    uint32_t i, k;

    printf("\nEL DISCO QUE VE WINDOWS (bloques de 512 sobre sectores de 4 kB)\n");

    w25q_reset();
    f = fopen(img, "rb");
    if (!f || fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  falta o no mide 1 MB: %s\n", img); return 1;
    }
    fclose(f);
    spi_flash_init();

    printf("\n  el tamano que se le dice a Windows\n");
    di("1 MB en bloques de 512 = 2048", disco_bloques() == 2048U);
    di("la zona alta NO entra en el disco",
       disco_bloques() * DISCO_BLOQUE == VOLUMEN);

    printf("\n  leer devuelve lo que hay en el chip\n");
    {
        int bien = 1;
        for (i = 0U; i < 2048U; i += 173U) {
            if (!disco_lee(i, leido) ||
                memcmp(leido, &g_chip[i * DISCO_BLOQUE], DISCO_BLOQUE) != 0) { bien = 0; }
        }
        di("doce bloques repartidos por todo el volumen", bien);
        di("un bloque fuera del disco se rechaza", disco_lee(2048U, leido) == 0U);
    }

    printf("\n  escribir UN bloque y mirar a los otros siete del sector\n");
    copia = malloc(VOLUMEN);
    for (k = 0U; k < 8U; k++) {
        uint32_t base = 400U;                  /* un sector con datos dentro */
        uint32_t lba  = base + k;
        int vecinos_ok = 1, escrito_ok;

        memcpy(copia, g_chip, VOLUMEN);
        for (i = 0U; i < DISCO_BLOQUE; i++) { buf[i] = (uint8_t)(0xA0U + k + i); }

        if (!disco_escribe(lba, buf)) { di("la escritura se acepta", 0); continue; }

        (void)disco_lee(lba, leido);
        escrito_ok = (memcmp(leido, buf, DISCO_BLOQUE) == 0);

        for (i = 0U; i < 8U; i++) {
            uint32_t v = base + i;
            if (v == lba) { continue; }
            if (memcmp(&g_chip[v * DISCO_BLOQUE], &copia[v * DISCO_BLOQUE],
                       DISCO_BLOQUE) != 0) { vecinos_ok = 0; }
        }
        {
            char q[80];
            sprintf(q, "bloque %u de los 8: se escribe y los vecinos aguantan",
                    (unsigned)k);
            di(q, escrito_ok && vecinos_ok);
        }
    }

    /*
     * EL CAMINO RAPIDO: la clase MSC entrega 4 kB de golpe. disco_escribe_n()
     * los mete de una sola pasada cuando cubren sectores enteros. Lo que hay
     * que comprobar es que sale EXACTAMENTE lo mismo que bloque a bloque -no
     * "parecido"- y que cuando la tirada NO esta alineada se cae al camino
     * lento sin estropear a los vecinos.
     */
    printf("\n  escribir 4 kB de golpe, como los entrega el USB\n");
    {
        static uint8_t ocho[8U * DISCO_BLOQUE];
        uint8_t *lento = malloc(VOLUMEN);
        uint32_t base = 800U;   /* 800 % 8 == 0: sector entero */
        int igual;

        for (i = 0U; i < sizeof ocho; i++) { ocho[i] = (uint8_t)(i * 7U + 3U); }

        /* primero, bloque a bloque */
        memcpy(g_chip, copia, VOLUMEN);
        for (i = 0U; i < 8U; i++) { (void)disco_escribe(base + i, &ocho[i * DISCO_BLOQUE]); }
        memcpy(lento, g_chip, VOLUMEN);

        /* y ahora de golpe, desde el mismo punto de partida */
        memcpy(g_chip, copia, VOLUMEN);
        di("la escritura de 8 bloques se acepta",
           disco_escribe_n(base, ocho, 8U) != 0U);
        igual = (memcmp(g_chip, lento, VOLUMEN) == 0);
        di("el volumen queda IDENTICO al camino bloque a bloque", igual);

        /* y una tirada que NO empieza alineada: tiene que caer al lento */
        memcpy(g_chip, copia, VOLUMEN);
        for (i = 0U; i < 8U; i++) { (void)disco_escribe(base + 3U + i, &ocho[i * DISCO_BLOQUE]); }
        memcpy(lento, g_chip, VOLUMEN);
        memcpy(g_chip, copia, VOLUMEN);
        (void)disco_escribe_n(base + 3U, ocho, 8U);
        di("sin alinear: tambien identico", memcmp(g_chip, lento, VOLUMEN) == 0);
        free(lento);
        memcpy(g_chip, copia, VOLUMEN);
        for (i = 0U; i < 8U; i++) { (void)disco_escribe(400U + i, &ocho[0]); }
    }

    printf("\n  y que el resto del volumen no se ha movido\n");
    {
        /* fuera del sector tocado, todo tiene que estar igual que la imagen */
        FILE *g = fopen(img, "rb");
        uint8_t *orig = malloc(VOLUMEN);
        int bien = 1;
        if (fread(orig, 1, VOLUMEN, g) != VOLUMEN) { return 1; }
        fclose(g);
        for (i = 0U; i < VOLUMEN; i++) {
            uint32_t lba = i / DISCO_BLOQUE;
            if (lba >= 400U && lba < 408U) { continue; }
            if (g_chip[i] != orig[i]) { bien = 0; break; }
        }
        di("ni un byte fuera del sector de 4 kB tocado", bien);
        free(orig);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

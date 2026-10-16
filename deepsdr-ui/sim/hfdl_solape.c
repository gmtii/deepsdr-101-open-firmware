/*
 * ======================================================================
 * LA SALIDA DEL VITERBI SE ESCRIBE ENCIMA DE LA TABLA DEL DESENTRELAZADOR
 * ======================================================================
 *
 * Este banco no mide si algo funciona: mide una PREMISA, la que sostiene
 * el cerrojo de hfdl_modo.c (ver s_remate alli).
 *
 * De una revision a fondo del 10/10/2026 salio que dos contextos escriben
 * la misma memoria prestada sin coordinarse: hfdl_modo_mete() desde la
 * interrupcion del audio y hfdl_modo_poll() desde el bucle principal. Se
 * puso un cerrojo. Pero un cerrojo cuyo motivo no esta medido es un
 * cerrojo que alguien quita dentro de seis meses porque "no parece hacer
 * falta".
 *
 * Asi que lo que se mide aqui es el solape: QUE finish_ex() escribe
 * encima de los bytes que begin_segment_ex() reparte al desentrelazador.
 * Si eso es verdad -y lo es, se ve abajo-, entonces rematar una trama
 * mientras otra se esta llenando corrompe las dos, y el cerrojo tiene que
 * seguir ahi.
 *
 * El dia que alguien reorganice el reparto de la region prestada y la
 * salida deje de solaparse, este banco se pondra rojo. Eso NO sera un
 * fallo: sera el aviso de que la premisa cambio y hay que releer el
 * cerrojo. Esta escrito para eso.
 *
 * Se llama al codigo DE VERDAD del firmware, no a una copia.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hfdl_payload_decode.h"
#include "hfdl_deinterleaver.h"
#include "hfdl_ram.h"
#include "ft8_shared_ram.h"

#define CAPACIDAD ((uint32_t)sizeof(ft8_shared_ram_t) - HFDL_RAM_CABECERA)
#define SELLO 0x5Au          /* el patron con el que se marca la tabla */

static int fallos = 0;

static void ver(const char *q, bool got, bool esperado)
{
    printf("    %-52s %s\n", q, (got == esperado) ? "bien" : "MAL");
    if (got != esperado) { fallos++; }
}

/* Muñon: el banco usa siempre las _ex con su propia memoria. */
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out)
{
    if (capacity_out != NULL) { *capacity_out = 0u; }
    return NULL;
}

int main(void)
{
    static uint8_t ram[131072];
    uint32_t estado = hfdl_payload_decode_estado_bytes();
    const hfdl_rate_params_t *r = &hfdl_rate_params[0];
    uint32_t tabla = (uint32_t)hfdl_deinterleaver_column_cnt(r)
                     * HFDL_DEINTERLEAVER_ROW_CNT;
    const uint8_t *datos = NULL;
    uint32_t largo = 0u, i, tocados = 0u;

    puts("");
    puts("EL REPARTO DE LA MEMORIA PRESTADA DE HFDL, Y DONDE SE PISA");
    puts("");
    printf("  el estado del segmento ocupa  %6u bytes  (del 0 al %u)\n",
           (unsigned)estado, (unsigned)estado - 1u);
    printf("  la tabla del giro 0 empieza en %5u y mide %u\n",
           (unsigned)estado, (unsigned)tabla);

    /* 1. Se abre un segmento: begin_segment_ex() reparte la region. */
    memset(ram, 0, sizeof ram);
    ver("se abre un segmento del giro 0",
        hfdl_payload_decode_begin_segment_ex(0u, ram, CAPACIDAD), true);

    /* 2. Se marca la tabla entera con un sello reconocible. Esto es lo que
     *    la interrupcion estaria escribiendo mientras llena una rafaga. */
    memset(ram + estado, SELLO, tabla);

    /* 3. Y se remata, que es lo que hace el bucle principal. */
    hfdl_payload_decode_finish_ex(ram, &datos, &largo);

    /* 4. Cuantos bytes del sello siguen en pie. */
    for (i = 0u; i < tabla; i++) {
        if (ram[estado + i] != SELLO) { tocados++; }
    }
    printf("\n  bytes de la tabla que el remate ha cambiado: %u de %u (%u%%)\n\n",
           (unsigned)tocados, (unsigned)tabla,
           (unsigned)((tocados * 100u) / tabla));

    ver("rematar PISA la tabla del desentrelazador", tocados > 0u, true);
    ver("y la salida del remate cae dentro de esa tabla",
        datos != NULL && datos >= (ram + estado)
                      && datos <  (ram + estado + tabla), true);

    puts("");
    puts("  LO QUE ESTO SIGNIFICA");
    puts("    Si una rafaga se remata mientras otra se esta llenando, las");
    puts("    dos salen mal: el remate escribe donde la otra guarda sus");
    puts("    simbolos. Por eso hfdl_modo.c tiene un cerrojo (s_remate) y");
    puts("    la interrupcion no abre segmento mientras el bucle principal");
    puts("    esta dentro. Si este banco se pone rojo, el reparto ha");
    puts("    cambiado: hay que releer ese cerrojo antes de tocarlo.");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

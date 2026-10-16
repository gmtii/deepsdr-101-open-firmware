/*
 * ======================================================================
 * LA MEMORIA PRESTADA DE HFDL, CONTRA LOS OCHO GIROS
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * HFDL tiene ocho combinaciones de velocidad y ranura -los "giros", el
 * indice M1 del preambulo- y cada una pide un trozo de memoria distinto
 * para desentrelazar y para el Viterbi. El decodificador no tiene memoria
 * propia: le presta la suya la cascada (ver hfdl_ram.c), y cuando lo que
 * pide el giro no cabe, begin_segment() se niega y la rafaga se pierde.
 *
 * POR QUE ESTE BANCO, Y POR QUE AHORA.
 *
 * El numero de cuanto pide cada giro estaba escrito en un comentario de
 * hfdl_ram.h desde el 28/09, a mano. Un numero a mano en un comentario es
 * un numero que nadie recalcula: el dia que alguien cambie el tamano del
 * estado del segmento, o el de una decision del Viterbi, o la cabecera
 * reservada, el comentario seguira diciendo lo mismo y estara mintiendo.
 *
 * Esto lo mide llamando a la funcion DE VERDAD -la del firmware, no una
 * copia- con la capacidad DE VERDAD, sacada de sizeof(ft8_shared_ram_t)
 * menos la cabecera. Si alguien ensancha la union, este banco lo dice
 * solo y sin que nadie se acuerde de volver aqui.
 *
 * LO QUE COMPRUEBA:
 *
 *   - que los seis giros que caben hoy se abren;
 *   - que los dos que no caben se niegan;
 *   - que la frontera MUERDE: un byte menos del que pide el giro, y se
 *     niega. Sin esto, un banco que solo mirara "se abre" pasaria aunque
 *     la comprobacion de capacidad se hubiera borrado entera;
 *   - y los dos casos malos de siempre: un giro que no existe y una
 *     memoria nula.
 *
 * NO JUZGA si la capacidad de hoy es la correcta - eso es una decision de
 * reparto de RAM y no de este fichero. Lo que hace es que el numero este
 * SIEMPRE a la vista, para que esa decision se tome con el delante.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hfdl_payload_decode.h"
#include "hfdl_deinterleaver.h"
#include "hfdl_viterbi.h"   /* HFDL_VITERBI_DECISION_BYTES() */
#include "hfdl_ram.h"
#include "ft8_shared_ram.h"

/* La capacidad de verdad, la misma cuenta que hace hfdl_scope_get_hfdl_ram().
 * No es un numero a mano: si la union crece, esto crece con ella. */
#define CAPACIDAD ((uint32_t)sizeof(ft8_shared_ram_t) - HFDL_RAM_CABECERA)

static int fallos = 0;

static void ver(const char *q, bool got, bool esperado)
{
    printf("    %-46s %s\n", q, (got == esperado) ? "bien" : "MAL");
    if (got != esperado) { fallos++; }
}

/*
 * Lo que pide un giro, con la misma cuenta que begin_segment_ex(). Se repite
 * aqui a proposito: si alguien cambia la de alla y no la de aqui, los dos
 * numeros dejan de cuadrar y el banco lo canta en la frontera -que es
 * justo lo que un banco tiene que hacer-.
 */
static uint32_t pide_el_giro(uint32_t i)
{
    const hfdl_rate_params_t *r = &hfdl_rate_params[i];
    uint32_t tabla = (uint32_t)hfdl_deinterleaver_column_cnt(r)
                     * HFDL_DEINTERLEAVER_ROW_CNT;
    uint32_t simbolos = (uint32_t)r->data_segment_cnt * 30u;
    uint32_t bits_cod = simbolos * (uint32_t)r->scheme_bits;
    uint32_t vit_ent  = (r->code_rate_denom == 4u) ? (bits_cod / 2u) : bits_cod;
    uint32_t vit_sal  = vit_ent / 2u;

    return hfdl_payload_decode_estado_bytes()
           + tabla + 512u + (uint32_t)HFDL_VITERBI_DECISION_BYTES(vit_sal);
}

/*
 * Muñon: el banco llama siempre a las _ex con su propia memoria, asi que la
 * version que va a buscar la region prestada de verdad no se ejerce nunca.
 * Esta aqui solo para enlazar, y devuelve NULL a proposito: si algun dia
 * alguna ruta la llamara sin querer, se notaria en seguida.
 */
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out)
{
    if (capacity_out != NULL) { *capacity_out = 0u; }
    return NULL;
}

int main(void)
{
    static uint8_t ram[131072];
    uint32_t i, caben = 0u;

    puts("");
    puts("LA MEMORIA PRESTADA DE HFDL, CONTRA LOS OCHO GIROS");
    puts("");
    printf("  la union mide            %6u bytes\n",
           (unsigned)sizeof(ft8_shared_ram_t));
    printf("  la cabecera reservada     %6u\n", (unsigned)HFDL_RAM_CABECERA);
    printf("  le queda al decodificador %6u\n\n", (unsigned)CAPACIDAD);

    for (i = 0u; i < 8u; i++) {
        const hfdl_rate_params_t *r = &hfdl_rate_params[i];
        uint32_t p = pide_el_giro(i);
        bool cabe = (p <= CAPACIDAD);
        char q[96];

        if (cabe) { caben++; }
        sprintf(q, "giro %u  %4u bps %-6s  pide %6u  %s",
                (unsigned)i, (unsigned)r->bitrate_bps,
                r->double_slot ? "doble" : "simple", (unsigned)p,
                cabe ? "se abre " : "se niega");
        memset(ram, 0xA5, sizeof ram);
        ver(q, hfdl_payload_decode_begin_segment_ex(i, ram, CAPACIDAD), cabe);
    }

    printf("\n  caben %u de los 8\n", (unsigned)caben);

    /*
     * Y AQUI SE EXIGE, QUE HASTA HOY SOLO SE CONTABA - 11/10/2026.
     *
     * Pedido por el usuario.
     *
     * La X de esa chapa decia que las veinte rafagas tiradas eran del giro
     * 6, y el 11/10 se pagaron los 5.872 bytes que le faltaban: la union
     * paso de 54.432 a 60.304 y el giro 6 entro. Eso es RAM de la radio, y
     * lo que se compra con RAM hay que vigilarlo, porque el dia que alguien
     * necesite sitio esta union es el primer sitio donde va a mirar.
     *
     * Siete, no ocho: el giro 7 pide 76.288 + 9.216 = 85.504, o sea 25.200
     * mas de los que hay libres EN TODA LA RADIO. Que se niegue no es un
     * fallo, es la respuesta correcta - y por eso se exige SIETE y no "los
     * que sean".
     */
    ver("caben siete de los ocho giros (el 7 no entra ni pagando)",
        caben == 7u, true);

    puts("");
    puts("  y que la frontera muerde de verdad");
    for (i = 0u; i < 8u; i++) {
        uint32_t p = pide_el_giro(i);
        char q[80];

        memset(ram, 0xA5, sizeof ram);
        sprintf(q, "giro %u con UN BYTE MENOS del que pide: se niega",
                (unsigned)i);
        ver(q, hfdl_payload_decode_begin_segment_ex(i, ram, p - 1u), false);
        memset(ram, 0xA5, sizeof ram);
        sprintf(q, "giro %u con justo lo que pide: se abre", (unsigned)i);
        ver(q, hfdl_payload_decode_begin_segment_ex(i, ram, p), true);
    }

    puts("");
    puts("  y los dos casos malos");
    memset(ram, 0xA5, sizeof ram);
    ver("un giro que no existe (8): se niega",
        hfdl_payload_decode_begin_segment_ex(8u, ram, CAPACIDAD), false);
    ver("sin memoria (NULL): se niega",
        hfdl_payload_decode_begin_segment_ex(0u, NULL, CAPACIDAD), false);

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * LA RADIO SE ADAPTA AL CHIP QUE TENGA SOLDADO.
 *
 * Esto corre el User/spi_flash.c, el User/disco.c y el User/zona_alta.c de
 * verdad contra el W25Q simulado, cambiandole el tamaño.
 *
 * LA REGLA SE ESCRIBE AQUI APARTE, a mano, y no se le pregunta al firmware:
 *
 *     disco     = capacidad - 1 MB
 *     za_base   = capacidad - 1 MB + 4 kB
 *     za_tope   = capacidad
 *
 * Si este banco llamara a las mismas cuentas que el firmware comprobaria
 * que son iguales a si mismas. Lo que tiene que comprobar es que dan el
 * numero correcto, y para eso el numero correcto tiene que estar escrito
 * en otro sitio.
 *
 * Y TRES COSAS QUE IMPORTAN MAS QUE EL NUMERO:
 *
 *  - que el disco NUNCA se meta en la zona alta, en ningun tamaño. Es lo
 *    que impide que un formateo desde Windows se lleve las bases;
 *  - que un chip recien borrado -todo 0xFF, donde la medida por alias no
 *    distingue nada- se resuelva por el codigo JEDEC y no se quede en el
 *    suelo. Es el caso del chip nuevo recien soldado;
 *  - y que ante la duda se conteste de MENOS. Anunciar de mas es lo unico
 *    que puede corromper el volumen.
 */
#include "w25q_sim.h"
#include "spi_flash.h"
#include "disco.h"
#include "zona_alta.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-54s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* La regla, escrita aparte. */
#define MB(n)        ((uint32_t)(n) * 1024UL * 1024UL)
#define RESERVA      MB(1)
#define HUECO        4096UL

static uint32_t disco_esperado(uint32_t cap) { return cap - RESERVA; }
static uint32_t base_esperada(uint32_t cap)  { return cap - RESERVA + HUECO; }

/*
 * Pone el chip del tamaño pedido y le mete algo que NO sea uniforme en los
 * primeros 64 bytes, que es lo que la medida por alias necesita para poder
 * distinguir. Equivale a un chip con un volumen dentro, que es el caso
 * normal.
 */
static void chip_con_datos(uint32_t cap)
{
    uint32_t i;

    w25q_reset();
    w25q_tam(cap);
    /*
     * LOS PRIMEROS 64 kB CON ALGO DENTRO, QUE ES LO QUE HAY DE VERDAD.
     *
     * Un volumen FAT recien formateado tiene ahi el sector de arranque, las
     * dos copias de la tabla de asignacion y el directorio raiz. De ahi
     * para arriba, hasta que se copian ficheros, es todo 0xFF.
     *
     * Antes aqui se escribian solo los primeros 64 bytes, y eso hacia que
     * el banco midiera un chip que no se parece a ninguno: con una sola
     * ventana buena. La medida por alias necesita datos en las tres, y
     * poner solo 64 bytes escondia justo el caso que importaba.
     */
    for (i = 0U; i < 64U * 1024U; i++) {
        g_chip[i] = (uint8_t)((i * 7U + 1U) ^ (i >> 8));
    }
    spi_flash_olvida_capacidad();
}

static void una_talla(uint32_t cap, const char *nombre)
{
    char t[64];
    uint32_t disco_b, cap_leida;

    printf("\n  %s: %u MB\n", nombre, (unsigned)(cap / MB(1)));
    chip_con_datos(cap);

    cap_leida = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap_leida);
    di("mide la capacidad que tiene el chip", cap_leida == cap, t);

    disco_b = disco_bloques() * DISCO_BLOQUE;
    snprintf(t, sizeof t, "%u bytes = %u MB", (unsigned)disco_b,
             (unsigned)(disco_b / MB(1)));
    di("el disco USB es la capacidad menos un mega",
       disco_b == disco_esperado(cap), t);

    snprintf(t, sizeof t, "0x%06X", (unsigned)za_base());
    di("la zona alta empieza donde toca", za_base() == base_esperada(cap), t);

    snprintf(t, sizeof t, "0x%06X", (unsigned)za_tope());
    di("y acaba en el final del chip", za_tope() == cap, t);

    /* LA QUE DE VERDAD PROTEGE ALGO. */
    di("el disco NO llega a la zona alta (ni un bloque)",
       disco_b <= za_base() - HUECO, 0);
    di("y la region de datos mide lo mismo en todos los chips",
       za_tope() - za_base() == RESERVA - HUECO, 0);
}

int main(void)
{
    char t[64];
    uint32_t cap;

    printf("\nLA RADIO SE ADAPTA AL CHIP QUE TENGA SOLDADO\n");

    una_talla(MB(2),  "W25Q16, el de fabrica");
    una_talla(MB(8),  "W25Q64");
    una_talla(MB(16), "W25Q128");

    /*
     * EL CHIP RECIEN SOLDADO. Todo a 0xFF: leer en 0 y leer en 4 MB dan lo
     * mismo aunque el chip tenga 8, asi que el alias no distingue nada y
     * hay que creerle al JEDEC.
     */
    /*
     * EL FALSO ALIAS - 20/10/2026, preguntado por el usuario: "¿puede ser
     * que un chip de 2 MB lo estemos viendo como de 1?".
     *
     * Con una sola ventana de 64 bytes, si: basta que en 0x100000 haya por
     * casualidad lo mismo que en 0 -una copia del sector de arranque, por
     * ejemplo- para declarar un alias que no existe y anunciar la mitad
     * del chip. Aqui se monta ese chip a mala idea: 2 MB de verdad, con
     * los 64 bytes del principio copiados en el megabyte de arriba.
     *
     * Tiene que seguir midiendo 2 MB, porque las otras dos ventanas -un
     * cuarto y la mitad- no casan.
     */
    printf("\n  un chip de 2 MB con una copia del principio en 0x100000\n");
    chip_con_datos(MB(2));
    memcpy(&g_chip[MB(1)], &g_chip[0], 64U);
    spi_flash_olvida_capacidad();
    cap = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap);
    di("no se traga el falso alias: sigue midiendo 2 MB", cap == MB(2), t);
    di("y el disco sigue siendo de 1 MB",
       disco_bloques() * DISCO_BLOQUE == MB(1), 0);

    printf("\n  un W25Q64 recien borrado, todo a 0xFF\n");
    w25q_reset();
    w25q_tam(MB(8));
    memset(g_chip, 0xFF, MB(8));
    spi_flash_olvida_capacidad();
    cap = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap);
    di("el alias no vale, pero el JEDEC si: salen 8 MB", cap == MB(8), t);
    di("y el disco sale de 7 MB",
       disco_bloques() * DISCO_BLOQUE == MB(7), 0);

    /*
     * UN CHIP DE 8 MB QUE NO DA LA VUELTA - 11/10/2026.
     *
     * alta_mide_tam() mide buscando donde se repite el principio del chip,
     * y eso solo funciona si la direccion da la vuelta. Un chip que por
     * encima de su final contesta 0xFF no tiene alias que encontrar, y el
     * bucle se acaba devolviendo su tope: 16 MB.
     *
     * Eso NO es una medida de 16 MB, es "no lo he encontrado", y hasta hoy
     * se trataban igual: un chip de 8 se anunciaba de 16, el disco USB
     * decia tener 15 MB y Windows escribia en bloques que no existen. Por
     * eso esta prueba existe y por eso mira el numero exacto y no solo que
     * sea "razonable".
     */
    printf("\n  un W25Q64 que NO da la vuelta (contesta 0xFF al pasarse)\n");
    w25q_reset();
    w25q_tam(MB(8));
    w25q_sin_alias(1U);
    /*
     * Y LOS PRIMEROS 64 BYTES TIENEN QUE SER DISTINTOS ENTRE SI, que si no
     * esta prueba no prueba nada. *** Aqui ponia un memset a 0x5A, y con
     * eso alta_mide_tam() se sale por "el sector 0 es uniforme, el metodo
     * no vale" ANTES de buscar el alias: contestaba el JEDEC y la prueba
     * pasaba igual con el fallo dentro. ***
     */
    {
        unsigned q;
        for (q = 0U; q < 64U; q++) { g_chip[q] = (uint8_t)(q * 7U + 3U); }
    }
    spi_flash_olvida_capacidad();
    cap = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap);
    di("sin alias manda el JEDEC: 8 MB, no 16", cap == MB(8), t);
    di("y el disco sale de 7 MB, no de 15",
       disco_bloques() * DISCO_BLOQUE == MB(7), 0);
    w25q_sin_alias(0U);

    /*
     * Y LA RESPUESTA SEGURA. Un chip que no contesta al JEDEC (todo 0xFF en
     * la respuesta) y cuyo alias no vale no se puede medir. La respuesta
     * tiene que ser el suelo -2 MB-, nunca un numero grande: anunciar de
     * mas es lo unico que puede corromper el volumen.
     */
    printf("\n  un chip que no se deja medir\n");
    w25q_reset();
    w25q_tam(MB(2));
    memset(g_chip, 0xFF, MB(2));
    spi_flash_olvida_capacidad();
    cap = spi_flash_capacidad();
    di("ante la duda, el numero pequeño: 2 MB", cap == MB(2), 0);
    di("nunca contesta de mas", cap <= w25q_tam_actual(), 0);

    /*
     * Y UN CHIP MAS PEQUEÑO QUE EL SUELO - 18/10/2026.
     *
     * Este caso faltaba, y era el que importaba. El suelo de 2 MB se
     * aplicaba a TODA respuesta, no solo a la que no habia podido medirse,
     * asi que un chip de 1 MB -uno mas pequeño soldado, o un clon que dice
     * la verdad sobre un tamaño menor- se anunciaba como de 2. Y entonces
     * za_base() cae en 0x101000, que en un chip de 1 MB da la vuelta y
     * aterriza en 0x001000: encima de la primera copia de la FAT.
     *
     * Lo que pasa a partir de ahi esta medido en sim/chip_chico.c. Aqui
     * solo se mira el numero, que es donde nace.
     */
    printf("\n  un chip de 1 MB, por debajo del suelo\n");
    chip_con_datos(MB(1));
    cap = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap);
    di("se anuncia 1 MB, no el suelo de 2", cap == MB(1), t);
    di("nunca contesta de mas", cap <= w25q_tam_actual(), 0);
    snprintf(t, sizeof t, "0x%06X", (unsigned)za_base());
    di("y za_base() no se sale del chip", za_base() < cap, t);

    /*
     * UN CHIP DE 2 MB QUE DICE SER DE 1 - 20/10/2026.
     *
     * El chip es un W25Q16JVSIQ, 2 MB. Y contesta EF 40 14,
     * que como codigo de capacidad son 1 MB. La etiqueta de fabrica y el
     * identificador no coinciden.
     *
     * La medida por alias NO puede equivocarse en este sentido: en un chip
     * de 1 MB la direccion 1 MB + k es fisicamente la direccion k, asi que
     * encontrar una diferencia ahi demuestra que hay mas de 1 MB. Por eso
     * cuando la medida dice mas que el codigo, gana la medida.
     */
    printf("\n  un chip de 2 MB cuyo codigo JEDEC dice 1 MB\n");
    chip_con_datos(MB(2));
    /* Y solo con datos al principio, que es como esta un chip recien
     * formateado: de ahi para arriba, todo 0xFF. Es el caso que antes
     * dejaba sin medir y hacia mandar al codigo. */
    w25q_jedec_capacidad(0x14U);          /* miente: dice 1 MB */
    spi_flash_olvida_capacidad();
    cap = spi_flash_capacidad();
    snprintf(t, sizeof t, "%u", (unsigned)cap);
    di("se queda con los 2 MB medidos, no con el MB del codigo",
       cap == MB(2), t);
    di("y no se pasa del chip de verdad", cap <= w25q_tam_actual(), 0);
    snprintf(t, sizeof t, "0x%06X", (unsigned)za_base());
    di("la zona alta cae dentro del chip", za_base() < cap, t);
    w25q_jedec_capacidad(0U);             /* que vuelva a decir la verdad */

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

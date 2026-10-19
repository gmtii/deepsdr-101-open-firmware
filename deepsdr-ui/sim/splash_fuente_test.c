/*
 * EL LECTOR DE LA FUENTE DEL SPLASH, CON FICHEROS ROTOS - 17/10/2026.
 *
 * splash_fuente_carga() construye TRES PUNTEROS a partir de numeros que
 * vienen de un fichero que graba el usuario: donde empiezan los glifos,
 * donde los rangos y donde el bitmap. Si uno de esos numeros miente, lo
 * siguiente que pasa es que gfx2_text_in() lee fuera del buffer, y eso en
 * una radio no se ve como un fallo del BD.BIN: se ve como que la radio se
 * cuelga al encender.
 *
 * Asi que este banco no comprueba que la fuente buena se cargue -eso es la
 * mitad facil- sino que las malas NO. Se parte del fichero que genera
 * tools/fuente_pack.py y se le hace una cosa distinta en cada caso:
 * cambiarle la marca, la version, mentir en los tamaños, poner un rango
 * que apunta fuera de la tabla, truncarlo. Cada una tiene que devolver 0,
 * que es lo que splash.c traduce a "pantalla fija".
 *
 * NO HAY SPI AQUI. El lector recibe quien lee -za_lee_fn- igual que
 * zona_alta_abre(), asi que el banco le pasa una funcion que copia de un
 * BD.BIN que vive en memoria. Es lo que permite probar esto sin radio.
 */

#include "splash_fuente.h"
#include "zona_alta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *g_bd;
static unsigned long  g_bd_n;
static int fallos;

/*
 * EL CHIP QUE DICE QUE HAY. zona_alta.c saca za_base() de la capacidad de
 * la flash, asi que el banco contesta lo que contestaria un W25Q16 de
 * fabrica: 2 MB. No hace falta mas - lo que se prueba aqui es el lector
 * de la fuente, no la geometria, que ya tiene su propio banco.
 */
uint32_t spi_flash_capacidad(void) { return 0x200000UL; }

/* El "chip": BD.BIN colocado donde za_base() lo espera. */
static void lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    /* BD.BIN empieza, byte 0, justo en za_base(): ahi va su directorio.
     * (El primer intento restaba ademas ZA_HUECO y la fuente buena no
     *  cargaba: el hueco ya esta dentro de za_base().) */
    uint32_t off = addr - (uint32_t)za_base();
    uint32_t i;

    for (i = 0U; i < len; i++) {
        uint32_t k = off + i;
        buf[i] = (k < g_bd_n) ? g_bd[k] : 0xFFU;
    }
}

static void di(int bien, const char *que)
{
    printf("    %-58s %s\n", que, bien ? "bien" : "MAL");
    if (!bien) { fallos++; }
}

/* Donde empieza la seccion SPLF dentro del BD.BIN que hay en memoria. */
static unsigned long splf_off(void)
{
    unsigned long i;
    for (i = 0; i + 4 <= g_bd_n; i += 4) {
        if (memcmp(g_bd + i, "SPLF", 4) == 0 && i >= 4096UL) { return i; }
    }
    return 0UL;
}

static const gfx2_font_t *intenta(uint8_t *pozo, uint32_t pozo_n,
                                  gfx2_font_t *f)
{
    return splash_fuente_carga(lee, pozo, pozo_n, f);
}

int main(int argc, char **argv)
{
    static uint8_t pozo[16384];
    gfx2_font_t f;
    unsigned long off;
    unsigned char *limpio;
    FILE *fp;

    if (argc < 2) { printf("  uso: %s BD.BIN\n", argv[0]); return 2; }
    fp = fopen(argv[1], "rb");
    if (fp == 0) { printf("  no puedo abrir %s\n", argv[1]); return 2; }
    fseek(fp, 0, SEEK_END); g_bd_n = (unsigned long)ftell(fp); rewind(fp);
    g_bd = malloc(g_bd_n);
    if (g_bd == 0 || fread(g_bd, 1, g_bd_n, fp) != g_bd_n) { return 2; }
    fclose(fp);
    limpio = malloc(g_bd_n);
    memcpy(limpio, g_bd, g_bd_n);

    printf("\nLA FUENTE DEL SPLASH, LEIDA DE BD.BIN\n\n");
    printf("  1. LA BUENA SE CARGA\n");
    {
        const gfx2_font_t *r = intenta(pozo, (uint32_t)sizeof pozo, &f);
        di(r != 0, "la fuente de un BD.BIN bueno se carga");
        if (r != 0) {
            printf("       %u rangos, alto de linea %u, ascent %u\n",
                   (unsigned)r->n_ranges, (unsigned)r->line_h,
                   (unsigned)r->ascent);
            di(r->n_ranges > 0U && r->line_h > 0U, "trae rangos y alto de linea");
            /* Los punteros tienen que caer DENTRO del pozo. */
            di(r->bitmap >= pozo && r->bitmap < pozo + sizeof pozo,
               "el bitmap apunta dentro de la memoria prestada");
            di((const uint8_t *)r->glyphs >= pozo
               && (const uint8_t *)r->glyphs < pozo + sizeof pozo,
               "y los glifos tambien");
        }
    }

    off = splf_off();
    if (off == 0UL) { printf("\n  no encuentro la seccion SPLF   MAL\n"); return 1; }

    printf("\n  2. LAS ROTAS NO\n");
#define ROMPE(QUE, ACCION) do {                                           \
        memcpy(g_bd, limpio, g_bd_n);                                     \
        { ACCION; }                                                       \
        di(intenta(pozo, (uint32_t)sizeof pozo, &f) == 0, QUE);           \
    } while (0)

    ROMPE("la marca cambiada: no es 'SPLF'", g_bd[off] = 'X');
    ROMPE("una version que no conocemos",    g_bd[off + 4] = 2);
    ROMPE("cero glifos",                     g_bd[off + 8] = 0; g_bd[off + 9] = 0);
    ROMPE("cero rangos",                     g_bd[off + 10] = 0; g_bd[off + 11] = 0);
    ROMPE("un bitmap de cero bytes",
          g_bd[off + 12] = 0; g_bd[off + 13] = 0;
          g_bd[off + 14] = 0; g_bd[off + 15] = 0);
    ROMPE("mas glifos de los que caben en lo que mide",
          g_bd[off + 8] = 0xFF; g_bd[off + 9] = 0x00);
    ROMPE("un numero de glifos absurdo",
          g_bd[off + 8] = 0xFF; g_bd[off + 9] = 0xFF);
    ROMPE("un numero de rangos absurdo",
          g_bd[off + 10] = 0xFF; g_bd[off + 11] = 0xFF);
    ROMPE("un bitmap mas grande de lo que mide el fichero",
          g_bd[off + 15] = 0x10);
    ROMPE("un alto de linea de cero",        g_bd[off + 7] = 0);
    /* El primer rango: lo(2) hi(2) first(2), detras de los glifos. */
    {
        unsigned long n_gl = (unsigned long)g_bd[off + 8]
                           | ((unsigned long)g_bd[off + 9] << 8);
        unsigned long r0 = off + 16UL + n_gl * 12UL;
        ROMPE("un rango cuyo 'first' se sale de la tabla de glifos",
              g_bd[r0 + 4] = 0xF0; g_bd[r0 + 5] = 0x00);
        ROMPE("un rango con hi por debajo de lo",
              g_bd[r0] = 0x50; g_bd[r0 + 1] = 0x30;
              g_bd[r0 + 2] = 0x00; g_bd[r0 + 3] = 0x30);
    }
    memcpy(g_bd, limpio, g_bd_n);

    printf("\n  3. Y CUANDO NO HAY SITIO NI FICHERO\n");
    di(intenta(pozo, 16U, &f) == 0, "un pozo mas pequeño que la fuente");
    di(intenta(0, 4096U, &f) == 0, "sin memoria prestada");
    di(splash_fuente_carga(0, pozo, (uint32_t)sizeof pozo, &f) == 0,
       "sin nadie que sepa leer la flash");
    {
        unsigned long i;
        for (i = 0; i < g_bd_n; i++) { g_bd[i] = 0xFF; }
        di(intenta(pozo, (uint32_t)sizeof pozo, &f) == 0,
           "una flash en blanco, sin directorio");
    }
    memcpy(g_bd, limpio, g_bd_n);

    if (fallos) { printf("\n  *** %d FALLOS ***\n", fallos); return 1; }
    printf("\nTODO CORRECTO (0 fallos)\n");
    return 0;
}

/*
 * UN CHIP MAS PEQUEÑO QUE EL SUELO, Y EL DISCO QUE SE LLEVO POR DELANTE.
 *
 * DE DONDE SALE ESTO - 18/10/2026
 * -------------------------------
 * Con tres sintomas y ninguna otra informacion: el mapa se
 * graba bien, BD.BIN da un error al grabarse, y despues el disco queda
 * corrupto. Los tres salen de la misma linea.
 *
 * spi_flash_capacidad() cruza dos fuentes -la medida por alias y el codigo
 * JEDEC- y se queda con la pequeña, que es lo correcto: anunciar de MENOS
 * desaprovecha sitio, anunciar de MAS rompe el volumen. Y detras tenia
 * esto:
 *
 *     if (tam < CAP_SUELO) { tam = CAP_SUELO; }
 *
 * El suelo estaba pensado para cuando las dos fuentes fallan: ante la
 * nada, contestar los 2 MB que la radio ha tenido siempre. Pero escrito
 * asi tambien SUBIA una medida buena. Con un chip de 1 MB -uno mas
 * pequeño soldado, o un clon que dice la verdad sobre un tamaño menor- la
 * radio anuncia 2 MB, y a partir de ahi:
 *
 *   disco USB      = 2 MB - 1 MB = 1 MB, o sea el chip ENTERO
 *   za_base()      = 0x101000, que en un chip de 1 MB da la vuelta
 *                    y cae en 0x001000: la primera copia de la FAT
 *
 * Las dos guardias de datos_arranca() comparan contra ese mismo numero
 * inflado, asi que las dos pasan.
 *
 * LO QUE SE MIDE AQUI
 * -------------------
 * Se monta el volumen de verdad en un chip de 1 MB simulado a nivel de
 * pin, se guarda el megabyte entero, y se hace EL RITUAL: abrir la copia
 * de BD.BIN a la zona alta y andarla hasta el final. Despues se cuenta
 * cuantos bytes del disco han cambiado. Tienen que ser CERO.
 *
 * Antes del arreglo cambiaban 204.076, el primero en 0x001000. El sector
 * de arranque sobrevive -esta por debajo del hueco-, asi que Windows
 * sigue montando el volumen y lo ve destrozado; y como el fichero que se
 * esta leyendo se machaca a si mismo por el camino, lo copiado no cuadra
 * y la pantalla da el error DESPUES de haber roto el disco.
 *
 * El mapa, con eso puesto, parecia funcionar: son 8 kB en za_mapa(), que
 * da la vuelta a 0x0D0000 -zona de datos- y se verifica leyendo de esa
 * misma direccion. Escribe donde no debe y dice que todo bien. Por eso el
 * issue decia que el mapa iba perfecto.
 *
 * Y SE COMPRUEBA LAS DOS COSAS: que con el chip pequeño se niega, y que
 * con el de 2 MB de siempre SIGUE COPIANDO. Un arreglo que se niegue
 * siempre tambien pasaria la primera mitad.
 */
#include "w25q_sim.h"
#include "spi_flash.h"
#include "disco.h"
#include "zona_alta.h"
#include "formato.h"
#include <stdio.h>
#include <string.h>

#define MB(n)   ((uint32_t)(n) * 1024UL * 1024UL)
#define VOLUMEN MB(1)

static int fallos = 0;
static uint8_t antes[VOLUMEN];

static void di(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

static uint32_t bps(void)   { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
static uint32_t datos(void) { return raiz() + (uint32_t)(g_chip[17] |
                                     (g_chip[18] << 8)) * 32U; }

static void fat_pon(uint32_t c, uint16_t v)
{
    uint32_t o = c + (c / 2U), k;

    for (k = 0U; k < 2U; k++) {
        uint8_t *f = &g_chip[fat1() + k * fatsz() + o];
        uint16_t p = (uint16_t)(f[0] | (f[1] << 8));
        p = (c & 1U) ? (uint16_t)((p & 0x000FU) | (v << 4))
                     : (uint16_t)((p & 0xF000U) | (v & 0x0FFFU));
        f[0] = (uint8_t)(p & 0xFFU); f[1] = (uint8_t)(p >> 8);
    }
}

/* Un BD.BIN seguido en la ranura 1 del raiz, con contenido que depende
 * del sitio: asi un trozo escrito donde no toca no se parece al que le
 * tocaba. */
static uint32_t pon_bd(uint32_t tam)
{
    /* EL CLUSTER NO ES UN SECTOR. Esto daba por hecho que si, porque las
     * dos primeras escenas cargan una imagen con un sector por cluster. El
     * formateador de la radio pone OCHO -4 kB, un borrado- y con esta
     * cuenta la cadena y el tamaño no cuadraban, asi que el fichero no se
     * encontraba y la escena nueva fallaba por culpa del banco. */
    uint32_t cl = (uint32_t)g_chip[13] * bps();
    uint32_t n, i, k;
    uint8_t *e = &g_chip[raiz() + 32U];

    if (cl == 0U) { cl = bps(); }
    n = (tam + cl - 1U) / cl;

    memcpy(e, "BD      BIN", 11);
    e[11] = 0x20U;
    e[26] = 2U; e[27] = 0U;
    e[28] = (uint8_t)(tam & 0xFFU); e[29] = (uint8_t)((tam >> 8) & 0xFFU);
    e[30] = (uint8_t)((tam >> 16) & 0xFFU); e[31] = 0U;
    for (i = 0U; i < n; i++) {
        uint32_t ini = datos() + i * cl;
        for (k = 0U; k < cl; k++) { g_chip[ini + k] = (uint8_t)(i * 31U + k); }
        fat_pon(2U + i, (i + 1U < n) ? (uint16_t)(3U + i) : (uint16_t)0x0FFFU);
    }
    return n;
}

/* Devuelve cuantos bytes del megabyte del volumen han cambiado. */
static uint32_t una_talla(const char *plantilla, uint32_t chip,
                          const char *nombre, int debe_copiar)
{
    FILE *f;
    spi_flash_alta_t a;
    const uint32_t tam = 200U * 1024U;
    uint32_t bloques, cambiados = 0U, primero = 0xFFFFFFFFUL, i;
    int8_t r;
    uint8_t abrio;

    printf("\n  %s\n", nombre);
    f = fopen(plantilla, "rb");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return 0U; }
    w25q_reset();
    w25q_tam(chip);
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido leer %s   MAL\n", plantilla);
        fclose(f); fallos++; return 0U;
    }
    fclose(f);
    spi_flash_olvida_capacidad();
    spi_flash_init();

    (void)pon_bd(tam);
    memcpy(antes, g_chip, VOLUMEN);

    spi_flash_mira_alta(&a);
    printf("    el chip mide %u y la radio dice %u\n",
           (unsigned)chip, (unsigned)spi_flash_capacidad());
    printf("    disco %u   za_base 0x%06X   fs_bytes %u\n",
           (unsigned)(disco_bloques() * DISCO_BLOQUE), (unsigned)ZA_BASE,
           (unsigned)a.fs_bytes);

    bloques = (tam + 4095UL) & ~4095UL;
    abrio = spi_flash_restaura_abre("BD      ", "BIN", ZA_BASE, bloques,
                                    a.fs_bytes, ZA_TOPE);
    if (abrio) {
        do { r = spi_flash_volcado_paso(); } while (r > 0);
    }

    for (i = 0U; i < VOLUMEN; i++) {
        if (g_chip[i] != antes[i]) {
            if (primero == 0xFFFFFFFFUL) { primero = i; }
            cambiados++;
        }
    }
    if (cambiados != 0U) {
        printf("    *** %u bytes del disco cambiados, el primero en 0x%06X ***\n",
               (unsigned)cambiados, (unsigned)primero);
    }
    di(debe_copiar ? "la copia arranca, como tiene que hacer"
                   : "la copia se niega: la zona alta cae en el volumen",
       abrio == (debe_copiar ? 1U : 0U));
    di("y el disco no ha cambiado ni un byte", cambiados == 0U);
    return cambiados;
}

/*
 * LA OTRA MITAD: UN VOLUMEN QUE DICE MEDIR MAS QUE EL DISCO.
 *
 * El caso de arriba es el chip mas pequeño de lo que la radio cree. Este es
 * el simetrico y no necesita ningun fallo nuestro: el volumen formateado
 * mas grande que el disco, que es lo que queda de un formateo hecho cuando
 * la capacidad se media mal -o de un sector 0 tocado-.
 *
 * Y DESDE FUERA SE VE EXACTAMENTE COMO EL ISSUE:
 *
 *   - el mapa se graba perfecto. Grabarlo solo LEE el disco, y geo_lee()
 *     deja leer un volumen asi a proposito (lee=1, vale=0): la condicion
 *     "dice medir mas que el disco" es de ESTE driver y de su borrado por
 *     bloques, no del formato, y del lado de leer hacia daño - ver su
 *     comentario, que lo cuenta entero-;
 *   - BD.BIN da un error, porque datos_arranca() si mira el tamaño
 *     declarado del sistema de ficheros antes de escribir en la zona alta;
 *   - y el disco esta roto para Windows, que es lo que lo empezo todo.
 *
 * Lo que se mide aqui es que el driver conteste esas dos cosas distintas a
 * las dos preguntas distintas, que es lo que permite que el cartel diga
 * cual de las dos averias es en vez de culpar al chip.
 */
static void escena_inflado(const char *plantilla)
{
    FILE *f;
    spi_flash_alta_t a;
    uint32_t disco_b;

    printf("\n  un volumen formateado mas grande que el disco\n");
    f = fopen(plantilla, "rb");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return; }
    w25q_reset();
    w25q_tam(MB(2));
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido leer %s   MAL\n", plantilla);
        fclose(f); fallos++; return;
    }
    fclose(f);
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();
    spi_flash_init();
    spi_flash_mira_alta(&a);

    disco_b = disco_bloques() * DISCO_BLOQUE;
    printf("    el BPB dice %u bytes y el disco tiene %u\n",
           (unsigned)a.fs_bytes, (unsigned)disco_b);
    printf("    geo dice: %s\n", spi_flash_geo_txt());

    di("el volumen se deja LEER (por eso el mapa se graba)",
       spi_flash_geometria_lectura(0, 0, 0, 0) != 0U);
    di("y NO se deja escribir", spi_flash_geometria(0, 0, 0, 0) == 0U);
    di("la guardia de datos_arranca() lo ve: fs_bytes pasa de ZA_BASE",
       ZA_BASE < a.fs_bytes);
    di("y se distingue del chip corto: fs_bytes pasa del disco",
       a.fs_bytes > disco_b);
}

/*
 * LA TERCERA ESCENA: EL CHIP DE 1 MB, DESPUES DE FORMATEAR - 20/10/2026.
 *
 * Las dos de arriba comprueban que no se rompe nada. Esta comprueba que la
 * radio SIRVE PARA ALGO, que es lo que faltaba.
 *
 * Con la region de la zona alta fija en un mega, un chip de 1 MB se
 * quedaba sin disco -cero bloques-, y de ahi salia todo lo que conto el
 * issue: BD.BIN no se puede copiar porque no hay donde dejarlo, el sitio
 * para fotos no se puede leer, el volumen de fabrica dice medir mas que el
 * disco y la pantalla manda formatear... y el formateador contestaba "no
 * se ve la flash" porque disco_bloques() valia cero. Un callejon.
 *
 * Con la region a la mitad del chip, el camino existe y es el normal:
 * formatear deja un volumen de 512 kB, BD.BIN cabe en el disco, y la copia
 * a la zona alta arranca y termina sin tocar un byte del volumen. Eso es
 * lo que se mide aqui, de punta a punta.
 */
static void escena_formatea_chico(void)
{
    spi_flash_alta_t a;
    const uint32_t tam = 200U * 1024U;
    uint32_t disco_b, bloques, cambiados = 0U, i;
    int8_t r;
    uint8_t abrio;
    formato_r_t fr;

    printf("\n  un chip de 1 MB, formateado por la propia radio\n");
    w25q_reset();
    w25q_tam(MB(1));
    memset(g_chip, 0xFF, MB(1));
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();
    spi_flash_init();

    disco_b = disco_bloques() * DISCO_BLOQUE;
    printf("    disco %u bytes   za_base 0x%06X   techo de datos 0x%06X\n",
           (unsigned)disco_b, (unsigned)ZA_BASE, (unsigned)ZA_TOPE_DATOS);
    di("el disco ya no es de cero bloques", disco_bloques() != 0U);

    fr = formato_haz();
    printf("    formatear dice: %s\n", formato_porque_txt(fr));
    di("formatear funciona (y no 'no se ve la flash')", fr == FORMATO_OK);

    spi_flash_geo_olvida();
    spi_flash_mira_alta(&a);
    printf("    el volumen nuevo mide %u y el disco %u\n",
           (unsigned)a.fs_bytes, (unsigned)disco_b);
    di("el volumen cabe en el disco", a.fs_bytes <= disco_b && a.fs_bytes != 0U);
    di("y la zona alta queda por encima del volumen", ZA_BASE >= a.fs_bytes);

    (void)pon_bd(tam);
    memcpy(antes, g_chip, disco_b);

    bloques = (tam + 4095UL) & ~4095UL;
    abrio = spi_flash_restaura_abre("BD      ", "BIN", ZA_BASE, bloques,
                                    a.fs_bytes, ZA_TOPE_DATOS);
    if (abrio) {
        do { r = spi_flash_volcado_paso(); } while (r > 0);
    }
    di("un BD.BIN de 200 kB se copia a la zona alta", abrio == 1U);

    for (i = 0U; i < disco_b; i++) {
        if (g_chip[i] != antes[i]) { cambiados++; }
    }
    di("y el disco no ha cambiado ni un byte", cambiados == 0U);

    /* Y lo que NO tiene que caber: el hueco del mapa es intocable. */
    abrio = spi_flash_restaura_abre("BD      ", "BIN", ZA_BASE,
                                    ZA_TOPE_DATOS - ZA_BASE + 4096UL,
                                    a.fs_bytes, ZA_TOPE_DATOS);
    di("uno que pasara del techo de datos se niega", abrio == 0U);
    printf("    y el aviso dice: %s\n", spi_flash_volcado_porque_txt());
    di("el aviso dice cuanto cabe, no 'destino no permitido'",
       spi_flash_volcado_porque() == SPI_VOL_NO_CABE);
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    const char *inflado   = (argc > 2) ? argv[2] : "out/fat12_inflado.img";

    printf("\n=== LA ZONA ALTA NO PUEDE CAER DENTRO DEL DISCO ===\n");

    /* El chip de siempre: tiene que seguir copiando. */
    (void)una_talla(plantilla, MB(2), "W25Q16, el de fabrica (2 MB)", 1);

    /* Y el que lo descubrio. */
    (void)una_talla(plantilla, MB(1), "un chip de 1 MB, por debajo del suelo", 0);

    escena_inflado(inflado);
    escena_formatea_chico();

    printf("\n  %s\n", fallos ? "" : "TODO CORRECTO");
    if (fallos) { printf("  *** %d ***\n", fallos); }
    return fallos ? 1 : 0;
}

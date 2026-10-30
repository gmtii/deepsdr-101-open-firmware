/*
 * ======================================================================
 * APAGAR LA RADIO EN MITAD DE UN GUARDADO.
 * ======================================================================
 *
 * EL CASO QUE LO TRAJO, con la imagen del disco de un usuario delante.
 * Apago la radio mientras guardaba y al encender tenia los ajustes de
 * fabrica y UN solo fichero en el disco, con 2,26 MB marcados como
 * ocupados. La imagen lo conto sin margen:
 *
 *     directorio raiz: 256 entradas en DOS bloques de borrado de 4 kB
 *     bloque 0:  3.968 de 4.096 bytes a 0xFF   <- borrado, nunca regrabado
 *     bloque 1:  intacto
 *
 * Para cambiar UNA entrada hay que borrar los 4 kB en los que vive, y en
 * esos 4 kB caben 128 entradas de OTROS ficheros. El corte cayo en esa
 * ventana y se llevo 124 nombres.
 *
 * NINGUN BANCO PODIA VER ESTO, porque ninguno sabia apagar la radio. Ahora
 * el chip simulado si: w25q_corta_en(k) hace que la operacion de escritura
 * numero k no llegue a volver, con el chip exactamente como lo deja un
 * corte ahi.
 *
 * LO QUE SE EXIGE, y es lo unico que importa:
 *
 *   1. que guardar los ajustes no toque el directorio cuando la entrada no
 *      cambia -ahi es donde el usuario perdio lo suyo-;
 *   2. que cortando la corriente en CUALQUIER punto de una operacion que
 *      SI tiene que tocarlo, al arrancar no falte ni un nombre de los que
 *      habia.
 *
 * Lo segundo se prueba por fuerza bruta: se corta en la operacion 1, en la
 * 2, en la 3... y en cada una se arranca otra vez y se cuentan las
 * entradas.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define VOLUMEN   (7U * 1024U * 1024U)   /* como el disco de la radio */
#define CHIP      (8U * 1024U * 1024U)

static int fallos = 0;
static void di(int ok, const char *que, const char *det)
{
    printf("    %-56s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* --- leer el directorio raiz del chip simulado ------------------------- */

typedef struct { char n[12]; } nombre_t;

static uint32_t raiz_abs, raiz_bytes;

static void geo_pon(void)
{
    uint32_t fat1, raiz, datos, clusters;
    if (!spi_flash_geometria(&fat1, &raiz, &datos, &clusters)) {
        printf("    el volumen no es escribible para el driver\n");
        exit(1);
    }
    raiz_abs = raiz * 512U;
    /* raiz_ent del BPB, en el sector 0 */
    raiz_bytes = ((uint32_t)g_chip[17] | ((uint32_t)g_chip[18] << 8)) * 32U;
}

/* Cuantas entradas vivas hay, y cuales. */
static uint32_t lista(nombre_t *out, uint32_t max)
{
    uint32_t i, n = 0U;

    for (i = 0U; i + 32U <= raiz_bytes; i += 32U) {
        const uint8_t *e = &g_chip[raiz_abs + i];
        uint32_t k;

        if (e[0] == 0x00U || e[0] == 0xE5U || e[0] == 0xFFU) { continue; }
        if (e[11] == 0x0FU) { continue; }       /* trozo de nombre largo */
        if (n < max) {
            for (k = 0U; k < 11U; k++) { out[n].n[k] = (char)e[k]; }
            out[n].n[11] = '\0';
        }
        n++;
    }
    return n;
}

static uint32_t bloques_en_blanco(void)
{
    uint32_t b, n = 0U;

    for (b = 0U; b < raiz_bytes; b += 4096U) {
        uint32_t i, ff = 0U;
        for (i = 0U; i < 4096U && (b + i) < raiz_bytes; i++) {
            if (g_chip[raiz_abs + b + i] == 0xFFU) { ff++; }
        }
        if (ff > 4000U) { n++; }
    }
    return n;
}

/* --- el volumen de partida -------------------------------------------- */

static uint8_t g_patron[CHIP];

static void monta(const char *plantilla)
{
    FILE *f = fopen(plantilla, "rb");
    size_t n;

    if (!f) { printf("    falta %s\n", plantilla); exit(1); }
    memset(g_chip, 0xFF, CHIP);
    n = fread(g_chip, 1, VOLUMEN, f);
    fclose(f);
    if (n != VOLUMEN) { printf("    %s no mide 7 MB\n", plantilla); exit(1); }
}

/* Llena el primer bloque del directorio de entradas, como un disco con
 * historia: eso es lo que el apagon se llevaba por delante. */
static void puebla(uint32_t cuantos)
{
    uint8_t dato[64];
    uint32_t i;

    memset(dato, 0x5A, sizeof dato);
    for (i = 0U; i < cuantos; i++) {
        char n8[8];
        n8[0] = 'F'; n8[1] = 'I'; n8[2] = 'C';
        n8[3] = (char)('0' + (i / 100U) % 10U);
        n8[4] = (char)('0' + (i / 10U) % 10U);
        n8[5] = (char)('0' + i % 10U);
        n8[6] = ' '; n8[7] = ' ';
        if (!spi_flash_write_or_update_file(n8, "BIN", dato, sizeof dato)) {
            printf("    no se ha podido crear el fichero %u\n", i);
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_7m.img";
    nombre_t antes[300], ahora[300];
    uint32_t n_antes, n_ahora;
    uint32_t borrados_ini, k, peor = 0U, reparados = 0U;
    uint8_t  dato[64];
    uint32_t resp_base = 0U;

    printf("\n== APAGAR LA RADIO EN MITAD DE UN GUARDADO ==\n\n");

    w25q_reset();
    w25q_tam(CHIP);
    monta(plantilla);
    spi_flash_init();
    spi_flash_geo_olvida();
    geo_pon();

    puebla(60U);
    n_antes = lista(antes, 300U);
    printf("    el disco de partida tiene %u entradas en %u bytes de raiz\n",
           n_antes, raiz_bytes);
    di(n_antes >= 60U, "el banco monta un disco con historia, no uno vacio", 0);

    /* ------------------------------------------------------------------
     * 1. GUARDAR LOS AJUSTES NO TOCA EL DIRECTORIO.
     *    Es el camino que corre cada dos por tres y el que le costo los
     *    124 nombres al usuario. Se mide en BORRADOS del chip: si el
     *    bloque del directorio se borra, el contador sube.
     * ------------------------------------------------------------------ */
    memset(dato, 0x11, sizeof dato);
    if (!spi_flash_write_or_update_file("CONFIG  ", "CSV", dato, sizeof dato)) {
        printf("    no se ha podido crear CONFIG.CSV\n");
        return 1;
    }
    borrados_ini = g_borrados;
    {
        uint32_t v;
        char det[80];
        for (v = 0U; v < 10U; v++) {
            memset(dato, (int)(0x20U + v), sizeof dato);
            (void)spi_flash_write_or_update_file("CONFIG  ", "CSV",
                                                 dato, sizeof dato);
        }
        snprintf(det, sizeof det, "(%u borrados de 4 kB en 10 guardados)",
                 g_borrados - borrados_ini);
        /* Diez guardados: diez borrados del bloque de DATOS y ni uno del
         * directorio. Antes del 30/10/2026 eran veinte. */
        di((g_borrados - borrados_ini) <= 10U,
           "diez guardados de ajustes, ni un borrado del directorio", det);
    }
    /*
     * Y LA RED, PUESTA ANTES DE EMPEZAR. RESPALDO.BIN lo crea el arranque
     * -ver respaldo_donde(): no se puede crear desde dentro de una
     * escritura, que fue el primer intento y se llevo por delante al banco
     * de la carta de fax-. Asi que aqui se arranca una vez, como hace la
     * radio, y a partir de ahi ya hay donde guardar la copia.
     */
    (void)spi_flash_respaldo_repara();
    {
        /* Donde ha quedado RESPALDO.BIN, leido del directorio. */
        uint32_t i;
        resp_base = 0U;
        for (i = 0U; i + 32U <= raiz_bytes; i += 32U) {
            const uint8_t *e = &g_chip[raiz_abs + i];
            if (memcmp(e, "RESPALDOBIN", 11) == 0) {
                uint32_t cl = (uint32_t)e[26] | ((uint32_t)e[27] << 8);
                uint32_t fat1, raiz, datos, clus;
                (void)spi_flash_geometria(&fat1, &raiz, &datos, &clus);
                resp_base = (datos + (cl - 2U) * 8U) * 512U;
            }
        }
        printf("    RESPALDO.BIN en %06X\n", (unsigned)resp_base);
    }

    memcpy(g_patron, g_chip, CHIP);
    n_antes = lista(antes, 300U);

    /* ------------------------------------------------------------------
     * 2. Y AHORA EL APAGON, EN CADA PASO.
     *    Se crea un fichero nuevo -que SI tiene que tocar el directorio-
     *    cortando la corriente en la operacion 1, en la 2, ... y en cada
     *    una se arranca otra vez y se cuentan los nombres.
     * ------------------------------------------------------------------ */
    printf("\n    cortando la corriente en cada operacion de escritura:\n");
    for (k = 1U; k <= 120U; k++) {
        uint32_t i, j, faltan = 0U;
        uint32_t rep_antes = reparados;

        memcpy(g_chip, g_patron, CHIP);
        spi_flash_init();
        spi_flash_geo_olvida();

        if (setjmp(*w25q_salto()) == 0) {
            w25q_corta_en(k);
            memset(dato, 0x77, sizeof dato);
            (void)spi_flash_write_or_update_file("NUEVO   ", "BIN",
                                                 dato, sizeof dato);
            w25q_corta_en(0U);          /* termino sin que hiciera falta */
        }
        w25q_corta_en(0U);

        /* El arranque siguiente: lo PRIMERO que hace la radio con el
         * disco. Ver su llamada en main(). */
        spi_flash_init();
        spi_flash_geo_olvida();
        if (spi_flash_respaldo_repara()) { reparados++; }

        n_ahora = lista(ahora, 300U);
        for (i = 0U; i < n_antes; i++) {
            uint8_t hay = 0U;
            for (j = 0U; j < n_ahora; j++) {
                if (memcmp(antes[i].n, ahora[j].n, 11) == 0) { hay = 1U; break; }
            }
            if (!hay) { faltan++; }
        }
        if (faltan > peor) { peor = faltan; }
        if (faltan != 0U) {
            /* Que dice la copia, para saber por que no se ha reparado. */
            uint32_t b = resp_base, q;
            printf("        raiz_abs=%06X  ", (unsigned)raiz_abs);
            for (q = 0U; q < raiz_bytes; q += 4096U) {
                uint32_t z, ff = 0U;
                for (z = 0U; z < 4096U; z++) {
                    if (g_chip[raiz_abs + q + z] == 0xFFU) { ff++; }
                }
                printf("bloque %06X: %u FF   ", (unsigned)(raiz_abs + q), ff);
            }
            printf("\n");
            {
                uint32_t sm = 0x1234ABCDUL, z;
                uint32_t guardada;
                for (z = 0U; z < 4096U; z++) {
                    sm = (sm << 1) | (sm >> 31);
                    sm += (uint32_t)g_chip[b + z];
                }
                guardada = (uint32_t)g_chip[b+4096+12]
                         | ((uint32_t)g_chip[b+4096+13] << 8)
                         | ((uint32_t)g_chip[b+4096+14] << 16)
                         | ((uint32_t)g_chip[b+4096+15] << 24);
                printf("        suma del payload %08X, la cabecera dice %08X  %s\n",
                       (unsigned)sm, (unsigned)guardada,
                       (sm == guardada) ? "CUADRAN" : "NO CUADRAN");
            }
            printf("        copia: magia %c%c%c%c%c%c%c estado %02X destino %06X\n",
                   g_chip[b+4096], g_chip[b+4097], g_chip[b+4098], g_chip[b+4099],
                   g_chip[b+4100], g_chip[b+4101], g_chip[b+4102],
                   g_chip[b+4096+16],
                   (unsigned)(g_chip[b+4096+8] | (g_chip[b+4096+9]<<8)
                              | (g_chip[b+4096+10]<<16)));
        }
        if (faltan != 0U || reparados != rep_antes || (k % 20U) == 0U) {
            printf("      corte en la operacion %3u -> %3u entradas, faltan %u%s%s\n",
                   k, n_ahora, faltan,
                   (reparados != rep_antes) ? "   REPARADO" : "",
                   bloques_en_blanco() ? "   (un bloque del raiz en blanco)" : "");
        }
    }

    {
        char det[80];
        snprintf(det, sizeof det, "(lo peor fueron %u; se reparo %u vez/veces)",
                 peor, reparados);
        di(peor == 0U,
           "ningun corte pierde una sola entrada del directorio", det);
    }

    printf("\n  %s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * QUE LA CARTA SE RECOLOQUE HACIA EL LADO QUE ES.
 *
 * El giro de una carta ya grabada se puede escribir al reves sin que se
 * note al leer el codigo: dst[i] = src[(c+i)%a] y dst[(i+c)%a] = src[i]
 * son las dos de una linea y las dos "parecen" bien. La diferencia solo se
 * ve en la imagen, y cuando se ve ya esta el fichero reescrito encima del
 * bueno.
 *
 * Asi que esto no comprueba la formula: comprueba LA IMAGEN. Se graba una
 * carta con una marca que no deja lugar a dudas -una barra negra pegada a
 * un borde-, se recoloca, se lee el fichero DEL CHIP y se mira donde quedo
 * la barra. Y se dibuja en ASCII para que se pueda ver sin creerse a nadie.
 *
 * Los tres casos del dibujo:
 *
 *   la linea cae a la DERECHA  -> lo de la derecha se pega a la izquierda
 *   la linea cae a la IZQUIERDA-> lo de la izquierda se va al final
 *   la linea en 0              -> no se toca el fichero
 *
 * Y DOS VOLUMENES, no uno: el fichero de una carta crece a tramos de 64 kB
 * (spi_flash_amplia), asi que en un volumen repartido NO es seguido. Un
 * giro que suponga que el bloque 100 esta pegado al 99 escribe 4 kB encima
 * del vecino, que es la familia de averia que ya se llevo por delante un
 * CONFIG.CSV. El segundo volumen va troceado a proposito y ademas se
 * comprueba que el vecino siga entero al acabar.
 *
 * Debajo hay un W25Q16 simulado a nivel de pin (sim/w25q_sim.c), o sea que
 * el que gira es User/wfx_recoloca.c de verdad, con User/spi_flash.c de
 * verdad.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "w25q_sim.h"
#include "spi_flash.h"
#include "bmp.h"
#include "wefax.h"
#include "wfx_recoloca.h"

/*
 * *** LOS DOS QUE NO SON DE AQUI, DE DONDE SON - 21/10/2026. ***
 *
 * ANCHO y BLOQUE estaban escritos a mano -756 y 4096- cuando los dos viven
 * en una cabecera del firmware. Un banco con los numeros copiados sigue
 * dando verde el dia que el firmware cambia uno: deja de probar el
 * firmware y se prueba a si mismo.
 *
 * Es el mismo fallo que ya salio hoy dos veces: el tope de ficheros de
 * sim/fax_guarda.c, que era una copia del valor del cerrojo, y su modelo de
 * fax_poll(), que era una copia del firmware de anteayer.
 *
 * ALTO si es de aqui: es cuantos renglones decide escribir ESTE banco.
 */
#define VOLUMEN   (1024U * 1024U)
#define ANCHO     WEFAX_ANCHO
#define ALTO      40U
#define BLOQUE    WFX_REC_BLOQUE

static uint8_t s_buf[BLOQUE];
static uint8_t s_img[ALTO][ANCHO];
static uint8_t s_lee[ALTO][ANCHO];
static int     s_fallos;

static void mal(const char *q) { printf("    %-54s MAL\n", q); s_fallos++; }
static void bien(const char *q) { printf("    %-54s bien\n", q); }

/* --- el volumen ------------------------------------------------------ */

static int monta(const char *plantilla)
{
    FILE *f = fopen(plantilla, "rb");

    w25q_reset();
    if (!f) { printf("  falta %s\n", plantilla); return 0; }
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  %s no mide 1 MB\n", plantilla); fclose(f); return 0;
    }
    fclose(f);
    g_intocable_ini = VOLUMEN;
    g_intocable_fin = 2U * 1024U * 1024U;
    spi_flash_init();
    return 1;
}

/* Deja un fichero de relleno para partir el volumen, como hace el PC. */
static void pone(const char *n11, uint32_t bytes)
{
    char n8[8], e3[3];
    uint32_t addr = 0UL, dados = 0UL, hechos = 0UL;
    spi_anade_r_t porque;

    memcpy(n8, n11, 8); memcpy(e3, n11 + 8, 3);
    while (hechos * BLOQUE < bytes) {
        uint32_t i;
        if (!spi_flash_amplia(n8, e3, 1U, &addr, &dados, &porque)) { return; }
        for (i = 0UL; i < dados; i++) {
            memset(s_buf, (int)(0xA0U + (hechos & 0x0FU)), BLOQUE);
            spi_flash_write_block_4k(addr + i * BLOQUE, s_buf);
            hechos++;
        }
    }
}

/* --- la carta -------------------------------------------------------- */

/*
 * El contenido: un valor distinto por punto -depende del renglon Y de la
 * columna- para que un renglon cambiado de sitio tambien se vea, no solo
 * una columna. Encima, una barra negra de 50 puntos en `barra`, que es la
 * marca que se mira en el dibujo.
 */
static void pinta(uint32_t barra)
{
    uint32_t y, x;

    for (y = 0U; y < ALTO; y++) {
        for (x = 0U; x < ANCHO; x++) {
            uint32_t v = ((y * 7U) + (x * 3U)) % 251U;
            if (v < 16U) { v = 16U; }              /* que no se confunda con la barra */
            s_img[y][x] = (uint8_t)v;
        }
        for (x = 0U; x < 50U; x++) {
            s_img[y][(barra + x) % ANCHO] = 0U;    /* negro */
        }
    }
}

static int graba(const char *n8, const char *e3)
{
    uint32_t addr = 0UL, dados = 0UL, quedan, usado;
    uint32_t nb, b = 0UL, p = 0UL;
    spi_anade_r_t porque;
    static uint8_t plano[ALTO * ANCHO];
    uint32_t y;

    nb = 1UL + ((ALTO * ANCHO) + BLOQUE - 1UL) / BLOQUE;
    quedan = 0UL;
    for (y = 0U; y < ALTO; y++) { memcpy(&plano[y * ANCHO], s_img[y], ANCHO); }

    while (b < nb) {
        uint32_t i;
        if (quedan == 0UL) {
            if (!spi_flash_amplia(n8, e3, 16U, &addr, &dados, &porque)) {
                printf("  no se pudo reservar la carta\n"); return 0;
            }
            quedan = dados; i = 0UL;
        }
        i = dados - quedan;
        memset(s_buf, 0xFF, BLOQUE);
        if (b == 0UL) {
            (void)bmp_cabecera(s_buf, (uint16_t)ANCHO, (uint16_t)ALTO, BMP_8);
        } else {
            uint32_t n = (ALTO * ANCHO) - p;
            if (n > BLOQUE) { n = BLOQUE; }
            memcpy(s_buf, &plano[p], n);
            p += n;
        }
        spi_flash_write_block_4k(addr + i * BLOQUE, s_buf);
        quedan--; b++;
    }
    usado = BLOQUE + ALTO * ANCHO;
    (void)spi_flash_recorta_cadena(n8, e3, usado);
    return 1;
}

static int relee(const char *n8, const char *e3)
{
    spi_bloques_t it;
    uint32_t a, b = 0UL, p = 0UL;
    static uint8_t plano[ALTO * ANCHO];
    uint32_t y;

    if (!spi_flash_bloques_abre(n8, e3, &it)) { return 0; }
    while (spi_flash_bloques_sig(&it, &a)) {
        spi_flash_read(a, s_buf, BLOQUE);
        if (b > 0UL) {
            uint32_t n = (ALTO * ANCHO) - p;
            if (n > BLOQUE) { n = BLOQUE; }
            if (n == 0UL) { break; }
            memcpy(&plano[p], s_buf, n);
            p += n;
        }
        b++;
    }
    if (p != (ALTO * ANCHO)) { return 0; }
    for (y = 0U; y < ALTO; y++) { memcpy(s_lee[y], &plano[y * ANCHO], ANCHO); }
    return 1;
}

static void corre(uint32_t corte)
{
    uint32_t vueltas = 0UL;

    if (!wfx_recoloca_empieza("WFAX001", "BMP", (uint16_t)ANCHO, (uint16_t)ALTO,
                              (uint16_t)corte, s_buf)) {
        printf("    no arranca el recolocado (corte %u)\n", corte);
        s_fallos++;
        return;
    }
    while (wfx_recoloca_paso() == WFX_REC_ANDA) {
        if (++vueltas > 10000UL) { printf("    no termina\n"); s_fallos++; return; }
    }
    if (wfx_recoloca_estado() != WFX_REC_BIEN) {
        printf("    acabo MAL: %s\n", wfx_recoloca_porque() ? wfx_recoloca_porque() : "?");
        s_fallos++;
    }
}

/* --- el dibujo ------------------------------------------------------- */

/* Un renglon en 63 columnas: '#' si hay algo de barra negra en ese tramo. */
static void dibuja(const char *rotulo, const uint8_t *fila)
{
    char l[64];
    uint32_t i;

    for (i = 0U; i < 63U; i++) {
        uint32_t x0 = (i * ANCHO) / 63U, x1 = ((i + 1U) * ANCHO) / 63U, x;
        char c = '.';
        for (x = x0; x < x1; x++) { if (fila[x] == 0U) { c = '#'; break; } }
        l[i] = c;
    }
    l[63] = '\0';
    printf("    %-14s |%s|\n", rotulo, l);
}

static uint32_t donde(const uint8_t *fila)
{
    uint32_t x;
    for (x = 0U; x < ANCHO; x++) { if (fila[x] == 0U) { return x; } }
    return ANCHO;
}

/* --- un caso --------------------------------------------------------- */

static void caso(const char *titulo, const char *plantilla,
                 uint32_t barra, uint32_t corte, int mira_vecino)
{
    uint32_t y, x, malos = 0UL;

    printf("\n  %s\n", titulo);
    if (!monta(plantilla)) { s_fallos++; return; }
    if (mira_vecino) {
        pone("RELLENO BIN", 160U * 1024U);
        pone("VECINO  BIN", 32U * 1024U);
        /* Un hueco en medio para que la carta NO pueda ser seguida. */
        (void)spi_flash_fichero_borra("RELLENO ", "BIN");
    }

    pinta(barra);
    if (!graba("WFAX001", "BMP")) { s_fallos++; return; }

    printf("    la barra negra entra en la columna %u\n", barra);
    dibuja("antes", s_img[0]);

    corre(corte);

    if (!relee("WFAX001", "BMP")) { mal("el fichero se puede releer entero"); return; }
    dibuja("despues", s_lee[0]);
    printf("    la linea estaba en la columna %u; la barra sale en la %u\n",
           corte, donde(s_lee[0]));

    /* El punto a punto: cada renglon girado como dice el dibujo. */
    for (y = 0U; y < ALTO; y++) {
        for (x = 0U; x < ANCHO; x++) {
            if (s_lee[y][x] != s_img[y][(corte + x) % ANCHO]) { malos++; }
        }
    }
    if (malos == 0UL) { bien("cada punto esta donde tiene que estar"); }
    else { printf("    %u puntos fuera de sitio                            MAL\n", malos); s_fallos++; }

    if (donde(s_lee[0]) != 0U) {
        mal("la barra tiene que quedar pegada al borde izquierdo");
    } else {
        bien("la barra queda pegada al borde izquierdo");
    }

    if (mira_vecino) {
        uint32_t a, n = 0UL, raros = 0UL;
        spi_bloques_t it;
        if (!spi_flash_bloques_abre("VECINO  ", "BIN", &it)) {
            mal("el vecino sigue existiendo");
        } else {
            while (spi_flash_bloques_sig(&it, &a)) {
                uint32_t i;
                spi_flash_read(a, s_buf, BLOQUE);
                for (i = 0UL; i < BLOQUE; i++) {
                    if ((s_buf[i] & 0xF0U) != 0xA0U) { raros++; }
                }
                n++;
            }
            if (n == 8UL && raros == 0UL) { bien("y el vecino sigue entero"); }
            else { printf("    el vecino tiene %u bloques y %u bytes raros   MAL\n",
                          n, raros); s_fallos++; }
        }
    }
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";

    printf("\n=== Recolocar una carta de fax ya grabada ===\n");

    caso("la linea cae a la DERECHA: lo de la derecha se pega a la izquierda",
         plantilla, 700U, 700U, 0);
    caso("la linea cae a la IZQUIERDA: lo de la izquierda se va al final",
         plantilla, 100U, 100U, 0);
    caso("y en un volumen repartido, con un vecino al lado",
         plantilla, 400U, 400U, 1);

    /* El corte a cero no es un giro de 0: es no tocar el fichero. */
    printf("\n  la linea en el borde: no se toca el fichero\n");
    if (monta(plantilla)) {
        pinta(300U);
        if (graba("WFAX001", "BMP")) {
            if (wfx_recoloca_empieza("WFAX001", "BMP", ANCHO, ALTO, 0U, s_buf)) {
                mal("con la linea en 0 no se empieza nada");
            } else {
                bien("con la linea en 0 no se empieza nada");
            }
        }
    }

    /* Y una altura mentida: girar 4 kB de lo que sea encima de un fichero. */
    printf("\n  y si le mienten en la altura\n");
    if (wfx_recoloca_empieza("WFAX001", "BMP", ANCHO, (uint16_t)(ALTO * 4U), 100U, s_buf)) {
        mal("una altura que no cabe en el fichero se rechaza");
    } else {
        bien("una altura que no cabe en el fichero se rechaza");
    }

    printf("\n");
    if (s_fallos == 0) { printf("TODO CORRECTO (0 fallos)\n\n"); return 0; }
    printf("%d FALLO(S)\n\n", s_fallos);
    return 1;
}

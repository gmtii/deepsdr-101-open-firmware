/*
 * QUE UN .map NUEVO NO SE ROMPA EN UN FIRMWARE VIEJO - 14/10/2026.
 *
 * ESTE ES EL BANCO QUE FALTABA, y por eso se escribe con el fallo todavia
 * fresco en vez de despues.
 *
 * Los anillos de tierra se pusieron primero en el BIT 14 de la cuenta de
 * puntos de cada trazo, con este razonamiento escrito en el codigo: "un
 * .map viejo no trae este bit y entonces la radio dibuja como siempre, por
 * eso no hubo que subir la version del formato". El razonamiento mira en
 * una sola direccion. La otra -un .map NUEVO leido por el firmware VIEJO-
 * no la miro nadie, y ahi el lector de entonces enmascaraba 0x7FFF: el bit
 * 14 se le colaba DENTRO de la cuenta, el primer anillo se tragaba el
 * resto de la tabla y el mapa salia como un garabato de un solo trazo.
 * Medido sobre los cien ficheros: 51 rotos, en silencio, sin reventar.
 *
 * Asi que aqui se recorre cada .map DOS VECES:
 *
 *   - con el lector de verdad, User/mapa_bin.c, el que corre en la radio;
 *   - con un lector de VERSION 2 escrito aparte en este fichero, que es lo
 *     que hacia el firmware anterior: enmascarar 0x7FFF y parar en el cero.
 *
 * Y se exige que los dos saquen LOS MISMOS TRAZOS Y LOS MISMOS PUNTOS. Si
 * alguien vuelve a meter informacion dentro de la cuenta, esto se cae.
 *
 * El lector v2 se copia a proposito en vez de llamar al de verdad: un
 * banco que mide el mismo codigo que prueba no mide nada. Son veinte
 * lineas y NO se tocan cuando cambie mapa_bin.c, porque representan un
 * firmware que ya esta instalado en radios de gente y no se va a actualizar
 * solo.
 */
#include "mapa_bin.h"
#include "palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define CAB     60U
#define NIV_CAB 16U

static int fallos = 0;
static void comprueba(int cond, const char *que, const char *como)
{
    printf("    %-46s %s  %s\n", que, cond ? "ok " : "MAL", como ? como : "");
    if (!cond) { fallos++; }
}

static uint32_t le16v(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32v(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- EL LECTOR DE VERSION 2, tal cual lo hacia el firmware viejo ---- */
static void recorre_v2(const uint8_t *b, uint32_t tam,
                       unsigned long *trazos, unsigned long *puntos,
                       int *desborde)
{
    uint32_t p = 0U;
    *trazos = 0UL; *puntos = 0UL; *desborde = 0;
    for (;;) {
        uint32_t n, i;
        if (p + 2U > tam) { *desborde = 1; return; }
        n = le16v(&b[p]); p += 2U;
        if (n == 0U) { return; }          /* el cero cierra la tabla */
        n &= 0x7FFFU;                     /* el bit 15 era la frontera */
        (*trazos)++;
        if (p + 4U > tam) { *desborde = 1; return; }
        p += 4U; (*puntos)++;             /* el primero va absoluto */
        for (i = 1U; i < n; i++) {
            if (p + 1U > tam) { *desborde = 1; return; }
            if (b[p] == 0x80U) {
                if (p + 5U > tam) { *desborde = 1; return; }
                p += 5U;
            } else {
                if (p + 2U > tam) { *desborde = 1; return; }
                p += 2U;
            }
            (*puntos)++;
        }
    }
}

/* ---- Y EL DE VERDAD, el que corre en la radio ---- */
static void recorre_real(unsigned long *trazos, unsigned long *puntos,
                         unsigned long *anillos)
{
    mapa_bin_iter_t it;
    int32_t lo, la;
    *trazos = 0UL; *puntos = 0UL; *anillos = 0UL;
    mapa_bin_primero(&it);
    while (mapa_bin_trazo(&it)) {
        (*trazos)++;
        if (it.anillo) { (*anillos)++; }
        while (mapa_bin_punto(&it, &lo, &la)) { (*puntos)++; }
    }
}

static uint8_t g_buf[256U * 1024U];
static const char *g_con_anillos;

/* Devuelve 0 si los dos lectores coinciden. */
static int un_mapa(const char *ruta, int ruidoso,
                   unsigned long *n_anillos, uint8_t *estilo)
{
    FILE *f = fopen(ruta, "rb");
    size_t n;
    uint32_t off, tam;
    unsigned long t1, p1, t2, p2, an;
    int desb;

    if (!f) { printf("  no se abre %s\n", ruta); fallos++; return 1; }
    n = fread(g_buf, 1, sizeof g_buf, f);
    fclose(f);

    if (mapa_bin_pon(g_buf, (uint32_t)n) != MAPA_BIN_OK) {
        printf("  el lector de verdad no acepta %s\n", ruta);
        fallos++; return 1;
    }
    off = le32v(&g_buf[CAB + 4U]);
    tam = le32v(&g_buf[CAB + 8U]);

    recorre_real(&t1, &p1, &an);
    recorre_v2(&g_buf[off], tam, &t2, &p2, &desb);
    if (n_anillos) { *n_anillos = an; }
    if (estilo)    { *estilo = mapa_bin_estilo(); }

    if (ruidoso) {
        printf("      de verdad: %4lu trazos, %5lu puntos  (%lu anillos)\n", t1, p1, an);
        printf("      version 2: %4lu trazos, %5lu puntos  (desborde %d)\n", t2, p2, desb);
    }
    return (t1 != t2 || p1 != p2 || desb) ? 1 : 0;
}

int main(int argc, char **argv)
{
    int i, rotos = 0;
    unsigned long an;
    uint8_t est;

    if (argc < 2) { printf("uso: mapacompat <ficheros .map>\n"); return 2; }

    printf("\nUN .map NUEVO EN UN FIRMWARE VIEJO\n");
    printf("==================================\n\n");

    printf("  1. los dos lectores tienen que ver lo mismo\n");
    for (i = 1; i < argc; i++) {
        if (un_mapa(argv[i], 0, &an, &est)) { rotos++; }
    }
    {
        char d[80];
        snprintf(d, sizeof d, "%d de %d ficheros", argc - 1 - rotos, argc - 1);
        comprueba(rotos == 0, "el lector de version 2 saca lo mismo", d);
    }

    /* Uno QUE RELLENE de verdad, no el primero de la lista: en orden
     * alfabetico el primero suele ser de contorno y la linea diria "uno
     * que rellena" ensenando cero anillos. */
    {
        int cual = 1;
        for (i = 1; i < argc; i++) {
            if (!un_mapa(argv[i], 0, &an, &est) && an > 0UL) { cual = i; g_con_anillos = argv[i]; break; }
        }
        printf("\n  2. y con detalle, uno que rellena: %s\n", argv[cual]);
        (void)un_mapa(argv[cual], 1, &an, &est);
        comprueba(an > 0UL, "trae anillos de tierra", "");
    }

    /*
     * 3. Y QUE ESTO SEPA FALLAR. Un banco que no se ha visto fallar no
     * esta probado: se mete a mano el bit 14 en la primera cuenta -que es
     * EXACTAMENTE lo que hacia el formato anterior- y el lector v2 tiene
     * que discrepar. Si no discrepa, el banco no vale y da igual lo verde
     * que salga lo de arriba.
     */
    printf("\n  3. y que sepa fallar (se mete el bit 14 a mano)\n");
    {
        FILE *f = fopen(g_con_anillos ? g_con_anillos : argv[1], "rb");
        size_t n = f ? fread(g_buf, 1, sizeof g_buf, f) : 0;
        uint32_t off, tam;
        unsigned long t2, p2, t2b, p2b;
        int desb, desb2;
        if (f) { fclose(f); }
        off = le32v(&g_buf[CAB + 4U]);
        tam = le32v(&g_buf[CAB + 8U]);
        (void)n;
        recorre_v2(&g_buf[off], tam, &t2, &p2, &desb);
        g_buf[off + 1U] |= 0x40U;              /* el bit 14 de la cuenta */
        recorre_v2(&g_buf[off], tam, &t2b, &p2b, &desb2);
        {
            char d[96];
            snprintf(d, sizeof d, "%lu trazos -> %lu", t2, t2b);
            comprueba(t2b != t2 || p2b != p2 || desb2 != desb,
                      "con el bit 14 el lector v2 YA NO ve lo mismo", d);
        }
    }

    printf("\n%s (%d fallos)\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

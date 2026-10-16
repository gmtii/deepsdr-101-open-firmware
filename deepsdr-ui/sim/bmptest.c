/*
 * Banco de las cabeceras BMP (User/bmp.c del firmware).
 *
 * LO QUE SE MIDE
 * --------------
 * Que un visor que no sabe nada de este proyecto abra lo que escribimos y
 * vea los puntos que pusimos. Ni mas ni menos.
 *
 * Una cabecera de BMP es treinta y tantos numeros en un orden concreto, y
 * equivocarse en uno da un fichero que casi funciona: se abre en un visor
 * y en otro no, o sale del reves, o con los colores cambiados. Comprobarla
 * leyendo el codigo es comprobar que hice lo que pretendia hacer, que es
 * justo lo que no sirve. Asi que este banco escribe los ficheros y
 * tools/bmp_check.py se los da a Pillow, que es un lector de imagenes de
 * otra gente, y compara punto por punto contra el patron que se puso.
 *
 * El patron no es un degradado suave a proposito: lleva bordes duros y los
 * colores puros en las esquinas. Un degradado disimula que la imagen salga
 * del reves o con el rojo y el azul cambiados; unas barras no.
 */
#include "bmp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ANCHO 320U
#define ALTO  256U

static int fallos = 0;

/*
 * El patron, en ternas RGB. Se calcula aqui y lo vuelve a calcular el
 * comprobador en Python a partir de esta misma descripcion, que esta
 * escrita en el comentario de tools/bmp_check.py:
 *
 *   - fila 0 entera ROJA y fila ALTO-1 entera AZUL: si la imagen sale del
 *     reves, se ve al instante.
 *   - columna 0 VERDE y columna ANCHO-1 BLANCA: lo mismo en horizontal.
 *   - el resto, cuatro barras verticales de colores puros.
 */
static void patron(uint16_t x, uint16_t y, uint8_t *rgb)
{
    if (y == 0U)            { rgb[0] = 255; rgb[1] = 0;   rgb[2] = 0;   return; }
    if (y == ALTO - 1U)     { rgb[0] = 0;   rgb[1] = 0;   rgb[2] = 255; return; }
    if (x == 0U)            { rgb[0] = 0;   rgb[1] = 255; rgb[2] = 0;   return; }
    if (x == ANCHO - 1U)    { rgb[0] = 255; rgb[1] = 255; rgb[2] = 255; return; }
    switch ((x * 4U) / ANCHO) {
    case 0:  rgb[0] = 248; rgb[1] = 0;   rgb[2] = 0;   break;
    case 1:  rgb[0] = 0;   rgb[1] = 252; rgb[2] = 0;   break;
    case 2:  rgb[0] = 0;   rgb[1] = 0;   rgb[2] = 248; break;
    default: rgb[0] = 248; rgb[1] = 252; rgb[2] = 0;   break;
    }
}

static int escribe(const char *ruta, uint8_t bits)
{
    static uint8_t cab[BMP_CAB_BYTES];
    static uint8_t fila[ANCHO * 3U];
    static uint8_t sal[ANCHO * 3U];
    uint32_t total = bmp_cabecera(cab, ANCHO, ALTO, bits);
    uint32_t nfila = bmp_fila_bytes(ANCHO, bits);
    uint16_t x, y;
    FILE *f = fopen(ruta, "wb");

    if (!f) { printf("  no puedo escribir %s   MAL\n", ruta); return 1; }
    fwrite(cab, 1, BMP_CAB_BYTES, f);
    for (y = 0U; y < ALTO; y++) {
        for (x = 0U; x < ANCHO; x++) { patron(x, y, &fila[x * 3U]); }
        if (bits == BMP_8) {
            /* El fax entrega un byte de gris por punto y el BMP de 8 bits
             * guarda indices de paleta: con la rampa que pone la cabecera,
             * el byte es el mismo. Aqui se fabrica ese gris a partir del
             * patron de color para poder comparar. */
            for (x = 0U; x < ANCHO; x++) {
                sal[x] = (uint8_t)((fila[x * 3U] * 77U + fila[x * 3U + 1U] * 151U
                                    + fila[x * 3U + 2U] * 28U) >> 8);
            }
            fwrite(sal, 1, nfila, f);
        } else if (bits == BMP_16) {
            bmp_fila565(fila, sal, ANCHO);
            fwrite(sal, 1, nfila, f);
        } else {
            bmp_fila24(fila, sal, ANCHO);
            fwrite(sal, 1, nfila, f);
        }
    }
    fclose(f);

    printf("  %-14s %u bits, %ux%u, fila %u B, total %u B (%u bloques de 4 kB)\n",
           ruta, bits, ANCHO, ALTO, nfila, total, total / 4096U);
    if ((total % 4096U) != 0U) {
        printf("      el total no es multiplo de 4096: el ultimo bloque quedaria a medias   MAL\n");
        fallos++;
    }
    if (total != BMP_CAB_BYTES + nfila * ALTO) {
        printf("      bfSize no cuadra con lo que se ha escrito   MAL\n");
        fallos++;
    }
    return 0;
}

int main(void)
{
    printf("\n=== Cabeceras BMP ===\n\n");
    (void)escribe("out/bmp24.bmp", BMP_24);
    (void)escribe("out/bmp16.bmp", BMP_16);
    (void)escribe("out/bmp8.bmp",  BMP_8);
    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "escritos (falta que Pillow diga lo suyo)");
    return fallos ? 1 : 0;
}

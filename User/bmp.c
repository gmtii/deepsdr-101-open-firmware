#include "bmp.h"

/* Numeros al fichero en el orden que manda el formato: primero el byte
 * bajo. No se vuelca una estructura de C a proposito - el relleno que
 * mete el compilador entre campos no esta en el formato, y una cabecera
 * de BMP con dos bytes de mas no la abre nadie. */
static void p16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)(v >> 8);
}

static void p32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    b[2] = (uint8_t)((v >> 16) & 0xFFU);
    b[3] = (uint8_t)((v >> 24) & 0xFFU);
}

uint32_t bmp_fila_bytes(uint16_t ancho, uint8_t bits)
{
    uint32_t b = (uint32_t)ancho * ((bits == BMP_8) ? 1UL :
                                    (bits == BMP_16) ? 2UL : 3UL);
    return (b + 3UL) & ~3UL;   /* cada fila acaba en frontera de 4 bytes */
}

uint32_t bmp_cabecera(uint8_t *b, uint16_t ancho, uint16_t alto, uint8_t bits)
{
    uint32_t fila = bmp_fila_bytes(ancho, bits);
    uint32_t puntos = fila * (uint32_t)alto;
    uint32_t total = BMP_CAB_BYTES + puntos;
    uint32_t i;
    uint8_t  v4 = (uint8_t)(bits == BMP_16);   /* las mascaras piden cabecera V4 */
    uint8_t  gris = (uint8_t)(bits == BMP_8);  /* y el gris pide paleta */
    uint32_t info = v4 ? 108UL : 40UL;

    for (i = 0U; i < BMP_CAB_BYTES; i++) { b[i] = 0U; }

    /* --- BITMAPFILEHEADER, 14 bytes --- */
    b[0] = 'B'; b[1] = 'M';
    p32(&b[2], total);
    /* b[6..9] reservados, a cero */
    p32(&b[10], BMP_CAB_BYTES);   /* donde empiezan los puntos */

    /* --- BITMAPINFOHEADER (40) o BITMAPV4HEADER (108) --- */
    p32(&b[14], info);
    p32(&b[18], (uint32_t)ancho);
    /*
     * ALTURA NEGATIVA = de arriba abajo.
     *
     * Un BMP normal guarda la ultima fila primero, herencia de como
     * dibujaban los primeros sistemas. Aqui eso obligaria a tener las 256
     * filas en memoria antes de escribir la primera, y no las hay: el
     * decodificador entrega una y la olvida. Con la altura en negativo el
     * formato acepta el orden natural y se puede escribir segun llega.
     */
    p32(&b[22], (uint32_t)(-(int32_t)alto));
    p16(&b[26], 1U);                    /* planos, siempre 1 */
    p16(&b[28], (uint16_t)bits);
    p32(&b[30], v4 ? 3UL : 0UL);        /* 3 = BI_BITFIELDS, 0 = sin comprimir */
    p32(&b[34], puntos);
    p32(&b[38], 2835UL);                /* 72 ppp en puntos por metro, por decir algo */
    p32(&b[42], 2835UL);
    if (gris) {
        /*
         * LA PALETA, 256 GRISES
         *
         * Un BMP de 8 bits no guarda colores: guarda INDICES, y la paleta
         * dice a que color corresponde cada indice. Poniendo una rampa
         * donde el indice N vale (N,N,N), el byte de gris que entrega el
         * decodificador vale tal cual como byte del fichero: cero
         * conversion por punto, y son 453.600 puntos en un fax.
         *
         * Va justo detras de la cabecera de informacion, que es donde la
         * busca el formato, y cabe de sobra en el bloque: 54 + 1024 =
         * 1078 de los 4096.
         */
        p32(&b[46], 256UL);   /* colores usados */
        p32(&b[50], 256UL);   /* y cuantos importan */
        for (i = 0U; i < 256U; i++) {
            uint32_t o = info + 14UL + i * 4UL;
            b[o]      = (uint8_t)i;   /* azul  */
            b[o + 1U] = (uint8_t)i;   /* verde */
            b[o + 2U] = (uint8_t)i;   /* rojo  */
            b[o + 3U] = 0U;           /* sin usar */
        }
    }
    /* b[46..53] en los otros formatos: cero en ambos */

    if (v4) {
        /* Donde cae cada color dentro de los 16 bits: 5 de rojo, 6 de
         * verde, 5 de azul. El verde se lleva el bit de mas porque el ojo
         * distingue mas tonos de verde que de los otros dos. */
        p32(&b[54], 0x0000F800UL);      /* rojo */
        p32(&b[58], 0x000007E0UL);      /* verde */
        p32(&b[62], 0x0000001FUL);      /* azul */
        p32(&b[66], 0x00000000UL);      /* alfa: no hay */
        p32(&b[70], 0x73524742UL);      /* 'BGRs': espacio de color sRGB */
    }

    return total;
}

void bmp_fila565(const uint8_t *rgb, uint8_t *out, uint16_t n)
{
    uint16_t x;

    for (x = 0U; x < n; x++) {
        uint16_t v = (uint16_t)(((uint16_t)(rgb[x * 3U] >> 3) << 11) |
                                ((uint16_t)(rgb[x * 3U + 1U] >> 2) << 5) |
                                 (uint16_t)(rgb[x * 3U + 2U] >> 3));
        out[x * 2U]      = (uint8_t)(v & 0xFFU);
        out[x * 2U + 1U] = (uint8_t)(v >> 8);
    }
}

void bmp_fila24(const uint8_t *rgb, uint8_t *out, uint16_t n)
{
    uint16_t x;

    for (x = 0U; x < n; x++) {
        out[x * 3U]      = rgb[x * 3U + 2U];   /* azul */
        out[x * 3U + 1U] = rgb[x * 3U + 1U];   /* verde */
        out[x * 3U + 2U] = rgb[x * 3U];        /* rojo */
    }
}

/*
 * LA FUENTE DE LA LLUVIA DEL ARRANQUE, LEIDA DE LA ZONA ALTA.
 * Ver splash_fuente.h y tools/fuente_pack.py.
 */

#include "splash_fuente.h"
#include "zona_alta.h"

/*
 * LA FUENTE DE LA LLUVIA, QUE YA NO ESTA EN LA FLASH INTERNA - 17/10/2026.
 *
 * font_rain_16 eran 5.592 bytes del firmware -45 katakana- para una
 * pantalla de tres segundos. Se fue a BD.BIN, en la zona alta de la flash
 * SPI, y se carga aqui.
 *
 * DONDE SE CARGA: en el mismo buffer prestado que usan las columnas, detras
 * de ellas. Son 54 kB de los que la lluvia usa 396; la fuente son otros
 * 5.592 y entran de sobra. No hace falta .bss nuevo -que no lo hay: la
 * radio va al 100 % de RAM- ni pedirle nada a nadie.
 *
 * EL FORMATO SE LEE DE UN TIRON Y SE APUNTA DENTRO: los glifos vienen ya
 * como los quiere gfx2_glyph_t en memoria y los rangos como gfx2_range_t,
 * asi que no hay bucle de conversion, solo tres punteros. Ver la cabecera
 * de tools/fuente_pack.py para el porque.
 *
 * EL LECTOR SE LE PASA, no lo llama por su nombre: en la radio es
 * spi_flash_read y en el banco un fichero en memoria, que es lo que
 * permite probar esto con ficheros rotos sin una radio delante. Es el
 * mismo trato que ya tenia zona_alta_abre().
 *
 * SE COMPRUEBA TODO LO QUE SE VA A USAR COMO INDICE. La fuente sale de un
 * fichero que el usuario graba, asi que las cuentas de aqui son las que
 * impiden que un BD.BIN a medias mande a gfx2_text_in() a leer fuera del
 * buffer: la marca, la version, que los tres bloques quepan en lo que se
 * leyo, y que ningun rango apunte mas alla de la tabla de glifos. Si algo
 * no cuadra se devuelve 0 y la pantalla sale fija, que es lo que pasa
 * tambien cuando no hay BD.BIN.
 */
const gfx2_font_t *splash_fuente_carga(za_lee_fn lee, uint8_t *pozo,
                                       uint32_t pozo_n, gfx2_font_t *f)
{
    uint32_t addr = 0UL, tam = 0UL;
    uint32_t n_gl, n_rg, n_bm, hacen;
    const uint8_t *p;

    if (lee == 0 || pozo == 0 || f == 0) { return 0; }
    if (!zona_alta_abre(lee)) { return 0; }
    if (!zona_alta_donde(ZA_TIPO_SPLASH, &addr, &tam)) { return 0; }
    if (tam < 16UL || tam > pozo_n) { return 0; }

    lee(addr, pozo, tam);
    if (pozo[0] != 'S' || pozo[1] != 'P' || pozo[2] != 'L' || pozo[3] != 'F') {
        return 0;
    }
    if (pozo[4] != 1U) { return 0; }      /* version */

    n_gl = (uint32_t)pozo[8]  | ((uint32_t)pozo[9]  << 8);
    n_rg = (uint32_t)pozo[10] | ((uint32_t)pozo[11] << 8);
    n_bm = (uint32_t)pozo[12] | ((uint32_t)pozo[13] << 8)
         | ((uint32_t)pozo[14] << 16) | ((uint32_t)pozo[15] << 24);
    if (n_gl == 0UL || n_rg == 0UL || n_bm == 0UL) { return 0; }
    if (n_gl > 1024UL || n_rg > 256UL) { return 0; }

    /* 12 por glifo y 6 por rango: el formato del fichero es el de memoria. */
    hacen = 16UL + n_gl * 12UL + n_rg * 6UL + n_bm;
    if (hacen != tam) { return 0; }

    p = pozo + 16;
    f->glyphs  = (const gfx2_glyph_t *)(const void *)p;
    f->ranges  = (const gfx2_range_t *)(const void *)(p + n_gl * 12UL);
    f->bitmap  = p + n_gl * 12UL + n_rg * 6UL;
    f->n_ranges = (uint8_t)n_rg;
    f->ascent   = pozo[5];
    f->descent  = pozo[6];
    f->line_h   = pozo[7];
    if (f->line_h == 0U) { return 0; }

    /* Que ningun rango mande a leer fuera de la tabla de glifos. */
    {
        uint8_t i;
        for (i = 0U; i < f->n_ranges; i++) {
            const gfx2_range_t *r = &f->ranges[i];
            if (r->hi < r->lo) { return 0; }
            if ((uint32_t)r->first + (uint32_t)(r->hi - r->lo) >= n_gl) {
                return 0;
            }
        }
    }
    return f;
}

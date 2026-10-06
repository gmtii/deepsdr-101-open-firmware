#include "formato.h"
#include "disco.h"
#include "spi_flash.h"
#include "debug_uart.h"
#include <string.h>

#define SEC            512U
#define POR_4K           8U        /* sectores de 512 en un borrado de 4 kB */
#define RAIZ_ENT       256U        /* entradas del directorio raiz */
#define RAIZ_SEC      (RAIZ_ENT * 32U / SEC)   /* = 16 sectores */

static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;  p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/*
 * EL REPARTO, Y POR QUE SALE ASI.
 *
 * spc (sectores por cluster) tiene que ser potencia de dos Y DIVIDIR al
 * borrado de 4 kB, o sea 1, 2, 4 u 8. Se coge el mas grande -4 kB de
 * cluster- porque asi un cluster es justo un borrado: escribir dentro de
 * un fichero nunca obliga a leer-modificar-escribir el bloque de al lado,
 * que es de donde salian la mitad de los sustos de este driver.
 *
 * Con 4 kB de cluster, 7 MB de disco son 1792 clusters y 1 MB son 250: los
 * dos por debajo de 4084, o sea FAT12 en los dos casos. No se usa FAT16
 * aunque el driver lo lea: una tabla menos de la que dudar.
 *
 * Y LA ZONA DE DATOS TIENE QUE EMPEZAR ALINEADA a 4 kB, que es la otra
 * condicion del driver. Como el raiz y las tablas salen de las cuentas, lo
 * que se ajusta para cuadrar es el numero de sectores RESERVADOS: se
 * empieza en 1 y se sube hasta que (reservados + 2*fat + raiz) sea
 * multiplo de ocho. Son siete sectores de margen como mucho, 3,5 kB, y a
 * cambio ningun cluster cruza un borrado.
 */
uint8_t formato_plan(uint32_t bloques, uint8_t *spc_o, uint16_t *res_o,
                     uint16_t *fat_o, uint16_t *raiz_o, uint16_t *clus_o)
{
    uint8_t  spc = POR_4K;
    uint16_t raiz = (uint16_t)RAIZ_SEC;
    uint16_t fat, res;
    uint32_t datos, clusters, prev = 0U;
    uint8_t  vuelta;

    if (bloques < 64U) { return 0U; }

    /*
     * Cuantos clusters caben y cuanto mide la tabla se necesitan el uno al
     * otro: la tabla ocupa sitio que deja de ser datos. Dos vueltas bastan
     * -la segunda ya no mueve el numero- y asi no hay bucle que pueda no
     * terminar dentro de una radio.
     */
    fat = 1U; res = 1U; clusters = 0U;
    for (vuelta = 0U; vuelta < 4U; vuelta++) {
        uint32_t cabecera = (uint32_t)res + 2UL * fat + raiz;
        if (bloques <= cabecera) { return 0U; }
        datos = bloques - cabecera;
        clusters = datos / spc;
        if (clusters < 1UL) { return 0U; }
        /* FAT12: 1,5 bytes por cluster, mas las dos entradas reservadas */
        fat = (uint16_t)((((clusters + 2UL) * 3UL + 1UL) / 2UL + SEC - 1UL) / SEC);
        if (fat < 1U) { fat = 1U; }
        /* y ahora los reservados que hagan falta para alinear los datos */
        res = 1U;
        while ((((uint32_t)res + 2UL * fat + raiz) % POR_4K) != 0U) { res++; }
        if (clusters == prev) { break; }
        prev = clusters;
    }
    if (clusters > 4084UL) {
        /* No deberia pasar con 4 kB de cluster hasta 16 MB de disco, pero
         * si pasara habria que irse a FAT16 y este formateador no lo hace:
         * mejor decir que no que dejar un volumen que el driver lea como
         * FAT12 y tenga 5000 clusters. */
        return 0U;
    }
    if (spc_o)  { *spc_o = spc; }
    if (res_o)  { *res_o = res; }
    if (fat_o)  { *fat_o = fat; }
    if (raiz_o) { *raiz_o = raiz; }
    if (clus_o) { *clus_o = (uint16_t)clusters; }
    return 1U;
}

formato_r_t formato_haz(void)
{
    uint8_t  sec[SEC];
    uint8_t  spc;
    uint16_t res, fat, raiz, clusters;
    uint32_t bloques = disco_bloques();
    uint32_t lba, i;

    if (bloques == 0UL) { return FORMATO_SIN_CHIP; }
    if (!formato_plan(bloques, &spc, &res, &fat, &raiz, &clusters)) {
        return FORMATO_MUY_PEQUENO;
    }

    /* --- el sector de arranque --- */
    memset(sec, 0, sizeof sec);
    sec[0] = 0xEBU; sec[1] = 0x3CU; sec[2] = 0x90U;
    memcpy(&sec[3], "DEEPSDR ", 8U);
    le16(&sec[11], (uint16_t)SEC);
    sec[13] = spc;
    le16(&sec[14], res);
    sec[16] = 2U;                        /* DOS tablas: el driver lo exige */
    le16(&sec[17], (uint16_t)RAIZ_ENT);
    le16(&sec[19], (bloques < 65536UL) ? (uint16_t)bloques : 0U);
    sec[21] = 0xF8U;                     /* disco fijo */
    le16(&sec[22], fat);
    le16(&sec[24], 32U);                 /* sectores por pista, decorativo */
    le16(&sec[26], 8U);                  /* cabezas, decorativo */
    le32(&sec[28], 0UL);                 /* ocultos */
    le32(&sec[32], (bloques < 65536UL) ? 0UL : bloques);
    sec[36] = 0x80U;
    sec[38] = 0x29U;                     /* hay numero de serie y etiqueta */
    le32(&sec[39], 0x44454550UL);        /* "DEEP" como numero de serie */
    memcpy(&sec[43], "DEEPSDR    ", 11U);
    memcpy(&sec[54], "FAT12   ", 8U);
    sec[510] = 0x55U; sec[511] = 0xAAU;
    if (!disco_escribe(0UL, sec)) { return FORMATO_ERROR_ESCRITURA; }

    /* --- las dos tablas --- */
    for (i = 0U; i < 2U; i++) {
        uint32_t base = (uint32_t)res + i * fat;
        uint16_t k;
        for (k = 0U; k < fat; k++) {
            memset(sec, 0, sizeof sec);
            if (k == 0U) {
                /* Las dos entradas reservadas. 0xF8 es el mismo byte de
                 * medio que el sector de arranque: si no cuadran, chkdsk
                 * de Windows avisa. */
                sec[0] = 0xF8U; sec[1] = 0xFFU; sec[2] = 0xFFU;
            }
            if (!disco_escribe(base + k, sec)) { return FORMATO_ERROR_ESCRITURA; }
        }
    }

    /* --- el directorio raiz, vacio salvo la etiqueta --- */
    lba = (uint32_t)res + 2UL * fat;
    for (i = 0U; i < raiz; i++) {
        memset(sec, 0, sizeof sec);
        if (i == 0U) {
            memcpy(&sec[0], "DEEPSDR    ", 11U);
            sec[11] = 0x08U;             /* etiqueta de volumen */
        }
        if (!disco_escribe(lba + i, sec)) { return FORMATO_ERROR_ESCRITURA; }
    }

    /*
     * Y que el driver se olvide de lo que creia saber del volumen: acaba
     * de cambiar debajo de el. Sin esto seguiria usando la geometria
     * vieja hasta el siguiente arranque, que es justo el fallo que
     * estamos arreglando pero al reves.
     */
    spi_flash_geo_olvida();
    debug_print_dec("formato: hecho, clusters", (uint32_t)clusters);
    return FORMATO_OK;
}

/*
 * La comprobacion. Ver el comentario de formato_comprueba() en formato.h:
 * las dos primeras preguntas las contesta el sector de arranque, y la
 * tercera -escribir y releer- es la unica que toca la tabla, el directorio
 * y los datos, que es donde se rompen las cosas de verdad.
 */
formato_r_t formato_comprueba(void)
{
    static const char k_txt[] = "DEEPSDR disco formateado";
    uint32_t a, b, d, cl;
    uint8_t  leido[sizeof k_txt];
    uint32_t n;

    spi_flash_geo_olvida();
    if (!spi_flash_geometria_lectura(&a, &b, &d, &cl)) { return FORMATO_NO_LEE; }
    if (!spi_flash_geometria(&a, &b, &d, &cl))         { return FORMATO_NO_GRABA; }

    if (spi_flash_write_or_update_file("PRUEBA  ", "TXT",
                                       (const uint8_t *)k_txt,
                                       (uint32_t)(sizeof k_txt - 1U)) <= 0) {
        return FORMATO_PRUEBA_MAL;
    }
    memset(leido, 0, sizeof leido);
    n = spi_flash_read_file_by_name("PRUEBA  ", "TXT", leido, sizeof leido - 1U);
    if (n != (uint32_t)(sizeof k_txt - 1U)) { return FORMATO_PRUEBA_MAL; }
    if (memcmp(leido, k_txt, sizeof k_txt - 1U) != 0) { return FORMATO_PRUEBA_MAL; }

    debug_print("formato: comprobado, escribe y lee\n");
    return FORMATO_OK;
}

const char *formato_porque_txt(formato_r_t r)
{
    switch (r) {
    case FORMATO_OK:              return "formateado";
    case FORMATO_NO_LEE:          return "escrito, pero no lo reconozco";
    case FORMATO_NO_GRABA:        return "lo leo pero no lo puedo grabar";
    case FORMATO_PRUEBA_MAL:      return "el fichero de prueba no cuadra";
    case FORMATO_SIN_CHIP:        return "no se ve la flash";
    case FORMATO_MUY_PEQUENO:     return "el disco no da para un volumen";
    case FORMATO_ERROR_ESCRITURA: return "no se pudo escribir";
    default:                      return "?";
    }
}

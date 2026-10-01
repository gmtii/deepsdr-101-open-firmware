/*
 * EL CARGADOR DE ARRANQUE. Ver cargador.h para el por que y para el
 * comportamiento exacto que se esta copiando.
 *
 * UNA SOLA REGLA AL ESCRIBIR ESTO: no mejorar nada. Cada vez que apetece
 * anadir una comprobacion -un CRC, la longitud, releer lo escrito- hay que
 * acordarse de que el criterio de aprobado es "hace lo mismo que el que
 * ya funciona". Las mejoras vienen DESPUES, cuando esto arranque en una
 * placa de verdad, y cada una con su banco. Si se mejora ahora no hay
 * forma de saber si un fallo es del cargador nuevo o de la mejora.
 */
#include "cargador.h"
#include "spi_flash.h"
#include <string.h>

/* Los ocho bytes que el gestor compara en 0x08060000. Estan aqui y en
 * User/firma_app.c; son el mismo numero por los dos lados y si alguna vez
 * cambia uno tiene que cambiar el otro. El banco lo comprueba. */
static const uint8_t k_firma[CARGA_FIRMA_BYTES] = {
    0x8FU, 0x25U, 0xC8U, 0x65U, 0x59U, 0x9CU, 0x55U, 0x31U
};

#define SECTOR_BYTES   512U

/* El nombre 8.3 tal y como esta en el directorio. El gestor original abre
 * "update4.bin" por FatFs, que normaliza a mayusculas; aqui se compara
 * contra la entrada del directorio, que es donde Windows lo deja. */
static const char k_nombre[8] = { 'U','P','D','A','T','E','4',' ' };
static const char k_ext[3]    = { 'B','I','N' };

static uint8_t  s_buf[SECTOR_BYTES];
static uint32_t s_grabados;

/* --- geometria del volumen, leida una vez --------------------------- */

static uint32_t s_fat1, s_raiz, s_datos, s_nclus;
static uint8_t  s_geo_ok;

static uint8_t geo(void)
{
    if (!s_geo_ok) {
        s_geo_ok = spi_flash_geometria(&s_fat1, &s_raiz, &s_datos, &s_nclus);
    }
    return s_geo_ok;
}

/*
 * El siguiente eslabon de la cadena. FAT12: cada entrada son 12 bits, o
 * sea byte y medio, y la de indice impar vive en el nibble alto. Se leen
 * dos bytes desde el desplazamiento cluster + cluster/2 y se elige mitad.
 *
 * Se lee del chip en cada llamada -dos bytes- en vez de cargar la FAT
 * entera: el cargador no tiene RAM que gastar y 640 lecturas de 2 bytes
 * es lo que cuesta leer un fichero de 320 kB de todas formas.
 */
static uint16_t fat_siguiente(uint32_t cluster)
{
    uint32_t off = cluster + (cluster >> 1);
    uint8_t  par[2];
    uint16_t v;

    spi_flash_read(s_fat1 * SECTOR_BYTES + off, par, 2U);
    v = (uint16_t)((uint16_t)par[0] | ((uint16_t)par[1] << 8));
    return (cluster & 1U) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFFU);
}

/*
 * Buscar UPDATE4.BIN en el directorio raiz.
 *
 * Tres cosas que hay que respetar y que se han roto alguna vez en este
 * proyecto (ver sim/fat_troceado.c): una entrada a 0x00 significa FIN del
 * directorio y ahi se para; una a 0xE5 es un borrado y se salta; y una con
 * atributo 0x0F es un trozo de nombre largo de Windows, que se salta
 * TAMBIEN -si no, sus bytes se leen como si fueran un nombre 8.3 y a veces
 * cuelan-. El raiz tiene 32 sectores, no uno: un fichero copiado desde
 * Windows a un volumen con historia cae mas adelante.
 */
uint32_t cargador_busca_update4(uint32_t *primer_cluster)
{
    uint32_t sect, i;
    uint32_t sectores_raiz;

    if (!geo()) { return 0U; }
    sectores_raiz = s_datos - s_raiz;

    for (sect = 0U; sect < sectores_raiz; sect++) {
        spi_flash_read((s_raiz + sect) * SECTOR_BYTES, s_buf, SECTOR_BYTES);
        for (i = 0U; i < SECTOR_BYTES; i += 32U) {
            const uint8_t *e = &s_buf[i];

            if (e[0] == 0x00U)            { return 0U; }   /* fin */
            if (e[0] == 0xE5U)            { continue; }    /* borrada */
            if ((e[11] & 0x0FU) == 0x0FU) { continue; }    /* nombre largo */
            if ((e[11] & 0x08U) != 0U)    { continue; }    /* etiqueta */
            if (memcmp(e, "UPDATE4 BIN", 11) != 0) { continue; }

            *primer_cluster = (uint32_t)e[26] | ((uint32_t)e[27] << 8);
            return (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                 | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        }
    }
    return 0U;
}

/*
 * Copiar el fichero a la flash interna, cluster a cluster.
 *
 * "Tal cual y sin mirar nada", igual que el gestor de ahora: se copian
 * exactamente `tam` bytes del fichero a 0x08020000 y no se comprueba
 * ninguno. El unico limite es el que ya impone el gestor, 0x50000, y se
 * decide ANTES de borrar nada: si el fichero no cabe no se toca la flash.
 */
static carga_r_t copia(const carga_fmc_t *fmc, uint32_t tam, uint32_t cluster)
{
    uint32_t addr = CARGA_APP_BASE;
    uint32_t quedan = tam;
    uint32_t s;

    for (s = CARGA_APP_BASE; s < CARGA_BORRA_HASTA; s += 0x20000UL) {
        if (!fmc->borra(s)) { return CARGA_ERROR_BORRAR; }
    }

    while (quedan > 0U) {
        uint32_t n = (quedan > SECTOR_BYTES) ? SECTOR_BYTES : quedan;

        if (cluster < 2U || cluster >= (s_nclus + 2U)) { return CARGA_FICHERO_ROTO; }
        spi_flash_read((s_datos + (cluster - 2U)) * SECTOR_BYTES, s_buf, SECTOR_BYTES);
        if (!fmc->escribe(addr, s_buf, n)) { return CARGA_ERROR_GRABAR; }

        addr    += n;
        quedan  -= n;
        s_grabados += n;
        cluster  = fat_siguiente(cluster);
        if (quedan > 0U && (cluster < 2U || cluster >= 0x0FF8U)) {
            return CARGA_FICHERO_ROTO;   /* la cadena se acaba antes que el fichero */
        }
    }
    return CARGA_GRABADA;
}

/*
 * Las dos unicas pruebas que hace el gestor, y en su orden.
 *
 * La primera es la que sorprende: la pila inicial de la aplicacion -la
 * primera palabra del vector, en 0x08020000- tiene que estar en el TCM.
 * En el original son tres instrucciones: ubfx r0, r0, #17, #12 y comparar
 * con 0x800, que es lo mismo que exigir 0x10000000..0x1001FFFF. Sirve de
 * "hay algo con pinta de aplicacion aqui", y una flash recien borrada
 * (0xFFFFFFFF) no pasa.
 */
uint8_t cargador_imagen_vale(const carga_fmc_t *fmc, carga_r_t *porque)
{
    uint8_t  v[CARGA_FIRMA_BYTES];
    uint32_t sp;

    fmc->lee(CARGA_APP_BASE, v, 4U);
    sp = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
       | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    if (((sp >> 17) & 0x0FFFU) != 0x800U) {
        *porque = CARGA_SIN_APP;
        return 0U;
    }

    fmc->lee(CARGA_FIRMA_ADDR, v, CARGA_FIRMA_BYTES);
    if (memcmp(v, k_firma, CARGA_FIRMA_BYTES) != 0) {
        *porque = CARGA_SIN_FIRMA;
        return 0U;
    }

    *porque = CARGA_ARRANCA;
    return 1U;
}

carga_r_t cargador_arranca(const carga_fmc_t *fmc)
{
    uint32_t tam, cluster = 0U;
    carga_r_t porque;

    s_grabados = 0U;

    /*
     * El fichero PRIMERO, antes de comprobar la imagen. Es lo que salva la
     * radio: con una imagen mala dentro, el siguiente arranque vuelve a
     * leer el volumen y se recupera copiando un update4.bin bueno, sin
     * SWD. Si esto se moviera detras de la comprobacion, una imagen mala
     * seria un ladrillo.
     */
    if (!geo()) {
        /* sin volumen legible no hay nada que grabar, pero la imagen que
         * ya este dentro puede ser perfectamente buena: se comprueba. */
        if (cargador_imagen_vale(fmc, &porque)) { return CARGA_ARRANCA; }
        return CARGA_NO_HAY_VOLUMEN;
    }

    tam = cargador_busca_update4(&cluster);
    if (tam > CARGA_TAM_MAX) {
        /*
         * MAS DE 0x50000: el gestor cierra el fichero y salta DIRECTO a la
         * comprobacion (0x800a68e: f_close y "b 0x800a7ca"), saltandose el
         * f_unlink. O sea que un update4.bin demasiado grande se queda en
         * el disco para siempre y se vuelve a mirar -y a descartar- en cada
         * arranque. No es un descuido que convenga arreglar aqui: si se
         * borrara, el dueno perderia el fichero sin enterarse de por que.
         */
        (void)cargador_imagen_vale(fmc, &porque);
        return CARGA_FICHERO_GRANDE;
    }
    if (tam > 0U) {
        carga_r_t r = copia(fmc, tam, cluster);
        if (r != CARGA_GRABADA) { return r; }
        /*
         * Y AHORA SE BORRA EL FICHERO. Esto no estaba en el comentario del
         * linker script y solo sale desensamblando: en 0x800a7a8, despues
         * del f_close, el gestor llama a f_unlink("update4.bin").
         *
         * Es lo que impide que la radio se regrabe en CADA arranque, y es
         * la razon por la que el cargador SI escribe en el volumen. Sin
         * esto, el clon pasaria todas las pruebas del banco y luego, en la
         * placa, regrabaria 320 kB cada vez que se enciende.
         */
        (void)spi_flash_fichero_borra(k_nombre, k_ext);
    }

    if (cargador_imagen_vale(fmc, &porque)) {
        return (tam > 0U) ? CARGA_GRABADA : CARGA_ARRANCA;
    }
    return porque;
}

uint32_t cargador_grabados(void) { return s_grabados; }

const char *cargador_porque_txt(carga_r_t r)
{
    switch (r) {
    case CARGA_ARRANCA:         return "arranca";
    case CARGA_GRABADA:         return "grabada y arranca";
    case CARGA_SIN_APP:         return "APP Not Programmed !";
    case CARGA_SIN_FIRMA:       return "Running APP---";
    case CARGA_NO_HAY_VOLUMEN:  return "no se lee el volumen";
    case CARGA_FICHERO_GRANDE:  return "update4.bin de mas de 0x50000";
    case CARGA_FICHERO_ROTO:    return "la cadena del fichero no cuadra";
    case CARGA_ERROR_BORRAR:    return "no se pudo borrar";
    case CARGA_ERROR_GRABAR:    return "no se pudo grabar";
    default:                    return "?";
    }
}

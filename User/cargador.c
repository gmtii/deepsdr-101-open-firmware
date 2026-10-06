/*
 * EL CARGADOR DE ARRANQUE. Ver cargador.h para el por que y para el
 * comportamiento exacto que se esta copiando.
 *
 * ESTO SE ESCRIBIO CON LA REGLA DE "no mejorar nada": el criterio de
 * aprobado era "hace lo mismo que el gestor de fabrica, que ya funciona",
 * y cada vez que apetecia anadir un CRC o una longitud habia que aguantarse,
 * porque mejorando a la vez no hay forma de saber si un fallo es del
 * cargador nuevo o de la mejora.
 *
 * ESA ETAPA YA PASO. El cargador arranco en la placa el 01/10/2026 y lleva
 * desde entonces funcionando, asi que en la rama "reload" si se mejora, y
 * cada mejora trae su banco:
 *
 *  - La aplicacion empieza en 0x0800C000 y la region es de una pieza, sin
 *    firma clavada en medio ni tope de 0x50000.  Ver cargador.h.
 *  - El borrado va por TABLA de sectores y no por un bucle que sumaba
 *    0x20000: con S3 y S4 en medio -que miden 16 y 64 kB- ese bucle se
 *    saltaba sectores.
 *  - La validacion es cabecera + longitud + CRC32 en vez de ocho bytes
 *    constantes, asi que una imagen grabada a medias ya no arranca.
 */
#include "cargador.h"
#include "idioma.h"
#include "spi_flash.h"
#include <string.h>

/*
 * EL CRC32, Y CUAL DE ELLOS.
 *
 * Hay mas de un "CRC32" y no dan el mismo numero.  Este es CRC-32/MPEG-2:
 * polinomio 0x04C11DB7, empieza en 0xFFFFFFFF, SIN reflejar la entrada ni
 * la salida y SIN xor final.  Su valor de control sobre "123456789" es
 * 0x0376E6E7, y esta comprobado -no recordado- en tools/cabecera.py, que
 * es quien tiene que dar exactamente el mismo numero que esto.
 *
 * NO es el de zlib.crc32() de Python ni el de los ficheros ZIP, que van
 * reflejados y con xor final y dan otra cosa.  Si alguien reescribe
 * cabecera.py con zlib, el cargador rechazara todas las imagenes y el
 * motivo no se vera por ningun lado.  Por eso el banco compara los dos.
 *
 * Es ademas el mismo que calcula la unidad de CRC por hardware del chip,
 * por si algun dia interesa cambiar esto por cuatro escrituras a un
 * registro.  Hoy va en software A PROPOSITO: asi el banco del PC ejecuta
 * ESTA funcion y no una imitacion.
 *
 * La tabla es de nibble -16 entradas, 64 bytes- en vez de la de 256 (1 kB).
 * Sale a unos 18 ms para los 320 kB de imagen de hoy, que al arrancar no
 * se notan, y ahorra 960 bytes de un cargador que vive en 48 kB.
 */
static const uint32_t k_crc_nib[16] = {
    0x00000000U, 0x04C11DB7U, 0x09823B6EU, 0x0D4326D9U,
    0x130476DCU, 0x17C56B6BU, 0x1A864DB2U, 0x1E475005U,
    0x2608EDB8U, 0x22C9F00FU, 0x2F8AD6D6U, 0x2B4BCB61U,
    0x350C9B64U, 0x31CD86D3U, 0x3C8EA00AU, 0x384FBDBDU
};

uint32_t cargador_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    while (n-- > 0U) {
        crc ^= (uint32_t)(*p++) << 24;
        crc  = (crc << 4) ^ k_crc_nib[(crc >> 28) & 0x0FU];
        crc  = (crc << 4) ^ k_crc_nib[(crc >> 28) & 0x0FU];
    }
    return crc;
}

/* Leer una palabra little-endian de un buffer. El Cortex-M4 es little
 * endian y podria hacerse con un cast, pero un cast a uint32_t* sobre un
 * puntero que no se sabe alineado es comportamiento indefinido y aqui no
 * hace falta correr. */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * LOS SECTORES QUE OCUPA LA APLICACION, UNO A UNO.
 *
 * Antes esto era un bucle "for (s = BASE; s < HASTA; s += 0x20000)", y
 * funcionaba porque TODOS los sectores de la aplicacion median 128 kB.
 * Con la base en 0x0800C000 eso deja de ser verdad: el S3 mide 16 kB y el
 * S4 mide 64, asi que sumar 0x20000 desde 0x0800C000 daria 0x0802C000
 * -dentro del S5- y el S4 no se borraria NUNCA.  La imagen se grabaria
 * encima de un sector sin borrar, que en una flash NOR significa que los
 * bits a 0 se quedan a 0: pasaria el CRC solo si los datos viejos y los
 * nuevos encajaran por casualidad, o sea nunca.
 *
 * Con la tabla delante, el dia que la base cambie el que no cuadre salta
 * en el _Static_assert de abajo y no en la radio.
 */
static const uint32_t k_sectores[] = {
    0x0800C000UL,   /* S3,  16 kB */
    0x08010000UL,   /* S4,  64 kB */
    0x08020000UL,   /* S5, 128 kB */
    0x08040000UL,   /* S6, 128 kB */
    0x08060000UL    /* S7, 128 kB */
};
#define N_SECTORES  (sizeof k_sectores / sizeof k_sectores[0])

_Static_assert(0x0800C000UL == CARGA_APP_BASE,
               "la tabla de sectores no empieza donde empieza la aplicacion");

#define SECTOR_BYTES   512U

/*
 * EL NOMBRE 8.3 TAL Y COMO ESTA EN EL DIRECTORIO.
 *
 * *** "update.bin", desde el 02/10/2026 y por el dueño: "vamos a dejar de
 * usar el nombre update4.bin, quiero que todo el sistema funcione con el
 * nombre update.bin". ***
 *
 * El "4" venia del gestor de fabrica, que abre "update4.bin" por FatFs.
 * Nuestro cargador no tiene por que heredar su nomenclatura, y en la rama
 * "reload" ya no hereda nada mas suyo.
 *
 * OJO AL RELLENO: son OCHO caracteres de nombre, rellenados con espacios,
 * mas tres de extension, porque asi es como lo guarda el directorio FAT.
 * "UPDATE" son seis letras, asi que van dos espacios detras. Un espacio de
 * menos y la comparacion de mas abajo no casa nunca, y el sintoma seria
 * una radio que ignora el fichero sin decir nada.
 *
 * EFECTO SECUNDARIO QUE CONVIENE SABER: un UPDATE4.BIN que quedara de
 * antes en el volumen ya no lo mira nadie. No molesta -no se graba ni se
 * borra- pero ocupa su sitio hasta que se borre a mano por USB.
 */
static const char k_nombre[8] = { 'U','P','D','A','T','E',' ',' ' };
#define K_NOMBRE_DIR  "UPDATE  BIN"   /* los 8+3 seguidos, como en el directorio */
_Static_assert(sizeof(K_NOMBRE_DIR) == 12, "el nombre del directorio son 11 caracteres");
static const char k_ext[3]    = { 'B','I','N' };

static uint8_t  s_buf[SECTOR_BYTES];
static uint32_t s_grabados;

/* --- geometria del volumen, leida una vez --------------------------- */

static uint32_t s_fat1, s_raiz, s_datos, s_nclus;
static uint8_t  s_geo_ok;
static uint8_t  s_spc;      /* sectores por cluster; 0 = sin leer */

static uint8_t geo(void)
{
    if (!s_geo_ok) {
        /*
         * LECTURA, NO ESCRITURA - 06/10/2026.
         *
         * *** De un issue: "The loader doesn't see update.bin on the disk",
         * con una ficha que decia "Volumen: no se reconoce". Y la radio con
         * el formateo DE FABRICA, sin tocar. ***
         *
         * Este cargador SOLO LEE: busca un fichero, lo copia a la flash
         * interna y se aparta. spi_flash_geometria() contesta a otra
         * pregunta -si se puede ESCRIBIR sin romper nada-, y para eso exige
         * dos copias de la FAT, que el cluster divida el bloque de borrado
         * de 4 kB, que el area de datos caiga en frontera y que sea FAT12 y
         * no FAT16. Nada de eso hace falta para leer, y son condiciones de
         * ESTE driver, no del formato.
         *
         * Con el formato de fabrica rechazado por alguna de ellas, esto
         * devolvia 0 y cargador_busca_update() se iba sin mirar el
         * directorio: el fichero estaba en el disco y nadie lo buscaba. Y
         * la salida que la pantalla ofrece -copiar update.bin al disco USB-
         * es justo la que no funciona. Pescadilla.
         */
        s_geo_ok = spi_flash_geometria_lectura(&s_fat1, &s_raiz, &s_datos, &s_nclus);
        s_spc    = spi_flash_spc();
        if (s_spc == 0U) { s_geo_ok = 0U; }   /* sin esto no se puede andar la cadena */
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
 * Buscar UPDATE.BIN en el directorio raiz.
 *
 * Tres cosas que hay que respetar y que se han roto alguna vez en este
 * proyecto (ver sim/fat_troceado.c): una entrada a 0x00 significa FIN del
 * directorio y ahi se para; una a 0xE5 es un borrado y se salta; y una con
 * atributo 0x0F es un trozo de nombre largo de Windows, que se salta
 * TAMBIEN -si no, sus bytes se leen como si fueran un nombre 8.3 y a veces
 * cuelan-. El raiz tiene 32 sectores, no uno: un fichero copiado desde
 * Windows a un volumen con historia cae mas adelante.
 */
uint32_t cargador_busca_update(uint32_t *primer_cluster)
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
            if (memcmp(e, K_NOMBRE_DIR, 11) != 0) { continue; }

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
 * Se copian exactamente `tam` bytes del fichero a CARGA_APP_BASE sin mirar
 * ninguno: quien mira es la comprobacion de DESPUES, que recalcula el CRC
 * sobre lo que ha quedado escrito en la flash -no sobre lo que creiamos
 * estar escribiendo-, que es justo la diferencia que hace que una copia
 * cortada a la mitad no arranque.
 *
 * Que el fichero quepa se decide ANTES de borrar nada: si no cabe, la
 * flash no se toca y la imagen que hubiera dentro sigue entera.
 */
static carga_r_t copia(const carga_fmc_t *fmc, uint32_t tam, uint32_t cluster)
{
    uint32_t addr = CARGA_APP_BASE;
    uint32_t quedan = tam;
    uint32_t s;

    for (s = 0U; s < N_SECTORES; s++) {
        if (!fmc->borra(k_sectores[s])) { return CARGA_ERROR_BORRAR; }
    }

    /*
     * UN CLUSTER NO ES UN SECTOR - 02/10/2026.
     *
     * Esto leia 512 bytes por eslabon de la cadena porque el volumen de
     * fabrica tiene un sector por cluster. En cuanto Windows formatea con
     * clusters de 4 kB, cada eslabon son OCHO sectores, y leyendo uno solo
     * se copiaba 1/8 del fichero mezclado con basura. Peor todavia: la
     * cadena se acababa antes de tiempo y salia "la cadena del fichero no
     * cuadra" sobre un fichero perfecto.
     *
     * El bucle de dentro vacia el cluster entero antes de pedir el
     * siguiente. s_buf sigue siendo de 512 porque no hace falta mas: se
     * copia de sector en sector, solo que ahora sabiendo cuantos van por
     * cluster.
     */
    while (quedan > 0U) {
        uint32_t base, hecho;

        if (cluster < 2U || cluster >= (s_nclus + 2U)) { return CARGA_FICHERO_ROTO; }

        base  = (s_datos + ((cluster - 2U) * s_spc)) * SECTOR_BYTES;
        hecho = 0U;
        while (hecho < ((uint32_t)s_spc * SECTOR_BYTES) && quedan > 0U) {
            uint32_t n = (quedan > SECTOR_BYTES) ? SECTOR_BYTES : quedan;

            spi_flash_read(base + hecho, s_buf, SECTOR_BYTES);
            if (!fmc->escribe(addr, s_buf, n)) { return CARGA_ERROR_GRABAR; }

            addr       += n;
            quedan     -= n;
            s_grabados += n;
            hecho      += SECTOR_BYTES;
        }

        cluster = fat_siguiente(cluster);
        if (quedan > 0U && (cluster < 2U || cluster >= 0x0FF8U)) {
            return CARGA_FICHERO_ROTO;   /* la cadena se acaba antes que el fichero */
        }
    }
    return CARGA_GRABADA;
}

/*
 * LAS TRES PRUEBAS, Y EN SU ORDEN.
 *
 * 1. LA PILA INICIAL EN EL TCM.  Es la que viene del gestor de fabrica
 *    -tres instrucciones suyas: "ubfx r0,r0,#17,#12" y comparar con
 *    0x800, o sea exigir 0x10000000..0x1001FFFF- y se queda porque es la
 *    mas barata que existe de "aqui dentro hay algo con pinta de
 *    aplicacion".  Una flash recien borrada (0xFFFFFFFF) no la pasa.
 *
 * 2. LA CABECERA.  Magia "DSDR" y una longitud que tenga sentido.  Separa
 *    "no hay imagen" de "hay una imagen y esta mal", que desde la pantalla
 *    del cargador son dos cosas muy distintas de arreglar.
 *
 * 3. EL CRC32 DE LA IMAGEN ENTERA.  Es la que no tenia el gestor viejo y
 *    la que de verdad importa: con ocho bytes constantes, una imagen
 *    grabada a medias arrancaba.
 *
 * EL CRC SE CALCULA CON EL CAMPO DEL PROPIO CRC CONTADO COMO CERO, porque
 * si no es la pescadilla: el valor que hay que meter en la cabecera
 * depende de la cabecera.  La alternativa -dejar la cabecera fuera del
 * calculo- se descarto a proposito: dejaria sin cubrir el vector de
 * interrupciones, que son los primeros 428 bytes, o sea justo la parte
 * cuya corrupcion hace que la radio no arranque de forma entendible.
 *
 * Se lee de la flash en trozos de 64 bytes en vez de de un cast al puntero
 * porque este modulo no toca la flash directamente NUNCA: todo pasa por
 * carga_fmc_t, y eso es lo que permite que el banco del PC ejecute esta
 * misma funcion contra una flash simulada.
 */
static uint32_t crc_trozo(const carga_fmc_t *fmc, uint32_t desde,
                          uint32_t hasta, uint32_t crc)
{
    uint8_t t[64];

    while (desde < hasta) {
        uint32_t n = hasta - desde;
        if (n > sizeof t) { n = (uint32_t)sizeof t; }
        fmc->lee(CARGA_APP_BASE + desde, t, n);
        crc = cargador_crc32(crc, t, n);
        desde += n;
    }
    return crc;
}

uint8_t cargador_imagen_vale(const carga_fmc_t *fmc, carga_r_t *porque)
{
    static const uint8_t k_ceros[4] = { 0U, 0U, 0U, 0U };
    uint8_t  v[CARGA_CAB_BYTES];
    uint32_t sp, longitud, crc;

    fmc->lee(CARGA_APP_BASE, v, 4U);
    sp = le32(v);
    if (((sp >> 17) & 0x0FFFU) != 0x800U) {
        *porque = CARGA_SIN_APP;
        return 0U;
    }

    fmc->lee(CARGA_CAB_ADDR, v, CARGA_CAB_BYTES);
    if (le32(&v[0]) != CARGA_CAB_MAGIA) {
        *porque = CARGA_SIN_CABECERA;
        return 0U;
    }

    longitud = le32(&v[4]);
    /* Tiene que llegar por lo menos hasta el final de la propia cabecera,
     * no pasarse de la region, y ser multiplo de 4 -el enlazador alinea
     * todas las secciones a 4, asi que una longitud impar significa que
     * quien escribio la cabecera se equivoco-. */
    if (longitud < (CARGA_CAB_OFF + CARGA_CAB_BYTES)
     || longitud > CARGA_TAM_MAX
     || (longitud & 3U) != 0U) {
        *porque = CARGA_CAB_RARA;
        return 0U;
    }

    crc = 0xFFFFFFFFU;
    crc = crc_trozo(fmc, 0U, CARGA_CAB_OFF + 8U, crc);
    crc = cargador_crc32(crc, k_ceros, 4U);
    crc = crc_trozo(fmc, CARGA_CAB_OFF + 12U, longitud, crc);

    if (crc != le32(&v[8])) {
        *porque = CARGA_CRC_MALO;
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
     * leer el volumen y se recupera copiando un update.bin bueno, sin
     * SWD. Si esto se moviera detras de la comprobacion, una imagen mala
     * seria un ladrillo.
     */
    if (!geo()) {
        /* sin volumen legible no hay nada que grabar, pero la imagen que
         * ya este dentro puede ser perfectamente buena: se comprueba. */
        if (cargador_imagen_vale(fmc, &porque)) { return CARGA_ARRANCA; }
        return CARGA_NO_HAY_VOLUMEN;
    }

    tam = cargador_busca_update(&cluster);
    if (tam > CARGA_TAM_MAX) {
        /*
         * MAS DE 0x50000: el gestor cierra el fichero y salta DIRECTO a la
         * comprobacion (0x800a68e: f_close y "b 0x800a7ca"), saltandose el
         * f_unlink.  Aqui se hace lo mismo, y a proposito: un update.bin
         * que no cabe se queda en el disco y se vuelve a mirar -y a
         * descartar- en cada arranque.  Si se borrara, el dueno perderia
         * el fichero sin enterarse de por que.
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

/*
 * EL VEREDICTO, EN EL IDIOMA DE LA RADIO - 06/10/2026.
 *
 * Esto sale en la pantalla de MODO ACTUALIZACION y era la otra tanda de
 * cadenas de interfaz que no pasaba por tr(). Y es de las peores donde
 * dejarlo sin traducir: son exactamente las frases que lee alguien cuando
 * algo no le ha funcionado, y el que abrio el issue de "the loader doesn't
 * see update.bin" escribe en ingles.
 */
const char *cargador_porque_txt(carga_r_t r)
{
    switch (r) {
    case CARGA_ARRANCA:         return tr("arranca", "boots");
    case CARGA_GRABADA:         return tr("grabada y arranca", "written, booting");
    case CARGA_SIN_APP:         return tr("no hay aplicacion", "no application");
    case CARGA_SIN_CABECERA:    return tr("sin cabecera: ahi no hay imagen",
                                          "no header: there is no image there");
    case CARGA_CAB_RARA:        return tr("cabecera con longitud imposible",
                                          "header with an impossible length");
    case CARGA_CRC_MALO:        return tr("imagen incompleta o corrompida",
                                          "incomplete or corrupted image");
    case CARGA_NO_HAY_VOLUMEN:  return tr("no se lee el volumen",
                                          "the volume cannot be read");
    case CARGA_FICHERO_GRANDE:  return tr("update.bin no cabe en la flash",
                                          "update.bin does not fit in flash");
    case CARGA_FICHERO_ROTO:    return tr("la cadena del fichero no cuadra",
                                          "the file chain does not add up");
    case CARGA_ERROR_BORRAR:    return tr("no se pudo borrar", "could not erase");
    case CARGA_ERROR_GRABAR:    return tr("no se pudo grabar", "could not write");
    default:                    return "?";
    }
}

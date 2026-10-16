#include "mapa_flash.h"
#include "zona_alta.h"
#include "spi_flash.h"
#include "idioma.h"

/*
 * Ver mapa_flash.h para el porque de todo esto. Aqui solo estan las
 * cuentas.
 */

#define MARCA0 'D'
#define MARCA1 'M'
#define MARCA2 'A'
#define MARCA3 'P'
#define VERSION 1U

static uint32_t s_total;          /* bytes del .map que se estan grabando */

static uint32_t u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void pon32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    b[2] = (uint8_t)((v >> 16) & 0xFFU);
    b[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/* Donde empieza el .map de verdad, pasada la cabecera. */
static uint32_t cuerpo(void) { return ZA_MAPA + (uint32_t)MAPA_FLASH_CAB; }

uint32_t mapa_flash_donde(void) { return cuerpo(); }

uint32_t mapa_flash_sitio(void)
{
    return ZA_MAPA_BYTES - (uint32_t)MAPA_FLASH_CAB;
}

const char *mapa_flash_porque_txt(mapa_flash_r_t r)
{
    switch (r) {
    case MAPA_FLASH_NADA:      return tr("no hay mapa guardado",
                                         "no map stored");
    case MAPA_FLASH_OK:        return tr("grabado", "stored");
    case MAPA_FLASH_ESTORBA:   return tr("la base de datos ocupa su sitio",
                                         "the database is in its place");
    case MAPA_FLASH_GRANDE:    return tr("no cabe en la flash",
                                         "too big for the flash");
    case MAPA_FLASH_NO_ESTA:   return tr("el fichero no esta en el disco",
                                         "the file is not on the disk");
    case MAPA_FLASH_COPIA_MAL: return tr("la copia fallo",
                                         "the copy failed");
    case MAPA_FLASH_SUMA_MAL:  return tr("copiado, pero no cuadra",
                                         "copied, but it does not add up");
    case MAPA_FLASH_NO_BORRA:  return tr("grabado, pero no se pudo borrar del disco",
                                         "stored, but it could not be deleted from the disk");
    default: break;
    }
    return tr("no se sabe", "unknown");
}

uint8_t mapa_flash_hay(uint32_t *n)
{
    uint8_t b[16];
    uint32_t len;

    spi_flash_read(ZA_MAPA, b, (uint32_t)sizeof b);
    if (b[0] != (uint8_t)MARCA0 || b[1] != (uint8_t)MARCA1
     || b[2] != (uint8_t)MARCA2 || b[3] != (uint8_t)MARCA3) { return 0U; }
    if (b[4] != (uint8_t)VERSION) { return 0U; }

    len = u32(&b[8]);
    /*
     * Y QUE LA LONGITUD TENGA SENTIDO antes de que nadie la use para leer.
     * Un hueco a medio borrar da 0xFFFFFFFF aqui, y eso mandaria a leer
     * cuatro gigas desde el final del chip.
     */
    if (len == 0UL || len > mapa_flash_sitio()) { return 0U; }
    if (n != 0) { *n = len; }
    return 1U;
}

/* La suma de lo que hay grabado, leyendo la flash a trozos. */
static uint32_t suma_grabada(uint32_t n)
{
    uint8_t  buf[256];
    uint32_t p = 0UL, v = ZA_SUMA_INICIO;

    while (p < n) {
        uint32_t k = n - p;

        if (k > (uint32_t)sizeof buf) { k = (uint32_t)sizeof buf; }
        spi_flash_read(cuerpo() + p, buf, k);
        v = zona_alta_suma(buf, k, v);
        p += k;
    }
    return v;
}

mapa_bin_r_t mapa_flash_carga(uint8_t *pozo, uint32_t cap)
{
    uint32_t n = 0UL;
    uint8_t  b[16];

    if (!mapa_flash_hay(&n)) { return MAPA_BIN_NADA; }
    /*
     * Y AQUI YA NO SE MIRA SI CABE EN EL POZO, porque ya no se copia al
     * pozo: el v2 mide 70 kB y prestados hay 45. Esa guardia habria
     * rechazado todos los mapas nuevos con "no cabe en la RAM", que
     * ademas es mentira - el que tiene que caber es el nivel x1, y de eso
     * opina mapa_bin_abre_flash()-.
     */
    (void)cap;

    /*
     * LA SUMA DE LA CABECERA, ANTES DE DARSELO A NADIE. Esto no comprueba
     * que el .map fuera bueno -de eso opina mapa_bin_pon(), que tiene su
     * propia suma dentro del fichero- sino que los bytes siguen siendo los
     * que se grabaron. Son dos preguntas distintas: una es "el fichero
     * valia" y la otra es "la flash se ha estropeado desde entonces".
     */
    spi_flash_read(ZA_MAPA, b, (uint32_t)sizeof b);
    if (suma_grabada(n) != u32(&b[12])) { return MAPA_BIN_SUMA_MAL; }

    /*
     * SE ABRE LEYENDO DE LA FLASH, NO COPIANDOLO A LA RAM - 12/10/2026.
     *
     * El .map v2 mide unos 70 kB -tres niveles- y el pozo prestado son
     * 45. Copiarlo entero dejo de ser posible el dia que entraron los
     * niveles, y el banco lo dijo claro: "el fichero no tiene trazos",
     * que es lo que contesta el lector cuando le das los primeros 64 kB
     * de un fichero de 70.
     *
     * mapa_bin_abre_flash() hace lo mismo en tres pasadas y sin buffer
     * grande: la suma a trozos, la cabecera, y el x1 al buffer. De paso
     * se queda con el lector y la direccion, que es lo que le hace falta
     * para traer las ventanas del x4 y del x8.
     *
     * `pozo` ya no se usa aqui y se queda en la firma a proposito: quien
     * llama sigue teniendo que tener RAM prestada para el camino de
     * grabar, que si necesita el fichero entero.
     */
    (void)pozo;
    return mapa_bin_abre_flash(spi_flash_read, cuerpo(), n);
}

mapa_flash_r_t mapa_flash_graba_abre(const char name8[8])
{
    uint32_t n = 0UL;

    s_total = 0UL;

    if (!spi_flash_fichero_tam(name8, "MAP", &n) || n == 0UL) {
        return MAPA_FLASH_NO_ESTA;
    }
    if (n > mapa_flash_sitio()) { return MAPA_FLASH_GRANDE; }

    /*
     * Y QUE NO HAYA NADIE VIVIENDO AHI. Esto se pregunta ANTES de borrar
     * un solo bloque: si la base de datos llega hasta aqui, no se toca
     * nada y se dice por que. Perder los aviones por meter un mapa seria
     * un intercambio malisimo, y uno sin vuelta atras.
     */
    if (zona_alta_estorba()) { return MAPA_FLASH_ESTORBA; }

    /*
     * LA CABECERA SE BORRA LA PRIMERA. A partir de aqui no hay mapa
     * guardado hasta que la copia termine y se verifique. Si esto se corta
     * por la mitad, al arrancar no habra cabecera y saldra el mapa de
     * serie - que es lo correcto: medio mapa no es un mapa-.
     */
    spi_flash_sector_erase_4k(ZA_MAPA);

    if (!spi_flash_restaura_abre(name8, "MAP", cuerpo(), n,
                                 za_base(), za_tope())) {
        return MAPA_FLASH_COPIA_MAL;
    }
    s_total = n;
    return MAPA_FLASH_OK;
}

int8_t   mapa_flash_graba_paso(void)   { return spi_flash_volcado_paso(); }
uint32_t mapa_flash_graba_hechos(void) { return spi_flash_volcado_hechos(); }
uint32_t mapa_flash_graba_total(void)  { return s_total; }

mapa_flash_r_t mapa_flash_graba_cierra(const char name8[8],
                                       uint8_t *pozo, uint32_t cap)
{
    uint8_t  b[16];
    uint32_t v;

    if (s_total == 0UL) { return MAPA_FLASH_COPIA_MAL; }
    /*
     * YA NO HACE FALTA QUE EL FICHERO QUEPA EN EL POZO. El v2 mide 70 kB
     * y prestados hay 45; la comprobacion se hace leyendo de la flash a
     * trozos, igual que la carga del arranque. La guardia de antes
     * rechazaba todos los mapas nuevos con "copiado, pero no cuadra",
     * que ademas mandaba a mirar a los bytes cuando el problema era el
     * buffer.
     */
    (void)pozo; (void)cap;

    /*
     * LA COMPROBACION DE VERDAD: leer lo grabado y pasarselo al lector del
     * .map. Su suma esta DENTRO del fichero, asi que si cuadra es que los
     * bytes han llegado enteros desde el disco - por el USB, por el
     * volumen FAT y por los bloques de borrado y grabacion, que es donde
     * de verdad se pueden estropear-.
     */
    if (mapa_bin_abre_flash(spi_flash_read, cuerpo(), s_total) != MAPA_BIN_OK) {
        return MAPA_FLASH_SUMA_MAL;
    }

    /*
     * Y SE GUARDA EL LARGO DE VERDAD, no el que decia el directorio del
     * disco. Un fichero de 7.195 bytes ocupa dos bloques de 4 kB, y segun
     * quien lo haya escrito el directorio puede decir 7.195 o puede decir
     * 8.192. Guardar el relleno no rompe nada -el lector ya lo ignora-
     * pero hace que la suma de la cabecera cubra bytes que no son del
     * mapa, y entonces un relleno distinto el dia de manana parece una
     * flash estropeada. La tabla del propio .map no tiene esa duda.
     */
    s_total = mapa_bin_largo();

    /* Y ahora si, la cabecera, con la suma de lo que ha quedado. */
    v = suma_grabada(s_total);
    b[0] = (uint8_t)MARCA0; b[1] = (uint8_t)MARCA1;
    b[2] = (uint8_t)MARCA2; b[3] = (uint8_t)MARCA3;
    b[4] = (uint8_t)VERSION;
    b[5] = 0U; b[6] = 0U; b[7] = 0U;
    pon32(&b[8], s_total);
    pon32(&b[12], v);
    spi_flash_page_program(ZA_MAPA, b, (uint32_t)sizeof b);

    /* Y comprobar que la cabecera tambien ha entrado. Programar es
     * escribir ceros sobre unos: un bit que no baja no da error, se queda
     * arriba y en silencio. */
    if (!mapa_flash_hay(0)) { return MAPA_FLASH_SUMA_MAL; }

    /* LO ULTIMO DE TODO. Hasta esta linea el disco tenia la unica copia. */
    if (!spi_flash_fichero_borra(name8, "MAP")) {
        return MAPA_FLASH_NO_BORRA;
    }
    return MAPA_FLASH_OK;
}

uint8_t mapa_flash_borra(void)
{
    if (!mapa_flash_hay(0)) { return 0U; }
    spi_flash_sector_erase_4k(ZA_MAPA);
    return (uint8_t)(mapa_flash_hay(0) == 0U);
}

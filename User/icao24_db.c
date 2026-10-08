#include "icao24_db.h"

#if HFDL_ICAO24_DB

#include "spi_flash.h"
#include "zona_alta.h"
/*
 * LA BASE VIVE EN LA ZONA ALTA, NO EN EL DISCO USB. 25/09/2026.
 *
 * *** Idea del dueno: "tenemos 1 mega no accesible via usb, quizas
 * podriamos usarlo para guardar ahi cosas". ***
 *
 * El proyecto padre guarda ICAO24.BIN en el volumen FAT y lo lee con un
 * spi_flash_ro.c que recorre la cadena de clusters. Aqui vive en la mitad
 * alta del chip, la que el modo USB no ensena, y se lee con una suma.
 *
 * LO QUE ESO QUITA DE EN MEDIO, que no es poco:
 *   - la FAT entera: ni directorio, ni cadena, ni fragmentacion;
 *   - el relleno de 4.096 bytes con el que su fichero empieza, que existe
 *     SOLO porque el guardado de CONFIG.CSV borra el bloque donde caeria el
 *     principio del fichero en el volumen. Fuera del volumen no hay nadie
 *     que borre nada;
 *   - y el megabyte del disco, que se queda entero para el usuario.
 *
 * QUE HABIA AHI. Una fuente china HZK24 de 487 kB que el firmware de
 * fabrica usaba para pintar los nombres de canal del CHANNEL.CSV en chino
 * (lo recordo el dueno; encaja con que sea el juego GB2312 completo). El
 * nuestro no la usa y el gestor de arranque tampoco - se borro y la radio
 * arranca igual. Hay un volcado guardado por si algun dia se vuelve al
 * firmware de serie.
 *
 * LO QUE NO SE TOCABA, Y POR QUE YA SE TOCA - 02/10/2026.
 *
 * Aqui ponia: "de 0x180000 arriba no se toca. Ese medio mega borrado son
 * exactamente dos imagenes de 256 kB y sigue siendo el candidato a area de
 * preparacion del gestor de arranque. Escribir ahi y acertar mal deja la
 * radio sin forma de recibir la siguiente version."
 *
 * Era prudente y era correcto MIENTRAS el gestor de arranque fuera el de
 * fabrica, porque de un programa que no escribimos nosotros no se puede
 * saber donde escribe: solo se puede medir que hoy no escribe, que no es
 * lo mismo.
 *
 * El cargador de la rama "reload" es nuestro y no usa area de preparacion
 * ninguna: lee update.bin del volumen FAT y lo programa directo en la flash
 * interna, sin pasar por la SPI. Asi que ese medio mega deja de ser "el
 * sitio donde quiza escribe alguien a quien no conocemos" y pasa a ser
 * sitio nuestro. ZA_TOPE (User/zona_alta.h) sube a 0x200000.
 *
 * Y NO, LAS IMAGENES NO VIVEN AHI. Lo parecia -el desaparecido
 * img_store.h hablaba de "la
 * cola de bloques borrados por encima del sistema de ficheros"- y por eso
 * casi no lo tocamos. Pero las fotos de SSTV y WEFAX se guardan como BMP en
 * el VOLUMEN FAT, el disco que se ve por USB (gsv_abre(), en main.c). De la
 * zona alta solo tiraba "Probar imagenes", el diagnostico, y ese a partir
 * de ahora dira que no tiene sitio. Es un diagnostico; la base de aviones
 * es la radio.
 */
/*
 * DONDE ESTA LA BASE: LO DICE EL DIRECTORIO, NO ESTE FICHERO - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "creo que para futuras versiones estaria
 * bien fusionarlos, para solo tener que grabar un .bin cada vez". ***
 *
 * Esta direccion era fija, y ademas el limite de abajo (I24_TOPE) decia
 * que la base podia llegar hasta 0x180000 - o sea, por encima de donde
 * despues se puso la de emisoras-. Dos sitios con el reparto escrito a
 * mano, y el dia que cambio uno el otro se quedo atras.
 *
 * Ahora las dos cosas salen de zona_alta_donde(): si hay un BD.BIN
 * cargado, del directorio; si no lo hay, de la distribucion vieja, para
 * que una radio que ya tenia sus datos siga funcionando tras actualizar.
 */
/* La distribucion vieja empezaba donde empieza la zona alta, que hasta el
 * 02/10/2026 era 0x101000 a pelo. Ahora sale de za_base(), que en el chip
 * de fabrica da ese mismo numero: las radios que tengan datos cargados del
 * reparto viejo siguen encontrandolos. */
#define I24_BASE_VIEJA za_base()
/*
 * Hasta donde puede llegar la base. Por arriba manda 0x180000, que es donde
 * empieza el medio mega intocable (ver arriba). Son 520.192 bytes: a cinco
 * por avion, unos 100.000 aviones, de sobra para todos los reactores que
 * vuelan HFDL.
 *
 * Esta cuenta es ademas la que impide que una cabecera corrupta mande leer
 * al area del gestor de arranque: si los numeros del fichero dicen que los
 * datos acaban mas arriba de aqui, el fichero se rechaza entero.
 */
/* Solo para la distribucion vieja; con directorio manda el tamaño que
 * diga el directorio. Ver i24_limites().
 *
 * El reparto viejo daba a los aviones de 0x101000 a 0x140000, o sea
 * 258.048 bytes. Se escribe como la RESTA y no como el numero para que
 * siga diciendo lo mismo ahora que la base se calcula: lo que define el
 * reparto viejo es su tamaño, no las direcciones donde cayo. */
#define I24_TOPE_VIEJO (0x140000UL - 0x101000UL)
#include "debug_uart.h"

#define I24_HDR_LEN   16U
#define I24_PAD       4096U /* padded layout: the structure starts this far into the file (see icao24_db.h) */
#define I24_REC_LEN   5U
#define I24_NAME_MAX  32U


static uint32_t s_base;      /* file offset of the header: I24_PAD or 0 */
static uint32_t s_n_aircraft;
static uint32_t s_types_off;
static uint16_t s_n_types;
static uint8_t  s_name_len;
static uint8_t  s_state; /* 0 = not tried yet, 1 = ready, 2 = unavailable (until reboot) */
static const char *s_fail_reason; /* why s_state is 2, so icao24_db_report() can repeat it later */
static uint8_t  s_hdr[I24_HDR_LEN];  /* first 16 bytes of the file exactly as read, kept for the diagnostic below */
static uint8_t  s_hdr2[I24_HDR_LEN]; /* the 16 bytes at offset I24_PAD (where a padded file has its header) */
static uint8_t  s_hdr_valid;
/* A cero hasta que i24_limites() los rellene: I24_BASE_VIEJA ya no es una
 * constante -sale de za_base(), que mide el chip- y un inicializador
 * estatico no puede llamar a una funcion. Se ponen en i24_open(), que es
 * quien llama a i24_limites() antes de leer nada. */
static uint32_t s_zona_base;   /* donde empieza la seccion */
static uint32_t s_zona_tope;   /* cuanto puede ocupar */

/*
 * Pregunta al directorio de la zona alta donde vive la base y cuanto mide.
 * Sin directorio, la distribucion vieja. Se llama una vez, en i24_open().
 */
static void i24_limites(void)
{
    uint32_t addr = I24_BASE_VIEJA, tam = 0U;

    if (zona_alta_donde(ZA_TIPO_AVIONES, &addr, &tam)) {
        s_zona_base = addr;
        s_zona_tope = (tam != 0U) ? tam : I24_TOPE_VIEJO;
    } else {
        s_zona_base = I24_BASE_VIEJA;
        s_zona_tope = I24_TOPE_VIEJO;
    }
}

/* debug_print_hex32() sends its digits straight to the USART (PA9) and NOT to the USB serial
 * mirror, so over USB it shows "0x" and nothing else. These helpers format into a buffer and go
 * through debug_print(), which does reach USB. */
static const char k_hex[] = "0123456789ABCDEF";

static void i24_print_hexbytes(const char *label, const uint8_t *b, uint32_t n)
{
    char line[3U * 16U + 2U];
    uint32_t i;
    (void)line; /* solo la consume debug_print, apagado en esta radio */
    uint32_t p = 0U;

    for (i = 0U; (i < n) && (i < 16U); i++) {
        line[p++] = k_hex[b[i] >> 4];
        line[p++] = k_hex[b[i] & 0xFU];
        line[p++] = ' ';
    }
    line[p++] = '\n';
    line[p] = '\0';
    debug_print(label);
    debug_print(line);
}

/* signed decimal, e.g. "-5" */
static void i24_print_signed(const char *label, int32_t v)
{
    char s[14];
    uint32_t u = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    (void)s; /* idem */
    uint32_t p = 12U;
    s[13] = '\0';
    s[12] = '\n';
    do {
        s[--p] = (char)('0' + (u % 10U));
        u /= 10U;
    } while ((u > 0U) && (p > 1U));
    if (v < 0) {
        s[--p] = '-';
    }
    debug_print(label);
    debug_print(&s[p]);
}

/* Looks for the ICAO24.BIN magic ('I','2','4','B') in the 4 KB of flash around where the file
 * should begin, 1 KB before to 3 KB after, and prints its offset relative to that start. Answers
 * "is the file there but shifted?" - 0 would mean it is exactly where it should be, so the fault
 * lies elsewhere. Failure path only. */
static void i24_scan_for_magic(void)
{
    uint32_t base = s_zona_base;
    int32_t off;
    uint8_t w[68];

    for (off = -1024; off < 5120; off += 64) {
        uint32_t i;
        if ((int32_t)base + off < 0) {
            continue;
        }
        spi_flash_read((uint32_t)((int32_t)base + off), w, sizeof(w));
        for (i = 0U; i < 64U; i++) {
            if ((w[i] == (uint8_t)'I') && (w[i + 1U] == (uint8_t)'2') && (w[i + 2U] == (uint8_t)'4') && (w[i + 3U] == (uint8_t)'B')) {
                i24_print_signed("icao24_db:   magic I24B found at byte offset (0 = where the file should start): ", off + (int32_t)i);
                return;
            }
        }
    }
    debug_print("icao24_db:   magic I24B not found within -1024..+5120 bytes of where the file should start\n");
}

/* "icao24_db:   LBA <n>: xx xx ..." - the first 16 bytes of one 512-byte sector as the firmware reads
 * them straight from the chip. */
static void i24_print_sector(uint32_t lba)
{
    char label[40];
    uint8_t b[16];
    uint32_t p = 0U;
    uint32_t i;
    uint32_t div = 10000U;
    static const char pre[] = "icao24_db:   LBA ";

    for (i = 0U; pre[i] != '\0'; i++) { label[p++] = pre[i]; }
    for (; div > 0U; div /= 10U) {
        uint32_t d = (lba / div) % 10U;
        if ((d != 0U) || (p > (sizeof(pre) - 1U)) || (div == 1U)) { label[p++] = (char)('0' + d); }
    }
    label[p++] = ':';
    label[p++] = ' ';
    label[p] = '\0';
    spi_flash_read(lba * 512U, b, sizeof(b));
    i24_print_hexbytes(label, b, sizeof(b));
}

/* Failure-path probe of the whole volume as the FIRMWARE sees the chip, to compare with what the PC
 * sees through the bootloader's USB volume (dd + xxd). 1) The boot sector's own parameters - the ones
 * the PC's FAT driver uses to place a file. 2) The first bytes of the sectors around where the file
 * should start. 3) Every sector start of the 2 MiB chip searched for the ICAO24.BIN magic, so a file
 * that was written but not where it is expected still turns up. Reads only. */
static void i24_diag_volume(void)
{
    uint8_t b[36];
    uint32_t s;
    uint32_t hits = 0U;
    uint32_t expect_lba = s_zona_base / 512U;
    (void)expect_lba; /* idem */
    static const uint16_t k_lba[] = { 0U, 4U, 16U, 48U, 49U, 50U, 51U, 52U };

    spi_flash_read(0U, b, sizeof(b));
    debug_print("icao24_db:   boot sector (sector 0) as the firmware reads it:\n");
    debug_print_dec("icao24_db:     bytes per sector   ", (uint32_t)(b[11] | ((uint32_t)b[12] << 8)));
    debug_print_dec("icao24_db:     sectors per cluster", b[13]);
    debug_print_dec("icao24_db:     reserved sectors   ", (uint32_t)(b[14] | ((uint32_t)b[15] << 8)));
    debug_print_dec("icao24_db:     FAT copies         ", b[16]);
    debug_print_dec("icao24_db:     root dir entries   ", (uint32_t)(b[17] | ((uint32_t)b[18] << 8)));
    debug_print_dec("icao24_db:     total sectors      ", (uint32_t)(b[19] | ((uint32_t)b[20] << 8)));
    debug_print_dec("icao24_db:     sectors per FAT    ", (uint32_t)(b[22] | ((uint32_t)b[23] << 8)));
    debug_print_dec("icao24_db:   sector where the file should start (LBA)", expect_lba);
    for (s = 0U; s < (sizeof(k_lba) / sizeof(k_lba[0])); s++) {
        i24_print_sector(k_lba[s]);
    }
    for (s = 0U; (s < 4096U) && (hits < 3U); s++) {
        uint8_t m[4];
        spi_flash_read(s * 512U, m, sizeof(m));
        if ((m[0] == (uint8_t)'I') && (m[1] == (uint8_t)'2') && (m[2] == (uint8_t)'4') && (m[3] == (uint8_t)'B')) {
            debug_print_dec("icao24_db:   magic I24B at the start of LBA", s);
            hits++;
        }
    }
    if (hits == 0U) {
        debug_print("icao24_db:   magic I24B at no sector start of the whole 2 MiB chip\n");
    }
}

static bool i24_fail(const char *why)
{
    s_fail_reason = why;
    debug_print(why);
    return false;
}

static bool i24_is_header(const uint8_t *h)
{
    return (h[0] == (uint8_t)'I') && (h[1] == (uint8_t)'2') && (h[2] == (uint8_t)'4') && (h[3] == (uint8_t)'B') && (h[4] == 1U);
}

static bool i24_open(void)
{
    uint8_t h[I24_HDR_LEN];

    i24_limites();
    spi_flash_read(s_zona_base, h, I24_HDR_LEN);
    {
        uint32_t k;
        for (k = 0U; k < I24_HDR_LEN; k++) {
            s_hdr[k] = h[k];
        }
        s_hdr_valid = 1U;
    }
    /*
     * EL DE DESPLAZAMIENTO 0 PRIMERO. Cambiado por nosotros el 27/09/2026.
     *
     * Venia al reves: miraba el de 4096 -el formato con relleno de su
     * script- y solo si ese no valia bajaba al 0. El problema es que
     * cargar un fichero NO borra la zona entera: spi_flash escribe solo
     * los bloques que ocupa. Asi que si alguna vez hubo alli un fichero
     * con relleno y despues se carga uno pequeno de tools/icao24.py -que
     * escribe la cabecera en el 0 y nunca en el 4096-, la cabecera vieja
     * del 4096 sigue estando, sigue validando, y el lector contesta con
     * la base ANTERIOR: modelos equivocados y ni un error.
     *
     * Reproducido en el simulador: los tres aviones de prueba salian
     * "C172 Cessna 172" despues de cargar el fichero bueno.
     *
     * Se mira el 0 primero porque es lo que escribe nuestro generador, y
     * el 4096 se queda como alternativa para los ficheros del proyecto
     * padre. Y si el de 4096 valida pero luego no cuadra, se vuelve a
     * probar el 0 en vez de rendirse.
     */
    s_base = 0U;
    if (!i24_is_header(h)) {
        s_base = I24_PAD;
        spi_flash_read(s_zona_base + I24_PAD, s_hdr2, I24_HDR_LEN);
        if (!i24_is_header(s_hdr2)) {
            return i24_fail("icao24_db: ICAO24.BIN has the wrong magic/version (a padded file has it at byte 4096, an old one at 0)\n");
        }
        {
            uint32_t k;
            for (k = 0U; k < I24_HDR_LEN; k++) { h[k] = s_hdr2[k]; }
        }
    }
    s_name_len   = h[5];
    s_n_types    = (uint16_t)(h[6] | ((uint16_t)h[7] << 8));
    s_n_aircraft = (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);
    s_types_off  = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);

    if ((s_name_len == 0U) || (s_name_len > I24_NAME_MAX) || (s_n_aircraft == 0U)
        || (s_n_aircraft > 0x1000000U)
        || (s_types_off != (I24_HDR_LEN + (I24_REC_LEN * s_n_aircraft)))
        || ((s_base + s_types_off + ((uint32_t)s_n_types * s_name_len)) > s_zona_tope)) {
        return i24_fail("icao24_db: la cabecera de ICAO24.BIN se sale de la zona alta\n");
    }
    return true;
}

void icao24_db_report(void)
{
    if (spi_flash_async_save_poll() == SPI_FLASH_ASYNC_BUSY) {
        return; /* never read the chip while it programs/erases */
    }
    if (s_state == 0U) {
        s_state = i24_open() ? 1U : 2U;
    }
    if (s_state == 1U) {
        debug_print("icao24_db: ICAO24.BIN ready\n");
        debug_print_dec("icao24_db:   aircraft", s_n_aircraft);
        debug_print_dec("icao24_db:   type names", s_n_types);
        debug_print_dec("icao24_db:   file size (bytes)", s_file.size);
        debug_print_dec("icao24_db:   fragments (max 12)", s_file.extent_count);
        debug_print_dec("icao24_db:   header at byte", s_base);
    } else {
        debug_print("icao24_db: NOT available until the next reboot. Reason:\n");
        debug_print((s_fail_reason != (const char *)0) ? s_fail_reason : "icao24_db: unknown\n");
        if (s_hdr_valid != 0U) {
            /* Where the file was found and what its first bytes really are (a valid ICAO24.BIN
             * starts 49 32 34 42 01, i.e. 'I','2','4','B',1). Typical answers: 30 30 30 30 ... =
             * ASCII digits (the PortaPack icao24.db, not the converted file); FF FF FF FF = flash
             * bytes never written (the USB copy did not reach the chip); 00 00 ... = zeros. If the
             * magic turns up at a non-zero offset, the data is shifted relative to the file table. */
            debug_print_dec("icao24_db:   file found: size (bytes)", s_file.size);
            debug_print_dec("icao24_db:   file found: first cluster", (uint32_t)s_file.extents[0].first_cluster);
            debug_print_dec("icao24_db:   file found: fragments", s_file.extent_count);
            i24_print_hexbytes("icao24_db:   first 16 bytes (offset 0):    ", s_hdr, I24_HDR_LEN);
            i24_print_hexbytes("icao24_db:   16 bytes at offset 4096:      ", s_hdr2, I24_HDR_LEN);
            i24_scan_for_magic();
            i24_diag_volume();
        }
    }
}

bool icao24_db_lookup(uint32_t icao24, char *out, uint32_t out_len)
{
    uint32_t lo;
    uint32_t hi;

    if ((out == (char *)0) || (out_len == 0U)) {
        return false;
    }
    out[0] = '\0';

    if (s_state == 2U) {
        return false;
    }
    /*
     * NUESTRO EQUIVALENTE. El proyecto padre tiene un
     * spi_flash_async_save_in_progress(); aqui el estado se pregunta con
     * spi_flash_async_save_poll(), que devuelve BUSY mientras graba. Mismo
     * motivo: el chip ignora las lecturas mientras programa o borra.
     */
    if (spi_flash_async_save_poll() == SPI_FLASH_ASYNC_BUSY) {
        return false;
    }
    if (s_state == 0U) {
        s_state = i24_open() ? 1U : 2U;
        if (s_state != 1U) {
            return false;
        }
    }

    icao24 &= 0xFFFFFFU;
    lo = 0U;
    hi = s_n_aircraft; /* search [lo, hi) */
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) / 2U);
        uint8_t r[I24_REC_LEN];
        uint32_t key;

        spi_flash_read(s_zona_base + s_base + I24_HDR_LEN + (mid * I24_REC_LEN), r, I24_REC_LEN);
        key = ((uint32_t)r[0] << 16) | ((uint32_t)r[1] << 8) | (uint32_t)r[2];
        if (key == icao24) {
            uint16_t idx = (uint16_t)(r[3] | ((uint16_t)r[4] << 8));
            uint8_t name[I24_NAME_MAX];
            uint32_t n;
            uint32_t k;

            if (idx >= s_n_types) {
                return false;
            }
            spi_flash_read(s_zona_base + s_base + s_types_off + ((uint32_t)idx * s_name_len),
                           name, s_name_len);
            n = 0U;
            for (k = 0U; (k < s_name_len) && (name[k] != 0U) && (n < (out_len - 1U)); k++) {
                out[n++] = ((name[k] >= 0x20U) && (name[k] <= 0x7EU)) ? (char)name[k] : '?';
            }
            while ((n > 0U) && (out[n - 1U] == ' ')) {
                n--;
            }
            out[n] = '\0';
            return n > 0U;
        }
        if (key < icao24) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return false;
}

#else
typedef int icao24_db_translation_unit_is_empty_when_HFDL_ICAO24_DB_is_0_t; /* ISO C: no empty translation unit */
#endif

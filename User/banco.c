#include "banco.h"
#include "spi_flash.h"
#include <string.h>

/*
 * El razonamiento entero -por que cuatro bancos de quince, por que no se
 * guarda un nombre, y por que vive en un CSV de la flash externa y no en la
 * interna- esta en banco.h. Aqui solo esta el como.
 */

/* MEM.CSV y no BANCO.CSV - 26/10/2026, pedido por el usuario. Son los
 * ocho caracteres del nombre 8.3 de la FAT, rellenos con espacios. */
#define BANCO_FICH_N8  "MEM     "
#define BANCO_FICH_E3  "CSV"

static banco_mem_t s_mem[BANCO_N][BANCO_MEM_N];
/* El nombre que le haya puesto el usuario, o "" si ninguno. Ver
 * BANCO_NOM_MAX en banco.h. */
static char        s_nom[BANCO_N][BANCO_NOM_MAX];

const char *banco_nombre(uint8_t b)
{
    return (b < BANCO_N) ? s_nom[b] : "";
}

void banco_nombre_pon(uint8_t b, const char *s)
{
    uint8_t k = 0U;

    if (b >= BANCO_N) { return; }
    if (s != 0) {
        /*
         * Se copia a mano y no con strncpy por dos cosas a la vez: el
         * cero final garantizado, y que lo que entra sea IMPRIMIBLE. Este
         * texto viene de un fichero que se edita en un PC, asi que puede
         * traer tabuladores, bytes de control o media letra acentuada en
         * UTF-8 -dos bytes- que la fuente no sabe pintar. Lo que no se
         * puede pintar se queda fuera en vez de salir como un cuadradito.
         */
        while (s[k] != '\0' && k < (BANCO_NOM_MAX - 1U)) {
            char c = s[k];

            if (c < ' ' || (unsigned char)c > 126U) { break; }
            s_nom[b][k] = c;
            k++;
        }
    }
    s_nom[b][k] = '\0';
    /* Y lo que quede detras, a cero: un nombre nuevo mas corto que el de
     * antes no puede dejar cola del anterior. */
    while (k + 1U < BANCO_NOM_MAX) { k++; s_nom[b][k] = '\0'; }
}

static uint8_t vale(uint8_t b, uint8_t i)
{
    return (uint8_t)(b < BANCO_N && i < BANCO_MEM_N);
}

/*
 * ======================================================================
 * LA TABLA DE PASOS, PRESTADA - 27/10/2026, al añadir el paso de 9 kHz.
 * ======================================================================
 *
 * EL FICHERO GUARDA EL PASO EN HERCIOS, NO SU INDICE. Hasta hoy guardaba
 * el indice, y eso es una bomba de relojeria conocida: el dia que se
 * INSERTA un paso en medio de la lista -como pasa hoy con los 9 kHz de
 * onda media, que van entre los 5 y los 10- todos los indices por encima
 * se corren uno, y las memorias ya grabadas vuelven con OTRO paso. Una
 * memoria de marina guardada en 12,5 kHz volveria en 10.
 *
 * settings.h ya lo tenia resuelto para CONFIG.CSV y lo dice con todas las
 * letras: "se guarda el VALOR en Hz y no un indice crudo... asi que
 * insertar un paso nuevo no apunta en silencio un CONFIG.CSV viejo al
 * paso equivocado". MEM.CSV se habia quedado sin esa leccion.
 *
 * Y de paso el fichero se lee mejor a mano, que es como se usa: "9000"
 * dice lo que es y "5" no decia nada.
 *
 * POR QUE LA TABLA SE PRESTA Y NO SE COPIA AQUI. Este fichero no depende
 * de main.c a proposito -por eso se puede medir entero en el simulador- y
 * copiar la lista de pasos seria el mismo dato escrito dos veces, que es
 * el fallo que mas veces ha mordido en este proyecto. Main la presta una
 * vez al arrancar; el banco de pruebas hace lo mismo.
 */
/* Los de serie y el renglon de ayuda, que los pone main.c: ver
 * banco_nombres_pon() en banco.h. */
static const char *const *s_serie;
static uint8_t            s_serie_n;
static const char        *s_pista;

static const uint32_t *s_pasos_hz;
static uint8_t         s_pasos_n;

void banco_nombres_pon(const char *const *serie, uint8_t n, const char *pista)
{
    s_serie   = serie;
    s_serie_n = n;
    s_pista   = pista;
}

void banco_pasos_pon(const uint32_t *hz, uint8_t n)
{
    s_pasos_hz = hz;
    s_pasos_n  = (hz != 0) ? n : 0U;
}

/* Indice -> hercios. 0 si no hay tabla o el indice no vale: un 0 en el
 * fichero vuelve como paso 0, que es lo mismo que hacia antes un campo
 * que faltaba. */
static uint32_t paso_a_hz(uint8_t idx)
{
    if (s_pasos_hz == 0 || idx >= s_pasos_n) { return 0UL; }
    return s_pasos_hz[idx];
}

/*
 * Hercios -> indice, con la puerta de atras para los ficheros VIEJOS.
 *
 * El orden importa y no es casual: primero se mira si el numero es uno de
 * los pasos de verdad, y SOLO si no lo es se interpreta como el indice
 * que escribia el firmware anterior. Asi no hay ambigüedad posible: el
 * paso mas pequeño son 10 Hz y los indices viejos llegaban como mucho al
 * 9, o sea que ningun indice viejo puede confundirse con unos hercios.
 */
static uint8_t paso_de_hz(uint32_t v)
{
    uint8_t i;

    if (s_pasos_hz == 0 || v == 0UL) { return 0U; }
    for (i = 0U; i < s_pasos_n; i++) {
        if (s_pasos_hz[i] == v) { return i; }
    }
    if (v < (uint32_t)s_pasos_n) { return (uint8_t)v; }  /* fichero viejo */
    return 0U;
}

void banco_init(void)
{
    memset(s_mem, 0, sizeof s_mem);
    memset(s_nom, 0, sizeof s_nom);
}

uint8_t banco_lee(uint8_t b, uint8_t i, banco_mem_t *out)
{
    if (!vale(b, i) || out == 0) { return 0U; }
    if (s_mem[b][i].hz == 0UL)   { return 0U; }
    *out = s_mem[b][i];
    return 1U;
}

uint8_t banco_graba(uint8_t b, uint8_t i, const banco_mem_t *m)
{
    if (!vale(b, i) || m == 0) { return 0U; }
    s_mem[b][i] = *m;
    return 1U;
}

void banco_borra(uint8_t b, uint8_t i)
{
    if (!vale(b, i)) { return; }
    memset(&s_mem[b][i], 0, sizeof s_mem[b][i]);
}

uint8_t banco_usadas(uint8_t b)
{
    uint8_t i, n = 0U;
    if (b >= BANCO_N) { return 0U; }
    for (i = 0U; i < BANCO_MEM_N; i++) {
        if (s_mem[b][i].hz != 0UL) { n++; }
    }
    return n;
}

uint8_t banco_siguiente(uint8_t b, uint8_t desde)
{
    uint8_t k, i;

    if (b >= BANCO_N) { return 0xFFU; }
    /*
     * Se empieza en la de DESPUES y se da una vuelta entera. Con 'desde'
     * fuera de rango -que es como el barrido dice "empieza por el
     * principio"- la cuenta sigue valiendo porque el modulo la mete dentro.
     */
    for (k = 1U; k <= BANCO_MEM_N; k++) {
        i = (uint8_t)(((uint16_t)desde + k) % BANCO_MEM_N);
        if (s_mem[b][i].hz != 0UL) { return i; }
    }
    return 0xFFU;
}

/* --- el CSV ---------------------------------------------------------- */

static uint32_t pon_u32(char *b, uint32_t cap, uint32_t pos, uint32_t v)
{
    char t[11];
    uint8_t n = 0U;

    if (v == 0UL) { t[n++] = '0'; }
    while (v > 0UL) { t[n++] = (char)('0' + (v % 10UL)); v /= 10UL; }
    while (n > 0U) {
        if (pos < cap) { b[pos] = t[--n]; }
        else { n--; }
        pos++;
    }
    return pos;
}

static uint32_t pon_c(char *b, uint32_t cap, uint32_t pos, char c)
{
    if (pos < cap) { b[pos] = c; }
    return pos + 1U;
}

static uint32_t pon_s(char *b, uint32_t cap, uint32_t pos, const char *s)
{
    while (*s != '\0') { pos = pon_c(b, cap, pos, *s++); }
    return pos;
}

uint32_t banco_a_csv(char *buf, uint32_t cap)
{
    uint32_t pos = 0U;
    uint8_t  b, i;

    /*
     * La cabecera dice los campos. Es para el que abra el fichero en el PC,
     * que es media razon de que esto sea un CSV y no un binario.
     */
    pos = pon_s(buf, cap, pos,
                "banco,memoria,hz,modo,paso_hz,fil_lo,fil_hi,cw\n");
    /* El renglon de ayuda de los nombres, si main.c lo ha dado. */
    if (s_pista != 0 && s_pista[0] != '\0') {
        pos = pon_s(buf, cap, pos, "# ");
        pos = pon_s(buf, cap, pos, s_pista);
        pos = pon_c(buf, cap, pos, '\n');
    }
    /*
     * Los nombres, los primeros y solo los que tengan uno puesto. Arriba
     * del todo porque es lo que se busca cuando abres el fichero para
     * cambiarlos, y porque asi se leen antes que las memorias.
     */
    for (b = 0U; b < BANCO_N; b++) {
        const char *n = s_nom[b];

        if (n[0] == '\0') {
            /* Sin tocar: sale el de serie COMENTADO, para que se vea que
             * se puede cambiar sin cambiar nada. Ver banco_nombres_pon(). */
            if (s_serie == 0 || b >= s_serie_n || s_serie[b] == 0) { continue; }
            pos = pon_s(buf, cap, pos, "# ");
            n = s_serie[b];
        }
        pos = pon_s  (buf, cap, pos, "nombre,");
        pos = pon_u32(buf, cap, pos, (uint32_t)(b + 1U));
        pos = pon_c  (buf, cap, pos, ',');
        pos = pon_s  (buf, cap, pos, n);
        pos = pon_c  (buf, cap, pos, '\n');
    }
    for (b = 0U; b < BANCO_N; b++) {
        for (i = 0U; i < BANCO_MEM_N; i++) {
            const banco_mem_t *m = &s_mem[b][i];

            /* Las vacias NO se escriben: un fichero con sesenta renglones a
             * cero no dice mas que uno con los seis que de verdad usas, y
             * cuesta cuatro veces mas de leer y de escribir. */
            if (m->hz == 0UL) { continue; }
            pos = pon_u32(buf, cap, pos, (uint32_t)(b + 1U));
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, (uint32_t)(i + 1U));
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, m->hz);
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, (uint32_t)m->modo);
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, paso_a_hz(m->paso));
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, (uint32_t)m->fil_lo);
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, (uint32_t)m->fil_hi);
            pos = pon_c  (buf, cap, pos, ',');
            pos = pon_u32(buf, cap, pos, (uint32_t)m->cw);
            pos = pon_c  (buf, cap, pos, '\n');
        }
    }
    return pos;
}

uint8_t banco_de_csv(const char *buf, uint32_t len)
{
    uint32_t pos = 0U;
    uint8_t  leidas = 0U;

    if (buf == 0 || len == 0UL) { return 0U; }
    banco_init();

    while (pos < len) {
        uint32_t ini = pos, fin;
        uint32_t campo[8];
        uint8_t  nc = 0U;
        uint32_t k;

        while (pos < len && buf[pos] != '\n') { pos++; }
        fin = pos;
        if (pos < len) { pos++; }
        /* El '\r' de un fichero editado en Windows, igual que en
         * settings.c: este fichero se escribe con '\n' pero se EDITA en un
         * PC, y alli vuelve con dos. */
        if (fin > ini && buf[fin - 1U] == '\r') { fin--; }
        if (fin == ini) { continue; }
        /*
         * EL NOMBRE DE UN BANCO - 26/10/2026, pedido por el usuario.
         *
         *     nombre,1,Rusos
         *
         * Va ANTES de la guarda de "empieza por una cifra" porque este
         * renglon empieza por letra a proposito: asi un fichero viejo -sin
         * nombres- se lee igual, y uno nuevo abierto con un firmware viejo
         * se salta estos renglones en vez de atragantarse. El formato
         * crece sin romperse en ninguno de los dos sentidos.
         *
         * El texto se coge TAL CUAL hasta el final del renglon, comas
         * incluidas: el nombre es el ultimo campo, asi que no hay nada
         * detras con lo que confundirse, y quien escriba "Rusos, OM" no
         * tiene por que saber que la coma separa campos.
         */
        if ((fin - ini) > 7U && memcmp(&buf[ini], "nombre,", 7U) == 0) {
            uint32_t k = ini + 7U;
            uint32_t b = 0UL;

            while (k < fin && buf[k] >= '0' && buf[k] <= '9') {
                b = b * 10UL + (uint32_t)(buf[k] - '0');
                k++;
            }
            if (k < fin && buf[k] == ',') { k++; }
            if (b >= 1UL && b <= (uint32_t)BANCO_N) {
                char t[BANCO_NOM_MAX];
                uint8_t n = 0U;

                while (k < fin && n < (BANCO_NOM_MAX - 1U)) { t[n++] = buf[k++]; }
                t[n] = '\0';
                banco_nombre_pon((uint8_t)(b - 1UL), t);
            }
            continue;
        }
        /* La cabecera y cualquier renglon que no empiece por una cifra. */
        if (buf[ini] < '0' || buf[ini] > '9') { continue; }

        for (k = ini; k < fin && nc < 8U; ) {
            uint32_t v = 0UL;
            uint8_t  hay = 0U;

            while (k < fin && buf[k] >= '0' && buf[k] <= '9') {
                /* El tope corta un numero absurdo sin desbordar: la
                 * frecuencia mas alta que esta radio entiende cabe de
                 * sobra, y un fichero corrompido no puede dar la vuelta. */
                if (v < 400000000UL) { v = v * 10UL + (uint32_t)(buf[k] - '0'); }
                k++; hay = 1U;
            }
            campo[nc++] = hay ? v : 0UL;
            while (k < fin && buf[k] != ',') { k++; }
            if (k < fin) { k++; }
        }

        if (nc >= 3U && campo[0] >= 1UL && campo[0] <= (uint32_t)BANCO_N
            && campo[1] >= 1UL && campo[1] <= (uint32_t)BANCO_MEM_N
            && campo[2] != 0UL) {
            banco_mem_t m;
            uint8_t b = (uint8_t)(campo[0] - 1UL);
            uint8_t i = (uint8_t)(campo[1] - 1UL);

            memset(&m, 0, sizeof m);
            m.hz     = campo[2];
            m.modo   = (nc > 3U) ? (uint8_t)campo[3]  : 0U;
            m.paso   = (nc > 4U) ? paso_de_hz(campo[4]) : 0U;
            m.fil_lo = (nc > 5U) ? (uint16_t)campo[5] : 0U;
            m.fil_hi = (nc > 6U) ? (uint16_t)campo[6] : 0U;
            m.cw     = (nc > 7U) ? (uint16_t)campo[7] : 0U;
            s_mem[b][i] = m;
            leidas++;
        }
    }
    return leidas ? 1U : 0U;
}

/*
 * EL BUFFER DEL FICHERO LO PONE QUIEN LLAMA, Y AQUI ESTA EL PORQUE.
 *
 * EL TAMAÑO: el renglon mas largo posible son
 * "4,15,399999999,99,99,65535,65535,65535\n", 39 caracteres. Sesenta de
 * esos mas la cabecera son 2.383, y con 60 memorias de verdad se han
 * medido 1.707. Hace falta BANCO_CSV_MIN y no mas.
 *
 * EL SITIO: ni propio ni de la union compartida.
 *
 *   - Propio no cabe: la SRAM de este chip tiene menos de 2 kB libres de
 *     196.608, y la pila tiene 2.200 de margen.
 *
 *   - Y LA UNION COMPARTIDA TAMPOCO VALE, aunque lo parezca. Ahi caben de
 *     sobra 2,5 kB... pero esa union la reparten FT8, HFDL, WSPR, AIS, ALE
 *     y JTTY ademas de la cascada. Pisar la cascada son tres renglones de
 *     dibujo que se van solos; pisar las tablas de FT8 mientras decodifica
 *     es romperle el decodificador. Grabar una memoria tiene que poder
 *     hacerse en CUALQUIER modo, asi que ese sitio no sirve.
 *
 * Lo pone quien llama, y en el firmware es s_gsv_buf -el buffer con el que
 * se escriben las fotos de SSTV y las cartas de WEFAX en el disco-, que ya
 * se presta con contrato a wfx_recoloca_paso() y es de la misma clase de
 * cosa: un sitio de paso para escribir en la flash desde el bucle
 * principal. Ver donde se llama a banco_guarda().
 */

uint8_t banco_carga(char *buf, uint32_t cap)
{
    uint32_t n;

    if (buf == 0 || cap < BANCO_CSV_MIN) { banco_init(); return 0U; }
    n = spi_flash_read_file_by_name(BANCO_FICH_N8, BANCO_FICH_E3,
                                    (uint8_t *)buf, cap);
    if (n == 0UL) { banco_init(); return 0U; }
    return banco_de_csv(buf, n);
}

uint8_t banco_guarda(char *buf, uint32_t cap)
{
    uint32_t n;

    if (buf == 0 || cap < BANCO_CSV_MIN) { return 0U; }
    n = banco_a_csv(buf, cap);
    if (n > cap) { return 0U; }   /* no cabe: no se escribe a medias */
    return spi_flash_write_or_update_file(BANCO_FICH_N8, BANCO_FICH_E3,
                                          (const uint8_t *)buf, n) ? 1U : 0U;
}

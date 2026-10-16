#include "hfdl_acars.h"

#define SOH 0x01U
#define STX 0x02U
#define ETX 0x03U
#define ETB 0x17U
#define ACK 0x06U
#define NAK 0x15U

/* El bit 7 es paridad, no dato. Ver la cabecera. */
static uint8_t sinp(uint8_t b) { return (uint8_t)(b & 0x7FU); }

static uint8_t imprimible(uint8_t c)
{
    return (uint8_t)((c >= 0x20U && c < 0x7FU) ? 1U : 0U);
}

uint8_t hfdl_acars_parse(const uint8_t *buf, uint32_t len, hfdl_acars_t *out)
{
    uint32_t i, p;
    uint8_t c;

    if (buf == (const uint8_t *)0 || out == (hfdl_acars_t *)0) { return 0U; }
    /* SOH + modo + 7 de matricula + ack + 2 de etiqueta + bloque = 13 */
    if (len < 13U) { return 0U; }
    if (sinp(buf[0]) != SOH) { return 0U; }

    out->modo = (char)sinp(buf[1]);
    if (!imprimible((uint8_t)out->modo)) { return 0U; }

    /*
     * LA MATRICULA, SIN LOS PUNTOS DE RELLENO. Van delante -".A6-EOF"- y
     * son relleno a la izquierda, no parte del nombre. Quitarlos aqui y no
     * al pintar evita que cada sitio que la enseñe tenga que acordarse.
     */
    p = 0U;
    for (i = 2U; i < 9U; i++) {
        c = sinp(buf[i]);
        if (!imprimible(c)) { return 0U; }
        if (p == 0U && c == (uint8_t)'.') { continue; }
        out->matricula[p++] = (char)c;
    }
    out->matricula[p] = '\0';
    if (p == 0U) { return 0U; }

    c = sinp(buf[9]);
    if (c != ACK && c != NAK) { return 0U; }
    out->ack = (uint8_t)((c == ACK) ? 1U : 0U);

    out->etiqueta[0] = (char)sinp(buf[10]);
    out->etiqueta[1] = (char)sinp(buf[11]);
    out->etiqueta[2] = '\0';
    /*
     * La etiqueta puede llevar un byte de control -la de "_d" del enlace
     * lleva 0x7F-, asi que se admite, pero DOS bytes no imprimibles ya no
     * es una etiqueta: es que estamos leyendo donde no era.
     */
    if (!imprimible((uint8_t)out->etiqueta[0])
        && !imprimible((uint8_t)out->etiqueta[1])) { return 0U; }

    out->bloque = (char)sinp(buf[12]);

    /* El texto es opcional: sin STX, el mensaje no lo lleva. */
    out->texto[0] = '\0';
    out->texto_n = 0U;
    if (len > 13U && sinp(buf[13]) == STX) {
        p = 0U;
        for (i = 14U; i < len && p < HFDL_ACARS_TEXTO_MAX; i++) {
            c = sinp(buf[i]);
            if (c == ETX || c == ETB) { break; }
            /* Los saltos de linea del texto se guardan como espacios: el
             * renglon de la pantalla es uno y no hay donde partirlo. */
            out->texto[p++] = (char)(imprimible(c) ? c : (uint8_t)' ');
        }
        out->texto[p] = '\0';
        out->texto_n = (uint8_t)p;
    }
    return 1U;
}

/* ------------------------------------------------------------------ */
/*
 * LA POSICION DENTRO DEL TEXTO.
 *
 * Esto NO es un decodificador de ACARS por etiqueta: cada aerolinea mete
 * su posicion a su manera y copiar la tabla de libacars de memoria seria
 * exactamente el error que acaba de costar un mes -la frase de "bytes
 * 10-11" que resulto ser el byte 1-.
 *
 * Lo que si esta normalizado, y es lo que se busca aqui, son las dos
 * formas en que se escribe un par de coordenadas en texto ACARS:
 *
 *     N37254W122050     letra delante, grados y minutos pegados
 *     3725N12205W       letra detras
 *
 * con grados y minutos (DDMM o DDMMM, el ultimo digito decimas de minuto)
 * y la longitud con un digito mas. Se exige que las dos vayan PEGADAS, sin
 * nada en medio salvo un separador, porque una latitud suelta en un texto
 * no es una posicion.
 *
 * Y SE RECHAZA MUCHO A PROPOSITO. Un texto de ACARS lleva horas, niveles
 * de vuelo, combustible y temperaturas, o sea numeros a punta pala; sin
 * las comprobaciones de abajo cualquier "N" seguida de digitos pasaria por
 * una latitud. Pintar un avion donde no esta es peor que no pintarlo: un
 * punto en un mapa no dice de donde salio.
 */
static uint8_t dig(char c) { return (uint8_t)((c >= '0' && c <= '9') ? 1U : 0U); }
static int32_t num(const char *s, uint8_t n)
{
    int32_t v = 0; uint8_t i;
    for (i = 0U; i < n; i++) { v = v * 10 + (int32_t)(s[i] - '0'); }
    return v;
}

/* grados+minutos -> centesimas de grado. `dm` son los digitos sin signo,
 * `nd` cuantos son de grados y `nm` cuantos de minutos (2 o 3; con 3 el
 * ultimo son decimas de minuto). */
static int32_t gm_a_e4(const char *s, uint8_t nd, uint8_t nm, uint8_t *vale)
{
    int32_t g = num(s, nd);
    int32_t m = num(&s[nd], nm);
    int32_t md10 = (nm == 3U) ? m : (m * 10);   /* minutos x10 */

    *vale = (uint8_t)((md10 < 600) ? 1U : 0U);  /* 60 minutos no existen */
    /*
     * grados*10000 + (minutos x10) * 10000 / 600, REDONDEANDO al mas
     * cercano y no truncando: una centesima de milesima de grado son
     * once metros, y truncar los tira siempre para el mismo lado. El +300
     * es media cuenta de 600.
     */
    return g * 10000L + (md10 * 10000L + 300L) / 600L;
}

static uint8_t par_en(const char *t, uint32_t i, uint32_t n,
                      int32_t *lat, int32_t *lon);

uint8_t hfdl_acars_posicion(const hfdl_acars_t *a, int32_t *lat_e4, int32_t *lon_e4)
{
    uint32_t i, n;

    if (a == (const hfdl_acars_t *)0) { return 0U; }
    n = (uint32_t)a->texto_n;
    if (n < 10U) { return 0U; }

    for (i = 0U; i + 10U <= n; i++) {
        int32_t la, lo;
        if (par_en(a->texto, i, n, &la, &lo)) {
            if (la < -900000L || la > 900000L)   { continue; }
            if (lo < -1800000L || lo > 1800000L) { continue; }
            /* 0,0 es el golfo de Guinea y es lo que sale de un texto lleno
             * de ceros. Como posicion de un avion no se la cree nadie. */
            if (la == 0L && lo == 0L) { continue; }
            if (lat_e4) { *lat_e4 = la; }
            if (lon_e4) { *lon_e4 = lo; }
            return 1U;
        }
    }
    return 0U;
}

/* Las dos formas, probadas en la posicion `i` del texto. */
static uint8_t par_en(const char *t, uint32_t i, uint32_t n,
                      int32_t *lat, int32_t *lon)
{
    uint8_t vale = 0U, k;
    char ns, ew;
    uint32_t p;

    /* --- forma 1: N37254W122050 (letra delante) --- */
    ns = t[i];
    if (ns == 'N' || ns == 'S') {
        for (k = 4U; k <= 5U; k++) {          /* DDMM o DDMMM */
            uint8_t nm = (uint8_t)(k - 2U), j, ok = 1U;
            if (i + 1U + k >= n) { continue; }
            for (j = 0U; j < k; j++) { if (!dig(t[i + 1U + j])) { ok = 0U; } }
            if (!ok) { continue; }
            p = i + 1U + k;
            /* un separador opcional entre las dos */
            if (p < n && (t[p] == ' ' || t[p] == '/' || t[p] == ',')) { p++; }
            if (p >= n) { continue; }
            ew = t[p];
            if (ew != 'E' && ew != 'W') { continue; }
            {
                uint8_t kl = (uint8_t)(k + 1U), j2, ok2 = 1U;
                if (p + 1U + kl > n) { continue; }
                for (j2 = 0U; j2 < kl; j2++) { if (!dig(t[p + 1U + j2])) { ok2 = 0U; } }
                if (!ok2) { continue; }
                {
                    int32_t la = gm_a_e4(&t[i + 1U], 2U, nm, &vale);
                    int32_t lo;
                    uint8_t v2 = 0U;
                    if (!vale) { continue; }
                    lo = gm_a_e4(&t[p + 1U], 3U, nm, &v2);
                    if (!v2) { continue; }
                    *lat = (ns == 'S') ? -la : la;
                    *lon = (ew == 'W') ? -lo : lo;
                    return 1U;
                }
            }
        }
    }

    /* --- forma 2: 3725N12205W (letra detras) --- */
    if (dig(t[i])) {
        for (k = 4U; k <= 5U; k++) {
            uint8_t nm = (uint8_t)(k - 2U), j, ok = 1U;
            if (i + k >= n) { continue; }
            for (j = 0U; j < k; j++) { if (!dig(t[i + j])) { ok = 0U; } }
            if (!ok) { continue; }
            ns = t[i + k];
            if (ns != 'N' && ns != 'S') { continue; }
            p = i + k + 1U;
            if (p < n && (t[p] == ' ' || t[p] == '/' || t[p] == ',')) { p++; }
            {
                uint8_t kl = (uint8_t)(k + 1U), j2, ok2 = 1U;
                if (p + kl >= n) { continue; }
                for (j2 = 0U; j2 < kl; j2++) { if (!dig(t[p + j2])) { ok2 = 0U; } }
                if (!ok2) { continue; }
                ew = t[p + kl];
                if (ew != 'E' && ew != 'W') { continue; }
                {
                    int32_t la = gm_a_e4(&t[i], 2U, nm, &vale);
                    int32_t lo;
                    uint8_t v2 = 0U;
                    if (!vale) { continue; }
                    lo = gm_a_e4(&t[p], 3U, nm, &v2);
                    if (!v2) { continue; }
                    *lat = (ns == 'S') ? -la : la;
                    *lon = (ew == 'W') ? -lo : lo;
                    return 1U;
                }
            }
        }
    }
    return 0U;
}

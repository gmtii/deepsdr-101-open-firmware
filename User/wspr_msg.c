#include "wspr_msg.h"
#include <string.h>

/*
 * EL VECTOR DE SINCRONISMO. 162 posiciones, parte del protocolo.
 *
 * No es aleatorio ni se puede deducir: es una tabla fija que emisor y
 * receptor comparten. Va en el bit de MENOS peso de cada simbolo, asi que
 * un receptor que no sepa nada del mensaje puede encontrar la emision
 * entera correlando contra esto.
 */
const uint8_t k_wspr_sincro[WSPR_SIMBOLOS] = {
    1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0
};

/* Los dos polinomios del codigo convolucional, K=32 tasa 1/2. */
#define WSPR_POLY1 0xF2D05351UL
#define WSPR_POLY2 0xE4613C47UL

/* El alfabeto de los indicativos: 0-9, A-Z y el hueco. */
static const char k_abc[37] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

static int8_t letra(char c)
{
    uint8_t i;

    if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); }
    for (i = 0U; i < 37U; i++) {
        if (k_abc[i] == c) { return (int8_t)i; }
    }
    return -1;
}

/* Cuantos unos tiene un entero de 32 bits: la paridad es lo que sale del
 * codificador convolucional. */
static uint8_t paridad(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1U);
}

/* ---------------- indicativo ---------------- */

/*
 * UN INDICATIVO EN 28 BITS.
 *
 * La forma que admite WSPR es rigida a proposito: hasta dos caracteres,
 * un digito, y hasta tres letras. "EA4XYZ" cabe; "EA4XYZ/P" no. Cuando el
 * indicativo no cabe, WSPR manda en su lugar un mensaje de dos partes con
 * un resumen numerico - eso no esta aqui, y por eso wspr_empaqueta()
 * dice que no en vez de inventarse algo.
 *
 * La posicion del digito es lo que ordena todo: se alinea de forma que el
 * digito caiga SIEMPRE en el tercer hueco de seis.
 */
static uint8_t cal_a_n(const char *cal, uint32_t *out)
{
    char t[7];
    uint8_t n = 0U, i;
    int8_t v;
    uint32_t r;

    while (cal[n] != '\0') {
        if (n >= 6U) { return 0U; }
        n++;
    }
    if (n < 3U) { return 0U; }

    /* Donde esta el digito: en la posicion 1 o en la 2, nunca en otra. */
    for (i = 0U; i < 7U; i++) { t[i] = ' '; }
    t[6] = '\0';
    if (cal[1] >= '0' && cal[1] <= '9') {
        /* "E4XYZ": el digito ya esta en el hueco 2 si se mete un espacio
         * delante. */
        t[0] = ' ';
        for (i = 0U; i < n && (i + 1U) < 6U; i++) { t[i + 1U] = cal[i]; }
    } else if (n >= 3U && cal[2] >= '0' && cal[2] <= '9') {
        for (i = 0U; i < n && i < 6U; i++) { t[i] = cal[i]; }
    } else {
        return 0U;   /* el digito no esta donde WSPR lo espera */
    }

    /* Y ahora los seis huecos, con sus bases: 37,36,10,27,27,27. */
    v = letra(t[0]); if (v < 0 || v > 36) { return 0U; } r = (uint32_t)v;
    v = letra(t[1]); if (v < 0 || v > 35) { return 0U; } r = r * 36U + (uint32_t)v;
    v = letra(t[2]); if (v < 0 || v > 9)  { return 0U; } r = r * 10U + (uint32_t)v;
    v = letra(t[3]); if (v < 10 && v >= 0) { return 0U; }
    if (v < 0) { return 0U; } r = r * 27U + (uint32_t)(v - 10);
    v = letra(t[4]); if (v < 0 || (v < 10)) { return 0U; } r = r * 27U + (uint32_t)(v - 10);
    v = letra(t[5]); if (v < 0 || (v < 10)) { return 0U; } r = r * 27U + (uint32_t)(v - 10);

    *out = r;
    return 1U;
}

static void n_a_cal(uint32_t n, char *cal)
{
    char t[7];
    uint8_t i, j = 0U;

    t[5] = k_abc[10U + (n % 27U)]; n /= 27U;
    t[4] = k_abc[10U + (n % 27U)]; n /= 27U;
    t[3] = k_abc[10U + (n % 27U)]; n /= 27U;
    t[2] = k_abc[n % 10U];         n /= 10U;
    t[1] = k_abc[n % 36U];         n /= 36U;
    t[0] = k_abc[n % 37U];
    t[6] = '\0';

    for (i = 0U; i < 6U; i++) {
        if (t[i] != ' ') { break; }      /* espacios de delante fuera */
    }
    for (; i < 6U; i++) { cal[j++] = t[i]; }
    while (j > 0U && cal[j - 1U] == ' ') { j--; }   /* y los de detras */
    cal[j] = '\0';
}

/* ---------------- localizador y potencia ---------------- */

/*
 * El localizador Maidenhead de cuatro caracteres y la potencia viajan
 * juntos en 22 bits: n = (loc * 128) + (dbm + 64).
 *
 * El localizador se guarda como grados, no como letras: longitud en pasos
 * de 2 grados y latitud en pasos de 1, que es exactamente lo que es un
 * cuadrado de cuatro caracteres. Y la longitud va DEL REVES:
 *
 *     n = 180 * (179 - lon) + lat
 *
 * El 179 menos no es un capricho: WSPR cuenta la longitud desde el este,
 * asi que el numero crece hacia donde el localizador decrece.
 *
 * ESTO ESTUVO MAL Y EL BANCO NO PUDO VERLO. La version anterior hacia
 * n = lon * 180 + lat en los dos sentidos: se le olvidaba el 179 menos al
 * meter y al sacar, y como sim/wspr_aire.c codifica con nuestro
 * codificador y decodifica con nuestro decodificador, el error se
 * cancelaba y la ida y vuelta salia bien. En el aire no: ON4WS, que esta
 * en JO20, salia en la pantalla como IO70. Lo destapo una emision de
 * verdad y una fuente de fuera (qrzcq.com), no el banco.
 *
 * Por eso abajo, en las pruebas, hay un ancla ABSOLUTA - JO20 tiene que
 * dar 15800, ni mas ni menos- y no solo una ida y vuelta. Una ida y
 * vuelta no comprueba el convenio, solo comprueba que somos coherentes
 * con nosotros mismos.
 */
static uint8_t loc_a_n(const char *g, uint32_t *out)
{
    int32_t lon, lat;

    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') { return 0U; }
    if (g[2] < '0' || g[2] > '9' || g[3] < '0' || g[3] > '9') { return 0U; }
    if (g[4] != '\0') { return 0U; }

    lon = ((int32_t)(g[0] - 'A') * 10) + (int32_t)(g[2] - '0');   /* 0..179 */
    lat = ((int32_t)(g[1] - 'A') * 10) + (int32_t)(g[3] - '0');   /* 0..179 */

    *out = (uint32_t)((179 - lon) * 180 + lat);
    return 1U;
}

static void n_a_loc(uint32_t n, char *g)
{
    uint32_t lon = 179U - (n / 180U);
    uint32_t lat = n % 180U;

    g[0] = (char)('A' + (lon / 10U));
    g[1] = (char)('A' + (lat / 10U));
    g[2] = (char)('0' + (lon % 10U));
    g[3] = (char)('0' + (lat % 10U));
    g[4] = '\0';
}

/* ---------------- los 50 bits ---------------- */

static void mete_bits(uint8_t *bits, uint8_t desde, uint8_t cuantos, uint32_t v)
{
    uint8_t i;

    for (i = 0U; i < cuantos; i++) {
        uint8_t b = (uint8_t)((v >> (cuantos - 1U - i)) & 1U);
        uint8_t p = (uint8_t)(desde + i);

        if (b) { bits[p >> 3] |= (uint8_t)(0x80U >> (p & 7U)); }
        else   { bits[p >> 3] &= (uint8_t)~(0x80U >> (p & 7U)); }
    }
}

static uint32_t saca_bits(const uint8_t *bits, uint8_t desde, uint8_t cuantos)
{
    uint32_t v = 0UL;
    uint8_t i;

    for (i = 0U; i < cuantos; i++) {
        uint8_t p = (uint8_t)(desde + i);
        v = (v << 1) | (uint32_t)((bits[p >> 3] >> (7U - (p & 7U))) & 1U);
    }
    return v;
}

/*
 * LAS POTENCIAS DE WSPR NO SON CUALQUIER NUMERO.
 *
 * Solo existen estas: 0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40,
 * 43, 47, 50, 53, 57 y 60 dBm. O sea, las que acaban en 0, en 3 o en 7.
 * No es una lista de cortesia: ES LA MARCA DEL TIPO DE MENSAJE.
 *
 * WSPR mete tres formatos distintos en los mismos 50 bits:
 *
 *   tipo 1   indicativo normal + cuadrado de 4 + potencia   <- el de aqui
 *   tipo 2   indicativo con barra (EA8/G8SCU, G4XYZ/P) + potencia
 *   tipo 3   indicativo RESUMIDO (15 bits de hash) + cuadrado de 6
 *
 * Y lo unico que los distingue es el campo de potencia: si el numero que
 * sale ahi no acaba en 0, 3 o 7, el mensaje NO es del tipo 1 y los otros
 * dos campos significan otra cosa.
 *
 * ESTO SALIO EN ANTENA. Con la v1.95 aparecio en la tabla:
 *
 *     05:59  G8SCU  EK07  39 dBm  -2,1  Cal.100
 *
 * Corroboracion 100 -o sea, los 162 simbolos recibidos cuadran exactos
 * con volver a codificar ese mensaje-, asi que no era ruido ni un fallo
 * del decodificador: en el aire iba de verdad ese grupo de 50 bits. Lo
 * que fallaba era leerlos como tipo 1 cuando el 39 estaba gritando que
 * no lo eran. G8SCU emite de verdad desde KM56, no desde EK07.
 *
 * De momento se rechaza. Ensenar un indicativo inventado con un 100 al
 * lado es peor que no ensenar nada: el 100 invita a creerselo.
 */
static uint8_t potencia_vale(uint32_t dbm)
{
    uint32_t u = dbm % 10U;

    if (dbm > 60U) { return 0U; }
    return (uint8_t)(u == 0U || u == 3U || u == 7U);
}

uint8_t wspr_empaqueta(const wspr_msg_t *m, uint8_t bits[7])
{
    uint32_t ncal, nloc;

    if (!potencia_vale((uint32_t)m->dbm)) { return 0U; }
    if (!cal_a_n(m->indicativo, &ncal)) { return 0U; }
    if (!loc_a_n(m->locutor, &nloc)) { return 0U; }

    memset(bits, 0, 7);
    mete_bits(bits, 0U, 28U, ncal);
    mete_bits(bits, 28U, 15U, nloc);
    mete_bits(bits, 43U, 7U, (uint32_t)(m->dbm + 64U));
    return 1U;
}

uint8_t wspr_desempaqueta(const uint8_t bits[7], wspr_msg_t *m)
{
    uint32_t ncal = saca_bits(bits, 0U, 28U);
    uint32_t nloc = saca_bits(bits, 28U, 15U);
    uint32_t npot = saca_bits(bits, 43U, 7U);

    if (ncal >= 262177560UL) { return 0U; }   /* no es un indicativo normal */
    if (nloc >= 32400UL) { return 0U; }
    if (npot < 64UL || npot > 124UL) { return 0U; }
    /* Y la marca del tipo: ver potencia_vale() ahi arriba. */
    if (!potencia_vale(npot - 64UL)) { return 0U; }

    n_a_cal(ncal, m->indicativo);
    n_a_loc(nloc, m->locutor);
    m->dbm = (uint8_t)(npot - 64UL);
    return 1U;
}

/* ---------------- entrelazado ---------------- */

void wspr_entrelaza(const uint8_t ent[WSPR_SIMBOLOS], uint8_t sal[WSPR_SIMBOLOS],
                    uint8_t deshacer)
{
    uint8_t p = 0U;
    uint16_t i = 0U;

    /*
     * La posicion destino es el indice con sus OCHO bits dados la vuelta,
     * y los que se pasan de 162 se saltan. Se recorre i de 0 a 255 y se va
     * colocando: por eso hay que llegar hasta 256 y no hasta 162.
     */
    while (p < (uint8_t)WSPR_SIMBOLOS && i < 256U) {
        uint8_t j = (uint8_t)i;

        j = (uint8_t)(((j & 0xF0U) >> 4) | ((j & 0x0FU) << 4));
        j = (uint8_t)(((j & 0xCCU) >> 2) | ((j & 0x33U) << 2));
        j = (uint8_t)(((j & 0xAAU) >> 1) | ((j & 0x55U) << 1));

        if (j < (uint8_t)WSPR_SIMBOLOS) {
            if (deshacer) { sal[p] = ent[j]; }
            else          { sal[j] = ent[p]; }
            p++;
        }
        i++;
    }
}

/* ---------------- el camino de transmision ---------------- */

void wspr_codifica(const uint8_t bits[7], uint8_t simbolos[WSPR_SIMBOLOS])
{
    uint8_t canal[WSPR_SIMBOLOS];
    uint8_t mezclado[WSPR_SIMBOLOS];
    uint32_t reg = 0UL;
    uint8_t i, n = 0U;

    /* 81 bits de entrada -50 de mensaje y 31 de cola- dan 162 de salida. */
    for (i = 0U; i < (uint8_t)WSPR_BITS_ENTRA; i++) {
        uint8_t b = (i < (uint8_t)WSPR_BITS_MSG)
                  ? (uint8_t)((bits[i >> 3] >> (7U - (i & 7U))) & 1U)
                  : 0U;

        reg = (reg << 1) | (uint32_t)b;
        canal[n++] = paridad(reg & WSPR_POLY1);
        canal[n++] = paridad(reg & WSPR_POLY2);
    }

    wspr_entrelaza(canal, mezclado, 0U);

    for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) {
        simbolos[i] = (uint8_t)(k_wspr_sincro[i] + 2U * mezclado[i]);
    }
}

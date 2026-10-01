#include "jtty_msg.h"
#include <string.h>

/* El alfabeto de TEXT5: 64 caracteres, seis bits cada uno. */
static const char k_abc64[65] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;";

/* Las 18 frases de control, por su numero. De la 18 en adelante, invalido. */
static const char *const k_control[18] = {
    "AGN?", "CALL?", "AGN CALL", "NR?", "AGN NR", "EXCH?",
    "STATE?", "SECTION?", "ZONE?", "GRID?", "RPRT?", "QSL TU",
    "TU", "QRZ?", "QSO B4", "WAIT", "NIL?", "OK?"
};

/*
 * Las secciones de la ARRL, 1..86. La tabla NO se copia aqui por gusto:
 * el estandar de JTTY dice expresamente que el registro se COMPARTE con
 * lib/77bit/packjt77_grammar.f90 en vez de duplicarse, asi que esto es
 * esa misma lista y el dia que crezca hay que traerla otra vez.
 */
static const char k_seccion[86][4] = {
    "AB","AK","AL","AR","AZ","BC","CO","CT","DE","EB",
    "EMA","ENY","EPA","EWA","GA","GH","IA","ID","IL","IN",
    "KS","KY","LA","LAX","NS","MB","MDC","ME","MI","MN",
    "MO","MS","MT","NC","ND","NE","NFL","NH","NL","NLI",
    "NM","NNJ","NNY","TER","NTX","NV","OH","OK","ONE","ONN",
    "ONS","OR","ORG","PAC","PR","QC","RI","SB","SC","SCV",
    "SD","SDG","SF","SFL","SJV","SK","SNJ","STX","SV","TN",
    "UT","VA","VI","VT","WCF","WI","WMA","WNY","WPA","WTX",
    "WV","WWA","WY","DX","PE","NB"
};

/* Los tipos de EXCH_NUM y de EXCH_LOC solo cambian el RANGO que se
 * admite, no el texto: lo que se enseña es el numero o el token. */

#define NTOKENS  2063592UL
#define MAX22    4194304UL

/* ------------------------------------------------------------------ */

static uint32_t saca(const uint8_t *b, uint8_t desde, uint8_t n)
{
    uint32_t v = 0UL;
    uint8_t i;

    for (i = 0U; i < n; i++) { v = (v << 1) | (uint32_t)(b[desde + i] & 1U); }
    return v;
}

static char *num(char *p, uint32_t v, uint8_t minimo)
{
    char t[12];
    uint8_t n = 0U;

    do { t[n++] = (char)('0' + (v % 10UL)); v /= 10UL; } while (v != 0UL && n < 12U);
    while (n < minimo && n < 12U) { t[n++] = '0'; }
    while (n > 0U) { *p++ = t[--n]; }
    return p;
}

static char *txt(char *p, const char *s)
{
    while (*s != '\0') { *p++ = *s++; }
    return p;
}

/*
 * UN TOKEN EN BASE 36. El estandar dice que se acumula de izquierda a
 * derecha, o sea que "ABC" es ((10*36)+11)*36+12, y que la longitud va
 * en un bit aparte: 0 son dos caracteres y 1 son tres.
 *
 * Y la comprobacion que NO se puede saltar: con longitud 0 el valor tiene
 * que ser menor que 36^2, y con longitud 1 tiene que estar entre 36^2 y
 * 36^3. Un "0AB" -tres caracteres con un cero delante- tendria una forma
 * canonica de dos y por eso es invalido. Sin eso, el mismo token se
 * podria escribir de dos maneras y dos radios enseñarian cosas distintas.
 */
static uint8_t token36(uint32_t v, uint8_t largo, char *out)
{
    static const char k36[37] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint8_t n = (uint8_t)(largo ? 3U : 2U);
    uint8_t i;

    if (!largo && v >= 1296UL) { return 0U; }
    if (largo && (v < 1296UL || v >= 46656UL)) { return 0U; }
    for (i = n; i > 0U; i--) { out[i - 1U] = k36[v % 36UL]; v /= 36UL; }
    out[n] = '\0';
    return 1U;
}

/*
 * EL INDICATIVO DE 28 BITS. Es el mismo `call28` de los mensajes de 77
 * bits de FT8/FT4, y por eso aqui solo se descodifica: el reparto de
 * rangos -tokens, resumen de 22 bits, indicativo normal- es de ese
 * formato y no de JTTY.
 *
 * LO QUE NO SE PUEDE HACER: el tramo del medio son indicativos NO
 * normales (con barra, o demasiado largos) guardados como un resumen de
 * 22 bits, y el resumen no se puede deshacer - hay que haber oido antes
 * el indicativo entero y tenerlo apuntado. Aqui no hay esa tabla, asi que
 * se enseña <...> y no una invencion.
 */
static uint8_t call28(uint32_t n, char *out)
{
    static const char k27[28] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char k36[37] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char k37[38] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char t[7];
    uint8_t i, j = 0U;

    if (n < NTOKENS) {
        if (n == 0UL) { (void)strcpy(out, "DE");  return 1U; }
        if (n == 1UL) { (void)strcpy(out, "QRZ"); return 1U; }
        if (n == 2UL) { (void)strcpy(out, "CQ");  return 1U; }
        if (n <= 1002UL) {
            char *p = txt(out, "CQ ");

            p = num(p, n - 3UL, 3U);
            *p = '\0';
            return 1U;
        }
        if (n <= 532443UL) {
            uint32_t v = n - 1003UL;
            char a[5];
            char *p;

            a[4] = '\0';
            for (i = 4U; i > 0U; i--) { a[i - 1U] = k27[v % 27UL]; v /= 27UL; }
            p = txt(out, "CQ ");
            i = 0U;
            while (a[i] == ' ') { i++; }
            while (a[i] != '\0') { *p++ = a[i++]; }
            *p = '\0';
            return 1U;
        }
        return 0U;
    }
    n -= NTOKENS;
    if (n < MAX22) { (void)strcpy(out, "<...>"); return 1U; }

    n -= MAX22;
    t[6] = '\0';
    t[5] = k27[n % 27UL]; n /= 27UL;
    t[4] = k27[n % 27UL]; n /= 27UL;
    t[3] = k27[n % 27UL]; n /= 27UL;
    t[2] = k36[n % 10UL]; n /= 10UL;
    t[1] = k36[n % 36UL]; n /= 36UL;
    t[0] = k37[n % 37UL];
    for (i = 0U; i < 6U; i++) { if (t[i] != ' ') { break; } }
    for (; i < 6U; i++) { out[j++] = t[i]; }
    while (j > 0U && out[j - 1U] == ' ') { j--; }
    out[j] = '\0';
    return (uint8_t)(j > 0U);
}

/* ------------------------------------------------------------------ */

static uint8_t struct30(const uint8_t *b, char *p)
{
    uint32_t fam = saca(b, 27U, 3U);     /* bits 28-30 */
    uint8_t rol;

    switch (fam) {
    case 0UL: {   /* EXCH_NUM: role1 kind4 value17 zero5 */
        uint32_t kind = saca(b, 1U, 4U);
        uint32_t v    = saca(b, 5U, 17U);

        rol = (uint8_t)saca(b, 0U, 1U);
        if (saca(b, 22U, 5U) != 0UL) { return 0U; }
        if (kind > 7UL) { return 0U; }
        if (kind == 1UL && (v < 1UL || v > 40UL)) { return 0U; }   /* zona CQ */
        if (kind == 2UL && (v < 1UL || v > 90UL)) { return 0U; }   /* zona UIT */
        if (kind == 6UL && v > 9999UL) { return 0U; }              /* año de licencia */
        if (rol) { p = txt(p, "599 "); }
        if (kind == 0UL || kind == 5UL) { p = num(p, v, 3U); }
        else if (kind == 1UL || kind == 2UL) { p = num(p, v, 2U); }
        else if (kind == 6UL) { p = num(p, v, 4U); }
        else { p = num(p, v, 1U); }
        *p = '\0';
        return 1U;
    }
    case 1UL: {   /* EXCH_LOC: role1 kind4 length1 token16 zero5 */
        uint32_t kind = saca(b, 1U, 4U);
        uint8_t  largo = (uint8_t)saca(b, 5U, 1U);
        uint32_t tok = saca(b, 6U, 16U);
        char s[4];

        rol = (uint8_t)saca(b, 0U, 1U);
        if (saca(b, 22U, 5U) != 0UL) { return 0U; }
        if (kind > 4UL) { return 0U; }
        if (!token36(tok, largo, s)) { return 0U; }
        if (rol) { p = txt(p, "599 "); }
        p = txt(p, s);
        *p = '\0';
        return 1U;
    }
    case 2UL: {   /* EXCH_PAIR: schema3 pair_data23 zero1 */
        uint32_t esq = saca(b, 0U, 3U);

        if (esq == 0UL) {          /* ZONE_LOC3: cq_zone6 length1 token16 */
            uint32_t zona = saca(b, 3U, 6U);
            uint8_t  largo = (uint8_t)saca(b, 9U, 1U);
            uint32_t tok = saca(b, 10U, 16U);
            char s[4];

            if (saca(b, 26U, 1U) != 0UL) { return 0U; }
            if (zona < 1UL || zona > 40UL) { return 0U; }
            if (!token36(tok, largo, s)) { return 0U; }
            p = txt(p, "599 ");
            p = num(p, zona, 2U);
            *p++ = ' ';
            p = txt(p, s);
            *p = '\0';
            return 1U;
        }
        if (esq == 1UL) {          /* CLASS_SECTION: count6 class3 section7 zero7 */
            uint32_t cuenta = saca(b, 3U, 6U);
            uint32_t clase  = saca(b, 9U, 3U);
            uint32_t sec    = saca(b, 12U, 7U);

            if (saca(b, 19U, 7U) != 0UL || saca(b, 26U, 1U) != 0UL) { return 0U; }
            if (cuenta < 1UL || cuenta > 32UL) { return 0U; }
            if (clase > 5UL) { return 0U; }
            if (sec < 1UL || sec > 86UL) { return 0U; }
            p = num(p, cuenta, 1U);
            *p++ = (char)('A' + clase);
            *p++ = ' ';
            p = txt(p, k_seccion[sec - 1UL]);
            *p = '\0';
            return 1U;
        }
        return 0U;
    }
    case 3UL: {   /* EXCH_NUM_TIME: role1 serial14 minute11 zero1 */
        uint32_t ser = saca(b, 1U, 14U);
        uint32_t min = saca(b, 15U, 11U);

        rol = (uint8_t)saca(b, 0U, 1U);
        if (saca(b, 26U, 1U) != 0UL) { return 0U; }
        if (min > 1439UL) { return 0U; }
        if (rol) { p = txt(p, "599 "); }
        p = num(p, ser, 3U);
        *p++ = ' ';
        p = num(p, min / 60UL, 2U);
        p = num(p, min % 60UL, 2U);
        *p = '\0';
        return 1U;
    }
    case 4UL: {   /* MISC: subtype4 subtype_data23 */
        uint32_t sub = saca(b, 0U, 4U);

        if (sub == 0UL) {          /* CONTROL: phrase7 zero16 */
            uint32_t fr = saca(b, 4U, 7U);

            if (saca(b, 11U, 16U) != 0UL) { return 0U; }
            if (fr > 17UL) { return 0U; }
            p = txt(p, k_control[fr]);
            *p = '\0';
            return 1U;
        }
        if (sub == 1UL) {          /* GRID4: role1 grid4_code15 zero7 */
            uint32_t cod;
            uint32_t c1, c2, d1, d2;

            rol = (uint8_t)saca(b, 4U, 1U);
            cod = saca(b, 5U, 15U);
            if (saca(b, 20U, 7U) != 0UL) { return 0U; }
            if (cod > 32399UL) { return 0U; }
            d2 = cod % 10UL; cod /= 10UL;
            d1 = cod % 10UL; cod /= 10UL;
            c2 = cod % 18UL; cod /= 18UL;
            c1 = cod;
            if (rol) { p = txt(p, "599 "); }
            *p++ = (char)('A' + c1);
            *p++ = (char)('A' + c2);
            *p++ = (char)('0' + d1);
            *p++ = (char)('0' + d2);
            *p = '\0';
            return 1U;
        }
        return 0U;
    }
    default:
        /* Familias 101, 110 y 111: reservadas o guarda de invalidez. */
        return 0U;
    }
}

uint8_t jtty_msg_lee(const uint8_t carga[JTTY_CARGA_BITS], jtty_atomo_t *out)
{
    uint32_t i2;
    uint8_t i;
    char *p;

    if (carga == 0 || out == 0) { return 0U; }
    out->texto[0] = '\0';
    out->fin = 0U;
    out->texto5 = 0U;

    /* El bit 33 va siempre a cero; si no, esto no es un atomo valido. */
    if (carga[JTTY_BIT_RESERVADO - 1U] != 0U) { return 0U; }
    out->fin = (uint8_t)(carga[33] & 1U);

    /* Y la palabra de gramatica toda a ceros es un centinela invalido. */
    {
        uint8_t algo = 0U;

        for (i = 0U; i < 32U; i++) { algo |= (uint8_t)(carga[i] & 1U); }
        if (!algo) { return 0U; }
    }

    i2 = saca(carga, 30U, 2U);    /* bits 31-32 */
    p = out->texto;

    if (i2 == 3UL) {
        /*
         * TEXT5: cinco caracteres de seis bits, en orden de emision.
         *
         * LOS ESPACIOS DE LA DERECHA SOLO SOBRAN EN EL ULTIMO - 28/09/2026.
         *
         * Aqui se quitaban SIEMPRE, y eso parecia razonable: "HI___" es
         * "HI". Lo es cuando el atomo es el mensaje entero. Cuando no,
         * "ALLY " y "NEED " son trozos de una cadena mas larga y ese
         * espacio de la derecha es el que separa las dos palabras: al
         * quitarlo y volver a poner otro al juntar, la cuenta sale igual
         * por casualidad... hasta que el trozo NO acaba en espacio, y
         * entonces "WE REALLY" sale "WE RE ALLY".
         *
         * Se vio en antena: "WE RE ALLY NEED EO ON".
         *
         * Asi que solo se recorta si este atomo cierra el mensaje, que es
         * lo unico que el bit 34 ya nos esta diciendo.
         */
        out->texto5 = 1U;
        for (i = 0U; i < 5U; i++) {
            p[i] = k_abc64[saca(carga, (uint8_t)(6U * i), 6U)];
        }
        p[5] = '\0';
        if (out->fin) {
            i = 5U;
            while (i > 0U && p[i - 1U] == ' ') { i--; p[i] = '\0'; }
        }
        return 1U;
    }

    if (i2 == 2UL) {
        return struct30(carga, p);
    }

    /* i2 = 0 o 1: indicativo de 28 bits mas la accion. */
    {
        uint32_t n2 = saca(carga, 28U, 2U);
        uint32_t accion = i2 * 4UL + n2;
        char cal[16];

        if (accion > 5UL) { return 0U; }   /* 1.2 y 1.3 estan reservadas */
        if (!call28(saca(carga, 0U, 28U), cal)) { return 0U; }

        switch (accion) {
        case 0UL: p = txt(p, "CQ "); p = txt(p, cal); p = txt(p, " CQ");   break;
        case 1UL: p = txt(p, cal);                                         break;
        case 2UL: p = txt(p, "TU "); p = txt(p, cal); p = txt(p, " CQ");   break;
        case 3UL: p = txt(p, cal); p = txt(p, " TU");                      break;
        case 4UL: p = txt(p, cal); p = txt(p, " AGN?");                    break;
        default:  p = txt(p, "TU NOW "); p = txt(p, cal);                  break;
        }
        *p = '\0';
        return 1U;
    }
}

/* ---------------------------------------------------------------- */
/* Ver jtty_msg.h: la regla de juntar, donde el banco la puede medir. */

void jtty_msg_pega(char *dst, uint32_t max, const jtty_atomo_t *a, uint8_t ant5)
{
    uint32_t n = 0UL;
    uint8_t k = 0U;

    if (dst == 0 || a == 0 || max < 2UL) { return; }
    while (dst[n] != '\0') { n++; }

    /*
     * DOS TEXT5 SEGUIDOS SE PEGAN SIN NADA EN MEDIO: son trozos de cinco
     * caracteres de la misma cadena y ya traen sus propios espacios. Todo
     * lo demas son frases enteras y van separadas.
     */
    if (n > 0UL && !(ant5 && a->texto5) && n < (max - 2UL)) {
        dst[n++] = ' ';
    }
    while (a->texto[k] != '\0' && n < (max - 1UL)) { dst[n++] = a->texto[k++]; }
    dst[n] = '\0';
}

/* Una letra A-Z (los mensajes de JTTY van en mayusculas). */
static uint8_t es_letra(char c) { return (uint8_t)(c >= 'A' && c <= 'Z'); }
static uint8_t es_cifra(char c) { return (uint8_t)(c >= '0' && c <= '9'); }

/* Un trozo sin barras: prefijo(1-2, con letra) + digito + 1..4 letras. */
static uint8_t forma_de_indicativo(const char *p, uint8_t n)
{
    uint8_t i = 0U, pref = 0U, letras = 0U, suf;

    if (n < 3U || n > 10U) { return 0U; }

    /* El prefijo: uno o dos alfanumericos, y al menos uno letra. */
    while (i < n && pref < 2U) {
        if (es_letra(p[i])) { letras++; }
        else if (!es_cifra(p[i])) { return 0U; }
        /* Se para en cuanto lo que viene es "digito + letra", que es el
         * digito de zona: "F4JUO" tiene prefijo F, "IK3CHK" tiene IK. */
        if (es_cifra(p[i]) && (i + 1U) < n && es_letra(p[i + 1U])
            && letras > 0U) {
            break;
        }
        i++; pref++;
    }
    if (letras == 0U || i >= n) { return 0U; }

    /* El digito de zona. */
    if (!es_cifra(p[i])) { return 0U; }
    i++;

    /* Y el sufijo: de una a cuatro letras y se acaba el testigo. */
    suf = 0U;
    while (i < n && es_letra(p[i])) { i++; suf++; }
    return (uint8_t)((i == n) && suf >= 1U && suf <= 4U);
}

/* El testigo entero vale si CUALQUIERA de sus trozos entre barras vale. */
static uint8_t indicativo_vale(const char *p, uint8_t n)
{
    uint8_t i = 0U, ini = 0U;

    for (i = 0U; i <= n; i++) {
        if (i == n || p[i] == '/') {
            if (forma_de_indicativo(&p[ini], (uint8_t)(i - ini))) { return 1U; }
            ini = (uint8_t)(i + 1U);
        }
    }
    return 0U;
}

uint8_t jtty_msg_indicativo(const char *texto, char *out)
{
    uint32_t i = 0UL;
    uint8_t hay = 0U;

    if (texto == 0 || out == 0) { return 0U; }
    out[0] = '\0';

    while (texto[i] != '\0') {
        uint32_t ini;
        uint8_t n;

        while (texto[i] == ' ' || texto[i] == '\t') { i++; }
        ini = i;
        while (texto[i] != '\0' && texto[i] != ' ' && texto[i] != '\t') { i++; }
        n = (uint8_t)((i - ini > 15UL) ? 15UL : (i - ini));
        if (n > 0U && (uint32_t)n == (i - ini)
            && indicativo_vale(&texto[ini], n)) {
            uint8_t k;

            /* El ULTIMO gana, asi que se apunta y se sigue buscando. */
            for (k = 0U; k < n; k++) { out[k] = texto[ini + k]; }
            out[n] = '\0';
            hay = 1U;
        }
    }
    return hay;
}

void jtty_msg_cierra(char *dst)
{
    uint32_t n = 0UL;

    if (dst == 0) { return; }
    while (dst[n] != '\0') { n++; }
    while (n > 0UL && dst[n - 1U] == ' ') { n--; dst[n] = '\0'; }
}

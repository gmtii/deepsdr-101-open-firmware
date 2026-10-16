/*
 * El CAT de Kenwood. Ver cat.h para por que es de Kenwood y no nuestro.
 */
#include "cat.h"

#define ORDEN_MAX  32U

static const cat_radio_t *s_r;
static char    s_orden[ORDEN_MAX];
static uint8_t s_n;
static uint8_t s_perdida;   /* la orden en curso ya no cabe: tirarla entera */

void cat_reset(void)
{
    s_n = 0U;
    s_perdida = 0U;
}

void cat_init(const cat_radio_t *radio)
{
    s_r = radio;
    cat_reset();
}

/* --- escribir numeros con ancho fijo, que es lo unico que hace falta --- */

static uint8_t pon(char *d, uint8_t j, const char *t)
{
    while (*t != '\0') { d[j++] = *t++; }
    return j;
}

static uint8_t pon_num(char *d, uint8_t j, uint32_t v, uint8_t cifras)
{
    uint8_t k = cifras;

    /*
     * De derecha a izquierda, que es como se rellena de ceros sin dividir
     * dos veces. Y acotado: un numero mas largo que su hueco saldria
     * desplazando todo lo que viene detras, y el bloque IF tiene que medir
     * EXACTAMENTE 37 caracteres o hamlib lo descarta entero.
     */
    while (k > 0U) {
        k--;
        d[j + k] = (char)('0' + (char)(v % 10UL));
        v /= 10UL;
    }
    return (uint8_t)(j + cifras);
}

static uint32_t lee_num(const char *s, uint8_t n)
{
    uint32_t v = 0UL;
    uint8_t  i;

    for (i = 0U; i < n; i++) {
        if ((s[i] < '0') || (s[i] > '9')) { return 0xFFFFFFFFUL; }
        v = (v * 10UL) + (uint32_t)(s[i] - '0');
    }
    return v;
}

static uint8_t dos(const char *o, char a, char b)
{
    return ((o[0] == a) && (o[1] == b)) ? 1U : 0U;
}

static uint8_t error(char *s)
{
    s[0] = '?'; s[1] = ';';
    return 2U;
}

/*
 * EL BLOQUE IF, QUE MIDE 37 CARACTERES Y NI UNO MAS.
 *
 *   IF  f(11)  _____(5)  rit(5)  R X  b  cc  t  m  v  s  p  n  tt  h  ;
 *    2  + 11   +   5     +  5    +1+1 +1 + 2 +1 +1 +1 +1 +1 +1 + 2 +1  = 37
 *
 * hamlib lo parsea por POSICION, no por separadores: si sobra o falta un
 * caracter no lee mal un campo, descarta la respuesta entera y la radio
 * "no contesta". Por eso cada campo se escribe con su ancho fijo y por eso
 * pon_num() acota.
 */
static uint8_t bloque_if(char *s)
{
    uint8_t j = 0U;

    j = pon(s, j, "IF");
    j = pon_num(s, j, s_r->freq_lee(), 11U);   /* frecuencia */
    j = pon(s, j, "     ");                    /* 5 sin usar */
    j = pon_num(s, j, 0UL, 5U);                /* RIT/XIT en Hz */
    s[j++] = '0';                              /* RIT apagado */
    s[j++] = '0';                              /* XIT apagado */
    s[j++] = '0';                              /* banco de memorias */
    j = pon_num(s, j, 0UL, 2U);                /* canal de memoria */
    s[j++] = '0';                              /* 0 = recibiendo. Siempre. */
    s[j++] = (char)('0' + (char)s_r->modo_lee());
    s[j++] = '0';                              /* VFO A */
    s[j++] = '0';                              /* sin exploracion */
    s[j++] = '0';                              /* sin split */
    s[j++] = '0';                              /* sin tono */
    j = pon_num(s, j, 0UL, 2U);                /* numero de tono */
    s[j++] = '0';                              /* sin desplazamiento */
    s[j++] = ';';
    return j;
}

static uint8_t procesa(const char *o, uint8_t n, char *s)
{
    uint8_t j = 0U;

    if (n < 2U) { return error(s); }

    /* --- identificacion y enganche --- */
    if (dos(o, 'I', 'D')) { return (uint8_t)pon(s, 0U, "ID019;"); }
    if (dos(o, 'P', 'S')) {
        /* Encendida. Y encenderla o apagarla desde el ordenador no se
         * admite: esta radio no tiene por donde. */
        if (n == 2U) { return (uint8_t)pon(s, 0U, "PS1;"); }
        return 0U;
    }
    if (dos(o, 'A', 'I')) {
        /*
         * "Informacion automatica": que la radio avise sola cuando algo
         * cambia, sin que se lo pregunten. Se contesta siempre AI0 -que
         * esta apagada- y poner AI1 se acepta y se ignora.
         *
         * Decir que si seria comprometerse a mandar un IF; cada vez que se
         * gira el mando, desde dentro del hilo de la interfaz y sin saber
         * si al otro lado hay alguien leyendo. Un programa que pide AI1 y
         * no lo recibe sigue funcionando -consulta y ya-; uno que recibe
         * tramas que nadie recoge llena el buffer del puerto.
         */
        if (n == 2U) { return (uint8_t)pon(s, 0U, "AI0;"); }
        return 0U;
    }

    /* --- frecuencia --- */
    if (dos(o, 'F', 'A') || dos(o, 'F', 'B')) {
        if (n == 2U) {
            j = pon(s, 0U, dos(o, 'F', 'A') ? "FA" : "FB");
            j = pon_num(s, j, s_r->freq_lee(), 11U);
            s[j++] = ';';
            return j;
        }
        if (n == 13U) {
            uint32_t f = lee_num(&o[2], 11U);

            if (f == 0xFFFFFFFFUL) { return error(s); }
            s_r->freq_pon(f);
            return 0U;
        }
        return error(s);
    }

    /* --- modo --- */
    if (dos(o, 'M', 'D')) {
        if (n == 2U) {
            j = pon(s, 0U, "MD");
            s[j++] = (char)('0' + (char)s_r->modo_lee());
            s[j++] = ';';
            return j;
        }
        if (n == 3U) {
            if ((o[2] < '1') || (o[2] > '9')) { return error(s); }
            s_r->modo_pon((uint8_t)(o[2] - '0'));
            return 0U;
        }
        return error(s);
    }

    /* --- el S, que es lo que mas se pregunta --- */
    if (dos(o, 'S', 'M')) {
        /* SM0; -> SM0 + 4 cifras. El rango de Kenwood es 0..30, no 0..255:
         * mandar 255 hace que hamlib enseñe S9+40 con el ruido de fondo. */
        uint8_t v = s_r->smeter_lee();

        if (v > 30U) { v = 30U; }
        j = pon(s, 0U, "SM0");
        j = pon_num(s, j, (uint32_t)v, 4U);
        s[j++] = ';';
        return j;
    }

    /* --- el bloque gordo --- */
    if (dos(o, 'I', 'F')) {
        if (n != 2U) { return error(s); }
        return bloque_if(s);
    }

    /* --- VFO: solo hay uno, y se dice que es el A --- */
    if (dos(o, 'F', 'R') || dos(o, 'F', 'T')) {
        if (n == 2U) {
            j = pon(s, 0U, dos(o, 'F', 'R') ? "FR" : "FT");
            s[j++] = '0';
            s[j++] = ';';
            return j;
        }
        return 0U;   /* elegir VFO: se acepta y no cambia nada */
    }

    /* --- volumen --- */
    if (dos(o, 'A', 'G')) {
        if (n == 3U) {   /* AG0; */
            j = pon(s, 0U, "AG0");
            j = pon_num(s, j, (uint32_t)s_r->vol_lee(), 3U);
            s[j++] = ';';
            return j;
        }
        if (n == 6U) {   /* AG0255; */
            uint32_t v = lee_num(&o[3], 3U);

            if (v > 255UL) { return error(s); }
            s_r->vol_pon((uint8_t)v);
            return 0U;
        }
        return error(s);
    }

    /* --- ancho del filtro --- */
    if (dos(o, 'F', 'W')) {
        if (n == 2U) {
            j = pon(s, 0U, "FW");
            j = pon_num(s, j, (uint32_t)s_r->ancho_lee(), 4U);
            s[j++] = ';';
            return j;
        }
        return 0U;   /* ponerlo desde fuera: todavia no */
    }

    /*
     * --- y todo lo de transmitir ---
     *
     * Esto es un receptor. "?;" es lo que contesta un Kenwood a una orden
     * que no conoce, y es exactamente lo que hay que decir: hamlib lo lee
     * como "no soportado" y deja de pedirlo. Callarse costaria un tiempo de
     * espera entero por cada intento.
     */
    return error(s);
}

uint8_t cat_byte(char c, char *salida)
{
    if (c == ';') {
        uint8_t n = s_n;
        uint8_t perdida = s_perdida;

        cat_reset();
        if (perdida != 0U) { return error(salida); }
        if (n == 0U)       { return 0U; }
        return procesa(s_orden, n, salida);
    }

    /* Los Kenwood admiten que las ordenes vengan en minusculas. */
    if ((c >= 'a') && (c <= 'z')) { c = (char)(c - 32); }

    /* Retornos y espacios de relleno entre ordenes: fuera, sin romper la
     * orden en curso. Hay programas que mandan "FA;\r\n". */
    if ((c == '\r') || (c == '\n')) { return 0U; }

    if (s_n < ORDEN_MAX) { s_orden[s_n++] = c; }
    else                 { s_perdida = 1U; }
    return 0U;
}

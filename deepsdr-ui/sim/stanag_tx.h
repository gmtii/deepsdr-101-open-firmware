/*
 * UN TRANSMISOR DE STANAG 4285, PARA PODER PROBAR LAS ONCE
 * COMBINACIONES Y NO SOLO LA QUE HAY GRABADA.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * La unica grabacion de 4285 que hay es 300 bps con entrelazado largo.
 * Con ella se puede comprobar que la busqueda automatica encuentra ESA,
 * y nada mas. Las otras diez combinaciones -que son las que el usuario se
 * encuentra por la banda- quedaban sin probar, y un banco que solo mira
 * el caso que ya funcionaba no es un banco.
 *
 * Esto genera la forma de onda. No es un simulador aparte: es el INVERSO
 * EXACTO de lo que hace User/stanag_rx.c, escrito desde el mismo anexo E
 * y con las mismas tablas.
 *
 * COMO SE SABE QUE EL TRANSMISOR NO MIENTE. Un transmisor equivocado y
 * un receptor equivocado del mismo modo se entienden perfectamente, y el
 * banco saldria verde con los dos rotos. Por eso la primera prueba de
 * quien use esto tiene que ser IDA Y VUELTA CONTRA LA REALIDAD:
 * sintetizar 300 bps con entrelazado largo -lo mismo que la grabacion
 * del aire- y exigir que el receptor saque la frase. Si el transmisor
 * estuviera mal, esa no saldria, porque el receptor ya esta validado
 * contra el aire. Ver sim/stanag_busca.c.
 *
 * LA CADENA, que es la de stanag_rx.h del reves:
 *
 *   texto -> ASCII 8 bits LSB primero, con SOM delante y EOM detras
 *         -> convolucional K=7 tasa 1/2 (las mismas tomas que hfdl)
 *         -> repeticion POR PAREJAS: T1 T2 T1 T2, no T1 T1 T2 T2
 *         -> entrelazador convolucional de 32 filas, la fila r retrasa
 *            j*r (en el receptor j*(31-r): entre las dos, 31*j siempre)
 *         -> simbolos BPSK / QPSK / 8PSK
 *         -> trama: 80 de preambulo + [32 datos, 16 sondeo] x3 + 32
 *         -> scrambler de 176 (se SUMA el indice, el receptor lo resta)
 *         -> raiz de coseno alzado alfa 0,35, 5 muestras por simbolo
 *         -> a 1800 Hz, real, 12 kHz
 */
#ifndef STANAG_TX_H
#define STANAG_TX_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#define STX_SPS      5
#define STX_PRE      80
#define STX_TRAMA    256
#define STX_SPAN     4
#define STX_TAPS     (2 * STX_SPAN * STX_SPS + 1)
#define STX_FILAS    32
#define STX_RETMAX   (24 * 31)    /* el mayor j de la lista por 31 */

typedef struct {
    /* las dos secuencias de la norma, generadas igual que en tablas() */
    int8_t  pre[STX_PRE];
    uint8_t scr[176];
    uint8_t conocido[176];
    float   rrc[STX_TAPS];
    /* el entrelazador: 32 filas, la fila r retrasa j*r */
    uint8_t fila[STX_FILAS][STX_RETMAX + 1];
    uint16_t flen[STX_FILAS];
    uint16_t fpos[STX_FILAS];
    uint8_t  j;
    uint8_t  bits_sim;    /* 1 BPSK, 2 QPSK, 3 8PSK */
    uint8_t  rep;         /* repeticiones por parejas */
    /* estado del modulador */
    uint8_t  reg;         /* los 6 bits del convolucional */
    uint8_t  par[32];     /* bits codificados esperando grupo */
    uint8_t  npar;
    uint32_t nsim;        /* simbolos ya emitidos en la trama */
    float    cola_r[STX_TAPS];
    float    cola_i[STX_TAPS];
    uint32_t n;           /* muestras emitidas */
    uint8_t  sin_marcas;  /* 1 = ni SOM ni EOM: para probar la fase de byte
                           * sola. La grabacion del aire no trae SOM. */
    uint8_t  desfase;     /* bits de informacion de relleno antes del mensaje.
                           * Corre la fase de byte a proposito, que es la
                           * unica manera de ver si el receptor la encuentra
                           * o si acertaba por casualidad. */
} stx_t;

static const float stx_cos8[8] = {
    1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f
};
static const float stx_sen8[8] = {
    0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f
};
/* el mapa Gray de 8PSK, igual que k_g8 en el receptor */
static const uint8_t stx_g8[8] = { 1U, 0U, 2U, 3U, 6U, 7U, 5U, 4U };

static void stx_tablas(stx_t *t)
{
    uint8_t est5[5] = { 1U, 1U, 0U, 1U, 0U };
    uint8_t est9[9];
    int i, k;
    float e = 0.0f;
    const float beta = 0.35f;

    for (i = 0; i < STX_PRE; i++) {
        uint8_t b = est5[4];
        uint8_t n = (uint8_t)(est5[2] ^ est5[4]);
        t->pre[i] = (b != 0U) ? (int8_t)-1 : (int8_t)1;
        for (k = 4; k > 0; k--) { est5[k] = est5[k - 1]; }
        est5[0] = n;
    }
    for (i = 0; i < 9; i++) { est9[i] = 1U; }
    for (i = 0; i < 176; i++) {
        int r;
        t->scr[i] = (uint8_t)((est9[6] << 2) | (est9[7] << 1) | est9[8]);
        for (r = 0; r < 3; r++) {
            uint8_t n = (uint8_t)(est9[4] ^ est9[8]);
            for (k = 8; k > 0; k--) { est9[k] = est9[k - 1]; }
            est9[0] = n;
        }
        t->conocido[i] = (uint8_t)(((i % 48) >= 32) ? 1U : 0U);
    }
    for (i = 0; i < STX_TAPS; i++) {
        float x = ((float)i - (float)(STX_TAPS / 2)) / (float)STX_SPS;
        float a = fabsf(x), v;
        if (a < 1e-6f) {
            v = 1.0f - beta + (4.0f * beta / 3.14159265f);
        } else if (fabsf((4.0f * beta * a) - 1.0f) < 1e-5f) {
            v = (beta / 1.41421356f)
              * ((1.0f + (2.0f / 3.14159265f)) * sinf(3.14159265f / (4.0f * beta))
               + (1.0f - (2.0f / 3.14159265f)) * cosf(3.14159265f / (4.0f * beta)));
        } else {
            float d = 3.14159265f * x * (1.0f - ((4.0f * beta * x) * (4.0f * beta * x)));
            v = (sinf(3.14159265f * x * (1.0f - beta))
               + 4.0f * beta * x * cosf(3.14159265f * x * (1.0f + beta))) / d;
        }
        t->rrc[i] = v;
        e += v * v;
    }
    e = sqrtf(e);
    for (i = 0; i < STX_TAPS; i++) { t->rrc[i] /= e; }
}

/* vel: 0=75 1=150 2=300 3=600 4=1200 5=2400, igual que el enum del receptor */
static void stx_init(stx_t *t, int vel, int largo)
{
    int r;
    memset(t, 0, sizeof *t);
    stx_tablas(t);
    t->bits_sim = (uint8_t)((vel == 5) ? 3 : ((vel == 4) ? 2 : 1));
    t->rep = (uint8_t)((vel == 2) ? 2 : ((vel == 1) ? 4 : ((vel == 0) ? 8 : 1)));
    if (vel == 5)      { t->j = (uint8_t)(largo ? 48 : 4); }
    else if (vel == 4) { t->j = (uint8_t)(largo ? 24 : 2); }
    else               { t->j = (uint8_t)(largo ? 12 : 1); }
    for (r = 0; r < STX_FILAS; r++) {
        t->flen[r] = (uint16_t)(((uint32_t)t->j * (uint32_t)r) + 1U);
        t->fpos[r] = 0U;
    }
}

/* un grupo de 32 bits de FEC entra, 32 bits de canal salen */
static void stx_entrelaza(stx_t *t, const uint8_t *f, uint8_t *c)
{
    int i, r;
    for (i = 0; i < STX_FILAS; i++) {
        int rr = (9 * i) % STX_FILAS;
        uint16_t L = t->flen[rr], h = t->fpos[rr];
        t->fila[rr][h] = f[i];
        t->fpos[rr] = (uint16_t)((h + L - 1U) % L);
    }
    for (r = 0; r < STX_FILAS; r++) {
        c[r] = t->fila[r][t->fpos[r]];
    }
}

/* de bits de canal a indice de 8PSK (0..7), sin scrambler todavia */
static uint8_t stx_simbolo(const stx_t *t, const uint8_t *b)
{
    if (t->bits_sim == 1U) { return (uint8_t)(b[0] ? 4U : 0U); }
    if (t->bits_sim == 2U) {
        /* 00 -> 0, 01 -> 2, 11 -> 4, 10 -> 6: ver bits_de() del receptor */
        static const uint8_t m[4] = { 0U, 2U, 6U, 4U };
        return m[(b[0] << 1) | b[1]];
    }
    {
        uint8_t v = (uint8_t)((b[0] << 2) | (b[1] << 1) | b[2]);
        uint8_t k;
        for (k = 0U; k < 8U; k++) { if (stx_g8[k] == v) { return k; } }
        return 0U;
    }
}

/*
 * Mete un simbolo ya scrambleado en la salida: lo conforma con la raiz
 * de coseno alzado y lo sube a 1800 Hz. Devuelve cuantas muestras puso.
 */
static uint32_t stx_pon_simbolo(stx_t *t, uint8_t idx, float *sal)
{
    int k, m;
    float sr = stx_cos8[idx], si = stx_sen8[idx];
    for (k = 0; k < STX_SPS; k++) {
        float ar = 0.0f, ai = 0.0f, th;
        /* la cola se desplaza y entra el impulso solo en la primera */
        for (m = STX_TAPS - 1; m > 0; m--) {
            t->cola_r[m] = t->cola_r[m - 1];
            t->cola_i[m] = t->cola_i[m - 1];
        }
        t->cola_r[0] = (k == 0) ? sr : 0.0f;
        t->cola_i[0] = (k == 0) ? si : 0.0f;
        for (m = 0; m < STX_TAPS; m++) {
            ar += t->cola_r[m] * t->rrc[m];
            ai += t->cola_i[m] * t->rrc[m];
        }
        th = 6.28318531f * 3.0f * (float)t->n / 20.0f;
        sal[k] = (ar * cosf(th)) - (ai * sinf(th));
        t->n++;
    }
    return STX_SPS;
}

/* --- la capa de bits: FEC, repeticion y el mensaje -------------------- */
/*
 * Las tomas son las mismas que usa el receptor (hfdl_viterbi):
 * T1 = 0x6d, T2 = 0x4f sobre los 7 bits del registro.
 */
#define STX_POLYA 0x6dU
#define STX_POLYB 0x4fU

static uint8_t stx_paridad(uint32_t v)
{
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (uint8_t)(v & 1U);
}

/* Un bit de informacion entra; salen 2*rep bits de FEC en parejas
 * T1 T2 T1 T2 ... (NO T1 T1 T2 T2: la norma lo dice y el receptor lo
 * espera asi). Devuelve cuantos puso. */
static uint32_t stx_codifica(stx_t *t, uint8_t bit, uint8_t *sal)
{
    uint32_t r7 = (uint32_t)(((uint32_t)t->reg << 1) | (bit & 1U)) & 0x7FU;
    uint8_t a = stx_paridad(r7 & STX_POLYA);
    uint8_t b = stx_paridad(r7 & STX_POLYB);
    uint8_t k;
    t->reg = (uint8_t)(r7 & 0x3FU);
    for (k = 0U; k < t->rep; k++) { sal[2U * k] = a; sal[(2U * k) + 1U] = b; }
    return 2U * (uint32_t)t->rep;
}

/*
 * El mensaje entero en bits de FEC: SOM, el texto en ASCII de 8 bits con
 * el bit bajo primero, EOM, y cola de ceros para vaciar el registro.
 */
static uint32_t stx_mensaje(stx_t *t, const char *txt, uint8_t *sal, uint32_t max)
{
    static const uint32_t SOM = 0x03873C3CUL, EOM = 0x4B65A5B2UL;
    uint32_t n = 0U;
    int i, k;

    t->reg = 0U;
    for (k = 0; k < (int)t->desfase; k++) {
        if ((n + (2U * t->rep)) > max) { return n; }
        n += stx_codifica(t, (uint8_t)(k & 1), &sal[n]);
    }
    if (!t->sin_marcas) {
        for (k = 31; k >= 0; k--) {
            if ((n + (2U * t->rep)) > max) { return n; }
            n += stx_codifica(t, (uint8_t)((SOM >> k) & 1U), &sal[n]);
        }
    }
    for (i = 0; txt[i]; i++) {
        for (k = 0; k < 8; k++) {           /* LSB primero */
            if ((n + (2U * t->rep)) > max) { return n; }
            n += stx_codifica(t, (uint8_t)(((uint8_t)txt[i] >> k) & 1U), &sal[n]);
        }
    }
    if (!t->sin_marcas) {
        for (k = 31; k >= 0; k--) {
            if ((n + (2U * t->rep)) > max) { return n; }
            n += stx_codifica(t, (uint8_t)((EOM >> k) & 1U), &sal[n]);
        }
    }
    /*
     * LA COLA DE CEROS, Y OCHO BITS MAS SI HACE FALTA.
     *
     * stx_audio() manda este mismo mensaje una y otra vez, como una
     * estacion de verdad. Si el ciclo entero no mide un numero redondo
     * de BYTES, cada repeticion empieza en una fase de byte distinta y
     * el receptor no tiene ninguna fase estable que encontrar. Eso no es
     * un fallo del receptor: es un fallo del banco, y me costo media
     * hora creyendome que el buscador de fase patinaba.
     */
    for (k = 0; k < 144; k++) {
        if ((n + (2U * t->rep)) > max) { return n; }
        n += stx_codifica(t, 0U, &sal[n]);
    }
    {
        int sobra = (int)((8U - (t->desfase % 8U)) % 8U);
        for (k = 0; k < sobra; k++) {
            if ((n + (2U * t->rep)) > max) { return n; }
            n += stx_codifica(t, 0U, &sal[n]);
        }
    }
    return n;
}

/*
 * Y la forma de onda. Los bits se repiten en bucle: una emision de
 * verdad manda el mensaje una y otra vez, y el entrelazado largo
 * necesita diez segundos de relleno antes de soltar el primer dato.
 *
 * Devuelve cuantas muestras ha puesto (a 12 kHz).
 */
static uint32_t stx_audio(stx_t *t, const uint8_t *bits, uint32_t nbits,
                          float *sal, uint32_t max)
{
    uint32_t puestas = 0U, leidos = 0U;
    uint8_t  f[STX_FILAS], c[STX_FILAS];
    uint8_t  canal[384];          /* 128 simbolos x 3 bits, el peor caso */
    uint32_t ncanal = 0U, usados = 0U;
    uint32_t idata = 0U;          /* indice dentro de los 176 */

    if (nbits == 0U) { return 0U; }
    while ((puestas + (uint32_t)STX_TRAMA * STX_SPS) <= max) {
        int i;
        /* el preambulo */
        for (i = 0; i < STX_PRE; i++) {
            uint8_t idx = (uint8_t)((t->pre[i] < 0) ? 4U : 0U);
            puestas += stx_pon_simbolo(t, idx, &sal[puestas]);
        }
        /* los 176: sondeos donde toca, datos en el resto */
        ncanal = 0U; usados = 0U;
        for (idata = 0U; idata < 176U; idata++) {
            uint8_t idx;
            if (t->conocido[idata]) {
                idx = t->scr[idata];           /* el receptor espera fase 0 */
            } else {
                uint8_t b[3];
                int k;
                while ((usados + (uint32_t)t->bits_sim) > ncanal) {
                    /* hace falta otro grupo de 32 */
                    int r, q = 0;
                    for (r = 0; r < STX_FILAS; r++) {
                        f[r] = bits[leidos % nbits]; leidos++;
                    }
                    stx_entrelaza(t, f, c);
                    if (t->bits_sim == 3U) {
                        /* perforado: se tiran las filas 3 modulo 4 */
                        for (r = 0; r < STX_FILAS; r++) {
                            if ((r % 4) != 3) { canal[ncanal + (uint32_t)(q++)] = c[r]; }
                        }
                        ncanal += 24U;
                    } else {
                        for (r = 0; r < STX_FILAS; r++) { canal[ncanal + (uint32_t)r] = c[r]; }
                        ncanal += (uint32_t)STX_FILAS;
                    }
                    if (ncanal > (sizeof canal - 32U)) {
                        /* compactar lo ya usado */
                        uint32_t z;
                        for (z = 0U; z < (ncanal - usados); z++) { canal[z] = canal[usados + z]; }
                        ncanal -= usados; usados = 0U;
                    }
                }
                for (k = 0; k < (int)t->bits_sim; k++) { b[k] = canal[usados + (uint32_t)k]; }
                usados += t->bits_sim;
                idx = stx_simbolo(t, b);
                idx = (uint8_t)((idx + t->scr[idata]) & 7U);
            }
            puestas += stx_pon_simbolo(t, idx, &sal[puestas]);
        }
    }
    return puestas;
}

#endif /* STANAG_TX_H */

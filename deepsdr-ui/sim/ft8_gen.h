/*
 * EL GENERADOR DE RANURAS DE FT8, PARA LOS BANCOS QUE LO NECESITEN.
 *
 * Salio de sim/ft8_banda.c el 28/10/2026, cuando hizo falta un segundo
 * banco -sim/ft8_ranuras.c, el de la maquina de ranuras- que tambien
 * necesitaba fabricar emisiones de verdad. Copiarlo habria sido tener dos
 * codificadores que un dia dicen cosas distintas.
 *
 * Ft8Lib trae el decodificador pero NO el codificador: lo de aqui -CRC,
 * producto por la matriz generadora, codigo Gray y los tres grupos de
 * sincronismo- esta escrito a partir de la estructura del protocolo, con
 * las tablas que Ft8Lib ya publica. Se comprueba solo: si estuviera mal,
 * el decodificador del firmware no sacaria el mensaje.
 */
#ifndef FT8_GEN_H
#define FT8_GEN_H

#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ft8/constants.h"
#include "ft8/crc.h"
#include "ft8/message.h"

/* ------------------------------------------------------- el codificador */
static uint8_t paridad8(uint8_t x)
{
    x ^= (uint8_t)(x >> 4);
    x ^= (uint8_t)(x >> 2);
    x ^= (uint8_t)(x >> 1);
    return (uint8_t)(x & 1U);
}

/* 91 bits (77 de carga + 14 de CRC) -> 174 bits. El producto por la matriz
 * generadora de Ft8Lib, bit a bit. */
static void ldpc_codifica(const uint8_t *m91, uint8_t *cw174)
{
    int i, j;
    uint8_t mascara = (uint8_t)(0x80U >> (FTX_LDPC_K % 8));
    int idx = FTX_LDPC_K_BYTES - 1;

    for (j = 0; j < FTX_LDPC_N_BYTES; j++) {
        cw174[j] = (j < FTX_LDPC_K_BYTES) ? m91[j] : 0U;
    }
    for (i = 0; i < FTX_LDPC_M; i++) {
        uint8_t acc = 0U;
        for (j = 0; j < FTX_LDPC_K_BYTES; j++) {
            acc ^= (uint8_t)(kFTX_LDPC_generator[i][j] & m91[j]);
        }
        if (paridad8(acc)) { cw174[idx] |= mascara; }
        mascara >>= 1;
        if (mascara == 0U) { mascara = 0x80U; idx++; }
    }
}

/* Texto -> los 79 tonos. Devuelve 0 si el texto no se puede codificar. */
static int ft8_tonos(const char *texto, uint8_t tonos[FT8_NN])
{
    ftx_message_t msg;
    uint8_t a91[FTX_LDPC_K_BYTES];
    uint8_t cw[FTX_LDPC_N_BYTES];
    uint8_t mascara = 0x80U;
    int i_byte = 0, i;

    ftx_message_init(&msg);
    if (ftx_message_encode(&msg, 0, texto) != FTX_MESSAGE_RC_OK) { return 0; }

    ftx_add_crc(msg.payload, a91);
    ldpc_codifica(a91, cw);

    for (i = 0; i < FT8_NN; i++) {
        if (i < 7)                  { tonos[i] = kFT8_Costas_pattern[i]; }
        else if (i >= 36 && i < 43) { tonos[i] = kFT8_Costas_pattern[i - 36]; }
        else if (i >= 72)           { tonos[i] = kFT8_Costas_pattern[i - 72]; }
        else {
            uint8_t b3 = 0U;
            int k;
            for (k = 0; k < 3; k++) {
                b3 = (uint8_t)(b3 << 1);
                if (cw[i_byte] & mascara) { b3 |= 1U; }
                mascara >>= 1;
                if (mascara == 0U) { mascara = 0x80U; i_byte++; }
            }
            tonos[i] = kFT8_Gray_map[b3];
        }
    }
    return 1;
}

/* ------------------------------------------------------------ el audio */
#define FS            12000
#define RANURA_N      (15 * FS)
/* 1920 = 0,160 s a 12 kHz. Escrito como numero y comprobado, y no como
 * (int)(FT8_SYMBOL_PERIOD * FS): eso no es una constante de compilacion y
 * no vale para dimensionar un array. */
#define SIMBOLO_N     1920
_Static_assert(SIMBOLO_N == (FS * 160) / 1000,
               "SIMBOLO_N ya no son 0,160 s a la frecuencia de muestreo");

static float g_audio[RANURA_N];

/* Ruido gaussiano con una semilla propia: el banco tiene que dar lo mismo
 * en cada compilacion y en cada maquina, asi que no se usa rand(). */
static uint32_t g_sem;
static void semilla(uint32_t s) { g_sem = s ? s : 1U; }
static uint32_t siguiente(void)
{
    g_sem ^= g_sem << 13; g_sem ^= g_sem >> 17; g_sem ^= g_sem << 5;
    return g_sem;
}
static float gauss(void)
{
    /* Box-Muller, con los dos uniformes abiertos por abajo. */
    float u1 = ((float)(siguiente() >> 8) + 1.0f) / 16777217.0f;
    float u2 = ((float)(siguiente() >> 8) + 1.0f) / 16777217.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.283185307f * u2);
}

/*
 * UNA EMISION, CON EL PULSO GAUSSIANO DE LA NORMA.
 *
 * FT8 es GFSK con BT=2, y eso NO es un detalle del sintetizador: la primera
 * version de este banco repartia la transicion entre tonos a lo largo de un
 * simbolo entero -mas suave, "parecido"- y el resultado fue que una emision
 * a +5 dB no se decodificaba en casi ninguna frecuencia. No fallaba el
 * firmware: fallaba la senal, que con esa transicion solo pasa por su tono
 * nominal un instante y el resto del simbolo lo pasa barriendo.
 *
 * Lo que se mide con una senal que no es la que manda la norma no es el
 * decodificador. Asi que aqui va el pulso de verdad: la diferencia de dos
 * funciones error, que es el pulso rectangular filtrado por la gaussiana,
 * extendido sobre tres simbolos y sumado solapado.
 */
#define GFSK_K   5.336446256636997f   /* pi*sqrt(2/ln2), la constante del BT */
#define GFSK_BT  2.0f

static float g_dfrec[(FT8_NN + 2) * SIMBOLO_N];

static void mete_emision(float f0_hz, float dt_s, float amplitud,
                         const uint8_t *tonos)
{
    double fase = 0.0;
    int i, j, n;
    int n0 = (int)(dt_s * (float)FS + 0.5f);
    int largo = (FT8_NN + 2) * SIMBOLO_N;

    for (n = 0; n < largo; n++) { g_dfrec[n] = 0.0f; }

    for (i = 0; i < FT8_NN; i++) {
        for (j = 0; j < 3 * SIMBOLO_N; j++) {
            float t = (float)j / (float)SIMBOLO_N - 1.5f;
            float a1 = GFSK_K * GFSK_BT * (t + 0.5f);
            float a2 = GFSK_K * GFSK_BT * (t - 0.5f);
            float p  = (erff(a1) - erff(a2)) * 0.5f;
            int   k  = i * SIMBOLO_N + j;
            if (k >= 0 && k < largo) { g_dfrec[k] += (float)tonos[i] * p; }
        }
    }
    /* Los medios simbolos del principio y del final: el primer y el ultimo
     * tono se prolongan, que es lo que hace ft8_lib para que la emision no
     * arranque ni acabe con un escalon. */
    for (j = 0; j < SIMBOLO_N; j++) {
        float t = (float)j / (float)SIMBOLO_N - 0.5f;
        float p = (erff(GFSK_K * GFSK_BT * (t + 0.5f)) + 1.0f) * 0.5f;
        g_dfrec[j] += (float)tonos[0] * (1.0f - p);
        t = (float)j / (float)SIMBOLO_N;
        p = (erff(GFSK_K * GFSK_BT * (t - 0.5f)) + 1.0f) * 0.5f;
        g_dfrec[(FT8_NN - 1) * SIMBOLO_N + 2 * SIMBOLO_N - SIMBOLO_N + j]
            += 0.0f * p;   /* ya lo cubre el solape de tres simbolos */
    }

    for (n = 0; n < (FT8_NN + 1) * SIMBOLO_N; n++) {
        double f = (double)f0_hz + (double)g_dfrec[n] * 6.25;
        int    k = n0 + n - SIMBOLO_N / 2;   /* el pulso adelanta medio simbolo */
        fase += 6.283185307179586 * f / (double)FS;
        if (fase > 6.283185307179586) { fase -= 6.283185307179586; }
        if (k >= 0 && k < RANURA_N) {
            g_audio[k] += (float)(amplitud * sin(fase));
        }
    }
}

/*
 * La relacion senal/ruido de FT8 se da SIEMPRE en 2.500 Hz de referencia,
 * que es como la escribe WSJT-X. El ruido de aqui es blanco sobre los
 * 6.000 Hz de banda que hay a 12 kHz de muestreo, asi que hay que llevar
 * la cuenta de una banda a la otra antes de despejar la amplitud.
 */
#define RUIDO_SIGMA 1000.0f
static float amplitud_para(float snr_db)
{
    float pn_ref = RUIDO_SIGMA * RUIDO_SIGMA * (2500.0f / 6000.0f);
    float ps = pn_ref * powf(10.0f, snr_db * 0.1f);
    return sqrtf(2.0f * ps);
}

static void ranura_vacia(uint32_t sem)
{
    int n;
    semilla(sem);
    for (n = 0; n < RANURA_N; n++) { g_audio[n] = RUIDO_SIGMA * gauss(); }
}


#endif /* FT8_GEN_H */

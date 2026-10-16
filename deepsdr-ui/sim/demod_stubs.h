/* Ver sim/demod_stubs.c: los consumidores del camino de audio, vacios y
 * contados, para poder medir el REPARTO que hace demod_am.c. */
#ifndef DEMOD_STUBS_H
#define DEMOD_STUBS_H

#include <stdint.h>

enum {
    CONS_AIS = 0, CONS_ALE, CONS_ANALIZ, CONS_APRS, CONS_CW, CONS_DCF77,
    CONS_DSC, CONS_FT8, CONS_HELL, CONS_HFDL, CONS_IDENT, CONS_JTTY,
    CONS_NAVTEX, CONS_PSK31, CONS_RDS, CONS_RTTY, CONS_SCOPE, CONS_SSTV,
    CONS_STANAG, CONS_WEFAX, CONS_WSPR, CONS_BIP, CONS_I2S,
    CONS_N
};

typedef struct {
    uint8_t   activo;    /* lo mueve el banco: X_activo() devuelve esto */
    uint32_t  llamadas;  /* cuantas veces le han dado muestras */
    uint32_t  muestras;  /* cuantas en total */
    uintptr_t ultimo;    /* el puntero de la ultima entrega */
    uint32_t  ultimo_n;  /* y cuantas muestras llevaba */
} consumidor_t;

extern consumidor_t g_cons[CONS_N];

const char *cons_nombre(int i);
void cons_reinicia(void);          /* pone a cero los contadores */
void cons_todos_apagados(void);    /* apaga los veintitantos */
void cons_dcf77_fase(uint8_t si);  /* ALS162 (fase) contra las demas */
void cons_dcf77_submuestras(int n);
void cons_bloque_corrupto(uint8_t si);
void cons_captura_tramas(uint32_t tramas); /* cuantas tramas copia el I2S */
void cons_paso_ciclos(uint32_t c);         /* cuanto avanza DWT por bloque */
const int16_t *cons_audio(void);           /* la ultima media que se mando */

#endif

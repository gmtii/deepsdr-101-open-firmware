/* Ver ecualiz.h para el porque. Aqui solo esta el como. */
#include "ecualiz.h"

/* Cuanto pesa cada muestra nueva en las medias suavizadas. 1/64 son unos
 * 27 ms a 2400 baudios: suficiente para que el numero no tiemble y bastante
 * mas rapido que la propia convergencia del lazo, que es lo que mide. */
#define ECU_SUAVE   (1.0f / 64.0f)

void ecu_init(ecu_t *e, uint8_t taps, float mu, float r2)
{
    if (taps == 0U) { taps = 1U; }
    if (taps > ECU_TAPS_MAX) { taps = ECU_TAPS_MAX; }
    e->n    = taps;
    e->mu   = mu;
    e->r2   = (r2 > 0.0f) ? r2 : 1.0f;
    e->modo = ECU_CIEGO;
    ecu_reset(e);
}

void ecu_reset(ecu_t *e)
{
    uint8_t i;
    for (i = 0U; i < ECU_TAPS_MAX; i++) {
        e->wr[i] = 0.0f; e->wi[i] = 0.0f;
        e->xr[i] = 0.0f; e->xi[i] = 0.0f;
    }
    /*
     * El centro a 1 y los demas a cero. Arrancar todo a cero deja la salida
     * muerta y el CMA no sabe por donde empezar -su gradiente tambien es
     * cero-; arrancar con ruido tarda mas y a veces converge a un retardo
     * distinto del que esperaba quien lo llama.
     */
    e->wr[e->n / 2U] = 1.0f;
    e->ptr  = 0U;
    e->disp = 0.0f;
    e->err  = 0.0f;
}

void ecu_modo(ecu_t *e, uint8_t modo)
{
    e->modo = modo;
}

void ecu_paso(ecu_t *e, float mu)
{
    e->mu = mu;
}

void ecu_mete(ecu_t *e, float xr, float xi, float *yr, float *yi)
{
    uint8_t k, j;
    float ar = 0.0f, ai = 0.0f;

    /* la muestra nueva entra por la cabeza */
    e->ptr = (uint8_t)((e->ptr == 0U) ? (e->n - 1U) : (e->ptr - 1U));
    e->xr[e->ptr] = xr;
    e->xi[e->ptr] = xi;

    /* y = suma de w[k] * x[k], producto complejo */
    j = e->ptr;
    for (k = 0U; k < e->n; k++) {
        float br = e->xr[j], bi = e->xi[j];
        ar += e->wr[k] * br - e->wi[k] * bi;
        ai += e->wr[k] * bi + e->wi[k] * br;
        j = (uint8_t)((j + 1U) % e->n);
    }
    *yr = ar;
    *yi = ai;
}

void ecu_corrige(ecu_t *e, float yr, float yi, float dr, float di)
{
    uint8_t k, j;
    float er, ei, p;

    if (e->modo == ECU_DIRIGIDO) {
        /* error = y - decision */
        er = yr - dr;
        ei = yi - di;
        e->err += ECU_SUAVE * ((er * er + ei * ei) - e->err);
    } else {
        /*
         * CMA de Godard con p=2: error = y * (|y|^2 - R2). Lo que persigue no
         * es un simbolo, es el MODULO; por eso funciona sin saber la fase y
         * por eso deja la salida girada.
         */
        p  = yr * yr + yi * yi - e->r2;
        er = yr * p;
        ei = yi * p;
        e->disp += ECU_SUAVE * ((p * p) - e->disp);
    }

    /* w[k] -= mu * error * conj(x[k]) */
    j = e->ptr;
    for (k = 0U; k < e->n; k++) {
        float br = e->xr[j], bi = e->xi[j];
        /* error * conj(x) */
        float gr = er * br + ei * bi;
        float gi = ei * br - er * bi;
        e->wr[k] -= e->mu * gr;
        e->wi[k] -= e->mu * gi;
        j = (uint8_t)((j + 1U) % e->n);
    }
}

float ecu_dispersion(const ecu_t *e) { return e->disp; }
float ecu_error(const ecu_t *e)      { return e->err;  }

float ecu_energia(const ecu_t *e)
{
    uint8_t k;
    float s = 0.0f;
    for (k = 0U; k < e->n; k++) {
        s += e->wr[k] * e->wr[k] + e->wi[k] * e->wi[k];
    }
    return s;
}

/*
 * El AGC. Ver agc.h para por que esto es un fichero y no dos bucles
 * copiados dentro de demod_am.c, y para la aritmetica del inverso.
 */
#include "agc.h"

static int16_t recorta(float y)
{
    int32_t out = (int32_t)y;

    if (out > 32767)  { out = 32767; }
    if (out < -32768) { out = -32768; }
    return (int16_t)out;
}

void agc_manual(const float *entra, int16_t *sale, uint32_t n, float ganancia)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        int16_t v = recorta(entra[i] * ganancia);

        sale[2U * i]      = v;
        sale[2U * i + 1U] = v;
    }
}

float agc_bloque(const float *entra, int16_t *sale, uint32_t n,
                 float pico, float relajacion, float pico_min,
                 float ganancia)
{
    /*
     * inv_rel y inv_min se calculan UNA vez por bloque. inv_min no es una
     * comodidad: sin el, el "else if" de abajo hacia una division por
     * muestra SIEMPRE QUE LA SEÑAL ESTUVIERA POR DEBAJO DEL SUELO, o sea
     * en todos los silencios -que es la mitad del tiempo que se escucha
     * una radio-. La optimizacion del 05/10/2026 quito la division del
     * caso con señal y dejo intacta la del caso sin ella.
     */
    float inv_rel = 1.0f / relajacion;
    float inv_min = 1.0f / pico_min;
    float inv     = (pico < pico_min) ? inv_min : (1.0f / pico);
    uint32_t i;

    for (i = 0U; i < n; i++) {
        float mag = (entra[i] < 0.0f) ? -entra[i] : entra[i];
        float ganan;

        pico *= relajacion;
        inv  *= inv_rel;
        if (mag > pico) {
            /* Ataque instantaneo: el pico salta a la muestra. Es el unico
             * caso que obliga a dividir de verdad, y aun asi no si la
             * muestra esta por debajo del suelo. */
            pico = mag;
            inv  = (pico < pico_min) ? inv_min : (1.0f / pico);
        } else if (pico < pico_min) {
            inv = inv_min;
        }

        ganan = AGC_TARGET * inv;
        if (ganan > AGC_GAIN_MAX) { ganan = AGC_GAIN_MAX; }

        sale[2U * i]      = recorta(entra[i] * ganan * ganancia);
        sale[2U * i + 1U] = sale[2U * i];
    }

    return pico;
}

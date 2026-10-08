/* Ver iqancho.h para el porque. Aqui solo esta el como. */
#include "iqancho.h"
#include <math.h>

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/*
 * Paso de adaptacion. Pequeño a proposito: lo que se corrige aqui no cambia
 * con el tiempo, cambia con la FRECUENCIA, o sea que solo se mueve cuando el
 * dueño mueve el dial. Con 0,02 y bloques de 256 muestras, los coeficientes
 * se asientan en unos dos segundos, que es del orden de lo que tarda uno en
 * mirar la pantalla despues de sintonizar.
 */
#define IQA_MU      0.02f

/*
 * FUGA. Cada ajuste tira ademas un poquito de los coeficientes hacia cero.
 *
 * No es un adorno: sin ella, con la entrada YA EQUILIBRADA -que es el caso en
 * el que esto no tiene que hacer nada- los coeficientes se van paseando
 * detras del ruido de la propia estimacion y acaban valiendo algo. En el
 * banco llegaban a 0,15 sobre una señal perfecta. Eso es ruido añadido a
 * cambio de nada.
 *
 * Con la fuga, el punto de reposo deja de ser "donde me lleve el ruido" y
 * pasa a ser cero, y solo se sale de ahi mientras haya una correlacion de
 * verdad empujando. El precio es un sesgo pequeño en la correccion -queda
 * algo corta- y vale la pena: 1/256 por bloque contra un mu de 0,02 deja el
 * sesgo en menos del 0,2 por ciento.
 */
#define IQA_FUGA    (1.0f / 256.0f)

/*
 * Se ADAPTA uno de cada dos bloques; se FILTRA siempre.
 *
 * El filtro no se puede saltar: es la salida. La correlacion si, y es la
 * mitad del coste. Esto corre dentro de la interrupcion del DMA a 96 kHz, y
 * lo que de verdad limita cuantos coeficientes se pueden poner no es la
 * memoria sino los ciclos de ahi dentro. Adaptar la mitad de veces duplica el
 * tiempo de convergencia -de unos dos segundos a unos cuatro- y eso no se
 * nota: lo que se corrige aqui solo cambia cuando el dueño mueve el dial.
 */
#define IQA_UNO_DE  2U

/* Debajo de esto no hay señal que medir y no se adapta: con la entrada
 * muerta, la correlacion sale de dividir ruido de cuantizacion entre ruido
 * de cuantizacion. Es el mismo umbral que usa iqbal.c. */
#define IQA_POT_MIN 100.0f

/* La linea de retardo tiene que cubrir el filtro Y el retardo del camino
 * principal, mas el bloque mas largo que puede llegar. */
#define IQA_HIST    (IQA_TAPS + IQA_D + 2U)

static uint8_t s_on        TCMRAM_BSS;
static float   s_wr[IQA_TAPS] TCMRAM_BSS;
static float   s_wi[IQA_TAPS] TCMRAM_BSS;
/* Cola de las ultimas muestras de ENTRADA, que es lo que necesita el filtro:
 * circular, con la cabeza en s_h. */
static float   s_zr[IQA_HIST] TCMRAM_BSS;
static float   s_zi[IQA_HIST] TCMRAM_BSS;
static uint8_t s_h         TCMRAM_BSS;
static uint8_t s_turno     TCMRAM_BSS;

void iqa_init(void)
{
    uint8_t k;
    s_on = 1U;
    for (k = 0U; k < IQA_TAPS; k++) { s_wr[k] = 0.0f; s_wi[k] = 0.0f; }
    for (k = 0U; k < IQA_HIST; k++) { s_zr[k] = 0.0f; s_zi[k] = 0.0f; }
    s_h = 0U;
    s_turno = 0U;
}

void    iqa_set_on(uint8_t on) { s_on = on ? 1U : 0U; }
uint8_t iqa_get_on(void)       { return s_on; }

/* La muestra de hace d, con d = 0 la recien metida. */
static inline uint8_t atras(uint8_t d)
{
    return (uint8_t)((s_h + d) % IQA_HIST);
}

void iqa_bloque(int16_t *crudo, uint32_t n)
{
    uint32_t j;
    uint8_t  k;
    /* Correlacion SIN conjugar de la salida, por retardo. Es lo que tiene que
     * valer cero si no hay desequilibrio. Ver iqancho.h. */
    float cr[IQA_TAPS], ci[IQA_TAPS];
    float pot = 0.0f;
    /* Las ultimas IQA_TAPS salidas, para poder correlacionar y[n] con
     * y[n-l] sin guardar el bloque entero. */
    float yr_h[IQA_TAPS], yi_h[IQA_TAPS];
    uint8_t yh = 0U;
    uint32_t nval = 0U;
    uint8_t  adapta;

    if (n == 0U) { return; }
    adapta = (uint8_t)(s_turno == 0U);
    s_turno = (uint8_t)((s_turno + 1U) % IQA_UNO_DE);
    for (k = 0U; k < IQA_TAPS; k++) {
        cr[k] = 0.0f; ci[k] = 0.0f;
        yr_h[k] = 0.0f; yi_h[k] = 0.0f;
    }

    for (j = 0U; j < n; j++) {
        float zr = (float)crudo[2U * j];
        float zi = (float)crudo[2U * j + 1U];
        float yr, yi;

        /* entra por la cabeza */
        s_h = (uint8_t)((s_h == 0U) ? (IQA_HIST - 1U) : (s_h - 1U));
        s_zr[s_h] = zr;
        s_zi[s_h] = zi;

        /* y = z[n] + suma w[k] * conj(z[n-k]); ver IQA_D en iqancho.h */
        {
            uint8_t d = atras((uint8_t)IQA_D);
            yr = s_zr[d];
            yi = s_zi[d];
        }
        for (k = 0U; k < IQA_TAPS; k++) {
            uint8_t d = atras(k);
            float ar = s_zr[d], ai = -s_zi[d];   /* conj(z[n-k]) */
            yr += s_wr[k] * ar - s_wi[k] * ai;
            yi += s_wr[k] * ai + s_wi[k] * ar;
        }

        pot += zr * zr + zi * zi;

        /*
         * La correlacion sin conjugar, y[n]*y[n-l], para l = 0 .. IQA_TAPS-1.
         *
         * *** EL RETARDO CERO ENTRA, Y ES EL IMPORTANTE. La primera version
         * guardaba y[n] DESPUES de correlacionar, asi que lo que medía eran
         * los retardos 1..4 y el 0 no estaba. El retardo 0 es E{y^2}, o sea
         * exactamente la parte PLANA del desequilibrio -la grande-; sin el,
         * los cuatro coeficientes se ajustaban para anular un sistema que no
         * era el que habia que anular. Se veia en el banco: valia en 5 y en
         * 25 kHz y no valia en 15, 35 y 45. ***
         *
         * Se empieza a contar cuando ya hay IQA_TAPS salidas de verdad en la
         * cola: antes, y[n-l] son los ceros del arranque y meterlos sesga la
         * medida hacia cero, o sea hacia "no hace falta corregir nada".
         */
        yh = (uint8_t)((yh == 0U) ? (IQA_TAPS - 1U) : (yh - 1U));
        yr_h[yh] = yr;
        yi_h[yh] = yi;

        if (adapta && j >= (uint32_t)IQA_TAPS) {
            nval++;
            for (k = 0U; k < IQA_TAPS; k++) {
                uint8_t d = (uint8_t)((yh + k) % IQA_TAPS);
                float br = yr_h[d], bi = yi_h[d];
                cr[k] += yr * br - yi * bi;
                ci[k] += yr * bi + yi * br;
            }
        }

        if (s_on) {
            /* La salida saturada, igual que en iqbal.c: envolver convierte un
             * pico en un salto de escala entera, que en la FFT es un suelo de
             * ruido nuevo repartido por toda la pantalla. */
            if (yr >  32767.0f) { yr =  32767.0f; }
            if (yr < -32768.0f) { yr = -32768.0f; }
            if (yi >  32767.0f) { yi =  32767.0f; }
            if (yi < -32768.0f) { yi = -32768.0f; }
            crudo[2U * j]      = (int16_t)yr;
            crudo[2U * j + 1U] = (int16_t)yi;
        }
    }

    if (nval == 0U) { return; }
    pot /= (float)n;
    if (pot < IQA_POT_MIN) { return; }

    /*
     * w[l] -= mu * c_l / P, con el retardo cero a la mitad de paso.
     *
     * Ver la deduccion en iqancho.h: con la entrada razonablemente blanca y
     * sin retardo en el camino principal,
     *
     *     c_0 = C_z(0) + 2*P*w[0]      c_l = C_z(l) + P*w[l]   (l >= 1)
     *
     * El dos del retardo cero sale de que ahi los dos terminos del
     * desarrollo caen sobre el MISMO coeficiente. No es un detalle fino: es
     * el coeficiente que lleva la parte plana del desequilibrio, o sea la
     * grande, y pasarse de paso justo ahi es lo que hace que el lazo
     * oscile en vez de asentarse.
     */
    {
        float inv = 1.0f / ((float)nval * pot);
        for (k = 0U; k < IQA_TAPS; k++) {
            float mu = (k == 0U) ? (IQA_MU * 0.5f) : IQA_MU;
            float nr = s_wr[k] * (1.0f - IQA_FUGA) - mu * cr[k] * inv;
            float ni = s_wi[k] * (1.0f - IQA_FUGA) - mu * ci[k] * inv;
            float m2 = nr * nr + ni * ni;
            /* Tope por modulo y no por componente: recortar la parte real y
             * la imaginaria por separado GIRA el coeficiente, que es cambiar
             * la correccion por otra distinta en vez de limitarla. */
            if (m2 > (IQA_W_MAX * IQA_W_MAX)) {
                float e = IQA_W_MAX / sqrtf(m2);
                nr *= e; ni *= e;
            }
            s_wr[k] = nr;
            s_wi[k] = ni;
        }
    }
}

float iqa_energia(void)
{
    uint8_t k;
    float s = 0.0f;
    for (k = 0U; k < IQA_TAPS; k++) { s += s_wr[k] * s_wr[k] + s_wi[k] * s_wi[k]; }
    return s;
}

float iqa_mayor(void)
{
    uint8_t k;
    float m = 0.0f;
    for (k = 0U; k < IQA_TAPS; k++) {
        float v = s_wr[k] * s_wr[k] + s_wi[k] * s_wi[k];
        if (v > m) { m = v; }
    }
    return sqrtf(m);
}

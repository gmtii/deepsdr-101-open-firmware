#include "analizador.h"

/* ---------------------------------------------------------------------------
 * EL FILTRO DE DIEZMADO
 *
 * Una semibanda de 23 coeficientes, simetrica, disenada con remez para dejar
 * plana la banda 0..0,20 y rechazar de 0,30 en adelante. Medido:
 *
 *     taps   rizado en banda   rechazo
 *      15      0,919 dB         35,5 dB
 *      23      0,213 dB         47,8 dB   <- elegido
 *      31      0,052 dB         59,9 dB
 *      39      0,013 dB         72,0 dB
 *
 * 23 es donde la curva deja de pagar: de 15 a 23 se ganan 12 dB de rechazo
 * por cuatro multiplicaciones mas, y de 23 a 31 otros 12 dB por otras cuatro
 * - pero 47,8 dB ya estan muy por debajo de lo que el resto de la cadena
 * aporta, asi que seguir subiendo solo gasta.
 *
 * En cascada de ocho etapas la caida acumulada en el borde de la banda util
 * es de 0,555 dB. Eso es lo que justifica quedarse en 0,8 de Nyquist y no
 * intentar llegar al final: mas alla, el rechazo se hunde y lo que se
 * pintaria serian alias, o sea rayas de señales que no estan ahi.
 * ------------------------------------------------------------------------- */
#define FIR_N 23
static const float k_fir[FIR_N] = {
    -0.00358518f,  0.00487305f,  0.01383466f, -0.00318823f, -0.02467051f,
     0.00519254f,  0.04712661f, -0.00616945f, -0.09467922f,  0.00690488f,
     0.31096557f,  0.48679057f,  0.31096557f,  0.00690488f, -0.09467922f,
    -0.00616945f,  0.04712661f,  0.00519254f, -0.02467051f, -0.00318823f,
     0.01383466f,  0.00487305f, -0.00358518f
};

/* Ocho etapas: de 96 kHz se baja hasta 375 Hz. */
#define ETAPAS 8

/* La banda util acaba aqui, en fraccion de la frecuencia de Nyquist de la
 * etapa. Ver el comentario del filtro. */
#define BANDA_UTIL 0.80f

/*
 * LA HISTORIA, POR DUPLICADO - 05/10/2026.
 *
 * *** El dueño: "quiero que repases absolutamente todo de la radio y busques
 * razones que hagan que se ralentice tanto la radio completa como el
 * espectro". ***
 *
 * Y el analizador de audio, con el zoom de 300 Hz, era la cosa mas cara de
 * toda la interrupcion de audio: ocho etapas en cascada, veintitres tomas
 * cada una... y un MODULO POR 23 en el bucle interno, una vez por toma. 23
 * no es potencia de dos, asi que eso no es un AND: es una division entera de
 * verdad, y van unas seis mil por bloque.
 *
 * El truco de siempre, y en este mismo proyecto ya esta usado en rds.c:
 * guardar cada muestra DOS VECES, en z[pos] y en z[pos+FIR_N]. Con eso la
 * convolucion recorre FIR_N posiciones seguidas desde `pos` sin salirse
 * nunca del array -pos < FIR_N, asi que pos+FIR_N-1 < 2*FIR_N- y lee
 * exactamente los mismos valores en el mismo orden que la vuelta con modulo.
 *
 * Cuesta 92 bytes mas por etapa. El bucle se queda en multiplicar y sumar,
 * que es lo unico que de verdad hay que hacer.
 */
typedef struct {
    float   z[2U * FIR_N];  /* historia, escrita por duplicado */
    uint8_t n;          /* cuantas muestras han entrado desde la ultima salida */
    uint8_t pos;        /* donde escribir en z[] */
} etapa_t;

static etapa_t s_et[ETAPAS];
static float   s_fs = 96000.0f;
static uint8_t s_zoom = (uint8_t)ANALIZ_300;
static uint8_t s_activo;

/* El buffer de analisis. Se llena a la salida de la etapa elegida. */
static int16_t  s_buf[FFT_SIZE];
static uint16_t s_llenado;
static uint8_t  s_listo;

/* Cuantas etapas se usan en cada zoom. 2400 Hz de tope con audio a 96 kHz
 * salen de cuatro etapas: 96000/16 = 6000, Nyquist 3000, util 2400. */
static const uint8_t k_etapas[ANALIZ_N] = { 4U, 5U, 6U, 7U, 8U };

void analiz_start(float fs_hz)
{
    uint8_t e;
    uint16_t k;

    s_fs = (fs_hz > 1.0f) ? fs_hz : 1.0f;
    for (e = 0U; e < ETAPAS; e++) {
        for (k = 0U; k < 2U * FIR_N; k++) { s_et[e].z[k] = 0.0f; }
        s_et[e].n = 0U;
        s_et[e].pos = 0U;
    }
    for (k = 0U; k < FFT_SIZE; k++) { s_buf[k] = 0; }
    s_llenado = 0U;
    s_listo = 0U;
    s_activo = 1U;
}

void analiz_stop(void) { s_activo = 0U; }
uint8_t analiz_activo(void) { return s_activo; }

void analiz_zoom(analiz_zoom_t z)
{
    if ((uint8_t)z >= (uint8_t)ANALIZ_N) { return; }
    if ((uint8_t)z == s_zoom) { return; }
    s_zoom = (uint8_t)z;
    /* Lo que hubiera a medio llenar es de otra tasa: no se mezcla. */
    s_llenado = 0U;
    s_listo = 0U;
}

analiz_zoom_t analiz_zoom_actual(void) { return (analiz_zoom_t)s_zoom; }

float analiz_hz_por_bin(void)
{
    /* La etapa k trabaja a fs/2^k, y la FFT reparte esa tasa entre FFT_SIZE
     * bins. Los FFT_BINS_USEFUL primeros cubren hasta Nyquist. */
    float fs_e = s_fs;
    uint8_t k;
    for (k = 0U; k < k_etapas[s_zoom]; k++) { fs_e *= 0.5f; }
    return fs_e / (float)FFT_SIZE;
}

float analiz_tope_hz(void)
{
    return analiz_hz_por_bin() * (float)FFT_BINS_USEFUL * BANDA_UTIL;
}

uint16_t analiz_bins_utiles(void)
{
    return (uint16_t)((float)FFT_BINS_USEFUL * BANDA_UTIL);
}

/* Una muestra por una etapa. Devuelve 1 y deja la salida en *out cuando esa
 * etapa produce muestra (o sea, una de cada dos). */
static uint8_t etapa_mete(etapa_t *e, float x, float *out)
{
    uint16_t k, p;
    float acc = 0.0f;

    e->z[e->pos] = x;
    e->z[e->pos + FIR_N] = x;          /* la copia: ver etapa_t */
    e->pos++;
    if (e->pos >= (uint8_t)FIR_N) { e->pos = 0U; }

    e->n++;
    if (e->n < 2U) { return 0U; }
    e->n = 0U;

    /* Convolucion. Se recorre la historia desde la mas antigua, que es la
     * que esta justo donde se va a escribir la siguiente. Seguida y sin dar
     * la vuelta, porque la copia de arriba la deja entera a partir de pos. */
    p = e->pos;
    for (k = 0U; k < FIR_N; k++) {
        acc += e->z[p + k] * k_fir[k];
    }
    *out = acc;
    return 1U;
}

void analiz_feed(const float *audio, uint32_t n)
{
    uint32_t i;
    uint8_t  etapas = k_etapas[s_zoom];

    if (s_listo) { return; }   /* el anterior aun no se ha recogido */

    for (i = 0U; i < n; i++) {
        float x = audio[i];
        uint8_t e;
        uint8_t vivo = 1U;

        for (e = 0U; e < etapas && vivo; e++) {
            float y;
            vivo = etapa_mete(&s_et[e], x, &y);
            x = y;
        }
        if (!vivo) { continue; }

        /*
         * A int16. La FFT toma enteros porque es la que ya existia y la que
         * usa el espectro de radiofrecuencia; el factor da igual porque lo
         * que se pinta son dB relativos, pero el recorte NO da igual: una
         * muestra recortada se convierte en armonicos, o sea en rayas
         * inventadas. Por eso se recorta con tope explicito en vez de dejar
         * que el cast de vueltas.
         */
        {
            float v = x;
            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            s_buf[s_llenado++] = (int16_t)v;
        }
        if (s_llenado >= FFT_SIZE) {
            s_llenado = 0U;
            s_listo = 1U;
            return;
        }
    }
}

uint8_t analiz_listo(void) { return s_listo; }

void analiz_bins(float *db_out)
{
    fft_compute_db(s_buf, db_out);
    s_listo = 0U;
}

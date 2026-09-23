#include "spec_agc.h"

/*
 * Percentiles por histograma, que es lo que permite hacerlo sin ordenar
 * y sin memoria extra digna de mencion: 128 casillas de 16 bits sobre el
 * rango completo son 256 bytes y un recorrido de los datos.
 *
 * La resolucion que se pierde por cuantizar es el ancho de casilla,
 * 1,17 dB sobre los 150 dB de rango. Frente a los margenes de 3 y 6 dB
 * que se aplican despues, es ruido.
 */
#define AGC_BUCKETS 128U

static uint16_t s_hist[AGC_BUCKETS];

static float bucket_to_db(uint32_t b)
{
    return SPEC_AGC_DB_FLOOR +
           ((float)b + 0.5f) * ((SPEC_AGC_DB_CEIL - SPEC_AGC_DB_FLOOR) / (float)AGC_BUCKETS);
}

void spec_agc_target(const float *db, uint32_t n, float *out_min, float *out_max)
{
    const float escala = (float)AGC_BUCKETS / (SPEC_AGC_DB_CEIL - SPEC_AGC_DB_FLOOR);
    uint32_t i, acc, meta_lo, meta_hi;
    float p_lo, p_hi, target_min, target_max;

    for (i = 0U; i < AGC_BUCKETS; i++) { s_hist[i] = 0U; }

    for (i = 0U; i < n; i++) {
        float v = db[i];
        int32_t b;

        if (v < SPEC_AGC_DB_FLOOR) { v = SPEC_AGC_DB_FLOOR; }
        if (v > SPEC_AGC_DB_CEIL)  { v = SPEC_AGC_DB_CEIL; }
        b = (int32_t)((v - SPEC_AGC_DB_FLOOR) * escala);
        if (b < 0) { b = 0; }
        if (b >= (int32_t)AGC_BUCKETS) { b = (int32_t)AGC_BUCKETS - 1; }
        s_hist[b]++;
    }

    meta_lo = (uint32_t)(SPEC_AGC_PCT_LO * (float)n);
    meta_hi = (uint32_t)(SPEC_AGC_PCT_HI * (float)n);

    p_lo = SPEC_AGC_DB_FLOOR;
    p_hi = SPEC_AGC_DB_CEIL;
    acc = 0U;
    for (i = 0U; i < AGC_BUCKETS; i++) {
        uint32_t antes = acc;
        acc += s_hist[i];
        if (antes <= meta_lo && acc > meta_lo) { p_lo = bucket_to_db(i); }
        if (antes <= meta_hi && acc > meta_hi) { p_hi = bucket_to_db(i); break; }
    }

    target_min = p_lo - SPEC_AGC_FLOOR_MARGIN_DB;
    /* Recorrido minimo garantizado: sin senal, el percentil alto y el
     * bajo casi se tocan, y sin este suelo la ventana se cerraria sobre
     * el ruido y no quedaria sitio donde ver aparecer nada. */
    target_max = target_min + SPEC_AGC_MIN_SPAN_DB;
    if (p_hi + SPEC_AGC_CEIL_MARGIN_DB > target_max) {
        target_max = p_hi + SPEC_AGC_CEIL_MARGIN_DB;
    }

    if (target_min < SPEC_AGC_DB_FLOOR) { target_min = SPEC_AGC_DB_FLOOR; }
    if (target_max > SPEC_AGC_DB_CEIL)  { target_max = SPEC_AGC_DB_CEIL; }
    if (target_max < target_min + SPEC_AGC_DB_MIN_GAP) {
        target_max = target_min + SPEC_AGC_DB_MIN_GAP;
    }

    *out_min = target_min;
    *out_max = target_max;
}

void spec_agc_step(const float *db, uint32_t n, float *db_min, float *db_max)
{
    float tmin, tmax;

    spec_agc_target(db, n, &tmin, &tmax);
    *db_min += (tmin - *db_min) * SPEC_AGC_SMOOTH_ALPHA;
    *db_max += (tmax - *db_max) * SPEC_AGC_SMOOTH_ALPHA;
}

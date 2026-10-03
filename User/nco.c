#include "nco.h"
#include <math.h>

#define NCO_BITS   10U
#define NCO_N      (1UL << NCO_BITS)          /* 1024 puntos */
#define NCO_FRAC   (32U - NCO_BITS)           /* bits que quedan de fraccion */
#define NCO_FRAC_1 (1.0f / 4194304.0f)        /* 1 / 2^22 */

/* Una vuelta entera MAS el punto de cierre, para que la interpolacion del
 * ultimo tramo no tenga que dar la vuelta al indice con un modulo. Cuesta
 * cuatro bytes y quita una rama del bucle mas caliente que hay. */
static float s_sen[NCO_N + 1U];
static uint8_t s_listo = 0U;

void nco_init(void)
{
    uint32_t k;

    if (s_listo) {
        return;
    }
    for (k = 0U; k <= NCO_N; k++) {
        s_sen[k] = sinf(2.0f * 3.14159265358979f * (float)k / (float)NCO_N);
    }
    s_listo = 1U;
}

/* Seno y coseno de una fase de 32 bits, interpolados. El coseno es el mismo
 * seno un cuarto de vuelta por delante: 2^30 en el acumulador. */
static void sen_cos(uint32_t fase, float *sen, float *cos_)
{
    uint32_t i;
    float f;

    i = fase >> NCO_FRAC;
    f = (float)(fase & 0x003FFFFFUL) * NCO_FRAC_1;
    *sen = s_sen[i] + f * (s_sen[i + 1U] - s_sen[i]);

    fase += 0x40000000UL;                  /* +90 grados */
    i = fase >> NCO_FRAC;
    f = (float)(fase & 0x003FFFFFUL) * NCO_FRAC_1;
    *cos_ = s_sen[i] + f * (s_sen[i + 1U] - s_sen[i]);
}

void nco_freq(nco_t *o, float hz, float fs_hz)
{
    double v;

    if (fs_hz <= 0.0f) {
        o->inc = 0U;
        return;
    }
    /*
     * En double y no en float a proposito. El incremento es hz/fs * 2^32, y
     * 2^32 tiene 33 bits significativos: un float solo lleva 24, asi que
     * redondear aqui en float tiraria los nueve ultimos bits del incremento
     * y dejaria el error de frecuencia en decimas de hercio en vez de en
     * cienmilesimas. Es una division por muestra... no, es una division por
     * CAMBIO de frecuencia, que pasa cuando alguien mueve el mando. Gratis.
     */
    v = (double)hz / (double)fs_hz;
    v = v - floor(v);                      /* a [0,1): lo de fuera no significa nada */
    o->inc = (uint32_t)(v * 4294967296.0 + 0.5);
}

float nco_freq_real(const nco_t *o, float fs_hz)
{
    double f = (double)o->inc / 4294967296.0;
    if (f > 0.5) {
        f -= 1.0;                          /* por encima de Fs/2 es una frecuencia negativa */
    }
    return (float)(f * (double)fs_hz);
}

void nco_fase_cero(nco_t *o)
{
    o->fase = 0U;
}

void nco_paso(nco_t *o, float *sen, float *cos_)
{
    sen_cos(o->fase, sen, cos_);
    o->fase += o->inc;
}

void nco_mezcla(nco_t *o, float *i_buf, float *q_buf, uint32_t n)
{
    uint32_t fase = o->fase;
    uint32_t inc = o->inc;
    uint32_t k;

    /*
     * (I + jQ) * (cos - j sen) = (I cos + Q sen) + j (Q cos - I sen).
     *
     * Con f = +Fs/4 esto da 1, -j, -1, +j, que es lo que ya hacia a mano
     * demod_am.c. Lo comprueba sim/ncotest.c muestra a muestra.
     */
    for (k = 0U; k < n; k++) {
        float s, c, vi, vq;

        sen_cos(fase, &s, &c);
        vi = i_buf[k];
        vq = q_buf[k];
        i_buf[k] = vi * c + vq * s;
        q_buf[k] = vq * c - vi * s;
        fase += inc;
    }
    o->fase = fase;
}

uint32_t nco_lo_aparcado(uint32_t freq_hz, uint32_t lo_actual_hz,
                         uint32_t off_ini_hz, uint32_t off_min_hz,
                         uint32_t off_max_hz)
{
    int64_t off;

    /* Sin oscilador previo no hay nada que conservar. */
    if (lo_actual_hz == 0UL) {
        return freq_hz - off_ini_hz;
    }

    off = (int64_t)freq_hz - (int64_t)lo_actual_hz;

    /*
     * Se ha salido por ARRIBA: venia subiendo. Se reaparca dejando la
     * distancia en el MINIMO, no en la de siempre, para que quede toda la
     * ventana por delante en el sentido en que se estaba yendo. Reaparcar al
     * valor clasico seria dejarlo en medio y volver a saltar a la mitad de
     * camino: con Fs/4 = 24 kHz y un tope de 44, subir daria un salto cada
     * 20 kHz en vez de cada 38. Lo midio sim/ncotest.c - la primera version
     * hacia justo eso.
     */
    if (off > (int64_t)off_max_hz) {
        return freq_hz - off_min_hz;
    }

    /* Y por ABAJO al reves: venia bajando, se deja en el maximo. Esta rama
     * cubre tambien las distancias negativas, que no se admiten nunca. */
    if (off < (int64_t)off_min_hz) {
        return freq_hz - off_max_hz;
    }

    return lo_actual_hz;                      /* cabe: no se toca nada */
}

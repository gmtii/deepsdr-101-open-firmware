#include "disc_fm.h"
#include <string.h>

#define PI_F 3.14159265358979f

void disc_fm_init(disc_fm_t *d, float centro_hz, float fs_hz)
{
    if (!d || fs_hz <= 0.0f) { return; }
    memset(d, 0, sizeof *d);
    d->centro_hz = centro_hz;
    d->fs = fs_hz;
    nco_init();
    nco_fase_cero(&d->nco);
    nco_freq(&d->nco, centro_hz, fs_hz);
    d->nivel = 1e-6f;
}

float disc_fm_nivel(const disc_fm_t *d) { return d ? d->nivel : 0.0f; }

float disc_fm_paso(disc_fm_t *d, float x)
{
    float sn, cs, mi, mq, i1, q1, i2, q2, cruz, pot;

    /* 1. bajar el tono a banda base alrededor del centro */
    nco_paso(&d->nco, &sn, &cs);
    mi = x * cs;  mq = -x * sn;

    /*
     * 2. quitar la imagen del mezclador con DOS promediadores en cascada.
     *
     * El producto deja la señal util cerca de continua y una copia alrededor
     * del doble de la frecuencia central -de 3400 a 4200 Hz con el centro en
     * 1900-. Un promediador de doce muestras a 12 kHz tiene ceros cada 1000
     * Hz, que es justo donde cae esa copia, y en cascada la deja unos 40 dB
     * por debajo. Cuesta cuatro sumas y cuatro restas por muestra: mas barato
     * que cualquier filtro con coeficientes, y aqui no hace falta nada mejor.
     */
    d->s1i += mi - d->h1i[d->idx];  d->h1i[d->idx] = mi;
    d->s1q += mq - d->h1q[d->idx];  d->h1q[d->idx] = mq;
    i1 = d->s1i * (1.0f / (float)DISC_FM_LPF_N);
    q1 = d->s1q * (1.0f / (float)DISC_FM_LPF_N);
    d->s2i += i1 - d->h2i[d->idx];  d->h2i[d->idx] = i1;
    d->s2q += q1 - d->h2q[d->idx];  d->h2q[d->idx] = q1;
    i2 = d->s2i * (1.0f / (float)DISC_FM_LPF_N);
    q2 = d->s2q * (1.0f / (float)DISC_FM_LPF_N);
    d->idx++;
    if (d->idx >= DISC_FM_LPF_N) { d->idx = 0U; }

    /*
     * 3. el discriminador: cuanto ha girado la fase de una muestra a la otra
     * ES la frecuencia. Se hace con el producto cruzado y no con un arco
     * tangente porque el giro maximo aqui son 0,21 radianes -400 Hz a 12
     * kHz- y en ese tramo el seno y su angulo son lo mismo con un error de
     * menos del 1%. Un arco tangente por muestra costaria treinta veces mas
     * para no cambiar el resultado.
     *
     * OJO: eso vale mientras la desviacion sea pequeña frente a la tasa de
     * muestreo. El sincronismo del SSTV cae a 1200 Hz, o sea 700 por debajo
     * del centro, y ahi el error ya es del 3%: de sobra para reconocer un
     * pulso de sincronismo, que es lo unico que se hace con el.
     */
    cruz = d->i_ant * q2 - d->q_ant * i2;
    pot  = d->i_ant * d->i_ant + d->q_ant * d->q_ant;
    d->i_ant = i2; d->q_ant = q2;

    d->nivel += 0.0002f * (pot - d->nivel);
    if (d->nivel < 1e-12f) { d->nivel = 1e-12f; }

    if (pot < 1e-12f) { return d->centro_hz; }
    return d->centro_hz + (cruz / pot) * d->fs / (2.0f * PI_F);
}

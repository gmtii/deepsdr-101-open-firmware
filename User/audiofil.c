/*
 * Diseño del filtro de audio de dos cortes. Ver audiofil.h para el porque.
 *
 * Los biquads son los de la tabla de Robert Bristow-Johnson, con la
 * transformacion bilineal y precompensacion de frecuencia (prewarp), que
 * es lo que hace que el corte caiga donde se pide y no un poco por debajo
 * segun se sube hacia Nyquist.
 */
#include "audiofil.h"

#include <math.h>

#define PI_F 3.14159265358979f

/*
 * Las Q de un Butterworth de cuarto orden en dos etapas. No son numeros
 * elegidos: son cos de los angulos de sus polos, 1/(2*cos(pi/8)) y
 * 1/(2*cos(3*pi/8)). Con estas dos, y solo con estas dos, la banda de
 * paso sale plana.
 */
#define Q_LP_1 0.54119610f
#define Q_LP_2 1.30656296f

/* Y la de un Butterworth de segundo orden, que es una sola. */
#define Q_HP   0.70710678f

static void biquad(float *c, float f0, float fs, float q, uint8_t alto)
{
    float w0, cw, sw, alpha, a0, b0, b1, b2, a1, a2;

    w0    = 2.0f * PI_F * f0 / fs;
    cw    = cosf(w0);
    sw    = sinf(w0);
    alpha = sw / (2.0f * q);

    if (alto) {
        b0 =  (1.0f + cw) * 0.5f;
        b1 = -(1.0f + cw);
        b2 =  (1.0f + cw) * 0.5f;
    } else {
        b0 =  (1.0f - cw) * 0.5f;
        b1 =   1.0f - cw;
        b2 =  (1.0f - cw) * 0.5f;
    }
    a0 =  1.0f + alpha;
    a1 = -2.0f * cw;
    a2 =  1.0f - alpha;

    /* Convenio de CMSIS: a1 y a2 con el signo cambiado. */
    c[0] =  b0 / a0;
    c[1] =  b1 / a0;
    c[2] =  b2 / a0;
    c[3] = -a1 / a0;
    c[4] = -a2 / a0;
}

/* Una etapa que no hace nada: paso directo. Es lo que va en el hueco del
 * paso alto cuando el corte de abajo es 0 - mas barato y mas exacto que
 * un paso alto a 1 Hz, que ademas tendria el polo pegado al circulo. */
static void directo(float *c)
{
    c[0] = 1.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; c[4] = 0.0f;
}

void audiofil_disena(float lo_hz, float hi_hz, float fs_hz, float *coef,
                     float *lo_usado, float *hi_usado)
{
    float nyq;

    /*
     * LAS SALIDAS SE ESCRIBEN ANTES DE PODER IRSE - 06/10/2026.
     *
     * Este return dejaba *lo_usado y *hi_usado sin tocar, y el llamante
     * -fil_construye() en demod_am.c- los declara sin inicializar y GUARDA
     * lo que salga en s_fil_lo[fam]/s_fil_hi[fam], que es la configuracion
     * que luego se enseña y que entra como peticion del siguiente diseño.
     * O sea que la basura de la pila no se quedaba en la pila: se
     * persistia.
     *
     * Hoy no se dispara -coef nunca es nulo y fs_hz es 48000 o 96000- pero
     * un camino de salida que deja medio escrito su contrato es una trampa
     * puesta para el dia que alguien añada un llamante.
     */
    if (lo_usado != 0) { *lo_usado = lo_hz; }
    if (hi_usado != 0) { *hi_usado = hi_hz; }
    if (coef == 0 || fs_hz <= 0.0f) { return; }
    nyq = fs_hz * 0.5f;

    /*
     * Los recortes, en este orden a proposito: primero cada uno a SU
     * rango, y solo despues el de "que no se crucen". Al reves, un hi
     * disparatado empujaria a lo fuera de su rango antes de que nadie lo
     * hubiera recortado.
     */
    if (hi_hz > AUDIOFIL_HI_MAX) { hi_hz = AUDIOFIL_HI_MAX; }
    if (hi_hz < AUDIOFIL_HI_MIN) { hi_hz = AUDIOFIL_HI_MIN; }
    /* Y nunca tan arriba que el biquad se salga por Nyquist: a partir de
     * ahi el bilineal ya no representa nada. */
    if (hi_hz > nyq * 0.90f) { hi_hz = nyq * 0.90f; }

    if (lo_hz < AUDIOFIL_LO_MIN) { lo_hz = AUDIOFIL_LO_MIN; }
    if (lo_hz > AUDIOFIL_LO_MAX) { lo_hz = AUDIOFIL_LO_MAX; }
    if (lo_hz > hi_hz - AUDIOFIL_ANCHO_MIN) {
        lo_hz = hi_hz - AUDIOFIL_ANCHO_MIN;
        if (lo_hz < 0.0f) { lo_hz = 0.0f; }
    }

    biquad(&coef[0], hi_hz, fs_hz, Q_LP_1, 0U);
    biquad(&coef[5], hi_hz, fs_hz, Q_LP_2, 0U);

    if (lo_hz < 20.0f) { directo(&coef[10]); lo_hz = 0.0f; }
    else               { biquad(&coef[10], lo_hz, fs_hz, Q_HP, 1U); }

    if (lo_usado) { *lo_usado = lo_hz; }
    if (hi_usado) { *hi_usado = hi_hz; }
}

float audiofil_respuesta_db(const float *coef, float hz, float fs_hz)
{
    float w, c1, s1;
    /* Revision a fondo del 08/10/2026: audiofil_disena() si mira coef y
     * fs_hz antes de usarlos -"un camino de salida que deja medio escrito
     * su contrato es una trampa puesta", dice su comentario- y esta, que
     * divide por fs_hz en la primera linea, no miraba nada: con fs_hz a
     * cero salia inf o NaN y la curva del panel se pintaba con eso. Misma
     * guarda, y se devuelve el mismo "no se puede medir" que ya usa el
     * denominador degenerado de abajo. */
    if (coef == 0 || fs_hz <= 0.0f) { return -999.0f; }
    w = 2.0f * PI_F * hz / fs_hz;
    c1 = cosf(w);  s1 = sinf(w);
    float c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);
    float mag = 1.0f;
    uint8_t k;

    for (k = 0U; k < AUDIOFIL_ETAPAS; k++) {
        const float *c = &coef[k * 5U];
        /* Numerador: b0 + b1 z^-1 + b2 z^-2 */
        float nr = c[0] + c[1] * c1 + c[2] * c2;
        float ni =      -c[1] * s1 - c[2] * s2;
        /* Denominador: 1 - a1 z^-1 - a2 z^-2 (a1,a2 con el signo ya
         * cambiado, convenio CMSIS, de ahi los menos). */
        float dr = 1.0f - c[3] * c1 - c[4] * c2;
        float di =        c[3] * s1 + c[4] * s2;
        float n2 = nr * nr + ni * ni;
        float d2 = dr * dr + di * di;
        if (d2 <= 0.0f) { return -999.0f; }
        mag *= sqrtf(n2 / d2);
    }
    if (mag <= 1.0e-12f) { return -240.0f; }
    return 20.0f * log10f(mag);
}

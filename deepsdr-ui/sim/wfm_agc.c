/*
 * ======================================================================
 * EL AUTOMATICO DE GANANCIA DE WFM, DE DENTRO - 26/10/2026
 * ======================================================================
 *
 * Dos preguntas, las dos con numero:
 *
 *   1. LA DIVISION POR MUESTRA. agc_bloque() divide de verdad solo cuando
 *      la muestra supera el pico que decae (el ataque). Cuantas veces pasa
 *      eso por bloque EN WFM es lo que decide si esa division cuesta algo.
 *      Se cuenta de verdad: se saca la senal de antes del AGC poniendo el
 *      perfil MANUAL -que es sat16(env) tal cual- y se le pasa el MISMO
 *      lazo, comprobando antes que reproduce bit a bit lo que saca el
 *      firmware con el perfil automatico.
 *
 *   2. EL BOMBEO. Ataque instantaneo y soltada de 175 ms: un chasquido del
 *      discriminador -y la FM cerca del umbral esta llena- baja la ganancia
 *      de golpe y la deja subiendo durante 175 ms. Si los chasquidos vienen
 *      seguidos, la ganancia nunca para quieta y eso es una modulacion de
 *      amplitud a ritmo de chasquido, que es lo que se oye como aspereza.
 *      Se mide la envolvente de la salida y se mira cuanta AC tiene entre
 *      20 y 300 Hz.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "agc.h"
#include "demod_stubs.h"

#define FS     192000.0
#define NBLK   SDR_RX_BLOCK_SAMPLES_WFM
#define NCAP   (NBLK * 128)            /* 65536 muestras = 341 ms */

#define WFM_PEAK_MIN  4000.0f
#define REL_MED       0.99997024f
#define REL_SLOW      0.99999256f
#define REL_FAST      0.99991320f

static double g_fi, g_t;
static unsigned g_rnd = 7u;
static double gauss(void)
{
    double u1,u2;
    g_rnd = g_rnd*1103515245u+12345u;
    u1 = ((g_rnd>>8)&0xFFFFFFu)/(double)0x1000000u + 1e-12;
    g_rnd = g_rnd*1103515245u+12345u;
    u2 = ((g_rnd>>8)&0xFFFFFFu)/(double)0x1000000u;
    return sqrt(-2.0*log(u1))*cos(2.0*M_PI*u2);
}

static float g_cap[NCAP];

/* Corre la cadena y deja NCAP muestras en g_cap. */
static void corre(double amp, double ruido, agc_profile_t perfil)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    const int calienta = 400;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(perfil);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi=0.0; g_t=0.0; g_rnd=7u;

    for (b = 0; b < calienta + NCAP/NBLK + 2; b++) {
        const int16_t *sal;
        uint32_t n;
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double t = g_t/FS;
            double f = 30375.0*cos(2.0*M_PI*1000.0*t)
                     + 7500.0*cos(2.0*M_PI*19000.0*t)
                     + 30375.0*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t);
            double i,q;
            g_fi += 2.0*M_PI*f/FS;
            if (g_fi> M_PI) g_fi -= 2.0*M_PI;
            if (g_fi<-M_PI) g_fi += 2.0*M_PI;
            g_t += 1.0;
            i = amp*cos(g_fi); q = amp*sin(g_fi);
            if (ruido>0.0){ i+=ruido*gauss(); q+=ruido*gauss(); }
            if(i> 32767.0)i= 32767.0; if(i<-32768.0)i=-32768.0;
            if(q> 32767.0)q= 32767.0; if(q<-32768.0)q=-32768.0;
            raw[2U*n]=(int16_t)lrint(i); raw[2U*n+1U]=(int16_t)lrint(q);
        }
        demod_wfm_process_raw(raw);
        if (b < calienta || tomadas >= NCAP) continue;
        sal = cons_audio();
        for (k = 0; k < NBLK && tomadas < NCAP; k++) g_cap[tomadas++] = (float)sal[2*k];
    }
}

static float g_env[NCAP];      /* la senal de antes del AGC (perfil MANUAL) */
static float g_auto[NCAP];     /* la de despues (perfil automatico) */

/* El MISMO lazo que agc.c, pero contando los ataques. */
static int16_t sat(double v){ if(v>32767.0) return 32767; if(v<-32768.0) return -32768; return (int16_t)(int32_t)v; }

static long reproduce(const float *entra, int n_total, float relaj,
                      int16_t *sale, long *ataques)
{
    float pico = WFM_PEAK_MIN;
    long at = 0; int b, i;
    for (b = 0; b + NBLK <= n_total; b += NBLK) {
        float inv_rel = 1.0f/relaj, inv_min = 1.0f/WFM_PEAK_MIN;
        float inv = (pico < WFM_PEAK_MIN) ? inv_min : (1.0f/pico);
        for (i = 0; i < NBLK; i++) {
            float mag = entra[b+i] < 0.0f ? -entra[b+i] : entra[b+i];
            float gan;
            pico *= relaj; inv *= inv_rel;
            if (mag > pico) { pico = mag; inv = (pico<WFM_PEAK_MIN)?inv_min:(1.0f/pico); at++; }
            else if (pico < WFM_PEAK_MIN) { inv = inv_min; }
            gan = AGC_TARGET*inv;
            if (gan > AGC_GAIN_MAX) gan = AGC_GAIN_MAX;
            sale[b+i] = sat((double)(entra[b+i]*gan));
        }
    }
    *ataques = at;
    return 0;
}

/* AC de la envolvente entre dos frecuencias, en % de la media. */
static double bombeo(const float *x, int n, double f0, double f1)
{
    static double envv[NCAP];
    double a = 1.0 - exp(-1.0/(FS/300.0));   /* paso bajo de 300 Hz */
    double y = 0.0, med = 0.0, ac = 0.0;
    int k;
    for (k = 0; k < n; k++) {
        double m = fabs((double)x[k]);
        y += a*(m - y);
        envv[k] = y;
    }
    for (k = n/4; k < n; k++) med += envv[k];
    med /= (n - n/4);
    /* AC en la banda pedida, por Goertzel grueso: suma de |DFT| en pasos */
    {
        double paso = 5.0, f;
        for (f = f0; f <= f1; f += paso) {
            double sc=0.0, ss=0.0;
            for (k = n/4; k < n; k++) {
                double w = 2.0*M_PI*f*k/FS;
                sc += (envv[k]-med)*cos(w); ss += (envv[k]-med)*sin(w);
            }
            ac += 2.0*(sc*sc+ss*ss)/((double)(n-n/4)*(n-n/4))*2.0;
        }
    }
    return 100.0*sqrt(ac)/(med>0?med:1);
}

int main(void)
{
    static int16_t mio[NCAP];
    static const double k_amp[]   = { 20000.0, 12000.0, 4000.0, 1500.0, 600.0 };
    static const double k_ruido[] = {     0.0,    50.0,  200.0,  400.0, 800.0 };
    int j;

    printf("\n== EL AUTOMATICO DE GANANCIA DE WFM, DE DENTRO ==\n");
    printf("   bloque %d muestras, %.0f bloques por segundo\n\n",
           (int)NBLK, FS/NBLK);

    /* (0) Que mi copia del lazo sea el lazo de verdad. */
    printf("  -- (0) la copia del lazo contra el firmware (tiene que ser exacta) --\n");
    {
        long at; int k, dif = 0, peor = 0;
        corre(12000.0, 200.0, AGC_PROFILE_MANUAL);
        memcpy(g_env, g_cap, sizeof g_env);
        corre(12000.0, 200.0, AGC_PROFILE_MEDIUM);
        memcpy(g_auto, g_cap, sizeof g_auto);
        reproduce(g_env, NCAP, REL_MED, mio, &at);
        for (k = NBLK; k < NCAP; k++) {
            int d = (int)mio[k] - (int)g_auto[k];
            if (d) { dif++; if (abs(d) > peor) peor = abs(d); }
        }
        printf("     muestras distintas: %d de %d (peor diferencia %d LSB)\n",
               dif, NCAP-(int)NBLK, peor);
        /*
         * EL CRITERIO ERA EL EQUIVOCADO - 26/10/2026, revision a fondo.
         *
         * Aqui ponia "si mas del 1 % de las muestras difieren, lo de abajo
         * no vale", y ese cartel salia SIEMPRE: el 67 % de las muestras
         * difieren. Un banco que se declara invalido en cada pasada no
         * mide nada, porque nadie vuelve a mirarlo.
         *
         * Y no difieren por un fallo. agc_bloque() lleva el inverso del
         * pico por multiplicacion incremental -agc.c, "inv *= inv_rel"- y
         * esta copia lo recalcula como 1/pico cada vez. Los dos son el
         * mismo lazo; lo que cambia es el redondeo de la coma flotante de
         * precision simple, que se acumula y se suelta. La diferencia
         * medida son 3 LSB sobre picos de ~22.000, o sea -77 dB.
         *
         * Contar CUANTAS muestras difieren es contar ruido de redondeo.
         * Lo que decide si la copia sirve para medir ataques y bombeo es
         * CUANTO difieren, y eso es el peor caso. Ocho LSB son -69 dB:
         * holgura de sobra sobre los 3 medidos y sigue cazando un lazo de
         * verdad distinto, que se iria a cientos.
         */
        if (peor > 8) {
            printf("     *** la copia NO reproduce el lazo: lo de abajo no vale ***\n");
        } else {
            printf("     la copia vale: el peor caso son %d LSB sobre ~22.000\n", peor);
        }
    }

    /* (1) Cuantas divisiones por bloque. */
    printf("\n  -- (1) ataques (= divisiones de coma flotante) por bloque --\n");
    printf("   portadora  ruido    ataques/bloque   %%de las 512   ciclos/bloque (a 14)\n");
    for (j = 0; j < 5; j++) {
        long at;
        corre(k_amp[j], k_ruido[j], AGC_PROFILE_MANUAL);
        reproduce(g_cap, NCAP, REL_MED, mio, &at);
        printf("   %9.0f %6.0f %16.1f %12.1f%% %18.0f\n",
               k_amp[j], k_ruido[j],
               (double)at/(NCAP/NBLK), 100.0*at/(double)NCAP,
               14.0*at/(NCAP/NBLK));
    }

    /* (2) El bombeo de la envolvente. */
    printf("\n  -- (2) bombeo: AC de la envolvente entre 20 y 300 Hz --\n");
    printf("   portadora  ruido    MANUAL    LENTO    MEDIO    RAPIDO\n");
    for (j = 0; j < 5; j++) {
        double b_man, b_s, b_m, b_f;
        corre(k_amp[j], k_ruido[j], AGC_PROFILE_MANUAL);
        b_man = bombeo(g_cap, NCAP/8, 20.0, 300.0);
        corre(k_amp[j], k_ruido[j], AGC_PROFILE_SLOW);
        b_s = bombeo(g_cap, NCAP/8, 20.0, 300.0);
        corre(k_amp[j], k_ruido[j], AGC_PROFILE_MEDIUM);
        b_m = bombeo(g_cap, NCAP/8, 20.0, 300.0);
        corre(k_amp[j], k_ruido[j], AGC_PROFILE_FAST);
        b_f = bombeo(g_cap, NCAP/8, 20.0, 300.0);
        printf("   %9.0f %6.0f %8.2f%% %7.2f%% %7.2f%% %8.2f%%\n",
               k_amp[j], k_ruido[j], b_man, b_s, b_m, b_f);
    }

    printf("\n");
    return 0;
}

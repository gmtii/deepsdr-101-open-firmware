/*
 * ======================================================================
 * EL TOPE DEL QUITADOR DE CONTINUA DE WFM, MAS ALLA DE DONDE SE MIDIO
 * 26/10/2026
 * ======================================================================
 *
 * sim/wfm_dc.c mide el quitador de continua de la FM ancha con offsets de
 * 0, 120 y 300 cuentas. WFM_DC_TOPE vale 512, asi que esos tres caben
 * ENTEROS dentro del tope y el banco sale verde sin haber pisado nunca el
 * limite. Lo que no esta medido es que pasa por encima.
 *
 * Y la pregunta no es academica, por tres cosas que estan en el arbol:
 *
 *   1. en WFM apply_demod_mode() clava el PGA en el tope, 47,5 dB
 *      (main.c, "pga: WFM, al tope por perdida de VHF"), y un PGA
 *      amplifica la fuga de continua igual que amplifica la senal;
 *   2. la cadena de AM / banda lateral / NFM NO lleva tope: su quitador
 *      (s_rf_dc_i, RF_DC_ALFA_N = 64) es un lazo cerrado que converge a
 *      la fuga ENTERA, la que sea. O sea que el mismo hardware se corrige
 *      del todo en AM y a medias en WFM;
 *   3. lo que queda sin corregir no sale como una continua quitable al
 *      otro lado del discriminador: TUERCE el angulo. Eso ya esta medido
 *      en demod_am.c, pero solo en el caso extremo D >= A.
 *
 * Esto barre el offset desde dentro del tope hasta muy por encima y mide
 * lo que sale por el audio de verdad.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS     192000.0
#define NBLK   SDR_RX_BLOCK_SAMPLES_WFM
#define NFFT   16384

static double re_[NFFT], im_[NFFT];
static void fft(void)
{
    int i, j, len;
    for (i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) { double t;
            t=re_[i];re_[i]=re_[j];re_[j]=t; t=im_[i];im_[i]=im_[j];im_[j]=t; }
    }
    for (len = 2; len <= NFFT; len <<= 1) {
        double ang=-2.0*M_PI/len, wr=cos(ang), wi=sin(ang);
        for (i = 0; i < NFFT; i += len) {
            double cr=1.0, ci=0.0;
            for (j = 0; j < len/2; j++) {
                double ur=re_[i+j], ui=im_[i+j];
                double vr=re_[i+j+len/2]*cr-im_[i+j+len/2]*ci;
                double vi=re_[i+j+len/2]*ci+im_[i+j+len/2]*cr;
                double nr;
                re_[i+j]=ur+vr; im_[i+j]=ui+vi;
                re_[i+j+len/2]=ur-vr; im_[i+j+len/2]=ui-vi;
                nr=cr*wr-ci*wi; ci=cr*wi+ci*wr; cr=nr;
            }
        }
    }
}

static double g_fi, g_t;
static float  g_audio[NFFT];
static double g_mag[NFFT/2+1];

static void corre(double amp, double dc, double desv, double piloto,
                  double estereo, double faudio)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    const int calienta = 700;   /* > 512 bloques: el estimador ya converge lento */

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < calienta + NFFT/NBLK + 2; b++) {
        const int16_t *sal;
        uint32_t n;
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double t = g_t / FS;
            double f = desv*cos(2.0*M_PI*faudio*t)
                     + piloto*cos(2.0*M_PI*19000.0*t)
                     + estereo*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t);
            double i, q;
            g_fi += 2.0*M_PI*f/FS;
            if (g_fi >  M_PI) { g_fi -= 2.0*M_PI; }
            if (g_fi < -M_PI) { g_fi += 2.0*M_PI; }
            g_t += 1.0;
            i = amp*cos(g_fi) + dc;
            q = amp*sin(g_fi);
            if (i >  32767.0) { i =  32767.0; }
            if (i < -32768.0) { i = -32768.0; }
            if (q >  32767.0) { q =  32767.0; }
            if (q < -32768.0) { q = -32768.0; }
            raw[2U*n] = (int16_t)lrint(i);
            raw[2U*n+1U] = (int16_t)lrint(q);
        }
        demod_wfm_process_raw(raw);
        if (b < calienta || tomadas >= NFFT) { continue; }
        sal = cons_audio();
        for (k = 0; k < NBLK && tomadas < NFFT; k++) {
            g_audio[tomadas++] = (float)sal[2*k];
        }
    }
    for (k = 0; k < NFFT; k++) {
        double w = 0.5 - 0.5*cos(2.0*M_PI*k/NFFT);
        re_[k] = g_audio[k]*w; im_[k] = 0.0;
    }
    fft();
    for (k = 0; k <= NFFT/2; k++) {
        g_mag[k] = sqrt(re_[k]*re_[k]+im_[k]*im_[k])*2.0/(NFFT*0.5);
    }
}

static double pico_en(double hz)
{
    int c=(int)lrint(hz*NFFT/FS), k; double s=0.0;
    for (k=c-2;k<=c+2;k++) if(k>=0&&k<=NFFT/2) s+=g_mag[k]*g_mag[k];
    return sqrt(s);
}

/* La basura audible: todo lo que hay entre 100 Hz y 8 kHz menos el tono. */
static void mide(double faudio, double *tono, double *basura_aud, double *thd)
{
    int k, a; double e_aud=0.0, e_arm=0.0;
    *tono = pico_en(faudio);
    for (k=(int)(100.0*NFFT/FS); k<=(int)(8000.0*NFFT/FS); k++) e_aud += g_mag[k]*g_mag[k];
    for (a=2;a<=8;a++){ double v=pico_en(a*faudio); e_arm+=v*v; }
    *basura_aud = 100.0*(e_aud - (*tono)*(*tono))/(e_aud>0?e_aud:1);
    *thd = 100.0*sqrt(e_arm)/((*tono)>0?*tono:1);
}

int main(void)
{
    static const double k_dc[] = { 0.0, 256.0, 512.0, 1000.0, 2000.0,
                                   3000.0, 5000.0, 8000.0, 12000.0 };
    int j;

    printf("\n== EL TOPE DEL QUITADOR DE CONTINUA DE WFM (WFM_DC_TOPE) ==\n\n");

    printf("  -- emisora fuerte: portadora 12000 cuentas, composite entero --\n");
    printf("   offset   lo que el firmware dice restar   residuo   tono    THD    basura audible\n");
    for (j = 0; j < 9; j++) {
        float di=0.0f, dq=0.0f; double tono, bas, thd;
        corre(12000.0, k_dc[j], 0.45*0.9*75000.0, 0.1*75000.0, 0.45*0.9*75000.0, 1000.0);
        demod_wfm_get_dc(&di, &dq);
        mide(1000.0, &tono, &bas, &thd);
        printf("   %6.0f %26.1f %10.0f %7.0f %6.2f%% %12.2f%%\n",
               k_dc[j], (double)di, k_dc[j]-(double)di, tono, thd, bas);
    }

    printf("\n  -- la misma, con la portadora a 4000 cuentas (emisora mediana) --\n");
    printf("   offset   dice restar   residuo   residuo/portadora   tono    THD    basura audible\n");
    for (j = 0; j < 9; j++) {
        float di=0.0f, dq=0.0f; double tono, bas, thd;
        corre(4000.0, k_dc[j], 0.45*0.9*75000.0, 0.1*75000.0, 0.45*0.9*75000.0, 1000.0);
        demod_wfm_get_dc(&di, &dq);
        mide(1000.0, &tono, &bas, &thd);
        printf("   %6.0f %13.1f %9.0f %17.0f%% %7.0f %6.2f%% %12.2f%%\n",
               k_dc[j], (double)di, k_dc[j]-(double)di,
               100.0*(k_dc[j]-(double)di)/4000.0, tono, thd, bas);
    }

    printf("\n  -- y cuanto aguanta un offset de 3000 segun la portadora --\n");
    printf("   portadora   residuo/portadora    tono    THD    basura audible\n");
    {
        static const double k_amp[] = { 30000.0, 20000.0, 12000.0, 8000.0,
                                        5000.0, 3000.0, 2000.0 };
        for (j = 0; j < 7; j++) {
            float di=0.0f, dq=0.0f; double tono, bas, thd;
            corre(k_amp[j], 3000.0, 0.45*0.9*75000.0, 0.1*75000.0, 0.45*0.9*75000.0, 1000.0);
            demod_wfm_get_dc(&di, &dq);
            mide(1000.0, &tono, &bas, &thd);
            printf("   %9.0f %18.0f%% %7.0f %6.2f%% %12.2f%%\n",
                   k_amp[j], 100.0*(3000.0-(double)di)/k_amp[j], tono, thd, bas);
        }
    }

    printf("\n");
    return 0;
}

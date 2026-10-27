/*
 * ======================================================================
 * LO QUE EL CONVERTIDOR DEJA ENTRAR, EN FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * El banco sim/wfm_zumbido.c mete en demod_wfm_process_raw() un I/Q
 * generado DIRECTAMENTE a 192 kHz, o sea exp(j.fi[n]) muestreado. Y con eso
 * la cadena sale limpisima -THD 0,18 %- porque muestrear la fase es exacto
 * mientras el salto por muestra se quede dentro de +-pi: las bandas
 * laterales que se salen de +-96 kHz se doblan COHERENTEMENTE y vuelven a
 * sumar la misma senal. No se pierde nada.
 *
 * EL CONVERTIDOR DE VERDAD NO HACE ESO. El AIC3204 lleva su propio
 * diezmador con un filtro que TIRA lo que pasa de fs/2 en vez de doblarlo.
 * O sea que de la emisora llega solo el trozo que cabe en la ventana, y lo
 * que falta no vuelve: eso ya no es un doblado inofensivo, es una senal
 * MUTILADA, y un discriminador de FM alimentado con una envolvente
 * compleja recortada en banda saca distorsion de verdad.
 *
 * ESTE BANCO LO MODELA: genera la emisora a 1,536 MHz -ocho veces-, le
 * pasa un paso bajo complejo con el codo donde se quiera, diezma a 192 kHz
 * y ESO es lo que entra por demod_wfm_process_raw(). Asi se puede medir lo
 * que cuesta cada kilohercio de ventana.
 *
 * Y la cuenta de Carson dice de antemano lo que tiene que salir:
 *
 *   mono solo (audio hasta 15 kHz)      2*(75+15) = 180 kHz  <- cabe
 *   + piloto de 19                      2*(75+19) = 188 kHz  <- justo
 *   + subportadora de estereo en 38     2*(75+38) = 226 kHz  <- NO cabe
 *   + RDS en 57                         2*(75+57) = 264 kHz  <- NO cabe
 *
 * La ventana son 192 kHz. Toda emisora comercial lleva piloto y casi todas
 * llevan RDS.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS      192000.0
#define OS      8                      /* sobremuestreo del generador */
#define FSO     (FS * OS)              /* 1,536 MHz */
#define NBLK    SDR_RX_BLOCK_SAMPLES_WFM
#define NFFT    16384
#define NTAPS   513                    /* el paso bajo complejo del modelo */

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
        double ang = -2.0*M_PI/len, wr = cos(ang), wi = sin(ang);
        for (i = 0; i < NFFT; i += len) {
            double cr = 1.0, ci = 0.0;
            for (j = 0; j < len/2; j++) {
                double ur=re_[i+j], ui=im_[i+j];
                double vr=re_[i+j+len/2]*cr - im_[i+j+len/2]*ci;
                double vi=re_[i+j+len/2]*ci + im_[i+j+len/2]*cr;
                double nr;
                re_[i+j]=ur+vr; im_[i+j]=ui+vi;
                re_[i+j+len/2]=ur-vr; im_[i+j+len/2]=ui-vi;
                nr=cr*wr-ci*wi; ci=cr*wi+ci*wr; cr=nr;
            }
        }
    }
}

/* El paso bajo del modelo del convertidor: ventana de Blackman, corte fc. */
static double h[NTAPS];
static void disena(double fc)
{
    int k; double s = 0.0;
    for (k = 0; k < NTAPS; k++) {
        double m = k - (NTAPS - 1) / 2.0;
        double x = 2.0 * fc / FSO;
        double v = (m == 0.0) ? x : (sin(M_PI * x * m) / (M_PI * m));
        double w = 0.42 - 0.5*cos(2.0*M_PI*k/(NTAPS-1)) + 0.08*cos(4.0*M_PI*k/(NTAPS-1));
        h[k] = v * w; s += h[k];
    }
    for (k = 0; k < NTAPS; k++) { h[k] /= s; }
}

/* Cola circular del filtro, para que no haya costuras entre bloques. */
static double hi_[NTAPS], hq_[NTAPS];
static int    hidx;
static double g_fi, g_t;

typedef struct { double amp, desv, faudio, piloto, estereo, rds; } emis_t;

static void bloque(int16_t *raw, const emis_t *e)
{
    uint32_t n; int o, k;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double ai = 0.0, aq = 0.0;
        for (o = 0; o < OS; o++) {
            double t = g_t / FSO;
            double f = e->desv * cos(2.0*M_PI*e->faudio*t)
                     + e->piloto * cos(2.0*M_PI*19000.0*t)
                     + e->estereo * cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t)
                     + e->rds * cos(2.0*M_PI*57000.0*t);
            g_fi += 2.0*M_PI*f/FSO;
            if (g_fi >  M_PI) { g_fi -= 2.0*M_PI; }
            if (g_fi < -M_PI) { g_fi += 2.0*M_PI; }
            g_t += 1.0;
            hi_[hidx] = e->amp*cos(g_fi);
            hq_[hidx] = e->amp*sin(g_fi);
            hidx = (hidx + 1) % NTAPS;
        }
        /* una salida de cada OS: el diezmado, con el filtro ya aplicado */
        ai = aq = 0.0;
        for (k = 0; k < NTAPS; k++) {
            int p = (hidx + k) % NTAPS;
            ai += h[NTAPS-1-k] * hi_[p];
            aq += h[NTAPS-1-k] * hq_[p];
        }
        if (ai >  32767.0) { ai =  32767.0; }
        if (ai < -32768.0) { ai = -32768.0; }
        if (aq >  32767.0) { aq =  32767.0; }
        if (aq < -32768.0) { aq = -32768.0; }
        raw[2U*n] = (int16_t)lrint(ai);
        raw[2U*n+1U] = (int16_t)lrint(aq);
    }
}

static float g_audio[NFFT];
static double g_mag[NFFT/2+1];

static void corre(const emis_t *e, double fc)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    const int calienta = 250;

    disena(fc);
    memset(hi_, 0, sizeof hi_); memset(hq_, 0, sizeof hq_); hidx = 0;
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < calienta + NFFT/NBLK + 2; b++) {
        const int16_t *sal;
        bloque(raw, e);
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
        g_mag[k] = sqrt(re_[k]*re_[k]+im_[k]*im_[k]) * 2.0/(NFFT*0.5);
    }
}

static double pico_en(double hz)
{
    int c = (int)lrint(hz*NFFT/FS), k; double s = 0.0;
    for (k = c-2; k <= c+2; k++) { if (k>=0 && k<=NFFT/2) s += g_mag[k]*g_mag[k]; }
    return sqrt(s);
}

static void informe(const char *rot, double faudio)
{
    double tono = pico_en(faudio), e_tot = 0.0, e_arm = 0.0;
    double e_aud = 0.0;   /* lo que cae en la banda audible, 100 Hz - 8 kHz */
    int k, a;
    for (k = 1; k <= NFFT/2; k++) { e_tot += g_mag[k]*g_mag[k]; }
    for (a = 2; a <= 20; a++) {
        double v = pico_en(a*faudio); e_arm += v*v;
    }
    for (k = (int)(100.0*NFFT/FS); k <= (int)(8000.0*NFFT/FS); k++) {
        e_aud += g_mag[k]*g_mag[k];
    }
    printf("   %-30s tono %7.0f   THD %6.2f%%   basura en 0,1-8 kHz %6.2f%%   total no-tono %6.2f%%\n",
           rot, tono, 100.0*sqrt(e_arm)/(tono>0?tono:1),
           100.0*(e_aud - tono*tono)/(e_aud>0?e_aud:1),
           100.0*(e_tot - tono*tono)/(e_tot>0?e_tot:1));
}

int main(void)
{
    emis_t e;
    static const double k_fc[] = { 400000.0, 130000.0, 96000.0, 86000.0, 80000.0 };
    int j;

    printf("\n== LO QUE EL CONVERTIDOR DEJA ENTRAR, EN FM ANCHA ==\n");
    printf("   La emisora se genera a %.0f kHz, se le pasa un paso bajo complejo\n"
           "   con el codo que dice cada fila, se diezma a 192 kHz y ESO entra\n"
           "   por demod_wfm_process_raw(). fc=400k es \"sin recortar\".\n\n", FSO/1000.0);

    /* (1) MONO PURO: por Carson 2*(75+15) = 180 kHz, cabe en la ventana. */
    printf("  -- (1) mono puro, audio 1 kHz, desviacion +-75 kHz --\n");
    memset(&e,0,sizeof e);
    e.amp=12000.0; e.faudio=1000.0; e.desv=75000.0;
    for (j = 0; j < 5; j++) {
        char r[64]; sprintf(r, "ventana +-%.0f kHz", k_fc[j]/1000.0);
        corre(&e, k_fc[j]); informe(r, e.faudio);
    }

    /* (2) LA EMISORA DE VERDAD: piloto 19k + estereo 38k + RDS 57k. */
    printf("\n  -- (2) emisora comercial entera: mono + piloto 19k + estereo 38k + RDS 57k --\n");
    memset(&e,0,sizeof e);
    e.amp=12000.0; e.faudio=1000.0;
    e.desv=0.45*0.9*75000.0; e.piloto=0.1*75000.0;
    e.estereo=0.45*0.9*75000.0; e.rds=0.04*75000.0;
    for (j = 0; j < 5; j++) {
        char r[64]; sprintf(r, "ventana +-%.0f kHz", k_fc[j]/1000.0);
        corre(&e, k_fc[j]); informe(r, e.faudio);
    }

    /* (3) Solo mono + piloto, sin estereo ni RDS: el escalon de Carson. */
    printf("\n  -- (3) mono + piloto, sin estereo ni RDS --\n");
    memset(&e,0,sizeof e);
    e.amp=12000.0; e.faudio=1000.0;
    e.desv=0.9*75000.0; e.piloto=0.1*75000.0;
    for (j = 0; j < 5; j++) {
        char r[64]; sprintf(r, "ventana +-%.0f kHz", k_fc[j]/1000.0);
        corre(&e, k_fc[j]); informe(r, e.faudio);
    }

    printf("\n");
    return 0;
}

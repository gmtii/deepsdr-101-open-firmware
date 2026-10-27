/*
 * ======================================================================
 * EL ESPEJO DE I/Q, EN FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * En AM, banda lateral y NFM la emisora NO esta en continua: la cadena baja
 * en frecuencia con el NCO desde un desplazamiento (DEMOD_IF_OFFSET_HZ), asi
 * que el espejo que produce el desequilibrio entre I y Q cae AL OTRO LADO
 * del centro y el filtro de canal se lo come.
 *
 * EN FM ANCHA NO HAY DESPLAZAMIENTO: "la portadora vive en continua", dice
 * la nota de demod_am.h, y el camino de WFM se salta el mezclador entero. O
 * sea que el espejo de la emisora cae ENCIMA DE LA EMISORA. No hay filtro
 * que lo quite, porque esta en la misma banda, y no es ruido: es la MISMA
 * senal con la frecuencia invertida, sumada a si misma.
 *
 * Para una senal de FM eso es una modulacion de amplitud Y de fase que el
 * discriminador saca como distorsion de verdad, no como siseo.
 *
 * Esto lo mide: se le mete a demod_wfm_process_raw() una emisora con un
 * desequilibrio de ganancia y de fase conocido y se mira lo que sale.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS    192000.0
#define NBLK  SDR_RX_BLOCK_SAMPLES_WFM
#define NFFT  16384

static double re_[NFFT], im_[NFFT];
static void fft(void)
{
    int i,j,len;
    for (i=1,j=0;i<NFFT;i++){ int bit=NFFT>>1;
        for(;j&bit;bit>>=1) j^=bit; j^=bit;
        if(i<j){double t;t=re_[i];re_[i]=re_[j];re_[j]=t;t=im_[i];im_[i]=im_[j];im_[j]=t;} }
    for(len=2;len<=NFFT;len<<=1){ double ang=-2.0*M_PI/len,wr=cos(ang),wi=sin(ang);
        for(i=0;i<NFFT;i+=len){ double cr=1.0,ci=0.0;
            for(j=0;j<len/2;j++){ double ur=re_[i+j],ui=im_[i+j];
                double vr=re_[i+j+len/2]*cr-im_[i+j+len/2]*ci;
                double vi=re_[i+j+len/2]*ci+im_[i+j+len/2]*cr; double nr;
                re_[i+j]=ur+vr; im_[i+j]=ui+vi;
                re_[i+j+len/2]=ur-vr; im_[i+j+len/2]=ui-vi;
                nr=cr*wr-ci*wi; ci=cr*wi+ci*wr; cr=nr; } } }
}

static double g_fi, g_t;
static float  g_audio[NFFT];
static double g_mag[NFFT/2+1];

/* desq_db: desequilibrio de ganancia entre I y Q, en dB.
 * desq_gr: error de cuadratura, en grados. */
static void corre(double amp, double desq_db, double desq_gr,
                  double desv, double piloto, double estereo, double faudio)
{
    static int16_t raw[2*NBLK];
    int b,k,tomadas=0; const int calienta=300;
    double gi = pow(10.0, desq_db/40.0), gq = 1.0/gi;
    double fase = desq_gr*M_PI/180.0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi=0.0; g_t=0.0;

    for (b=0;b<calienta+NFFT/NBLK+2;b++){
        const int16_t *sal; uint32_t n;
        for(n=0;n<(uint32_t)NBLK;n++){
            double t=g_t/FS;
            double f = desv*cos(2.0*M_PI*faudio*t)
                     + piloto*cos(2.0*M_PI*19000.0*t)
                     + estereo*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t);
            double i,q;
            g_fi += 2.0*M_PI*f/FS;
            if(g_fi> M_PI) g_fi-=2.0*M_PI;
            if(g_fi<-M_PI) g_fi+=2.0*M_PI;
            g_t += 1.0;
            /* El desequilibrio clasico: ganancia distinta y cuadratura
             * imperfecta, que es lo que produce el espejo. */
            i = amp*gi*cos(g_fi);
            q = amp*gq*sin(g_fi + fase);
            if(i> 32767.0)i= 32767.0;
            if(i<-32768.0)i=-32768.0;
            if(q> 32767.0)q= 32767.0;
            if(q<-32768.0)q=-32768.0;
            raw[2U*n]=(int16_t)lrint(i); raw[2U*n+1U]=(int16_t)lrint(q);
        }
        demod_wfm_process_raw(raw);
        if(b<calienta||tomadas>=NFFT) continue;
        sal=cons_audio();
        for(k=0;k<NBLK&&tomadas<NFFT;k++) g_audio[tomadas++]=(float)sal[2*k];
    }
    for(k=0;k<NFFT;k++){ double w=0.5-0.5*cos(2.0*M_PI*k/NFFT);
        re_[k]=g_audio[k]*w; im_[k]=0.0; }
    fft();
    for(k=0;k<=NFFT/2;k++) g_mag[k]=sqrt(re_[k]*re_[k]+im_[k]*im_[k])*2.0/(NFFT*0.5);
}

static double pico_en(double hz)
{ int c=(int)lrint(hz*NFFT/FS),k; double s=0.0;
  for(k=c-2;k<=c+2;k++) if(k>=0&&k<=NFFT/2) s+=g_mag[k]*g_mag[k];
  return sqrt(s); }

static void informe(const char *rot, double faudio)
{
    double tono=pico_en(faudio), e_aud=0.0, e_arm=0.0; int k,a;
    for(k=(int)(100.0*NFFT/FS);k<=(int)(8000.0*NFFT/FS);k++) e_aud+=g_mag[k]*g_mag[k];
    for(a=2;a<=8;a++){ double v=pico_en(a*faudio); e_arm+=v*v; }
    printf("   %-28s tono %7.0f   THD %7.2f%%   basura 0,1-8 kHz %7.2f%%\n",
           rot, tono, 100.0*sqrt(e_arm)/(tono>0?tono:1),
           100.0*(e_aud-tono*tono)/(e_aud>0?e_aud:1));
}

int main(void)
{
    static const double k_db[] = { 0.0, 0.1, 0.3, 1.0, 2.0, 3.0 };
    static const double k_gr[] = { 0.0, 0.5, 1.0, 3.0, 5.0, 10.0 };
    int j;

    printf("\n== EL ESPEJO DE I/Q EN FM ANCHA (la portadora vive en continua) ==\n\n");

    printf("  -- (1) desequilibrio de GANANCIA entre I y Q, emisora entera --\n");
    for (j=0;j<6;j++){
        char r[64]; sprintf(r,"%.1f dB de desequilibrio",k_db[j]);
        corre(12000.0, k_db[j], 0.0, 0.45*0.9*75000.0, 0.1*75000.0,
              0.45*0.9*75000.0, 1000.0);
        informe(r, 1000.0);
    }

    printf("\n  -- (2) error de CUADRATURA (fase), emisora entera --\n");
    for (j=0;j<6;j++){
        char r[64]; sprintf(r,"%.1f grados de cuadratura",k_gr[j]);
        corre(12000.0, 0.0, k_gr[j], 0.45*0.9*75000.0, 0.1*75000.0,
              0.45*0.9*75000.0, 1000.0);
        informe(r, 1000.0);
    }

    printf("\n  -- (3) los dos a la vez, que es lo que hay en una radio --\n");
    {
        static const double d[][2] = {{0.0,0.0},{0.2,0.5},{0.5,1.0},
                                      {1.0,2.0},{2.0,5.0}};
        for (j=0;j<5;j++){
            char r[64]; sprintf(r,"%.1f dB + %.1f grados",d[j][0],d[j][1]);
            corre(12000.0, d[j][0], d[j][1], 0.45*0.9*75000.0, 0.1*75000.0,
                  0.45*0.9*75000.0, 1000.0);
            informe(r, 1000.0);
        }
    }

    printf("\n  -- (4) y mono puro, para ver si depende del ancho ocupado --\n");
    {
        static const double d[][2] = {{0.0,0.0},{0.5,1.0},{2.0,5.0}};
        for (j=0;j<3;j++){
            char r[64]; sprintf(r,"mono +-75k: %.1f dB + %.1f gr",d[j][0],d[j][1]);
            corre(12000.0, d[j][0], d[j][1], 75000.0, 0.0, 0.0, 1000.0);
            informe(r, 1000.0);
        }
        for (j=0;j<3;j++){
            char r[64]; sprintf(r,"mono +-10k: %.1f dB + %.1f gr",d[j][0],d[j][1]);
            corre(12000.0, d[j][0], d[j][1], 10000.0, 0.0, 0.0, 1000.0);
            informe(r, 1000.0);
        }
    }
    printf("\n");
    return 0;
}

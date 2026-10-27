/*
 * ======================================================================
 * LA EMISORA DE AL LADO, Y EL MARGEN DEL DISCRIMINADOR - 26/10/2026
 * ======================================================================
 *
 * Dos cosas que solo le pasan a la FM ancha, y las dos se miden aqui metiendo
 * muestras crudas por demod_wfm_process_raw().
 *
 * (A) EL MARGEN DE VUELTA DEL ATAN2. El discriminador saca el salto de fase
 *     por muestra y atan2 solo distingue +-pi. A 192 kHz eso son +-96 kHz de
 *     desviacion INSTANTANEA. La emisora da +-75 de pico, asi que el margen
 *     que queda para TODO lo demas -error de sintonia, deriva del oscilador,
 *     sobremodulacion- son 21 kHz. Pasado eso el angulo da la vuelta, y una
 *     vuelta no es un poco de distorsion: es un pinchazo de fondo de escala.
 *
 * (B) LA EMISORA DE AL LADO. La cadena de WFM NO lleva filtro de canal por
 *     omision: WFM_IFBW_WIDE se salta el filtro ENTERO (demod_am.c, paso 0b).
 *     En Europa la rejilla de FM es de 100 kHz, o sea que la vecina cae
 *     justo en el borde de la ventana de +-96 kHz -o se dobla dentro-. En
 *     AM, banda lateral y NFM esto no pasa: ahi SI hay filtro de canal
 *     (CHF_COEFFS, 4 kHz; NFM_CHF, 6,25) y la vecina no llega al
 *     discriminador.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS    192000.0
#define OS    8
#define FSO   (FS*OS)
#define NBLK  SDR_RX_BLOCK_SAMPLES_WFM
#define NFFT  16384
#define NTAPS 513

static double re_[NFFT], im_[NFFT];
static void fft(void)
{
    int i,j,len;
    for(i=1,j=0;i<NFFT;i++){int bit=NFFT>>1;
        for(;j&bit;bit>>=1) j^=bit; j^=bit;
        if(i<j){double t;t=re_[i];re_[i]=re_[j];re_[j]=t;t=im_[i];im_[i]=im_[j];im_[j]=t;}}
    for(len=2;len<=NFFT;len<<=1){double ang=-2.0*M_PI/len,wr=cos(ang),wi=sin(ang);
        for(i=0;i<NFFT;i+=len){double cr=1.0,ci=0.0;
            for(j=0;j<len/2;j++){double ur=re_[i+j],ui=im_[i+j];
                double vr=re_[i+j+len/2]*cr-im_[i+j+len/2]*ci;
                double vi=re_[i+j+len/2]*ci+im_[i+j+len/2]*cr;double nr;
                re_[i+j]=ur+vr;im_[i+j]=ui+vi;
                re_[i+j+len/2]=ur-vr;im_[i+j+len/2]=ui-vi;
                nr=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=nr;}}}
}

static double h[NTAPS];
static void disena(double fc)
{
    int k; double s=0.0;
    for(k=0;k<NTAPS;k++){
        double m=k-(NTAPS-1)/2.0, x=2.0*fc/FSO;
        double v=(m==0.0)?x:(sin(M_PI*x*m)/(M_PI*m));
        double w=0.42-0.5*cos(2.0*M_PI*k/(NTAPS-1))+0.08*cos(4.0*M_PI*k/(NTAPS-1));
        h[k]=v*w; s+=h[k];
    }
    for(k=0;k<NTAPS;k++) h[k]/=s;
}

static double hi_[NTAPS], hq_[NTAPS];
static int hidx;
static double fi1, fi2, tt;
static float g_audio[NFFT];
static double g_mag[NFFT/2+1];

/* off2/amp2: la vecina. df: error de sintonia de la nuestra. */
static void corre(double amp, double desv, double piloto, double estereo,
                  double faudio, double df,
                  double amp2, double off2, wfm_ifbw_t ifbw, int modela_adc)
{
    static int16_t raw[2*NBLK];
    int b,k,tomadas=0; const int calienta=260;

    disena(96000.0);
    memset(hi_,0,sizeof hi_); memset(hq_,0,sizeof hq_); hidx=0;
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(ifbw);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    fi1=0.0; fi2=0.0; tt=0.0;

    for(b=0;b<calienta+NFFT/NBLK+2;b++){
        const int16_t *sal; uint32_t n; int o,j;
        for(n=0;n<(uint32_t)NBLK;n++){
            double ai,aq;
            for(o=0;o<(modela_adc?OS:1);o++){
                double paso = modela_adc ? FSO : FS;
                double t = tt/paso;
                double f1 = desv*cos(2.0*M_PI*faudio*t)
                          + piloto*cos(2.0*M_PI*19000.0*t)
                          + estereo*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t)
                          + df;
                /* la vecina lleva su propio programa */
                double f2 = 60000.0*cos(2.0*M_PI*430.0*t)
                          + 7500.0*cos(2.0*M_PI*19000.0*t) + off2;
                fi1 += 2.0*M_PI*f1/paso; fi2 += 2.0*M_PI*f2/paso;
                if(fi1> M_PI) fi1-=2.0*M_PI; if(fi1<-M_PI) fi1+=2.0*M_PI;
                if(fi2> M_PI) fi2-=2.0*M_PI; if(fi2<-M_PI) fi2+=2.0*M_PI;
                tt += 1.0;
                hi_[hidx]=amp*cos(fi1)+amp2*cos(fi2);
                hq_[hidx]=amp*sin(fi1)+amp2*sin(fi2);
                hidx=(hidx+1)%NTAPS;
            }
            if(modela_adc){
                ai=aq=0.0;
                for(j=0;j<NTAPS;j++){int p=(hidx+j)%NTAPS;
                    ai+=h[NTAPS-1-j]*hi_[p]; aq+=h[NTAPS-1-j]*hq_[p];}
            } else {
                int p=(hidx-1+NTAPS)%NTAPS; ai=hi_[p]; aq=hq_[p];
            }
            if(ai> 32767.0)ai= 32767.0; if(ai<-32768.0)ai=-32768.0;
            if(aq> 32767.0)aq= 32767.0; if(aq<-32768.0)aq=-32768.0;
            raw[2U*n]=(int16_t)lrint(ai); raw[2U*n+1U]=(int16_t)lrint(aq);
        }
        demod_wfm_process_raw(raw);
        if(b<calienta||tomadas>=NFFT) continue;
        sal=cons_audio();
        for(k=0;k<NBLK&&tomadas<NFFT;k++) g_audio[tomadas++]=(float)sal[2*k];
    }
    for(k=0;k<NFFT;k++){double w=0.5-0.5*cos(2.0*M_PI*k/NFFT);
        re_[k]=g_audio[k]*w; im_[k]=0.0;}
    fft();
    for(k=0;k<=NFFT/2;k++) g_mag[k]=sqrt(re_[k]*re_[k]+im_[k]*im_[k])*2.0/(NFFT*0.5);
}

static double pico_en(double hz)
{ int c=(int)lrint(hz*NFFT/FS),k; double s=0.0;
  for(k=c-2;k<=c+2;k++) if(k>=0&&k<=NFFT/2) s+=g_mag[k]*g_mag[k];
  return sqrt(s); }

static void informe(const char *rot, double faudio)
{
    double tono=pico_en(faudio), e_aud=0.0, e_tot=0.0, comb=0.0, mx=0.0, mxhz=0.0;
    int k,hh;
    for(k=(int)(100.0*NFFT/FS);k<=(int)(8000.0*NFFT/FS);k++) e_aud+=g_mag[k]*g_mag[k];
    for(k=1;k<=NFFT/2;k++) e_tot+=g_mag[k]*g_mag[k];
    for(hh=1;hh<=200;hh++){
        double hz=hh*375.0, v; if(hz>=FS/2) break;
        v=pico_en(hz); comb+=v*v; if(v>mx){mx=v;mxhz=hz;}
    }
    printf("   %-30s tono %7.0f  basura 0,1-8k %7.2f%%  comb375 %6.2f%% (peor %5.0f Hz)  no-tono %6.2f%%\n",
           rot, tono, 100.0*(e_aud-tono*tono)/(e_aud>0?e_aud:1),
           100.0*comb/(e_tot>0?e_tot:1), mxhz,
           100.0*(e_tot-tono*tono)/(e_tot>0?e_tot:1));
}

int main(void)
{
    int j;
    const double DESV=0.45*0.9*75000.0, PIL=0.1*75000.0, EST=0.45*0.9*75000.0;

    printf("\n== (A) EL MARGEN DE VUELTA DEL ATAN2 EN FM ANCHA ==\n");
    printf("   desviacion de pico del composite: %.0f Hz. La vuelta cae en %.0f Hz.\n\n",
           DESV+PIL+EST, FS/2.0);
    {
        static const double k_df[]={0.0,5000.0,10000.0,15000.0,20000.0,
                                    25000.0,30000.0,40000.0};
        for(j=0;j<8;j++){
            char r[64]; sprintf(r,"error de sintonia %.0f kHz",k_df[j]/1000.0);
            corre(12000.0,DESV,PIL,EST,1000.0,k_df[j],0.0,0.0,WFM_IFBW_WIDE,0);
            informe(r,1000.0);
        }
    }
    printf("\n   Y con el composite a desviacion PLENA (+-75 kHz de pico):\n");
    {
        static const double k_df[]={0.0,5000.0,10000.0,15000.0,21000.0,25000.0};
        for(j=0;j<6;j++){
            char r[64]; sprintf(r,"pleno + sintonia %.0f kHz",k_df[j]/1000.0);
            corre(12000.0,0.75*75000.0,0.1*75000.0,0.15*75000.0,1000.0,
                  k_df[j],0.0,0.0,WFM_IFBW_WIDE,0);
            informe(r,1000.0);
        }
    }

    printf("\n   (A2) Y el acantilado: desviacion de pico TOTAL del composite,\n"
           "        sin error de sintonia. La vuelta del atan2 cae en 96 kHz.\n");
    {
        static const double k_pk[]={60000.0,75000.0,85000.0,92000.0,96000.0,
                                    100000.0,110000.0,130000.0};
        for(j=0;j<8;j++){
            char r[64]; double e=k_pk[j];
            sprintf(r,"pico total %.0f kHz",e/1000.0);
            /* se reparte 72%% mono, 10%% piloto, 18%% estereo */
            corre(12000.0,0.72*e,0.10*e,0.18*e,1000.0,0.0,0.0,0.0,WFM_IFBW_WIDE,0);
            informe(r,1000.0);
        }
    }

    printf("\n== (B) LA EMISORA DE AL LADO, SIN FILTRO DE CANAL (IFBW ANCHO) ==\n");
    printf("   La nuestra a 12000 cuentas. El modelo del convertidor recorta en +-96 kHz.\n\n");
    {
        static const double k_off[]={100000.0,200000.0,400000.0};
        static const double k_rel[]={0.1,0.3,1.0};
        int a,c;
        for(a=0;a<3;a++){
            for(c=0;c<3;c++){
                char r[80];
                sprintf(r,"vecina a %.0f kHz, %.0f dB",k_off[a]/1000.0,
                        20.0*log10(k_rel[c]));
                corre(12000.0,DESV,PIL,EST,1000.0,0.0,
                      12000.0*k_rel[c],k_off[a],WFM_IFBW_WIDE,1);
                informe(r,1000.0);
            }
        }
        printf("\n   Y lo mismo con el prefiltro de FI ESTRECHO (80 kHz) puesto:\n");
        for(a=0;a<3;a++){
            for(c=0;c<3;c++){
                char r[80];
                sprintf(r,"estrecho: %.0f kHz, %.0f dB",k_off[a]/1000.0,
                        20.0*log10(k_rel[c]));
                corre(12000.0,DESV,PIL,EST,1000.0,0.0,
                      12000.0*k_rel[c],k_off[a],WFM_IFBW_NARROW,1);
                informe(r,1000.0);
            }
        }
    }
    printf("\n");
    return 0;
}

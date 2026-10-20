/*
 * CUANTOS RENGLONES SALE UN MENSAJE - 28/09/2026.
 *
 * Esto mide LO QUE EL VE: se manda un mensaje de diez tramas -de los que
 * de verdad se mandan en la banda- por un canal con desvanecimiento
 * Rayleigh, y se cuenta en cuantos renglones sale. Uno es bien; tres es lo
 * que salia en la foto.
 *
 * No va en `make comprueba` porque tarda diez segundos por punto y la
 * suite entera tarda eso. Se corre a mano cuando se toca el encadenado:
 *
 *     make jttytrozos
 *
 * Medido el 28/09, con el alcance en dos saltos y en cuatro
 * (JTTY_SALTOS_MAX, 60 mensajes por punto):
 *
 *                     2 saltos     4 saltos
 *      -9 dB            67%          97%
 *     -11 dB            33%          92%
 *
 * Y el precio: el plazo de cerrar sube de 4,2 s a 9,6 s, o sea que un
 * mensaje al que se le pierde el final tarda mas en salir. Ver
 * JTTY_ESPERA_MS.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "jtty_fec.h"
#include "jtty_msg.h"
#include "jtty_rx.h"
#include "jtty_modo.h"
#include "rtc_hw.h"

#define FS 12000.0f
#define CENTRO 1500.0f
#define TONO_HZ 31.25f
#define MS_SIM 384
#define TRAMAS 10
#define TRAMA_MU ((long)JTTY_TRAMA_SIM*MS_SIM)

volatile uint32_t g_msticks = 0;
static uint8_t prest[54432];
uint8_t *hfdl_ram_todo(uint32_t *c){ if(c)*c=sizeof prest; return prest; }
void hfdl_ram_coge(void){} void hfdl_ram_suelta(void){}
uint8_t waterfall_prestada(void){ return 0U; }
void rtc_hw_get(rtc_hw_datetime_t *o){ if(o){memset(o,0,sizeof *o);o->hour=17;o->minute=31;} }

static uint32_t sem=555u;
static float uni(void){ sem=sem*1103515245u+12345u; return (float)((sem>>8)&0xFFFFFFu)/16777216.0f; }
static float gauss(void){ float a=uni(),b=uni(); if(a<1e-7f)a=1e-7f;
    return sqrtf(-2.0f*logf(a))*cosf(6.28318531f*b); }
static float gp(float t,float bt){ float a=2.0f*3.14159265f*bt/sqrtf(logf(2.0f));
    return 0.5f*(erff(a*(t+0.5f)/1.41421356f)-erff(a*(t-0.5f)/1.41421356f)); }

#define N_AUD ((long)(12000*34))
static float aud[N_AUD];
static float inst[JTTY_TRAMA_SIM*MS_SIM];

static void mete(const uint8_t c[JTTY_CARGA_BITS], long ini, float amp)
{
    uint8_t d[JTTY_SIMBOLOS],t[JTTY_TRAMA_SIM]; float f2=0.0f; int i,m,n=(int)JTTY_TRAMA_SIM;
    jtty_fec_codifica(c,d);
    for(i=0;i<(int)JTTY_SYNC_SIM;i++) t[i]=k_jtty_sync[i];
    for(i=0;i<(int)JTTY_SIMBOLOS;i++) t[JTTY_SYNC_SIM+i]=d[i];
    for(i=0;i<n*MS_SIM;i++) inst[i]=0.0f;
    for(i=0;i<n;i++){ float f=((float)t[i]-1.5f)*TONO_HZ;
        for(m=0;m<n*MS_SIM;m++){ float x=((float)m-((float)i+0.5f)*(float)MS_SIM)/(float)MS_SIM;
            if(x>-2.5f&&x<2.5f) inst[m]+=f*gp(x,1.0f);} }
    for(m=0;m<n*MS_SIM;m++){
        f2+=6.28318531f*(CENTRO+inst[m])/FS;
        if(f2>6.28318531f) { f2-=6.28318531f; }
        aud[ini+m]+=amp*cosf(f2);
    }
}
static void fading(long n)
{ long i; float gi=gauss(),gq=gauss(); const float a=expf(-1.0f/12000.0f);
  for(i=0;i<n;i++){ gi=a*gi+sqrtf(1.0f-a*a)*gauss(); gq=a*gq+sqrtf(1.0f-a*a)*gauss();
      aud[i]*=sqrtf(0.5f*(gi*gi+gq*gq)); } }

int main(int argc,char**argv)
{
    static const char *bits="0000100110111101111000110101000001";
    uint8_t med[JTTY_CARGA_BITS], fin[JTTY_CARGA_BITS];
    float snr=(argc>1)?(float)atof(argv[1]):-9.0f;
    int rep, i; long hist[12]; int con_algo=0, enteros=0;

    for(i=0;i<12;i++) hist[i]=0;
    for(i=0;i<(int)JTTY_CARGA_BITS;i++) fin[i]=(uint8_t)(bits[i]=='1');
    memcpy(med,fin,sizeof med); med[JTTY_CARGA_BITS-1]=0;

    for(rep=0;rep<60;rep++){
        long j,n; int lineas;
        float amp=sqrtf(2.0f*powf(10.0f,snr/10.0f)*2500.0f/6000.0f);
        memset(aud,0,sizeof aud);
        for(i=0;i<TRAMAS;i++)
            mete((i==TRAMAS-1)?fin:med, 6000L+(long)i*TRAMA_MU, amp);
        n=6000L+(long)TRAMAS*TRAMA_MU+14000L; if(n>N_AUD) n=N_AUD;
        fading(n);
        for(j=0;j<n;j++) aud[j]+=gauss();

        g_msticks=0; jtty_modo_start();
        for(j=0;j+64<=n;j+=64){
            jtty_modo_mete(&aud[j],64U);
            if((j/64)%6==0){ g_msticks=(uint32_t)(j*1000L/12000L); jtty_modo_poll(); }
        }
        for(i=0;i<60;i++) jtty_modo_poll();
        g_msticks+=30000U;
        for(i=0;i<60;i++) jtty_modo_poll();
        lineas=jtty_modo_lineas();
        if(lineas>0) con_algo++;
        if(lineas==1) enteros++;
        if(lineas<12) hist[lineas]++;
        jtty_modo_stop();
    }
    printf("  SNR %+5.1f dB con desvanecimiento, %d mensajes de %d tramas\n",
           snr, 60, TRAMAS);
    for(i=0;i<12;i++) if(hist[i]) printf("     %2d renglon(es)  %3ld\n", i, hist[i]);
    printf("   sale algo: %d/60   sale en UN renglon: %d/60 (%.0f%%)\n",
           con_algo, enteros, 100.0*enteros/60.0);
    return 0;
}

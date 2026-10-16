/* Cuantos decodificados FALSOS salen de puro ruido, por minuto de escucha. */
#include "jtty_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static uint32_t sem=1u;
static float uni(void){sem=sem*1103515245u+12345u;return (float)((sem>>8)&0xFFFFFFu)/16777216.0f;}
static float gauss(void){float a=uni(),b=uni();if(a<1e-7f)a=1e-7f;return sqrtf(-2.0f*logf(a))*cosf(6.2831853f*b);}
static jtty_rx_ram_t ram;
static float x[12000];
int main(int argc,char**argv){
    int seg = (argc>1)?atoi(argv[1]):300;
    long cand=0; int falsos=0; int i,t;
    jtty_rx_start(&ram,12000.0f);
    for(t=0;t<seg;t++){
        for(i=0;i<12000;i++) x[i]=gauss();
        for(i=0;i+64<=12000;i+=64){
            jtty_rx_r_t r;
            jtty_rx_mete(&x[i],64U);
            while(jtty_rx_saca(&r)){ falsos++; printf("      falso: \"%s\"\n", r.atomo.texto); }
        }
    }
    cand=(long)jtty_rx_probados();
    printf("    %d s de ruido: %ld candidatos (%.1f/s), %d falsos = %.2f por minuto\n",
           seg, cand, (double)cand/seg, falsos, 60.0*falsos/seg);
    jtty_rx_stop();
    return 0;
}

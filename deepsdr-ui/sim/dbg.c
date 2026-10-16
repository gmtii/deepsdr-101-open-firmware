#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sstv.h"
#define FS 12000.0
static double fase=0, off=0.0;
static void emite(double hz,double seg){
    static float b[64]; static int j=0; long n=(long)(seg*FS+0.5);
    for(long i=0;i<n;i++){ fase+=2*M_PI*(hz+off)/FS; if(fase>2*M_PI)fase-=2*M_PI;
        b[j++]=(float)sin(fase); if(j==64){ sstv_process(b,64); j=0; } }
}
int main(void){
    sstv_info_t in; sstv_start((float)FS);
    emite(1900,0.300); sstv_info(&in); printf("tras lider1: desvio %.0f\n", sstv_desvio_hz());
    emite(1200,0.010); sstv_info(&in); printf("tras corte : desvio %.0f\n", sstv_desvio_hz());
    emite(1900,0.300); sstv_info(&in); printf("tras lider2: desvio %.0f\n", sstv_desvio_hz());
    emite(1200,0.030);
    int vis=44, par=0;
    for(int i=0;i<7;i++){ int b=(vis>>i)&1; par^=b; emite(b?1100:1300,0.030); }
    emite(par?1100:1300,0.030); emite(1200,0.030);
    sstv_info(&in);
    printf("VIS=%u modo=%s desvio=%.0f\n", in.vis, in.modo?in.modo:"(ninguno)", sstv_desvio_hz());
    return 0;
}

/*
 * CUANTO TARDA EL QUITADOR DE CONTINUA DE WFM EN COMERSE UNA PORTADORA
 * SIN MODULAR, CON Y SIN EL REINICIO QUE AÑADIO demod_wfm_reset_diag()
 * EL 26/10/2026. Ver sim/wfm_revision.c para el resto de la revision.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"
#define FS 192000.0
#define NBLK SDR_RX_BLOCK_SAMPLES_WFM
#define FA 1000.0
static double g_fi,g_t;
static void bloque(int16_t*raw,double amp,double desv){uint32_t n;for(n=0;n<(uint32_t)NBLK;n++){double f=desv*sin(2*M_PI*FA*g_t/FS);double i=amp*cos(g_fi),q=amp*sin(g_fi);g_fi+=2*M_PI*f/FS;if(g_fi>M_PI)g_fi-=2*M_PI;if(g_fi<-M_PI)g_fi+=2*M_PI;g_t+=1;raw[2*n]=(int16_t)lrint(i);raw[2*n+1]=(int16_t)lrint(q);}}
static void corre(int con_reset){
 static int16_t raw[2*NBLK];int b;float qi,qq;
 demod_am_init();demod_am_set_mode(DEMOD_MODE_WFM);demod_wfm_reset_diag();cons_captura_tramas(NBLK);g_fi=g_t=0;
 for(b=0;b<800;b++){bloque(raw,2000.0,50000.0);demod_wfm_process_raw(raw);}  /* 2,1 s modulado: n llega a 512 */
 if(con_reset) demod_wfm_reset_diag();   /* <- lo que hace el cambio 3 en un salto de Rin */
 printf("  %-28s","(cambio 3) reset_diag"); if(!con_reset)printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b  sin reset (como antes)      ");
 printf("\n   bloques de portadora quieta:  ");
 for(b=1;b<=1500;b++){bloque(raw,2000.0,0.0);demod_wfm_process_raw(raw);
  if(b==1||b==5||b==20||b==100||b==375||b==750||b==1500){demod_wfm_get_dc(&qi,&qq);
   printf("%d->%.0f  ",b,sqrt((double)qi*qi+(double)qq*qq));}}
 printf("\n\n");}
int main(void){printf("\nPORTADORA 2000 SIN MODULAR: cuanto tarda el quitador en comersela\n"
 "(cuentas restadas tras N bloques; 2000 = portadora borrada del todo; 375 bloques = 1 s)\n\n");
 corre(1);corre(0);return 0;}

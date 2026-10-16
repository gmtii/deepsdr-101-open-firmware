/*
 * BANCO DE SENSIBILIDAD DE JTTY, aparte de `comprueba`.
 *
 * Existe porque medir dB de verdad pide cientos de intentos por punto y eso
 * no puede vivir en una suite que corre en diez segundos. Aqui se puede
 * gastar un minuto y sacar una curva con la que comparar cambios.
 *
 * Y con DESVANECIMIENTO, que es lo que hay en onda corta: la amplitud de la
 * señal sube y baja con una constante de tiempo de alrededor de un segundo.
 * Con ruido plano y amplitud fija -lo que media el banco de antes- un
 * detector que pondera mal los simbolos sale igual de bien que uno que los
 * pondera bien, o sea que el banco no puede ver la diferencia.
 */
#include "jtty_rx.h"
#include "jtty_fec.h"
#include "jtty_msg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS 12000.0f
#define BAUD 31.25f
#define TONO_HZ 31.25f
#define CENTRO 1500.0f
#define MS_SIM ((int)(FS/BAUD))

static uint32_t sem = 1u;
static float uni(void){ sem = sem*1103515245u+12345u; return (float)((sem>>8)&0xFFFFFFu)/16777216.0f; }
static float gauss(void){ float a=uni(),b=uni(); if(a<1e-7f)a=1e-7f; return sqrtf(-2.0f*logf(a))*cosf(6.2831853f*b); }

static float gpul(float t, float bt){
    float a = 2.0f*3.14159265f*bt/sqrtf(logf(2.0f));
    return 0.5f*(erff(a*(t+0.5f)/1.4142136f)-erff(a*(t-0.5f)/1.4142136f));
}

static jtty_rx_ram_t ram;
static int *g_cand;
#define NMAX (12000*8)
static float x[NMAX];
static float inst[64*384];

/* fade: 0 = amplitud fija, 1 = Rayleigh lento (1 s de coherencia) */
static int corre(const uint8_t *carga, int retardo, float snr, float desv, int fade)
{
    uint8_t datos[JTTY_SIMBOLOS], tonos[JTTY_TRAMA_SIM];
    int n = (int)JTTY_TRAMA_SIM, i, m, k = 0, buenos = 0;
    float fase = 0.0f, amp;
    float fi = 0.0f, fq = 0.0f;   /* el desvanecimiento, filtrado */

    jtty_fec_codifica(carga, datos);
    for (i = 0; i < (int)JTTY_SYNC_SIM; i++) tonos[i] = k_jtty_sync[i];
    for (i = 0; i < (int)JTTY_SIMBOLOS; i++) tonos[JTTY_SYNC_SIM+i] = datos[i];

    amp = sqrtf(2.0f*powf(10.0f, snr/10.0f)*2500.0f/6000.0f);
    memset(x, 0, sizeof x);
    for (i = 0; i < n*MS_SIM; i++) inst[i] = 0.0f;
    for (i = 0; i < n; i++) {
        float f = ((float)tonos[i]-1.5f)*TONO_HZ;
        for (m = 0; m < n*MS_SIM; m++) {
            float t = ((float)m-((float)i+0.5f)*(float)MS_SIM)/(float)MS_SIM;
            if (t > -2.5f && t < 2.5f) inst[m] += f*gpul(t, 1.0f);
        }
    }
    for (i = 0; i < retardo; i++) x[k++] = 0.0f;
    for (m = 0; m < n*MS_SIM; m++) {
        float a = amp;

        if (fade) {
            /* Dos gaussianas filtradas paso bajo: envolvente Rayleigh con
             * una constante de tiempo de un segundo, que es lo tipico en HF.
             * Se normaliza para que la potencia MEDIA sea la misma, o si no
             * se estaria comparando con menos señal y no con otra clase. */
            float al = 1.0f/(1.0f*FS);
            fi += al*(gauss()-fi); fq += al*(gauss()-fq);
            a = amp*sqrtf(fi*fi+fq*fq)/(sqrtf(al/2.0f)*1.4142136f);
        }
        fase += 6.2831853f*(CENTRO+desv+inst[m])/FS;
        x[k++] = a*cosf(fase);
    }
    n = k + 12000; if (n > NMAX) n = NMAX;
    for (i = 0; i < n; i++) x[i] += gauss();

    jtty_rx_start(&ram, FS);
    for (i = 0; i+64 <= n; i += 64) {
        jtty_rx_r_t r;
        jtty_rx_mete(&x[i], 64U);
        while (jtty_rx_saca(&r)) {
            if (strcmp(r.atomo.texto, "CQ K1ABC CQ") == 0) buenos++;
        }
    }
    if (g_cand) { *g_cand += (int)jtty_rx_probados(); }
    jtty_rx_stop();
    return buenos;
}

int main(int argc, char **argv)
{
    uint8_t carga[JTTY_CARGA_BITS];
    const char *bits = "0000100110111101111000110101000001";
    static const float snr[] = {-8,-10,-11,-12,-13,-14,-15,-16,-17};
    int rep = (argc > 1) ? atoi(argv[1]) : 60;
    int fade = (argc > 2) ? atoi(argv[2]) : 0;
    unsigned i;
    int j;

    for (j = 0; j < (int)JTTY_CARGA_BITS; j++) carga[j] = (uint8_t)(bits[j]=='1');
    printf("\n  %s, %d intentos por punto\n\n", fade ? "CON desvanecimiento" : "ruido plano", rep);
    for (i = 0; i < sizeof snr/sizeof snr[0]; i++) {
        int b = 0, cand = 0;
        g_cand = &cand;
        for (j = 0; j < rep; j++) {
            sem = 7919u*(uint32_t)(j+1) + 104729u*(uint32_t)i;
            b += (corre(carga, 300 + j*37, snr[i], ((j%7)-3)*11.0f, fade) >= 1);
        }
        printf("    %6.1f dB   %3d de %3d   %5.1f %%   sincronismos %4d\n",
               (double)snr[i], b, rep, 100.0*b/rep, cand);
    }
    return 0;
}

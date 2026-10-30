/*
 * LA CADENA, DEL I/Q DEL ADC A dcf77_feed(): ¿LLEGA LA ENVOLVENTE ENTERA?
 * ======================================================================
 *
 * *** Un probador: "MSF en 60 kHz no funciona en absoluto, con el audio
 * claramente presente". ***
 *
 * Habia DOS bancos de MSF y los dos pasaban, y entre los dos quedaba un
 * hueco por el que cabia justo esa averia:
 *
 *   sim/msftest.c    el FORMATO, con señal que fabrica el propio banco
 *   sim/msf_real.c   el DECODIFICADOR, con la envolvente de una grabacion
 *                    de verdad - llega a LISTO y saca 20:56 20/09/24
 *
 * Los dos entran por `dcf77_feed()`, o sea por el final. Lo que ninguno
 * miraba es lo que hay ANTES: el I/Q crudo del ADC, el desplazamiento de
 * Fs/4, el filtro de canal, el detector de envolvente y el promediado por
 * bloque de demod_am.c. Si la cadena se comiera el contraste, los dos
 * bancos seguirian en verde y la radio seguiria sin decodificar nada.
 *
 * Esto cierra ese hueco, y contesta UNA pregunta: la envolvente que se le
 * mete al ADC, ¿llega entera al decodificador?
 *
 * COMO. Se sintetiza la señal tal como la ve el ADC -portadora en la
 * frecuencia intermedia, amplitud modulada por una marca de MSF- y se mete
 * por `demod_am_process_raw()`, que es la MISMA puerta por la que entra la
 * radio. Al otro lado se captura lo que llega a `dcf77_feed()` y se
 * compara con lo que se mando.
 *
 * UN BLOQUE ES UNA MUESTRA DE ENVOLVENTE, exactamente: a 96 kHz el bloque
 * son 256 muestras y `dcf77_submuestras()` vale 1 para MSF, o sea que
 * demod_am.c llama a `dcf77_feed()` una vez por bloque, 375 veces por
 * segundo. No hay que remuestrear nada.
 *
 * LO QUE NO MIDE, y conviene decirlo: la antena, el ruido del sitio y la
 * propagacion a 60 kHz. Esto mide la radio de la entrada del ADC para
 * adentro. Si esto pasa y la radio no decodifica, el problema esta fuera
 * de la radio.
 */
#include "demod_stubs.h"
#include "demod_am.h"
#include "sdr_rx.h"
#include "dcf77.h"
#include <stdio.h>
#include <math.h>

#define FS      96000.0f
#define F_IF    24000.0f     /* DEMOD_IF_OFFSET_HZ */
#define AMP     9000.0f      /* portadora encendida, en cuentas del ADC */
#define BAJO    0.05f        /* MSF apaga del todo; 5% es ruido de fondo */
#define SEG     3            /* segundos de señal */
#define N_ENV   (SEG * 375)

static int16_t g_blq[SDR_RX_BLOCK_SAMPLES * 2u];
static float   g_dado[N_ENV];
static float   g_visto[N_ENV + 64];
static long    g_nv;

/* El decodificador de verdad no entra aqui: lo que se mide es la cadena,
 * y para eso hace falta VER las muestras, no decodificarlas. */
uint8_t dcf77_activo(void) { return 1u; }
void    dcf77_feed(float m)
{
    if (g_nv < (long)(sizeof g_visto / sizeof g_visto[0])) {
        g_visto[g_nv++] = m;
    }
}
float   dcf77_fase_iq(float i, float q) { (void)i; (void)q; return 0.0f; }
uint8_t dcf77_usa_fase(hora_emisora_t e) { (void)e; return 0u; }
uint8_t dcf77_submuestras(hora_emisora_t e) { (void)e; return 1u; }
hora_emisora_t dcf77_emisora(void) { return (hora_emisora_t)0; }

int main(void)
{
    double fase = 0.0;
    long i;
    int mal = 0;
    float mx = 0.0f, mn = 1e30f, c_dado, c_visto;

    printf("\n  LA CADENA DE MSF, DEL I/Q DEL ADC A dcf77_feed()\n\n");

    /* La marca de cada segundo: 100 ms apagados y 900 encendidos, que es
     * el caso mas corto que MSF emite y por tanto el que peor sobrevive a
     * cualquier filtro. */
    for (i = 0; i < N_ENV; i++) {
        double ms = fmod((double)i * 1000.0 / 375.0, 1000.0);
        g_dado[i] = (ms < 100.0) ? BAJO : 1.0f;
    }

    demod_am_init();
    demod_am_set_active_rate(0u);          /* 96 kHz, el de siempre */
    demod_am_set_rf_doble(0u);
    demod_am_set_mode(DEMOD_MODE_AM);
    demod_am_set_audio_bw(AUDIO_BW_1K8);   /* lo que pone hora_sintoniza() */

    for (i = 0; i < N_ENV; i++) {
        uint32_t k;
        double dp = 2.0 * M_PI * (double)F_IF / (double)FS;

        for (k = 0; k < SDR_RX_BLOCK_SAMPLES; k++) {
            float a = AMP * g_dado[i];
            g_blq[2u * k]      = (int16_t)(a * (float)cos(fase));
            g_blq[2u * k + 1u] = (int16_t)(a * (float)sin(fase));
            fase += dp;
            if (fase > 2.0 * M_PI) { fase -= 2.0 * M_PI; }
        }
        demod_am_process_raw(g_blq);
    }

    /* El primer segundo se tira: los filtros tienen que asentarse, y eso
     * es verdad en la radio tambien. */
    for (i = 375; i < g_nv; i++) {
        if (g_visto[i] > mx) { mx = g_visto[i]; }
        if (g_visto[i] < mn) { mn = g_visto[i]; }
    }
    c_dado  = (1.0f - BAJO) / 1.0f;
    c_visto = (mx > 0.0f) ? ((mx - mn) / mx) : 0.0f;

    printf("    muestras dadas / recibidas ... %d / %ld\n", N_ENV, g_nv);
    printf("    contraste dado ............... %.3f\n", (double)c_dado);
    printf("    contraste recibido ........... %.3f\n", (double)c_visto);
    printf("    alto / bajo recibidos ........ %.1f / %.1f\n",
           (double)mx, (double)mn);

    printf("\n  LO QUE SE EXIGE:\n");
    printf("    una muestra por bloque ....... %s\n",
           (g_nv == (long)N_ENV) ? "ok" : "*** NO ***");
    if (g_nv != (long)N_ENV) { mal++; }

    /*
     * El liston es 0,90 y no 0,95: el filtro de canal de 1,8 kHz tiene
     * que redondear los flancos de una marca de 100 ms, y eso cuesta algo.
     * Lo que NO puede es comerse la marca - y el decodificador se planta
     * por diseño por debajo de 0,25, asi que hay sitio de sobra.
     */
    printf("    la cadena conserva el contraste %s\n",
           (c_visto >= 0.90f) ? "ok" : "*** NO ***");
    if (c_visto < 0.90f) { mal++; }

    printf("\n");
    if (mal) {
        printf("  *** %d fallo(s): la cadena se come la señal de MSF ***\n\n",
               mal);
        return 1;
    }
    printf("  la cadena entrega la envolvente entera  ok\n\n");
    return 0;
}

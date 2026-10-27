/*
 * ======================================================================
 * TOCAR LOS MANDOS DE FM ANCHA EN MARCHA - 26/10/2026
 * ======================================================================
 *
 * La cadena de WFM tiene CINCO juegos de biquads vivos a la vez:
 *   s_wfm_alpf_wide / norm / narrow   (el ancho de audio, s_audio_bw)
 *   s_wfm_ifbw_i / q                  (el prefiltro de FI, s_wfm_ifbw)
 *
 * Cada bloque corre UNO de los tres de audio y, si el prefiltro esta en
 * estrecho, los dos de FI. Los que no corren se quedan con el estado que
 * tuvieran de la ultima vez, y NADIE lo borra: demod_am_init() los
 * construye al arrancar y ya esta. demod_wfm_reset_diag() no los toca y
 * cadena_olvida() tampoco -solo llama a nb_reset(), nr_ss_reset() y
 * anotch_reset()-.
 *
 * Asi que cambiar de ancho, o encender el prefiltro, mete en la cadena el
 * estado rancio del filtro que entra. Esto lo mide: cuanto salta el audio
 * en el bloque del cambio.
 *
 * El del PREFILTRO es el peor caso de los dos, porque ese filtro esta ANTES
 * del discriminador: lo que mete no es un escalon en el audio, es un
 * escalon en la FASE de la senal compleja, y el discriminador lo convierte
 * en un pinchazo.
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

static double g_fi, g_t;

static void bloque(int16_t *raw, double amp, double desv)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t/FS;
        double f = desv*cos(2.0*M_PI*1000.0*t)
                 + 7500.0*cos(2.0*M_PI*19000.0*t)
                 + 30375.0*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t);
        double i,q;
        g_fi += 2.0*M_PI*f/FS;
        if(g_fi> M_PI) g_fi-=2.0*M_PI;
        if(g_fi<-M_PI) g_fi+=2.0*M_PI;
        g_t += 1.0;
        i = amp*cos(g_fi); q = amp*sin(g_fi);
        if(i> 32767.0)i= 32767.0; if(i<-32768.0)i=-32768.0;
        if(q> 32767.0)q= 32767.0; if(q<-32768.0)q=-32768.0;
        raw[2U*n]=(int16_t)lrint(i); raw[2U*n+1U]=(int16_t)lrint(q);
    }
}

/* El pico del bloque y el salto mas grande entre muestras seguidas. */
static void mira(const int16_t *s, int *pico, int *salto)
{
    int k, p = 0, d = 0;
    for (k = 0; k < NBLK; k++) {
        int v = s[2*k]; if (v<0) v=-v; if (v>p) p=v;
        if (k) { int e = (int)s[2*k]-(int)s[2*k-2]; if(e<0)e=-e; if(e>d)d=e; }
    }
    *pico = p; *salto = d;
}

static void prueba(const char *rot, int cambia_en, audio_bw_t bw0, audio_bw_t bw1,
                   wfm_ifbw_t if0, wfm_ifbw_t if1, double amp0, double amp1)
{
    static int16_t raw[2*NBLK];
    int b, pico, salto, base_p = 0, base_s = 0;
    int peor_p = 0, peor_s = 0, peor_b = -1;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi=0.0; g_t=0.0;

    /* Primero una sesion larga con el OTRO ajuste puesto, para que el
     * filtro que luego va a entrar quede con estado de verdad dentro -que
     * es lo que pasa en la radio cuando se va y se vuelve-. */
    demod_am_set_audio_bw(bw1); demod_am_set_wfm_ifbw(if1);
    for (b = 0; b < 400; b++) { bloque(raw, amp1, 60000.0); demod_wfm_process_raw(raw); }
    /* y ahora la de verdad, con el ajuste inicial */
    demod_am_set_audio_bw(bw0); demod_am_set_wfm_ifbw(if0);
    for (b = 0; b < 400; b++) { bloque(raw, amp0, 30375.0); demod_wfm_process_raw(raw); }
    mira(cons_audio(), &base_p, &base_s);

    for (b = 0; b < 40; b++) {
        if (b == cambia_en) { demod_am_set_audio_bw(bw1); demod_am_set_wfm_ifbw(if1); }
        bloque(raw, amp0, 30375.0);
        demod_wfm_process_raw(raw);
        mira(cons_audio(), &pico, &salto);
        if (b >= cambia_en && salto > peor_s) { peor_s = salto; peor_p = pico; peor_b = b-cambia_en; }
    }
    printf("   %-42s regimen pico %6d salto %6d  ->  tras el cambio pico %6d salto %6d (bloque +%d)\n",
           rot, base_p, base_s, peor_p, peor_s, peor_b);
}

int main(void)
{
    printf("\n== TOCAR LOS MANDOS DE FM ANCHA EN MARCHA ==\n");
    printf("   'salto' es la mayor diferencia entre dos muestras seguidas del audio.\n\n");

    prueba("ancho de audio 15k -> 8k",   5, AUDIO_BW_4K0, AUDIO_BW_2K3,
           WFM_IFBW_WIDE, WFM_IFBW_WIDE, 12000.0, 12000.0);
    prueba("ancho de audio 15k -> 4k",   5, AUDIO_BW_4K0, AUDIO_BW_1K8,
           WFM_IFBW_WIDE, WFM_IFBW_WIDE, 12000.0, 12000.0);
    prueba("ancho de audio 8k -> 15k",   5, AUDIO_BW_2K3, AUDIO_BW_4K0,
           WFM_IFBW_WIDE, WFM_IFBW_WIDE, 12000.0, 12000.0);
    prueba("prefiltro FI ancho -> estrecho", 5, AUDIO_BW_4K0, AUDIO_BW_4K0,
           WFM_IFBW_WIDE, WFM_IFBW_NARROW, 12000.0, 12000.0);
    printf("\n   Y lo mismo cuando el filtro que entra guarda estado de una\n"
           "   emisora MUCHO mas fuerte (es lo que pasa al volver a WFM):\n");
    prueba("audio 15k -> 8k, rancio de 30000 cuentas", 5, AUDIO_BW_4K0, AUDIO_BW_2K3,
           WFM_IFBW_WIDE, WFM_IFBW_WIDE, 1500.0, 30000.0);
    prueba("prefiltro ancho -> estrecho, rancio de 30000", 5, AUDIO_BW_4K0, AUDIO_BW_4K0,
           WFM_IFBW_WIDE, WFM_IFBW_NARROW, 1500.0, 30000.0);
    printf("\n");
    return 0;
}

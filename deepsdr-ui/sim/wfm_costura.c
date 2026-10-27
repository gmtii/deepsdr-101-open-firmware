/*
 * ======================================================================
 * LAS COSTURAS DE BLOQUE DE LA FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * Un zumbido aspero y constante a ritmo de bloque -375 Hz y multiplos- sale
 * de que algo se rompa SIEMPRE EN LA MISMA POSICION de cada bloque. Esto no
 * mira ninguna etapa: mira la salida de audio y pregunta si la rotura
 * depende de DONDE cae la muestra dentro del bloque.
 *
 * COMO. Se promedia |y[n] - 2y[n-1] + y[n-2]| (la segunda diferencia, que es
 * lo que mas delata un salto) separando por la posicion n dentro del bloque.
 * Si el proceso es continuo, las 512 posiciones dan lo mismo. Si una etapa
 * arranca con basura, se queda sin estado, o algo se reinicia por bloque,
 * la posicion 0 -o la que sea- se dispara.
 *
 * Se mide con la cadena de verdad: demod_wfm_process_raw() del firmware.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS     192000.0
#define NBLK   SDR_RX_BLOCK_SAMPLES_WFM

static double g_fi, g_t;

static void bloque(int16_t *raw, double amp, double desv, double faudio,
                   double piloto, double estereo)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / FS;
        double f = desv * cos(2.0 * M_PI * faudio * t)
                 + piloto * cos(2.0 * M_PI * 19000.0 * t)
                 + estereo * cos(2.0*M_PI*38000.0*t) * cos(2.0*M_PI*700.0*t);
        double i, q;
        g_fi += 2.0 * M_PI * f / FS;
        if (g_fi >  M_PI) { g_fi -= 2.0 * M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0 * M_PI; }
        g_t += 1.0;
        i = amp * cos(g_fi); q = amp * sin(g_fi);
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U*n] = (int16_t)lrint(i);
        raw[2U*n+1U] = (int16_t)lrint(q);
    }
}

static double g_pos[NBLK];      /* |2a dif| acumulada por posicion */
static long   g_cnt;

static void perfil(const char *rotulo, int calienta, int bloques,
                   double amp, double desv, audio_bw_t bw, wfm_ifbw_t ifbw,
                   agc_profile_t agc, int corrompe_cada)
{
    static int16_t raw[2*NBLK];
    static double hist[3];
    int b, k;
    double med = 0.0, mx = 0.0; int mxk = 0;

    memset(g_pos, 0, sizeof g_pos); g_cnt = 0;
    hist[0] = hist[1] = hist[2] = 0.0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(bw);
    demod_am_set_wfm_ifbw(ifbw);
    demod_am_set_agc_profile(agc);
    cons_captura_tramas((uint32_t)NBLK);
    cons_bloque_corrupto(0);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < calienta + bloques; b++) {
        const int16_t *sal;
        bloque(raw, amp, desv, 1000.0, 0.1*75000.0, 0.3*75000.0);
        if (corrompe_cada && ((b % corrompe_cada) == 0)) {
            cons_bloque_corrupto(1);
        } else {
            cons_bloque_corrupto(0);
        }
        demod_wfm_process_raw(raw);
        if (b < calienta) { continue; }
        sal = cons_audio();
        for (k = 0; k < NBLK; k++) {
            double y = (double)sal[2*k];
            double d2 = y - 2.0*hist[0] + hist[1];
            hist[1] = hist[0]; hist[0] = y;
            if (b > calienta) { g_pos[k] += fabs(d2); }
        }
        if (b > calienta) { g_cnt++; }
    }
    for (k = 0; k < NBLK; k++) { med += g_pos[k]; }
    med /= (double)NBLK;
    for (k = 0; k < NBLK; k++) {
        if (g_pos[k] > mx) { mx = g_pos[k]; mxk = k; }
    }
    printf("  %-34s media %8.1f   peor pos %3d = %8.1f  (x%.1f)   n=%d/%d/%d\n",
           rotulo, med / (g_cnt?g_cnt:1), mxk, mx / (g_cnt?g_cnt:1),
           med > 0 ? mx / med : 0.0,
           (int)(g_pos[0]/(g_cnt?g_cnt:1)), (int)(g_pos[1]/(g_cnt?g_cnt:1)),
           (int)(g_pos[511]/(g_cnt?g_cnt:1)));
}

int main(void)
{
    printf("\n== LAS COSTURAS DE BLOQUE DE LA FM ANCHA ==\n");
    printf("   |y[n]-2y[n-1]+y[n-2]| medio, por posicion dentro del bloque.\n"
           "   Un proceso continuo da el mismo numero en las 512.\n"
           "   La ultima columna son las posiciones 0, 1 y 511.\n\n");

    printf("  -- regimen, ya convergido --\n");
    perfil("ANCHO/IFBW ancho/MEDIO", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);
    perfil("ANCHO/IFBW estrecho/MEDIO", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_NARROW, AGC_PROFILE_MEDIUM, 0);
    perfil("NORMAL 8k", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_2K3, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);
    perfil("MANUAL", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MANUAL, 0);
    perfil("RAPIDO", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_FAST, 0);

    printf("\n  -- recien entrado en WFM (sin calentar: el silencio tapa 200) --\n");
    perfil("bloques 1..300", 0, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);
    perfil("bloques 201..500 (tras el mute)", 200, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);

    printf("\n  -- con bloques descartados por corruptos --\n");
    perfil("uno de cada 8 corrupto", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 8);
    perfil("uno de cada 2 corrupto", 400, 300, 12000.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 2);

    printf("\n  -- portadora floja (el discriminador cerca del umbral) --\n");
    perfil("amp 300", 400, 300, 300.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);
    perfil("amp 60", 400, 300, 60.0, 30000.0,
           AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM, 0);

    printf("\n");
    return 0;
}

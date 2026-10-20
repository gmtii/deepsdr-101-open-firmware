/*
 * EL ECUALIZADOR CONTRA UN CANAL CON ECO.
 *
 * Un ecualizador que no se mide no sirve de nada, porque CONVERGER NO ES
 * LO MISMO QUE ACERTAR: un lazo puede quedarse quieto en una solucion mala
 * y parecer que ha enganchado. Asi que aqui no se mira si los coeficientes
 * se estabilizan, se mira el OJO: cuanto se parece la salida a la
 * constelacion que se transmitio.
 *
 * Y se mide tres veces sobre el MISMO canal:
 *
 *   1. Sin ecualizar. Tiene que salir MAL. Si esto saliera bien, el canal
 *      de prueba no tendria eco y el resto del banco no probaria nada.
 *   2. Solo con CMA. Tiene que abrir el ojo y dejarlo GIRADO.
 *   3. CMA y luego dirigido. Tiene que quedar mejor que 2.
 *
 * Y una cuarta, que es la que de verdad asusta: con un canal LIMPIO el
 * ecualizador no puede estropear nada. Un lazo adaptativo suelto sobre una
 * señal que ya estaba bien es una forma excelente de romper los diecinueve
 * modos que ya funcionaban.
 */
#include "ecualiz.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *det)
{
    printf("    %-52s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!bien) { fallos++; }
}

/* --- generador --------------------------------------------------------- */

static uint32_t s_rnd = 12345u;
static uint32_t rnd(void) { s_rnd = s_rnd * 1103515245u + 12345u; return s_rnd >> 8; }

/* ruido gaussiano aproximado por suma de doce uniformes (Irwin-Hall) */
static float gauss(void)
{
    int i; float s = 0.0f;
    for (i = 0; i < 12; i++) { s += (float)(rnd() & 0xFFFFu) / 65535.0f; }
    return s - 6.0f;
}

#define N_SIM   24000
#define N_CMA   10000     /* simbolos a ciegas antes de pasar a dirigido */
#define N_MIDE   3000     /* los ultimos, sobre los que se mide el ojo */

static float g_ar[N_SIM], g_ai[N_SIM];   /* lo que se transmitio */
static int   g_qam = 0;                  /* 0 = QPSK, 1 = 16-QAM */

/* los cuatro niveles de una 16-QAM, normalizados a potencia media 1 */
static const float k_q16[4] = { -0.94868f, -0.31623f, 0.31623f, 0.94868f };

static void transmite(void)
{
    int i;
    s_rnd = 12345u;
    for (i = 0; i < N_SIM; i++) {
        uint32_t b = rnd();
        if (g_qam) {
            g_ar[i] = k_q16[b & 3u];
            g_ai[i] = k_q16[(b >> 2) & 3u];
        } else {
            g_ar[i] = (b & 1u) ? 0.70710678f : -0.70710678f;
            g_ai[i] = (b & 2u) ? 0.70710678f : -0.70710678f;
        }
    }
}

/* el decisor que usa el lazo dirigido y la medida del ojo */
static void decide(float a, float b, float *dr, float *dj)
{
    if (!g_qam) {
        *dr = (a >= 0.0f) ? 0.70710678f : -0.70710678f;
        *dj = (b >= 0.0f) ? 0.70710678f : -0.70710678f;
        return;
    }
    {
        int k, mr = 0, mi = 0;
        float er = 1e30f, ei = 1e30f;
        for (k = 0; k < 4; k++) {
            float d = a - k_q16[k]; if (d < 0.0f) d = -d;
            if (d < er) { er = d; mr = k; }
            d = b - k_q16[k]; if (d < 0.0f) d = -d;
            if (d < ei) { ei = d; mi = k; }
        }
        *dr = k_q16[mr]; *dj = k_q16[mi];
    }
}

/*
 * El canal. Tres ecos con el principal en medio, que es la forma de un
 * multitrayecto de HF: algo llega antes por la onda de tierra y algo despues
 * por un salto de mas. Los numeros no son inventados a ojo: con h=[0,3 1
 * 0,4] la respuesta tiene un cero cerca del circulo unidad, que es el caso
 * que de verdad cierra el ojo y el que hace falta un ecualizador para
 * abrir. Un eco de 0,1 lo aguantaria cualquier cosa.
 */
static void canal(int i, float snr_db, float *yr, float *yi)
{
    static const float hr[3] = { 0.30f, 1.00f, 0.40f };
    float ar = 0.0f, ai = 0.0f;
    int k;
    for (k = 0; k < 3; k++) {
        int j = i - k;
        if (j < 0) { continue; }
        ar += hr[k] * g_ar[j];
        ai += hr[k] * g_ai[j];
    }
    if (snr_db < 90.0f) {
        float s = powf(10.0f, -snr_db / 20.0f);
        ar += s * gauss() * 0.4f;
        ai += s * gauss() * 0.4f;
    }
    *yr = ar; *yi = ai;
}

/* canal limpio: solo retardo, sin eco */
static void canal_limpio(int i, float *yr, float *yi)
{
    *yr = g_ar[i]; *yi = g_ai[i];
}

/* --- medida del ojo ---------------------------------------------------- */

/*
 * MER en dB sobre los ultimos N_MIDE simbolos, QUITANDO ANTES EL GIRO. El
 * CMA no mira la fase, asi que su salida sale girada un angulo cualquiera y
 * medir sin deshacerlo daria siempre mal aunque el ojo estuviese perfecto.
 * El giro se estima elevando a la cuarta -que es donde una QPSK deja de
 * tener la simetria de cuatro- y promediando.
 */
static float mer_db(const float *yr, const float *yi, int n0, int n1)
{
    float sr = 0.0f, si = 0.0f, c, s, pot = 0.0f, err = 0.0f;
    float fase;
    int i;

    for (i = n0; i < n1; i++) {
        float a = yr[i], b = yi[i];
        /* (a+jb)^2 */
        float p2r = a * a - b * b, p2i = 2.0f * a * b;
        /* ^4 */
        sr += p2r * p2r - p2i * p2i;
        si += 2.0f * p2r * p2i;
    }
    /*
     * OJO CON LOS 45 GRADOS. Una QPSK ideal esta en +-45, asi que su cuarta
     * potencia apunta a 180, no a 0. Girando por -atan2/4 a secas la
     * constelacion se queda sobre los EJES y entonces no se parece a
     * (+-0,707, +-0,707) por mucho que el ojo este abierto: la primera
     * version de este banco media 2,3 dB sobre una señal PERFECTA. Hay que
     * quitarle esos 45 grados.
     */
    fase = atan2f(si, sr) / 4.0f - 0.78539816f;
    /* la cuarta potencia deja cuatro soluciones; cualquiera vale porque la
     * constelacion es simetrica de cuatro. Se gira al reves. */
    c = cosf(-fase); s = sinf(-fase);

    for (i = n0; i < n1; i++) {
        float a = yr[i] * c - yi[i] * s;
        float b = yr[i] * s + yi[i] * c;
        float dr, dj;
        decide(a, b, &dr, &dj);
        pot += dr * dr + dj * dj;
        err += (a - dr) * (a - dr) + (b - dj) * (b - dj);
    }
    if (err <= 0.0f) { return 99.0f; }
    return 10.0f * log10f(pot / err);
}

/* --- las pasadas ------------------------------------------------------- */

static float g_yr[N_SIM], g_yi[N_SIM];
static float g_mu_dd = 0.001f;

/* modo: 0 = nada (pasa el canal tal cual), 1 = solo CMA, 2 = CMA y dirigido */
static float pasada(int modo, float snr_db, int limpio, ecu_t *eq_out)
{
    ecu_t eq;
    int i;

    /* R2 de Godard = E[|a|^4]/E[|a|^2]: 1,0 en QPSK, 1,32 en 16-QAM */
    ecu_init(&eq, 21U, 0.004f, g_qam ? 1.32f : 1.0f);

    for (i = 0; i < N_SIM; i++) {
        float xr, xi, yr, yi;

        if (limpio) { canal_limpio(i, &xr, &xi); }
        else        { canal(i, snr_db, &xr, &xi); }

        if (modo == 0) { g_yr[i] = xr; g_yi[i] = xi; continue; }

        ecu_mete(&eq, xr, xi, &yr, &yi);

        if (modo == 2 && i == N_CMA) {
            ecu_modo(&eq, ECU_DIRIGIDO);
            /* y el paso a la cuarta parte: ver ecu_paso() en ecualiz.h */
            ecu_paso(&eq, g_mu_dd);
        }

        if (eq.modo == ECU_DIRIGIDO) {
            float dr, dj;
            decide(yr, yi, &dr, &dj);
            ecu_corrige(&eq, yr, yi, dr, dj);
        } else {
            ecu_corrige(&eq, yr, yi, 0.0f, 0.0f);
        }

        g_yr[i] = yr; g_yi[i] = yi;
    }
    if (eq_out) { *eq_out = eq; }
    return mer_db(g_yr, g_yi, N_SIM - N_MIDE, N_SIM);
}

int main(void)
{
    char det[96];
    float sin_ecu, solo_cma, cma_dd, limpio_sin, limpio_con;
    ecu_t eq_cma, eq_dd, eq_lim;

    printf("\nEL ECUALIZADOR CONTRA UN CANAL CON ECO\n\n");
    transmite();

    printf("  canal h = [0,30  1,00  0,40], sin ruido\n");

    sin_ecu  = pasada(0, 99.0f, 0, 0);
    solo_cma = pasada(1, 99.0f, 0, &eq_cma);
    cma_dd   = pasada(2, 99.0f, 0, &eq_dd);

    snprintf(det, sizeof det, "%.1f dB", (double)sin_ecu);
    di("sin ecualizar el ojo esta cerrado (<10 dB)", sin_ecu < 10.0f, det);

    snprintf(det, sizeof det, "%.1f dB (sin: %.1f)", (double)solo_cma, (double)sin_ecu);
    di("el CMA solo ya abre el ojo (>15 dB)", solo_cma > 15.0f, det);

    snprintf(det, sizeof det, "%.1f dB (CMA: %.1f)", (double)cma_dd, (double)solo_cma);
    di("y el dirigido lo deja mejor todavia (>25 dB)", cma_dd > 25.0f, det);
    /*
     * AQUI NO SE COMPARA CMA CONTRA DIRIGIDO. Sin ruido los dos llegan a
     * setenta y pico dB, que es el suelo numerico del float: comparar ahi es
     * comparar dos errores de redondeo y la respuesta sale a cara o cruz. El
     * dirigido se gana el sitio donde se nota, que es CON RUIDO, y eso se
     * mide abajo.
     */
    snprintf(det, sizeof det, "%.3g", (double)ecu_dispersion(&eq_cma));
    di("la dispersion de Godard baja de 0,02", ecu_dispersion(&eq_cma) < 0.02f, det);

    snprintf(det, sizeof det, "%.3g", (double)ecu_energia(&eq_dd));
    di("los coeficientes no se disparan (<10)", ecu_energia(&eq_dd) < 10.0f, det);

    printf("\n  canal limpio: no puede estropear lo que ya iba bien\n");
    limpio_sin = pasada(0, 99.0f, 1, 0);
    limpio_con = pasada(2, 99.0f, 1, &eq_lim);
    snprintf(det, sizeof det, "%.1f dB", (double)limpio_sin);
    di("sin ecualizar, perfecto", limpio_sin > 40.0f, det);
    snprintf(det, sizeof det, "%.1f dB", (double)limpio_con);
    di("con ecualizador, sigue perfecto (>30 dB)", limpio_con > 30.0f, det);

    printf("\n  con ruido encima del eco\n");
    {
        float snr[3] = { 20.0f, 15.0f, 10.0f };
        int k;
        for (k = 0; k < 3; k++) {
            float a = pasada(0, snr[k], 0, 0);
            float b = pasada(2, snr[k], 0, 0);
            printf("      SNR %4.1f dB:  sin %5.1f dB   con %5.1f dB   gana %+5.1f\n",
                   (double)snr[k], (double)a, (double)b, (double)(b - a));
            if (k == 0) {
                float c1 = pasada(1, snr[k], 0, 0);
                snprintf(det, sizeof det, "gana %.1f dB", (double)(b - a));
                di("a SNR 20 dB gana mas de 8 dB de MER", (b - a) > 8.0f, det);
                /*
                 * MEDIDO, NO SUPUESTO: en QPSK el dirigido gana MEDIO dB
                 * sobre el CMA y nada mas (20,8 contra 20,3 a SNR 20, con el
                 * paso bajado a 0,001; con el mismo paso, 20,2 contra 20,3,
                 * o sea nada). Lo que limita a los dos es el ruido de
                 * gradiente, que solo depende del paso, y en una señal de
                 * modulo constante el criterio del CMA ya es el correcto.
                 *
                 * Asi que aqui no se le pide que gane: se le pide que NO
                 * EMPEORE. Donde el dirigido se gana el sitio es en 16-QAM
                 * -los 4800 bps del 4539-, y eso se mide mas abajo, que es
                 * donde el CMA deja de valer porque la señal ya no tiene un
                 * solo modulo.
                 */
                snprintf(det, sizeof det, "CMA %.1f dB, dirigido %.1f dB",
                         (double)c1, (double)b);
                di("el dirigido no empeora al CMA en QPSK", b > c1 - 0.2f, det);
            }
        }
    }

    /*
     * 16-QAM: AQUI ES DONDE EL DIRIGIDO HACE FALTA.
     *
     * Los 4800 bps del STANAG 4539 son 16-QAM, y una 16-QAM tiene TRES
     * modulos distintos. El CMA persigue uno solo, asi que por bien que
     * converja deja la constelacion mal escalada y con las tres coronas
     * encima. Pasar a dirigido es lo unico que la abre, y la diferencia ya
     * no es medio dB.
     */
    printf("\n  16-QAM, que es lo que lleva el 4539 a 4800 bps\n");
    {
        float cma16, dd16;
        g_qam = 1; transmite();
        cma16 = pasada(1, 30.0f, 0, 0);
        g_mu_dd = 0.0008f;
        dd16  = pasada(2, 30.0f, 0, 0);
        snprintf(det, sizeof det, "CMA %.1f dB, dirigido %.1f dB",
                 (double)cma16, (double)dd16);
        di("en 16-QAM el dirigido gana mas de 5 dB", dd16 > cma16 + 5.0f, det);
        g_qam = 0; g_mu_dd = 0.001f; transmite();
    }

    printf("\n  cuanto baja el paso al pasar a dirigido (SNR 20 dB)\n");
    {
        float mus[5] = { 0.004f, 0.002f, 0.001f, 0.0004f, 0.0002f };
        int k;
        for (k = 0; k < 5; k++) {
            g_mu_dd = mus[k];
            printf("      mu %.4f: %.1f dB\n", (double)mus[k],
                   (double)pasada(2, 20.0f, 0, 0));
        }
        g_mu_dd = 0.001f;
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

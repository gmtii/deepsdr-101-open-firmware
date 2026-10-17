/*
 * ======================================================================
 * LA REDUCCION DE RUIDO EN SAM - 11/10/2026
 * ======================================================================
 *
 * *** De un issue: la reduccion de ruido no funcionaba en SAM y se
 * pedia que lo hiciera. ***
 *
 * Y era peor que "no funciona": el boton de Ruido no esta limitado por
 * modo, asi que en SAM se encendia, se quedaba encendido, ponia "activo"
 * en la barra... y demod_am.c no entraba en el bloque. Un control que dice
 * una cosa y hace otra.
 *
 * Este banco mete una portadora de AM modulada con ruido encima por
 * demod_am_process_raw() -la entrada de verdad, muestras crudas de 16 bits
 * I/Q entrelazadas- y mira lo que sale por donde sale el audio.
 *
 * LO QUE SE MIDE es el RUIDO QUE QUEDA, no el nivel: el AGC devuelve el
 * nivel, asi que una atenuacion uniforme no cuenta. Se ajusta el tono de
 * modulacion por correlacion, se resta, y lo que queda es ruido.
 *
 * Y SE MIDE EN LOS DOS MODOS: AM es el testigo. Si el banco dijera que la
 * reduccion funciona en SAM pero no supiera decir que en AM tambien, no
 * estaria midiendo el cambio, estaria midiendo cualquier cosa.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"
#include "nr_ss.h"

#define NBLK     SDR_RX_BLOCK_SAMPLES     /* 256 */
#define F_MOD    1000.0                   /* tono de modulacion */
#define M_PROF   0.6                      /* profundidad */
#define F_PORT   50.0                     /* la portadora, un poco fuera del centro */
#define BLOQUES  900
#define ASIENTO  700

static int fallos;
static void mal(const char *q) { printf("  MAL: %s\n", q); fallos++; }

static double g_fi, g_t, g_fs;
static uint32_t sem;
static double ruidito(void)
{
    sem = sem * 1103515245u + 12345u;
    return (double)((int32_t)(sem >> 8) % 20001 - 10000) / 10000.0;
}

static void bloque(int16_t *raw, double amp, double ruido)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double env = 1.0 + M_PROF * sin(2.0 * M_PI * F_MOD * g_t / g_fs);
        double i = amp * env * cos(g_fi) + amp * ruido * ruidito();
        double q = amp * env * sin(g_fi) + amp * ruido * ruidito();
        g_fi += 2.0 * M_PI * F_PORT / g_fs;
        if (g_fi > M_PI) { g_fi -= 2.0 * M_PI; }
        g_t += 1.0;
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

/* Devuelve por `tono` la amplitud del tono de modulacion recuperado y por
 * `ruido` la parte de la salida que NO es ese tono. */
static void mide(demod_mode_t modo, uint8_t nr, uint16_t mando,
                 double *tono, double *ruido)
{
    static int16_t raw[2 * NBLK];
    int b;
    double sc = 0.0, ss = 0.0, et = 0.0, nm = 0.0;

    demod_am_init();
    demod_am_set_mode(modo);
    nr_ss_set_enabled(nr);
    nr_ss_set_strength(mando);
    cons_captura_tramas((uint32_t)NBLK);
    g_fs = (double)demod_am_get_active_fs_hz();
    g_fi = 0.0; g_t = 0.0; sem = 7777u;

    for (b = 0; b < BLOQUES; b++) {
        const int16_t *sal;
        uint32_t n;
        double t0 = g_t;

        bloque(raw, 6000.0, 0.25);
        demod_am_process_raw(raw);
        if (b < ASIENTO) { continue; }

        sal = cons_audio();
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double y = (double)sal[2U * n];
            double w = 2.0 * M_PI * F_MOD * (t0 + (double)n) / g_fs;
            sc += y * cos(w); ss += y * sin(w);
            et += y * y; nm += 1.0;
        }
    }
    {
        double e_t = 2.0 * (sc * sc + ss * ss) / nm;
        double e_r = et - e_t;
        if (e_r < 0.0) { e_r = 0.0; }
        *tono  = sqrt(e_t / nm);
        *ruido = sqrt(e_r / nm);
    }
}

static void uno(const char *nombre, demod_mode_t modo)
{
    double t_off, r_off, t_on, r_on;
    double caida_ruido, caida_tono;

    mide(modo, 0U, 2000U, &t_off, &r_off);
    mide(modo, 1U, 2000U, &t_on,  &r_on);

    caida_ruido = 20.0 * log10((r_on + 1e-9) / (r_off + 1e-9));
    caida_tono  = 20.0 * log10((t_on + 1e-9) / (t_off + 1e-9));

    printf("  %-5s  tono %7.0f -> %7.0f (%+5.1f dB)   ruido %7.0f -> %7.0f (%+5.1f dB)\n",
           nombre, t_off, t_on, caida_tono, r_off, r_on, caida_ruido);

    if (t_off < 1.0) {
        mal("sin reduccion de ruido no sale audio: el banco no mide nada");
        return;
    }
    /*
     * LO QUE SE EXIGE. Que el ruido baje al menos 6 dB MAS que el tono:
     * asi una atenuacion uniforme -que no sirve de nada, porque detras hay
     * un AGC- no cuela como reduccion de ruido.
     */
    if ((caida_tono - caida_ruido) < 6.0) {
        printf("  MAL: en %s el boton de Ruido no reduce el ruido\n", nombre);
        printf("       (baja el ruido %.1f dB y el tono %.1f dB: solo %.1f de diferencia)\n",
               caida_ruido, caida_tono, caida_tono - caida_ruido);
        fallos++;
    }
}

int main(void)
{
    printf("\n== LA REDUCCION DE RUIDO, EN AM Y EN SAM ==\n\n");
    printf("  portadora de AM al 60%% con un tono de %.0f Hz, ruido encima,\n"
           "  por demod_am_process_raw() de verdad\n\n", F_MOD);

    uno("AM",  DEMOD_MODE_AM);
    uno("SAM", DEMOD_MODE_SAM);

    printf("\n  AM es el testigo: ya funcionaba antes del 11/10/2026.\n");
    printf("  SAM es lo que pedia el issue, y hasta ese dia el boton\n");
    printf("  decia \"activo\" sin tocar el audio.\n");

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

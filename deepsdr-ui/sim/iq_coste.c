/*
 * LO QUE CUESTA LA INTERRUPCION DEL ADC.
 *
 * POR QUE EXISTE ESTE BANCO. El 07/10 meti dos funciones en
 * DMA0_Channel3_IRQHandler, que es la UNICA linea del proyecto por la que
 * pasan TODAS las muestras de TODOS los modos. Y la radio empezo a perder
 * pulsaciones de boton.
 *
 * Tarde TRES ENTREGAS en encontrarlo, y las tres fueron hipotesis mias sobre
 * otras cosas: la interrupcion del 4415 (era un defecto, lo arregle, no era),
 * los repintados del panel (defecto, arreglado, no era), y que se repintara
 * por reloj (defecto, arreglado, no era). Lo encontro el usuario diciendome que
 * mirara el diff.
 *
 * Y la razon de fondo es que NO HABIA NINGUNA MEDIDA de lo que cuesta esa
 * interrupcion. Cada modulo tiene su banco y comprueba que ACIERTA; ninguno
 * comprobaba cuanto cuesta el sitio por el que pasan todos.
 *
 * LO QUE MIDE. 60 segundos de audio del ADC a 96 kHz, en bloques como los de
 * verdad, cronometrando lo que corre dentro del manejador.
 *
 * EL TOPE, Y POR QUE ESE. Medido:
 *
 *     iqbal_bloque()    13,0 ms / 60 s  =  0,022 %   <- el que se queda
 *     iqa_bloque()     123,2 ms / 60 s  =  0,205 %   <- el que la ralentizo
 *
 * El tope es 0,05 %: por encima del que funciona y MUY por debajo del que no.
 * Un tope flojo habria dejado pasar el malo, que es exactamente lo que hace
 * inutil a un banco.
 *
 * Y EL FIR DEL ESPEJO (19/10/2026). Este banco media iqbal_bloque() SIN
 * calibrar nunca, o sea con el FIR apagado, que es la rama barata. Cuando
 * el FIR entro en el camino, su coste -cinco veces el del corrector plano,
 * por encima del tope- no lo vio nadie: el banco puesto justo para vigilar
 * esta interrupcion estaba mirando la otra rama.
 *
 * Asi que ahora se miden LAS DOS, y la de con FIR se calibra de verdad,
 * dandole de comer al panadaptador como en la radio. El reloj del PC no es
 * el del Cortex-M4 -aqui no hay SSAT y la conversion a entero es cara-, asi
 * que el numero que manda sobre lo que de verdad cuesta en la radio lo da
 * tools/isr_coste_check.py, contando instrucciones del codigo ARM. Este
 * sigue siendo util para lo que fue: ver si algo se dispara.
 */
#include "iqbal.h"
#include "iqancho.h"
#include "fft.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-46s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

#define N        256          /* parejas I/Q por bloque, como s_block_samples */
#define FS_ADC   96000.0
#define SEGS     60

static double ahora(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

static int16_t g_b[2 * N];
static unsigned g_rnd = 1u;
static void llena(void)
{
    int i;
    for (i = 0; i < 2 * N; i++) {
        g_rnd = g_rnd * 1103515245u + 12345u;
        g_b[i] = (int16_t)(((g_rnd >> 16) & 0x3FFF) - 8192);
    }
}

/*
 * Una escena con cinco portadoras y desequilibrio CON PENDIENTE, que es lo
 * que hace falta para que iqbal_resuelve() ponga el FIR: con ruido blanco
 * no hay picos que calibrar y el FIR no llega a existir.
 */
static double g_fase[5];
static double g_zq;
static const double k_fp[5] = { -31000.0, -17000.0, 5000.0, 19000.0, 33000.0 };

static void llena_portadoras(void)
{
    int n, c;
    for (n = 0; n < N; n++) {
        double i = 0.0, q = 0.0, io, qo;
        for (c = 0; c < 5; c++) {
            i += 3000.0 * cos(g_fase[c]);
            q += 3000.0 * sin(g_fase[c]);
            g_fase[c] += 2.0 * 3.14159265358979 * k_fp[c] / FS_ADC;
        }
        g_rnd = g_rnd * 1103515245u + 12345u;
        i += (double)(int)((g_rnd >> 20) & 0x1FFu) - 256.0;
        g_rnd = g_rnd * 1103515245u + 12345u;
        q += (double)(int)((g_rnd >> 20) & 0x1FFu) - 256.0;

        io = i;
        qo = 0.93 * q + 0.115 * g_zq;   /* ganancia distinta y un retardo */
        g_zq = q;

        if (io >  32767.0) { io =  32767.0; }
        if (io < -32768.0) { io = -32768.0; }
        if (qo >  32767.0) { qo =  32767.0; }
        if (qo < -32768.0) { qo = -32768.0; }
        g_b[2 * n]     = (int16_t)io;
        g_b[2 * n + 1] = (int16_t)qo;
    }
}

/* Calibra como lo hace la radio: el panadaptador mira y iqbal_resuelve()
 * monta el FIR. Devuelve 1 si lo consiguio. */
static int enciende_fir(void)
{
    static int16_t ii[N], qq[N];
    static float   db[N];
    int ronda, b, n;

    fft_init();
    fft_set_bins_hook(iqbal_calibra);
    for (ronda = 0; ronda < 20 && !iqbal_fir_hay(); ronda++) {
        for (b = 0; b < 600; b++) {
            llena_portadoras();
            iqbal_bloque(g_b, N);
            for (n = 0; n < N; n++) { ii[n] = g_b[2 * n]; qq[n] = g_b[2 * n + 1]; }
            fft_compute_db_iq(ii, qq, db);
        }
        (void)iqbal_resuelve();
    }
    fft_set_bins_hook(0);
    return iqbal_fir_hay() ? 1 : 0;
}

int main(void)
{
    long bloques = (long)(FS_ADC * SEGS / N);
    long k;
    double t_fir = 0.0;
    double t0, t_bal = 0.0, t_anc = 0.0;
    char det[128];

    printf("\nLO QUE CUESTA LA INTERRUPCION DEL ADC\n");
    printf("=====================================\n\n");
    printf("  %d s de audio a 96 kHz, bloques de %d parejas\n\n", SEGS, N);

    iqbal_init(); iqa_init(); iqbal_set_on(1U); iqa_set_on(1U);
    for (k = 0; k < bloques; k++) {
        llena(); t0 = ahora(); iqbal_bloque(g_b, N); t_bal += ahora() - t0;
        llena(); t0 = ahora(); iqa_bloque(g_b, N);   t_anc += ahora() - t0;
    }

    printf("    iqbal_bloque()  (estrecho, el que corre)  %7.1f ms   %.3f %%\n",
           t_bal * 1e3, 100.0 * t_bal / SEGS);
    printf("    iqa_bloque()    (ancho, FUERA del camino) %7.1f ms   %.3f %%\n",
           t_anc * 1e3, 100.0 * t_anc / SEGS);
    printf("\n");

    snprintf(det, sizeof det, "(%.3f %% del tiempo real)", 100.0 * t_bal / SEGS);
    di("lo que corre en la interrupcion cabe de sobra",
       (t_bal / SEGS) < 0.0005, det);

    snprintf(det, sizeof det, "(%.0fx mas caro: por eso no corre)",
             (t_bal > 0.0) ? (t_anc / t_bal) : 0.0);
    di("y el corrector ancho sigue siendo el caro", (t_anc / SEGS) > 0.0005, det);

    /* --- y ahora LA OTRA RAMA, la que estaba sin medir --------------- */
    printf("\n  el mismo camino con el FIR del espejo puesto\n\n");
    if (!enciende_fir()) {
        di("la calibracion llega a poner el FIR", 0, "no se encendio");
    } else {
        di("la calibracion llega a poner el FIR", 1, 0);
        for (k = 0; k < bloques; k++) {
            llena(); t0 = ahora(); iqbal_bloque(g_b, N); t_fir += ahora() - t0;
        }
        printf("    iqbal_bloque()  (estrecho, CON FIR)       %7.1f ms   %.3f %%\n",
               t_fir * 1e3, 100.0 * t_fir / SEGS);

        snprintf(det, sizeof det, "(%.3f %% del tiempo real)", 100.0 * t_fir / SEGS);
        di("con el FIR puesto sigue cabiendo", (t_fir / SEGS) < 0.0015, det);

        snprintf(det, sizeof det, "(x%.1f sobre el plano)",
                 (t_bal > 0.0) ? (t_fir / t_bal) : 0.0);
        di("y no cuesta mas de cuatro veces el plano",
           (t_bal > 0.0) && (t_fir / t_bal) < 4.0, det);
    }

    printf("\n  La interrupcion del ADC es por donde pasan TODAS las muestras de\n");
    printf("  TODOS los modos. Lo que se meta aqui lo paga la radio entera, y\n");
    printf("  se nota en que los botones dejan de responder a la primera.\n");

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

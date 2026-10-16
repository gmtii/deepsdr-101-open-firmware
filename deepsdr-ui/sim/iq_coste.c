/*
 * LO QUE CUESTA LA INTERRUPCION DEL ADC.
 *
 * Pedido por el usuario, 08/10/2026.
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
 */
#include "iqbal.h"
#include "iqancho.h"
#include <stdio.h>
#include <stdint.h>
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

int main(void)
{
    long bloques = (long)(FS_ADC * SEGS / N);
    long k;
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

    printf("\n  La interrupcion del ADC es por donde pasan TODAS las muestras de\n");
    printf("  TODOS los modos. Lo que se meta aqui lo paga la radio entera, y\n");
    printf("  se nota en que los botones dejan de responder a la primera.\n");

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

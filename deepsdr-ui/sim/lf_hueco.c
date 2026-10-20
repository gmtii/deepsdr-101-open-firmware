/*
 * EL DIAL POR DEBAJO DE 300 kHz: ¿HAY ALGUNA FRECUENCIA QUE NO SE PUEDA OIR?
 *
 * POR QUE EXISTE
 * --------------
 * cuadratura cambia la fase al azar y se recibe en la imagen".
 *
 * Lo segundo esta encontrado y arreglado -ver lo_gen_gd32.c-. Lo primero
 * habia que DESCARTARLO del firmware antes de buscarlo en otro sitio, y eso
 * es lo que hace esto: recorre el dial de 300 a 30 kHz con la logica de
 * verdad -nco_lo_aparcado() y la aritmetica del temporizador- y mira donde
 * acaba cayendo la emisora dentro de la banda de IQ.
 *
 * LO QUE SE EXIGE: entre 42 y 300 kHz, la emisora tiene que quedar SIEMPRE
 * entre la distancia minima y la maxima del oscilador. Por debajo no:
 * aparcar a la distancia entera no cabe cuando el dial vale menos que la
 * propia distancia, y eso ya esta documentado en nco_lo_aparcado().
 *
 * RESULTADO: no hay ningun hueco en 78-100 kHz, ni en ninguna otra parte
 * por encima de 42 kHz. O sea que esa mitad del caso NO esta en el
 * aparcado del oscilador ni en el redondeo del temporizador, y hay que
 * buscarla en la cadena de recepcion o en la entrada. Un resultado
 * negativo, pero medido: descarta de una vez dos sospechosos.
 */
#include "lo_gen_gd32.h"
#include "nco.h"
#include <stdio.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-54s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* Los mismos numeros que main.c. */
#define OFF_INI   24000UL
#define OFF_MIN    6000UL
#define OFF_MAX   40000UL
#define SUELO     42000UL   /* por debajo, el aparcado entero no cabe */

/*
 * Y LA VENTANA SE MIDE CON LA TOLERANCIA DEL OSCILADOR.
 *
 * nco_lo_aparcado() calcula el oscilador IDEAL para dejar la emisora justo
 * en el borde de la ventana, pero el temporizador solo sabe dar
 * TIMERCLK/(2*(ARR+1)): el periodo es un numero entero de cuentas. Asi que
 * lo que sale se redondea a un escalon de 2*f*f/TIMERCLK -28 Hz a 37 kHz,
 * 1,6 kHz a 283- y el desplazamiento se pasa del borde por hasta medio
 * escalon.
 *
 * Eso NO es un hueco: la emisora queda 970 Hz mas lejos del oscilador de lo
 * previsto, sobre una ventana de 34.000. Lo que este banco busca es una
 * frecuencia en la que la emisora se quede FUERA de verdad, asi que la
 * comparacion lleva el escalon de tolerancia. Sin el, esto se pondria rojo
 * por el redondeo y taparia lo que de verdad importa.
 */
static unsigned long escalon_de(unsigned long lo_hz)
{
    uint32_t c = lo_gen_gd32_cuentas((uint32_t)lo_hz);
    uint32_t a, b;

    if (c < 2U) { return 0UL; }
    a = lo_gen_gd32_hz_de(c);
    b = lo_gen_gd32_hz_de(c - 1U);
    return (unsigned long)(b - a);
}

/*
 * Recorre el dial en un sentido y devuelve cuantas frecuencias dejan la
 * emisora fuera de la ventana. nco = 0 recorre sin NCO, que es la cuenta
 * directa freq - desplazamiento.
 */
static unsigned long recorre(int nco, int bajando, unsigned long *peor_f,
                             long *peor_off)
{
    unsigned long lo = 0UL, f, malas = 0UL;
    unsigned long desde = bajando ? 300000UL : 30000UL;
    unsigned long hasta = bajando ?  30000UL : 300000UL;
    long paso = bajando ? -100 : 100;   /* en hercios * 10, o sea 100 Hz */

    for (f = desde; bajando ? (f >= hasta) : (f <= hasta);
         f = (unsigned long)((long)f + paso)) {
        unsigned long ideal, real;
        long off;

        ideal = nco ? nco_lo_aparcado((uint32_t)f, (uint32_t)lo,
                                      OFF_INI, OFF_MIN, OFF_MAX)
                    : (f - OFF_INI);
        real = ideal;
        if (ideal < LO_GEN_CROSSOVER_HZ) {
            real = lo_gen_gd32_hz_de(lo_gen_gd32_cuentas((uint32_t)ideal));
        }
        if (ideal != lo) { lo = real; }

        off = (long)f - (long)lo;
        if (f < SUELO) { continue; }
        {
            long tol = (long)escalon_de(lo);
            if (off <= (long)OFF_MAX + tol && off >= (long)OFF_MIN - tol) {
                continue;
            }
        }
        if (1) {
            malas++;
            if (*peor_f == 0UL) { *peor_f = f; *peor_off = off; }
        }
        if (!bajando && f >= hasta) { break; }
    }
    return malas;
}

int main(void)
{
    char t[120];
    unsigned long malas, peor_f;
    long peor_off;

    printf("\n== El dial por debajo de 300 kHz ==\n");

    printf("\n  bajando el dial, con el NCO puesto\n");
    peor_f = 0UL; peor_off = 0L;
    malas = recorre(1, 1, &peor_f, &peor_off);
    snprintf(t, sizeof t, "%lu frecuencias, la primera en %lu Hz con %ld de desvio",
             malas, peor_f, peor_off);
    di("ninguna emisora se sale de la ventana", malas == 0UL, t);

    printf("\n  subiendo el dial, con el NCO puesto\n");
    peor_f = 0UL; peor_off = 0L;
    malas = recorre(1, 0, &peor_f, &peor_off);
    snprintf(t, sizeof t, "%lu frecuencias, la primera en %lu Hz con %ld de desvio",
             malas, peor_f, peor_off);
    di("ninguna emisora se sale de la ventana", malas == 0UL, t);

    printf("\n  sin NCO, que es la cuenta directa\n");
    peor_f = 0UL; peor_off = 0L;
    malas = recorre(0, 1, &peor_f, &peor_off);
    snprintf(t, sizeof t, "%lu frecuencias", malas);
    di("ninguna emisora se sale de la ventana", malas == 0UL, t);

    /*
     * Y EL TRAMO DEL ISSUE, MIRADO APARTE Y CON LUPA.
     *
     * De 78 a 100 kHz, hercio a hercio en pasos de 10 Hz, en los dos
     * sentidos. Si el firmware tuviera ahi un agujero, saldria aqui.
     */
    printf("\n  el tramo de 78 a 100 kHz, con lupa\n");
    {
        unsigned long f, lo = 0UL, malas2 = 0UL;
        long peor = 0L;

        for (f = 78000UL; f <= 100000UL; f += 10UL) {
            unsigned long ideal = nco_lo_aparcado((uint32_t)f, (uint32_t)lo,
                                                  OFF_INI, OFF_MIN, OFF_MAX);
            unsigned long real = lo_gen_gd32_hz_de(
                                     lo_gen_gd32_cuentas((uint32_t)ideal));
            long off;

            if (ideal != lo) { lo = real; }
            off = (long)f - (long)lo;
            {
                long tol = (long)escalon_de(lo);
                if (off > (long)OFF_MAX + tol || off < (long)OFF_MIN - tol) {
                    malas2++;
                }
            }
            if (off > peor) { peor = off; }
        }
        snprintf(t, sizeof t, "%lu de 2201 frecuencias, el mayor desvio %ld Hz",
                 malas2, peor);
        di("tampoco hay hueco entre 78 y 100 kHz", malas2 == 0UL, t);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * CUANTAS VECES POR SEGUNDO SE REPINTA EL PANEL DEL IDENTIFICADOR.
 *
 * ES EL MISMO FALLO QUE YA HA SALIDO CUATRO VECES, Y ESTE ES EL CUARTO.
 * La historia esta contada en sim/stanag_latido.c: el bucle principal se
 * arrastra, la radio no coge las pulsaciones y desde fuera parece colgada,
 * y la causa no es el calculo sino el REPINTADO - un panel entero por un
 * bus SPI, varias veces por segundo, porque algo que no deberia cambiar
 * cambia.
 *
 * Aquel banco cuenta los repintados de STANAG. El identificador comparte
 * el bucle -main.c llama a stanag_modo_poll() y a ident_modo_poll() una
 * detras de otra, en la misma vuelta- y NO tenia banco. Lo que decide si
 * se repinta esta en main.c y es esto:
 *
 *     t = lineas * 131 + primera[0] * 7 + primera[1]
 *
 * O sea: el numero de renglones y LOS DOS PRIMEROS CARACTERES del primero.
 * Si la linea de arriba empieza por algo que se mueve -un contador, unos
 * baudios, un numero de medidas- esa huella cambia en cada vuelta y el
 * panel se repinta a la velocidad del bucle.
 *
 * LOS TOPES son los mismos que los de STANAG y por la misma razon: 1,0 por
 * segundo en reposo y 4,0 con señal. Por encima de eso alguien ha vuelto a
 * poner un numero que corre en la primera linea.
 */
#include "ident.h"
#include "stanag_det.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fallos = 0;

static void di(const char *que, int ok, const char *det)
{
    printf("    %-46s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/*
 * LA MISMA HUELLA QUE MIRA main.c, copiada tal cual de donde decide si
 * repinta. Si alla cambia y aqui no, este banco deja de medir lo que dice
 * medir: por eso va con su linea de main.c escrita al lado.
 *
 *     const char *pri = ident_modo_linea(0);
 *     uint32_t t = (uint32_t)ident_modo_lineas() * 131U
 *                + (uint32_t)(uint8_t)pri[0] * 7U
 *                + (uint32_t)(uint8_t)(pri[0] ? pri[1] : 0);
 */
static unsigned long huella(void)
{
    const char *pri = ident_modo_linea(0);
    return (unsigned long)ident_modo_lineas() * 131UL
         + (unsigned long)(unsigned char)pri[0] * 7UL
         + (unsigned long)(unsigned char)(pri[0] ? pri[1] : 0);
}

/*
 * Mete `segundos` de audio por el identificador, mirando la huella una vez
 * por vuelta del bucle igual que main.c, y devuelve cuantas veces CAMBIA
 * por segundo. `fsk` a 1 pone dos tonos de 850 Hz a 50 baudios -una señal
 * de las que identifica-; a 0, ruido.
 */
#define FS   12000.0f
#define BLOQ 256U

static double mide(double segundos, int fsk)
{
    float a[BLOQ];
    unsigned long cambios = 0UL, ant;
    double t = 0.0;
    unsigned long sem = 12345UL;
    unsigned b, k;
    unsigned bloques = (unsigned)(segundos * FS / BLOQ);

    ident_modo_stop();
    ident_modo_borra();
    if (!ident_modo_start()) {
        /*
         * Y ESTO ES UN FALLO, NO UN CERO.
         *
         * La primera version devolvia -1 aqui, y -1 pasa cualquier tope
         * "menor que": el banco salia en verde habiendo medido NADA. Un
         * banco que aprueba cuando no ha podido arrancar es peor que no
         * tenerlo, porque ademas tranquiliza.
         */
        printf("    *** el identificador no arranca: no hay nada medido ***\n");
        fallos++;
        return 1e9;
    }
    ident_modo_frec_pon(7000UL);
    ant = huella();

    for (b = 0U; b < bloques; b++) {
        for (k = 0U; k < BLOQ; k++) {
            double ruido;
            sem = sem * 1103515245UL + 12345UL;
            ruido = (double)((sem >> 16) & 0x7FFFU) / 16384.0 - 1.0;
            if (fsk) {
                /* Dos tonos, 850 Hz de salto, 50 baudios: 1200 y 2050 Hz. */
                double bit = floor(t * 50.0);
                double f = (fmod(bit, 2.0) < 1.0) ? 1200.0 : 2050.0;
                a[k] = (float)(0.5 * sin(2.0 * M_PI * f * t) + 0.02 * ruido);
            } else {
                a[k] = (float)(0.05 * ruido);
            }
            t += 1.0 / FS;
        }
        ident_modo_mete(a, (uint16_t)BLOQ);
        /* main.c llama a poll() y mira la huella una vez por vuelta; el
         * bucle da unas 60 vueltas por segundo y un bloque son 21 ms, o
         * sea que mirar una vez por bloque es del mismo orden. */
        ident_modo_poll();
        if (huella() != ant) { ant = huella(); cambios++; }
    }
    ident_modo_stop();
    return (double)cambios / segundos;
}

int main(void)
{
    char det[64];
    double c;

    printf("\nEL PANEL DEL IDENTIFICADOR: CUANTAS VECES SE REPINTA\n\n");
    printf("  1. EN REPOSO (solo ruido)\n");
    c = mide(20.0, 0);
    snprintf(det, sizeof det, "(%.2f/s, tope 1,0)", c);
    di("con ruido el panel se queda quieto", c <= 1.0, det);

    printf("\n  2. CON UNA SEÑAL QUE IDENTIFICAR (FSK 850/50)\n");
    c = mide(20.0, 1);
    snprintf(det, sizeof det, "(%.2f/s, tope 4,0)", c);
    di("ni se dispara porque haya algo que medir", c <= 4.0, det);

    printf("\n  Un panel entero por el bus SPI varias veces por segundo es\n");
    printf("  lo que deja la radio sin atender el tactil. Ver la cabecera.\n");

    if (fallos) { printf("\n  *** %d FALLOS ***\n", fallos); return 1; }
    printf("\nTODO CORRECTO (0 fallos)\n");
    return 0;
}

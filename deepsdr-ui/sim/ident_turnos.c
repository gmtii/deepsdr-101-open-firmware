/*
 * DOS SEÑALES TURNANDOSE EN LA MISMA FRECUENCIA.
 *
 * Pedido por el usuario.
 *
 * En 5339 hay dos cosas: un STANAG 4481 FSK que esta casi todo el rato y
 * una red ALE que solo emite cuando alguien llama -sondeos de dos o tres
 * segundos separados por minutos-. La pantalla enseñaba una sola, y hasta
 * la V3.56 enseñaba la que hubiera ganado la ULTIMA ventana, que es por lo
 * que "iba variando entre lo que es".
 *
 * La mediana de la V3.56 arreglo el baile, pero al arreglarlo se come la
 * rafaga: tres segundos no mueven una mediana de cuatro. De ahi la cuenta
 * aparte de ident.c, y de ahi este banco.
 *
 * SE MIDEN LAS DOS MITADES, Y LA SEGUNDA IMPORTA MAS QUE LA PRIMERA:
 *
 *   1. CON RAFAGAS, que salga el renglon "y a ratos: ...". Si no sale, la
 *      pantalla sigue contando la mitad de lo que pasa.
 *
 *   2. SIN RAFAGAS, que NO salga. Esto es lo que de verdad hay que vigilar:
 *      un aviso de segunda señal que se enciende solo con ruido es peor
 *      que no tenerlo, porque manda al usuario a buscar algo que no existe.
 *      Es la misma piedra del parpadeo del nombre, una fila mas abajo.
 *
 * El audio se fabrica aqui -no hace falta grabacion- con los numeros
 * medidos en antena el 06/10/2026 sobre 5339: tonos 508 y 1348 Hz, salto
 * 840, 75 baudios. El ALE, con los de la norma: 8-FSK de 750 a 2500 Hz
 * cada 250, 125 baudios.
 */
#include "ident.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS      12000.0
#define BLOQUE  256U

static int fallos;

static uint32_t sem = 0x12345678u;
static uint32_t dado(void) { sem = sem * 1103515245u + 12345u; return sem >> 16; }

/*
 * Un generador de tonos con fase continua: el salto de un tono a otro sin
 * saltar la fase. Partir la fase ensucia el espectro con bandas laterales
 * que no estan en el aire, y este banco mide justamente anchos.
 */
/*
 * EL GENERADOR LLEVA LA CUENTA DEL SIMBOLO, NO SOLO DE LA FASE.
 *
 * La primera version empezaba simbolo nuevo en cada bloque de 256
 * muestras. A 75 baudios un simbolo son 160 muestras, asi que cada bloque
 * salia partido en uno de 160 y otro de 96, y el medidor -con razon- leia
 * 122 baudios en vez de 75. El banco decia que el Ident fallaba y el que
 * fallaba era el banco.
 *
 * Asi que el simbolo cruza la frontera del bloque, igual que en el aire.
 */
typedef struct {
    double   fase;
    double   hz;        /* el tono que suena ahora */
    unsigned restan;    /* muestras que le quedan a este simbolo */
} tono_t;

static void mete(tono_t *t, float *d, unsigned n, double amp)
{
    unsigned i;

    for (i = 0U; i < n; i++) {
        d[i] += (float)(amp * sin(t->fase));
        t->fase += 2.0 * M_PI * t->hz / FS;
        if (t->fase > 2.0 * M_PI) { t->fase -= 2.0 * M_PI; }
    }
}

/* FSK de dos tonos, la del 4481. */
static void fsk(tono_t *t, float *d, unsigned n, double lo, double hi, double bd)
{
    unsigned paso = (unsigned)(FS / bd + 0.5), i = 0U;

    while (i < n) {
        unsigned k;
        if (t->restan == 0U) {
            t->restan = paso;
            t->hz = (dado() & 1U) ? hi : lo;
        }
        k = (n - i < t->restan) ? (n - i) : t->restan;
        mete(t, &d[i], k, 0.35);
        t->restan -= k;
        i += k;
    }
}

/* 8-FSK de ALE: 750..2500 cada 250 Hz, 125 Bd. */
static void ale(tono_t *t, float *d, unsigned n)
{
    unsigned paso = (unsigned)(FS / 125.0 + 0.5), i = 0U;

    while (i < n) {
        unsigned k;
        if (t->restan == 0U) {
            t->restan = paso;
            t->hz = 750.0 + 250.0 * (double)(dado() % 8U);
        }
        k = (n - i < t->restan) ? (n - i) : t->restan;
        mete(t, &d[i], k, 0.35);
        t->restan -= k;
        i += k;
    }
}

static void ruido(float *d, unsigned n, double amp)
{
    unsigned i;
    for (i = 0U; i < n; i++) {
        d[i] += (float)(amp * (((double)(dado() % 2000U) / 1000.0) - 1.0));
    }
}

/*
 * Una tirada entera. `con_ale` dice si se cuelan rafagas. Devuelve 1 si la
 * pantalla acabo diciendo "y a ratos:".
 */
static int tirada(int con_ale, int ensena)
{
    unsigned seg, total_s = 180U, paso = 0U;
    tono_t ta = {0.0, 0.0, 0U}, tb = {0.0, 0.0, 0U};
    int hay = 0;

    if (!ident_modo_start()) { printf("    no arranca el modo\n"); fallos++; return 0; }

    for (seg = 0U; seg < total_s; seg++) {
        /*
         * Una rafaga de ALE de tres segundos cada treinta, y mientras dura
         * el 4481 se calla: en el aire se turnan, no se suman.
         */
        int es_ale = con_ale && ((seg % 30U) < 3U) && (seg >= 20U);
        unsigned b;

        for (b = 0U; b < (unsigned)(FS / BLOQUE); b++) {
            float d[BLOQUE];
            memset(d, 0, sizeof d);
            if (es_ale) { ale(&tb, d, BLOQUE); }
            else        { fsk(&ta, d, BLOQUE, 508.0, 1348.0, 75.0); }
            ruido(d, BLOQUE, 0.02);
            ident_modo_mete(d, (uint16_t)BLOQUE);
            if (++paso >= 4U) { paso = 0U; ident_modo_poll(); }
        }
    }
    ident_modo_poll();

    {
        uint8_t nl = ident_modo_lineas(), i;
        if (ensena) { printf("\n"); }
        for (i = 0U; i < nl; i++) {
            const char *s = ident_modo_linea(i);
            if (!s || !*s) { continue; }
            if (ensena) { printf("      %s\n", s); }
            if (strncmp(s, "y a ratos:", 10) == 0) { hay = 1; }
        }
        if (ensena) { printf("\n"); }
    }

    ident_modo_stop();
    return hay;
}

static void di(const char *que, int bien)
{
    printf("    %-52s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

int main(void)
{
    int con, sin;

    printf("\n  SEÑALES QUE SE TURNAN EN LA MISMA FRECUENCIA\n");

    printf("\n  1. 4481 todo el rato, con rafagas de ALE cada 30 s\n");
    con = tirada(1, 1);
    di("sale el renglon \"y a ratos:\"", con == 1);

    printf("  2. el mismo 4481, SIN rafagas\n");
    sin = tirada(0, 1);
    di("NO sale el renglon (no inventarse una segunda señal)", sin == 0);

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

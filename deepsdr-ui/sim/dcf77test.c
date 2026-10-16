/*
 * Banco del decodificador DCF77 (User/dcf77.c).
 *
 * Aqui no se puede "probar escuchando": si la radio no sincroniza no se
 * sabria si es que no hay señal o es que el decodificador esta mal, y esas
 * dos cosas se arreglan en sitios muy distintos. Asi que se le da señal
 * sintetica de la que se conoce la respuesta.
 *
 * Lo que se mide:
 *   1. con señal limpia saca la hora exacta, y cuanto tarda
 *   2. aguanta ruido, y hasta donde
 *   3. aguanta que se caiga la señal a ratos (desvanecimiento)
 *   4. un bit corrompido NO se cuela
 *   5. y lo mas importante de todo: SIN SEÑAL NO SE INVENTA UNA HORA
 *
 * El (5) es el unico fallo de verdad grave que puede tener esto. Que no
 * sincronice es un fastidio; que sincronice mal y te deje el reloj en una
 * hora falsa diciendo que ha ido bien, es peor que no tenerlo.
 */
#include "dcf77.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS      375.0f   /* muestras/s: una por bloque de 256 a 96 kHz */
#define ALTO    1000.0f
#define BAJO     150.0f  /* el 15% de la portadora, como el emisor real */

static int g_minuto = 0;
static unsigned long s_rnd = 987654321UL;
static double uni(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return (double)((s_rnd >> 16) & 0x7FFFUL) / 32767.0;
}
static double gauss(void)
{
    double u1 = uni(), u2 = uni();
    if (u1 < 1e-9) { u1 = 1e-9; }
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

/* ---- generador de la trama, a partir de una hora dada ------------------ */

static void pon_bcd(uint8_t *b, int ini, int n, int v)
{
    static const int peso[8] = { 1, 2, 4, 8, 10, 20, 40, 80 };
    int k;
    for (k = n - 1; k >= 0; k--) {
        if (v >= peso[k]) { b[ini + k] = 1; v -= peso[k]; } else { b[ini + k] = 0; }
    }
}
static void pon_paridad(uint8_t *b, int ini, int fin)
{
    int k, n = 0;
    for (k = ini; k < fin; k++) { n += b[k]; }
    b[fin] = (uint8_t)(n & 1);   /* par: el bit de paridad completa a par */
}

/* Construye los 59 bits de una trama para h:m del dia d/mes/anno. */
static void trama(uint8_t *b, int h, int m, int d, int mes, int anno, int ds, int verano)
{
    int k;
    for (k = 0; k < 59; k++) { b[k] = 0; }
    b[0]  = 0;
    b[17] = (uint8_t)(verano ? 1 : 0);
    b[18] = (uint8_t)(verano ? 0 : 1);
    b[20] = 1;
    pon_bcd(b, 21, 7, m);   pon_paridad(b, 21, 28);
    pon_bcd(b, 29, 6, h);   pon_paridad(b, 29, 35);
    pon_bcd(b, 36, 6, d);
    pon_bcd(b, 42, 3, ds);
    pon_bcd(b, 45, 5, mes);
    pon_bcd(b, 50, 8, anno);
    pon_paridad(b, 36, 58);
}

/*
 * Emite la envolvente de un minuto entero. bit_roto >= 0 invierte ese bit,
 * ruido es sigma del gaussiano, desvan!=0 apaga la portadora unos segundos
 * a mitad del minuto, y sin_senal!=0 emite SOLO ruido (nada de portadora).
 */
static void emite_minuto(const uint8_t *b, double ruido, int bit_roto,
                         int desvan, int sin_senal)
{
    int seg, k;
    for (seg = 0; seg < 60; seg++) {
        int n_seg = (int)(FS + 0.5f);
        int bajo_n;

        if (seg == 59) { bajo_n = 0; }   /* el hueco que marca el minuto */
        else {
            int bit = b[seg];
            if (seg == bit_roto) { bit = !bit; }
            bajo_n = (int)(FS * (bit ? 0.2 : 0.1) + 0.5);
        }

        for (k = 0; k < n_seg; k++) {
            double v;
            if (sin_senal) {
                v = ruido * fabs(gauss()) * 2.0;   /* solo ruido, sin portadora */
            } else if (desvan && g_minuto < 2 && seg >= 25 && seg < 31) {
                v = ruido * fabs(gauss()) * 2.0;   /* seis segundos sin señal */
            } else {
                v = (k < bajo_n) ? BAJO : ALTO;
                v += ruido * gauss();
                if (v < 0.0) { v = 0.0; }
            }
            dcf77_feed((float)v);
        }
    }
}

typedef struct { int ok; int minutos; dcf_info_t inf; } res_t;

/* Emite `minutos` minutos consecutivos empezando en h:m y devuelve cuando
 * sincronice (o al agotarse). */
static res_t corre(int h, int m, double ruido, int bit_roto, int desvan,
                   int sin_senal, int minutos)
{
    uint8_t b[59];
    res_t r;
    int i, hh = h, mm = m;

    memset(&r, 0, sizeof r);
    s_rnd = 987654321UL;
    dcf77_start(HORA_DCF77, FS);

    for (i = 0; i < minutos; i++) {
        /* La trama emitida durante un minuto describe el minuto SIGUIENTE. */
        int nh = hh, nm = mm + 1;
        if (nm > 59) { nm = 0; nh = (nh + 1) % 24; }
        trama(b, nh, nm, 15, 6, 26, 1, 1);
        g_minuto = i;
        emite_minuto(b, ruido, bit_roto, desvan, sin_senal);
        hh = nh; mm = nm;

        dcf77_info(&r.inf);
        r.minutos = i + 1;
        if (r.inf.estado == DCF_LISTO) { r.ok = 1; break; }
    }
    dcf77_stop();
    return r;
}

static int fallos = 0;
static void comprueba(const char *que, int ok)
{
    printf("    %-52s %s\n", que, ok ? "OK" : "NO");
    if (!ok) { fallos++; }
}

int main(void)
{
    res_t r;

    printf("DCF77: envolvente a %.0f muestras/s, portadora %.0f, marca %.0f (15%%)\n\n",
           FS, ALTO, BAJO);

    printf("  1. SEÑAL LIMPIA\n");
    r = corre(20, 30, 0.0, -1, 0, 0, 6);
    printf("    sincroniza en %d minutos: %02u:%02u  %02u/%02u/%02u  %s\n",
           r.minutos, r.inf.hora, r.inf.minuto, r.inf.dia, r.inf.mes, r.inf.anno,
           r.inf.verano ? "verano" : "invierno");
    comprueba("sincroniza", r.ok);
    /* 20:33 y no 20:32: la PRIMERA trama siempre se pierde, porque al
       arrancar el seguidor de pico no conoce aun el nivel de portadora y se
       come la marca del primer segundo. Eso pasa igual en la radio -nunca
       empiezas a escuchar justo en un cambio de minuto-, asi que el caso
       normal es perder la primera y sincronizar con la segunda y la
       tercera. */
    comprueba("la hora es la correcta (20:33)",
              r.inf.hora == 20U && r.inf.minuto == 33U);
    comprueba("la fecha es la correcta (15/06/26)",
              r.inf.dia == 15U && r.inf.mes == 6U && r.inf.anno == 26U);
    comprueba("tarda 4 minutos o menos", r.minutos <= 4);
    comprueba("solo descarta la primera trama, la del arranque", r.inf.tramas_malas == 1U);

    printf("\n  2. CON RUIDO - hasta donde aguanta\n");
    {
        double niveles[] = { 60.0, 150.0, 300.0, 500.0, 800.0, 1200.0 };
        int i, ultimo_ok = -1;
        for (i = 0; i < 6; i++) {
            r = corre(9, 15, niveles[i], -1, 0, 0, 8);
            printf("    ruido sigma %5.0f (%4.0f%% de la marca)   %s",
                   niveles[i], 100.0 * niveles[i] / (ALTO - BAJO),
                   r.ok ? "sincroniza" : "no sincroniza");
            if (r.ok) { printf(" en %d min", r.minutos); ultimo_ok = i; }
            printf("   (tramas buenas %u, malas %u)\n", r.inf.tramas_ok, r.inf.tramas_malas);
            if (r.ok) {
                comprueba("      ...y la hora es la correcta",
                          r.inf.hora == 9U && r.inf.minuto == 18U);
            }
        }
        comprueba("aguanta ruido de al menos el 35% de la marca", ultimo_ok >= 2);
    }

    /* Con un hueco de 6 s en CADA minuto no hay nada que hacer: faltan seis
     * bits de cada trama y ninguna se puede completar. Eso no es un fallo
     * del decodificador, es que la señal no esta. Lo que si hay que exigir
     * es que se RECUPERE cuando la señal vuelve, en vez de quedarse
     * atontado: por eso el desvanecimiento solo afecta a los dos primeros
     * minutos. */
    printf("\n  3. CON DESVANECIMIENTO (6 s sin señal los dos primeros minutos)\n");
    r = corre(3, 45, 30.0, -1, 1, 0, 10);
    printf("    %s tras %d minutos (buenas %u, malas %u)\n",
           r.ok ? "sincroniza" : "NO sincroniza", r.minutos,
           r.inf.tramas_ok, r.inf.tramas_malas);
    comprueba("se recupera y sincroniza cuando vuelve la señal", r.ok);

    printf("\n  4. CON UN BIT CORROMPIDO EN CADA TRAMA\n");
    {
        int bits[] = { 21, 29, 36, 50, 20 };
        int i;
        for (i = 0; i < 5; i++) {
            r = corre(11, 0, 0.0, bits[i], 0, 0, 5);
            printf("    bit %2d invertido -> %s (buenas %u, malas %u)\n",
                   bits[i], r.ok ? "*** SE HA COLADO ***" : "rechazada",
                   r.inf.tramas_ok, r.inf.tramas_malas);
            comprueba("      no se cuela", !r.ok);
        }
    }

    printf("\n  5. SIN SEÑAL - LO IMPORTANTE\n");
    {
        double niveles[] = { 50.0, 200.0, 1000.0 };
        int i;
        for (i = 0; i < 3; i++) {
            r = corre(0, 0, niveles[i], -1, 0, 1, 15);
            printf("    solo ruido sigma %6.0f, 15 minutos -> %s (buenas %u, malas %u)\n",
                   niveles[i], r.ok ? "*** SE INVENTA UNA HORA ***" : "no sincroniza",
                   r.inf.tramas_ok, r.inf.tramas_malas);
            comprueba("      NO se inventa una hora", !r.ok);
        }
    }

    printf("\n  %s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

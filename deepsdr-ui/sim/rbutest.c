/*
 * BANCO DE RBU (User/hora_rbu.c). 23/09/2026.
 *
 * QUE GENERA. La FASE de la portadora de RBU tal y como la vera el
 * demodulador, elemento a elemento de 100 ms:
 *
 *    0..10 ms   nada
 *   10..90 ms   un tono de 100 Hz (bit 0) o de 312,5 Hz (bit 1), con indice
 *               de modulacion 0,698 radianes, que es el de verdad
 *   90..95 ms   nada
 *   95..100 ms  portadora apagada: la fase es puro azar
 *
 * Encima va la DERIVA del oscilador local, que aqui no es un detalle: a
 * 66 kHz el temporizador del micro da pasos de unos 22 Hz, y ademas el
 * cuarzo se mueve. Una deriva es una rampa en la fase, y una rampa mal
 * tratada tapa los tonos. Por eso el banco barre la deriva de 1 a 500 Hz.
 *
 * QUE COMPRUEBA:
 *   1. Que con señal limpia da la hora de pared española EXACTA, partiendo
 *      de hora de Moscu y pasando por UTC. Un error aqui son tres horas, que
 *      se veria; pero tambien podria ser una sola, que no.
 *   2. Que encuentra la alineacion de los elementos sola, sin ayuda: el
 *      decodificador no sabe por donde empieza un elemento y tiene que
 *      elegir entre diez posibilidades.
 *   3. Que aguanta deriva de oscilador.
 *   4. Cuanto ruido de fase aguanta.
 *   5. Que con SOLO ruido no se inventa una hora nunca.
 *   6. Que una trama con un bit cambiado la cazan las paridades.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "dcf77.h"
#include "hora_comun.h"   /* hora_huso_pon(): este banco pone el huso a mano */
#include "hora_rbu.h"

#ifndef FS
#define FS 3000.0f
#endif

#define INDICE 0.698f

static unsigned long s_rnd = 1UL;
static float ruido(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return ((float)((s_rnd >> 8) & 0xFFFFUL) / 32768.0f) - 1.0f;
}

static float s_t = 0.0f;        /* tiempo continuo, para los tonos */
static float s_fase_deriva = 0.0f;
static float s_deriva_hz = 0.0f;
static float s_sigma = 0.0f;

/* Un elemento de 100 ms. bit<0 significa "sin tono" (no lo usa RBU, pero
 * sirve para medir que pasa si se pierde uno). */
static void elemento(int bit)
{
    int n, N = (int)(FS / 10.0f);
    for (n = 0; n < N; n++) {
        float ms = (float)n * 1000.0f / FS;
        float f;

        s_fase_deriva += 6.2831853f * s_deriva_hz / FS;
        if (s_fase_deriva >  1000.0f) { s_fase_deriva -= 1000.0f; }

        if (ms >= 95.0f) {
            f = 3.14159f * ruido();          /* portadora apagada */
        } else if (ms >= 10.0f && ms < 90.0f && bit >= 0) {
            float ft = (bit ? 312.5f : 100.0f);
            f = s_fase_deriva + INDICE * sinf(6.2831853f * ft * s_t);
        } else {
            f = s_fase_deriva;
        }
        f += s_sigma * ruido();
        /* La fase que entrega el demodulador viene de un atan2: siempre
         * envuelta en (-pi,pi]. El decodificador tiene que desenrollarla el
         * solo, asi que aqui se envuelve a proposito. */
        while (f >  3.14159265f) { f -= 6.28318531f; }
        while (f < -3.14159265f) { f += 6.28318531f; }
        rbu_feed(f);
        s_t += 1.0f / FS;
        if (s_t > 80.0f) { s_t -= 80.0f; }   /* 80 s: entero para 100 y 312,5 Hz */
    }
}

/* ----------------------------------------------------------------------- */
/* Construye los 60+60 bits de un minuto a partir de la hora de Moscu.      */
static void arma(int minuto, int hora, int dia, int mes, int anno, int dsem,
                 int dut, unsigned char *d1, unsigned char *d2)
{
    static const int min_s[7] = { 53,54,55,56,57,58,59 }, min_p[7] = { 40,20,10,8,4,2,1 };
    static const int hor_s[6] = { 47,48,49,50,51,52 },    hor_p[6] = { 20,10,8,4,2,1 };
    static const int dia_s[6] = { 41,42,43,44,45,46 },    dia_p[6] = { 20,10,8,4,2,1 };
    static const int sem_s[3] = { 38,39,40 },             sem_p[3] = { 4,2,1 };
    static const int mes_s[5] = { 33,34,35,36,37 },       mes_p[5] = { 10,8,4,2,1 };
    static const int ann_s[8] = { 25,26,27,28,29,30,31,32 }, ann_p[8] = { 80,40,20,10,8,4,2,1 };
    static const int dut_s[5] = { 19,20,21,22,23 },       dut_p[5] = { 10,8,4,2,1 };
    int k, v;

    for (k = 0; k < 60; k++) { d1[k] = 0; d2[k] = 0; }
    d1[0] = 1; d2[0] = 1;

#define PON(S,P,N,VAL) do { int _v=(VAL),_i; for(_i=0;_i<(N);_i++){ if(_v>=(P)[_i]){d1[(S)[_i]]=1;_v-=(P)[_i];} } } while(0)
    PON(min_s, min_p, 7, minuto);
    PON(hor_s, hor_p, 6, hora);
    PON(dia_s, dia_p, 6, dia);
    PON(sem_s, sem_p, 3, dsem);
    PON(mes_s, mes_p, 5, mes);
    PON(ann_s, ann_p, 8, anno);
    PON(dut_s, dut_p, 5, dut >= 0 ? dut : -dut);
#undef PON
    d1[18] = (unsigned char)(dut < 0);

    /* La fecha juliana truncada: no se usa, pero su paridad si se comprueba,
     * asi que hay que emitir algo coherente. */
    v = 6100;
    for (k = 0; k < 16; k++) {
        static const int pe[16] = {8000,4000,2000,1000,800,400,200,100,80,40,20,10,8,4,2,1};
        if (v >= pe[k]) { d2[18 + k] = 1; v -= pe[k]; }
    }

    /* Las ocho paridades PARES. */
    {
        int p;
#define PAR1(A,B) do { p=0; for(k=(A);k<=(B);k++) p^=d1[k]; } while(0)
#define PAR2(A,B) do { p=0; for(k=(A);k<=(B);k++) p^=d2[k]; } while(0)
        PAR1(18,24); d2[53] = (unsigned char)p;
        PAR1(25,32); d2[54] = (unsigned char)p;
        PAR1(33,40); d2[55] = (unsigned char)p;
        PAR1(41,46); d2[56] = (unsigned char)p;
        PAR1(47,52); d2[57] = (unsigned char)p;
        PAR1(53,59); d2[58] = (unsigned char)p;
        PAR2(18,25); d2[49] = (unsigned char)p;
        PAR2(26,33); d2[50] = (unsigned char)p;
#undef PAR1
#undef PAR2
    }
}

/* Emite un minuto entero (600 elementos) con esos bits. */
static void minuto(const unsigned char *d1, const unsigned char *d2)
{
    int s, e;
    for (s = 0; s < 60; s++) {
        for (e = 0; e < 10; e++) {
            int b;
            if (e == 0)      { b = d1[s]; }
            else if (e == 1) { b = d2[s]; }
            else if (e == 9) { b = 1; }
            else if ((e == 7 || e == 8) && s == 59) { b = 1; }
            else             { b = 0; }
            elemento(b);
        }
    }
}

typedef struct { int ok, malas, h, m, d, mes, a; dcf_estado_t estado; float contraste; } res_t;

/* Arranca en mitad de un minuto a proposito, para que tenga que encontrar la
 * marca el solo. minutos = cuantos minutos completos emite despues. */
static res_t corre(int h0, int m0, int dia, int mes, int anno, int dsem,
                   int minutos, float deriva, float sigma, int romper)
{
    unsigned char d1[60], d2[60];
    res_t r;
    dcf_info_t inf;
    int k, i;

    s_deriva_hz = deriva;
    s_sigma = sigma;

    /* Medio minuto de relleno con la trama del minuto anterior, para que el
     * enganche no empiece justo en una marca. */
    arma((m0 + 59) % 60, h0, dia, mes, anno, dsem, 3, d1, d2);
    for (k = 0; k < 300; k++) { elemento(k % 10 == 9); }

    for (i = 0; i < minutos; i++) {
        /* La trama que se emite durante un minuto describe el SIGUIENTE. */
        int m = (m0 + i + 1) % 60;
        int h = (h0 + (m0 + i + 1) / 60) % 24;
        arma(m, h, dia, mes, anno, dsem, 3, d1, d2);
        /* Se rompe el MISMO bit en TODAS las tramas: si solo se rompiera una,
         * las otras sincronizarian igual y el banco no estaria midiendo nada. */
        if (romper >= 0) { d1[romper] ^= 1; }
        minuto(d1, d2);
    }
    /*
     * La marca que cierra la ultima trama. Son SOLO DOS elementos: los tres
     * primeros unos de la racha de cinco ya los emitio el segundo 59 del
     * ultimo minuto (sus elementos 7, 8 y 9), y estos dos son los elementos
     * 0 y 1 del segundo 00 del minuto siguiente. Emitir aqui los cinco -que
     * es lo que hacia la primera version de este banco- mete tres marcas de
     * mas y deja el decodificador en BUSCANDO despues de haber sincronizado
     * bien, o sea que el banco suspendia por culpa del banco.
     */
    for (k = 0; k < 2; k++) { elemento(1); }

    rbu_info(&inf);
    r.ok = inf.tramas_ok; r.malas = inf.tramas_malas; r.estado = inf.estado;
    r.h = inf.hora; r.m = inf.minuto; r.d = inf.dia; r.mes = inf.mes; r.a = inf.anno;
    r.contraste = inf.contraste;
    return r;
}

int main(void)
{
    int fallos = 0;
    res_t r;

    printf("RBU - fase modulada por tonos, %d Hz de muestreo\n", (int)FS);

    /*
     * *** EL HUSO SE PONE A MANO - 10/10/2026. ***
     * Este banco decia "septiembre es verano en España" y comprobaba las 00.
     * RBU es rusa y da hora de Moscu; que de hora de Madrid era una decision
     * del firmware, no de la emisora, y ya no existe. Ver hora_comun.h.
     */
    hora_huso_pon(120);

    /* --- 1. Limpia ------------------------------------------------------ */
    /* Hora de Moscu 01:31 del jueves 24/09/2026, diferencia con UTC +3.
     * UTC = 22:31 del 23. Con el huso en +2:00 -> 00:31 del 24. */
    s_rnd = 1UL; s_t = 0.0f; s_fase_deriva = 0.0f;
    rbu_start(FS);
    r = corre(1, 30, 24, 9, 26, 4, 5, 0.0f, 0.0f, -1);
    printf("  limpia: buenas %d, malas %d, estado %d, contraste %.2f\n",
           r.ok, r.malas, (int)r.estado, (double)r.contraste);
    printf("    hora de Moscu 01:3x del 24/09 -> aqui %02d:%02d del %02d/%02d/%02d\n",
           r.h, r.m, r.d, r.mes, r.a);
    if (r.estado != DCF_LISTO) { printf("    FALLO: no llego a LISTO\n"); fallos++; }
    if (r.h != 0 || r.d != 24 || r.mes != 9 || r.a != 26) {
        printf("    FALLO: esperaba las 00:3x del 24/09/26\n"); fallos++;
    }

    /* --- 2. Deriva del oscilador --------------------------------------- */
    printf("  deriva del oscilador local (5 minutos cada una):\n");
    {
        float dv[] = { 1.0f, 10.0f, 22.0f, 50.0f, 200.0f, 500.0f };
        unsigned k;
        for (k = 0; k < sizeof(dv) / sizeof(dv[0]); k++) {
            s_rnd = 3UL; s_t = 0.0f; s_fase_deriva = 0.0f;
            rbu_start(FS);
            r = corre(1, 30, 24, 9, 26, 4, 5, dv[k], 0.0f, -1);
            printf("    %5.0f Hz  %s  (%02d:%02d)  buenas %d\n", (double)dv[k],
                   (r.estado == DCF_LISTO) ? "sincroniza   " : "no sincroniza",
                   r.h, r.m, r.ok);
            /* Se exige hasta 200 Hz. El error real a 66 kHz son unos 22 Hz
             * (el paso del temporizador del micro), asi que eso ya es diez
             * veces de margen; los 500 Hz estan en la tabla para ver donde
             * se cae, no como requisito. */
            if (dv[k] <= 200.0f && r.estado != DCF_LISTO) { fallos++; }
        }
    }

    /* --- 3. Ruido de fase ---------------------------------------------- */
    printf("  ruido de fase, en radianes (5 minutos cada uno, deriva 22 Hz):\n");
    {
        float sg[] = { 0.05f, 0.15f, 0.30f, 0.50f, 0.80f, 1.20f };
        unsigned k;
        for (k = 0; k < sizeof(sg) / sizeof(sg[0]); k++) {
            s_rnd = 11UL; s_t = 0.0f; s_fase_deriva = 0.0f;
            rbu_start(FS);
            r = corre(1, 30, 24, 9, 26, 4, 5, 22.0f, sg[k], -1);
            printf("    %.2f rad (%3.0f%% del indice)  %s  buenas %d, malas %d, contraste %.2f\n",
                   (double)sg[k], (double)(100.0f * sg[k] / INDICE),
                   (r.estado == DCF_LISTO) ? "sincroniza   " : "no sincroniza",
                   r.ok, r.malas, (double)r.contraste);
        }
    }

    /* --- 4. Solo ruido -------------------------------------------------- */
    printf("  solo ruido, 15 minutos (lo unico que no puede fallar):\n");
    {
        float sg[] = { 0.3f, 1.0f, 3.0f };
        unsigned k;
        for (k = 0; k < sizeof(sg) / sizeof(sg[0]); k++) {
            int n;
            dcf_info_t inf;
            s_rnd = 77UL + k;
            rbu_start(FS);
            for (n = 0; n < (int)(FS * 60.0f * 15.0f); n++) {
                float f = sg[k] * ruido();
                while (f >  3.14159265f) { f -= 6.28318531f; }
                while (f < -3.14159265f) { f += 6.28318531f; }
                rbu_feed(f);
            }
            rbu_info(&inf);
            printf("    sigma %.1f rad -> %s (buenas %u, malas %u, contraste %.2f)\n",
                   (double)sg[k],
                   (inf.estado == DCF_LISTO) ? "SINCRONIZA - FALLO GRAVE" : "no sincroniza",
                   inf.tramas_ok, inf.tramas_malas, (double)inf.contraste);
            if (inf.estado == DCF_LISTO || inf.tramas_ok > 0U) { fallos++; }
            /* Y el contraste tiene que quedarse abajo: es lo que la pantalla
             * usa para decidir si enseña "buscando" o "leyendo la trama", asi
             * que un contraste alto con ruido puro es una pantalla que miente,
             * aunque nunca llegue a dar una hora. */
            if (inf.contraste >= 0.25f) {
                printf("      FALLO: contraste %.2f con ruido puro (el limite es 0,25)\n",
                       (double)inf.contraste);
                fallos++;
            }
        }
    }

    /* --- 5. Un bit cambiado -------------------------------------------- */
    printf("  una trama con un bit de datos 1 cambiado (lo cazan las paridades):\n");
    {
        int bits[] = { 1, 24, 19, 27, 35, 39, 43, 49, 55, 59 };
        unsigned k;
        for (k = 0; k < sizeof(bits) / sizeof(bits[0]); k++) {
            s_rnd = 5UL; s_t = 0.0f; s_fase_deriva = 0.0f;
            rbu_start(FS);
            r = corre(1, 30, 24, 9, 26, 4, 4, 22.0f, 0.0f, bits[k]);
            printf("    segundo %2d -> %s (buenas %d, malas %d)\n", bits[k],
                   (r.estado == DCF_LISTO || r.ok > 0) ? "ACEPTADA - FALLO" : "rechazada",
                   r.ok, r.malas);
            if (r.estado == DCF_LISTO || r.ok > 0) { fallos++; }
        }
    }

    if (fallos == 0) { printf("  TODO CORRECTO (0 fallos)\n"); }
    else             { printf("  %d FALLOS\n", fallos); }
    return fallos ? 1 : 0;
}

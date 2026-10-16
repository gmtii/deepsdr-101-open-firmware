/*
 * EL ALFABETO DE SIETE BITS, CONTRA SEÑAL SINTETICA.
 *
 * Pedido por el usuario.
 *
 * Era una FSK de 50,00 baudios y 201 Hz de salto cuyo contenido es el
 * alfabeto CCIR 476 -siete bits con cuatro unos- cifrado. La fila del
 * ident ya existia y no cuadraba porque pedia velocidad de SIMBOLO y el
 * medidor daba 25,0 Bd: la mitad exacta, porque en una FSK de dos tonos
 * con bits al azar el tono cambia cada dos bits de media y ese medidor
 * cuenta cambios de tono, no simbolos.
 *
 * La segunda condicion de esa fila paso a ser cr7_pc -ver stanag_det.h-, y
 * esto es lo que la vigila.
 *
 * QUE MIDE, Y POR QUE ES SINTETICO
 * --------------------------------
 * La grabacion de verdad esta en sim/muestras/fsk200_16911.f32 y se mira
 * con "make identaire". Pero un banco de "make comprueba" no puede
 * depender de un fichero de dos megas, asi que aqui la señal se fabrica.
 *
 * Y fabricarla permite lo que una grabacion no permite: EL TESTIGO QUE DE
 * VERDAD IMPORTA. El par decisivo no es "CCIR contra ruido" -eso lo separa
 * cualquier cosa-, es:
 *
 *     CCIR 476 a 50 Bd y 200 Hz   contra   BITS AL AZAR a 50 Bd y 200 Hz
 *
 * Dos señales con el MISMO salto, la MISMA velocidad y el MISMO par de
 * tonos, que solo se distinguen por lo que llevan dentro. Si el dia de
 * mañana alguien afloja esta medida, es este par el que se cae, y por eso
 * el banco lo mira primero.
 *
 * SE HA VISTO FALLAR. Quitando las cuatro fases de muestreo el margen baja
 * de 36 puntos a 24; poniendo el umbral del ident en 70 en vez de 60, el
 * NAVTEX de verdad (78%) se queda a ocho puntos del borde. Los dos casos
 * los pilla la comprobacion de margen de abajo.
 *
 * LOS NUMEROS, medidos con este mismo motor (no fuera de la radio: fuera
 * salen veinte puntos mas altos porque alli hay recuperacion de reloj de
 * verdad, y lo que vale es lo que mide la radio):
 *
 *   CCIR 476  50 Bd / 200 Hz        79 %      <- lo que buscamos
 *   CCIR 476 100 Bd / 170 Hz        86 %
 *   bits al azar 50 Bd / 200 Hz     42 %      <- el testigo duro
 *   bits al azar 75 Bd / 850 Hz     41 %
 *   bits al azar 100 Bd / 170 Hz    36 %
 *   Baudot asincrono 50 Bd          36 %
 *   al azar 50 Bd / 200 Hz a 6 dB   33 %
 *   (el azar puro seria 35/128 = 27 %)
 */
#include "stanag_det.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS      12000.0
#define SEGS    45.0

/* El umbral que usa la tabla del ident. Si se cambia alli, se cambia aqui:
 * un banco que vigila un numero distinto del que se usa no vigila nada. */
#define UMBRAL_IDENT  60

static float g_trabajo[STANAG_DET_FLOATS];

/* ---- un generador de numeros propio, para que el banco sea repetible ---- */
static uint32_t g_sem = 123456789U;
static uint32_t azar(void)
{
    g_sem ^= g_sem << 13; g_sem ^= g_sem >> 17; g_sem ^= g_sem << 5;
    return g_sem;
}

/*
 * Los 35 patrones de siete bits con cuatro unos. Son el alfabeto CCIR 476
 * entero: lo que cambia entre el NAVTEX y la cifrada del usuario es que
 * letra representa cada uno, no cuales son.
 */
static uint8_t g_ccir[35];
static uint8_t g_nccir;

static void ccir_tabla(void)
{
    uint16_t c;
    g_nccir = 0U;
    for (c = 0U; c < 128U; c++) {
        uint8_t u = 0U, k;
        for (k = 0U; k < 7U; k++) { if (c & (1U << k)) { u++; } }
        if (u == 4U) { g_ccir[g_nccir++] = (uint8_t)c; }
    }
}

/*
 * Fabrica una FSK. `ccir` a 1 mete el alfabeto; a 0, bits al azar.
 * `asinc` a 1 mete Baudot: arranque a 0, cinco de datos y parada a 1.
 */
static float *fabrica(double bd, double salto, int ccir, int asinc,
                      double snr_db, uint32_t *n_out)
{
    uint32_t n = (uint32_t)(FS * SEGS);
    float *y = (float *)malloc(n * sizeof(float));
    uint32_t nb = (uint32_t)(SEGS * bd) + 16U;
    uint8_t *bits = (uint8_t *)malloc(nb);
    uint32_t i, b = 0U;
    double ph = 0.0, f0 = 1000.0, amp = 0.7;

    if (y == 0 || bits == 0) { printf("sin memoria\n"); exit(1); }

    while (b < nb) {
        if (ccir) {
            uint8_t c = g_ccir[azar() % g_nccir];
            uint8_t k;
            for (k = 0U; k < 7U && b < nb; k++) {
                bits[b++] = (uint8_t)((c >> (6U - k)) & 1U);
            }
        } else if (asinc) {
            uint8_t k;
            if (b < nb) { bits[b++] = 0U; }                 /* arranque */
            for (k = 0U; k < 5U && b < nb; k++) {
                bits[b++] = (uint8_t)(azar() & 1U);
            }
            if (b < nb) { bits[b++] = 1U; }                 /* parada */
            if (b < nb) { bits[b++] = 1U; }                 /* y media */
        } else {
            bits[b++] = (uint8_t)(azar() & 1U);
        }
    }

    for (i = 0U; i < n; i++) {
        uint32_t k = (uint32_t)(((double)i / FS) * bd);
        double f = bits[(k < nb) ? k : (nb - 1U)] ? f0 : (f0 + salto);
        double v;
        ph += (2.0 * 3.14159265358979 * f) / FS;
        v = amp * cos(ph);
        if (snr_db < 90.0) {
            /* ruido gaussiano barato: suma de doce uniformes menos seis */
            double g = 0.0; uint8_t j;
            for (j = 0U; j < 12U; j++) {
                g += (double)(azar() & 0xFFFFU) / 65535.0;
            }
            v += (amp * pow(10.0, -snr_db / 20.0)) * (g - 6.0);
        }
        y[i] = (float)v;
    }
    free(bits);
    *n_out = n;
    return y;
}

/* Mete la señal por la misma puerta que el audio de la radio y devuelve
 * lo ultimo que midio el motor. */
static void mide(const float *y, uint32_t n, stanag_det_t *out)
{
    uint32_t i;
    stanag_det_t m;

    if (!stanag_det_init(g_trabajo, STANAG_DET_FLOATS)) {
        printf("el detector no arranca\n"); exit(1);
    }
    memset(out, 0, sizeof *out);
    for (i = 0U; i + 256U <= n; i += 256U) {
        stanag_det_mete(&y[i], 256U);
        while (stanag_det_paso(&m)) { *out = m; }
    }
}

typedef struct {
    const char *nombre;
    double bd, salto, snr;
    int ccir, asinc;
    int espera_ccir;        /* 1 = tiene que dar alto; 0 = tiene que dar bajo */
    uint16_t espera_bd_x10; /* si espera_ccir, con que baudio. 0 = no se mira */
} caso_t;

int main(void)
{
    static const caso_t k[] = {
        /* nombre                        bd    salto  snr  cc as  si  bdx10 */
        { "CCIR 476  50 Bd / 200 Hz",    50.0, 200.0, 99.0, 1, 0, 1,  500U },
        { "CCIR 476 100 Bd / 170 Hz",   100.0, 170.0, 99.0, 1, 0, 1, 1000U },
        { "CCIR 476  75 Bd / 850 Hz",    75.0, 850.0, 99.0, 1, 0, 1,  750U },
        { "al azar   50 Bd / 200 Hz",    50.0, 200.0, 99.0, 0, 0, 0,    0U },
        { "al azar   75 Bd / 850 Hz",    75.0, 850.0, 99.0, 0, 0, 0,    0U },
        { "al azar  100 Bd / 170 Hz",   100.0, 170.0, 99.0, 0, 0, 0,    0U },
        { "Baudot    50 Bd / 450 Hz",    50.0, 450.0, 99.0, 0, 1, 0,    0U },
        { "al azar   50 Bd / 200 Hz  6 dB", 50.0, 200.0, 6.0, 0, 0, 0,  0U },
        { "CCIR 476  50 Bd / 200 Hz  6 dB", 50.0, 200.0, 6.0, 1, 0, 1, 500U },
    };
    const uint16_t nk = (uint16_t)(sizeof k / sizeof k[0]);
    uint16_t i;
    int fallos = 0;
    int peor_si = 100, mejor_no = 0;
    const char *peor_si_n = "-", *mejor_no_n = "-";

    ccir_tabla();
    printf("\nEL ALFABETO DE SIETE BITS (CCIR 476), POR EL MOTOR DE LA RADIO\n");
    printf("umbral del ident: %d %%   (bits al azar dan 27 %% por pura cuenta)\n\n",
           UMBRAL_IDENT);
    printf("  %-32s %6s  %8s   %s\n", "caso", "CR7", "baudio", "veredicto");

    for (i = 0U; i < nk; i++) {
        stanag_det_t m;
        float *y; uint32_t n;
        const char *v;
        int mal = 0;

        g_sem = 123456789U + (uint32_t)i * 7919U;
        y = fabrica(k[i].bd, k[i].salto, k[i].ccir, k[i].asinc, k[i].snr, &n);
        mide(y, n, &m);
        free(y);

        if (k[i].espera_ccir) {
            if (m.cr7_pc < (uint8_t)UMBRAL_IDENT) { mal = 1; }
            if (k[i].espera_bd_x10 != 0U && m.cr7_bd_x10 != k[i].espera_bd_x10) {
                mal = 1;
            }
            if (m.cr7_pc < peor_si) { peor_si = m.cr7_pc; peor_si_n = k[i].nombre; }
        } else {
            if (m.cr7_pc >= (uint8_t)UMBRAL_IDENT) { mal = 1; }
            if (m.cr7_pc > mejor_no) { mejor_no = m.cr7_pc; mejor_no_n = k[i].nombre; }
        }
        v = mal ? "MAL" : "bien";
        if (mal) { fallos++; }

        printf("  %-32s %5u%%  %4u,%u Bd   %s\n", k[i].nombre,
               (unsigned)m.cr7_pc,
               (unsigned)(m.cr7_bd_x10 / 10U), (unsigned)(m.cr7_bd_x10 % 10U),
               v);
    }

    /*
     * Y EL MARGEN, QUE ES LO QUE DE VERDAD SE VIGILA.
     *
     * Que todos los casos caigan del lado bueno no dice nada si el peor
     * "si" y el mejor "no" estan pegados: eso es un banco que pasa hoy y
     * se cae con la primera grabacion desvanecida. Se exige que entre los
     * dos haya al menos veinte puntos.
     */
    printf("\n  el peor que SI:  %3d %%   %s\n", peor_si, peor_si_n);
    printf("  el mejor que NO: %3d %%   %s\n", mejor_no, mejor_no_n);
    printf("  margen: %d puntos (hacen falta 20)\n", peor_si - mejor_no);
    if ((peor_si - mejor_no) < 20) {
        printf("  MAL: sin margen. Un umbral entre dos valores pegados lo\n"
               "       decide el desvanecimiento de turno, no la señal.\n");
        fallos++;
    }

    if (fallos) {
        printf("\nCR7: %d MAL\n\n", fallos);
        return 1;
    }
    printf("\nCR7: todo correcto\n\n");
    return 0;
}

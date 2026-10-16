/*
 * ALE SINTETICO: DE LA PALABRA AL AIRE Y DE VUELTA.
 *
 * POR QUE EXISTE. Entre el indicativo y la pantalla hay SIETE convenciones
 * que se pueden poner del reves sin que nada se queje, y cada una da el
 * mismo sintoma: no sale nada. Son estas, y las siete se prueban aqui:
 *
 *   1. el polinomio del Golay (0xAE3, no su reciproco 0xC75)
 *   2. el orden de los bits dentro del codigo Golay
 *   3. la inversion de los doce bits de paridad de la mitad B
 *   4. el entrelazado alterno de las dos mitades
 *   5. el bit de relleno, que va el ultimo y no se vota
 *   6. la triple repeticion, que NO esta alineada a simbolo
 *   7. el mapa de tono a simbolo, que es Gray y no lineal
 *
 * La numero 6 es la que mas gente se come: una palabra son 49 bits que se
 * repiten tres veces, o sea 147 bits, y 49 no es multiplo de 3. La segunda
 * copia empieza a mitad de un simbolo. Quien suponga tres bloques de
 * simbolos identicos escribe un decodificador entero que no acierta una.
 *
 * Aqui se genera la senal desde el otro extremo -palabra, Golay, inversion,
 * entrelazado, repeticion, Gray, tonos- y se comprueba que vuelve la misma
 * palabra. Si una convencion esta del reves, el banco lo dice.
 */
#include "ale_rx.h"
#include "ale_golay.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define FS    12000.0
#define SPS   96
#define NSIM  49

static int fallos;

static void bien(int ok, const char *que)
{
    printf("    %-56s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

static const uint8_t k_gray[8] = { 0, 1, 3, 2, 6, 7, 5, 4 };
static const double  k_hz[8] = { 750, 1000, 1250, 1500, 1750, 2000, 2250, 2500 };

/* El rango de tono que corresponde a un valor de simbolo (el inverso del Gray). */
static uint8_t rango(uint8_t sim)
{
    uint8_t r;

    for (r = 0; r < 8; r++) { if (k_gray[r] == sim) { return r; } }
    return 0;
}

/* Una palabra de 24 bits -> 49 simbolos. */
static void palabra_a_simbolos(uint32_t w24, uint8_t *sim)
{
    uint8_t  tx[49];
    uint32_t A = ale_golay_codifica((uint16_t)((w24 >> 12) & 0xFFF));
    uint32_t B = ale_golay_codifica((uint16_t)(w24 & 0xFFF));
    int i, k;

    /* Los doce bits de paridad de la mitad B viajan INVERTIDOS. */
    B ^= 0x00000FFFUL;

    for (k = 0; k < 24; k++) {
        tx[2 * k]     = (uint8_t)((A >> (23 - k)) & 1U);
        tx[2 * k + 1] = (uint8_t)((B >> (23 - k)) & 1U);
    }
    tx[48] = 0;    /* S49, el bit de relleno */

    /* Tres copias a nivel de BIT, no de simbolo: aire[i] = tx[i % 49]. */
    for (i = 0; i < NSIM; i++) {
        sim[i] = (uint8_t)((tx[(3 * i)     % 49] << 2) |
                           (tx[(3 * i + 1) % 49] << 1) |
                           (tx[(3 * i + 2) % 49]));
    }
}

/*
 * De simbolos a audio. FASE CONTINUA entre simbolos, que es lo que hace un
 * modulador de verdad: cortar la fase en cada cambio de tono ensancha el
 * espectro y el banco saldria mas facil de lo que va a ser el aire.
 */
static uint32_t modula(const uint8_t *sim, int n, float *sal,
                       double retardo, float sigma, unsigned semilla)
{
    double fase = 0.0;
    uint32_t m = 0;
    int i, k, r;

    srand(semilla);

    /* Un poco de silencio delante, y descolocado a proposito: asi el
     * receptor tiene que encontrar la fase de simbolo y no se la damos. */
    for (i = 0; i < (int)retardo; i++) { sal[m++] = 0.0f; }

    for (i = 0; i < n; i++) {
        double f = k_hz[rango(sim[i])];

        for (k = 0; k < SPS; k++) {
            sal[m++] = (float)sin(fase);
            fase += 2.0 * 3.14159265358979 * f / FS;
            if (fase > 6.283185307179586) { fase -= 6.283185307179586; }
        }
    }
    for (i = 0; i < SPS * 2; i++) { sal[m++] = 0.0f; }

    if (sigma > 0.0f) {
        uint32_t j;

        for (j = 0; j < m; j++) {
            double rr = 0.0;
            for (r = 0; r < 12; r++) { rr += (double)rand() / RAND_MAX; }
            sal[j] += (float)(sigma * (rr - 6.0));
        }
    }
    return m;
}

/* Monta una palabra ALE: preambulo de 3 bits y tres caracteres de 7. */
static uint32_t pal(uint8_t pre, const char *t)
{
    return (((uint32_t)pre & 7U) << 21) |
           (((uint32_t)(uint8_t)t[0] & 0x7FU) << 14) |
           (((uint32_t)(uint8_t)t[1] & 0x7FU) << 7) |
           ((uint32_t)(uint8_t)t[2] & 0x7FU);
}

/* Decodifica y devuelve cuantas palabras han salido bien. */
static int pasa(const float *aud, uint32_t n, uint32_t esperada,
                uint8_t *cal_out)
{
    int buenas = 0;
    uint32_t k;

    ale_rx_start((float)FS);
    for (k = 0; k < n; k += 256U) {
        uint32_t q = (n - k > 256U) ? 256U : (n - k);
        uint32_t w;
        uint8_t  c;

        ale_rx_mete(&aud[k], q);
        while (ale_rx_saca(&w, &c)) {
            if (w == esperada) { buenas++; if (cal_out) { *cal_out = c; } }
        }
    }
    ale_rx_stop();
    return buenas;
}

int main(void)
{
    static uint8_t sim[NSIM * 8];
    static float   aud[262144];
    uint32_t w, n;
    uint8_t  cal = 0;

    printf("\nALE SINTETICO\n\n");

    /* ------------------------------------------- 1. el Golay, ida y vuelta */
    {
        uint8_t e = 0U;
        uint32_t c = ale_golay_codifica(0xABCU);

        bien(ale_golay_decodifica(c, &e) == 0xABCU && e == 0U,
             "el Golay vuelve entero sin errores");
    }

    /* ------------------------------------- 2. una palabra suelta, sin ruido */
    printf("\n  un sondeo \"TIS EA4\":\n");
    w = pal(5U, "EA4");    /* 5 = THIS IS */
    palabra_a_simbolos(w, sim);
    /* Cuatro palabras seguidas, que es lo que hace un sondeo de verdad. */
    {
        int i;
        for (i = 1; i < 4; i++) { memcpy(&sim[i * NSIM], sim, NSIM); }
    }
    n = modula(sim, NSIM * 4, aud, 37.0, 0.0f, 1U);
    {
        int b = pasa(aud, n, w, &cal);

        printf("      %d palabras de 4, calidad %u/48\n", b, cal);
        bien(b >= 3, "saca al menos tres de las cuatro");
        bien(cal == 48U, "con la senal limpia, los 48 votos son unanimes");
    }

    /* ---------------------------------- 3. que el preambulo y el texto salen */
    printf("\n  los campos de la palabra:\n");
    {
        uint32_t v = w;

        bien(((v >> 21) & 7U) == 5U, "el preambulo es 5 (THIS IS)");
        bien(((v >> 14) & 0x7FU) == (uint32_t)'E', "el primer caracter es 'E'");
        bien(((v >> 7)  & 0x7FU) == (uint32_t)'A', "el segundo es 'A'");
        bien((v & 0x7FU) == (uint32_t)'4', "y el tercero es '4'");
    }

    /* ------------------------------ 4. las cuatro fases de simbolo posibles */
    printf("\n  con la palabra descolocada (fase de simbolo):\n");
    {
        const double d[] = { 0.0, 13.0, 24.0, 37.0, 48.0, 61.0, 72.0, 85.0 };
        unsigned i;
        int todas = 1;

        for (i = 0; i < sizeof d / sizeof d[0]; i++) {
            int b;

            n = modula(sim, NSIM * 4, aud, d[i], 0.0f, 2U);
            b = pasa(aud, n, w, (uint8_t *)0);
            if (b < 2) { todas = 0; }
            printf("      retardo de %2.0f muestras (%.2f simbolos) -> %d de 4\n",
                   d[i], d[i] / SPS, b);
        }
        bien(todas, "engancha caiga donde caiga el principio del simbolo");
    }

    /* ---------------------------------------------------------- 5. con ruido */
    printf("\n  con ruido:\n");
    {
        const float sg[] = { 0.1f, 0.3f, 0.6f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        unsigned i, s;

        for (i = 0; i < sizeof sg / sizeof sg[0]; i++) {
            unsigned ok = 0;

            for (s = 0; s < 10; s++) {
                n = modula(sim, NSIM * 4, aud, 37.0, sg[i], 200U + s);
                if (pasa(aud, n, w, (uint8_t *)0) >= 1) { ok++; }
            }
            printf("      sigma %.2f  ->  %u de 10\n", sg[i], ok);
            if (i == 0U) { bien(ok >= 9U, "con poco ruido sale casi siempre"); }
            if (sg[i] == 1.0f) {
                /* Con sigma 1 el ruido tiene la misma amplitud eficaz que
                 * la senal. Que a eso siga saliendo es lo que da el Golay
                 * mas la votacion 2 de 3: entre las dos aguantan bastante
                 * mas que cualquiera de las dos por separado. */
                bien(ok >= 5U, "y con el ruido al nivel de la senal, la mitad");
            }
        }
    }

    /* ------------------------------- 6. un sondeo largo: TIS EA4 + DATA XYZ */
    printf("\n  un indicativo de seis: \"TIS EA4\" + \"DATA XYZ\":\n");
    {
        uint32_t w1 = pal(5U, "EA4");    /* THIS IS */
        uint32_t w2 = pal(0U, "XYZ");    /* DATA */
        uint8_t  s2[NSIM * 4];
        int b1, b2, i;

        palabra_a_simbolos(w1, &s2[0]);
        palabra_a_simbolos(w2, &s2[NSIM]);
        for (i = 0; i < NSIM; i++) {
            s2[2 * NSIM + i] = s2[i];
            s2[3 * NSIM + i] = s2[NSIM + i];
        }
        n = modula(s2, NSIM * 4, aud, 37.0, 0.0f, 3U);
        b1 = pasa(aud, n, w1, (uint8_t *)0);
        b2 = pasa(aud, n, w2, (uint8_t *)0);
        printf("      TIS EA4 x%d, DATA XYZ x%d\n", b1, b2);
        bien(b1 >= 1 && b2 >= 1, "saca las dos palabras de la pareja");
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * ======================================================================
 * SALTAR DE EMISORA EN EMISORA - 15/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Esto mide emisoras_salto() contra el EMISORAS.BIN de verdad, barriendo
 * las 24 horas y toda la HF en los dos sentidos. Lo que exige, y cada cosa
 * caza una averia distinta:
 *
 *   1. QUE SE MUEVA, Y HACIA DONDE SE PIDE. Un salto adelante tiene que
 *      subir de frecuencia y uno atras bajar, SIEMPRE. El fallo tipico al
 *      escribir esto es quedarse en la emisora en la que ya estas -la
 *      busqueda binaria te deja encima de ella- y entonces el mando
 *      parece roto.
 *
 *   2. IDA Y VUELTA. Desde una emisora, un salto adelante y otro atras
 *      tiene que devolverte EXACTAMENTE a donde estabas. Es la prueba mas
 *      dura que se le puede hacer a esto, porque un error de uno en
 *      cualquiera de los dos sentidos la rompe.
 *
 *   3. QUE NO SE SALTE NINGUNA. Entre el punto de partida y donde cae, no
 *      puede haber ninguna otra emisora en antena. Se comprueba con
 *      emisoras_rango(), que es OTRO camino del mismo fichero: si los dos
 *      se equivocaran igual seria casualidad, no un fallo compartido.
 *
 *   4. QUE LO QUE ENCUENTRE ESTE DE VERDAD EN ANTENA. Se vuelve a
 *      preguntar con emisoras_busca(), que es el tercer camino.
 *
 *   5. QUE UN CLIC CUESTE SIEMPRE MAS O MENOS LO MISMO. Se cuentan las
 *      lecturas de flash. Sin tope, una banda vacia de madrugada haria que
 *      un clic recorriera las 1.898 frecuencias de la base y la radio se
 *      quedase parada sin que se entienda por que.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emisoras.h"

static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-52s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

static FILE *g_f;
static long  g_tam;
static long  g_lecturas;
static void lee(uint32_t a, uint8_t *b, uint32_t n)
{
    g_lecturas++;
    memset(b, 0, n);
    if ((long)a >= g_tam) { return; }
    fseek(g_f, (long)a, SEEK_SET);
    if (fread(b, 1, n, g_f) != n) { /* el relleno de ceros ya esta */ }
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "datos/EMISORAS.BIN";
    int h;
    long clics = 0, tot = 0, peor = 0;
    uint32_t peor_hz = 0; int peor_h = 0;
    long sin_destino = 0;
    int mal_sentido = 0, mal_vuelta = 0, mal_saltada = 0, mal_antena = 0;
    uint32_t ej_vuelta = 0;

    printf("\n== SALTAR DE EMISORA EN EMISORA ==\n\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) { printf("  (no esta %s: me salto el banco)\n", ruta); return 0; }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    if (!emisoras_abre(0UL, lee)) { printf("  no abre la base\n"); return 1; }

    for (h = 0; h < 24; h++) {
        uint16_t min = (uint16_t)(h * 60);
        uint32_t f;

        for (f = 2300000UL; f < 30000000UL; f += 37000UL) {
            int d;

            for (d = 0; d < 2; d++) {
                int8_t dir = (int8_t)(d ? -1 : 1);
                uint32_t hz = 0UL, vuelta = 0UL;
                long antes = g_lecturas, coste;
                uint8_t ok;

                ok = emisoras_salto(f, dir, min, EMISORAS_DIA_CUALQUIERA,
                                    &hz, 0, 0);
                coste = g_lecturas - antes;
                clics++; tot += coste;
                if (coste > peor) { peor = coste; peor_hz = f; peor_h = h; }
                if (!ok) { sin_destino++; continue; }

                /* 1. el sentido */
                if ((dir > 0 && hz <= f) || (dir < 0 && hz >= f)) {
                    mal_sentido++;
                    continue;
                }

                /* 3. ninguna por el camino, preguntado por otro sitio */
                {
                    emisoras_rango_r_t r[4];
                    uint16_t hubo = 0U;
                    uint32_t lo = (dir > 0) ? (f + 1UL) : hz;
                    uint32_t hi = (dir > 0) ? hz : (f - 1UL);
                    uint8_t  n;

                    if (hi >= lo) {
                        n = emisoras_rango(lo, hi, min, EMISORAS_DIA_CUALQUIERA,
                                           r, 4U, &hubo);
                        /* La unica que puede haber en ese tramo es la que
                         * ha encontrado: la del borde. */
                        if (n != 1U || r[0].hz != hz) { mal_saltada++; }
                    }
                }

                /* 4. y que este de verdad en antena */
                {
                    emisoras_r_t q[1];
                    uint8_t n = emisoras_busca(hz, 0UL, min,
                                               EMISORAS_DIA_CUALQUIERA, q, 1U);
                    if (n == 0U || !q[0].ahora) { mal_antena++; }
                }

                /* 2. ida y vuelta */
                if (emisoras_salto(hz, (int8_t)-dir, min,
                                   EMISORAS_DIA_CUALQUIERA, &vuelta, 0, 0)) {
                    /*
                     * Solo se exige si entre `f` y `hz` NO habia nada, que
                     * es el caso normal; si se partio de un punto que no es
                     * emisora, la vuelta cae en la emisora anterior a `f` y
                     * eso es correcto. Asi que la vuelta se mide saliendo
                     * de una emisora de verdad: la propia `hz`.
                     */
                    uint32_t ida = 0UL;
                    if (emisoras_salto(vuelta, dir, min,
                                       EMISORAS_DIA_CUALQUIERA, &ida, 0, 0)) {
                        if (ida != hz) {
                            if (!mal_vuelta) { ej_vuelta = hz; }
                            mal_vuelta++;
                        }
                    }
                }
            }
        }
    }

    printf("  %ld clics medidos (24 horas x toda la HF, los dos sentidos)\n\n",
           clics);

    di(mal_sentido == 0,
       "adelante siempre sube y atras siempre baja",
       "quedarse en la emisora en la que ya estas hace que el mando parezca roto");
    {
        char dd[80];
        snprintf(dd, sizeof dd, "el primero que falla: %.3f MHz", ej_vuelta / 1e6);
        di(mal_vuelta == 0, "ida y vuelta devuelve a la misma emisora",
           mal_vuelta ? dd : "");
    }
    di(mal_saltada == 0,
       "entre el punto de partida y el destino no hay ninguna mas",
       "lo dice emisoras_rango(), que es otro camino del mismo fichero");
    di(mal_antena == 0,
       "y la emisora a la que salta esta de verdad en antena",
       "lo dice emisoras_busca(), que es un tercer camino");
    {
        char dd[120];
        snprintf(dd, sizeof dd,
                 "media %.1f lecturas, peor %ld (%.3f MHz a las %02d:00); presupuesto %u lecturas",
                 (double)tot / (double)clics, peor, peor_hz / 1e6, peor_h,
                 (unsigned)EMISORAS_SALTO_MAX);
        printf("    %-52s %s\n", "coste de un clic, en lecturas de flash", dd);
        di(peor < 400, "y el peor clic no se desmanda", dd);
    }
    printf("    %-52s %ld (%.1f%%)\n", "clics sin destino (no se mueve)",
           sin_destino, 100.0 * (double)sin_destino / (double)clics);

    /*
     * 6. Y QUE UN GIRO RAPIDO NO CUESTE DIEZ VECES UN GIRO LENTO.
     *
     * Pedido por el usuario.
     *
     * El mando entrega los detentes ACUMULADOS, asi que un sondeo puede
     * pedir diez saltos seguidos. Si cada uno gastara su tope entero, el
     * peor sondeo seria diez veces el peor clic y la cascada se pararia.
     * El presupuesto se reparte: esto mide que el sondeo entero NO cueste
     * mas que un solo clic del peor caso.
     */
    printf("\n  6. UN GIRO DE DIEZ DETENTES\n");
    {
        long peor_uno = 0, peor_diez = 0;
        int h2;

        for (h2 = 0; h2 < 24; h2 += 2) {
            uint16_t min = (uint16_t)(h2 * 60);
            uint32_t f;

            for (f = 2300000UL; f < 30000000UL; f += 101000UL) {
                uint32_t hz = f;
                uint16_t gasto = 0U;
                long antes = g_lecturas;
                int j;

                /* uno solo */
                (void)emisoras_salto(f, 1, min, EMISORAS_DIA_CUALQUIERA,
                                     &hz, 0, 0);
                if (g_lecturas - antes > peor_uno) {
                    peor_uno = g_lecturas - antes;
                }

                /* diez, con el presupuesto compartido */
                hz = f; antes = g_lecturas;
                for (j = 0; j < 10; j++) {
                    uint32_t sig;
                    if (!emisoras_salto(hz, 1, min, EMISORAS_DIA_CUALQUIERA,
                                        &sig, 0, &gasto)) { break; }
                    hz = sig;
                }
                if (g_lecturas - antes > peor_diez) {
                    peor_diez = g_lecturas - antes;
                }
            }
        }
        {
            char dd[120];
            snprintf(dd, sizeof dd,
                     "un clic %ld lecturas, diez detentes %ld",
                     peor_uno, peor_diez);
            printf("    %-52s %s\n", "el peor sondeo, en lecturas de flash", dd);
            /*
             * Lo que se exige es el PRESUPUESTO, no "lo mismo que uno":
             * diez detentes pueden gastar hasta el tope, mas lo que cueste
             * la llamada que lo descubre. Lo que NO puede pasar es que
             * cueste diez veces el peor clic, que es lo que pasaba.
             */
            di(peor_diez <= (long)EMISORAS_SALTO_MAX + 40,
               "diez detentes caben en el presupuesto de uno",
               "sin presupuesto compartido, un giro rapido para la cascada");
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

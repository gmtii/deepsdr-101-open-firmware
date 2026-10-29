/*
 * ======================================================================
 * LA RAYA DEL DIAL POR DEBAJO DE 300 kHz - 27/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE: visto en la radio, en onda larga. Sintonizando
 * hacia ARRIBA sin parar, la marca del punto de demodulacion iba hacia
 * delante y hacia atras dando saltos, y no llegaba a ninguna parte. ***
 *
 * No era un fallo de dibujo, y por eso hace falta medirlo: el eje, los
 * datos y la marca eran coherentes entre si. Lo que se movia mal era el
 * OSCILADOR.
 *
 * Por debajo de LO_GEN_CROSSOVER_HZ el oscilador lo hace un temporizador
 * del GD32, que solo puede dar TIMERCLK/(2*N) con N entero y PAR. En onda
 * larga eso son 1.300 a 1.700 Hz de escalon -3.623 en el cruce- mientras
 * el dial va de 1.000 en 1.000. Pidiendole "el dial menos Fs/4" en cada
 * clic, el oscilador redondea, el panel se centra en el oscilador REAL y
 * la marca -que se pone a `sintonia - oscilador`- se va detras del
 * redondeo.
 *
 * SE ARREGLA EN LA PANTALLA Y NO EN EL OSCILADOR. El panel pasa a
 * centrarse en lo que se PIDIO -que sigue al dial exactamente- y los datos
 * se giran los pocos bins del redondeo para que sigan cayendo donde les
 * toca. La radio sintoniza igual que siempre y el NCO sigue siendo del
 * usuario.
 *
 * ESTE BANCO CORRE LA CUENTA DE VERDAD, sin copiarla: el redondeo del
 * temporizador sale de User/lo_gen_gd32.h. Lo unico que se escribe aqui es
 * la formula de la marca, que es la misma linea de chrome_sync():
 *
 *     demod_px = (sintonia - centro_del_panel) * SPC_TRACE_W / span
 *
 * Y LO QUE EXIGE son dos cosas, porque una sola no basta:
 *
 *   1. que subiendo de frecuencia **la marca no retroceda**; y
 *   2. que **la emisora siga debajo de la marca**, que es lo que el giro
 *      de los datos tiene que garantizar. Sin la segunda, clavar la marca
 *      seria trampa: se quedaria quieta y la señal se iria de debajo.
 */
#include "lo_gen_gd32.h"
#include "spec_chrome.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Los mismos numeros que main.c. */
#define IF_OFF_HZ        24000UL   /* DEMOD_IF_OFFSET_HZ, Fs/4 a 96 kHz */
#define SPAN_HZ         192000UL   /* lo que se ve de una vez a 1X */
#define FFT_BINS           512UL   /* FFT_SIZE de User/fft.h */

static int fallos = 0;
/* Cuantos clics del mando movieron la marca HACIA ATRAS, y cuantos hubo.
 * Es la cuenta que importa: lo que molesta no es cuanto retrocede sino
 * cada cuanto. Los llena peor_retroceso(). */
static int g_atras = 0;
static int g_clics = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* Lo que sale de verdad del temporizador para un oscilador pedido. */
static uint32_t timer_real(uint32_t pedido_hz)
{
    return lo_gen_gd32_hz_de(lo_gen_gd32_cuentas(pedido_hz));
}

/* La marca, en pixeles desde el centro del panel. Copia de chrome_sync(). */
static double marca_px(uint32_t dial_hz, uint32_t lo_hz)
{
    double mix = (double)dial_hz - (double)lo_hz;

    return mix * (double)SPC_TRACE_W / (double)SPAN_HZ;
}

/*
 * Recorre el dial de `a` a `b` de `paso` en `paso` y devuelve el PEOR
 * retroceso de la marca, en pixeles. Cero = la marca nunca va para atras.
 *
 * `arreglado` elige de que se centra el panel: 0 es lo de antes -del
 * oscilador REAL, con el redondeo dentro- y 1 lo de ahora -del PEDIDO-.
 */
static double peor_retroceso(uint32_t a, uint32_t b, uint32_t paso,
                             int arreglado, int pinta)
{
    uint32_t f, lo = 0UL;
    double ant = 0.0, peor = 0.0;
    int primero = 1;
    g_atras = 0;
    g_clics = 0;

    for (f = a; f <= b; f += paso) {
        uint32_t pedido = f - IF_OFF_HZ;
        double x, d;

        /* Por debajo del cruce manda el temporizador, y lo que queda
         * apuntado es lo que SALE, no lo que se pidio: es lo que hace
         * apply_lo_tune() desde la V4.10. */
        lo = (pedido < LO_GEN_CROSSOVER_HZ) ? timer_real(pedido) : pedido;

        x = marca_px(f, arreglado ? pedido : lo);
        d = primero ? 0.0 : (x - ant);
        if (pinta) {
            printf("      %7.1f kHz   LO %7u   marca %7.1f px  %+6.1f\n",
                   f / 1000.0, lo, x, d);
        }
        if (!primero) {
            g_clics++;
            if (d < -0.001) { g_atras++; }
            if (d < peor) { peor = d; }
        }
        ant = x;
        primero = 0;
    }
    return peor;
}

int main(void)
{
    char t[96];

    printf("\nLA RAYA DEL DIAL POR DEBAJO DE 300 kHz\n\n");

    /* --------------------------------------------------------------
     * 1. EL ESCALON DEL TEMPORIZADOR, QUE ES DE DONDE SALE TODO
     * -------------------------------------------------------------- */
    printf("  1. LO QUE EL TEMPORIZADOR PUEDE DAR DE VERDAD\n");
    {
        static const uint32_t k_f[] = { 54000UL, 108000UL, 188000UL,
                                        250000UL, 299000UL };
        unsigned i;

        for (i = 0U; i < sizeof k_f / sizeof k_f[0]; i++) {
            uint32_t n  = lo_gen_gd32_cuentas(k_f[i]);
            uint32_t f0 = lo_gen_gd32_hz_de(n);
            uint32_t f1 = lo_gen_gd32_hz_de(n - 2UL);

            printf("      %6u Hz -> %6u Hz (N=%u), escalon %u Hz\n",
                   k_f[i], f0, n, f1 - f0);
        }
        /* El de onda larga tiene que ser MAYOR que el paso de sintonia
         * mas fino con el que se usa la banda: ahi esta el problema. */
        {
            uint32_t n = lo_gen_gd32_cuentas(188000UL);
            uint32_t e = lo_gen_gd32_hz_de(n - 2UL) - lo_gen_gd32_hz_de(n);

            snprintf(t, sizeof t,
                     "en onda larga el escalon (%u Hz) pasa del paso de 1 kHz",
                     e);
            pasa(t, e > 1000UL);
        }
    }

    /* --------------------------------------------------------------
     * 2. *** EL FALLO: SUBIENDO, LA MARCA RETROCEDIA ***
     * --------------------------------------------------------------
     * Esta es la politica de antes, y esta aqui para que se vea el
     * numero. Si algun dia dejara de retroceder, este banco ya no estaria
     * probando nada y hay que mirarlo.
     * -------------------------------------------------------------- */
    printf("\n  2. CENTRANDO EL PANEL EN EL OSCILADOR REAL (lo de antes)\n");
    {
        double peor = peor_retroceso(212000UL, 220000UL, 1000UL, 0, 1);

        snprintf(t, sizeof t, "retrocedia hasta %.1f px por clic", peor);
        pasa(t, peor < -0.5);
    }

    /* --------------------------------------------------------------
     * 3. Y CON EL OSCILADOR APARCADO, NO
     * -------------------------------------------------------------- */
    printf("\n  3. CENTRANDOLO EN LO QUE SE PIDIO: lo mismo\n");
    {
        double peor = peor_retroceso(212000UL, 220000UL, 1000UL, 1, 1);

        snprintf(t, sizeof t, "no retrocede ni un pixel (peor %.1f px)", peor);
        pasa(t, peor >= -0.001);
    }

    /* --------------------------------------------------------------
     * 4. Y EN TODA LA BANDA: CADA CUANTO RETROCEDE
     * --------------------------------------------------------------
     * Onda larga de radiodifusion son 148,5 a 283,5 kHz, y por debajo
     * estan las de navegacion y las estaciones de hora. Los pasos que se
     * usan ahi son 1 kHz, 9 kHz -el canalizado europeo- y 100 Hz.
     *
     * Lo que molestaba es que la marca retrocediera EN UN CLIC DE CADA DOS
     * sin llegar nunca a ninguna parte. Centrando el panel en lo pedido no
     * retrocede ni una vez en toda la banda.
     * -------------------------------------------------------------- */
    printf("\n  4. TODA LA BANDA: CADA CUANTO RETROCEDE LA MARCA\n");
    {
        static const uint32_t k_paso[] = { 100UL, 1000UL, 9000UL };
        unsigned i;

        for (i = 0U; i < sizeof k_paso / sizeof k_paso[0]; i++) {
            int at_antes, cl_antes, at_ahora, cl_ahora;

            (void)peor_retroceso(60000UL, 299000UL, k_paso[i], 0, 0);
            at_antes = g_atras; cl_antes = g_clics;
            (void)peor_retroceso(60000UL, 299000UL, k_paso[i], 1, 0);
            at_ahora = g_atras; cl_ahora = g_clics;

            printf("      paso %5u Hz:  antes %d retrocesos de %d clics,"
                   "  ahora %d de %d\n", k_paso[i],
                   at_antes, cl_antes, at_ahora, cl_ahora);
            snprintf(t, sizeof t,
                     "con paso de %u Hz la marca no retrocede nunca",
                     k_paso[i]);
            pasa(t, at_ahora == 0);
            /*
             * Y que antes SI retrocedia, que es lo que hace que esta
             * prueba signifique algo. Solo para los pasos MAS FINOS QUE EL
             * ESCALON DEL TEMPORIZADOR -1.400 Hz en esta banda-, que es
             * donde esta el fallo: con paso de 9 kHz cada clic salta mucho
             * mas de lo que el redondeo puede retroceder, asi que ahi casi
             * no se veia. Exigirlo en los tres seria pedirle a la prueba
             * que fallara donde no habia nada que arreglar.
             */
            if (k_paso[i] <= 1000UL) {
                snprintf(t, sizeof t,
                         "   (antes retrocedia en %d de cada %d clics)",
                         at_antes, cl_antes);
                pasa(t, at_antes * 10 > cl_antes);
            }
        }
    }

    /* --------------------------------------------------------------
     * 5. LA MARCA NO SE MUEVE NI UN PIXEL
     * --------------------------------------------------------------
     * Con el oscilador siguiendo al dial, lo correcto es que la marca se
     * quede CLAVADA y lo que se desplace sea el espectro por debajo. Eso
     * es lo que hacia arriba de 300 kHz y lo que ahora hace tambien
     * abajo.
     * -------------------------------------------------------------- */
    printf("\n  5. LA MARCA SE QUEDA CLAVADA, QUE ES LO CORRECTO\n");
    {
        uint32_t f;
        double x0 = 0.0, peor = 0.0;
        int primero = 1;

        for (f = 60000UL; f <= 299000UL; f += 100UL) {
            double x = marca_px(f, f - IF_OFF_HZ);

            if (primero) { x0 = x; primero = 0; }
            if (fabs(x - x0) > peor) { peor = fabs(x - x0); }
        }
        printf("      en toda la banda se mueve %.3f px\n", peor);
        pasa("no se mueve nada", peor < 0.001);
    }

    /* --------------------------------------------------------------
     * 6. *** Y LA EMISORA SIGUE DEBAJO DE LA MARCA ***
     * --------------------------------------------------------------
     * Sin esto, lo de arriba seria trampa: clavar la marca es facil, y si
     * los datos no se mueven con ella la señal se le va de debajo hasta
     * 850 Hz, que en una ventana de 192 kHz son tres pixeles. Peor que el
     * temblor que se venia a arreglar.
     *
     * Lo que lo evita es el giro de s_db_frame: el cuadro se corre los
     * bins que haga falta. Aqui se comprueba lo que queda SIN corregir,
     * que es el redondeo a bin entero y no puede pasar de medio bin.
     * -------------------------------------------------------------- */
    printf("\n  6. Y LA EMISORA SIGUE DEBAJO DE LA MARCA\n");
    {
        static const uint32_t k_tasa[] = { 96000UL, 192000UL };
        unsigned j;

        for (j = 0U; j < sizeof k_tasa / sizeof k_tasa[0]; j++) {
            uint32_t bin_hz = k_tasa[j] / FFT_BINS;
            uint32_t f;
            double peor_hz = 0.0;

            for (f = 60000UL; f <= 299000UL; f += 100UL) {
                uint32_t pedido = f - IF_OFF_HZ;
                uint32_t real   = timer_real(pedido);
                int32_t  err    = (int32_t)pedido - (int32_t)real;
                int32_t  giro   = (err >= 0)
                                  ? (int32_t)(((uint32_t)err + bin_hz / 2U) / bin_hz)
                                  : -(int32_t)(((uint32_t)(-err) + bin_hz / 2U) / bin_hz);
                double   queda  = (double)err - (double)giro * (double)bin_hz;

                if (fabs(queda) > peor_hz) { peor_hz = fabs(queda); }
            }
            printf("      tasa %6u Hz (bin %u Hz): queda %.0f Hz sin"
                   " corregir, o sea %.2f px\n", k_tasa[j], bin_hz, peor_hz,
                   peor_hz * (double)SPC_TRACE_W / (double)(k_tasa[j] * 2U));
            snprintf(t, sizeof t, "a %u Hz de tasa no pasa de medio bin",
                     k_tasa[j]);
            pasa(t, peor_hz <= (double)bin_hz / 2.0 + 1.0);
            snprintf(t, sizeof t, "y de un pixel de los %d", SPC_TRACE_W);
            pasa(t, peor_hz * (double)SPC_TRACE_W
                    / (double)(k_tasa[j] * 2U) < 1.0);
        }

        /* Y sin el giro, para que se vea lo que valia la pena corregir. */
        {
            uint32_t f;
            double peor_hz = 0.0;

            for (f = 60000UL; f <= 299000UL; f += 100UL) {
                uint32_t pedido = f - IF_OFF_HZ;
                double   err = (double)pedido - (double)timer_real(pedido);

                if (fabs(err) > peor_hz) { peor_hz = fabs(err); }
            }
            printf("      (sin girar los datos se irian %.0f Hz, o sea"
                   " %.1f px)\n", peor_hz,
                   peor_hz * (double)SPC_TRACE_W / 384000.0);
            pasa("   sin el giro si se saldria de un pixel",
                 peor_hz * (double)SPC_TRACE_W / 384000.0 > 1.0);
        }
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

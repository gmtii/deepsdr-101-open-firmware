/*
 * LO QUE CUESTA EL CAMINO DE AUDIO, EN INSTRUCCIONES.
 *
 * EL PORQUE: el cuadro del espectro no se pinta en el tiempo que sobra, se
 * pinta en lo que queda DESPUES del audio. Con la carga de audio al 42 % y
 * un dibujo de 21 ms, el cuadro sale a 21/(1-0,42) = 36 ms. Si algo sube
 * esa carga al 60 %, el mismo dibujo da 52 ms: la cascada se va a la mitad
 * sin que el dibujo haya cambiado ni un pixel.
 *
 * O sea que el multiplicador no esta en el dibujo. Esta aqui. Por eso hace
 * falta este banco: para saber cuanto de cada segundo se lleva cada trozo
 * del camino de audio, en la CPU de verdad.
 *
 * Los bloques de audio llegan a 96.000/256 = 375 por segundo. Un tanto por
 * ciento es: instrucciones x 375 / 200.000.000.
 */
#include "semi.h"
void pila_di(unsigned long maximo);
#include "nr_ss.h"
#include "nco.h"
#include "iqbal.h"

int g_fallos;

static float s_a[NR_SS_BLOCK_SAMPLES];

/* El tanto por ciento de un segundo que se lleva algo que corre una vez por
 * bloque de audio. 375 bloques por segundo a 200 MHz. */
static void porciento(unsigned long ins)
{
    unsigned long milesimas = (ins * 375UL) / 200000UL;   /* por mil */
    pr("      se lleva ");
    prn(milesimas / 10UL); pr(",");
    prn(milesimas % 10UL);
    pr(" % de cada segundo\n");
}

int main(void)
{
    unsigned long a, b, v;
    int r;
    unsigned i;
    unsigned long vai = 0x1234567u;

    reloj_on();
    nr_ss_init();

    pr("\nEL CAMINO DE AUDIO, EN UN CORTEX-M4 DE VERDAD\n\n");

    for (i = 0U; i < NR_SS_BLOCK_SAMPLES; i++) {
        vai = vai * 1103515245u + 12345u;
        s_a[i] = (float)((long)((vai >> 16) & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
    }

    /* Apagado: lo que cuesta no hacer nada, para restarlo. */
    nr_ss_set_enabled(0U);
    a = tic(); for (r = 0; r < 100; r++) { nr_ss_process(s_a, NR_SS_BLOCK_SAMPLES); } b = tic();
    v = mide("el NR apagado                  ", a, b, 100);
    porciento(v);

    nr_ss_set_enabled(1U);
    nr_ss_set_strength(200U);
    nr_ss_reset();
    /* Unas cuantas pasadas antes de medir: el estimador de ruido arranca
     * y la primera pasada no es la que se repite 375 veces por segundo. */
    for (r = 0; r < 20; r++) { nr_ss_process(s_a, NR_SS_BLOCK_SAMPLES); }
    a = tic(); for (r = 0; r < 100; r++) { nr_ss_process(s_a, NR_SS_BLOCK_SAMPLES); } b = tic();
    v = mide("el NR encendido, fuerza 200    ", a, b, 100);
    porciento(v);
    tope("NR a 200", v, 60000UL);

    nr_ss_set_strength(1000U);
    for (r = 0; r < 20; r++) { nr_ss_process(s_a, NR_SS_BLOCK_SAMPLES); }
    a = tic(); for (r = 0; r < 100; r++) { nr_ss_process(s_a, NR_SS_BLOCK_SAMPLES); } b = tic();
    v = mide("el NR encendido, fuerza 1000   ", a, b, 100);
    porciento(v);

    /*
     * EL MEZCLADOR DEL NCO, que es lo que de verdad cambia al girar el
     * dial con el NCO puesto.
     *
     * Con el NCO apagado el oscilador va a la sintonia menos Fs/4 exactos
     * y el giro de la señal es un camino rapido sin una sola
     * multiplicacion: cuatro muestras por vuelta, solo cambios de signo.
     * Con el NCO puesto el desplazamiento es cualquier numero, y entonces
     * es una multiplicacion compleja POR MUESTRA, con su seno y su coseno
     * interpolados de la tabla.
     *
     * Aqui se mide el camino lento sobre un bloque entero.
     */
    {
        static float bi[512], bq[512];
        nco_t o;

        for (i = 0U; i < 512U; i++) { bi[i] = 0.5f; bq[i] = 0.0f; }
        nco_freq(&o, 3000.0f, 96000.0f);
        nco_fase_cero(&o);
        a = tic(); for (r = 0; r < 50; r++) { nco_mezcla(&o, bi, bq, 256U); } b = tic();
        v = mide("el mezclador, 256 muestras     ", a, b, 50);
        porciento(v);

        a = tic(); for (r = 0; r < 50; r++) { nco_mezcla(&o, bi, bq, 512U); } b = tic();
        v = mide("el mezclador, 512 muestras     ", a, b, 50);
        porciento(v);
    }

    /*
     * Y EL CORRECTOR DE ESPEJO, quieto y aprendiendo. Girar el dial con el
     * NCO lo pone a aprender en cuanto el desplazamiento se va del centro.
     */
    {
        static int16_t ci[512], cq[512];
        static int16_t cr[1024];

        for (i = 0U; i < 512U; i++) {
            vai = vai * 1103515245u + 12345u;
            ci[i] = (int16_t)((vai >> 16) & 0x3FFFu);
            cq[i] = (int16_t)((vai >> 8) & 0x3FFFu);
            cr[2U * i] = ci[i];
            cr[2U * i + 1U] = cq[i];
        }
        iqbal_init();
        iqbal_set_on(1U);
        iqbal_aprende(0U);
        a = tic(); for (r = 0; r < 50; r++) { iqbal_aplica(ci, cq, 256U); } b = tic();
        v = mide("el espejo, solo corrigiendo    ", a, b, 50);
        porciento(v);

        iqbal_aprende(1U);
        a = tic(); for (r = 0; r < 50; r++) { iqbal_mide(ci, cq, 256U);
                                             iqbal_aplica(ci, cq, 256U); } b = tic();
        v = mide("el espejo, APRENDIENDO         ", a, b, 50);
        porciento(v);
    }

    pr("\n");
    pila_di(4096UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}

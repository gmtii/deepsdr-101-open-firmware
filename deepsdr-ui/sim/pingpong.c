/*
 * EL PING-PONG DE SALIDA, MEDIDO. 26/10/2026.
 *
 * QUE SE MIDE Y POR QUE.
 *
 * gd32_i2s_stream_write_half() elige la mitad del buffer de transmision con
 * UNA FOTO del contador del DMA, sin memoria:
 *
 *     remaining    = DMA_CHCNT(DMA0, CH4)
 *     pos          = total_palabras - remaining
 *     playing_half = (pos < palabras_por_mitad) ? 0 : 1
 *     write_half   = 1 - playing_half
 *
 * El comentario largo de esa funcion sospecha que, si el procesado tarda
 * distinto de un bloque a otro, la eleccion puede DEJAR DE ALTERNAR: una
 * mitad se escribe dos veces y la otra repite 2,67 ms ya sonados, o sea un
 * peine de armonicos a 375 Hz.
 *
 * ESTE BANCO NO OPINA: cuenta. Reproduce el hardware -dos canales de DMA
 * circulares colgados del MISMO reloj de trama del codec- y mete jitter de
 * procesado a mano.
 *
 * LO QUE HAY QUE TENER EN LA CABEZA PARA LEER LOS NUMEROS, y es lo que el
 * comentario del firmware no dice:
 *
 *   1. RX (DMA0/CH3) y TX (DMA0/CH4) avanzan A LA MISMA VELOCIDAD, porque
 *      los dos cuentan palabras del mismo BCLK/WS que manda el codec. Las
 *      dos geometrias son identicas: 4*F palabras en total, 2*F por mitad.
 *      Asi que la diferencia de posicion entre los dos canales, fi, es
 *      CONSTANTE. No deriva.
 *
 *   2. fi la fija el armado, y en apply_demod_mode() los dos canales se
 *      arman con el codec en reset -sin BCLK-, uno detras de otro:
 *      sdr_rx_bringup() y despues gd32_i2s_stream_arm(). O sea fi = 0, mas
 *      la palabra que el I2S de transmision se lleva por adelantado en
 *      cuanto se le enciende la peticion de DMA (TBE ya esta a 1). fi = +1.
 *
 *   3. La interrupcion salta cuando pos_RX cruza 0 o 2*F. Con fi = +1, en
 *      ese instante pos_TX vale 1 o 2*F+1: PEGADO A LA FRONTERA. Lo que
 *      separa la llamada de la frontera no es fi, es el PROCESADO.
 *
 * O sea que la variable que decide es "donde cae la escritura dentro de la
 * mitad", que es exactamente lo que s_half_min/s_half_max miden. Este banco
 * barre esa variable entera y dice donde se rompe.
 *
 *   make pingpong FIRMWARE=...
 */
#include "sdr_rx.h"
#include <stdio.h>
#include <string.h>

static int fallos;

static void di(const char *q, int ok, const char *det)
{
    printf("    %-56s %s%s%s\n", q, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* ------------------------------------------------------------------ */
/* El modelo                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned repes;        /* lo que cuenta s_half_repes */
    unsigned min;          /* s_half_min */
    unsigned max;          /* s_half_max */
    unsigned mitad_doble;  /* mitades escritas dos veces seguidas */
    unsigned pisadas;      /* la copia alcanza al DMA leyendo esa mitad */
    unsigned bloques;
} resultado_t;

/*
 * Un generador de ruido reproducible. Nada de rand(): un banco que no da el
 * mismo numero dos veces no sirve para comparar un cambio.
 */
static unsigned s_sem;
static double uniforme(void)   /* en [-1, +1] */
{
    s_sem = s_sem * 1103515245u + 12345u;
    return ((double)((s_sem >> 8) & 0xFFFFFFu) / 8388608.0) - 1.0;
}

/*
 * LA LOGICA DEL FIRMWARE, COPIADA TAL CUAL.
 *
 * `pos` son palabras transferidas por el DMA de transmision desde el
 * principio del buffer, 0 .. 4F-1. Es lo mismo que calcula
 * gd32_i2s_stream_write_half() con total - DMA_CHCNT.
 */
static void elige(unsigned pos, unsigned pal_mitad,
                  unsigned *playing, unsigned *write, unsigned *dentro)
{
    *playing = (pos < pal_mitad) ? 0u : 1u;
    *write   = 1u - *playing;
    *dentro  = pos - (*playing * pal_mitad);
}

/*
 * Simula `bloques` bloques.
 *
 *   F          tramas por mitad (512 en WFM, 256 en el resto)
 *   fs         tramas por segundo (192000 / 96000)
 *   fi         desfase TX-RX en palabras, constante (ver la cabecera)
 *   t_med      tiempo medio del procesado, en fraccion del bloque [0..1]
 *   t_jit      jitter de pico a pico, en fraccion del bloque
 *   deriva     palabras que TX pierde cada bloque (subdesbordamiento):
 *              0 = ninguna. Es lo unico que puede mover fi.
 */
static resultado_t corre(unsigned F, double fs, int fi,
                         double t_med, double t_jit, double deriva,
                         unsigned bloques)
{
    const unsigned PM = 2u * F;        /* palabras por mitad */
    const unsigned PT = 4u * F;        /* palabras totales   */
    /* La copia: memcpy de 2F medias palabras, medido en el firmware en unos
     * 960 ciclos a 200 MHz para F=512. En palabras del I2S, a 2*fs palabras
     * por segundo. */
    const double copia_s = (960.0 * (double)F / 512.0) / 200.0e6;
    const double copia_pal = copia_s * 2.0 * fs;

    resultado_t r;
    unsigned k;
    int ult = -1;
    double perdido = 0.0;

    memset(&r, 0, sizeof r);
    r.min = 0xFFFFFFFFu;
    r.bloques = bloques;

    for (k = 0u; k < bloques; k++) {
        double t = t_med + 0.5 * t_jit * uniforme();
        double pos_d;
        unsigned pos, pl, wr, dentro;

        if (t < 0.0) { t = 0.0; }

        /*
         * Donde esta el DMA de transmision cuando se le llama. La
         * interrupcion del bloque k salta con pos_RX = (k%2)*PM; se le suma
         * el desfase, lo que el procesado ha tardado -en palabras- y lo que
         * TX lleve perdido por subdesbordamiento.
         */
        perdido += deriva;
        pos_d = (double)((k % 2u) * PM) + (double)fi
              + t * (double)PM - perdido;
        while (pos_d < 0.0) { pos_d += (double)PT; }
        pos = (unsigned)pos_d % PT;

        elige(pos, PM, &pl, &wr, &dentro);

        /* Los tres contadores del firmware, con su misma aritmetica. */
        if (ult >= 0 && (unsigned)ult == wr) { r.repes++; r.mitad_doble++; }
        ult = (int)wr;
        if (dentro < r.min) { r.min = dentro; }
        if (dentro > r.max) { r.max = dentro; }

        /*
         * Y LA OTRA FORMA DE ROMPERLO, que no es la misma y nadie cuenta:
         * que la eleccion sea correcta pero el DMA entre en la mitad que se
         * esta copiando antes de que la copia termine. Pasa cuando la
         * llamada cae a menos de `copia_pal` del final de la mitad que suena.
         */
        if ((double)(PM - dentro) < copia_pal) { r.pisadas++; }
    }
    return r;
}

static void fila(const char *q, resultado_t r, unsigned PM)
{
    printf("    %-30s repes %6u  dentro %5u..%-5u (%3.0f..%3.0f %%)  pisadas %u\n",
           q, r.repes, r.min, r.max,
           100.0 * (double)r.min / (double)PM,
           100.0 * (double)r.max / (double)PM,
           r.pisadas);
}

int main(void)
{
    const unsigned FW = (unsigned)SDR_RX_BLOCK_SAMPLES_WFM;   /* 512 */
    const unsigned FA = (unsigned)SDR_RX_BLOCK_SAMPLES;       /* 256 */
    const unsigned PMW = 2u * FW;
    resultado_t r;

    printf("\n  EL PING-PONG DE SALIDA, MEDIDO\n");
    printf("  WFM: %u tramas por mitad, %u palabras, bloque 2,667 ms a 192 kHz\n\n",
           FW, PMW);

    /* ----------------------------------------------------------------
     * 1. REGIMEN NORMAL. El desfase que deja apply_demod_mode() es +1
     *    palabra, y el procesado tarda lo que tarde.
     * ---------------------------------------------------------------- */
    printf("  1. REGIMEN NORMAL (fi = +1 palabra, lo que deja apply_demod_mode)\n");
    printf("     procesado 10..90 %% del bloque, jitter de 20 puntos, 100.000 bloques\n\n");
    {
        double med[] = { 0.10, 0.30, 0.50, 0.70, 0.85, 0.90 };
        unsigned z, malas = 0u;
        char q[64];
        for (z = 0u; z < 6u; z++) {
            s_sem = 12345u;
            r = corre(FW, 192000.0, +1, med[z], 0.20, 0.0, 100000u);
            snprintf(q, sizeof q, "procesado %2.0f %% +-10", 100.0 * med[z]);
            fila(q, r, PMW);
            if (r.repes != 0u && med[z] <= 0.85) { malas++; }
        }
        printf("\n");
        di("hasta el 85 % +-10 del bloque, NUNCA deja de alternar", malas == 0u, 0);
        printf("    El 90 %% +-10 si rompe, y tiene que romper: el borde de\n");
        printf("    arriba del jitter toca el 100 %%, o sea que la interrupcion\n");
        printf("    NO HA TERMINADO cuando llega el bloque siguiente. Eso ya no\n");
        printf("    es un fallo del ping-pong, es la interrupcion pasada de\n");
        printf("    plazo, y tiene su propio contador: sdr_rx_get_tarde().\n");
    }

    /* ----------------------------------------------------------------
     * 2. DONDE SE ROMPE DE VERDAD. Se barre el punto de trabajo hasta
     *    pegarlo a las dos fronteras.
     * ---------------------------------------------------------------- */
    printf("\n  2. DONDE SE ROMPE: el punto de trabajo pegado a la frontera\n");
    printf("     (jitter fijo de 4 puntos = 107 us de pico a pico)\n\n");
    {
        double med[] = { 0.000, 0.005, 0.010, 0.020, 0.050,
                         0.950, 0.980, 0.990, 0.995, 1.000 };
        unsigned z;
        char q[64];
        for (z = 0u; z < 10u; z++) {
            s_sem = 999u;
            r = corre(FW, 192000.0, +1, med[z], 0.04, 0.0, 100000u);
            snprintf(q, sizeof q, "procesado %5.1f %% +-2", 100.0 * med[z]);
            fila(q, r, PMW);
        }
    }

    /* ----------------------------------------------------------------
     * 3. CUANTO JITTER HACE FALTA. Procesado centrado al 50 %, se abre
     *    el jitter hasta que rompe.
     * ---------------------------------------------------------------- */
    printf("\n  3. CUANTO JITTER HACE FALTA, con el procesado centrado al 50 %%\n\n");
    {
        double jit[] = { 0.20, 0.50, 0.80, 0.95, 0.99, 1.00, 1.10, 1.50 };
        unsigned z;
        char q[64];
        for (z = 0u; z < 8u; z++) {
            s_sem = 4242u;
            r = corre(FW, 192000.0, +1, 0.50, jit[z], 0.0, 100000u);
            snprintf(q, sizeof q, "jitter %4.0f %% (%4.2f ms) p-p",
                     100.0 * jit[z], 2.667 * jit[z]);
            fila(q, r, PMW);
        }
        printf("\n    El jitter de pico a pico tiene que acercarse al BLOQUE ENTERO\n");
        printf("    -2,667 ms- para que la eleccion deje de alternar. Y para\n");
        printf("    entonces la interrupcion ya se ha pasado de su plazo, que es\n");
        printf("    un fallo distinto y mas gordo (sdr_rx_get_tarde()).\n");
    }

    /* ----------------------------------------------------------------
     * 4. SI EL PEINE DE 375 Hz FUERA ESTO, ¿CUANTAS REPETICIONES HARIAN
     *    FALTA? Una costura por bloque = peine a 375 Hz a tope. Una cada
     *    N bloques = la misma costura pero 1/N de energia.
     * ---------------------------------------------------------------- */
    printf("\n  4. LA DERIVA, que es lo UNICO que mueve fi: cada palabra que el\n");
    printf("     DMA de transmision no llega a servir (subdesbordamiento) le\n");
    printf("     retrasa una palabra para siempre.\n\n");
    {
        double der[] = { 0.0, 0.1, 1.0, 10.0, 100.0 };
        unsigned z;
        char q[64];
        for (z = 0u; z < 5u; z++) {
            s_sem = 7u;
            r = corre(FW, 192000.0, +1, 0.40, 0.10, der[z], 375u * 60u);
            snprintf(q, sizeof q, "%6.1f palabras perdidas/bloque", der[z]);
            fila(q, r, PMW);
            printf("        -> %6.2f repeticiones por segundo, %5.2f %% de los bloques\n",
                   (double)r.repes / 60.0,
                   100.0 * (double)r.repes / (double)r.bloques);
        }
        printf("\n    Un peine a 375 Hz a tope pide UNA repeticion POR BLOQUE: el\n");
        printf("    100 %% de la columna de la derecha. Con la deriva, la\n");
        printf("    eleccion solo se vuelve loca MIENTRAS fi cruza la frontera,\n");
        printf("    y eso dura (ancho del jitter)/(deriva) bloques. Ni con cien\n");
        printf("    palabras perdidas por bloque -que son 1,5 ms de audio que el\n");
        printf("    DAC no recibe cada 2,67 ms, o sea una averia mucho mayor que\n");
        printf("    la que se persigue- se pasa del 2 %% de los bloques.\n");
    }

    /* ----------------------------------------------------------------
     * 5. AM/BLU, por comparacion: la mitad de palabras, el mismo tiempo.
     * ---------------------------------------------------------------- */
    printf("\n  5. AM/BLU (%u tramas por mitad, 96 kHz, el mismo bloque de 2,667 ms)\n\n", FA);
    {
        s_sem = 55u;
        r = corre(FA, 96000.0, +1, 0.40, 0.20, 0.0, 100000u);
        fila("procesado 40 % +-10", r, 2u * FA);
        di("el margen es el mismo: el bloque dura lo mismo a las dos tasas",
           r.repes == 0u, 0);
    }

    /* ----------------------------------------------------------------
     * 6. LO QUE EL INSTRUMENTO NO DISTINGUE. s_half_min y s_half_max se
     *    quedan con los extremos, y cuando el punto de trabajo esta
     *    PEGADO a la frontera los dos extremos salen separadisimos -0 y
     *    casi 100 %- que es lo mismo que sale cuando el jitter es enorme
     *    y el punto de trabajo esta centrado. Dos averias opuestas, la
     *    misma lectura.
     * ---------------------------------------------------------------- */
    printf("\n  6. LAS DOS LECTURAS QUE SALEN IGUALES Y NO SON LO MISMO\n\n");
    {
        s_sem = 1u;
        r = corre(FW, 192000.0, +1, 0.999, 0.004, 0.0, 100000u);
        fila("pegado a la frontera, jitter 0,2 %", r, PMW);
        s_sem = 1u;
        r = corre(FW, 192000.0, +1, 0.50, 0.99, 0.0, 100000u);
        fila("centrado, jitter 99 %", r, PMW);
        printf("\n    Los dos renglones dicen 'dentro 0..~100 %%'. El primero es\n");
        printf("    una radio con un microsegundo de margen; el segundo es una\n");
        printf("    radio con 1,3 ms de margen por los dos lados. El par\n");
        printf("    min/max no los separa: haria falta la MEDIA, o el minimo de\n");
        printf("    la distancia a la frontera mas cercana sin perder el signo.\n");
    }

    /* ----------------------------------------------------------------
     * 7. Y QUE PASA SI fi NO ES +1. Toda la cabecera de este banco
     *    supone fi = +1 porque es lo que deja apply_demod_mode(): alli
     *    los dos canales se arman con el codec en reset, SIN BCLK, y el
     *    reloj arranca despues, asi que los dos empiezan en cero.
     *
     *    El ARRANQUE EN FRIO no lo hacia asi hasta el 26/10/2026: armaba
     *    el canal de recepcion, encendia BCLK/WCLK dentro de
     *    aic3204_phase2_init() -que son unos 130 ms de I2C a mano mas
     *    hasta 200 ms de sondeo de encendido- y SOLO DESPUES rearmaba el
     *    de transmision desde la palabra cero. O sea que fi salia de lo
     *    que tardara el I2C: un numero cualquiera del circulo, distinto
     *    en cada encendido de la misma radio.
     *
     *    Esto barre fi entero y cuenta en cuantos desfases el ping-pong
     *    se rompe. No es una pregunta academica: lo que decide si la
     *    radio zumba a 375 Hz toda la sesion es ese sorteo.
     * ---------------------------------------------------------------- */
    printf("\n  7. QUE PASA SI EL DESFASE NO ES +1 (el arranque en frio de antes)\n\n");
    {
        static const double k_med[] = { 0.20, 0.30, 0.40, 0.45 };
        static const double k_jit[] = { 0.10, 0.10, 0.20, 0.30 };
        unsigned j;
        unsigned peor_malos = 0u;

        printf("    %-24s %10s %12s %14s\n",
               "procesado", "fi malos", "de 1024", "peor desfase");
        for (j = 0u; j < 4u; j++) {
            unsigned malos = 0u, peor = 0u;
            int f, peor_fi = 0;

            for (f = 0; f < (int)(4u * FA); f++) {
                s_sem = 55u;
                r = corre(FA, 96000.0, f, k_med[j], k_jit[j], 0.0, 4000u);
                if (r.repes != 0u) {
                    malos++;
                    if (r.repes > peor) { peor = r.repes; peor_fi = f; }
                }
            }
            printf("    %-3.0f %% +-%-18.0f %10u %12u %10d (%u repes de 4000)\n",
                   100.0 * k_med[j], 50.0 * k_jit[j], malos, 4u * FA,
                   peor_fi, peor);
            if (malos > peor_malos) { peor_malos = malos; }
        }
        printf("\n    La fraccion de desfases malos es, clavada, el ancho del\n");
        printf("    vaiven del procesado medido sobre la mitad. En el centro de\n");
        printf("    la banda mala se repite una mitad en casi la mitad de los\n");
        printf("    bloques: un peine de 375 Hz casi a tope, toda la sesion.\n");
        printf("\n    Por eso el arranque en frio arma AHORA el canal de salida\n");
        printf("    ANTES de encender BCLK/WCLK, igual que el cambio de tasa en\n");
        printf("    vivo: asi fi vale +1 siempre y nada de esto se sortea.\n");
        di("hay desfases que rompen el ping-pong (por eso fi no se sortea)",
           peor_malos != 0u, 0);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

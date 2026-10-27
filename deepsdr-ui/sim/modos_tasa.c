/*
 * ======================================================================
 * TODOS LOS MODOS A TODAS LAS TASAS - 24/10/2026
 * ======================================================================
 *
 * POR QUE EXISTE, y conviene que quede escrito: porque en dos dias se
 * entregaron cuatro versiones con fallos que encontro el usuario y no los
 * bancos. Los cuatro tienen la misma forma.
 *
 *   el diezmador de medio banda   medido en su banco, perfecto, y nadie
 *                                 pregunto quien alimenta al conversor de
 *                                 salida: medio bloque de audio era
 *                                 memoria de mas alla del array.
 *   el interpolador               puesto "por simetria" con 35 tomas,
 *                                 sin medirlo. Salio mas caro que el
 *                                 diezmador entero.
 *   el NCO                        probado poniendole el desplazamiento a
 *                                 mano, que es saltarse el camino que
 *                                 falla.
 *   el gancho del demodulador     se añadio una tasa nueva y no se
 *                                 repaso que mas daba por supuesto que
 *                                 192 kHz era solo de FM ancha.
 *
 * NINGUNO ERA UN FALLO DIFICIL. Todos eran la misma omision: probar la
 * PIEZA en vez del SISTEMA donde se mete.
 *
 * Esto es la red que faltaba. No mira ninguna pieza: coge cada modo, lo
 * corre a cada tasa, y exige lo minimo que tiene que pasar siempre:
 *
 *   1. que entregue audio, y
 *   2. que entregue EXACTAMENTE las tramas que el conversor pide a esa
 *      tasa -que es el fallo del dia 23-, y
 *   3. que lo que entregue no sea silencio -que es como se ve un camino
 *      que corre pero no demodula-.
 *
 * Es una red basta a proposito. Lo que caza no son fallos finos: es "este
 * modo no va a esta tasa", que es justo lo que se ha escapado cuatro
 * veces.
 */
#include "demod_am.h"
#include "nr_ss.h"
#include "sdr_rx.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

extern void cons_captura_tramas(uint32_t);
extern const int16_t *cons_audio(void);
extern void cons_todos_apagados(void);
extern void cons_reinicia(void);
extern uint32_t g_i2s_tramas;

static int fallos;

static int di(int cond, const char *que, const char *como)
{
    printf("    %-54s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); }
    return cond ? 0 : 1;
}

static int16_t raw[1024 * 2];

static const struct { demod_mode_t modo; const char *nombre; uint8_t wfm; }
    k_modos[] = {
    { DEMOD_MODE_AM,  "AM",  0u },
    { DEMOD_MODE_SAM, "SAM", 0u },
    { DEMOD_MODE_USB, "USB", 0u },
    { DEMOD_MODE_LSB, "LSB", 0u },
    { DEMOD_MODE_NFM, "NFM", 0u },
    { DEMOD_MODE_WFM, "WFM", 1u }
};

/* 0 = 48 kHz, 1 = 96, 2 = 192. Igual que el campo de CONFIG.CSV. */
static const struct { const char *nombre; uint8_t es48; uint8_t doble; }
    k_tasas[] = {
    { " 48 kHz", 1u, 0u },
    { " 96 kHz", 0u, 0u },
    { "192 kHz", 0u, 1u }
};

int main(void)
{
    unsigned m, t;

    cons_todos_apagados();
    cons_reinicia();
    nr_ss_set_enabled(0u);

    printf("\n== TODOS LOS MODOS A TODAS LAS TASAS ==\n\n");
    printf("     %-5s %-8s %8s %8s  %s\n",
           "modo", "tasa", "tramas", "pide", "audio");

    for (m = 0; m < sizeof k_modos / sizeof k_modos[0]; m++) {
        for (t = 0; t < sizeof k_tasas / sizeof k_tasas[0]; t++) {
            /*
             * LA FM ANCHA VA SIEMPRE A 192 kHz: no es elegible. Correrla
             * a otra tasa seria probar algo que la radio no hace.
             */
            uint8_t doble = k_modos[m].wfm ? 1u : k_tasas[t].doble;
            uint8_t es48  = k_modos[m].wfm ? 0u : k_tasas[t].es48;
            uint32_t n, pide;
            double fs;
            unsigned b, k;
            double fase = 0.0, dw, pico = 0.0;

            if (k_modos[m].wfm && t != 2u) { continue; }

            n    = doble ? SDR_RX_BLOCK_SAMPLES_WFM : SDR_RX_BLOCK_SAMPLES;
            fs   = doble ? 192000.0 : (es48 ? 48000.0 : 96000.0);
            pide = n;     /* el conversor pide una trama por muestra cruda */

            demod_am_init();
            demod_am_set_active_rate(es48);
            demod_am_set_rf_doble(doble);
            demod_am_set_mode(k_modos[m].modo);
            /* La FM ancha va centrada; los demas, en su FI baja. */
            demod_am_set_mix_hz(k_modos[m].wfm ? 0.0f : 24000.0f);
            cons_reinicia();
            cons_captura_tramas(n);
            g_i2s_tramas = 0u;

            /* Un tono modulado, para que haya algo que demodular en todos:
             * portadora con envolvente para AM/SAM, desviacion para FM. */
            dw = 2.0 * M_PI * (k_modos[m].wfm ? 1000.0 : 25000.0) / fs;

            for (b = 0; b < 140u; b++) {
                for (k = 0; k < n; k++) {
                    double env = 0.6 + 0.4 * sin(2.0 * M_PI * 400.0
                                                 * (double)k / fs);
                    double ph  = fase;

                    if (k_modos[m].wfm) {
                        /* FM: la fase lleva la modulacion dentro. */
                        ph = fase + 3.0 * sin(2.0 * M_PI * 1000.0
                                              * (double)k / fs);
                    }
                    raw[2 * k]      = (int16_t)(6000.0 * env * cos(ph));
                    raw[2 * k + 1u] = (int16_t)(6000.0 * env * sin(ph));
                    fase += dw;
                }
                if (k_modos[m].wfm) { demod_wfm_process_raw(raw); }
                else                { demod_am_process_raw(raw); }
            }

            {
                const int16_t *a = cons_audio();

                for (k = 0; k < n; k++) {
                    double v = fabs((double)a[2 * k]);

                    if (v > pico) { pico = v; }
                }
            }

            printf("     %-5s %-8s %8u %8u  %8.0f\n",
                   k_modos[m].nombre, k_tasas[t].nombre,
                   (unsigned)g_i2s_tramas, (unsigned)pide, pico);

            {
                char d[128];

                snprintf(d, sizeof d, "%s a %s: entrego %u tramas y el DAC pide %u",
                         k_modos[m].nombre, k_tasas[t].nombre,
                         (unsigned)g_i2s_tramas, (unsigned)pide);
                fallos += di(g_i2s_tramas == pide,
                             "entrega las tramas que pide el conversor", d);

                snprintf(d, sizeof d, "%s a %s: el audio sale a cero",
                         k_modos[m].nombre, k_tasas[t].nombre);
                fallos += di(pico > 10.0, "y lo que entrega no es silencio", d);
            }
        }
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

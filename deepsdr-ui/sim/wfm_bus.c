/*
 * ======================================================================
 * EL BUS DEL PANEL, POR CUADRO Y POR SEGUNDO, EN FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * QUE PREGUNTA CONTESTA
 * ---------------------
 * El zumbido de WFM se va al abrir el menu de ajustes. El menu NO para el
 * audio: para el DIBUJO del panel (sdr_spectrum_waterfall_tick() salta su
 * bloque con s_menu_open) y para la marquesina del RDS (rds_marq_poll()
 * vuelve en la primera linea si s_menu_open). O sea que lo que se va con el
 * menu es exactamente el trafico de este banco.
 *
 * Asi que la pregunta es: CUANTO trafico es, repartido en quien lo mete.
 *
 * COMO SE MIDE
 * ------------
 * lcd_sim cuenta accesos de 16 bits al bus EXMC: 1 por pixel volcado y 17
 * por ventana abierta (CASET x4 + RASET x4, cada uno cmd+dato, mas RAMWR).
 * Se llama a las MISMAS funciones del firmware que llama main.c, con la
 * misma geometria, y se cuenta.
 *
 * El tiempo sale de la temporizacion real del bus: el registro que deja el
 * cargador es ASET=2, DSET=5 (ver rm68120_exmc.c, rama CARGADOR_ARRANQUE),
 * o sea (2+1)+(5+1) = 9 ciclos de AHB por acceso, y el AHB va a 200 MHz sin
 * dividir -> 45 ns por acceso. Ese es el mismo 45 ns que lcd_dma.c escribe
 * en el comentario de su tope de espera.
 *
 * LO QUE ESTE BANCO NO MIDE: los accesos a la SRAM (los del IPA montando la
 * banda y los del DMA leyendola). Aqui solo esta el lado del panel. Pero el
 * numero de pixeles es el mismo en los dos lados, asi que la columna de
 * pixeles vale para los dos.
 *
 *   make wfmbus FIRMWARE=..
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "waterfall.h"
#include "ui_top.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

/* --- geometria, la de main.c ---------------------------------------- */
#define PX   SPC_TRACE_X     /* 40  */
#define PY   SPC_TRACE_Y     /* 108 */
#define PW   SPC_TRACE_W     /* 756 */
#define PH   SPC_TRACE_H     /* 208 */
#define WFY  (104 + 240 + 4) /* 348, el WF_Y de main.c */

/* El bus de verdad: 9 ciclos por acceso a 200 MHz = 45 ns. */
#define CICLOS_ACC   9u
#define AHB_MHZ      200.0

#define BINS_WFM     512     /* SDR_RX_BLOCK_SAMPLES_WFM  */
#define BINS_OTROS   256     /* AM / BLU / NFM            */

static float g_db[BINS_WFM];
static uint8_t g_line[WATERFALL_WIDTH];

/*
 * Dos trazas, y la diferencia entre ellas es la que importa.
 *
 * WFM: la emisora ocupa 180 de los 192 kHz de ventana, o sea la MITAD
 * CENTRAL del panel, y su contenido salta de un cuadro a otro porque es
 * modulacion de verdad. El resto es suelo.
 *
 * AM/BLU: la señal son unos pocos kilohercios de los 96 de ventana -entre
 * 20 y 30 columnas de 756- y el resto es suelo de ruido.
 *
 * Las dos pasan despues por el suavizado temporal de main.c (alfa 0,75
 * sobre la traza anterior), que es lo que de verdad decide cuantas bandas
 * se puede saltar spectrum_draw().
 */
static float g_traza[BINS_WFM];
static int   g_traza_lista = 0;

static void suaviza(int n)
{
    int i;
    if (!g_traza_lista) {
        for (i = 0; i < n; i++) { g_traza[i] = g_db[i]; }
        g_traza_lista = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        g_traza[i] = 0.75f * g_traza[i] + 0.25f * g_db[i];
    }
}

static void senal_wfm(int n, int sem)
{
    int i;
    unsigned s = (unsigned)sem * 2654435761u + 7u;

    for (i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        g_db[i] = -102.0f + (float)((s >> 16) % 9u);
    }
    for (i = n / 4; i < (3 * n) / 4; i++) {          /* 180 de 192 kHz */
        s = s * 1103515245u + 12345u;
        g_db[i] = -38.0f + (float)((s >> 18) % 13u); /* la modulacion salta */
    }
    suaviza(n);
}

static void senal_estrecha(int n, int sem)
{
    int i;
    unsigned s = (unsigned)sem * 2654435761u + 7u;
    int c = n / 2, anch = (n * 6) / 96;              /* 6 kHz de 96 */

    for (i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        g_db[i] = -102.0f + (float)((s >> 16) % 9u);
    }
    for (i = c - anch; i < c + anch; i++) {
        s = s * 1103515245u + 12345u;
        g_db[i] = -52.0f + (float)((s >> 18) % 7u);
    }
    suaviza(n);
}


static void linea(int n, int sem)
{
    int i;
    unsigned s = (unsigned)sem * 69069u + 1u;

    for (i = 0; i < WATERFALL_WIDTH; i++) {
        s = s * 1103515245u + 12345u;
        g_line[i] = (uint8_t)((s >> 16) & 0xFFu);
    }
    (void)n;
}

typedef struct { double acc, win, px, over; } med_t;

static med_t mide_fin(int n)
{
    lcd_cost_t c = lcd_sim_cost();
    med_t m;
    m.acc  = (double)c.bus_writes / n;
    m.win  = (double)c.windows    / n;
    m.px   = (double)c.pixels     / n;
    m.over = (double)c.overdraw   / n;
    return m;
}

static void fila(const char *q, med_t m, double hz)
{
    double us = m.acc * (double)CICLOS_ACC / AHB_MHZ;
    printf("  %-28s %9.0f %7.1f %10.0f   %7.1f us   %6.0f %6.2f %%\n",
           q, m.acc, m.win, m.px, us, hz, 100.0 * us * hz / 1e6);
}

int main(void)
{
    const int N = 120;
    int k;
    med_t m_esp_wfm, m_esp_otros, m_wf, m_cro, m_reg, m_rds, m_cab, m_smt;
    double t_wfm, t_otros;

    lcd_sim_reset(0x0000);
    spectrum_init();
    spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
    spectrum_set_line_smooth(0u);
    spectrum_set_bridge(1u);
    waterfall_init();

    printf("\n");
    printf("=====================================================================\n");
    printf(" LO QUE EL REPINTADO METE EN EL BUS DEL PANEL, POR CUADRO\n");
    printf("=====================================================================\n");
    printf("\n  Bus: %u ciclos por acceso a %.0f MHz = %.1f ns\n\n",
           CICLOS_ACC, AHB_MHZ, 1000.0 * CICLOS_ACC / AHB_MHZ);
    printf("  %-28s %9s %7s %10s %12s %8s %8s\n",
           "", "accesos", "vent.", "pixeles", "tiempo", "veces/s", "del bus");
    printf("  ---------------------------------------------------------------"
           "--------------------------\n");

    /* --- 1. el espectro en WFM: 512 bins, emisora ancha ------------- */
    g_traza_lista = 0;
    for (k = 0; k < 20; k++) {           /* que el suavizado se asiente */
        senal_wfm(BINS_WFM, k + 1);
        spectrum_draw(g_traza, BINS_WFM, PX, PY, PW, PH, -110.0f, -20.0f,
                      0, 0u, 0, 0);
    }
    lcd_sim_cost_reset();
    for (k = 0; k < N; k++) {
        senal_wfm(BINS_WFM, k + 100);
        spectrum_draw(g_traza, BINS_WFM, PX, PY, PW, PH, -110.0f, -20.0f,
                      0, 0u, 0, 0);
    }
    m_esp_wfm = mide_fin(N);

    /* --- 2. el espectro en AM/BLU: 256 bins, emisora estrecha ------- */
    spectrum_invalida();
    g_traza_lista = 0;
    for (k = 0; k < 20; k++) {
        senal_estrecha(BINS_OTROS, k + 1);
        spectrum_draw(g_traza, BINS_OTROS, PX, PY, PW, PH, -110.0f, -20.0f,
                      0, 1u, -20, 20);
    }
    lcd_sim_cost_reset();
    for (k = 0; k < N; k++) {
        senal_estrecha(BINS_OTROS, k + 100);
        spectrum_draw(g_traza, BINS_OTROS, PX, PY, PW, PH, -110.0f, -20.0f,
                      0, 1u, -20, 20);
    }
    m_esp_otros = mide_fin(N);

    /* --- 3. la cascada: una linea nueva y el volcado ENTERO --------- */
    lcd_sim_cost_reset();
    for (k = 0; k < N; k++) {
        linea(0, k + 1);
        waterfall_push_line(g_line);
        waterfall_blit(PX, WFY, spectrum_colormap_lut());
    }
    m_wf = mide_fin(N);

    /* --- 4. el cromo entero y solo la regla ------------------------- */
    {
        spec_chrome_t st;
        memset(&st, 0, sizeof st);
        st.db_min = -110; st.db_max = -20;
        st.center_hz = 100300000u; st.span_hz = 192000u;
        st.demod_px = 0;
        st.band = SPC_BAND_NONE; st.band_hz = 0;
        st.cmap = spectrum_colormap_lut();

        lcd_sim_cost_reset();
        spec_chrome_draw(&st);
        m_cro = mide_fin(1);

        lcd_sim_cost_reset();
        spec_chrome_draw_axis(&st);
        m_reg = mide_fin(1);
    }

    /* --- 5. la marquesina del RDS, que SOLO corre en WFM ------------ */
    {
        ui_top_state_t st;
        static const char *txt =
            "12:41 26/10   MUSICA POP   RADIO 3 - Ahora suena algo que "
            "lleva un rato sonando y el radiotexto es largo de verdad     ";
        memset(&st, 0, sizeof st);
        st.freq = "100.300,00"; st.freq_digit = 4; st.mode = "WFM";
        st.band = "FM"; st.en_banda = 1u; st.fam_banda = 1u;
        st.clock = "12:41"; st.batt_pct = 78u;
        st.s_units = 9u; st.dbm = -42; st.dbm_valid = 1u;
        st.knob = UI_KNOB_VOLUME; st.knob_value = "18";
        st.agc = "MED"; st.bw = 0; st.att = "OFF";
        st.rds = txt;
        st.rds_nombre = "RADIO 3";

        /*
         * El barrido entero del desplazamiento, incluida la parte NEGATIVA:
         * main.c reinicia con s_rds_off = -ui_top_rds_ancho() cuando el
         * texto da la vuelta, asi que la marquesina pasa de verdad por ahi.
         * Si alguna primitiva escribiera por debajo del principio de la
         * banda, caeria dentro de s_stream_buf, que esta 28 bytes antes.
         */
        lcd_sim_cost_reset();
        for (k = -(int)ui_top_rds_ancho() - 8; k < N; k++) {
            st.rds_off = (int16_t)(k * 2);     /* RDS_MARQ_PX = 2 */
            ui_top_draw_rds(&st);
        }
        m_rds = mide_fin(N + (int)ui_top_rds_ancho() + 8);

        lcd_sim_cost_reset();
        ui_top_draw(&st);
        m_cab = mide_fin(1);

        /*
         * Y EL MEDIDOR DE S, que NO se apaga con el menu (ver
         * franja_arriba_tick(): solo se calla con la captura encima o la
         * pantalla dormida). Se mide para poder DESCARTARLO: lo que cambia
         * al abrir el menu no puede ser esto.
         */
        lcd_sim_cost_reset();
        ui_top_draw_smeter(&st);
        m_smt = mide_fin(1);
    }

    /*
     * Cadencias. El cuadro del panel sale de SPECTRUM_FRAME_MS = 22 ms con
     * el vaiven de -2..+2 enganchado a bloques de 2,667 ms: main.c lo mide
     * en 41,7..43,5 cuadros por segundo. Se toma 42.
     * La marquesina va a su propio reloj: RDS_MARQ_MS = 40 -> 25 Hz.
     * El cromo NO se repinta por cuadro (spec_chrome_tick() compara y solo
     * saca lo que cambia); se pone su peor caso a 1 Hz para tenerlo al lado.
     */
    fila("espectro WFM (512 bins)",  m_esp_wfm,   42.0);
    fila("espectro AM/BLU (256)",    m_esp_otros, 42.0);
    fila("cascada (volcado entero)", m_wf,        42.0);
    fila("cromo entero (peor caso)", m_cro,        1.0);
    fila("   de el, solo el eje dB",  m_reg,        1.0);
    fila("marquesina RDS (solo WFM)",m_rds,       25.0);
    fila("cabecera entera",          m_cab,        1.0);
    fila("medidor S (NO lo tapa el menu)", m_smt,  42.0);

    printf("\n");
    t_wfm = (m_esp_wfm.acc + m_wf.acc) * 42.0 + m_rds.acc * 25.0;
    t_otros = (m_esp_otros.acc + m_wf.acc) * 42.0;
    printf("  ===============================================================\n");
    printf("  TOTAL por segundo, escuchando:\n\n");
    printf("    WFM        %11.0f accesos/s  = %6.1f ms/s  (%4.1f %% del bus)\n",
           t_wfm, t_wfm * CICLOS_ACC / AHB_MHZ / 1000.0,
           100.0 * t_wfm * CICLOS_ACC / AHB_MHZ / 1e6);
    printf("    AM / BLU   %11.0f accesos/s  = %6.1f ms/s  (%4.1f %% del bus)\n",
           t_otros, t_otros * CICLOS_ACC / AHB_MHZ / 1000.0,
           100.0 * t_otros * CICLOS_ACC / AHB_MHZ / 1e6);
    /* NO es cero: el medidor de S sigue corriendo con el menu abierto
     * -franja_arriba_tick() solo se calla con la captura encima o la
     * pantalla dormida-. La version anterior de este renglon decia 0 a
     * mano y contradecia su propia fila de arriba. 26/10/2026. */
    {
        double t_menu = m_smt.acc * 42.0;

        printf("    menu abierto %11.0f accesos/s  = %6.1f ms/s  (%4.1f %% del bus)\n",
               t_menu, t_menu * CICLOS_ACC / AHB_MHZ / 1000.0,
               100.0 * t_menu * CICLOS_ACC / AHB_MHZ / 1e6);
    }
    printf("\n    Diferencia WFM - AM:  %+.0f accesos/s (%+.1f %%)\n",
           t_wfm - t_otros, 100.0 * (t_wfm - t_otros) / t_otros);
    printf("  ===============================================================\n");

    /*
     * Y EL OTRO LADO DEL BUS, el que no se ve desde el panel: cada pixel
     * que sale por EXMC ha sido antes ESCRITO en s_band (SRAM0) y despues
     * LEIDO de s_band. El que escribe es el IPA (cascada) o la CPU
     * (espectro, marquesina); el que lee es el DMA1/CH0.
     */
    printf("\n  EL OTRO LADO: accesos a la SRAM por los mismos pixeles\n\n");
    {
        double px_s = (m_esp_wfm.px + m_wf.px) * 42.0 + m_rds.px * 25.0;
        printf("    pixeles por segundo         %11.0f\n", px_s);
        printf("    escrituras de 16 b en SRAM0 %11.0f  (montar la banda)\n", px_s);
        printf("    lecturas  de 16 b en SRAM0  %11.0f  (DMA1/CH0 leyendola)\n", px_s);
        printf("    lecturas  de  8 b en SRAM1  %11.0f  (IPA leyendo la cascada)\n",
               m_wf.px * 42.0);
        printf("\n    Y el audio vive en la MISMA SRAM0: s_stream_buf esta en\n");
        printf("    0x200050e4 y s_band en 0x20006100, pegados.\n");

        /*
         * ==============================================================
         * Y AHORA EL REPARTO, QUE ES LO QUE DESCARTA LA TEORIA FACIL.
         * ==============================================================
         *
         * El AHB va a 200 MHz y un acceso de la SRAM se sirve en un
         * ciclo: 200 millones de accesos por segundo de capacidad en el
         * puerto de SRAM0. Contra eso:
         */
        {
            double cap   = 200e6;
            double disp  = 2.0 * px_s;                 /* montar + leer   */
            double audio = 2.0 * 192000.0 * 2.0;       /* TX + RX, estereo*/

            printf("\n    capacidad del puerto SRAM0   %11.0f acc/s  100,00 %%\n", cap);
            printf("    lo que mete el dibujo        %11.0f acc/s  %6.2f %%\n",
                   disp, 100.0 * disp / cap);
            printf("    lo que pide el audio         %11.0f acc/s  %6.2f %%\n",
                   audio, 100.0 * audio / cap);
            printf("\n    El DMA del audio pide UNA media palabra cada %.2f us y\n",
                   1e6 / (2.0 * 192000.0));
            printf("    la espera peor por arbitraje son unos pocos ciclos (%.2f us\n",
                   10.0 / 200.0);
            printf("    con diez). Hay %0.0f veces mas margen del que haria falta\n",
                   (1e6 / (2.0 * 192000.0)) / (10.0 / 200.0));
            printf("    para que una palabra llegue tarde. O sea que la ANCHURA\n");
            printf("    DE BANDA no es el mecanismo: ni pintando el doble.\n");
        }

        /*
         * LO QUE SI ES PERIODICO Y SOLO PASA EN WFM.
         */
        {
            double tb = 1e6 / 375.0;   /* un bloque de audio, en us */

            printf("\n  CADENCIAS, EN BLOQUES DE AUDIO (un bloque = %.3f ms)\n\n", tb / 1000.0);
            printf("    cuadro del panel   22 ms + vaiven de -2..+2  ->  %.2f .. %.2f bloques\n",
                   20000.0 / tb, 24000.0 / tb);
            printf("                       main.c lo DESPARRAMA a proposito (ver el\n");
            printf("                       comentario EL VAIVEN DEL FOTOGRAMA)\n");
            printf("    marquesina RDS     40 ms +-2 desde el 26/10         ->  %.2f bloques\n",
                   40000.0 / tb);
            printf("                       (medido en sim/wfm_marquesina.c: con el bucle\n");
            printf("                        lento ese vaiven NO desengancha nada)\n");
        }
    }
    printf("\n");
    return 0;
}

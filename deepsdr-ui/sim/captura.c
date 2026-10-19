/*
 * LA CAPTURA DE PANTALLA TIENE QUE SALIR IGUAL QUE LA PANTALLA.
 *
 * Pedido por el usuario.
 *
 * Esta radio no tiene copia de la pantalla en memoria -son 768 kB y quedan
 * menos de tres-, asi que una captura no es volcar lo que hay: es VOLVER A
 * DIBUJAR la pantalla entera, franja por franja, metiendola en el fichero.
 *
 * Y eso abre justo el fallo que este banco vigila: que el redibujado no se
 * parezca a lo que habia en la pantalla. Es el mismo peligro que una copia
 * de codigo -el dia que alguien toque una de las dos, se separan-, con el
 * agravante de que la captura no se mira todos los dias y la separacion
 * puede durar meses sin que nadie la note.
 *
 * COMO SE COMPRUEBA, y es la unica forma que no depende de que yo me acuerde
 * de mirar un elemento concreto: se pinta la pantalla ENTERA por el camino
 * de siempre, se guarda, se monta la captura franja por franja por el camino
 * nuevo, y se comparan PIXEL A PIXEL. Cero diferencias o nada.
 *
 * Asi no hay que enumerar capas. El dia que alguien anada un elemento a la
 * pantalla y se olvide de meterlo en la captura, esto lo dice.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "ui_top.h"
#include "ui_act.h"
#include "waterfall.h"
#include "specdata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

#define FFT_BINS 796
static float   g_db[FFT_BINS];
static uint8_t g_lin[SPC_TRACE_W];

static float DB_MIN = -110.0f;
static float DB_MAX =  -20.0f;

static ui_top_state_t  top;
static ui_act_state_t  act;
static spec_chrome_t   ch;

static uint16_t s_pantalla[LCD_W * LCD_H];
static uint16_t s_captura[LCD_W * LCD_H];

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-50s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* El estado de la radio, inventado pero completo. */
static void estado(void)
{
    static const char *nom[UI_ACT_N] = { "Bandas", "Modo", "Paso",
                                         "Volumen", "Ruido", "Ajustes" };
    static const char *val[UI_ACT_N] = { 0, "LSB", "1 kHz",
                                         "-12,0 dB", "activo", 0 };
    int i;

    memset(&top, 0, sizeof top);
    memset(&act, 0, sizeof act);
    memset(&ch, 0, sizeof ch);

    top.freq = "7.265,00"; top.freq_digit = 5;
    top.mode = "LSB"; top.band = "41 m"; top.clock = "22:14";
    top.batt_pct = 72;
    top.sam_ppm = 0;
    /*
     * EL NOMBRE LARGO Y CORRIENDO, NO UNO QUE QUEPA - 07/10/2026.
     *
     * Pedido por el usuario.
     *
     * Este banco llevaba desde el 05/10 comparando captura contra pantalla y
     * saliendo verde con la marquesina rota, porque usaba "RADIO EUROPA" -que
     * cabe de sobra en el hueco- y los dos desplazamientos a cero. Con un
     * texto que cabe no hay nada que recortar, asi que la falta de recorte en
     * X de la captura no se veia.
     *
     * Ahora van los de la emisora de verdad que lo destapo, la XSQ de
     * Guangzhou en 8.421,50, y con los dos offsets a media carrera: asi hay
     * texto que se sale por la IZQUIERDA (lo que se ha ido ya) y por la
     * DERECHA (lo que aun no ha entrado), que son los dos lados que la
     * ventana de gfx2_render() recorta en la pantalla de verdad.
     */
    top.rds_nombre = "XSQ Guangzhou Meteo Fax";
    top.rds        = "CHN - -TY - 00:00-24:00 UTC(+1)";
    top.rds_nom_off = 40;
    top.rds_off     = 70;
    top.s_units = 7; top.over_db = 0; top.dbm = -79; top.dbm_valid = 0;
    top.knob = UI_KNOB_TUNE; top.knob_value = "1 kHz";
    top.agc = "MED"; top.bw = "0-2,7k"; top.att = "-06"; top.nco_on = 1;

    for (i = 0; i < UI_ACT_N; i++) {
        act.name[i] = nom[i]; act.value[i] = val[i];
        act.active[i] = (uint8_t)(i == 4);
    }
    act.pressed = -1;

    ch.db_min = DB_MIN; ch.db_max = DB_MAX;
    ch.center_hz = 7265000U - 96000U / 4U;
    ch.span_hz = 96000U; ch.demod_px = (int16_t)(SPC_TRACE_W / 4);
    ch.band = SPC_BAND_LOWER; ch.band_hz = 2700U;
    ch.cmap = spectrum_colormap_lut();
}

/* El espectro y la cascada, con datos de verdad del generador. */
static void datos(void)
{
    int16_t demod_px = (int16_t)(SPC_TRACE_W / 4);
    int16_t bw_px = (int16_t)((uint32_t)SPC_TRACE_W * 2700U / 96000U);
    int pass, r, i;

    for (pass = 0; pass < 24; pass++) {
        specdata_fill(g_db, FFT_BINS, 1u);
        spectrum_draw(g_db, FFT_BINS, SPC_TRACE_X, SPC_TRACE_Y,
                      SPC_TRACE_W, SPC_TRACE_H, DB_MIN, DB_MAX, demod_px,
                      1U, (int16_t)(demod_px - bw_px), demod_px);
    }
    for (r = 0; r < SPC_WF_ROWS; r++) {
        specdata_fill(g_db, FFT_BINS, (unsigned)(r * 7 + 1));
        for (i = 0; i < SPC_TRACE_W; i++) {
            int src = (int)((long)i * FFT_BINS / SPC_TRACE_W);
            g_lin[i] = spectrum_colormap_index(g_db[src], DB_MIN, DB_MAX);
        }
        waterfall_push_line(g_lin);
    }
}

/* La pantalla por el camino de siempre. */
static void pinta_normal(void)
{
    lcd_sim_reset(0x0000);
    spectrum_draw(g_db, FFT_BINS, SPC_TRACE_X, SPC_TRACE_Y,
                  SPC_TRACE_W, SPC_TRACE_H, DB_MIN, DB_MAX,
                  ch.demod_px, 1U,
                  (int16_t)(ch.demod_px - (int16_t)((uint32_t)SPC_TRACE_W * 2700U / 96000U)),
                  ch.demod_px);
    waterfall_blit(SPC_TRACE_X, SPC_WF_Y, spectrum_colormap_lut());
    spec_chrome_draw(&ch);
    ui_top_draw(&top);
    ui_act_draw(&act);
}

/*
 * Y por el camino de la captura: la MISMA secuencia de capas que capt_pinta()
 * en main.c, banda a banda. Si alguien cambia una de las dos y no la otra,
 * la comparacion de abajo lo dice.
 */
static void pinta_captura(uint16_t *dest)
{
    static uint16_t banda[LCD_W * 16];
    int16_t y;

    for (y = 0; y < (int16_t)LCD_H; y = (int16_t)(y + 16)) {
        gfx2_surf_t s;
        int16_t nf = (int16_t)(LCD_H - y);
        int16_t k;

        if (nf > 16) { nf = 16; }
        s.px = banda; s.x = 0; s.y = y; s.w = (int16_t)LCD_W; s.h = nf;

        gfx2_fill(&s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(0x000000));
        spec_chrome_pinta_en(&s, &ch);
        spectrum_pinta_en(&s, (int16_t)SPC_TRACE_X, (int16_t)SPC_TRACE_Y);
        waterfall_pinta_en(&s, (int16_t)SPC_TRACE_X, (int16_t)SPC_WF_Y,
                           spectrum_colormap_lut());
        ui_top_pinta_en(&s, &top);
        ui_act_pinta_en(&s, &act);

        for (k = 0; k < nf; k++) {
            memcpy(&dest[(size_t)(y + k) * LCD_W], &banda[(size_t)k * LCD_W],
                   (size_t)LCD_W * sizeof dest[0]);
        }
    }
}

int main(void)
{
    size_t i, dif = 0, prim = 0;

    printf("\nLA CAPTURA CONTRA LA PANTALLA\n\n");

    spectrum_init();
    spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
    waterfall_init();
    estado();
    datos();

    pinta_normal();
    memcpy(s_pantalla, lcd_sim_fb(), sizeof s_pantalla);

    pinta_captura(s_captura);

    for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
        if (s_pantalla[i] != s_captura[i]) {
            if (dif == 0U) { prim = i; }
            dif++;
        }
    }

    printf("  %d x %d pixeles\n", (int)LCD_W, (int)LCD_H);
    if (dif != 0U) {
        printf("      primera diferencia en x=%d y=%d: pantalla %04X, captura %04X\n",
               (int)(prim % LCD_W), (int)(prim / LCD_W),
               s_pantalla[prim], s_captura[prim]);
    }
    {
        char det[64];
        snprintf(det, sizeof det, "%d pixeles distintos", (int)dif);
        di("la captura es la pantalla, pixel a pixel", dif == 0U, det);
    }

    /* Y que las dos tengan contenido: si las dos salieran negras, "iguales"
     * se cumpliria solo y esto no probaria nada. */
    {
        size_t vivos = 0U;
        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            if (s_captura[i] != 0U) { vivos++; }
        }
        {
            char det[64];
            snprintf(det, sizeof det, "%d pixeles no negros", (int)vivos);
            di("y no esta vacia", vivos > 100000U, det);
        }
    }

    /*
     * Y QUE NADIE LE ESCRIBA EN LA BANDA MIENTRAS LA TIENE COGIDA.
     *
     * Pedido por el usuario.
     *
     * Y se veia fatal: fondo morado, rayas negras, texto fantasma. No era
     * la captura: era gfx2_wf_blit() -el camino de respaldo de la cascada,
     * el que corre cuando el IPA no puede- escribiendo en la banda SIN
     * pedirla. Todos los demas la piden; ese no. Mientras nadie tuvo la
     * banda mas de un instante no se noto; la captura la tiene veinticinco
     * segundos y la cascada le escribia encima una y otra vez.
     *
     * Esto lo vigila de la unica forma que vale: se coge la banda, se
     * marca entera, se llama al que la queria, y tiene que estar intacta.
     */
    printf("\n  la banda es de quien la coge\n");
    {
        static uint8_t idx[16 * 64];
        uint16_t *b = gfx2_banda_coge();
        int32_t n = (int32_t)gfx2_banda_pixeles();
        int32_t k;
        int tocados = 0;

        if (b == 0) {
            di("se puede coger la banda", 0, "estaba cogida");
        } else {
            for (k = 0; k < n; k++) { b[k] = 0xA5A5U; }
            memset(idx, 0, sizeof idx);
            gfx2_wf_blit(0, 100, 64, 16, idx, 64, 0, spectrum_colormap_lut());
            for (k = 0; k < n; k++) { if (b[k] != 0xA5A5U) { tocados++; } }
            gfx2_banda_suelta();
            {
                char det[64];
                snprintf(det, sizeof det, "%d pixeles tocados", tocados);
                di("gfx2_wf_blit() no escribe en una banda ajena",
                   tocados == 0, det);
            }
        }
    }

    /* Para poder MIRARLA, que es la otra mitad de comprobar un dibujo. */
    memcpy((void *)lcd_sim_fb(), s_captura, sizeof s_captura);
    lcd_sim_write_ppm("out/captura.ppm");
    printf("      escrita out/captura.ppm\n");

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

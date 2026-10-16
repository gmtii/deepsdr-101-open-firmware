/*
 * Render de referencia "ANTES": reproduce el layout actual del firmware
 * usando el User/gfx.c, las fuentes y la paleta REALES del repositorio.
 *
 * Geometria tomada de User/main.c y User/waterfall.h:
 *   TOP_H            = 64
 *   STATUS_STRIP_Y   = 64,  STATUS_STRIP_H = 40
 *   SPEC_Y           = 104, SPEC_H = 242   (hasta WF_PANEL_Y)
 *   WF_PANEL_Y       = 346, WATERFALL_ROWS = 72 (+4 de marco)
 *   barra inferior   = y 422, h 58, 6 botones
 *   MENU_TILE_W/H/GAP= 190 / 95 / 8, rejilla 4 columnas x 3 filas
 */

#include "lcd_sim.h"
#include "gfx.h"
#include "specdata.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TOP_H          64
#define STATUS_Y       64
#define STATUS_H       40
#define SPEC_Y        104
#define SPEC_H        242
#define WF_Y          346
#define WF_ROWS        72
#define BOT_Y         422
#define BOT_H          58
#define MAIN_W        800

#define TILE_W 190
#define TILE_H  95
#define TILE_GAP 8
#define TILE_X0  8
#define MENU_AREA_Y SPEC_Y
#define TILE_Y0 (MENU_AREA_Y + 8)
#define TILE_COL(i) (TILE_X0 + (i) * (TILE_W + TILE_GAP))
#define TILE_ROW(i) (TILE_Y0 + (i) * (TILE_H + TILE_GAP))

/* colormap del waterfall al estilo "heatmap" clasico saturado */
static uint16_t wf_color(float db, float lo, float hi)
{
    float t = (db - lo) / (hi - lo);
    uint8_t r, g, b;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (t < 0.25f)      { r = 0;                        g = 0;                         b = (uint8_t)(t / 0.25f * 200); }
    else if (t < 0.5f)  { r = 0;                        g = (uint8_t)((t-0.25f)/0.25f*255); b = 200; }
    else if (t < 0.75f) { r = (uint8_t)((t-0.5f)/0.25f*255); g = 255;                   b = (uint8_t)(200 - (t-0.5f)/0.25f*200); }
    else                { r = 255;                      g = (uint8_t)(255 - (t-0.75f)/0.25f*255); b = 0; }
    return gfx_rgb565(r, g, b);
}

static void draw_radio_screen(void)
{
    static float spec[SPEC_BINS];
    int i;
    const float lo = -100.0f, hi = -15.0f;

    specdata_fill(spec, SPEC_BINS, 7);

    gfx_fill_screen(GFX_COLOR_BLACK);

    /* --- Top bar --- */
    gfx_fill_rect(0, 0, MAIN_W, TOP_H, GFX_COLOR_DARKGRAY);
    gfx_text(6, 14, "7.200.00", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 6);
    gfx_text(6, 46, "MHz", GFX_COLOR_GRAY, GFX_COLOR_DARKGRAY, 1);
    gfx_text(300, 10, "AM", GFX_COLOR_YELLOW, GFX_COLOR_DARKGRAY, 3);
    gfx_text(300, 40, "STEP 1kHz", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);
    gfx_text(420, 40, "VOL 18", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);
    gfx_text(600, 10, "21:47", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 3);
    /* bateria */
    gfx_rect(700, 40, 40, 16, GFX_COLOR_WHITE);
    gfx_fill_rect(702, 42, 26, 12, GFX_COLOR_GREEN);
    gfx_fill_rect(740, 45, 3, 6, GFX_COLOR_WHITE);
    gfx_text(750, 44, "3.9V", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);

    /* --- Status strip --- */
    gfx_fill_rect(0, STATUS_Y, MAIN_W, STATUS_H, GFX_COLOR_BLACK);
    /* S-meter de 12 segmentos */
    for (i = 0; i < 12; i++) {
        uint16_t c = (i < 9) ? GFX_COLOR_GREEN : GFX_COLOR_RED;
        if (i >= 8) c = GFX_COLOR_RED;
        gfx_fill_rect((uint16_t)(6 + i * 16), STATUS_Y + 10, 13, 18,
                      (i < 9) ? c : GFX_COLOR_DARKGRAY);
    }
    gfx_text(206, STATUS_Y + 12, "-53dBm", GFX_COLOR_CYAN, GFX_COLOR_BLACK, 2);
    /* badges */
    gfx_text(330, STATUS_Y + 12, "NR", GFX_COLOR_DARKGRAY, GFX_COLOR_BLACK, 2);
    gfx_text(372, STATUS_Y + 12, "SPT", GFX_COLOR_GREEN, GFX_COLOR_BLACK, 2);
    gfx_text(428, STATUS_Y + 12, "AGC", GFX_COLOR_GREEN, GFX_COLOR_BLACK, 2);
    gfx_rect(480, STATUS_Y + 8, 56, 26, GFX_COLOR_GRAY);
    gfx_text(486, STATUS_Y + 12, "MED", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_rect(544, STATUS_Y + 8, 68, 26, GFX_COLOR_GRAY);
    gfx_text(550, STATUS_Y + 12, "4K0", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_text(624, STATUS_Y + 12, "OVR", GFX_COLOR_DARKGRAY, GFX_COLOR_BLACK, 2);
    gfx_text(684, STATUS_Y + 12, "96K", GFX_COLOR_YELLOW, GFX_COLOR_BLACK, 2);

    /* --- Espectro --- */
    gfx_rect(0, SPEC_Y, MAIN_W, SPEC_H, GFX_COLOR_GRAY);
    /* rejilla */
    for (i = 1; i < 6; i++) {
        gfx_hline(2, (uint16_t)(SPEC_Y + i * SPEC_H / 6), MAIN_W - 4, GFX_COLOR_DARKGRAY);
    }
    for (i = 1; i < 8; i++) {
        gfx_vline((uint16_t)(2 + i * (MAIN_W - 4) / 8), SPEC_Y + 1, SPEC_H - 2, GFX_COLOR_DARKGRAY);
    }
    /* traza: linea vertical por bin, estilo "heatmap vs line" -> line */
    for (i = 0; i < SPEC_BINS; i++) {
        float t = (spec[i] - lo) / (hi - lo);
        int h;
        if (t < 0) { t = 0; }
        if (t > 1) { t = 1; }
        h = (int)(t * (SPEC_H - 24));
        if (h < 1) h = 1;
        gfx_vline((uint16_t)(2 + i), (uint16_t)(SPEC_Y + SPEC_H - 2 - h), (uint16_t)h, GFX_COLOR_GREEN);
    }
    /* marcador de VFO */
    gfx_vline(MAIN_W / 2, SPEC_Y + 1, SPEC_H - 2, GFX_COLOR_RED);
    /* etiquetas de escala */
    gfx_text(6, SPEC_Y + 4, "-96k", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_text(370, SPEC_Y + 4, "VFO", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_text(730, SPEC_Y + 4, "+96k", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_text(6, SPEC_Y + SPEC_H - 20, "-15dB", GFX_COLOR_GRAY, GFX_COLOR_BLACK, 2);

    /* --- Waterfall ---
     * Igual que waterfall.c en el firmware: se compone el buffer completo
     * en RAM y se vuelca con UN gfx_blit (una sola ventana), no pixel a
     * pixel. Medir de otra forma inflaria injustamente el coste del
     * "antes". */
    gfx_rect(0, WF_Y, MAIN_W, WF_ROWS + 4, GFX_COLOR_GRAY);
    {
        static uint16_t wfbuf[WF_ROWS * SPEC_BINS];
        int row;
        for (row = 0; row < WF_ROWS; row++) {
            static float line[SPEC_BINS];
            specdata_fill(line, SPEC_BINS, (unsigned)(7 + row * 13));
            for (i = 0; i < SPEC_BINS; i++) {
                wfbuf[row * SPEC_BINS + i] = wf_color(line[i], lo, hi);
            }
        }
        gfx_blit(2, WF_Y + 2, SPEC_BINS, WF_ROWS, wfbuf);
    }

    /* --- Barra inferior: 6 botones --- */
    {
        const char *labels[6] = { "MODE", "VOL", "STEP", "NR", "BANDS", "MENU" };
        int bw = MAIN_W / 6;
        for (i = 0; i < 6; i++) {
            gfx_fill_rect((uint16_t)(i * bw), BOT_Y, (uint16_t)(bw - 2), BOT_H, GFX_COLOR_DARKGRAY);
            gfx_rect((uint16_t)(i * bw), BOT_Y, (uint16_t)(bw - 2), BOT_H, GFX_COLOR_GRAY);
            gfx_text((uint16_t)(i * bw + (bw - 2 - gfx_text_width(labels[i], 2)) / 2),
                     BOT_Y + 20, labels[i], GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 2);
        }
    }
}

static void tile(int col, int row, const char *label, const char *value,
                 uint16_t fill, uint16_t border, uint16_t fg)
{
    uint16_t x = (uint16_t)TILE_COL(col), y = (uint16_t)TILE_ROW(row);
    gfx_fill_rect(x, y, TILE_W, TILE_H, fill);
    gfx_rect(x, y, TILE_W, TILE_H, border);
    gfx_text((uint16_t)(x + (TILE_W - gfx_text_width(label, 3)) / 2),
             (uint16_t)(y + (value ? 18 : 36)), label, fg, fill, 3);
    if (value) {
        gfx_text((uint16_t)(x + (TILE_W - gfx_text_width(value, 2)) / 2),
                 (uint16_t)(y + 56), value, GFX_COLOR_YELLOW, fill, 2);
    }
}

static void draw_menu_screen(void)
{
    /* parte de la pantalla de radio: top bar + status + barra inferior siguen vivos */
    draw_radio_screen();

    /* el menu tapa el area de espectro+waterfall */
    gfx_fill_rect(0, MENU_AREA_Y, MAIN_W, 318, GFX_COLOR_BLACK);

    /* columna 0: selector de pagina */
    tile(0, 0, "RADIO", NULL, GFX_COLOR_BLUE,  GFX_COLOR_CYAN, GFX_COLOR_WHITE);
    tile(0, 1, "UI",    NULL, GFX_COLOR_BLACK, GFX_COLOR_CYAN, GFX_COLOR_CYAN);
    tile(0, 2, "HW",    NULL, GFX_COLOR_BLACK, GFX_COLOR_CYAN, GFX_COLOR_CYAN);

    /* pagina RADIO, 8/8 slots */
    tile(1, 0, "AGC",   "MED",  GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(2, 0, "SQL",   "12",   GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(3, 0, "VOL",   "18",   GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(1, 1, "BW",    "4K0",  GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(2, 1, "PGA",   "24.0", GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(3, 1, "NR",    "OFF",  GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(1, 2, "RFAGC", "ON",   GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(2, 2, "ATT",   "-6",   GFX_COLOR_DARKGRAY, GFX_COLOR_GRAY, GFX_COLOR_WHITE);
    tile(3, 2, "EXIT",  NULL,   GFX_COLOR_RED,      GFX_COLOR_WHITE, GFX_COLOR_WHITE);
}

int main(void)
{
    lcd_cost_t c;

    lcd_sim_reset(GFX_COLOR_BLACK);
    lcd_sim_cost_reset();
    draw_radio_screen();
    c = lcd_sim_cost();
    lcd_sim_write_ppm("out/01_before_radio.ppm");
    printf("ANTES pantalla radio : ventanas=%llu accesos_bus=%llu overdraw=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes,
           (unsigned long long)c.overdraw);
    printf("  timings actuales (21 ciclos @200MHz): %.1f ms -> %.1f fps\n",
           lcd_sim_est_us(21, 200.0) / 1000.0,
           1000.0 / (lcd_sim_est_us(21, 200.0) / 1000.0));

    lcd_sim_reset(GFX_COLOR_BLACK);
    lcd_sim_cost_reset();
    draw_menu_screen();
    c = lcd_sim_cost();
    lcd_sim_write_ppm("out/02_before_menu.ppm");
    printf("ANTES pantalla menu  : ventanas=%llu accesos_bus=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes);
    return 0;
}

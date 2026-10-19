/*
 * LA CAPTURA TIENE QUE SALIR IGUAL QUE LA PANTALLA, TAMBIEN EN LOS MENUS.
 *
 * Pedido por el usuario.
 *
 * sim/captura.c ya vigila esto para la pantalla de la radio. Este banco es
 * el mismo trato para las SEIS pantallas de menu: ajustes con columna,
 * rejilla paginada, detalle de un ajuste, teclado, sincronizar la hora y
 * QTH. Todas ocupan el mismo hueco -y de 104 a 421- y solo puede haber una.
 *
 * COMO SE COMPRUEBA: se pinta cada pantalla ENTERA por el camino de siempre
 * -cabecera, la capa del menu, barra de botones-, se guarda, se monta por
 * el camino de la captura franja por franja, y se comparan PIXEL A PIXEL.
 * Cero diferencias o nada.
 *
 * Asi no hay que enumerar elementos: el dia que alguien anada un boton a
 * una de estas pantallas y la capa de captura se quede atras, esto lo dice
 * sin que nadie se haya acordado de mirar ese boton.
 *
 * Y de paso se comprueba que CADA UNA SE QUEDA EN SU SITIO: nada de lo que
 * pinta un menu puede salirse de 104..421, porque ahi arriba esta la
 * cabecera y ahi abajo la barra. Eso es justo lo que se escapaba en la
 * marquesina y en el numero de abajo de la cascada -las dos se recortaban
 * por la ventana que les daba gfx2_render() y no por si mismas-.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include "ui_act.h"
#include "ui_grid.h"
#include "ui_cfg.h"
#include "ui_det.h"
#include "ui_kbd.h"
#include "ui_hora.h"
#include "ui_qth.h"
#include "ui_digi.h"
#include "spectrum.h"
#include "specdata.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

static ui_top_state_t  top;
static ui_act_state_t  act;
static ui_grid_state_t grid;
static ui_cfg_state_t  cfg;
static ui_det_state_t  det;
static ui_kbd_state_t  kbd;
static ui_hora_state_t hora;
static ui_qth_state_t  qth;
static ui_digi_state_t digi;   /* panel entero: 104..421 */
static ui_digi_state_t digi_txt; /* con osciloscopio encima: 248..421 */

/* La geometria de los paneles digitales, la misma que en main.c. */
#define PAN_Y            104
#define PAN_H            318
#define TXT_Y            248
#define TXT_H            174
#define SCOPE_TRACE_H    142
#define SCOPE_BINS       256
#define SCOPE_DB_FLOOR  -60.0f
static float g_scope[SCOPE_BINS];

/* Las dos rayas del osciloscopio, como las pone scope_rayas() en main.c:
 * marca y espacio del RTTY, a 2.125 y 2.295 Hz con un Nyquist de 6.000. */
#define RAYA_A 90
#define RAYA_B 98

static uint16_t s_pantalla[LCD_W * LCD_H];
static uint16_t s_captura[LCD_W * LCD_H];

static int fallos;

/* Lo mismo que color_tema() en main.c: un color del tema en el formato que
 * pide gfx.c, que es quien dibuja las rayas en la pantalla de verdad. */
static uint16_t color_tema(uint32_t hex)
{
    return gfx2_to565((uint8_t)((hex >> 16) & 0xFFU),
                      (uint8_t)((hex >> 8) & 0xFFU),
                      (uint8_t)(hex & 0xFFU));
}

/* Que capa hay delante. El mismo reparto que menu_capa() en main.c. */
typedef enum { CAPA_GRID = 0, CAPA_CFG, CAPA_DET, CAPA_KBD, CAPA_HORA, CAPA_QTH,
               CAPA_DIGI, CAPA_SCOPE } capa_t;

static void capa_draw(capa_t c)
{
    switch (c) {
    case CAPA_GRID: ui_grid_draw(&grid); break;
    case CAPA_CFG:  ui_cfg_draw(&cfg);   break;
    case CAPA_DET:  ui_det_draw(&det);   break;
    case CAPA_KBD:  ui_kbd_draw(&kbd);   break;
    case CAPA_HORA: ui_hora_draw(&hora); break;
    case CAPA_QTH:  ui_qth_draw(&qth);   break;
    case CAPA_DIGI: ui_digi_draw_texto(&digi); break;
    default:
        /* Osciloscopio de sintonia arriba y panel de texto abajo, que es lo
         * que se ve en RTTY, CW, APRS, NAVTEX y PSK31. */
        spectrum_draw(g_scope, SCOPE_BINS, 0, PAN_Y, 800, SCOPE_TRACE_H,
                      SCOPE_DB_FLOOR, 0.0f, 0, 0, 0, 0);
        gfx_vline(RAYA_A, PAN_Y, SCOPE_TRACE_H, color_tema(PAL_ACCENT));
        gfx_vline(RAYA_B, PAN_Y, SCOPE_TRACE_H, color_tema(PAL_WARN));
        ui_digi_draw_texto(&digi_txt);
        break;
    }
}

static void capa_pinta_en(capa_t c, gfx2_surf_t *s)
{
    switch (c) {
    case CAPA_GRID: ui_grid_pinta_en(s, &grid); break;
    case CAPA_CFG:  ui_cfg_pinta_en(s, &cfg);   break;
    case CAPA_DET:  ui_det_pinta_en(s, &det);   break;
    case CAPA_KBD:  ui_kbd_pinta_en(s, &kbd);   break;
    case CAPA_HORA: ui_hora_pinta_en(s, &hora); break;
    case CAPA_QTH:  ui_qth_pinta_en(s, &qth);   break;
    case CAPA_DIGI: ui_digi_pinta_en(s, &digi); break;
    default: {
        /* La misma secuencia que capt_pinta() en main.c para el caso del
         * osciloscopio: el panel de texto y, encima, el trazo reconstruido
         * con sus rayas. */
        gfx2_surf_t sub = gfx2_sub_y(s, PAN_Y, SCOPE_TRACE_H);
        ui_digi_pinta_en(s, &digi_txt);
        if (sub.h > 0) {
            spectrum_pinta_en(&sub, 0, PAN_Y);
            gfx2_vline(&sub, RAYA_A, PAN_Y, SCOPE_TRACE_H, gfx2_rgb(PAL_ACCENT));
            gfx2_vline(&sub, RAYA_B, PAN_Y, SCOPE_TRACE_H, gfx2_rgb(PAL_WARN));
        }
        break;
    }
    }
}

static const char *k_nombre[] = {
    "rejilla paginada (bandas/modos/pasos)",
    "ajustes con columna de categorias",
    "detalle de un ajuste",
    "teclado de frecuencia",
    "sincronizar la hora",
    "QTH",
    "panel de un modo digital (FT8, HFDL...)",
    "osciloscopio de sintonia + panel de texto",
};

/* El estado de la cabecera y la barra: lo mismo que en sim/captura.c, que
 * es lo que se ve por encima y por debajo de cualquier menu. */
static void estado_comun(void)
{
    static const char *nom[UI_ACT_N] = { "Bandas", "Modo", "Paso",
                                         "Volumen", "Ruido", "Ajustes" };
    static const char *val[UI_ACT_N] = { 0, "LSB", "1 kHz",
                                         "-12,0 dB", "activo", 0 };
    int i;

    memset(&top, 0, sizeof top);
    memset(&act, 0, sizeof act);

    top.freq = "7.265,00"; top.freq_digit = 5;
    top.mode = "LSB"; top.band = "41 m"; top.clock = "22:14";
    top.batt_pct = 72;
    top.rds_nombre = "RADIO EUROPA";
    top.rds        = "HOL - NL - 00:00-24:00";
    top.s_units = 7; top.dbm = -79;
    top.knob = UI_KNOB_TUNE; top.knob_value = "1 kHz";
    top.agc = "MED"; top.bw = "0-2,7k"; top.att = "-06"; top.nco_on = 1;

    for (i = 0; i < UI_ACT_N; i++) {
        act.name[i] = nom[i]; act.value[i] = val[i];
        act.active[i] = (uint8_t)(i == 5);
    }
    act.pressed = -1;
}

static void estado_menus(void)
{
    int i;

    memset(&grid, 0, sizeof grid);
    grid.titulo = "Bandas de radiodifusion";
    grid.pagina = 1U; grid.paginas = 3U; grid.n = UIG_CELLS;
    for (i = 0; i < UIG_CELLS; i++) {
        static const char *l1[] = { "49 m", "41 m", "31 m", "25 m",
                                    "22 m", "19 m", "16 m", "13 m" };
        static const char *l2[] = { "5,9-6,2", "7,2-7,45", "9,4-9,9", "11,6-12,1",
                                    "13,5-13,87", "15,1-15,8", "17,48-17,9", "21,45-21,85" };
        grid.cel[i].l1 = l1[i % 8]; grid.cel[i].l2 = l2[i % 8]; grid.cel[i].l3 = "MHz";
    }
    grid.pressed = -1; grid.cursor = 2; grid.marcada = 1U;
    grid.nav[0] = "Anterior"; grid.nav[1] = "Siguiente";
    grid.nav_on[0] = 1U; grid.nav_on[1] = 1U;

    memset(&cfg, 0, sizeof cfg);
    cfg.cat[0] = "Radio"; cfg.cat[1] = "Audio"; cfg.cat[2] = "Pantalla";
    cfg.cat[3] = "Sistema";
    cfg.cats = 4U; cfg.cat_sel = 1U; cfg.denso = 0U; cfg.n = UIC_CELLS;
    for (i = 0; i < UIC_CELLS; i++) {
        static const char *n[] = { "Silenciador", "Filtro de ruido", "Ecualizador",
                                   "Volumen maximo", "Pitidos", "Tono de aviso",
                                   "Salida", "Ganancia" };
        static const char *v[] = { "12", "medio", "plano", "-6 dB",
                                   "si", "1200 Hz", "altavoz", "auto" };
        cfg.cel[i].nombre = n[i % 8]; cfg.cel[i].valor = v[i % 8];
        cfg.cel[i].extra = (i % 3 == 0) ? "tocar" : 0;
    }
    cfg.cursor = 3; cfg.pressed = -1; cfg.marcada = 0xFFU;

    memset(&det, 0, sizeof det);
    det.titulo = "Silenciador";
    det.valor = "12 dB";
    det.pie = "por debajo de este nivel, el audio se corta";
    det.alt = "Automatico"; det.alt_on = 1U;
    det.preset[0] = "0"; det.preset[1] = "12"; det.preset[2] = "24";
    det.preset_on = 1; det.pressed = -1;

    memset(&kbd, 0, sizeof kbd);
    kbd.titulo = "Frecuencia";
    kbd.lectura = "7265"; kbd.unidad = "kHz";
    kbd.pista = "escribe en kHz y pulsa Ir";
    {
        /* El mismo reparto que k_kbd_freq_rot/k_kbd_freq_tipo en main.c. */
        static const char *t[UIK_KEYS] = {
            "1","2","3","Borrar",
            "4","5","6","Vaciar",
            "7","8","9","Volver",
            ",","0","kHz","MHz",
        };
        static const uint8_t ti[UIK_KEYS] = {
            UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
            UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
            UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_VUELVE,
            UIK_DIGITO, UIK_DIGITO, UIK_ACEPTA, UIK_ACEPTA,
        };
        for (i = 0; i < UIK_KEYS; i++) { kbd.tecla[i] = t[i]; kbd.tipo[i] = ti[i]; }
    }
    kbd.pressed = -1; kbd.cursor = 4;

    memset(&hora, 0, sizeof hora);
    hora.titulo = "Poner la hora por radio";
    hora.estado = "Leyendo 23/59";
    hora.detalle = "faltan 37 s para el minuto";
    hora.hora = "22:14"; hora.fecha = "lunes 05/10/26";
    hora.nivel = 78U; hora.marca = 41U; hora.latido = 1U;
    hora.emisora = "DCF77"; hora.puede_aplicar = 1U; hora.pressed = -1;
    /* El cuarto boton, desde la V4.27. Ver ui_hora.h: lleva el valor escrito
     * dentro para que no haya que entrar a mirarlo. */
    hora.huso = "Huso +2:00";

    memset(&qth, 0, sizeof qth);
    qth.titulo = "QTH";
    qth.pie = "para las distancias y el gris";
    memcpy(qth.loc, "IN80dk", UIQ_N + 1);
    for (i = 0; i < UIQ_N; i++) {
        static const char *p[] = { "A-R", "A-R", "0-9", "0-9", "a-x", "a-x" };
        qth.pista[i] = p[i];
    }
    qth.coord = "40,4375 N   3,7083 W";
    qth.rot[0] = "-"; qth.rot[1] = "+"; qth.rot[2] = "Aplicar"; qth.rot[3] = "Salir";
    qth.sel = 3U; qth.cambiado = 1U; qth.pressed = -1;

    memset(&digi, 0, sizeof digi);
    digi.y = PAN_Y; digi.h = PAN_H;
    digi.titulos = "hora   de        a        dB   mensaje";
    for (i = 0; i < 8; i++) {
        static const char *f[] = {
            "22:14  EA4XYZ  CQ       -07  IN80",
            "22:14  DL1ABC  EA4XYZ   -12  JO31",
            "22:15  F5ZZZ   DL1ABC   -03  JN18",
            "22:15  G0QRP   CQ       -19  IO91",
            "22:16  SM7AAA  G0QRP    -08  JO65",
            "22:16  OH2BBB  CQ       -15  KP20",
            "22:17  I0CCC   OH2BBB   -11  JN61",
            "22:17  EA4XYZ  I0CCC    -05  IN80",
        };
        digi.fila[i] = f[i];
    }
    digi.chip = "21 ppm"; digi.enganchado = 1U;
    digi.btn = "Borrar"; digi.btn2 = "Frecuencias"; digi.btn5 = "Mapa";

    /* El mismo panel pero en su sitio de cuando hay osciloscopio encima. */
    digi_txt = digi;
    digi_txt.y = TXT_Y; digi_txt.h = TXT_H;
}

/* Un trazo de osciloscopio con datos de verdad del generador. */
static void datos_scope(void)
{
    int i;
    specdata_fill(g_scope, SCOPE_BINS, 3u);
    for (i = 0; i < SCOPE_BINS; i++) {
        /* El generador da dBm de radio; aqui la escala es -60..0. */
        g_scope[i] = (g_scope[i] + 110.0f) * 0.55f - 60.0f;
        if (g_scope[i] < SCOPE_DB_FLOOR) { g_scope[i] = SCOPE_DB_FLOOR; }
        if (g_scope[i] > 0.0f) { g_scope[i] = 0.0f; }
    }
}

/* La pantalla por el camino de siempre. */
static void pinta_normal(capa_t c)
{
    lcd_sim_reset(0x0000);
    ui_top_draw(&top);
    capa_draw(c);
    ui_act_draw(&act);
}

/* Y por el camino de la captura: la MISMA secuencia de capas que
 * capt_pinta() en main.c, banda a banda. */
static void pinta_captura(capa_t c, uint16_t *dest)
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
        capa_pinta_en(c, &s);
        ui_top_pinta_en(&s, &top);
        ui_act_pinta_en(&s, &act);

        for (k = 0; k < nf; k++) {
            memcpy(&dest[(size_t)(y + k) * LCD_W], &banda[(size_t)k * LCD_W],
                   (size_t)LCD_W * sizeof dest[0]);
        }
    }
}

/* Lo que pinta la capa del menu y SOLO ella, para ver si se sale de 104..421. */
static void mide_capa(capa_t c, int *y0, int *y1)
{
    static uint16_t banda[LCD_W * 16];
    int16_t y;
    uint16_t fuera;

    /* El color de fondo tal y como queda en la banda, para compararlo. */
    {
        gfx2_surf_t s0;
        s0.px = banda; s0.x = 0; s0.y = 0; s0.w = (int16_t)LCD_W; s0.h = 1;
        gfx2_fill(&s0, 0, 0, GFX2_W, 1, gfx2_rgb(0xFF00FF));
        fuera = banda[0];
    }

    *y0 = -1; *y1 = -1;
    for (y = 0; y < (int16_t)LCD_H; y = (int16_t)(y + 16)) {
        gfx2_surf_t s;
        int16_t nf = (int16_t)(LCD_H - y);
        int16_t k, x;

        if (nf > 16) { nf = 16; }
        s.px = banda; s.x = 0; s.y = y; s.w = (int16_t)LCD_W; s.h = nf;
        /* Fondo de un color que la capa no usa: lo que quede distinto es
         * exactamente lo que ha pintado la capa. */
        gfx2_fill(&s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(0xFF00FF));
        capa_pinta_en(c, &s);
        for (k = 0; k < nf; k++) {
            for (x = 0; x < (int16_t)LCD_W; x++) {
                if (banda[(size_t)k * LCD_W + x] != fuera) {
                    if (*y0 < 0) { *y0 = y + k; }
                    *y1 = y + k;
                }
            }
        }
    }
}

int main(void)
{
    int c;

    printf("\nLA CAPTURA CONTRA LA PANTALLA, EN LOS MENUS\n\n");

    spectrum_init();
    spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
    estado_comun();
    estado_menus();
    datos_scope();

    for (c = 0; c <= (int)CAPA_SCOPE; c++) {
        size_t i, dif = 0, prim = 0;
        int y0, y1;

        printf("  %s\n", k_nombre[c]);

        pinta_normal((capa_t)c);
        memcpy(s_pantalla, lcd_sim_fb(), sizeof s_pantalla);
        pinta_captura((capa_t)c, s_captura);

        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            if (s_pantalla[i] != s_captura[i]) {
                if (dif == 0U) { prim = i; }
                dif++;
            }
        }
        if (dif != 0U) {
            printf("    %-50s *** MAL ***  %u pixeles\n",
                   "la captura sale igual que la pantalla", (unsigned)dif);
            printf("      primera diferencia en x=%d y=%d: pantalla %04X, captura %04X\n",
                   (int)(prim % LCD_W), (int)(prim / LCD_W),
                   s_pantalla[prim], s_captura[prim]);
            fallos++;
        } else {
            printf("    %-50s ok\n", "la captura sale igual que la pantalla");
        }

        /* Y una imagen para mirarla con los ojos, que es lo unico que
         * caza "sale igual pero las dos estan mal". */
        {
            char nom[64];
            FILE *f;
            sprintf(nom, "out/captura_menu%d.ppm", c);
            f = fopen(nom, "wb");
            if (f) {
                size_t k;
                fprintf(f, "P6\n%d %d\n255\n", (int)LCD_W, (int)LCD_H);
                for (k = 0U; k < (size_t)LCD_W * (size_t)LCD_H; k++) {
                    uint16_t v = s_captura[k];
                    unsigned char rgb[3];
                    rgb[0] = (unsigned char)(((v >> 11) & 0x1FU) * 255U / 31U);
                    rgb[1] = (unsigned char)(((v >> 5) & 0x3FU) * 255U / 63U);
                    rgb[2] = (unsigned char)((v & 0x1FU) * 255U / 31U);
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
        }

        mide_capa((capa_t)c, &y0, &y1);
        if (y0 < 104 || y1 > 421) {
            printf("    %-50s *** MAL ***  pinta de %d a %d\n",
                   "y se queda dentro de su hueco (104..421)", y0, y1);
            fallos++;
        } else {
            printf("    %-50s ok  (%d..%d)\n",
                   "y se queda dentro de su hueco (104..421)", y0, y1);
        }
    }

    /*
     * ======================================================================
     * Y UN ROTULO A CERO, QUE ES COMO APARECIO ESTO - 10/10/2026
     * ======================================================================
     *
     * Al añadir el cuarto boton a la pantalla del reloj, este banco se cayo
     * entero con un segmentation fault: su ui_hora_state_t no tenia el campo
     * nuevo, asi que `huso` valia cero y las funciones de texto de gfx2.c se
     * metian en *str con un puntero nulo.
     *
     * Parecia un fallo del banco y NO LO ERA. Dos de los cuatro rotulos los
     * rellena main.c, asi que un cero puede llegar de verdad; y en la radio
     * un fallo de CPU no reinicia -no hay perro guardian y
     * HardFault_Handler es un bucle infinito-, o sea pantalla congelada y a
     * quitar la pila. Arreglar solo el banco habria tapado exactamente lo
     * que habia que ver.
     *
     * Asi que se arreglo en gfx2.c, que cubre a todas las pantallas, y el
     * caso se queda AQUI: si alguien quita esa guarda, esto se cae otra vez.
     */
    printf("\n  un rotulo a cero (lo que main.c puede dejar sin rellenar)\n");
    {
        const char *antes = hora.huso;
        int y0 = 0, y1 = 0;

        hora.huso = 0;
        hora.emisora = 0;
        pinta_normal(CAPA_HORA);           /* si no esta la guarda, aqui se cae */
        mide_capa(CAPA_HORA, &y0, &y1);
        if (y0 < 104 || y1 > 421) {
            printf("    %-50s *** MAL ***  pinta de %d a %d\n",
                   "se dibuja igual y se queda en su hueco", y0, y1);
            fallos++;
        } else {
            printf("    %-50s ok  (%d..%d)\n",
                   "se dibuja igual y se queda en su hueco", y0, y1);
        }
        hora.huso = antes;
        hora.emisora = "DCF77";
    }

    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO: ni un pixel distinto");
    return fallos ? 1 : 0;
}

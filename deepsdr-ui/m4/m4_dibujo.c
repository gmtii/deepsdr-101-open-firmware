/*
 * LO QUE CUESTA DIBUJAR, EN INSTRUCCIONES DE CORTEX-M4.
 *
 * El hermano de m4/m4_espectro.c, y no su repeticion. Aquel mide LA TRAZA
 * -spectrum_draw() en sus cuatro casos- y de la cascada solo mide meter la
 * linea en el anillo (waterfall_push_line). Lo caro de la cascada no es
 * meter la linea: es SACARLA AL PANEL, y eso aqui no lo media nadie.
 *
 * Asi que esto empieza justo donde el otro acaba:
 *
 *   1. LA CASCADA, los dos caminos de waterfall.c comparados: el de
 *      siempre -DESPLAZADO, que repinta las 72 filas en cada linea- contra
 *      el de BUCLE -que escribe solo la fila o las filas nuevas-. Y a las
 *      cinco velocidades de k_wf (main.c), porque lo que importa no es lo
 *      que cuesta un volcado sino lo que cuestan los volcados de un
 *      segundo.
 *   2. Las primitivas de gfx2.c, que son el suelo de todo lo demas.
 *   3. La franja de arriba (ui_top.c) y el panel digital (ui_digi.c).
 *   4. El cromo del espectro (spec_chrome.c) y los dos modulos de calculo
 *      que cuelgan de el: la autoescala (spec_agc.c) y el enganche al
 *      tocar (spec_snap.c).
 *   5. Las fuentes: medir y pintar texto con fonts_data.c, y lo que cuesta
 *      UNA letra.
 *
 * LO DE ui_top.c LO MIDE YA m4/m4_cabecera.c, y aqui se repite a
 * proposito por dos razones: para darle su % del segundo, que es lo que
 * dice si estorba, y porque sirve de contraste entre los dos bancos. Los
 * numeros cuadran -la franja de estado da 1.892.084 aqui y 1.894.040
 * alli, la regla da 331.290 contra 331.288 y el S-metro 82.492 en los
 * dos-, que es la forma de saber que ninguno de los dos miente.
 *
 * COMO SE LEE. Igual que el otro: 1 tic de SysTick = 40 instrucciones, y
 * el GD32 va a 200 MHz. Y lo que avisa m4/LEEME.md vale aqui el doble: el
 * bus del panel NO se modela -m4/panel.c escribe en una variable- asi que
 * estos numeros son un SUELO. En la radio lo real es del orden de 1,7
 * veces esto, y en la cascada desplazada mas, porque ahi lo que no se
 * modela son 54.432 escrituras al bus EXMC.
 *
 * LA COLUMNA QUE HAY QUE MIRAR es la del % DEL SEGUNDO. La pantalla se
 * repinta unas 30 veces por segundo, asi que una cosa que cuesta mucho y
 * pasa una vez por minuto no estorba, y una que cuesta poco y pasa 30
 * veces por segundo si. Instrucciones sueltas no dicen cual es cual.
 */
#include "semi.h"
void pila_di(unsigned long maximo);

#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "waterfall.h"
#include "spec_chrome.h"
#include "spec_agc.h"
#include "spec_snap.h"
#include "ui_top.h"
#include "ui_digi.h"
#include "font_ui_11.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_num_20.h"
#include "font_num_44.h"
#include "font_rain_16.h"

int g_fallos;
const palette_t *g_pal = &k_pal_oscura;

/*
 * SIN TOPES A PROPOSITO. Esto es una MEDIDA, no un guardian: un tope que
 * se pone rojo porque otra version del compilador ordena dos
 * instrucciones de otra manera no avisa de nada y hace que se deje de
 * mirar el numero, que es justo lo contrario de lo que se quiere. El que
 * vigila que no entre trabajo nuevo es m4_espectro.c, que ya lleva los
 * suyos sobre la traza.
 */

/* --------------------------------------------------------------------
 * EL % DEL SEGUNDO
 * --------------------------------------------------------------------
 * `v100` son las veces por segundo EN CENTESIMAS, para que quepan las
 * velocidades que no son enteras: 3000 = 30 veces por segundo, 375 = 3,75.
 */
static void porcien(unsigned long ins, unsigned long v100)
{
    /* centesimas de % = ins * (v100/100) * 100 / 200.000.000 */
    unsigned long long t = (unsigned long long)ins * (unsigned long long)v100;
    unsigned long c = (unsigned long)(t / 2000000ULL);

    pr("      -> "); prn(c / 100UL); pr(",");
    if ((c % 100UL) < 10UL) { pr("0"); }
    prn(c % 100UL);
    pr(" % del segundo a ");
    prn(v100 / 100UL);
    if (v100 % 100UL) { pr(","); if ((v100%100UL)<10UL) pr("0"); prn(v100 % 100UL); }
    pr((v100 == 100UL) ? " vez/s\n" : " veces/s\n");
}

/* --------------------------------------------------------------------
 * DATOS DE VERDAD, QUE ES LA MITAD DE LA MEDIDA
 * --------------------------------------------------------------------
 * Un dibujado sobre ceros se salta trabajo: el relleno de la traza no
 * sube, la cascada es toda del mismo color y el texto vacio no busca ni
 * un glifo. Asi que aqui va una banda con forma -suelo de ruido, tres
 * emisoras de anchos distintos y un pico de continua en el centro- y
 * rotulos con letras de verdad.
 */
static float   s_db[1024];
static uint8_t s_lin[WATERFALL_WIDTH];
static uint16_t s_px[GFX2_W * 64];
static gfx2_surf_t s_sf;

static unsigned long s_vai = 0x1234ABCDu;
static unsigned long ale(void)
{
    s_vai = s_vai * 1103515245u + 12345u;
    return (s_vai >> 16) & 0x7FFFu;
}

/* Una campana barata, sin libreria de matematicas: dos tramos de recta
 * sobre el cuadrado de la distancia. Vale para dar forma, que es lo que
 * hace falta. */
static float joroba(int x, int centro, int ancho, float alto)
{
    int d = x - centro;
    float u;

    if (d < 0) { d = -d; }
    if (d >= ancho) { return 0.0f; }
    u = 1.0f - (float)d / (float)ancho;
    return alto * u * u;
}

static void banda_de_verdad(void)
{
    unsigned i;

    for (i = 0; i < 1024u; i++) {
        float v = -108.0f + (float)(ale() % 60u) * 0.1f;   /* suelo de ruido */

        v += joroba((int)i, 180, 14, 46.0f);   /* una portadora estrecha   */
        v += joroba((int)i, 430, 60, 34.0f);   /* una voz en banda lateral */
        v += joroba((int)i, 512,  3, 22.0f);   /* el pico de continua      */
        v += joroba((int)i, 760, 30, 28.0f);   /* una emisora de radiodif. */
        s_db[i] = v;
    }
    for (i = 0; i < (unsigned)WATERFALL_WIDTH; i++) {
        unsigned b = (i * 1024u) / (unsigned)WATERFALL_WIDTH;
        float v = (s_db[b] + 120.0f) * 2.4f;

        if (v < 0.0f)   { v = 0.0f; }
        if (v > 255.0f) { v = 255.0f; }
        s_lin[i] = (uint8_t)v;
    }
}

/* Mueve un poco la linea, para que cada empujon de cascada lleve datos
 * distintos y la LUT no acabe leyendo siempre la misma entrada. */
static void menea_linea(void)
{
    unsigned i;
    for (i = 0; i < (unsigned)WATERFALL_WIDTH; i++) {
        int v = (int)s_lin[i] + (int)(ale() % 7u) - 3;
        if (v < 0)   { v = 0; }
        if (v > 255) { v = 255; }
        s_lin[i] = (uint8_t)v;
    }
}

static void fondo(gfx2_surf_t *s, void *c){ (void)c;
    gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }

/* --------------------------------------------------------------------
 * LA BASE DEL ANILLO DE LA CASCADA, SIN TOCAR waterfall.c
 * --------------------------------------------------------------------
 * Para medir el camino de CPU de la cascada desplazada hace falta llamar
 * a gfx2_wf_blit() con el buffer del anillo y con su cabeza, y las dos
 * cosas son privadas de waterfall.c. No hacen falta: waterfall_row(r)
 * devuelve &s_buf[(r+head) % ROWS][0], asi que la direccion MAS BAJA de
 * las 72 es &s_buf[0][0], y la r donde aparece dice la cabeza.
 */
static uint8_t *wf_base(int16_t *head)
{
    uint8_t *b = waterfall_row(0);
    int16_t r0 = 0, r;

    for (r = 1; r < (int16_t)WATERFALL_ROWS; r++) {
        uint8_t *p = waterfall_row((uint16_t)r);
        if (p < b) { b = p; r0 = r; }
    }
    /* waterfall_row(r0) == &s_buf[0][0]  =>  (r0 + head) % ROWS == 0 */
    *head = (int16_t)(((int16_t)WATERFALL_ROWS - r0) % (int16_t)WATERFALL_ROWS);
    return b;
}

/* --------------------------------------------------------------------
 * ROTULOS DE VERDAD
 * -------------------------------------------------------------------- */
static const char *const k_filas[8] = {
    "EA4XYZ DE LU7ABC  PSE K",
    "CQ CQ CQ DE EA1FBU EA1FBU PSE K",
    "RYRYRYRY DE DDK9 DDK9",
    "QTH BILBAO OP JOSE MARIA 599 599",
    "WX: WIND NE 12KT SEA 2M VIS 8KM",
    "VVV VVV DE CFH CFH CFH",
    "73 TU EE SK",
    "NNNN"
};

static ui_top_state_t  s_top;
static ui_digi_state_t s_dig;
static spec_chrome_t   s_ch;

static void monta_estados(void)
{
    unsigned i;
    char *p;

    p = (char *)&s_top; for (i = 0; i < sizeof s_top; i++) { p[i] = 0; }
    p = (char *)&s_dig; for (i = 0; i < sizeof s_dig; i++) { p[i] = 0; }
    p = (char *)&s_ch;  for (i = 0; i < sizeof s_ch;  i++) { p[i] = 0; }

    s_top.freq = "7.102,00"; s_top.freq_digit = 6;
    s_top.mode = "LSB"; s_top.band = "40 m"; s_top.en_banda = 1U;
    s_top.clock = "21:40"; s_top.batt_pct = 66; s_top.att = "OFF";
    s_top.s_units = 7U; s_top.dbm = -93; s_top.dbm_valid = 1U;
    s_top.agc = "MED"; s_top.bw = "0-2,7k";
    s_top.knob_name = "Sinton."; s_top.knob_value = "1 kHz";
    s_top.nco_on = 1U; s_top.nco_off = "+33k";
    s_top.rds_nombre = "Radio Nacional de Espana";
    s_top.rds = "SOMOS RNE - INFORMATIVO DE LAS 22:00 ";

    s_dig.y = 200; s_dig.h = (int16_t)(UDG_HDR_H + 8 * UDG_LINE_H);
    s_dig.filas = 8U;
    for (i = 0; i < 8u; i++) { s_dig.fila[i] = k_filas[i]; }
    s_dig.chip = "45 bd"; s_dig.enganchado = 1U;
    s_dig.btn = "Borrar"; s_dig.btn2 = "Guardar PNG";
    s_dig.barra_on = 1U; s_dig.barra_v = 7U; s_dig.barra_max = 15U;
    s_dig.barra_txt = "Ranura 7/15";
    s_dig.cebra = 1U;

    s_ch.center_hz = 7102000UL; s_ch.span_hz = 48000UL;
    s_ch.db_min = -120.0f; s_ch.db_max = -20.0f;
    s_ch.cmap = spectrum_colormap_lut();
    s_ch.en_banda = 1U; s_ch.band = SPC_BAND_LOWER; s_ch.band_hz = 2700UL;
}

/* --------------------------------------------------------------------
 * LA CASCADA
 * -------------------------------------------------------------------- */

/*
 * Un volcado con `n` lineas pendientes, que es lo que hay en "Muy rapida"
 * (dos) y en todas las demas (una). Sirve para los dos caminos: cual se
 * toma lo decide waterfall_bucle_pon().
 *
 * AQUI NO SE MENEA LA LINEA a proposito, aunque en la radio cada linea
 * lleve datos nuevos: menea_linea() son once mil instrucciones y se
 * colarian dentro de la medida del volcado. El anillo ya viene cargado
 * con ochenta lineas DISTINTAS del arranque, que es lo que importa para
 * el camino desplazado -recorre las 72-, y la que se empuja tiene forma
 * de banda de verdad. Lo que cuesta MONTAR la linea se mide aparte, mas
 * abajo, y se suma en la tabla de las cinco velocidades.
 */
static void wf_vuelca(unsigned n)
{
    unsigned k;
    for (k = 0; k < n; k++) { waterfall_push_line(s_lin); }
    waterfall_blit(SPC_TRACE_X, SPC_WF_Y, spectrum_colormap_lut());
}

/* --------------------------------------------------------------------
 * LAS PRIMITIVAS
 * -------------------------------------------------------------------- */
static const char k_frase[] = "Radio Nacional de Espana - 7.102,00 kHz";
static const char k_frase2[] =
    "Radio Nacional de Espana - 7.102,00 kHz LSB AGC MED 2,7 kHz S7 -93 dBm";

int main(void)
{
    unsigned long a, b, v, w, x1, x2;
    int r;
    unsigned i;
    unsigned long base_bucle_1 = 0, base_bucle_2 = 0, base_despl = 0;
    unsigned long cpu_despl = 0, push = 0, monta = 0;

    reloj_on();
    banda_de_verdad();
    monta_estados();

    s_sf.px = s_px; s_sf.x = 0; s_sf.y = 0; s_sf.w = GFX2_W; s_sf.h = 64;

    gfx2_render_screen(fondo, 0);
    spectrum_init();
    waterfall_init();
    for (i = 0; i < 80u; i++) { menea_linea(); waterfall_push_line(s_lin); }

    pr("\nLO QUE CUESTA DIBUJAR, EN UN CORTEX-M4 DE VERDAD\n");
    pr("(suelo: el bus del panel no se modela; ver m4/LEEME.md)\n");

    /* =============================================================== */
    pr("\n== LA CASCADA: BUCLE CONTRA DESPLAZADO ==\n\n");

    /*
     * LO QUE CUESTA UNA LINEA ANTES DE EMPUJARLA. main.c la monta bin a
     * bin con spectrum_colormap_idx() (ver el camino de k_wf), y eso se
     * paga una vez por linea, no por volcado: en "Muy rapida" van dos.
     */
    {
        float esc = spectrum_colormap_escala(-120.0f, -20.0f);

        a = tic(); for (r = 0; r < 10; r++) {
            for (i = 0; i < (unsigned)WATERFALL_WIDTH; i++) {
                s_lin[i] = spectrum_colormap_idx(
                               s_db[(i * 1024u) / WATERFALL_WIDTH],
                               -120.0f, esc); } } b = tic();
        monta = mide("MONTAR la linea (756 bins)     ", a, b, 10);
    }

    a = tic(); for (r = 0; r < 20; r++) { waterfall_push_line(s_lin); } b = tic();
    push = mide("push_line (meterla en el anillo)", a, b, 20);

    /*
     * EL CAMINO DE BUCLE. waterfall_blit() escribe solo las filas
     * empujadas desde el volcado anterior, cada una con gfx2_wf_fila().
     * Hay que quitarle antes la bandera de "esta vez, entero", que la
     * pone waterfall_init() y que haria el primer volcado completo.
     */
    waterfall_bucle_pon(1U);
    for (r = 0; r < 3; r++) { wf_vuelca(1); }

    a = tic(); for (r = 0; r < 20; r++) { wf_vuelca(1); } b = tic();
    v = mide("BUCLE, 1 linea pendiente       ", a, b, 20);
    base_bucle_1 = v - push;

    a = tic(); for (r = 0; r < 20; r++) { wf_vuelca(2); } b = tic();
    v = mide("BUCLE, 2 lineas (Muy rapida)   ", a, b, 20);
    base_bucle_2 = v - 2UL * push;

    /*
     * EL CAMINO DESPLAZADO, TAL COMO LO MIDE ESTE BANCO: con el IPA y el
     * DMA puestos, que es lo que simula m4/panel.c. O sea el CASO MEJOR
     * de los dos: la conversion de indice a color la hace el acelerador y
     * el empuje al panel lo hace el DMA, y aqui ninguno de los dos cuesta
     * nada. Lo que queda es la contabilidad de partir el anillo en tiras.
     */
    waterfall_bucle_pon(0U);
    for (r = 0; r < 3; r++) { wf_vuelca(1); }

    a = tic(); for (r = 0; r < 20; r++) { wf_vuelca(1); } b = tic();
    v = mide("DESPLAZADO (IPA+DMA puestos)   ", a, b, 20);
    base_despl = v - push;

    /*
     * Y EL MISMO CAMINO CON EL IPA APAGADO, que es un interruptor de la
     * ventana de Informacion y no un caso raro: ahi la CPU convierte las
     * 54.432 celdas con la LUT, una por una. Esto no se puede pedir por
     * waterfall_blit() -m4/panel.c dice siempre que el IPA esta- asi que
     * se llama al mismo gfx2_wf_blit() al que caeria, con el anillo y su
     * cabeza sacados de waterfall_row() (ver wf_base()).
     */
    {
        int16_t head;
        uint8_t *bse = wf_base(&head);

        a = tic();
        for (r = 0; r < 10; r++) {
            gfx2_wf_blit(SPC_TRACE_X, SPC_WF_Y, WATERFALL_WIDTH,
                         WATERFALL_ROWS, bse, WATERFALL_WIDTH, head,
                         spectrum_colormap_lut());
        }
        b = tic();
        cpu_despl = mide("DESPLAZADO sin IPA (la CPU)    ", a, b, 10);

        a = tic();
        for (r = 0; r < 20; r++) {
            gfx2_wf_fila(SPC_TRACE_X, SPC_WF_Y, WATERFALL_WIDTH,
                         bse, spectrum_colormap_lut());
        }
        b = tic();
        (void)mide("UNA fila suelta (gfx2_wf_fila) ", a, b, 20);
    }

    pr("\n  -- LAS CINCO VELOCIDADES DE k_wf, EL SEGUNDO ENTERO --\n");
    pr("  Cada volcado lleva montar la linea + empujarla + sacarla, y\n");
    pr("  en \"Muy rapida\" las dos primeras van DOS veces.\n\n");

    {
        /* mult, div, clave: la tabla k_wf de User/main.c tal cual. */
        static const struct { unsigned char mult, div, clave;
                              const char *nombre; } k[5] = {
            { 2, 1, 60, "Muy rapida" },
            { 1, 1, 30, "Rapida    " },
            { 1, 2, 15, "Media     " },
            { 1, 4,  7, "Lenta     " },
            { 1, 8,  3, "Muy lenta " }
        };
        unsigned j;

        for (j = 0; j < 5u; j++) {
            /* volcados por segundo, en centesimas: 30 cuadros / div */
            unsigned long v100 = (3000UL / (unsigned long)k[j].div);
            unsigned long bucle = (k[j].mult == 2U) ? base_bucle_2 : base_bucle_1;
            unsigned long prev = (unsigned long)k[j].mult * (push + monta);

            pr("  "); pr(k[j].nombre);
            pr("  "); prn(k[j].clave); pr(" lineas/s pedidas, ");
            prn(v100 / 100UL); pr(" volcados/s\n");

            pr("    en BUCLE      ");
            prn(bucle + prev); pr(" instrucciones por volcado\n");
            porcien(bucle + prev, v100);

            pr("    DESPLAZADO    ");
            prn(base_despl + prev); pr(" por volcado (IPA+DMA)\n");
            porcien(base_despl + prev, v100);

            pr("    DESPLAZADO    ");
            prn(cpu_despl + prev); pr(" por volcado (sin IPA)\n");
            porcien(cpu_despl + prev, v100);
            pr("\n");
        }
    }

    /* =============================================================== */
    pr("\n== LAS PRIMITIVAS DE gfx2.c ==\n\n");

    {
        gfx2_rgba_t co = gfx2_rgb(PAL_SURF_2);
        gfx2_rgba_t ci = gfx2_rgb(PAL_INK);
        gfx2_rgba_t ca = gfx2_rgba(PAL_ACCENT, 160);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_fill(&s_sf, 0, 0, GFX2_W, 64, co); } b = tic();
        v = mide("fill 800x64 (toda la banda)    ", a, b, 20);
        pr("        por pixel x1000: "); prn((v * 1000UL) / (800UL * 64UL)); pr("\n");

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_fill(&s_sf, 40, 8, 200, 40, co); } b = tic();
        (void)mide("fill 200x40                    ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_fill(&s_sf, 40, 8, 200, 40, ca); } b = tic();
        (void)mide("fill 200x40 CON alfa (mezcla)  ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_vgrad(&s_sf, 0, 0, GFX2_W, 64,
                       gfx2_rgb(PAL_HDR_TOP), co); } b = tic();
        (void)mide("vgrad 800x64 (con tramado)     ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_rrect(&s_sf, 40, 8, 200, 40, 8, co); } b = tic();
        (void)mide("rrect 200x40 r=8               ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_rrect_outline(&s_sf, 40, 8, 200, 40, 8, 2, ci); } b = tic();
        (void)mide("rrect_outline 200x40 r=8 t=2   ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_hline(&s_sf, 20, 32, 756, ci); } b = tic();
        (void)mide("hline de 756                   ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_vline(&s_sf, 400, 0, 64, ci); } b = tic();
        (void)mide("vline de 64                    ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_line(&s_sf, 20, 2, 776, 62, ci); } b = tic();
        v = mide("line diagonal 756x60           ", a, b, 20);
        pr("        por punto: "); prn(v / 756UL); pr("\n");

        /* Y lo mismo pero FUERA de la banda, que es el caso que de verdad
         * se repite: el codigo de dibujo corre una vez por banda y casi
         * todas las bandas no le tocan. Lo que importa es salir pronto. */
        a = tic(); for (r = 0; r < 20; r++) {
            gfx2_line(&s_sf, 20, 300, 776, 360, ci); } b = tic();
        (void)mide("line que NO toca la banda      ", a, b, 20);
    }

    /* La pantalla entera por el camino de verdad: gfx2_render_screen(),
     * que corre el dibujo una vez POR BANDA (480/8 = 60 bandas) y vuelca
     * cada una. Es el suelo de cualquier repintado completo. */
    a = tic(); for (r = 0; r < 5; r++) { gfx2_render_screen(fondo, 0); } b = tic();
    v = mide("render_screen de un fill liso  ", a, b, 5);
    porcien(v, 3000UL);

    a = tic(); for (r = 0; r < 20; r++) {
        gfx2_render(SPC_TRACE_X, SPC_WF_Y, WATERFALL_WIDTH, 40, fondo, 0); } b = tic();
    (void)mide("render de 756x40 (una ventana) ", a, b, 20);

    /* Y una ventana de boton, que es lo que de verdad se repinta cuando
     * se toca algo: ahi lo que se paga es abrir la ventana, no los
     * pixeles. */
    a = tic(); for (r = 0; r < 20; r++) {
        gfx2_render(6, 200, 104, 24, fondo, 0); } b = tic();
    (void)mide("render de 104x24 (un boton)    ", a, b, 20);

    /* =============================================================== */
    pr("\n== EL TEXTO Y LAS FUENTES (fonts_data.c) ==\n\n");

    {
        gfx2_rgba_t ci = gfx2_rgb(PAL_INK);
        int16_t nn;

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text_w(k_frase, &font_ui_14); } b = tic();
        v = mide("text_w, MEDIR 39 letras (ui_14)", a, b, 20);
        pr("        por letra: "); prn(v / 39UL); pr("\n");

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 10, k_frase, &font_ui_14, ci); } b = tic();
        x1 = mide("text,  PINTAR 39 letras (ui_14)", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 10, k_frase2, &font_ui_14, ci); } b = tic();
        x2 = mide("text,  PINTAR 70 letras (ui_14)", a, b, 20);
        pr("        UNA letra de ui_14: ");
        prn((x2 - x1) / (70UL - 39UL)); pr(" instrucciones\n");

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text_cabe(k_frase2, &font_ui_14, 300, &nn); } b = tic();
        (void)mide("text_cabe (recorte de 1 pasada)", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 10, k_frase, &font_ui_11, ci); } b = tic();
        (void)mide("39 letras en ui_11             ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 10, k_frase, &font_ui_14b, ci); } b = tic();
        (void)mide("39 letras en ui_14b (negrita)  ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 4, k_frase, &font_ui_18b, ci); } b = tic();
        (void)mide("39 letras en ui_18b            ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 4, "7.102,00", &font_num_20, ci); } b = tic();
        v = mide("8 cifras en num_20             ", a, b, 20);
        pr("        por cifra: "); prn(v / 8UL); pr("\n");

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 2, "7.102,00", &font_num_44, ci); } b = tic();
        v = mide("8 cifras en num_44 (el dial)   ", a, b, 20);
        pr("        por cifra: "); prn(v / 8UL); pr("\n");
        porcien(v, 3000UL);

        a = tic(); for (r = 0; r < 20; r++) {
            (void)gfx2_text(&s_sf, 10, 10, k_frase, &font_rain_16, ci); } b = tic();
        (void)mide("39 letras en rain_16           ", a, b, 20);
    }

    /* =============================================================== */
    pr("\n== LA FRANJA DE ARRIBA (ui_top.c) ==\n");
    pr("   Ojo: m4_cabecera.c ya mide esto. Aqui va con su cadencia.\n\n");

    ui_top_draw(&s_top);

    {
        static const char *const kf[4] =
            { "7.102,00", "7.103,00", "7.104,00", "7.105,00" };

        a = tic(); for (r = 0; r < 20; r++) {
            s_top.freq = kf[r & 3]; ui_top_draw_freq(&s_top); } b = tic();
        v = mide("la FRECUENCIA (caja del digito)", a, b, 20);
        porcien(v, 3000UL);

        /*
         * LO MISMO SIN RDS EN PANTALLA, y no es un capricho: la caja del
         * digito son unos 30 px de ancho, pero el dibujo de la franja
         * corre ENTERO dentro de esa ventana y recorta. Los rotulos que
         * quedan fuera no pintan ni un pixel... pero se recorren letra a
         * letra buscando cada glifo. Con la marquesina de RDS puesta eso
         * son sesenta letras de mas en CADA detente del mando.
         */
        s_top.rds = 0; s_top.rds_nombre = 0;
        a = tic(); for (r = 0; r < 20; r++) {
            s_top.freq = kf[r & 3]; ui_top_draw_freq(&s_top); } b = tic();
        v = mide("  la misma caja, SIN RDS       ", a, b, 20);
        porcien(v, 3000UL);
        s_top.rds = "SOMOS RNE - INFORMATIVO DE LAS 22:00 ";
        s_top.rds_nombre = "Radio Nacional de Espana";
        ui_top_draw(&s_top);

        a = tic(); for (r = 0; r < 20; r++) { ui_top_draw_status(&s_top); } b = tic();
        w = mide("la franja de ESTADO entera     ", a, b, 20);
        porcien(w, 3000UL);

        a = tic(); for (r = 0; r < 20; r++) { ui_top_draw(&s_top); } b = tic();
        v = mide("LAS DOS JUNTAS (ui_top_draw)   ", a, b, 20);
        porcien(v, 3000UL);

        a = tic(); for (r = 0; r < 20; r++) { ui_top_draw_smeter(&s_top); } b = tic();
        v = mide("solo el S-metro                ", a, b, 20);
        porcien(v, 3000UL);

        a = tic(); for (r = 0; r < 20; r++) {
            s_top.rds_off = (int16_t)(r * 2); ui_top_draw_rds(&s_top); } b = tic();
        v = mide("la marquesina de RDS           ", a, b, 20);
        porcien(v, 3000UL);
        s_top.rds_off = 0;

        a = tic(); for (r = 0; r < 20; r++) { ui_top_draw_chips(&s_top); } b = tic();
        (void)mide("solo los chips                 ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) { ui_top_draw_hora(&s_top); } b = tic();
        (void)mide("solo la hora                   ", a, b, 20);
    }

    /* =============================================================== */
    pr("\n== EL PANEL DIGITAL (ui_digi.c) ==\n\n");

    (void)ui_digi_draw_texto(&s_dig);

    a = tic(); for (r = 0; r < 10; r++) {
        ui_digi_olvida(); (void)ui_digi_draw_texto(&s_dig); } b = tic();
    v = mide("el panel ENTERO, 8 renglones   ", a, b, 10);
    porcien(v, 100UL);   /* un renglon nuevo por segundo, a 45 baudios */

    /*
     * ======================================================================
     * *** UNA LLAMADA ANTES DE CRONOMETRAR, Y POR QUE - 08/11/2026. ***
     * ======================================================================
     *
     * Aqui faltaba esta linea y el banco llevaba meses mintiendo. Justo
     * encima, el bucle del "panel ENTERO" termina con ui_digi_olvida(),
     * que pone s_fir_hay a cero: le dice a ui_digi_draw_cambios() que lo
     * que hay en la pantalla no es suyo. Asi que la PRIMERA de las diez
     * llamadas no comparaba nada, repintaba el panel entero -3.634.124
     * instrucciones- y las otras nueve si eran baratas. Repartido entre
     * diez salia 365.172, mas caro que repintar un renglon nuevo, y el
     * resumen decia que "no cambiar nada cuesta el doble que cambiar
     * algo", que es justo la clase de disparate que tendria que haber
     * hecho sospechar.
     *
     * Con una llamada de calentamiento, lo que se cronometra es lo que
     * se queria medir: comparar trece firmas y no pintar nada.
     */
    (void)ui_digi_draw_cambios(&s_dig);

    a = tic(); for (r = 0; r < 10; r++) {
        (void)ui_digi_draw_cambios(&s_dig); } b = tic();
    (void)mide("draw_cambios SIN cambios       ", a, b, 10);

    {
        static const char *const kr[2] = { "73 TU EE SK", "73 TU EE SK E" };
        a = tic(); for (r = 0; r < 10; r++) {
            s_dig.fila[7] = kr[r & 1];
            (void)ui_digi_draw_cambios(&s_dig); } b = tic();
        v = mide("draw_cambios con 1 renglon nvo ", a, b, 10);
        porcien(v, 450UL);   /* 45 baudios: ~4,5 letras/s, un renglon cada... */
        s_dig.fila[7] = k_filas[7];
    }

    a = tic(); for (r = 0; r < 20; r++) { ui_digi_draw_fila(&s_dig, 3U); } b = tic();
    (void)mide("UN renglon suelto              ", a, b, 20);

    a = tic(); for (r = 0; r < 20; r++) { ui_digi_draw_cabecera(&s_dig); } b = tic();
    (void)mide("la cabecera del panel          ", a, b, 20);

    a = tic(); for (r = 0; r < 20; r++) {
        s_dig.barra_v = (uint8_t)(r % 15); ui_digi_draw_barra(&s_dig); } b = tic();
    v = mide("la barra de progreso           ", a, b, 20);
    porcien(v, 3000UL);

    a = tic(); for (r = 0; r < 20; r++) { ui_digi_draw_chip(&s_dig); } b = tic();
    (void)mide("la chapa de estado             ", a, b, 20);

    a = tic(); for (r = 0; r < 20; r++) { ui_digi_draw_boton(&s_dig); } b = tic();
    (void)mide("un boton                       ", a, b, 20);

    /* =============================================================== */
    pr("\n== EL CROMO DEL ESPECTRO (spec_chrome.c) ==\n\n");

    spec_chrome_draw(&s_ch);

    a = tic(); for (r = 0; r < 10; r++) { spec_chrome_draw(&s_ch); } b = tic();
    v = mide("el cromo ENTERO                ", a, b, 10);
    porcien(v, 100UL);

    a = tic(); for (r = 0; r < 20; r++) {
        s_ch.center_hz = 7102000UL + (unsigned long)(r * 1000);
        spec_chrome_draw_ruler(&s_ch); } b = tic();
    v = mide("solo la REGLA (un detente)     ", a, b, 20);
    porcien(v, 3000UL);

    a = tic(); for (r = 0; r < 20; r++) {
        s_ch.db_min = -120.0f + (float)r * 0.5f;
        spec_chrome_draw_axis(&s_ch); } b = tic();
    v = mide("solo el EJE de dB + leyenda    ", a, b, 20);
    porcien(v, 3000UL);
    s_ch.db_min = -120.0f;

    {
        spec_chrome_t otro = s_ch;
        otro.center_hz += 1000UL;
        a = tic(); for (r = 0; r < 20; r++) {
            (void)spec_chrome_ruler_changed(&s_ch, &otro);
            (void)spec_chrome_axis_changed(&s_ch, &otro); } b = tic();
        (void)mide("preguntar si hay que repintar  ", a, b, 20);
    }

    /* =============================================================== */
    pr("\n== LO QUE SE CALCULA PARA DIBUJAR ==\n\n");

    {
        float mn = -120.0f, mx = -20.0f;

        a = tic(); for (r = 0; r < 20; r++) {
            spec_agc_step(s_db, 512u, &mn, &mx); } b = tic();
        v = mide("spec_agc_step, 512 bins        ", a, b, 20);
        porcien(v, 3000UL);

        a = tic(); for (r = 0; r < 20; r++) {
            spec_agc_step(s_db, 1024u, &mn, &mx); } b = tic();
        (void)mide("spec_agc_step, 1024 bins       ", a, b, 20);

        a = tic(); for (r = 0; r < 20; r++) {
            float o1, o2; spec_agc_target(s_db, 512u, &o1, &o2); } b = tic();
        (void)mide("spec_agc_target, 512 bins      ", a, b, 20);
    }

    {
        uint8_t enc;

        a = tic(); for (r = 0; r < 20; r++) {
            (void)spec_snap_bin(s_db, 1024u, 182.0f + (float)(r & 3),
                                SNAP_PICO, &enc); } b = tic();
        v = mide("spec_snap_bin, pico de AM      ", a, b, 20);
        porcien(v, 100UL);   /* un toque, no por cuadro */

        a = tic(); for (r = 0; r < 20; r++) {
            (void)spec_snap_bin(s_db, 1024u, 428.0f + (float)(r & 3),
                                SNAP_LSB, &enc); } b = tic();
        (void)mide("spec_snap_bin, borde de LSB    ", a, b, 20);
    }

    /* =============================================================== */
    pr("\n== EL CUADRO COMPLETO DE LA RADIO ==\n");
    pr("   traza (estilo Linea, el del usuario) + cascada, 30 veces/s\n\n");

    spectrum_set_style(SPECTRUM_STYLE_LINE);
    spectrum_set_line_smooth(1U);
    waterfall_bucle_pon(1U);
    for (r = 0; r < 3; r++) {
        spectrum_draw(s_db, 1024u, SPC_TRACE_X, SPC_Y, SPC_TRACE_W, SPC_TRACE_H,
                      -120.0f, -20.0f, 0, 1, -40, 40);
        wf_vuelca(1);
    }

    a = tic(); for (r = 0; r < 10; r++) {
        spectrum_draw(s_db, 1024u, SPC_TRACE_X, SPC_Y, SPC_TRACE_W, SPC_TRACE_H,
                      -120.0f, -20.0f, 0, 1, -40, 40);
        wf_vuelca(1); } b = tic();
    v = mide("cuadro con cascada en BUCLE    ", a, b, 10);
    porcien(v, 3000UL);

    waterfall_bucle_pon(0U);
    for (r = 0; r < 3; r++) { wf_vuelca(1); }
    a = tic(); for (r = 0; r < 10; r++) {
        spectrum_draw(s_db, 1024u, SPC_TRACE_X, SPC_Y, SPC_TRACE_W, SPC_TRACE_H,
                      -120.0f, -20.0f, 0, 1, -40, 40);
        wf_vuelca(1); } b = tic();
    v = mide("cuadro con cascada DESPLAZADA  ", a, b, 10);
    porcien(v, 3000UL);

    /* Y el mismo cuadro con la cascada desplazada PERO sin IPA, que es el
     * interruptor de la ventana de Informacion. Aqui la CPU hace las
     * 54.432 conversiones. */
    {
        int16_t head;
        uint8_t *bse = wf_base(&head);

        a = tic(); for (r = 0; r < 10; r++) {
            spectrum_draw(s_db, 1024u, SPC_TRACE_X, SPC_Y, SPC_TRACE_W,
                          SPC_TRACE_H, -120.0f, -20.0f, 0, 1, -40, 40);
            waterfall_push_line(s_lin);
            gfx2_wf_blit(SPC_TRACE_X, SPC_WF_Y, WATERFALL_WIDTH,
                         WATERFALL_ROWS, bse, WATERFALL_WIDTH, head,
                         spectrum_colormap_lut()); } b = tic();
        v = mide("cuadro, DESPLAZADA y sin IPA   ", a, b, 10);
        porcien(v, 3000UL);
    }

    pr("\n");
    pr("  choques por la banda compositora: ");
    prn((unsigned long)gfx2_banda_choques()); pr("\n");
    pila_di(0UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); }
    else          { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}

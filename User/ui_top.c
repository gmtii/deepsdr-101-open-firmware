#include "ui_top.h"
#include "idioma.h"
#include <string.h>
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_num_20.h"
#include "font_num_44.h"

/* --- utilidades de formato, sin stdio ---------------------------------------
 * Se evita snprintf: el firmware no la usa en ningun sitio, asi que la familia
 * printf de newlib-nano no esta validada en esta placa, y con nosys.specs una
 * peticion de memoria interna fallaria en silencio dejando la cadena vacia. */
static char *u2s(char *b, uint32_t v)
{
    char t[12];
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v);
    while (n) { b[i++] = t[--n]; }
    b[i] = 0;
    return b;
}

static char *i2s(char *b, int32_t v)
{
    if (v < 0) { b[0] = '-'; u2s(b + 1, (uint32_t)(-v)); }
    else       { u2s(b, (uint32_t)v); }
    return b;
}

/* ---------------------------------------------------------------------------
 * Zonas de toque de los chips, producidas por el propio dibujo.
 *
 * draw_status() se ejecuta una vez POR BANDA del compositor (dos bandas para
 * los 40 px de la barra), asi que la lista se reinicia al empezar cada banda
 * y se vuelve a llenar identica: la geometria no depende de la banda. Queda
 * bien igual por cual sea la ultima.
 * --------------------------------------------------------------------------- */
typedef struct { int16_t x, w; uint8_t id; } chip_hit_t;
static chip_hit_t s_hits[5];
static uint8_t    s_hits_n;

/*
 * Donde acaba la lectura de frecuencia - 22/09/2026, por el dueno del
 * proyecto: "estando en ajustes si toco arriba entre lsb y la hora sale esta
 * pantalla", la de meter la frecuencia a mano.
 *
 * El toque que abre esa pantalla lo decide main.c con FREQ_TAP_X1, que valia
 * MODE_X = 396: la X donde el rotulo de modo estaba en la cabecera ANTERIOR
 * al rediseno. Aqui el chip de modo se coloca detras del "kHz", o sea que
 * depende de lo ancha que sea la frecuencia, y acaba bastante antes de 396.
 * Resultado: un dedo de anchura de nada a la derecha del chip seguia contando
 * como "has tocado la frecuencia", y con los ajustes abiertos eso saltaba a
 * una pantalla que no tiene nada que ver.
 *
 * La geometria de la cabecera la sabe este fichero, que es el que la dibuja,
 * asi que la respuesta sale de aqui y no de una constante copiada en main.c.
 * Se anota al dibujar, igual que las zonas de los chips de arriba: la lectura
 * es de ancho variable ("7.159,00" no mide lo que "14.074,00") y recalcularla
 * seria tener la misma cuenta escrita dos veces.
 *
 * Valor de arranque prudente por si alguien pregunta antes del primer
 * dibujo: cero, o sea "ahi no hay frecuencia", que falla hacia no abrir una
 * pantalla que nadie ha pedido.
 */
static int16_t s_freq_x2 = 0;

/*
 * Lo mismo para el RELOJ, y por el mismo motivo: main.c abria el teclado de
 * poner la hora con x >= TIME_X - 10, o sea 680, que es donde estaba el
 * reloj en la cabecera vieja. Aqui el reloj va alineado a la DERECHA dentro
 * de una caja de 150 px, asi que su borde izquierdo depende de lo ancho que
 * sea el texto y cae bastante mas a la derecha de 680. Todo lo que quedaba
 * en medio era barra vacia que abria un teclado de hora.
 *
 * Los dos se anotan al dibujar y se leen con la misma forma; asi no hay dos
 * versiones de la misma geometria, que es exactamente lo que fallaba.
 */
static int16_t rds_x1(void);
static int16_t s_clock_x1 = 0;
static int16_t s_clock_x2 = 0;
static int16_t s_band_x1 = 0;
static int16_t s_band_x2 = 0;

uint8_t ui_top_freq_hit(uint16_t x, uint16_t y)
{
    return (uint8_t)(y < UI_TOP_H && (int16_t)x < s_freq_x2);
}

uint8_t ui_top_band_hit(uint16_t x, uint16_t y)
{
    if (s_band_x2 <= s_band_x1) { return 0U; }   /* no hay banda dibujada */
    return (uint8_t)(y < UI_TOP_H
                     && (int16_t)x >= (int16_t)(s_band_x1 - 4)
                     && (int16_t)x <  (int16_t)(s_band_x2 + 4));
}

uint8_t ui_top_clock_hit(uint16_t x, uint16_t y)
{
    /* Con margen: 10 px a cada lado, que en este panel resistivo son ~1,7 mm
     * y siguen sin tocar nada de al lado (a la izquierda no hay nada hasta el
     * error de PLL, a la derecha esta el borde de la pantalla). Vertical,
     * toda la cabecera menos la franja de abajo, donde vive la bateria. */
    if (s_clock_x2 <= s_clock_x1) { return 0U; } /* aun sin dibujar */
    return (uint8_t)(y < 34U
                     && (int16_t)x >= (int16_t)(s_clock_x1 - 10)
                     && (int16_t)x <  (int16_t)(s_clock_x2 + 10));
}

ui_top_hit_t ui_top_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    /*
     * Vertical: TODA la franja de estado, no solo los 24 px que mide el chip.
     * La zona de toque se hace mas grande que el dibujo a proposito - 24 px
     * son unos 4 mm en este panel, y este tactil resistivo ya pide tocar con
     * ganas. Los 40 px de la franja son ~6,8 mm, que sigue siendo poco pero
     * es todo el alto que hay. Nada mas vive en esa franja a esa altura, asi
     * que agrandar no le quita el toque a nadie.
     */
    if (y < UI_STATUS_Y || y >= (uint16_t)(UI_STATUS_Y + UI_STATUS_H)) {
        return UI_TOP_HIT_NONE;
    }
    for (i = 0U; i < s_hits_n; i++) {
        /* 3 px de margen a cada lado, por el mismo motivo. */
        if ((int16_t)x >= (int16_t)(s_hits[i].x - 3) &&
            (int16_t)x <  (int16_t)(s_hits[i].x + s_hits[i].w + 3)) {
            return (ui_top_hit_t)s_hits[i].id;
        }
    }
    return UI_TOP_HIT_NONE;
}

static const char *knob_name(ui_knob_t k)
{
    switch (k) {
    case UI_KNOB_TUNE:       return tr("Sintonía", "Tuning");
    case UI_KNOB_VOLUME:     return tr("Volumen", "Volume");
    case UI_KNOB_BACKLIGHT:  return tr("Brillo", "Brightness");
    case UI_KNOB_SCALE_LO:   return tr("Escala mín.", "Scale min.");
    case UI_KNOB_SCALE_HI:   return tr("Escala máx.", "Scale max.");
    case UI_KNOB_SQUELCH:    return tr("Silenciador", "Squelch");
    case UI_KNOB_SMOOTH:     return tr("Suavizado", "Smoothing");
    case UI_KNOB_PGA:        return tr("Ganancia", "Gain");
    case UI_KNOB_NR:         return tr("Reducción", "Noise red.");
    case UI_KNOB_RTTY_SHIFT: return "RTTY";
    case UI_KNOB_CW_TONE:    return tr("Tono CW", "CW pitch");
    default:                 return "?";
    }
}

/* ===========================================================================
 * CABECERA
 * =========================================================================== */
static void draw_header(gfx2_surf_t *s, void *ctx)
{
    const ui_top_state_t *st = (const ui_top_state_t *)ctx;
    gfx2_rgba_t ink = gfx2_rgb(PAL_INK);
    gfx2_rgba_t dim = gfx2_rgb(PAL_INK_DIM);
    gfx2_rgba_t acc = gfx2_rgb(PAL_ACCENT);
    int16_t x, i;
    const char *p;

    gfx2_vgrad(s, 0, 0, GFX2_W, UI_TOP_H,
               gfx2_rgb(PAL_HDR_TOP), gfx2_rgb(PAL_SURF_1));
    gfx2_hline(s, 0, UI_TOP_H - 1, GFX2_W, gfx2_rgb(PAL_LINE));

    /* --- lector de frecuencia, cifras tabulares ---
     * El digito que mueve el mando va SUBRAYADO en ambar, dentro del propio
     * numero. Asi el paso de sintonia se lee en la frecuencia y no hace falta
     * el "STEP 1kHz" de otra columna, que obliga a traducir mentalmente
     * cuanto se va a mover antes de girar. */
    x = 10;
    for (p = st->freq, i = 0; *p; p++, i++) {
        char b[2];
        int16_t aw;
        int hi = (i == st->freq_digit);
        b[0] = *p; b[1] = 0;
        aw = gfx2_text_w(b, &font_num_44);
        gfx2_text(s, x, 2, b, &font_num_44, hi ? acc : ink);
        if (hi) {
            gfx2_fill(s, x, 50, (int16_t)(aw - 2), 3, acc);
        }
        x = (int16_t)(x + aw);
    }
    gfx2_text(s, (int16_t)(x + 6), 26, "kHz", &font_ui_18b, dim); /* ETAPA 4: era "MHz" y el numero eran Hz - ver tune_freq_format_khz() en main.c */

    /* Fin de la lectura de frecuencia, para ui_top_freq_hit(): el final del
     * "kHz" mas un margen, y siempre por delante del chip de modo (que
     * empieza en x+62). Ver el comentario de s_freq_x2. */
    s_freq_x2 = (int16_t)(x + 6 + gfx2_text_w("kHz", &font_ui_18b) + 12);

    /* --- modo, como chip con acento --- */
    {
        int16_t cx = (int16_t)(x + 62);
        int16_t w  = (int16_t)(gfx2_text_w(st->mode, &font_ui_18b) + 26);
        gfx2_rrect(s, cx, 14, w, 32, 6, gfx2_rgba(PAL_ACCENT, 42));
        gfx2_rrect_outline(s, cx, 14, w, 32, 6, 1, gfx2_rgba(PAL_ACCENT, 170));
        gfx2_text_in(s, cx, 20, w, st->mode, &font_ui_18b, acc, GFX2_ALIGN_C);

        /*
         * --- banda, detras del modo ---
         *
         * En tono apagado y no con el acento del modo: el modo lo has elegido
         * tu y la banda es una consecuencia de donde estas. Que se distingan
         * de un vistazo es lo que evita leerlos como si fueran lo mismo.
         */
        s_band_x1 = s_band_x2 = 0;
        if (st->band) {
            int16_t bx = (int16_t)(cx + w + 10);
            int16_t bw = (int16_t)(gfx2_text_w(st->band, &font_ui_18b) + 22);
            gfx2_rrect(s, bx, 14, bw, 32, 6, gfx2_rgb(PAL_SURF_2));
            gfx2_rrect_outline(s, bx, 14, bw, 32, 6, 1, gfx2_rgb(PAL_LINE));
            gfx2_text_in(s, bx, 20, bw, st->band, &font_ui_18b, dim, GFX2_ALIGN_C);
            s_band_x1 = bx;
            s_band_x2 = (int16_t)(bx + bw);
        }
    }

    /* La marquesina de RDS NO se pinta aqui, aunque caiga dentro de la
     * cabecera: esta banda es de 800 px de ancho, asi que el texto se saldria
     * por debajo del reloj en vez de cortarse en su hueco. La pinta
     * ui_top_draw_rds(), que renderiza SOLO el hueco y por tanto recorta por
     * construccion. ui_top_draw() la llama al final. */

    /* --- error de PLL en SAM ---
     * Ocupa el mismo hueco que tenia antes, a la izquierda del reloj. En los
     * demas modos NO se pinta nada: la version anterior rellenaba ese
     * rectangulo con el gris de la barra para "limpiarlo", lo que con un
     * fondo plano era invisible y con el degradado de aqui dejaba un
     * cuadrado gris a la vista. Aqui el fondo lo pone el repintado de la
     * banda, asi que no hay nada que limpiar. */
    if (st->sam_ppm) {
        gfx2_text_in(s, 470, 40, 160, st->sam_ppm, &font_ui_14, dim,
                     GFX2_ALIGN_R);
    }

    /* --- reloj arriba a la derecha --- */
    gfx2_text_in(s, 640, 6, 150, st->clock, &font_num_20, ink, GFX2_ALIGN_R);
    /* Donde ha caido de verdad, para ui_top_clock_hit(): alineado a la
     * derecha dentro de la caja, o sea que el borde izquierdo se calcula, no
     * se supone. Ver s_clock_x1/x2. */
    {
        int16_t w = gfx2_text_w(st->clock, &font_num_20);
        s_clock_x2 = (int16_t)(640 + 150);
        s_clock_x1 = (int16_t)(s_clock_x2 - w);
    }

    /* --- bateria debajo del reloj, en la fila que ya ocupaba --- */
    {
        int16_t bw = 40, bh = 15;
        int16_t bx = (int16_t)(790 - bw - 2);
        int16_t by = 38;
        int16_t fill = (int16_t)((uint32_t)(bw - 4) * st->batt_pct / 100u);
        uint32_t col = (st->batt_pct > 25u) ? PAL_OK : PAL_CRIT;

        gfx2_rrect_outline(s, bx, by, bw, bh, 3, 2, gfx2_rgb(PAL_INK_MUTE));
        gfx2_rrect(s, (int16_t)(bx + bw), (int16_t)(by + 4), 3, 7, 1,
                   gfx2_rgb(PAL_INK_MUTE));
        if (fill > 0) {
            gfx2_fill(s, (int16_t)(bx + 2), (int16_t)(by + 2), fill,
                      (int16_t)(bh - 4), gfx2_rgb(col));
        }
        if (st->batt_volts) {
            gfx2_text_in(s, 620, (int16_t)(by + 1), (int16_t)(bx - 624),
                         st->batt_volts, &font_ui_14, dim, GFX2_ALIGN_R);
        }
    }
}

/* ===========================================================================
 * BARRA DE ESTADO
 * =========================================================================== */
static int16_t chip_w(const char *label, const char *value, int active);

static void chip(gfx2_surf_t *s, int16_t x, int16_t y,
                 const char *label, const char *value, int active)
{
    /*
     * EL ANCHO SALE DE chip_w(), NO DE UNA COPIA - 30/09/2026.
     *
     * Aqui habia la misma cuenta escrita otra vez, y con OTRO numero: +18
     * mientras chip_w() pone +16 (y su propio comentario habla de 14). Tres
     * numeros para la misma cosa.
     *
     * Quien reparte los chips de derecha a izquierda usa chip_w(), asi que
     * cada chip se pintaba 2 px mas ancho de lo que el reparto le habia
     * guardado: el ultimo pixel caia en x=791 con ST_CHIP_R en 790, y el aire
     * entre chips quedaba en 6 px en vez de los 8 de ST_CHIP_GAP. No se
     * solapaba nada -el sobrante crece hacia el hueco- ni la zona de toque se
     * quedaba corta, pero sim/chipstest.c medía con chip_w(), o sea con el
     * numero que NO era el que se dibujaba.
     */
    int16_t lw = gfx2_text_w(label, active ? &font_ui_14b : &font_ui_14);
    int16_t w  = chip_w(label, value, active);

    /*
     * Activo y apagado se distinguen por TRES cosas a la vez: relleno frente
     * a solo contorno, texto claro frente a apagado, y color de acento.
     *
     * El color es el que mejor se lee de un vistazo, y por eso esta - es el
     * mismo ambar de "Sintonia" y del chip de modo, asi que "encendido" tiene
     * un unico color en toda la interfaz. Pero NO va solo: el relleno y el
     * contraste dicen lo mismo sin depender de la vision del color, que es lo
     * que hace falta porque el verde y el rojo de estado quedan
     * indistinguibles en deuteranopia.
     *
     * Antes esto era "RNR si" / "RNR no", que es una traduccion literal de un
     * booleano y obliga a leer dos palabras para enterarte de una.
     */
    if (active) {
        gfx2_rrect(s, x, y, w, 24, 6, gfx2_rgb(PAL_SURF_3));
        gfx2_rrect_outline(s, x, y, w, 24, 6, 1, gfx2_rgba(PAL_ACCENT, 150));
    } else {
        gfx2_rrect_outline(s, x, y, w, 24, 6, 1, gfx2_rgb(PAL_LINE));
    }

    gfx2_text(s, (int16_t)(x + 9), (int16_t)(y + 4), label,
              active ? &font_ui_14b : &font_ui_14,
              gfx2_rgb(active ? (value ? PAL_INK_DIM : PAL_ACCENT)
                              : PAL_INK_MUTE));
    if (value) {
        gfx2_text(s, (int16_t)(x + 9 + lw + 6), (int16_t)(y + 4), value,
                  &font_ui_14b,
                  gfx2_rgb(active ? PAL_ACCENT : PAL_INK_MUTE));
    }
}

static int16_t chip_w(const char *label, const char *value, int active)
{
    return (int16_t)(gfx2_text_w(label, active ? &font_ui_14b : &font_ui_14)
                     /* 14 y no 18 (23/09/2026): con el chip del NCO, los
                      * cuatro fijos -AGC, BW, NR, NCO- sumaban 263 px y solo
                      * hay 246 en el peor caso de la pastilla del mando, asi
                      * que el NCO se caia siempre. Cuatro pixeles por chip
                      * son los que faltaban. Lo comprueba sim/chipstest.c. */
                     + (value ? gfx2_text_w(value, &font_ui_14b) + 6 : 0) + 16);
}

/*
 * Reparto horizontal de la barra de estado, en franjas fijas.
 *
 * Antes cada elemento se colocaba a partir de donde acababa el anterior, con
 * saltos ajustados a ojo. Eso se rompe en cuanto un texto crece: "S9+14" ocupa
 * mas que "S6", "Silenciador" mas que "Brillo", y el resultado era que la
 * lectura en dBm se metia debajo de la pastilla del mando. Con franjas fijas,
 * cada cosa tiene su sitio y el peor caso se comprueba una vez aqui:
 *
 *   10..160   barra del S-meter        (150)
 *  168..318   unidades S + dBm         (150) -> peor caso medido: 141 px
 *  326..530   pastilla del mando       (204) -> peor caso medido: ver abajo
 *  542..790   chips, alineados a la derecha
 *
 * El peor caso de la pastilla NO se estima: sim/toptest.c lo recorre con los
 * nueve nombres de mando y el valor mas largo, y avisa si se sale. La primera
 * version daba 194 px contra 180 disponibles.
 */
#define ST_BAR_X    10
/*
 * 120 y no 150, y la lectura 30 px a la izquierda - 23/09/2026, por el dueno:
 * "los indicadores de nr nco agc estan muy juntos hay que separarlos un
 * pelin... si te falta espacio haz un pelin mas pequeño el smeter".
 *
 * Y tiene razon en las dos cosas. Para meter el chip del NCO se habian
 * apretado los margenes de los chips (de 18 a 12) y el hueco entre ellos (de
 * 6 a 4), que es lo que hace que se lean como un bloque en vez de como
 * cuatro cosas. Los 30 px que suelta la barra vuelven ahi: margen 16, hueco
 * 8. La barra sigue teniendo su escala de unidades S marcada y no pierde
 * resolucion -la posicion se calcula en milesimas sobre su anchura, sea la
 * que sea-, solo mide 30 px menos.
 */
#define ST_BAR_W   120
#define ST_READ_X  138
#define ST_READ_W  150
#define ST_KNOB_X  284   /* 284: 314 menos los 30 que suelta la barra. Con la
                            * sobrecarga fuera
                            * de la lectura, el peor caso de esta -"S9+60" mas
                            * "AGC activo"- acaba en x=314, que es donde puede
                            * empezar la pastilla. Doce pixeles mas para los
                            * chips de la derecha. */
#define ST_KNOB_W  204
#define ST_CHIP_R  790
/* El hueco entre chips. Es un #define y no un 8 suelto porque lo usan DOS
 * sitios -el bucle que los dibuja y la cuenta que limita la pastilla del
 * mando- y ya se desincronizaron una vez: la cuenta seguia con 4 mientras el
 * dibujo usaba 8, y el chip del NCO se caia otra vez sin que se entendiera
 * por que. */
#define ST_CHIP_GAP  8

static int16_t s_knob_right = 0;   /* donde acaba la pastilla del mando */

static void draw_status(gfx2_surf_t *s, void *ctx)
{
    const ui_top_state_t *st = (const ui_top_state_t *)ctx;
    const int16_t Y = UI_STATUS_Y;
    char buf[24];
    int16_t x;

    gfx2_fill(s, 0, Y, GFX2_W, UI_STATUS_H, gfx2_rgb(PAL_SURF_0));

    /* --- S-meter: barra continua con marcas de unidad S ---
     * El de 12 segmentos del firmware actual da 7 dB por segmento, tan grueso
     * que el numero de al lado hacia todo el trabajo. Una barra continua con
     * la escala marcada se lee de un vistazo Y conserva la precision. */
    {
        int16_t mx = ST_BAR_X, my = (int16_t)(Y + 12), mw = ST_BAR_W, mh = 14;
        int16_t fw, i;
        uint32_t t1000;   /* posicion 0..1000, en enteros: sin float */

        /* S1..S9 ocupan el 62% de la barra y el resto es S9+, que es la
         * escala real de un S-meter: 6 dB por unidad hasta S9, dB sueltos
         * por encima. */
        if (st->s_units > 9u) {
            uint32_t o = st->over_db > 60u ? 60u : st->over_db;
            t1000 = 620u + o * 380u / 60u;
        } else {
            t1000 = (uint32_t)st->s_units * 620u / 9u;
        }
        if (t1000 > 1000u) t1000 = 1000u;

        gfx2_rrect(s, mx, my, mw, mh, 4, gfx2_rgb(PAL_SURF_2));
        fw = (int16_t)((uint32_t)mw * t1000 / 1000u);
        if (fw > 3) {
            gfx2_rrect(s, mx, my, fw, mh, 4, gfx2_rgb(PAL_TRACE));
            if (t1000 > 620u) {
                int16_t x0 = (int16_t)(mx + (int16_t)((uint32_t)mw * 620u / 1000u));
                gfx2_fill(s, x0, my, (int16_t)(mx + fw - x0), mh,
                          gfx2_rgb(PAL_ACCENT));
            }
        }
        for (i = 1; i <= 9; i += 2) {
            int16_t tx = (int16_t)(mx + (int16_t)((uint32_t)mw * (uint32_t)i * 620u / 9u / 1000u));
            gfx2_vline(s, tx, (int16_t)(my + mh + 1), 3, gfx2_rgb(PAL_INK_MUTE));
        }

        /* lectura: unidades S grandes, dBm como dato secundario */
        x = ST_READ_X;
        if (st->ovr) {
            /*
             * SOBRECARGA, 23/09/2026. Antes era un chip mas, a la derecha del
             * todo, y ocupaba 101 px de texto de los ~224 que hay como mucho
             * para chips: empujaba fuera a BW y a NR, o sea que justo cuando
             * la entrada se satura la pantalla dejaba de decirte que filtro y
             * que reduccion de ruido tienes puestos. Lo midio sim/chipstest.c.
             *
             * Y sustituye a la lectura ENTERA -unidades S y dBm-, no solo al
             * dBm, porque con la entrada saturada las dos mienten igual: la
             * barra esta clavada arriba y el numero es basura. El aviso ocupa
             * el sitio de algo que no vale, en vez de tapar un dato bueno.
             * Sigue siendo la palabra entera y en rojo, nunca solo el color.
             */
            gfx2_text(s, x, (int16_t)(Y + 10), tr("SOBRECARGA", "OVERLOAD"), &font_ui_14b,
                      gfx2_rgb(PAL_CRIT));
        } else {
            if (st->s_units > 9u) {
                int i2 = 0;
                buf[i2++] = 'S'; buf[i2++] = '9'; buf[i2++] = '+';
                u2s(&buf[i2], st->over_db);
            } else {
                buf[0] = 'S';
                u2s(&buf[1], st->s_units);
            }
            /* Se mide el ancho real en vez de saltar una cantidad fija: "S6"
             * y "S9+14" no ocupan lo mismo, y con el salto fijo el dBm se
             * pegaba a las unidades S en cuanto la lectura pasaba de S9. */
            x = (int16_t)(x + gfx2_text(s, x, (int16_t)(Y + 8), buf, &font_ui_18b,
                                        gfx2_rgb(PAL_INK)) + 12);
            if (st->dbm_valid) {
                int16_t w = gfx2_text(s, x, (int16_t)(Y + 12), i2s(buf, st->dbm),
                                      &font_ui_14b, gfx2_rgb(PAL_INK_DIM));
                gfx2_text(s, (int16_t)(x + w + 3), (int16_t)(Y + 12), "dBm",
                          &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
            } else {
                /* Con el AGC activo la lectura en dBm no significa nada: se
                 * dice, en vez de mostrar un numero que parece bueno y no lo
                 * es. El texto dice lo que pasa, sin abreviaturas: "n/d" no le
                 * dice nada a nadie, y puesto justo donde iria el numero,
                 * "AGC activo" ya explica por que no hay numero.
                 *
                 * Ojo con el texto: la raya larga (U+2014) NO esta en el juego
                 * de caracteres de las fuentes generadas, y saldria como un
                 * hueco. */
                gfx2_text(s, x, (int16_t)(Y + 12), tr("AGC activo", "AGC active"), &font_ui_14,
                          gfx2_rgb(PAL_INK_MUTE));
            }
        }
    }

    /* --- destino del mando, siempre en el mismo sitio ---
     * Es la respuesta a "que hace el mando ahora mismo", que hoy depende de lo
     * ultimo que se abrio en un menu y no esta escrita en ninguna parte. */
    {
        const char *nm = (st->knob_name ? st->knob_name : knob_name(st->knob));
        char nmbuf[24];
        int16_t kx = ST_KNOB_X;
        int16_t kw;
        /*
         * QUIEN CEDE CUANDO NO CABE TODO - 23/09/2026.
         *
         * Antes cedian los chips de la derecha: se dibujan de derecha a
         * izquierda y el que no cabia se saltaba. Es a prueba de solapes,
         * pero tiene la otra cara - un indicador que desaparece miente por
         * omision, y sim/chipstest.c midio que con el mando enganchado a
         * "Desplazamiento" el chip del NCO se caia siempre.
         *
         * Asi que se le da la vuelta a la prioridad: los cuatro chips de
         * ESTADO (AGC, BW, NR, NCO) son fijos y siempre estan, y lo que se
         * acorta es el NOMBRE del ajuste enganchado, que es lo que varia de
         * verdad -de "AGC" a "Desplazamiento"- y ademas es temporal. El
         * valor no se toca: es lo que estas mirando mientras giras.
         */
        {
            int16_t fijos = (int16_t)(chip_w("AGC", st->agc,
                                             (st->agc && st->agc[0] != 'O')) + ST_CHIP_GAP
                                      + chip_w("NR", 0, st->nr_on) + ST_CHIP_GAP
                                      + chip_w("NCO", 0, st->nco_on) + ST_CHIP_GAP);
            int16_t kw_max;
            if (st->bw) { fijos = (int16_t)(fijos + chip_w("BW", st->bw, 1) + ST_CHIP_GAP); }
            kw_max = (int16_t)(ST_CHIP_R - fijos - 10 - kx);

            kw = (int16_t)(gfx2_text_w(nm, &font_ui_14b)
                           + (st->knob_value ? gfx2_text_w(st->knob_value, &font_ui_14b) + 8 : 0)
                           + 40);   /* 40 y no 44, misma razon que chip_w() */
            if (kw > kw_max) {
                /*
                 * Acortar el nombre hasta que quepa, con un punto al final.
                 * Se corta por BYTES pero respetando UTF-8: los nombres
                 * llevan acentos ("Reduccion" no, pero "Inversion" si), y
                 * partir un caracter por la mitad pinta un hueco raro. Los
                 * bytes 0x80..0xBF son continuacion, nunca principio.
                 */
                size_t n = strlen(nm);
                if (n > sizeof nmbuf - 2U) { n = sizeof nmbuf - 2U; }
                memcpy(nmbuf, nm, n);
                while (n > 1U) {
                    n--;
                    while (n > 1U && (nmbuf[n] & 0xC0) == 0x80) { n--; }
                    nmbuf[n] = '.';
                    nmbuf[n + 1U] = '\0';
                    kw = (int16_t)(gfx2_text_w(nmbuf, &font_ui_14b)
                                   + (st->knob_value ? gfx2_text_w(st->knob_value, &font_ui_14b) + 8 : 0)
                                   + 40);
                    if (kw <= kw_max) { break; }
                }
                nm = nmbuf;
            }
        }
        s_knob_right = (int16_t)(kx + kw);
        gfx2_rrect(s, kx, (int16_t)(Y + 8), kw, 24, 12, gfx2_rgba(PAL_ACCENT, 38));
        gfx2_rrect_outline(s, kx, (int16_t)(Y + 8), kw, 24, 12, 1,
                           gfx2_rgba(PAL_ACCENT, 150));
        /* iconito de mando: circulo con muesca */
        gfx2_rrect(s, (int16_t)(kx + 8), (int16_t)(Y + 14), 12, 12, 6,
                   gfx2_rgb(PAL_ACCENT));
        gfx2_fill(s, (int16_t)(kx + 13), (int16_t)(Y + 14), 2, 4,
                  gfx2_rgb(PAL_SURF_0));
        {
            int16_t tx = (int16_t)(kx + 28);
            tx = (int16_t)(tx + gfx2_text(s, tx, (int16_t)(Y + 12), nm,
                                          &font_ui_14b, gfx2_rgb(PAL_ACCENT)));
            if (st->knob_value) {
                gfx2_text(s, (int16_t)(tx + 8), (int16_t)(Y + 12),
                          st->knob_value, &font_ui_14b, gfx2_rgb(PAL_INK));
            }
        }
    }

    /*
     * --- chips, de derecha a izquierda y por PRIORIDAD ---
     *
     * Cada chip se dibuja solo si cabe entre el borde derecho y donde acaba
     * la pastilla del mando. Si no cabe, se salta.
     *
     * Asi el solape es imposible POR CONSTRUCCION, en vez de depender de que
     * yo haya sumado bien los anchos de todos los textos posibles - que es
     * justo lo que fallo al anadir "SIN ALTAVOZ": las cuentas cuadraban hasta
     * que aparecio un chip mas y "SOBRECARGA" se metio encima de la pastilla.
     *
     * El orden es el de importancia, porque los ultimos son los que caen:
     * una sobrecarga hay que verla siempre; que el altavoz este mudo se nota
     * sin necesidad de leerlo.
     */
    {
        /* Seis, no cinco: al anadir el chip del NCO (23/09/2026) esto se
         * queda justo, y pasarse seria escribir fuera del array sin que
         * ningun aviso lo diga. El _Static_assert de debajo del bucle ata el
         * tamano al numero de chips que se meten de verdad. */
        const char *lbl[6];
        const char *val[6];
        uint8_t     act[6];
        uint8_t     id[6];
        uint8_t     n = 0U, i;
        int16_t     limite = (int16_t)(s_knob_right + 10);

        s_hits_n = 0U;

        /* "OFF" no es un estado activo: el chip se apaga con el, en vez de
         * quedarse encendido anunciando que no hace nada. */
        lbl[n] = "AGC"; val[n] = st->agc;
        act[n] = (uint8_t)(st->agc && st->agc[0] != 'O');
        id[n] = UI_TOP_HIT_AGC; n++;
        /* ETAPA 4: el ancho del filtro vuelve a verse. Va por delante de NR
         * porque se consulta mas: es lo que decide si la emisora de al lado
         * te entra. */
        if (st->bw) { lbl[n] = "BW"; val[n] = st->bw; act[n] = 1U;
                      id[n] = UI_TOP_HIT_BW; n++; }
        lbl[n] = "NR";  val[n] = 0; act[n] = st->nr_on;
        id[n] = UI_TOP_HIT_NR; n++;
        /* Justo al lado del NR, que es donde lo pidio el dueno. Igual que
         * el: siempre esta, encendido o apagado, para que el sitio no baile
         * segun el estado. */
        lbl[n] = "NCO"; val[n] = 0; act[n] = st->nco_on;
        id[n] = UI_TOP_HIT_NCO; n++;
        if (st->spk_muted) { lbl[n] = tr("MUDO", "MUTED"); val[n] = 0; act[n] = 1U;
                             id[n] = UI_TOP_HIT_SPK; n++; }
        /* Que el array siga siendo mas grande que el numero maximo de chips
         * que se pueden llegar a meter. No se puede comprobar en tiempo de
         * compilacion porque n depende del estado, asi que se comprueba aqui:
         * si algun dia se anade uno mas y se olvida crecer el array, esto lo
         * para en vez de escribir en la pila de al lado. */
        if (n > (uint8_t)(sizeof lbl / sizeof lbl[0])) {
            n = (uint8_t)(sizeof lbl / sizeof lbl[0]);
        }

        x = ST_CHIP_R;
        for (i = 0U; i < n; i++) {
            int16_t w = chip_w(lbl[i], val[i], act[i] ? 1 : 0);
            if ((int16_t)(x - w) < limite) {
                break;   /* ni este ni los siguientes caben */
            }
            x = (int16_t)(x - w);
            if (s_hits_n < (uint8_t)(sizeof s_hits / sizeof s_hits[0])) {
                s_hits[s_hits_n].x  = x;
                s_hits[s_hits_n].w  = w;
                s_hits[s_hits_n].id = id[i];
                s_hits_n++;
            }
            /* El 2 -el chip rojo de sobrecarga- ya no existe aqui: la
             * sobrecarga se avisa en la zona de la lectura, ver mas arriba. */
            chip(s, x, (int16_t)(Y + 8), lbl[i], val[i], act[i]);
            x = (int16_t)(x - ST_CHIP_GAP);
        }
    }
}

/* ---------------------------------------------------------------- marquesina */

/* Donde empieza el hueco: justo detras de la pastilla de banda. Si aun no se
 * ha dibujado ninguna (arranque), se usa un sitio razonable para no partir
 * por cero. */
static int16_t rds_x1(void)
{
    return (int16_t)((s_band_x2 > 0) ? (s_band_x2 + 12) : 470);
}

int16_t ui_top_rds_ancho(void)
{
    int16_t w = (int16_t)(UI_TOP_RDS_X2 - rds_x1());
    return (w > 40) ? w : 0;
}

static void rds_franja(gfx2_surf_t *s, void *ctx)
{
    const ui_top_state_t *st = (const ui_top_state_t *)ctx;
    int16_t x1 = rds_x1();

    /*
     * El fondo es el MISMO degradado de la cabecera, pedido en coordenadas
     * absolutas y de altura completa: la banda lo recorta a la franja, asi
     * que el trozo que se pinta encaja exactamente con lo que hay a los
     * lados. Rellenar con un color plano dejaria un rectangulo a la vista en
     * cuanto la paleta tenga degradado, que es siempre.
     */
    gfx2_vgrad(s, x1, 0, (int16_t)(UI_TOP_RDS_X2 - x1), UI_TOP_H,
               gfx2_rgb(PAL_HDR_TOP), gfx2_rgb(PAL_SURF_1));

    /* El nombre, en tinta viva. Quieto mientras quepa; ver rds_nom_off. */
    if (st->rds_nombre && st->rds_nombre[0] != '\0') {
        gfx2_text(s, (int16_t)(x1 - st->rds_nom_off),
                  (int16_t)UI_TOP_RDS_NOM_Y, st->rds_nombre,
                  &font_ui_14b, gfx2_rgb(PAL_INK));
    }
    /* Y la marquesina debajo, apagada: es contexto, no identidad. */
    if (st->rds && st->rds[0] != '\0') {
        gfx2_text(s, (int16_t)(x1 - st->rds_off), (int16_t)UI_TOP_RDS_MAR_Y,
                  st->rds, &font_ui_14, gfx2_rgb(PAL_INK_DIM));
    }

    /*
     * Y EL ERROR DE PLL DE SAM, QUE VIVE DENTRO DE ESTA MISMA FRANJA.
     * 28/09/2026.
     *
     * *** Es la QUINTA vez que aparece esta familia de fallo en el
     * proyecto: una funcion que repinta una franja FIJA y borra algo que
     * dibuja otro dentro de ella. Las cuatro anteriores fueron el tercer
     * boton con la barra, el cuarto con la chapa, y el del mapa con
     * solo_chip() -esa, tres veces seguidas-. ***
     *
     * Medido antes de tocar nada: la huella del PPM son 304 px en
     * x[566..628] y[42..55], y 300 de ellos -el 98%- caen dentro de esta
     * franja. Poniendo y quitando st->sam_ppm sobre el dibujado completo
     * cambian 4 pixeles de 384.000, que son los dos renglones de cola por
     * debajo de y=54, adonde la franja no llega. O sea que el numero NO SE
     * VE, y es el unico indicador de si el enganche de SAM es bueno.
     *
     * Y NO BASTA CON REORDENAR ui_top_draw(). La franja se repinta tambien
     * desde emis_marq_poll() y emisoras_tick() (main.c), en cualquier modo
     * que no sea WFM -SAM incluido- cada RDS_MARQ_MS. Arreglarlo solo en el
     * dibujado completo lo dejaria borrado igual dos decimas de segundo
     * despues.
     *
     * Asi que se repinta AQUI DENTRO, detras del degradado, que es
     * exactamente lo que hace solo_chip() con los botones cuarto y quinto
     * por este mismo motivo.
     */
    if (st->sam_ppm && st->sam_ppm[0] != '\0') {
        gfx2_text_in(s, 470, 40, 160, st->sam_ppm, &font_ui_14,
                     gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_R);
    }
}

int16_t ui_top_rds_nombre_w(const char *t)
{
    /* font_ui_14b, la misma con la que se pinta dos funciones mas arriba.
     * Medir con la fina daria de menos y el nombre volveria por la derecha
     * con el final todavia dentro. */
    return (t && t[0] != '\0') ? gfx2_text_w(t, &font_ui_14b) : 0;
}

int16_t ui_top_rds_texto_w(const char *t)
{
    /* La MISMA fuente con la que se pinta la marquesina: medir con otra es
     * como se consigue que el texto de la vuelta antes o despues de tiempo. */
    return (t && t[0] != '\0') ? gfx2_text_w(t, &font_ui_14) : 0;
}

void ui_top_draw_rds(const ui_top_state_t *st)
{
    int16_t w = ui_top_rds_ancho();
    if (w <= 0) { return; }
    gfx2_render(rds_x1(), UI_TOP_RDS_Y, w, UI_TOP_RDS_H, rds_franja, (void *)st);
}

/* =========================================================================== */
void ui_top_draw(const ui_top_state_t *st)
{
    gfx2_render(0, 0, GFX2_W, UI_TOP_H, draw_header, (void *)st);
    /* Despues de la cabecera y no dentro: necesita su propia banda para
     * recortarse al hueco. Ver draw_header(). */
    ui_top_draw_rds(st);
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
}

void ui_top_draw_status(const ui_top_state_t *st)
{
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
}

/*
 * SOLO LA PARTE QUE SE MUEVE CON LA SEÑAL - 30/09/2026.
 *
 * *** Por el dueño del proyecto: "se sigue ralentizando cuando recibe
 * señal". ***
 *
 * Y tenia razon, aunque el espectro no tenga nada que ver: medido con
 * lcd_sim, pintar el espectro cuesta 155.002 accesos al bus HAYA O NO HAYA
 * señal - es exactamente el mismo trabajo-. Lo que cambia con la señal es
 * otra cosa: la barra de S se mueve, y smeter_draw() repinta LA FRANJA
 * ENTERA, 800 x 40 = 32.051 accesos mas por fotograma. Sin señal la barra no
 * se mueve, smeter_draw() sale por la primera linea y esos 32.051 no se
 * gastan. De ahi que con señal el fotograma cueste un 21 % mas.
 *
 * Pero de esa franja, lo unico que cambia con la señal vive a la izquierda
 * de ST_KNOB_X: la barra, las unidades S, el dBm y el aviso de sobrecarga.
 * El destino del mando y los chips de la derecha no se mueven.
 *
 * Asi que esto pinta el MISMO dibujo con la ventana recortada a esa parte.
 * No hay una segunda funcion de dibujo que mantener al dia: es draw_status()
 * otra vez, y lo que cae fuera de la ventana lo recorta gfx2. 284 de 800 px,
 * o sea 11.379 accesos en vez de 32.051.
 */
/*
 * El ancho: hasta donde empieza el destino del mando, MAS seis pixeles de
 * aire. El peor caso de la lectura -"S9+60" seguido de "AGC activo" o de
 * "AGC active"- acaba justo en 284, o sea con CERO margen, y una traduccion
 * o una fuente distinta lo recortaria un pixel sin que se notara mas que
 * como una letra rara. Los seis de mas repintan el borde izquierdo de la
 * pastilla del mando, que se dibuja igual de bien recortada. Lo vigila
 * sim/top_franjas.c.
 */
#define ST_MOVIL_W  (ST_KNOB_X + 6)

void ui_top_draw_smeter(const ui_top_state_t *st)
{
    gfx2_render(0, UI_STATUS_Y, ST_MOVIL_W, UI_STATUS_H, draw_status, (void *)st);
}

/* Para el banco: ver ui_top_chip_w_dbg(). El enlazador la tira. */
int16_t ui_top_movil_w_dbg(void)  { return (int16_t)ST_MOVIL_W; }
int16_t ui_top_read_x_dbg(void)   { return (int16_t)ST_READ_X; }

/*
 * PARA LOS BANCOS DEL SIMULADOR, y por eso estan aqui y no alli.
 *
 * sim/chipstest.c necesita saber cuanto ocupa un chip y cuanto un texto para
 * poder decir cuanto sitio falta. Reimplementar esas cuentas en el banco es
 * exactamente como se consigue que el banco mida una cosa y la radio otra -
 * ya paso con la altura de los botones de categoria-, asi que se exponen las
 * de aqui.
 *
 * En el firmware no las llama nadie: con -ffunction-sections y --gc-sections
 * el enlazador las tira, asi que no cuestan ni un byte en la placa.
 */
int16_t ui_top_chip_w_dbg(const char *label, const char *value)
{
    return chip_w(label, value, 1);
}

int16_t ui_top_txtw_dbg(const char *t, int f)
{
    return gfx2_text_w(t, f == 18 ? &font_ui_18b : (f == 14 ? &font_ui_14b : &font_ui_14));
}

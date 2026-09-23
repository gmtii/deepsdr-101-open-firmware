#include "ui_top.h"
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
static int16_t s_clock_x1 = 0;
static int16_t s_clock_x2 = 0;

uint8_t ui_top_freq_hit(uint16_t x, uint16_t y)
{
    return (uint8_t)(y < UI_TOP_H && (int16_t)x < s_freq_x2);
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
    case UI_KNOB_TUNE:       return "Sintonía";
    case UI_KNOB_VOLUME:     return "Volumen";
    case UI_KNOB_BACKLIGHT:  return "Brillo";
    case UI_KNOB_SCALE_LO:   return "Escala mín.";
    case UI_KNOB_SCALE_HI:   return "Escala máx.";
    case UI_KNOB_SQUELCH:    return "Silenciador";
    case UI_KNOB_SMOOTH:     return "Suavizado";
    case UI_KNOB_PGA:        return "Ganancia";
    case UI_KNOB_NR:         return "Reducción";
    case UI_KNOB_RTTY_SHIFT: return "RTTY";
    case UI_KNOB_CW_TONE:    return "Tono CW";
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
    }

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
static void chip(gfx2_surf_t *s, int16_t x, int16_t y,
                 const char *label, const char *value, int active)
{
    int16_t lw = gfx2_text_w(label, active ? &font_ui_14b : &font_ui_14);
    int16_t vw = value ? gfx2_text_w(value, &font_ui_14b) : 0;
    int16_t w  = (int16_t)(lw + (value ? vw + 6 : 0) + 18);

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
                     + (value ? gfx2_text_w(value, &font_ui_14b) + 6 : 0) + 18);
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
#define ST_BAR_W   150
#define ST_READ_X  168
#define ST_READ_W  150
#define ST_KNOB_X  326
#define ST_KNOB_W  204
#define ST_CHIP_R  790

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
        if (st->s_units > 9u) {
            int i2 = 0;
            buf[i2++] = 'S'; buf[i2++] = '9'; buf[i2++] = '+';
            u2s(&buf[i2], st->over_db);
        } else {
            buf[0] = 'S';
            u2s(&buf[1], st->s_units);
        }
        /* Se mide el ancho real en vez de saltar una cantidad fija: "S6" y
         * "S9+14" no ocupan lo mismo, y con el salto fijo el dBm se pegaba a
         * las unidades S en cuanto la lectura pasaba de S9. */
        x = (int16_t)(x + gfx2_text(s, x, (int16_t)(Y + 8), buf, &font_ui_18b,
                                    gfx2_rgb(PAL_INK)) + 12);
        if (st->dbm_valid) {
            int16_t w = gfx2_text(s, x, (int16_t)(Y + 12), i2s(buf, st->dbm),
                                  &font_ui_14b, gfx2_rgb(PAL_INK_DIM));
            gfx2_text(s, (int16_t)(x + w + 3), (int16_t)(Y + 12), "dBm",
                      &font_ui_14, gfx2_rgb(PAL_INK_MUTE));
        } else {
            /* Con el AGC activo la lectura en dBm no significa nada: se dice,
             * en vez de mostrar un numero que parece bueno y no lo es.
             * El texto dice lo que pasa, sin abreviaturas: "n/d" no le dice
             * nada a nadie, y puesto justo donde iria el numero, "AGC activo"
             * ya explica por que no hay numero.
             *
             * Ojo con el texto: la raya larga (U+2014) NO esta en el juego de
             * caracteres de las fuentes generadas, y saldria como un hueco. */
            gfx2_text(s, x, (int16_t)(Y + 12), "AGC activo", &font_ui_14,
                      gfx2_rgb(PAL_INK_MUTE));
        }
    }

    /* --- destino del mando, siempre en el mismo sitio ---
     * Es la respuesta a "que hace el mando ahora mismo", que hoy depende de lo
     * ultimo que se abrio en un menu y no esta escrita en ninguna parte. */
    {
        const char *nm = knob_name(st->knob);
        /* 326 y no 348: con el nombre de mando mas largo ("Silenciador",
         * "Despl. RTTY") y la sobrecarga activa, la pastilla quedaba a 3 px
         * del chip rojo. */
        int16_t kx = ST_KNOB_X;
        int16_t kw = (int16_t)(gfx2_text_w(nm, &font_ui_14b)
                               + (st->knob_value ? gfx2_text_w(st->knob_value, &font_ui_14b) + 8 : 0)
                               + 44);
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
        const char *lbl[5];
        const char *val[5];
        uint8_t     act[5];
        uint8_t     id[5];
        uint8_t     n = 0U, i;
        int16_t     limite = (int16_t)(s_knob_right + 10);

        s_hits_n = 0U;

        if (st->ovr) { lbl[n] = "SOBRECARGA"; val[n] = 0; act[n] = 2U;
                       id[n] = UI_TOP_HIT_OVR; n++; }
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
        if (st->spk_muted) { lbl[n] = "MUDO"; val[n] = 0; act[n] = 1U;
                             id[n] = UI_TOP_HIT_SPK; n++; }

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
            if (act[i] == 2U) {
                /* Sobrecarga: el unico uso del rojo, y siempre con la palabra
                 * entera, nunca solo color. */
                gfx2_rrect(s, x, (int16_t)(Y + 8), w, 24, 6, gfx2_rgb(PAL_CRIT));
                gfx2_text_in(s, x, (int16_t)(Y + 12), w, lbl[i], &font_ui_14b,
                             gfx2_rgb(0xFFFFFF), GFX2_ALIGN_C);
            } else {
                chip(s, x, (int16_t)(Y + 8), lbl[i], val[i], act[i]);
            }
            x = (int16_t)(x - 6);
        }
    }
}

/* =========================================================================== */
void ui_top_draw(const ui_top_state_t *st)
{
    gfx2_render(0, 0, GFX2_W, UI_TOP_H, draw_header, (void *)st);
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
}

void ui_top_draw_status(const ui_top_state_t *st)
{
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
}

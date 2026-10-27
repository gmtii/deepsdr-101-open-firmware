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
 * Donde acaba la lectura de frecuencia - 22/09/2026, avisado por el usuario:
 * con los ajustes abiertos, un toque arriba entre el modo y la hora abria la
 * pantalla de meter la frecuencia a mano.
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

uint8_t ui_top_mem_hit(uint16_t x, uint16_t y)
{
    /* Sin margen de mas: por la derecha, a cuatro pixeles, empieza lo que
     * el reloj considera suyo (ui_top_clock_hit), y por abajo el renglon
     * del PPM. Lo que se dibuja y lo que se toca son el MISMO rectangulo,
     * que es la unica forma de que no se separen. */
    return (uint8_t)((int16_t)x >= UI_TOP_MEM_X
                     && (int16_t)x <  (int16_t)(UI_TOP_MEM_X + UI_TOP_MEM_W)
                     && (int16_t)y >= UI_TOP_MEM_Y
                     && (int16_t)y <  (int16_t)(UI_TOP_MEM_Y + UI_TOP_MEM_H));
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
    case UI_KNOB_FILTRO_LO:  return tr("Filtro mín.", "Filter min.");
    case UI_KNOB_FILTRO_HI:  return tr("Filtro máx.", "Filter max.");
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
            /*
             * El borde dice EN QUE CLASE de banda estas, con el mismo codigo
             * de color que la raya de entre el espectro y la cascada: verde
             * aficionados, azul radiodifusion, ambar utilidades, rojo VHF y
             * UHF, gris fuera. El porque de esos cinco y no otros -medido en
             * las ocho paletas- esta en spec_chrome.c, en draw_ruler().
             */
            {
                uint32_t cb = PAL_LINE;

                if (st->en_banda) {
                    switch (st->fam_banda) {
                    case 0U:  cb = PAL_OK;       break;
                    case 1U:  cb = PAL_PASSBAND; break;
                    case 2U:  cb = PAL_ACCENT;   break;
                    case 3U:  cb = PAL_CRIT;     break;
                    default:  cb = PAL_OK;       break;
                    }
                }
                gfx2_rrect_outline(s, bx, 14, bw, 32, 6, 1, gfx2_rgb(cb));
            }
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

    /*
     * --- error de PLL en SAM ---
     *
     * En el renglon de ARRIBA y a la derecha de la marquesina, no dentro de
     * ella: ver UI_TOP_SAM_X en ui_top.h, que lleva el por que y los numeros
     * medidos.
     *
     * En los demas modos NO se pinta nada: la version anterior rellenaba ese
     * rectangulo con el gris de la barra para "limpiarlo", lo que con un
     * fondo plano era invisible y con el degradado de aqui dejaba un
     * cuadrado gris a la vista. Aqui el fondo lo pone el repintado de la
     * banda, asi que no hay nada que limpiar.
     */
    if (st->sam_ppm) {
        gfx2_text_in(s, UI_TOP_SAM_X, UI_TOP_SAM_Y, UI_TOP_SAM_W,
                     st->sam_ppm, &font_ui_14, dim, GFX2_ALIGN_R);
    }

    /*
     * --- el boton del banco de memorias ---
     *
     * En el MISMO renglon que las pastillas de modo y de banda de aqui
     * arriba, que es donde el usuario lo pidio. Donde cabe y por que el
     * PPM del SAM tuvo que bajar un renglon para dejarle sitio esta en
     * UI_TOP_MEM_X (ui_top.h), con las cajas de tinta medidas.
     *
     * Se pinta como una chapa, igual que las de la franja de estado, para
     * que se lea como lo que es: algo que se toca. Y se dibuja SIEMPRE, en
     * todos los modos: un boton que aparece y desaparece es un boton que
     * se busca.
     */
    {
        /* Con el color de acento y no el del fondo: en esta cabecera lo
         * que se toca va en ambar -la pastilla del mando de abajo, la
         * chapa del modo- y lo que solo se lee va en gris. Un boton del
         * color de lo que no se toca es un boton que nadie toca. */
        /* Los mismos numeros que la pastilla del modo de aqui arriba -alto
         * 32, radio 6, relleno al 42 y borde al 170 del acento-: es un
         * boton de esa misma fila y tiene que leerse igual. */
        /* Con el barrido andando pone el NUMERO de la memoria que esta
         * escuchando, y va RELLENO en vez de solo tenido: es la misma
         * diferencia que separa un chip encendido de uno apagado en la
         * franja de abajo, asi que "esto esta pasando ahora" se lee igual
         * en los dos sitios. */
        const char *barre = st->mem_scan;

        gfx2_rrect(s, UI_TOP_MEM_X, UI_TOP_MEM_Y, UI_TOP_MEM_W, UI_TOP_MEM_H,
                   6, gfx2_rgba(PAL_ACCENT, barre ? 220U : 42U));
        gfx2_rrect_outline(s, UI_TOP_MEM_X, UI_TOP_MEM_Y, UI_TOP_MEM_W,
                           UI_TOP_MEM_H, 6, 1, gfx2_rgba(PAL_ACCENT, 170));
        /* La MISMA fuente y el mismo desplazamiento que las pastillas de
         * modo y de banda de aqui arriba -font_ui_18b y seis pixeles por
         * debajo del borde-, que es lo que hace que los tres se lean como
         * una fila y no como tres cosas sueltas. Estaba en font_ui_14b y
         * se notaba: 46 px contra 35 para el mismo "MEM". */
        gfx2_text_in(s, UI_TOP_MEM_X, (int16_t)(UI_TOP_MEM_Y + 6),
                     UI_TOP_MEM_W, barre ? barre : "MEM", &font_ui_18b,
                     barre ? gfx2_rgb(PAL_SURF_0) : gfx2_rgb(PAL_ACCENT),
                     GFX2_ALIGN_C);
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

    /*
     * --- bateria debajo del reloj, CON EL TANTO POR CIENTO DENTRO ---
     *
     * Tres cosas cambian y las tres se siguen unas a otras:
     *
     *   1) LOS VOLTIOS SE VAN. Eran el unico texto de esta esquina y lo que
     *      decian ya lo dice el dibujo. Siguen estando donde hacen falta de
     *      verdad -Ajustes, Informacion, fila "Bateria", con los milivoltios
     *      enteros- que es donde se mira cuando se sospecha de la medida.
     *
     *   2) EL NUMERO VA DENTRO, y para eso la caja tiene que crecer: la de
     *      antes median 40x15, y por dentro del contorno son 11 px de alto
     *      para una letra de catorce. Ahora son 48x20, que dejan tres
     *      pixeles de aire por arriba y por abajo (lo mide sim/toptest.c
     *      leyendo los pixeles pintados, no repitiendo esta resta).
     *
     *   3) EL RELLENO PASA A SER UN TINTE. Opaco, el numero quedaria la
     *      mitad sobre verde y la mitad sobre el fondo, y no hay un solo
     *      color de letra que se lea bien sobre los dos. Con alfa, lo de
     *      debajo es siempre el fondo de la cabecera tenido, asi que la
     *      tinta normal -la misma del reloj- contrasta con las dos mitades.
     *      El banco lo comprueba en las ocho paletas.
     */
    {
        int16_t bw = 48, bh = 20;
        int16_t bx = (int16_t)(790 - bw - 2);
        int16_t by = 34;
        uint8_t pct = (st->batt_pct > 100u) ? 100u : st->batt_pct;
        int16_t fill = (int16_t)((uint32_t)(bw - 4) * pct / 100u);
        uint32_t col = (pct > 25u) ? PAL_OK : PAL_CRIT;
        char t[6];
        uint8_t i = 0U;

        if (pct >= 100u) { t[i++] = '1'; t[i++] = '0'; t[i++] = '0'; }
        else {
            if (pct >= 10u) { t[i++] = (char)('0' + (pct / 10u)); }
            t[i++] = (char)('0' + (pct % 10u));
        }
        t[i++] = '%';
        t[i] = '\0';

        gfx2_rrect_outline(s, bx, by, bw, bh, 4, 2, gfx2_rgb(PAL_INK_MUTE));
        /* El pezon, a la altura del nuevo alto. */
        gfx2_rrect(s, (int16_t)(bx + bw), (int16_t)(by + 6), 3, 8, 1,
                   gfx2_rgb(PAL_INK_MUTE));
        if (fill > 0) {
            gfx2_fill(s, (int16_t)(bx + 2), (int16_t)(by + 2), fill,
                      (int16_t)(bh - 4), gfx2_rgba(col, UI_TOP_BAT_ALFA));
        }
        /*
         * EL "+1" ESTA MEDIDO, no estimado. gfx2_text() coloca cada glifo
         * en y + by del glifo, asi que el alto que ocupa una CIFRA no es
         * el ascent de la fuente: con font_ui_14 los digitos salen diez
         * pixeles mas abajo del origen del texto. Centrar con
         * (bh - ascent) / 2 dejaba siete pixeles de aire arriba y tres
         * abajo - el numero pegado al suelo de la caja.
         *
         * sim/toptest.c mide el aire de arriba y el de abajo leyendo los
         * pixeles y exige que no se diferencien en mas de uno, asi que el
         * dia que cambie la fuente esto sale en rojo diciendo cuanto se ha
         * descolgado, en vez de salir torcido en la radio.
         */
        gfx2_text_in(s, bx, (int16_t)(by + 1), bw, t,
                     &font_ui_14, ink, GFX2_ALIGN_C);
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

static int16_t chip_w_crudo(const char *label, const char *value, int active)
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
 * Y EL CHIP DEL ATENUADOR MIDE SIEMPRE LO MISMO - 19/10/2026.
 *
 * Pedido por el usuario: que la chapa ATT no cambie de tamaño, para que lo
 * que hay a su izquierda no se mueva de sitio al cambiar de estado.
 *
 * Los chips se colocan de DERECHA A IZQUIERDA desde ST_CHIP_R, asi que el
 * ancho de uno empuja a todos los de su izquierda. Con "-06" en vez de "-6"
 * los dos estados encendidos ya miden igual -24 px el valor en los dos-,
 * pero quedaban 2 px de diferencia con "OFF": el valor apagado es 5 px mas
 * ancho y el rotulo apagado 3 px mas estrecho, porque "encendido" se pinta
 * en negrita. Dos pixeles bastan para que la pastilla del mando y los demas
 * chips den un saltito cada vez que tocas el atenuador.
 *
 * Asi que ATT reserva SIEMPRE el ancho de su estado mas ancho, y se mide en
 * vez de escribirlo: una constante puesta a mano aqui deja de ser verdad el
 * dia que cambie la fuente, el rotulo o una de las tres siglas.
 *
 * Va en chip_w() y no en el bucle que los dibuja porque chip_w() es por
 * donde pasan TODOS: el reparto de derecha a izquierda, el recuadro que
 * pinta chip(), la zona de toque que apunta ui_top_hit() y la cuenta que
 * limita la pastilla del mando. Un solo sitio, imposible de desincronizar.
 */
static int16_t chip_w(const char *label, const char *value, int active)
{
    if (value != 0 && label[0] == 'A' && label[1] == 'T'
                   && label[2] == 'T' && label[3] == '\0') {
        static const char *const k_att[] = UI_TOP_ATT_SIGLAS;
        int16_t mayor = 0;
        uint8_t i;
        for (i = 0U; i < (uint8_t)(sizeof k_att / sizeof k_att[0]); i++) {
            /* El mismo criterio de "encendido" que usa el bucle de los
             * chips: "OFF" no cuenta, igual que en el del AGC. */
            int16_t w = chip_w_crudo(label, k_att[i], k_att[i][0] != 'O');
            if (w > mayor) { mayor = w; }
        }
        return mayor;
    }
    return chip_w_crudo(label, value, active);
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
 *   10..130   barra del S-meter        (120)  <- no se toca
 *  138..200   unidades S, "S9+60"              -> 62 px
 *  208..385   pastilla del mando, ANCHO FIJO   -> 177 px, no se acorta
 *       ..794   chips, anclados a la derecha: no se mueven nunca
 *
 *  Nada de esto depende de la señal ni del perfil del AGC: la franja no
 *  baila. El dBm ya no sale aqui (ver el bloque de la lectura).
 *
 * El peor caso de la pastilla NO se estima: sim/toptest.c lo recorre con los
 * nueve nombres de mando y el valor mas largo, y avisa si se sale. La primera
 * version daba 194 px contra 180 disponibles.
 */
#define ST_BAR_X    10
/*
 * 120 y no 150, y la lectura 30 px a la izquierda - 23/09/2026, pedido por el
 * usuario: los indicadores de NR, NCO y AGC iban muy juntos y habia que
 * separarlos, aunque fuera a costa de encoger un poco el S-metro.
 *
 * Y tiene razon en las dos cosas. Para meter el chip del NCO se habian
 * apretado los margenes de los chips (de 18 a 12) y el hueco entre ellos (de
 * 6 a 4), que es lo que hace que se lean como un bloque en vez de como
 * cuatro cosas. Los 30 px que suelta la barra vuelven ahi: margen 16, hueco
 * 8. La barra sigue teniendo su escala de unidades S marcada y no pierde
 * resolucion -la posicion se calcula en milesimas sobre su anchura, sea la
 * que sea-, solo mide 30 px menos.
 */
/*
 * LA BARRA ENTERA, Y LA PASTILLA EN EL HUECO QUE DEJO "AGC ACTIVO"
 * - 19/10/2026.
 *
 * Lo que pasaba. La lectura tiene RESERVADOS 141 px para su peor caso -
 * "S9+60" mas "-127 dBm"- y el dBm solo sale con el AGC en OFF. El resto
 * del tiempo ahi vivia "AGC activo"; hoy ese cartel se ha ido y lo que
 * queda es un agujero de ochenta y pico pixeles en mitad de la franja, que
 * es justo lo que el marco en la foto.
 *
 * Mis dos intentos anteriores fueron por el lado equivocado: el primero
 * encogio la barra de S de 120 a 80 para pagar los 41 px del valor del ATT,
 * y el segundo dejo la barra pero acorto el rotulo del mando a "Sin. 1 kHz".
 * Los dos quitaban algo que se usa para no tocar un hueco que no se usa.
 *
 * COMO QUEDA:
 *
 *   - la barra de S vuelve a sus 120 px y no se vuelve a tocar;
 *   - la lectura reserva lo que de verdad va a enseñar: "S9+60" a secas
 *     cuando no hay dBm, que es lo normal, y los 141 px enteros cuando lo
 *     hay. Dos sitios, y cual toca depende SOLO del perfil del AGC, no de
 *     la señal: "S6" y "S9+14" no mueven nada, que es lo que hace falta
 *     para que la franja no baile con el S-metro;
 *   - la pastilla del mando tiene ANCHO FIJO. Ni crece ni encoge ni se
 *     acorta el rotulo: mide siempre lo que mide su peor caso de verdad,
 *     "Sintonia 12,5 kHz" (177 px, el paso mas ancho de los diez). Con el
 *     BW enseñando los dos cortes le quedan 179, asi que entra.
 *
 * Y el ancho se MIDE con la fuente, no se escribe: una constante a mano
 * deja de ser verdad el dia que cambie la fuente o un rotulo.
 */
#define ST_BAR_W   120
#define ST_READ_X  138
#define ST_READ_W  150
#define ST_KNOB_W  204
#define ST_CHIP_R  794

/* Donde empieza la pastilla: detras de las unidades S, que es lo unico que
 * queda en ese hueco. Ocho pixeles de aire, los mismos que separan la barra
 * de la lectura. Es UN SITIO, no dos: no depende de la señal ni del perfil
 * del AGC, asi que no se mueve nunca. */
static int16_t knob_x(const ui_top_state_t *st)
{
    (void)st;
    return (int16_t)(ST_READ_X + gfx2_text_w("S9+60", &font_ui_18b) + 8);
}

/*
 * EL ANCHO FIJO DE LA PASTILLA, pedido por el usuario.
 *
 * El peor caso REAL del mando por omision: "Sintonia" con el paso de
 * sintonia mas ancho de los diez que hay. Cualquier ajuste enganchado que
 * pida mas sigue acortandose como siempre -esa es su forma de ceder-, pero
 * el rotulo que se ve el 99% del tiempo ya no se toca nunca, y la pastilla
 * mide lo mismo con el mando en lo que sea.
 */
static int16_t knob_w_fijo(void)
{
    return (int16_t)(gfx2_text_w(tr("Sintonía", "Tuning"), &font_ui_14b)
                     + gfx2_text_w("12,5 kHz", &font_ui_14b) + 8 + 40);
}
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

        /*
         * SOLO LAS UNIDADES S. El dBm ya no sale aqui - 19/10/2026.
         *
         * Este hueco ha ido perdiendo inquilinos hoy, y los tres por el
         * mismo motivo: ocupaban sitio fijo para algo que casi nunca se
         * mira. Primero se fue "SOBRECARGA" -tapaba la lectura entera justo
         * cuando querias leerla- y se mudo a la pastilla del mando. Despues
         * "AGC activo", que estaba puesto casi siempre y por eso habia
         * dejado de leerse. Y ahora el dBm, que solo valia con el AGC en
         * OFF y obligaba a reservarle 79 px los otros cinco perfiles.
         *
         * Lo que se gana es justo lo que el marco con una raya roja en la
         * foto: la pastilla del mando se mete ahi, con ancho fijo y sin
         * recortar su rotulo, y la barra de S se queda con sus 120 px.
         *
         * El numero no se ha perdido: la lectura calibrada y los dos
         * rechazos de imagen siguen en Ajustes -> Equipo -> Informacion,
         * que es donde se va a mirar un dato que se consulta y no se
         * vigila.
         */
        x = ST_READ_X;
        if (st->s_units > 9u) {
            int i2 = 0;
            buf[i2++] = 'S'; buf[i2++] = '9'; buf[i2++] = '+';
            u2s(&buf[i2], st->over_db);
        } else {
            buf[0] = 'S';
            u2s(&buf[1], st->s_units);
        }
        (void)gfx2_text(s, x, (int16_t)(Y + 8), buf, &font_ui_18b,
                        gfx2_rgb(PAL_INK));
    }

    /* --- destino del mando, siempre en el mismo sitio ---
     * Es la respuesta a "que hace el mando ahora mismo", que hoy depende de lo
     * ultimo que se abrio en un menu y no esta escrita en ninguna parte. */
    /*
     * LO QUE NO ENTRA EN LA VENTANA NO SE CALCULA - 20/10/2026.
     *
     * Debajo vienen la pastilla del mando y los cinco chips de la derecha,
     * y cada uno se mide antes de pintarse: chip_w() con su texto,
     * gfx2_text_w() del nombre y del valor, el ancho que queda hasta
     * ST_CHIP_R... Todo eso se hacia TAMBIEN cuando la ventana acababa
     * mucho antes de llegar a ellos, porque el recorte vive dentro de cada
     * primitiva: no se pintaba nada, pero se calculaba entero. Y una vez
     * por banda.
     *
     * Medido: el repintado del S-metro pasa de 241 us a 24 en el
     * simulador, que en la radio son 31 ms a 3. Y corre cada vez que la
     * barra cambia de segmento, o sea constantemente con señal.
     *
     * NO SE PIERDE NADA AL SALTARSELO. Las cajas del tactil (s_hits_*) y
     * s_knob_right siguen valiendo lo que valian, y tienen que valerlo: lo
     * que no se repinta tampoco se ha movido. Quien de verdad cambia un
     * chip o la pastilla repinta la franja entera, y entonces esta guardia
     * no se cumple y se recalcula todo.
     *
     * El limite es knob_x(), que es de donde sale la posicion de la
     * pastilla: el mismo numero decide donde empieza y donde corta, asi
     * que no pueden separarse.
     */
    if ((int16_t)(s->x + s->w) <= knob_x(st)) { return; }
    {
        const char *nm = (st->knob_name ? st->knob_name : knob_name(st->knob));
        const char *vl = st->knob_value;
        char nmbuf[24];
        char vlbuf[24];
        int16_t kx = knob_x(st);
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
         * ESTADO (AGC, BW, ATT, NCO) son fijos y siempre estan, y lo que se
         * acorta es el NOMBRE del ajuste enganchado, que es lo que varia de
         * verdad -de "AGC" a "Desplazamiento"- y ademas es temporal. El
         * valor no se toca: es lo que estas mirando mientras giras.
         */
        {
            int16_t fijos = (int16_t)(chip_w("AGC", st->agc,
                                             (st->agc && st->agc[0] != 'O')) + ST_CHIP_GAP
                                      /* ATT y no NR desde el 19/10/2026, y con
                                       * valor: ver ui_top_state_t::att. "OFF" no
                                       * cuenta como encendido, igual que en AGC. */
                                      + chip_w("ATT", st->att,
                                               (st->att && st->att[0] != 'O')) + ST_CHIP_GAP
                                      + chip_w("NCO", st->nco_on ? st->nco_off : 0,
                                               st->nco_on) + ST_CHIP_GAP
                                      + chip_w("JMP", 0, st->jmp_on) + ST_CHIP_GAP);
            int16_t kw_max;
            if (st->bw) { fijos = (int16_t)(fijos + chip_w("BW", st->bw, 1) + ST_CHIP_GAP); }
            kw_max = (int16_t)(ST_CHIP_R - fijos - 10 - kx);

            /*
             * EL ANCHO ES FIJO. Pedido por el usuario: "hazle un ancho fijo
             * a la chapa de sintonia".
             *
             * La pastilla mide siempre lo mismo, con el mando en lo que sea
             * y con sobrecarga o sin ella. Lo que cede, si el contenido no
             * cabe dentro, es el CONTENIDO -primero el nombre, despues el
             * valor-, no el recuadro. Antes cedia el recuadro y por eso los
             * chips de la derecha parecian moverse al cambiar de mando.
             */
            kw = knob_w_fijo();
            if (kw > kw_max) { kw = kw_max; }   /* tope duro: nunca empuja a los chips */
            if (kw < 44)     { kw = 44; }

            if (!st->ovr) {
                int16_t pide = (int16_t)(gfx2_text_w(nm, &font_ui_14b)
                                         + (vl ? gfx2_text_w(vl, &font_ui_14b) + 8 : 0)
                                         + 40);
                if (pide > kw) {
                    /*
                     * Acortar el nombre hasta que quepa, con un punto al
                     * final. Se corta por BYTES pero respetando UTF-8: los
                     * nombres llevan acentos y partir un caracter por la
                     * mitad pinta un hueco raro. Los bytes 0x80..0xBF son
                     * continuacion, nunca principio.
                     */
                    size_t n = strlen(nm);
                    if (n > sizeof nmbuf - 2U) { n = sizeof nmbuf - 2U; }
                    memcpy(nmbuf, nm, n);
                    nmbuf[n] = '\0';
                    while (n > 1U) {
                        n--;
                        while (n > 1U && (nmbuf[n] & 0xC0) == 0x80) { n--; }
                        nmbuf[n] = '.';
                        nmbuf[n + 1U] = '\0';
                        pide = (int16_t)(gfx2_text_w(nmbuf, &font_ui_14b)
                                         + (vl ? gfx2_text_w(vl, &font_ui_14b) + 8 : 0)
                                         + 40);
                        if (pide <= kw) { break; }
                    }
                    nm = nmbuf;

                    /* Y si con el nombre en una letra tampoco cabe, cede el
                     * valor. Sigue viendose, acortado, mientras que un chip
                     * que desaparece no deja rastro de que existia. */
                    if (pide > kw && vl != 0) {
                        size_t m = strlen(vl);
                        if (m > sizeof vlbuf - 2U) { m = sizeof vlbuf - 2U; }
                        memcpy(vlbuf, vl, m);
                        vlbuf[m] = '\0';
                        while (m > 1U) {
                            m--;
                            while (m > 1U && (vlbuf[m] & 0xC0) == 0x80) { m--; }
                            vlbuf[m] = '.';
                            vlbuf[m + 1U] = '\0';
                            pide = (int16_t)(gfx2_text_w(nm, &font_ui_14b)
                                             + gfx2_text_w(vlbuf, &font_ui_14b) + 8
                                             + 40);
                            if (pide <= kw) { break; }
                        }
                        vl = vlbuf;
                    }
                }
            }
        }
        s_knob_right = (int16_t)(kx + kw);
        if (st->ovr) {
            /* En rojo y con la palabra entera, nunca solo el color: es la
             * misma regla que seguia el aviso cuando vivia en la lectura. */
            gfx2_rrect(s, kx, (int16_t)(Y + 8), kw, 24, 12, gfx2_rgba(PAL_CRIT, 48));
            gfx2_rrect_outline(s, kx, (int16_t)(Y + 8), kw, 24, 12, 1,
                               gfx2_rgba(PAL_CRIT, 200));
            /*
             * Y CENTRADO EN LA PASTILLA - 24/10/2026, avisado por el
             * usuario con una foto.
             *
             * Iba pegado a la izquierda, a los mismos 12 pixeles que el
             * texto del mando. Pero el ancho de la pastilla -kw- se calcula
             * para el contenido del MANDO ("Sintonia 500 Hz" y compania),
             * que es mas largo que esta palabra: el aviso se quedaba a la
             * izquierda con un hueco a la derecha, y encima el hueco cambia
             * de tamano segun lo que ponga el mando en ese momento.
             *
             * Se centra con lo que mide el texto de verdad, no con una
             * constante: "SOBRECARGA" y "OVERLOAD" no miden lo mismo. Y con
             * tope por la izquierda, porque si algun dia la pastilla sale
             * mas estrecha que la palabra, lo que NO puede hacer es empezar
             * fuera de ella.
             */
            {
                const char *av = tr("SOBRECARGA", "OVERLOAD");
                int16_t aw = (int16_t)gfx2_text_w(av, &font_ui_14b);
                int16_t ax = (int16_t)(kx + ((kw - aw) / 2));

                if (ax < (int16_t)(kx + 4)) { ax = (int16_t)(kx + 4); }
                gfx2_text(s, ax, (int16_t)(Y + 12), av, &font_ui_14b,
                          gfx2_rgb(PAL_CRIT));
            }
        } else {
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
                if (vl) {
                    gfx2_text(s, (int16_t)(tx + 8), (int16_t)(Y + 12),
                              vl, &font_ui_14b, gfx2_rgb(PAL_INK));
                }
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
        /* SIETE desde el 15/10/2026 (el chip JMP). Eran seis, y seis era
         * justo lo que cabia: pasarse es escribir fuera del array sin que
         * ningun aviso lo diga. La guarda de debajo del bucle ata el tamaño
         * al numero de chips que se meten de verdad. */
        const char *lbl[7];
        const char *val[7];
        uint8_t     act[7];
        uint8_t     id[7];
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
        /*
         * ATT, EN EL SITIO QUE OCUPABA NR - 19/10/2026, pedido por el
         * usuario. Ver ui_top_state_t::att para el porque del cambio.
         *
         * Lleva valor, como BW: "OFF", "-6" o "-12". El numero sale en el
         * color de acento cuando hay atenuacion puesta y apagado cuando
         * no, que es lo que hace chip() con cualquier valor.
         */
        lbl[n] = "ATT"; val[n] = st->att;
        act[n] = (uint8_t)(st->att && st->att[0] != 'O');
        id[n] = UI_TOP_HIT_ATT; n++;
        /* Justo al lado del ATT, que es donde lo pidio el usuario. Igual que
         * el: siempre esta, encendido o apagado, para que el sitio no baile
         * segun el estado. */
        lbl[n] = "NCO"; val[n] = st->nco_on ? st->nco_off : 0;
        act[n] = st->nco_on;
        id[n] = UI_TOP_HIT_NCO; n++;
        /*
         * JMP VA DESPUES DEL NCO -o sea, mas a la izquierda- porque es el
         * ultimo que ha llegado y porque ahi lo pidio el usuario. Es fijo
         * como los otros cuatro: un modo que
         * cambia lo que hace el mando no puede desaparecer de la pantalla
         * cuando el nombre del ajuste enganchado crece.
         */
        lbl[n] = "JMP"; val[n] = 0; act[n] = st->jmp_on;
        id[n] = UI_TOP_HIT_JMP; n++;
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
     * AQUI YA NO SE PINTA EL PPM DE SAM - 05/10/2026.
     *
     * Desde el 28/09 se repintaba tambien aqui dentro, y la razon era buena:
     * el numero caia dentro de esta franja -una zona FIJA- y la franja se lo
     * comia al refrescar la marquesina. Es la QUINTA vez que aparece esa
     * familia de fallo en el proyecto; las cuatro anteriores fueron en el
     * panel digital -el tercer boton con la barra, el cuarto con la chapa, y
     * el del mapa con solo_chip(), esa tres veces seguidas-.
     *
     * Pero pintarlo DOS veces arreglaba que se borrara y dejaba el otro
     * problema entero en pie: el numero quedaba ENCIMA del horario de la
     * emisora, que vive en este mismo renglon.
     *
     * Lo que estaba mal era compartir sitio. Ahora el numero vive FUERA de
     * esta franja, en el renglon de arriba y a su derecha (UI_TOP_SAM_X en
     * ui_top.h), y con eso se caen las dos cosas de golpe: ni lo borra nadie
     * ni pisa a nadie, y este parche sobra. sim/top_franjas.c vigila las dos
     * -que el repintado parcial sea idempotente, y que las dos huellas no
     * compartan ni un pixel-.
     */
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
/*
 * LAS TRES FRANJAS, EN UNA SUPERFICIE QUE TRAE OTRO - 05/10/2026.
 *
 * Y esa es la pieza que faltaba. Esta radio NO tiene copia de la pantalla en
 * memoria: una pantalla son 768 kB y aqui quedan dos y medio. Lo que hay es
 * este compositor, que monta franjas de 16 filas, las empuja al panel y las
 * olvida. Asi que una captura no es volcar lo que hay, es VOLVER A DIBUJARLO
 * franja por franja metiendolo en el fichero.
 *
 * Para eso hace falta poder decirle a cada capa "pintate en ESTA banda" en
 * vez de "pintate y vuelcate". Esto es eso para la cabecera, y no duplica ni
 * una linea: llama a las mismas tres funciones de dibujo a las que llama
 * ui_top_draw(), que es justo lo que hace que la captura se parezca a la
 * pantalla.
 *
 * El orden es el mismo que el de ui_top_draw(): cabecera, marquesina encima
 * -recortada a su hueco a mano, ver abajo- y franja de estado.
 */
void ui_top_pinta_en(gfx2_surf_t *s, const ui_top_state_t *st)
{
    draw_header(s, (void *)st);
    /*
     * La marquesina, SOLO EN SU FRANJA. Pinta su degradado de y=0 a y=63 y
     * se queda en y=10..53 porque ui_top_draw_rds() le pide a gfx2 esa
     * ventana y nada mas. Aqui no hay ventana que recorte, asi que hay que
     * dársela: sin esto taparia la raya de abajo de la cabecera. Medido: 172
     * pixeles distintos entre la captura y la pantalla, todos en y=63.
     */
    {
        gfx2_surf_t sub = gfx2_sub_y(s, UI_TOP_RDS_Y, UI_TOP_RDS_H);
        if (sub.h > 0) {
            /*
             * Y SOLO EN SU HUECO TAMBIEN A LO ANCHO - 07/10/2026.
             *
             * Era eso exactamente. Aqui arriba ponia que la marquesina "se
             * recorta a su hueco por construccion", y era mentira a medias:
             * se recortaba en Y -gfx2_sub_y- y en X no se recortaba nada. En
             * la pantalla de verdad el recorte en X lo da la ventana que pide
             * ui_top_draw_rds() a gfx2_render(); aqui no hay ventana.
             *
             * Con un nombre corto -"RADIO EUROPA"- no se nota, y por eso
             * sim/captura.c llevaba desde el 05/10 diciendo que todo iba
             * bien: el texto que usaba cabia. Con "XSQ Guangzhou Meteo Fax"
             * el nombre se salia por la derecha y pisaba el reloj y los
             * voltios de la bateria. Ahora el banco usa el nombre largo.
             */
            gfx2_recorte_x(rds_x1(), UI_TOP_RDS_X2);
            rds_franja(&sub, (void *)st);
            gfx2_recorte_quita();
        }
    }
    draw_status(s, (void *)st);
}

static void nco_apunta(const ui_top_state_t *st);

void ui_top_draw(const ui_top_state_t *st)
{
    gfx2_render(0, 0, GFX2_W, UI_TOP_H, draw_header, (void *)st);
    /* Despues de la cabecera y no dentro: necesita su propia banda para
     * recortarse al hueco. Ver draw_header(). */
    ui_top_draw_rds(st);
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
    /* Esto tambien es la franja entera: apunta la chapa del NCO igual que
     * ui_top_draw_status(). Ver s_nco_pintado. */
    nco_apunta(st);
}

/*
 * ======================================================================
 * LO QUE LA CHAPA DEL NCO TIENE PINTADO AHORA MISMO - 25/10/2026.
 * ======================================================================
 *
 * Reportado por un usuario: falta que el valor del NCO salga solo nada mas
 * sintonizar.
 *
 * Y faltaba, por un motivo que no se ve leyendo main.c: el numero se
 * recalcula en cada cuadro, pero quien lo pinta es esta franja, y el clic
 * del mando no la repinta -freq_display_draw() se quedo a proposito en el
 * hueco de la lectura el 05/10/2026, para no gastar los 32.000 accesos de
 * la franja en cada clic-. Con el oscilador aparcado la sintonia se mueve y
 * el oscilador NO: el desplazamiento cambia en cada clic y es justo
 * entonces cuando hay algo nuevo que decir. La chapa se quedaba con el
 * numero del arranque.
 *
 * EL APUNTE VIVE AQUI Y NO EN main.c, y ese es el detalle que importa. La
 * primera version guardaba el texto alli y lo actualizaba en los cinco
 * sitios que repintan la franja... de los TRECE que hay. Los cinco de
 * "canal siguiente" -FT8, JTTY, AIS, WSPR, HFDL- mueven la sintonia y
 * repintan con ui_top_draw() sin pasar por ninguno de los cinco, asi que la
 * memoria se quedaba desfasada y la chapa podia volver a mentir.
 *
 * Puesto donde se pinta, no hay lista que mantener: cualquier camino que
 * repinte la franja lo apunta por el mero hecho de repintarla. Es la misma
 * leccion que este fichero lleva contada cinco veces.
 *
 * Y NO se apunta en ui_top_draw_smeter(), que dibuja lo mismo con la
 * ventana recortada a la parte de la izquierda: los chips caen fuera y
 * gfx2 los tira, asi que apuntar alli seria decir que esta pintado algo que
 * no se ha pintado.
 */
static char s_nco_pintado[12] = { '\1', '\0' };   /* imposible: fuerza el primero */

static void nco_apunta(const ui_top_state_t *st)
{
    const char *t = (st->nco_on && st->nco_off) ? st->nco_off : "";
    uint8_t     k = 0U;

    while (t[k] != '\0' && k < (uint8_t)(sizeof s_nco_pintado - 1U)) {
        s_nco_pintado[k] = t[k];
        k++;
    }
    s_nco_pintado[k] = '\0';
}

uint8_t ui_top_nco_al_dia(const ui_top_state_t *st)
{
    const char *t = (st->nco_on && st->nco_off) ? st->nco_off : "";
    uint8_t     k = 0U;

    while (t[k] != '\0' && s_nco_pintado[k] != '\0' && t[k] == s_nco_pintado[k]) {
        k++;
    }
    return (uint8_t)(t[k] == '\0' && s_nco_pintado[k] == '\0');
}

void ui_top_draw_status(const ui_top_state_t *st)
{
    gfx2_render(0, UI_STATUS_Y, GFX2_W, UI_STATUS_H, draw_status, (void *)st);
    nco_apunta(st);
}

/*
 * SOLO LA PARTE QUE SE MUEVE CON LA SEÑAL - 30/09/2026.
 *
 * Y era cierto., aunque el espectro no tenga nada que ver: medido con
 * lcd_sim, pintar el espectro cuesta 155.002 accesos al bus HAYA O NO HAYA
 * señal - es exactamente el mismo trabajo-. Lo que cambia con la señal es
 * otra cosa: la barra de S se mueve, y smeter_draw() repinta LA FRANJA
 * ENTERA, 800 x 40 = 32.051 accesos mas por fotograma. Sin señal la barra no
 * se mueve, smeter_draw() sale por la primera linea y esos 32.051 no se
 * gastan. De ahi que con señal el fotograma cueste un 21 % mas.
 *
 * Pero de esa franja, lo unico que cambia con la señal vive a la izquierda
 * de los chips: la barra, las unidades S, el dBm y -desde el 19/10/2026- el
 * aviso de sobrecarga, que vive en la pastilla del mando. Los chips de la
 * derecha no se mueven con la señal.
 *
 * Asi que esto pinta el MISMO dibujo con la ventana recortada a esa parte.
 * No hay una segunda funcion de dibujo que mantener al dia: es draw_status()
 * otra vez, y lo que cae fuera de la ventana lo recorta gfx2. 472 de 800 px,
 * o sea 18.900 accesos en vez de 32.051.
 */
/*
 * El ancho: la lectura MAS la pastilla del mando entera.
 *
 * ANTES ERA SOLO HASTA LA PASTILLA. Dejo de valer el
 * 19/10/2026, cuando el aviso de SOBRECARGA se mudo del hueco de la lectura
 * a la pastilla: el aviso depende de la señal igual que la barra de S, asi
 * que tiene que entrar en "la parte que se mueve con la señal" o se queda
 * con lo de antes hasta que algo repinte la franja por otro motivo.
 *
 * Hoy no se vería -las dos transiciones de s_top.ovr pasan por badges_draw(),
 * que repinta la franja entera- pero eso es un detalle de QUIEN llama, y el
 * fallo quedaba escrito aqui esperando a que alguien cambiara aquello. Es la
 * misma familia de fallo que este fichero lleva contada cinco veces.
 *
 * El coste: la ventana pasa de 268 a 472 px de ancho, o sea de 10.700 a
 * 18.900 accesos al bus por fotograma con señal. Sigue siendo bastante menos
 * que los 32.051 de la franja entera, que es lo que se vino a evitar.
 *
 * ST_KNOB_W es el ancho de la franja reservada a la pastilla; la pastilla
 * puede quedarse mas corta, nunca pasarse (ver el tope duro kw_max). Los
 * seis de mas son aire. Lo vigila sim/top_franjas.c.
 */
/*
 * El ancho: la lectura MAS la pastilla del mando entera, en su sitio mas a
 * la derecha. La pastilla tiene dos sitios -con dBm y sin el- y ademas se
 * pone roja con la sobrecarga, que depende de la señal igual que la barra
 * de S; si la ventana no la alcanzara, el aviso aparecería y desaparecería
 * con retraso. Se coge el caso mas ancho de los dos y se le suma el ancho
 * fijo de la pastilla mas seis de aire.
 */
#define ST_MOVIL_W  (int16_t)(ST_READ_X + 62 + 8 + ST_KNOB_W + 6)

/*
 * Y LA PASTILLA DEL MANDO NO SE MUEVE CON LA SEÑAL - 20/10/2026.
 *
 * Esta ventana iba hasta ST_MOVIL_W, que son 418 px porque incluye la
 * pastilla del mando ("Sintonia 1 kHz"). Pero esa pastilla NO cambia con
 * la señal: cambia cuando se toca el mando, y quien lo toca ya repinta.
 * Lo que se mueve con la señal es la barra (x 10..130) y su etiqueta
 * ("S6", x 138..200), y las dos acaban justo donde empieza la pastilla.
 *
 * Y de alto, la franja mide 40 filas y lo que se mueve vive entre la 6 y
 * la 31: la barra ocupa de la 12 a la 26, sus marquitas llegan a la 29 y
 * la etiqueta empieza en la 8.
 *
 * Medido en la radio con la vuelta partida en tramos: el S-metro se
 * llevaba 31 ms de un fotograma cuyo tope son 22, y corre CADA VEZ QUE
 * CAMBIA UN SEGMENTO, o sea constantemente mientras entra señal.
 *
 * El ancho sale de knob_x(), que es de donde sale tambien la posicion de
 * la pastilla: asi mover la pastilla mueve el borde de esta ventana sola,
 * y no se puede quedar apuntando a donde ya no esta.
 */
#define ST_MOVIL_Y1  6     /* la primera fila que se mueve con la señal */
#define ST_MOVIL_Y2  32    /* la primera que ya no */

void ui_top_draw_smeter(const ui_top_state_t *st)
{
    gfx2_render(0, (int16_t)(UI_STATUS_Y + ST_MOVIL_Y1), knob_x(st),
                (int16_t)(ST_MOVIL_Y2 - ST_MOVIL_Y1), draw_status, (void *)st);
}

/*
 * Y EL DEL PPM DEL SAM, QUE ES EL MISMO CUENTO OTRA VEZ - 04/10/2026.
 *
 * Seguia porque el otro extremo estaba intacto. main.c mira el error del PLL
 * una vez por fotograma y, si el numero cambio, llamaba a ui_top_draw(): la
 * cabecera ENTERA (800x64) mas la franja de estado ENTERA (800x40), 83.200
 * pixeles compuestos y empujados al bus. Y el numero cambia casi siempre,
 * porque son las DECIMAS de un lazo persiguiendo una portadora con
 * desvanecimiento: "+19,4 PPM" no para quieto ni un fotograma.
 *
 * El comentario de main.c daba por bueno ese repintado con el argumento de
 * que era "un numero que se mueve despacio". No se mueve despacio. Esa es
 * toda la historia.
 *
 * Es exactamente el fallo que ya se arreglo el 30/09 con el S-metro -"se
 * sigue ralentizando cuando recibe senal"- y la solucion es la misma: el
 * MISMO dibujo con la ventana recortada a donde vive el numero. 160x28 son
 * 4.480 accesos en vez de 83.200, o sea dieciocho veces menos, y no hay una
 * segunda funcion de dibujo que mantener al dia.
 *
 * La ventana sale de UI_TOP_SAM_R* (ui_top.h), que es donde estan tambien
 * las coordenadas del texto: un solo sitio para las dos cosas, para que
 * mover el numero no pueda dejar la ventana apuntando a donde ya no esta.
 * El 05/10/2026 se movio -ver UI_TOP_SAM_X- y esta ventana se vino sola.
 */
/*
 * SOLO EL BOTON DEL BANCO. 26/10/2026.
 *
 * El rotulo cambia al arrancar y al parar el barrido -dos veces por
 * barrido, no una por salto-, y repintar la cabecera entera por eso serian
 * 89.771 accesos al bus de la pantalla cuando hacen falta los del boton.
 * La ventana es la del boton y ni un pixel mas: ver UI_TOP_MEM_X.
 */
void ui_top_draw_mem(const ui_top_state_t *st)
{
    gfx2_render(UI_TOP_MEM_X, UI_TOP_MEM_Y, UI_TOP_MEM_W, UI_TOP_MEM_H,
                draw_header, (void *)st);
}

void ui_top_draw_sam(const ui_top_state_t *st)
{
    gfx2_render(UI_TOP_SAM_RX, UI_TOP_SAM_RY, UI_TOP_SAM_RW, UI_TOP_SAM_RH,
                draw_header, (void *)st);
}

/*
 * Y LA ESQUINA DEL RELOJ, QUE ERA LA QUE QUEDABA - 20/10/2026.
 *
 * La cuarta de la familia. Ver UI_TOP_HORA_RX en ui_top.h: el mismo
 * draw_header de siempre con la ventana recortada a la esquina de arriba a
 * la derecha, donde viven el reloj y la pastilla de la bateria. No hay una
 * segunda funcion de dibujo que mantener al dia, asi que mover el reloj no
 * puede dejar esto pintando otra cosa.
 */
void ui_top_draw_hora(const ui_top_state_t *st)
{
    gfx2_render(UI_TOP_HORA_RX, UI_TOP_HORA_RY, UI_TOP_HORA_RW,
                UI_TOP_HORA_RH, draw_header, (void *)st);
}

/*
 * Y EL DE LA FRECUENCIA, QUE ES EL QUE MAS VECES CORRE - 05/10/2026.
 *
 * Esta es la tercera de la misma familia, y la mas cara de las tres por el
 * ritmo al que corre. El S-metro (30/09) se repinta con la señal; el PPM del
 * SAM (04/10), con las decimas del PLL. La frecuencia se repinta CADA CLIC
 * DEL MANDO, o sea veinte o treinta veces por segundo mientras alguien
 * sintoniza - que es justo cuando mas falta hace que la radio responda.
 *
 * Y lo que llamaba, ui_top_draw(), es la cabecera entera (800x64) MAS la
 * marquesina MAS la franja de estado entera (800x40): unos 96.000 pixeles
 * compuestos y empujados al bus, de los cuales la franja de estado -32.000-
 * no tiene NADA que ver con la frecuencia.
 *
 * Esta ventana coge desde el borde izquierdo hasta donde empieza la
 * marquesina, que es lo unico que mueve un cambio de frecuencia: las cifras,
 * el subrayado del digito, la pastilla de modo y la de banda. Son unos
 * 31.000 pixeles, tres veces menos, y ni uno de ellos es de nadie mas.
 *
 * El limite es rds_x1() y no un numero escrito: si la pastilla de banda
 * cambia de ancho, el limite se mueve con ella. Un numero fijo aqui seria
 * exactamente como se consigue borrar la marquesina.
 */
/*
 * ======================================================================
 * LA PASTILLA DE BANDA SE CORTABA AL ENSANCHARSE - 11/10/2026
 * ======================================================================
 *
 * Las dos frases juntas son el diagnostico entero, y la segunda es la que
 * lo cierra.
 *
 * EL FALLO. Esta funcion recorta en rds_x1(), y rds_x1() devuelve
 * s_band_x2 + 12. Pero s_band_x2 NO es una cuenta: es una variable que
 * draw_header() rellena MIENTRAS PINTA. O sea que aqui, antes de pintar,
 * vale lo de la pintada ANTERIOR.
 *
 * Mientras la etiqueta no cambie de ancho da igual. Pero al pasar de
 * "20 m" a "SSTV 20" la pastilla nueva es mas ancha que la ventana que se
 * calculo con la vieja, y lo que sobra se queda fuera del recorte: en su
 * foto se lee "SSTV 2" y el borde cortado a media letra.
 *
 * Y POR ESO SE ARREGLABA SOLO. Esa misma pintada, aunque salga cortada,
 * SI deja s_band_x2 con el valor nuevo -la asignacion esta fuera del
 * dibujo-, asi que el siguiente repintado ya recorta bien. "Despues de un
 * rato" era el siguiente repintado por cualquier otro motivo.
 *
 * EL ARREGLO. Si despues de pintar resulta que la pastilla pedia mas
 * sitio del que se le dio, se pinta otra vez - y ahora ya se sabe cuanto
 * mide. Solo ocurre cuando la etiqueta CRECE, o sea al cambiar de banda,
 * no en cada clic del mando.
 *
 * Y al reves no hace falta: si la etiqueta ENCOGE, la primera ventana es
 * la ancha, asi que limpia de sobra lo que habia y no deja cola. Es el
 * mismo cuidado que la chapa de ui_digi.c, pero aqui sale gratis.
 *
 * (Lo propio seria que la geometria de la cabecera se calculara sin
 * pintar, y entonces esto no haria falta. Se deja apuntado: son el bucle
 * de los digitos, el chip del modo y la pastilla, y mientras no se
 * separen, cualquier medida que dependa de ellos tiene este mismo riesgo.)
 */
void ui_top_draw_freq(const ui_top_state_t *st)
{
    int16_t x1 = rds_x1();

    gfx2_render(0, 0, x1, UI_TOP_H, draw_header, (void *)st);
    if (rds_x1() > x1) {
        /* draw_header calcula s_band_x2 MIENTRAS dibuja, asi que hasta
         * terminar la primera pasada no se sabe si la pastilla de banda ha
         * crecido. Cuando crece hay que pintar lo que queda a su derecha,
         * pero SOLO eso: antes se repetia la franja entera desde cero y se
         * escribian 27.392 pixeles dos veces -1,23 ms de bus con la CPU
         * parada- en cada cambio de banda y en cada cruce de 9.999 kHz.
         * gfx2_render recorta a la ventana que se le pide, asi que lo ya
         * pintado se queda como esta. */
        gfx2_render(x1, 0, (int16_t)(rds_x1() - x1), UI_TOP_H,
                    draw_header, (void *)st);
    }
}

/* Para el banco: ver ui_top_chip_w_dbg(). El enlazador la tira. */
/*
 * LA VENTANA DE VERDAD, NO UNA CONSTANTE QUE YA NO DIBUJA NADA -
 * 26/10/2026, decimotercera vuelta de la revision a fondo.
 *
 * Esto devolvia ST_MOVIL_W (418 px), y ST_MOVIL_W dejo de usarlo ningun
 * dibujo el 20/10, cuando ui_top_draw_smeter() paso a pedir una ventana de
 * knob_x() px. Su unico consumidor era esta funcion de depuracion... y el
 * banco sim/top_franjas.c, que comparaba contra ella y por tanto daba 134
 * px de margen FANTASMA donde el margen real es NEGATIVO.
 *
 * Por eso el banco pasaba en verde mientras el aviso de SOBRECARGA no se
 * pintaba: la pastilla del mando se queda fuera de la ventana de verdad.
 * Un banco que mide contra una constante muerta no vigila nada.
 */
int16_t ui_top_movil_w_dbg(void)
{
    ui_top_state_t st;

    /* knob_x() solo depende de la geometria fija: ver su "(void)st;". */
    memset(&st, 0, sizeof st);
    return knob_x(&st);
}
int16_t ui_top_read_x_dbg(void)   { return (int16_t)ST_READ_X; }
int16_t ui_top_knob_x_dbg(uint8_t con_dbm)
{
    ui_top_state_t t;
    t.dbm_valid = con_dbm;
    return knob_x(&t);
}
int16_t ui_top_knob_w_dbg(void) { return knob_w_fijo(); }

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

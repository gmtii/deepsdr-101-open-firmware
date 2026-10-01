#include "aviones_pantalla.h"
#include "idioma.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"

/*
 * LA PANTALLA DE COPIAR LA BASE DE AVIONES.
 *
 * *** El dueno, del primer intento: "es muy cutre", y despues "solo deberia
 * de haber cargado la pantalla de copiando". ***
 *
 * Tenia razon las dos veces, y la segunda explica la primera. El primer
 * intento pintaba la radio entera y le plantaba encima una caja negra con
 * borde de un pixel y la fuente de mapa de bits a doble tamano. O sea: la
 * radio aparecia, tapada; y lo que tapaba estaba dibujado con primitivas
 * que no usa ninguna otra pantalla de este firmware.
 *
 * Lo que va aqui es lo contrario de las dos cosas:
 *
 *   1. ES LA PANTALLA, no un cartel encima. Al arrancar, si hay fichero
 *      que copiar, esto es lo unico que se ve; la radio se pinta despues,
 *      una sola vez y ya con todo hecho. Asi no hay nada a medias en
 *      pantalla, ni hay que repintar el espectro para quitar un recuadro.
 *
 *   2. ESTA DIBUJADA COMO EL RESTO. gfx2, la paleta del tema -o sea que
 *      sigue al tema claro o oscuro que tenga puesto-, font_ui_18b para el
 *      titulo y font_ui_14 para lo demas, esquinas redondeadas y
 *      antialias. Las mismas piezas con las que estan hechas la pantalla
 *      de arranque y los paneles.
 *
 * SIN DEPENDENCIAS DE HARDWARE, igual que splash.c: no mira el reloj ni la
 * flash, solo pinta lo que le dicen. Asi el simulador de host la compila
 * tal cual y se puede ver como queda sin la placa delante.
 */

/*
 * Repartido a ojo NO: la pantalla mide 480 de alto y el bloque va
 * centrado en ella. Titulo arriba, raya, de que va, la barra, en que va,
 * y abajo del todo el aviso. Las separaciones son las mismas entre
 * bloques (44) y mas cortas dentro de un bloque (26), que es lo que hace
 * que se lea como tres cosas y no como seis lineas sueltas.
 */
#define TIT_Y      140
#define BARRA_W    520
#define BARRA_H     14
#define BARRA_Y    286
#define PIE_Y      322
#define CUENTA_Y   350
#define AVISO_Y    438

typedef struct {
    avip_fase_t fase;
    uint8_t     pct;
    const char *motivo;
    uint32_t    bytes;
    const char *titulo;   /* 0 = el de siempre; ver aviones_pantalla_t() */
    const char *que;
} ctx_t;

/*
 * Un entero con puntos de millar, que es como se leen 515.720.
 *
 * LOS GRUPOS SE CUENTAN DESDE LA DERECHA - 28/09/2026.
 *
 * *** Foto del dueño de la pantalla de carga: "665.60 de 512.000 bytes". ***
 *
 * Esto ponia el punto cada tres cifras contando desde la IZQUIERDA
 * -`k % 3`-, que da lo mismo cuando el numero tiene 3, 6 o 9 cifras y
 * esta mal en todos los demas: 66.560 salia "665.60".
 *
 * Y por eso no se habia visto: los numeros que enseña esta pantalla eran
 * hasta hoy 515.720, 512.000, 294.909... todos de seis cifras. El que lo
 * destapo es el contador de lo que lleva copiado, que empieza en 4.096 y
 * pasa por todos los largos hasta el total.
 */
static void miles(char *out, uint32_t v)
{
    char cifras[12];
    uint8_t n = 0U, i = 0U, k;

    do { cifras[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v != 0U);
    for (k = 0U; k < n; k++) {
        /* cuantas cifras quedan detras de esta, incluida: n - k */
        if (k != 0U && (((n - k) % 3U) == 0U)) { out[i++] = '.'; }
        out[i++] = cifras[n - 1U - k];
    }
    out[i] = '\0';
}

static void pon_pct(char *out, uint8_t pct)
{
    uint8_t i = 0U;

    if (pct >= 100U) { out[i++] = '1'; out[i++] = '0'; out[i++] = '0'; }
    else {
        if (pct >= 10U) { out[i++] = (char)('0' + (pct / 10U)); }
        out[i++] = (char)('0' + (pct % 10U));
    }
    out[i++] = ' '; out[i++] = '%'; out[i] = '\0';
}

static void pinta(gfx2_surf_t *s, void *ctx)
{
    const ctx_t *c = (const ctx_t *)ctx;
    int16_t x0 = (int16_t)((GFX2_W - BARRA_W) / 2);
    char linea[40];

    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));

    /* Titulo, centrado, con una raya fina debajo como en los paneles. */
    gfx2_text_in(s, 0, TIT_Y, GFX2_W,
                 (c->titulo != 0) ? c->titulo : tr("Base de datos de aviones", "Aircraft database"),
                 &font_ui_18b, gfx2_rgb(PAL_ACCENT), GFX2_ALIGN_C);
    gfx2_hline(s, x0, (int16_t)(TIT_Y + 44), BARRA_W, gfx2_rgb(PAL_LINE));

    gfx2_text_in(s, 0, (int16_t)(TIT_Y + 74), GFX2_W,
                 (c->que != 0) ? c->que
                               : tr("ICAO24.BIN, del disco a la zona alta de la flash",
                                      "ICAO24.BIN, from disk to the upper flash area"),
                 &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);

    /*
     * LA BARRA. El canal siempre, y encima lo que lleve. Se pinta el canal
     * aunque el porcentaje sea 0 para que se vea CUANTO queda, que es la
     * unica informacion que de verdad se busca mirando una barra.
     */
    gfx2_rrect(s, x0, BARRA_Y, BARRA_W, BARRA_H, BARRA_H / 2,
               gfx2_rgb(PAL_SURF_2));
    if (c->fase == AVIP_COPIANDO || c->fase == AVIP_COMPROBANDO ||
        c->fase == AVIP_HECHO) {
        uint8_t p = (c->fase == AVIP_HECHO) ? 100U : c->pct;
        int16_t w = (int16_t)(((int32_t)BARRA_W * (int32_t)p) / 100);

        /* Menos de lo que mide el redondeo no se pinta: saldria una pastilla
         * deforme en vez de un principio de barra. */
        if (w >= BARRA_H) {
            gfx2_rrect(s, x0, BARRA_Y, w, BARRA_H, BARRA_H / 2,
                       gfx2_rgb(PAL_ACCENT));
        }
    }
    gfx2_rrect_outline(s, x0, BARRA_Y, BARRA_W, BARRA_H, BARRA_H / 2, 1,
                       gfx2_rgb(PAL_LINE));

    /* Y debajo, en que va. */
    switch (c->fase) {
    case AVIP_BUSCANDO:
        gfx2_text_in(s, 0, PIE_Y, GFX2_W, tr("Abriendo...", "Opening..."),
                     &font_ui_14, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        break;
    case AVIP_COPIANDO:
        pon_pct(linea, c->pct);
        gfx2_text_in(s, 0, PIE_Y, GFX2_W, linea,
                     &font_ui_14, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        /*
         * Y los bytes debajo. El porcentaje dice cuanto FALTA; los bytes
         * dicen que sigue pasando algo. En una copia de medio minuto por
         * un bus lento, un 37 % quieto cuatro segundos y un contador que
         * se mueve no se parecen en nada desde fuera.
         */
        {
            char t[48];
            uint8_t i = 0U, k = 0U;
            char n1[16], n2[16];

            miles(n1, (uint32_t)(((uint64_t)c->bytes * c->pct) / 100U));
            miles(n2, c->bytes);
            while (n1[k] != '\0') { t[i++] = n1[k++]; }
            t[i++] = ' '; t[i++] = 'd'; t[i++] = 'e'; t[i++] = ' ';
            k = 0U;
            while (n2[k] != '\0') { t[i++] = n2[k++]; }
            t[i++] = ' '; t[i++] = 'b'; t[i++] = 'y'; t[i++] = 't';
            t[i++] = 'e'; t[i++] = 's'; t[i] = '\0';
            gfx2_text_in(s, 0, CUENTA_Y, GFX2_W, t,
                         &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);
        }
        break;
    case AVIP_COMPROBANDO:
        /*
         * El texto dice que ESTO ES OTRA COSA, no la copia otra vez. Sin
         * eso, una barra que vuelve a cero despues de llegar al final se
         * lee como que la copia ha empezado de nuevo.
         */
        pon_pct(linea, c->pct);
        gfx2_text_in(s, 0, PIE_Y, GFX2_W, tr("Comprobando lo copiado...", "Verifying the copy..."),
                     &font_ui_14, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        gfx2_text_in(s, 0, CUENTA_Y, GFX2_W, linea,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);
        break;
    case AVIP_HECHO:
        miles(linea, c->bytes);
        {
            uint8_t i = 0U;
            char t[64];
            const char *a = tr("Cargada: ", "Loaded: ");
            const char *b = tr(" bytes. Borrada del disco.", " bytes. Deleted from the disk.");
            while (*a != '\0') { t[i++] = *a++; }
            { uint8_t k = 0U; while (linea[k] != '\0') { t[i++] = linea[k++]; } }
            while (*b != '\0') { t[i++] = *b++; }
            t[i] = '\0';
            gfx2_text_in(s, 0, PIE_Y, GFX2_W, t,
                         &font_ui_14, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        }
        break;
    default:
        /*
         * EL MOTIVO, NO UN "NO" A SECAS. Un "no se pudo" que no dice de
         * que es un "no" manda a mirar al sitio equivocado: el disco, el
         * fichero y el sitio de destino fallan de maneras distintas y se
         * arreglan de maneras distintas.
         */
        gfx2_text_in(s, 0, PIE_Y, GFX2_W,
                     (c->motivo != (const char *)0) ? c->motivo : tr("No se pudo", "Failed"),
                     &font_ui_14, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        break;
    }

    /*
     * Y abajo el aviso, solo mientras copia. Escribir en la flash externa
     * a medias deja la base inservible -no rota, INSERVIBLE: la busqueda
     * binaria sobre medio fichero devuelve modelos equivocados, que es
     * peor que no devolver ninguno-. Es medio minuto: merece la pena
     * pedirlo.
     */
    if (c->fase == AVIP_BUSCANDO || c->fase == AVIP_COPIANDO ||
        c->fase == AVIP_COMPROBANDO) {
        gfx2_text_in(s, 0, AVISO_Y, GFX2_W,
                     tr("No apagues la radio hasta que termine", "Do not switch the radio off until it finishes"),
                     &font_ui_14, gfx2_rgb(PAL_LINE), GFX2_ALIGN_C);
    }
}

/*
 * PARA EL BANCO, y por eso esta aqui y no alli.
 *
 * miles() es estatica y el banco no puede llamarla; copiarla al banco es
 * exactamente como se consigue que el banco mida una cosa y la radio otra.
 * Con -ffunction-sections y --gc-sections el enlazador la tira, asi que en
 * la placa no cuesta ni un byte.
 */
void aviones_pantalla_miles_dbg(char *out, uint32_t v) { miles(out, v); }

void aviones_pantalla_t(avip_fase_t fase, uint8_t pct, const char *motivo,
                        uint32_t bytes, const char *titulo, const char *que)
{
    ctx_t c;

    c.fase = fase;
    c.pct = pct;
    c.motivo = motivo;
    c.bytes = bytes;
    c.titulo = titulo;
    c.que = que;
    gfx2_render_screen(pinta, &c);
}

void aviones_pantalla(avip_fase_t fase, uint8_t pct, const char *motivo,
                      uint32_t bytes)
{
    aviones_pantalla_t(fase, pct, motivo, bytes, 0, 0);
}

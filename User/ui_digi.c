#include "ui_digi.h"
#include <stdint.h>
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"

/* 18 px de renglon con una fuente de 14 deja 4 de aire: ocho renglones en
 * 144 px exactos, mas los 30 de la cabecera. UDG_LINE_H vive en la cabecera
 * porque main.c necesita cuadrar el alto del panel con el. */
#define UDG_PAD_X   6

/* La cabecera: el boton a la izquierda, la chapa a la derecha. */
static int16_t btn_y(const ui_digi_state_t *st) { return (int16_t)(st->y + 3); }

/* Cuantos renglones pinta este panel. Un 0 en la estructura quiere decir
 * "los de siempre": asi los paneles que ya existian no tuvieron que tocar
 * nada cuando FT8 pidio el doble. Ver UDG_ROWS_MAX en la cabecera. */
static uint8_t nfilas(const ui_digi_state_t *st)
{
    uint8_t n = st->filas ? st->filas : (uint8_t)UDG_ROWS;
    return (n > (uint8_t)UDG_ROWS_MAX) ? (uint8_t)UDG_ROWS_MAX : n;
}
/*
 * LA CHAPA Y EL QUINTO BOTON SE PELEABAN POR EL SITIO - 28/09/2026.
 *
 * Exacto, y medido antes de tocar nada -con ui_digi_btn5_*_dbg()-:
 *
 *     JTTY, la chapa de 1 a 4 cifras   btn5 x617 -> x590   se MUEVE 27 px
 *     HFDL, "5% 12/34" -> "95% 12/34"  btn5 w 91 -> w 82   se ESTRECHA
 *     HFDL, "100% 999/999"             btn5 w  0           DESAPARECE
 *
 * La causa es que la caja de la chapa medía lo que midiera su texto, asi
 * que su borde izquierdo se movia, y el quinto boton se colocaba contra
 * ese borde. Un boton cuyo sitio y tamaño dependen de un contador que
 * cambia solo no se encuentra con el dedo - y lo tercero es peor que las
 * dos primeras: el boton no se encogia, se iba-.
 *
 * AHORA LA DERECHA DE LA CABECERA ES FIJA. La chapa tiene reservado
 * UDG_CHIP_RES pase lo que pase con su texto, el quinto boton tiene ancho
 * y sitio constantes, y el cuarto se queda con lo que hay entre el
 * principio de la zona y el quinto - que tambien sale constante-. Ninguno
 * de los tres depende ya de lo que los otros esten enseñando.
 *
 * El texto de la chapa se pega a la DERECHA, contra el punto de
 * sincronismo, para que un contador crezca hacia la izquierda desde un
 * sitio fijo en vez de empujar. Con la caja pegada al texto -los modos sin
 * quinto boton, SSTV y wefax- sale en x+10, que es donde estaba.
 */
#define UDG_CHIP_RES  140
/* Lo maximo que puede medir la chapa cuando NO hay quinto boton: su sitio
 * mas el del quinto (UDG_BTN5_W, 92, que se define mas abajo junto al resto
 * de su geometria; el _Static_assert de alli ata los dos numeros para que no
 * se separen). Asi el cuarto conserva sus UDG_BTN4_MAX_W pase lo que pase
 * con el texto. Ver chip_w(). */
#define UDG_CHIP_SIN5_MAX  (UDG_CHIP_RES + UDG_BTN2_GAP + 92)

/*
 * LA CHAPA SOLA EN SU MITAD DE LA CABECERA.
 *
 * O sea: ni barra, ni tercer boton, ni cuarto, ni quinto. Es la cabecera
 * de los tres modos que pintan imagen -WEFAX, SSTV y Feld-Hell-, que solo
 * llevan los dos botones de la izquierda y la chapa.
 *
 * Cuando pasa eso, entre donde acaba el segundo boton y donde empieza la
 * chapa hay 170 px sin nada, y la chapa los puede usar. No es por hacerla
 * grande: la cabecera del fax dice en que fase va -"Esperando el tono de
 * arranque"- y eso no cabia en lo que quedaba.
 *
 * Se pregunta por los CINCO y no solo por el cuarto a proposito: la franja
 * que se repinta crece con esto (ver UDG_CHIP_ANCHO_X0), y una franja que
 * crece por encima de algo que si esta dibujado se lo lleva por delante.
 * Esa averia ya ha salido cinco veces en este mismo sitio - ver solo_chip().
 */
static uint8_t chip_sola(const ui_digi_state_t *st)
{
    return (uint8_t)(!st->btn3 && !st->btn4 && !st->btn5
                     && !st->barra_on && !st->barra_dcha);
}

/* Hasta donde llega la chapa cuando esta sola: desde el final del hueco
 * del segundo boton hasta el borde derecho. El segundo acaba en
 * UDG_BTN2_X + UDG_BTN2_W y se le deja el mismo aire que entre dos
 * botones; si no hubiera segundo boton, ese tramo se queda vacio y no
 * pasa nada, que es lo mismo que pasaba antes. */
#define UDG_CHIP_ANCHO_X0  (UDG_BTN2_X + UDG_BTN2_W + UDG_BTN2_GAP)
#define UDG_CHIP_ANCHO_W   (GFX2_W - UDG_PAD_X - UDG_CHIP_ANCHO_X0)

static int16_t chip_w(const ui_digi_state_t *st)
{
    int16_t w;

    if (!st->chip) { return 0; }
    w = (int16_t)(gfx2_text_w(st->chip, &font_ui_14b) + 34);
    /*
     * CON QUINTO BOTON, NI MENOS NI MAS QUE LO RESERVADO.
     *
     * El minimo ya estaba, y es lo que impide que el borde izquierdo se
     * mueva y arrastre al boton. Faltaba el TOPE, y se vio el 02/10/2026
     * visto en pantalla: el estado del demodulador de STANAG pone
     * cosas como "llenando el entrelazador 180/392", la chapa crecia
     * hacia la izquierda y se pintaba ENCIMA del quinto boton.
     *
     * El arreglo de verdad no es acortar ese texto -que tambien- sino
     * que la chapa NO PUEDA invadirlo: lo reservado es lo reservado, y
     * el texto que no quepa se recorta. Un panel no se rompe porque otro
     * modulo escriba una linea larga.
     */
    if (st->btn5) {
        w = (int16_t)UDG_CHIP_RES;
    } else {
        /*
         * Y SIN QUINTO BOTON, EL MISMO CUIDADO CON EL CUARTO. 11/10/2026.
         *
         * El tope de arriba lo puso una foto del 02/10 en la que la chapa
         * se comia el quinto boton. Pero sin quinto boton la chapa seguia
         * creciendo libre, y entonces se come EL CUARTO: btn4_w() se mide
         * contra chip_x(), asi que una chapa larga lo estrecha y, pasada
         * de cierto punto, lo deja en ancho CERO y desaparece.
         *
         * Medido con las propias funciones del modulo, con btn3 y btn4
         * puestos y sin btn5:
         *
         *   "Esperando una imagen"              btn4 = 112   (su tope)
         *   "Modo no soportado, VIS 123"        btn4 = 106   se estrecha
         *   "llenando el entrelazador 180/392"  btn4 =   0   DESAPARECE
         *
         * Con btn5 puesto, los tres dan 112. O sea: era el mismo fallo del
         * 02/10 una posicion mas a la izquierda, y el comentario de
         * UDG_CHIP_RES -"ahora la derecha de la cabecera es fija"- solo era
         * verdad con quinto boton.
         *
         * Lo reservado es lo reservado: la chapa se topa a lo que queda
         * cuando el cuarto tiene su ancho maximo, que es exactamente el
         * sitio del quinto mas el suyo. El texto que no quepa se recorta,
         * igual que arriba. Un panel no se queda sin boton porque otro
         * modulo escriba una linea larga.
         */
        /*
         * Y SI NO HAY CUARTO BOTON, NO HAY NADA QUE RESERVAR - 19/10/2026.
         *
         * Avisado por el usuario: en WEFAX, "Esperando el tono de arranque"
         * salia como "ono de arranque".
         *
         * El tope de aqui arriba guarda el sitio del cuarto boton. Pero
         * btn4_w() devuelve CERO cuando no hay cuarto boton, o sea que en
         * un panel que no lo tiene el tope estaba guardando el sitio de
         * algo que no existe: 242 px de chapa, 208 para el texto, y la
         * cabecera del fax mide 316 con el estado y la velocidad. Lo que
         * sobraba se iba por delante -que es por donde recorta esta chapa,
         * y en un contador es lo que menos duele- asi que lo que se perdia
         * era justo la palabra que dice en que fase va, que es para lo que
         * existe esa cabecera.
         *
         * Lo reservado sigue siendo lo reservado. Lo que cambia es que
         * cuando no hay nada reservado la chapa se queda con el sitio
         * entero: ver chip_sola() y UDG_CHIP_ANCHO_W.
         */
        if (chip_sola(st)) {
            if (w > (int16_t)UDG_CHIP_ANCHO_W) { w = (int16_t)UDG_CHIP_ANCHO_W; }
        } else if (w > (int16_t)UDG_CHIP_SIN5_MAX) {
            w = (int16_t)UDG_CHIP_SIN5_MAX;
        }
    }
    return w;
}
static int16_t chip_x(const ui_digi_state_t *st)
{
    return (int16_t)(GFX2_W - UDG_PAD_X - chip_w(st));
}

/* --- la cabecera: boton a la izquierda, chapa a la derecha ---------------- */

/* Un boton de la cabecera. Los dos se pintan igual y solo cambian donde
 * empiezan y que rotulo llevan, asi que se dibujan con la misma funcion:
 * dos copias del mismo rectangulo redondeado es como se acaba con uno de
 * los dos un pixel mas alto que el otro. */
static void pinta_uno(gfx2_surf_t *s, const ui_digi_state_t *st,
                      int16_t x, int16_t w, const char *rotulo, uint8_t press)
{
    gfx2_rgba_t fondo = press ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_SURF_2);
    gfx2_rgba_t borde = press ? gfx2_rgb(PAL_ACCENT) : gfx2_rgb(PAL_LINE);
    gfx2_rgba_t tinta = press ? gfx2_rgb(PAL_SURF_0) : gfx2_rgb(PAL_INK_DIM);

    if (!rotulo) { return; }
    gfx2_rrect(s, x, btn_y(st), w, UDG_BTN_H, 6, fondo);
    gfx2_rrect_outline(s, x, btn_y(st), w, UDG_BTN_H, 6, 1, borde);
    gfx2_text_in(s, x, (int16_t)(btn_y(st) + 4), w, rotulo,
                 &font_ui_14b, tinta, GFX2_ALIGN_C);
}

static void pinta_boton2(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    pinta_uno(s, st, UDG_BTN2_X, UDG_BTN2_W, st->btn2, st->btn2_press);
}

static void pinta_boton(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    pinta_uno(s, st, UDG_BTN_X, UDG_BTN_W, st->btn, st->btn_press);
}

/*
 * LO QUE DE VERDAD SE VE DE LA CHAPA.
 *
 * Recortado a lo que cabe: ver el tope de chip_w(). Se quitan letras POR
 * DELANTE, que en un contador -que es para lo que nacio esta chapa- es lo
 * que menos duele.
 *
 * Esta en una funcion y no escrito dentro de pinta_chip() porque hay un
 * banco que comprueba que la cabecera del fax no se recorta
 * (sim/chapa_fax.c), y lo unico que vale para eso es preguntarle al modulo
 * que esta sacando por pantalla. Un banco con una copia del recorte mide
 * su copia.
 */
static const char *chip_txt(const ui_digi_state_t *st)
{
    const char *txt = st->chip ? st->chip : "";
    int16_t hueco = (int16_t)(chip_w(st) - 34);

    while (*txt != '\0' && gfx2_text_w(txt, &font_ui_14b) > hueco) { txt++; }
    return txt;
}

const char *ui_digi_chip_visible(const ui_digi_state_t *st)
{
    return chip_txt(st);
}

static void pinta_chip(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    int16_t x, w;

    if (!st->chip) { return; }
    w = chip_w(st);
    x = chip_x(st);
    gfx2_rrect(s, x, btn_y(st), w, UDG_BTN_H, 6, gfx2_rgb(PAL_SURF_2));
    gfx2_rrect_outline(s, x, btn_y(st), w, UDG_BTN_H, 6, 1, gfx2_rgb(PAL_LINE));
    /* Pegado a la derecha: con la caja justa sale en x+10 -donde estaba- y
     * con la caja reservada el numero crece hacia la izquierda desde el
     * punto en vez de empujar al boton. Ver chip_w(). */
    {
        const char *txt = chip_txt(st);
        gfx2_text(s, (int16_t)(x + w - 24 - gfx2_text_w(txt, &font_ui_14b)),
                  (int16_t)(btn_y(st) + 4),
                  txt, &font_ui_14b, gfx2_rgb(PAL_INK));
    }
    /* El punto: verde cuando lo que llega tiene forma de Morse, apagado
     * cuando no. No es adorno - es la diferencia entre "esta leyendo" y
     * "esta inventando letras a partir de ruido", que desde fuera no se
     * distingue mirando el texto. */
    gfx2_rrect(s, (int16_t)(x + w - 16), (int16_t)(btn_y(st) + UDG_BTN_H / 2 - 4), 8, 8, 4,
               st->enganchado ? gfx2_rgb(PAL_OK) : gfx2_rgb(PAL_LINE));
}

/*
 * LA BARRA DE PROGRESO. Ocupa el hueco de los dos botones, porque el panel
 * que la usa -FT8- no tiene nada que borrar ni ningun formato que elegir.
 *
 * El rotulo va DENTRO y centrado, no al lado: la barra es ancha y el numero
 * pequeno, y ponerlo fuera obliga a buscarlo con la vista cada vez.
 */
/*
 * DONDE VA - 25/09/2026, segunda version. Ocupaba el hueco de los DOS
 * botones, porque FT8 no tenia ninguno. Ahora tiene el de borrar (pedido por
 * el usuario, que echaba en falta uno), asi que la barra se queda
 * con el hueco del segundo, desde donde acaba el primero hasta donde empieza
 * la chapa.
 *
 * ANCHO, y por que 300. Empezo con 150 y cabia el texto de entonces por 9 px.
 * Al anadirle el desfase de ranura se fue a 170 y dejo de caber - que es
 * justo lo que avisaba el comentario que habia aqui: "si algun dia se le
 * anade un dato mas, hay que volver a medir".
 *
 * Entre donde acaba la barra y donde se dibuja la chapa -pegada al borde
 * derecho y midiendo unos 66 px- habia 460 px sin nada. Asi que la barra se
 * queda con 300 y la zona de la chapa empieza en 440: sigue siendo media
 * pantalla de zona tactil para un dedo.
 */
#define UDG_BAR_X  UDG_BTN2_X
#define UDG_BAR_W  300

/*
 * El aire entre los dos textos de la barra: el centrado y el que va pegado
 * al punto. Ver pinta_barra(). Diez pixeles son los mismos que separan dos
 * botones (UDG_BTN2_GAP), que es la distancia a la que el ojo ya lee "dos
 * cosas" en esta cabecera.
 */
#define UDG_BAR_AIRE  10

/* Donde empieza la franja de la derecha. Esta repetido mas abajo como
 * UDG_CHIP_X0 -donde vive su comentario- porque el cuarto boton, que se
 * define antes, lo necesita. El _Static_assert de alli impide que se
 * separen. */
#define UDG_CHIP_X0_F  (UDG_BAR_X + UDG_BAR_W + UDG_BTN2_GAP)

/*
 * LA SEGUNDA DISPOSICION: los cuatro botones juntos a la izquierda y la
 * barra a la derecha del todo, en el sitio que dejaba la chapa. Ver
 * barra_dcha en ui_digi.h.
 *
 * Los numeros no se eligen uno a uno: se encadenan desde el primer boton
 * con el hueco de siempre, y lo que sobra hasta el borde es la barra. El
 * _Static_assert de abajo es el que impide que un boton mas ancho se coma
 * la barra en silencio.
 */
#define UDG_D2_BTN3_X  (UDG_BTN_X + UDG_BTN_W + UDG_BTN2_GAP)
#define UDG_D2_BTN3_W  104
#define UDG_D2_BTN4_X  (UDG_D2_BTN3_X + UDG_D2_BTN3_W + UDG_BTN2_GAP)
#define UDG_D2_BTN4_W  112
#define UDG_D2_BTN5_X  (UDG_D2_BTN4_X + UDG_D2_BTN4_W + UDG_BTN2_GAP)
#define UDG_D2_BTN5_W  92
#define UDG_D2_BAR_X   (UDG_D2_BTN5_X + UDG_D2_BTN5_W + UDG_BTN2_GAP)
#define UDG_D2_BAR_W   (GFX2_W - UDG_PAD_X - UDG_D2_BAR_X)
/* 200 es el texto mas largo que la barra de FT8 puede llegar a tener mas
 * el aire de los lados; por debajo de eso esta disposicion no vale y hay
 * que estrechar un boton, no recortar un numero. */
_Static_assert(UDG_D2_BAR_W >= 200, "la barra de la derecha se ha quedado sin sitio");

/* El ancho que pide quien llama, o el de siempre. */
static int16_t bar_w(const ui_digi_state_t *st)
{
    if (st->barra_dcha) { return (int16_t)UDG_D2_BAR_W; }
    return (st->barra_w > 0 && st->barra_w <= UDG_BAR_W) ? st->barra_w
                                                         : (int16_t)UDG_BAR_W;
}

/* Y donde empieza. Lo usan el dibujo y el repintado de la barra sola. */
static int16_t bar_x(const ui_digi_state_t *st)
{
    return st->barra_dcha ? (int16_t)UDG_D2_BAR_X : (int16_t)UDG_BAR_X;
}

/*
 * EL TERCER BOTON: donde acaba la barra, hasta donde empieza la chapa.
 *
 * Se calcula, no se escribe: asi acortar la barra le da sitio solo y nadie
 * tiene que acordarse de mover un numero en dos ficheros. Si el hueco no
 * llega a 70 px -o sea, si la barra no se ha acortado- devuelve 0 y el boton
 * no se dibuja. Ver btn3 en ui_digi.h.
 */
#define UDG_BTN3_MIN_W 70
static int16_t btn3_x(const ui_digi_state_t *st)
{
    if (st->barra_dcha) { return (int16_t)UDG_D2_BTN3_X; }
    return (int16_t)(UDG_BAR_X + bar_w(st) + UDG_BTN2_GAP);
}
static int16_t btn3_w(const ui_digi_state_t *st)
{
    int16_t w;

    if (st->barra_dcha) { return st->btn3 ? (int16_t)UDG_D2_BTN3_W : 0; }

    /* Mirar st->btn3 aqui tambien: pinta_boton3(), draw_boton3() y
     * boton3_hit() ya lo miran, y este no. Como el ancho de la banda que
     * se repinta sale de aqui, un panel que acorte la barra sin poner
     * tercer boton pedia repintar 130 px que nadie pinta - y lo que no se
     * pinta sale negro. */
    if (!st->btn3) { return 0; }
    w = (int16_t)(UDG_BAR_X + UDG_BAR_W - btn3_x(st));
    return (w >= UDG_BTN3_MIN_W) ? w : 0;
}

static void pinta_barra(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    uint16_t tope = st->barra_max ? st->barra_max : 1U;
    uint16_t v = (st->barra_v > tope) ? tope : st->barra_v;
    int16_t  llena = (int16_t)(((uint32_t)(uint16_t)bar_w(st) * v) / tope);

    if (!st->barra_on) { return; }

    gfx2_rrect(s, bar_x(st), btn_y(st), bar_w(st), UDG_BTN_H, 6, gfx2_rgb(PAL_SURF_2));
    if (llena > 0) {
        /* Sin esquinas redondeadas en la parte llena: una barra que crece
         * desde cero con las esquinas redondeadas parece que arranca a
         * tirones en los primeros pixeles. */
        gfx2_fill(s, (int16_t)(bar_x(st) + 1), (int16_t)(btn_y(st) + 1),
                  (llena > 2) ? (int16_t)(llena - 2) : llena,
                  UDG_BTN_H - 2, gfx2_rgb(PAL_ACCENT));
    }
    gfx2_rrect_outline(s, bar_x(st), btn_y(st), bar_w(st), UDG_BTN_H, 6, 1,
                       gfx2_rgb(PAL_LINE));
    {
        /* Con el punto puesto, el texto se centra en lo que queda a su
         * izquierda: centrarlo en la barra entera lo metería por debajo. */
        int16_t hueco = (int16_t)(bar_w(st) - (st->barra_dcha ? 20 : 0));

        /*
         * Y LO QUE VA PEGADO AL PUNTO, si lo hay - 13/10/2026.
         *
         * Se dibuja PRIMERO para poder descontar su ancho del hueco del
         * otro: así no hay forma de que los dos se pisen por largo que se
         * ponga ninguno. Lo que se le quita al centrado es su ancho más
         * UDG_BAR_AIRE de separación, que es lo que hace que se lea como
         * dos cosas y no como una frase partida.
         */
        if (st->barra_txt_dcha && st->barra_dcha) {
            int16_t w2 = gfx2_text_w(st->barra_txt_dcha, &font_ui_14b);

            gfx2_text_in(s, (int16_t)(bar_x(st) + hueco - w2),
                         (int16_t)(btn_y(st) + 4), w2,
                         st->barra_txt_dcha, &font_ui_14b,
                         gfx2_rgb(PAL_INK), GFX2_ALIGN_L);
            hueco = (int16_t)(hueco - w2 - UDG_BAR_AIRE);
            if (hueco < 0) { hueco = 0; }
        }

        if (st->barra_txt && hueco > 0) {
            gfx2_text_in(s, bar_x(st), (int16_t)(btn_y(st) + 4), hueco,
                         st->barra_txt, &font_ui_14b, gfx2_rgb(PAL_INK),
                         GFX2_ALIGN_C);
        }
    }
    /*
     * Y LA LUCECITA, cuando es la barra la que lleva lo que llevaba la
     * chapa - 13/10/2026.
     *
     * Las dos cosas se quedan, y las dos estan aqui: el 8/15 lo escribe
     * quien arma el texto -ft8_chapa() en main.c- y el punto lo pinta
     * esto, en el mismo sitio de la caja donde lo pintaba la chapa
     * (ver pinta_chip): pegado a la derecha y centrado de alto. Mover una
     * funcion de sitio no es quitarla, pero solo si llega entera.
     */
    if (st->barra_dcha) {
        gfx2_rrect(s, (int16_t)(bar_x(st) + bar_w(st) - 16),
                   (int16_t)(btn_y(st) + UDG_BTN_H / 2 - 4), 8, 8, 4,
                   st->enganchado ? gfx2_rgb(PAL_OK) : gfx2_rgb(PAL_LINE));
    }
}

/*
 * EL CUARTO BOTON: desde donde acaba la franja de la barra hasta donde
 * empieza la chapa. Tambien calculado y no escrito, y por el mismo motivo
 * que el tercero: la chapa encoge y crece con su texto, asi que el hueco
 * que deja no es un numero fijo que se pueda apuntar en ningun sitio.
 *
 * 90 px de minimo: "21.955" mide 52 con font_ui_14b y el boton pide 34 de
 * aire a los lados, medido con pinta_uno(). Menos de eso y el rotulo sale
 * cortado, que en un boton que dice en que frecuencia estas es peor que no
 * tener boton.
 */
/*
 * Y TOPE, ademas de minimo. El hueco que deja la chapa de HFDL son 213-267
 * px segun lo que ponga -medido-, y un boton de 240 px para escribir
 * "11.184" no parece un boton: parece una barra con un numero perdido
 * dentro. 150 es lo que da un boton de proporcion normal al lado de
 * "Frec.", que mide 104.
 */
/*
 * EL TOPE BAJA DE 150 A 112, y es lo que paga el sitio fijo del quinto.
 *
 * La zona de la derecha mide 364 px y ahora se reparte en constantes:
 * 112 el cuarto, 10 de hueco, 92 el quinto, 10 de hueco y 140 la chapa.
 * Suman 364 justos -lo comprueba el _Static_assert de abajo-.
 *
 * Con 150 no salia: la chapa de HFDL en su caso peor pide 139, y
 * 150+10+139 ya se come los 364 sin dejar nada para el quinto. Eso es
 * exactamente lo que pasaba -btn5 con ancho 0, medido-, solo que en vez de
 * quedarse sin sitio de una manera visible el boton simplemente no se
 * pintaba.
 *
 * 112 sigue pareciendo un boton al lado de "Frec.", que mide 104, y le
 * sobra para "11.184" -que mide 55-.
 */
#define UDG_BTN4_MIN_W 90
#define UDG_BTN4_MAX_W 112

/* El quinto boton: dos constantes. Ver pinta_boton5(). */
#define UDG_BTN5_W  92
/* Y que el tope de la chapa sin quinto boton siga siendo el sitio del quinto
 * mas el suyo. Son dos sitios porque uno va antes que el otro en el fichero;
 * esto impide que se separen. Ver chip_w(). */
_Static_assert(UDG_CHIP_SIN5_MAX == UDG_CHIP_RES + UDG_BTN2_GAP + UDG_BTN5_W,
               "el tope de la chapa sin quinto boton no cuadra con UDG_BTN5_W");
#define UDG_BTN5_X  (GFX2_W - UDG_PAD_X - UDG_CHIP_RES \
                     - UDG_BTN2_GAP - UDG_BTN5_W)

/* La zona de la derecha, repartida en constantes y sin que sobre ni falte. */
_Static_assert(UDG_CHIP_X0_F + UDG_BTN4_MAX_W + UDG_BTN2_GAP + UDG_BTN5_W
               + UDG_BTN2_GAP + UDG_CHIP_RES == GFX2_W - UDG_PAD_X,
               "la cabecera digital no cuadra: cuarto + quinto + chapa");
/* Y el quinto empieza justo donde acaba el cuarto en su tope. */
_Static_assert(UDG_BTN5_X == UDG_CHIP_X0_F + UDG_BTN4_MAX_W + UDG_BTN2_GAP,
               "el quinto boton no cae detras del cuarto");
static int16_t btn4_x(const ui_digi_state_t *st)
{
    if (st->barra_dcha) { return (int16_t)UDG_D2_BTN4_X; }
    return (int16_t)UDG_CHIP_X0_F;
}
static int16_t btn4_w(const ui_digi_state_t *st)
{
    int16_t w;

    if (!st->btn4) { return 0; }
    if (st->barra_dcha) { return (int16_t)UDG_D2_BTN4_W; }
    /*
     * El tope de la derecha es el quinto boton cuando lo hay -que esta en
     * un sitio FIJO- y la chapa cuando no. Antes era siempre la chapa, y
     * por eso el cuarto tambien se movia con el contador.
     */
    w = (int16_t)((st->btn5 ? (int16_t)UDG_BTN5_X : chip_x(st))
                  - btn4_x(st) - UDG_BTN2_GAP);
    if (w > UDG_BTN4_MAX_W) { w = UDG_BTN4_MAX_W; }
    return (w >= UDG_BTN4_MIN_W) ? w : 0;
}

static void pinta_boton4(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    int16_t w = btn4_w(st);

    if (w == 0) { return; }
    pinta_uno(s, st, btn4_x(st), w, st->btn4, st->btn4_press);
}

/*
 * EL QUINTO: EL MAPA - 28/09/2026. Ver btn5 en ui_digi.h.
 *
 * Vive pegado a la chapa, que es lo que lo pone debajo de BW pase lo que
 * pase con lo que tenga a la izquierda. Se coloca desde la DERECHA y no
 * desde el final del cuarto boton a proposito: el cuarto cambia de ancho
 * con el rotulo -"11.184" no mide lo que "Frec."- y un boton que se mueve
 * de sitio segun la frecuencia sintonizada no se encuentra con el dedo.
 *
 * Y si no cabe, no sale. Lo mismo que el tercero y el cuarto: mas vale que
 * falte a que se monte encima de la chapa.
 */
/*
 * Y AQUI NO SE CALCULA NADA: DOS CONSTANTES.
 *
 * Esto era un hueco medido entre lo de la izquierda y la chapa, con un
 * minimo y un maximo, y de ahi salian las tres cosas que veia el usuario de
 * la radio -el boton se movia, se estrechaba y a veces desaparecia-.
 *
 * Un boton que se toca con el dedo tiene que estar SIEMPRE en el mismo
 * sitio y del mismo tamaño, asi que su rectangulo no puede depender de lo
 * que los vecinos esten enseñando. Se reserva y punto.
 */
/*
 * Y AQUI NO HAY RAMA, aunque lo pareciera - 13/10/2026.
 *
 * Esto decia `barra_dcha ? UDG_D2_BTN5_W : UDG_BTN5_W` y las dos
 * constantes valen 92: el compilador lo venia avisando
 * (-Wduplicated-branches) desde que existe la segunda disposicion, y el
 * bloque "Estado" decia mientras tanto "avisos del compilador 0".
 *
 * Una rama cuyos dos lados hacen lo mismo no documenta nada: lo que hay
 * que dejar escrito es que los dos repartos le dan el mismo ancho A
 * PROPOSITO, y eso lo dice mejor el _Static_assert, que ademas se queja
 * el dia que uno de los dos cambie.
 */
static int16_t btn5_w(const ui_digi_state_t *st)
{
    _Static_assert(UDG_D2_BTN5_W == UDG_BTN5_W,
                   "el quinto boton ya no mide lo mismo en las dos "
                   "disposiciones: aqui vuelve a hacer falta la rama");
    if (!st->btn5) { return 0; }
    return (int16_t)UDG_BTN5_W;
}

static int16_t btn5_x(const ui_digi_state_t *st)
{
    if (st->barra_dcha) { return (int16_t)UDG_D2_BTN5_X; }
    return (int16_t)UDG_BTN5_X;
}

static void pinta_boton5(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    int16_t w = btn5_w(st);

    if (w == 0) { return; }
    pinta_uno(s, st, btn5_x(st), w, st->btn5, st->btn5_press);
}

static void pinta_boton3(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    int16_t w = btn3_w(st);

    if (!st->btn3 || w == 0) { return; }
    pinta_uno(s, st, btn3_x(st), w, st->btn3, st->btn3_press);
}

/*
 * REPINTAR LA BARRA NO PUEDE BORRAR AL VECINO. 25/09/2026.
 *
 * Y no estaba porque lo borraba esto. Cuando solo cambia la barra se repinta
 * ella sola, y para eso primero se limpia su zona... que estaba escrita como
 * UDG_BAR_W fijo, o sea los 300 de siempre. Con la barra acortada a 170 esos
 * 300 incluyen el hueco del tercer boton, asi que cada repintado de la barra
 * se lo llevaba por delante.
 *
 * En HFDL la barra cambia cada dos por tres -es la fase del preambulo- asi
 * que el boton aparecia en el repintado entero y desaparecia al instante
 * siguiente. De ahi que pareciera no estar.
 *
 * Se limpia el ancho DE VERDAD, el que devuelve bar_w(). Y el boton se
 * repinta detras, que no cuesta nada y cubre el caso de que alguna vez se
 * solapen por un pixel de redondeo.
 */
static void solo_barra(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /*
     * Se rellena TODA la banda, no solo el ancho de la barra. El
     * rectangulo que se manda repintar llega hasta el final del tercer
     * boton, y el hueco de 10 px que hay entre los dos no lo pintaba
     * nadie: salia negro en cada actualizacion de la barra, que en HFDL
     * es constante.
     *
     * pinta_boton4() ya no se llama desde aqui: vive a partir del pixel
     * 430 y esta banda acaba en el 420, asi que se recortaba entero. No
     * era una red de seguridad, era una linea muerta.
     */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    pinta_barra(s, st);
    pinta_boton3(s, st);
}

/*
 * Lo ancha que es la banda que se repinta cuando cambia la barra. Esta
 * fuera de ui_digi_draw_barra() para que el banco pueda medirla sin
 * dibujar: ver ui_digi_barra_ventana_dbg() y sim/digi.c.
 */
static int16_t barra_ventana_w(const ui_digi_state_t *st)
{
    /*
     * LA VENTANA SOLO SE ESTIRA SI EL TERCER BOTON ESTA A LA DERECHA.
     *
     * Y se veia fatal: la barra de FT8 salia como una persiana de rayas
     * naranjas. Esto pedia repintar 336 (barra) + 10 + 104 (tercer boton)
     * = 450 pixeles EMPEZANDO EN EL 458, o sea hasta el 908 de una
     * pantalla de 800. El controlador no tiene columna 908: cada fila se
     * le derramaba 108 pixeles en la siguiente y eso es exactamente una
     * persiana.
     *
     * El estirón existe porque solo_barra() pinta tambien el tercer
     * boton, y en la disposicion de siempre el tercero va JUSTO A LA
     * DERECHA de la barra -ahi la ventana tiene que llegar hasta el o no
     * se veria-. En la disposicion de FT8 el tercero esta al otro lado,
     * pegado a "Borrar", y la barra es lo ultimo de la fila: no hay nada
     * a su derecha que cubrir, y el pinta_boton3() de solo_barra() cae
     * fuera de la banda y no pinta nada, que es lo correcto.
     *
     * Es el fallo de siempre con otra cara: UNA CUENTA QUE SE QUEDO
     * DESPUES DE QUE DESAPARECIERA SU MOTIVO. La disposicion nueva movio
     * el boton y nadie movio la suma.
     */
    int16_t w = bar_w(st);

    if (!st->barra_dcha && btn3_w(st)) {
        w = (int16_t)(w + UDG_BTN2_GAP + btn3_w(st));
    }
    return w;
}

void ui_digi_draw_barra(const ui_digi_state_t *st)
{
    if (!st->barra_on) { return; }
    gfx2_render(bar_x(st), btn_y(st), barra_ventana_w(st), UDG_BTN_H,
                solo_barra, (void *)st);
}

int16_t ui_digi_barra_ventana_w_dbg(const ui_digi_state_t *st)
{
    return barra_ventana_w(st);
}

static void cabecera(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    pinta_barra(s, st);
    pinta_boton(s, st);
    pinta_boton2(s, st);
    pinta_boton3(s, st);
    pinta_boton4(s, st);
    pinta_boton5(s, st);
    pinta_chip(s, st);
    /* Raya fina que separa la cabecera de los renglones: sin ella el primer
     * renglon parece parte de la fila de botones. */
    gfx2_hline(s, 0, (int16_t)(st->y + UDG_HDR_H - 1), GFX2_W, gfx2_rgb(PAL_LINE));
}

/*
 * LAS COLUMNAS DE FT8, y de donde salen los numeros.
 *
 * La pantalla tiene 800 px y una linea entera de FT8 escrita de corrido mide
 * 390: sobraba media pantalla. Medidos con gfx2_text_w() los campos mas
 * anchos que puede haber: la hora 41 px, la frecuencia 36 (cuatro digitos),
 * el dB 29 con signo, el mensaje hasta unos 260, y la distancia 45 (cinco
 * digitos). Con eso, repartir asi deja aire entre todos y llena el ancho:
 *
 *    8        Hora      05:17
 *  110        Hz        0263
 *  210        dB        +12
 *  290        DT        +0,3
 *  380        Mensaje   CQ DX DL5AN JN49
 * -660        km        3209   (negativo = pegado a la derecha)
 * -792        Pais      Alemania
 *
 * El primer reparto -8/110/200/290- cuadraba, pero dejaba 274 px de hueco
 * entre el mensaje y los kilometros: se cuadro la tabla y se movio el hueco
 * de sitio en vez de repartirlo. Estos numeros lo abren entre TODAS las
 * columnas, que es lo que se habia pedido ("algo mas espaciados").
 *
 * Si algun dia se anade un campo hay que volver a MEDIR, no estimar: este
 * reparto sale de los anchos de verdad de font_ui_14b.
 */
const int16_t ui_digi_cols_ft8[UI_DIGI_COLS_FT8_N] = { 8, 110, 210, 290, 380, -660, -792 };

/*
 * HFDL: quien es el avion, su vuelo, donde esta, que hizo y cuantas veces.
 *
 * CAMBIARON EL 25/09/2026, cuando el panel dejo de volcar bytes crudos y
 * paso a desempaquetar el PDU. Antes eran "Hora / CRC / Bytes / Mensaje" y
 * las tres primeras columnas no decian nada util: el CRC siempre pone "ok"
 * -las malas ya no se apuntan- y el numero de bytes es del paquete, no del
 * mensaje.
 *
 * Ahora la segunda columna es ancha porque lleva "SHANNON>aire", que es el
 * dato que de verdad ordena la lista, y la ultima se queda todo el resto
 * para el asunto y el texto que venga dentro.
 */
/*
 * SEIS COLUMNAS DESDE EL 25/09/2026: el modelo tiene la suya, entre OACI
 * y Vuelo, que es donde lo pidio el usuario.
 *
 * Los anchos NO estan puestos a ojo. Medidos con gfx2_text_w() y la misma
 * font_ui_14b con la que se dibujan:
 *
 *     "717C9C"                  56 px  ->  OACI      64
 *     "NOMA Government Nomad N" 218 px ->  Modelo   226
 *     "ABY259"                  57 px  ->  Vuelo     68
 *     "89,9S 179,9W"           103 px  ->  Posicion 118
 *     "> SAN FRANCISCO"        138 px  ->  el resto
 *     "999"                     27 px  ->  N, pegada a la derecha
 *
 * Los 226 del modelo NO salen del nombre mas largo que se me ocurra, sino
 * de MEDIR LOS 356 que tiene el fichero de verdad, uno a uno, con esta
 * misma fuente. El primer intento fueron 196 px, sacados del mas largo que
 * se me ocurrio a mi -"B77W Boeing 777-300ER", 185 px- y el fichero real
 * tenia dentro uno de 218: "NOMA Government Nomad N". Habria salido
 * cortado el dia que pasara un Nomad por encima, o sea nunca hasta que
 * pasara.
 */
const int16_t ui_digi_cols_hfdl[UI_DIGI_COLS_HFDL_N] = { 8, 72, 298, 366, 484, -792 };

/*
 * WSPR: quien es, desde donde, con cuanta potencia y como de bien cuadro.
 *
 * Seis campos, y todos CORTOS - lo mas largo de la tabla son 95 px y lo
 * de HFDL eran 226-, asi que aqui el problema no es que quepa sino que no
 * se amontone todo a la izquierda dejando media pantalla vacia. De ahi
 * los huecos anchos.
 *
 * Medidos con gfx2_text_w() y la misma font_ui_14b con la que se dibujan,
 * cogiendo el mayor entre el dato y su titulo:
 *
 *     "23:58"  41 px  /  "Hora"        36  ->  Hora        102
 *     "MM0WWWW" 95 px /  "Indicativo"  76  ->  Indicativo  150
 *     "RR99"   38 px  /  "Loc."        31  ->  Loc.        120
 *     "60"     18 px  /  "dBm"         34  ->  dBm         120
 *     "-150,0" 47 px  /  "Desvío"      51  ->  Desvío   el resto
 *     "100"    27 px  /  "Cal."        29  ->  Cal., pegada a la derecha
 *
 * EL TITULO DEL DESVIO NO LLEVA DELTA, y no por gusto: "ΔHz" mide 24 px y
 * "Hz" mide 19, o sea que la delta ocupa CINCO - no esta en la fuente y
 * lo que se dibuja no es una delta. Medido, no supuesto. Por eso pone
 * "Desvío", que ademas se entiende sin saber que significa el simbolo.
 *
 * El indicativo se dimensiona a SIETE caracteres anchos ("MM0WWWW") y no
 * a los seis de un indicativo normal: wspr_msg_t guarda ocho bytes y los
 * mensajes de tipo 2 llevan prefijo o sufijo. Son 95 px de los 800 que
 * hay; reservarlos es gratis y el dia que pase un "F/EA4XYZ" no sale
 * cortado.
 */
const int16_t ui_digi_cols_wspr[UI_DIGI_COLS_WSPR_N] = { 8, 110, 260, 380, 500, -792 };

/*
 * JTTY: hora, desvio, calidad, el mensaje y el pais.
 *
 * EL PAIS ENTRO EL 28/09 POR LA TARDE. Pedido por el usuario.
 *
 * Las tres primeras siguen estrechas a proposito: lo que
 * se lee aqui es el MENSAJE, y en JTTY un mensaje son varios atomos
 * pegados -"CQ K1ABC CQ FN42 599 123"- que se van largos enseguida. Las
 * otras tres son para afinar la sintonia y para saber cuanto fiarse, y se
 * miran de reojo.
 *
 * SE ESTRECHARON el 28/09 por la tarde, al entrar el pais. Pedido por el usuario.
 * Y sobraba sitio de verdad: la hora tenia 92 px para un contenido de 41 y
 * la calidad 60 para uno de 29.
 *
 * Anchos medidos con gfx2_text_w() y font_ui_14b, no estimados. La columna
 * es lo que mide el contenido mas ancho o el titulo -lo que mande de los
 * dos- mas 13 px de aire:
 *
 *                 contenido          titulo      manda   columna
 *     Hora        "23:59"      41    "Hora"   36    41     8..62
 *     Desvío      "-437 Hz"    57    "Desvío" 51    57    62..132
 *     Cal.        "100"        27    "Cal."   29    29   132..176
 *     Mensaje                                           176..672
 *     País        "Nueva Zel." 115   "País"   32   115   672..800
 *
 * El mensaje se queda con 496 px de los 560 que tenia antes del pais: de
 * los 140 que costaba la columna nueva, 64 los devuelven la hora y la
 * calidad y 12 mas el pais, que con 128 px sigue teniendo 13 de aire sobre
 * su peor caso.
 *
 * 672 y no pegado a la derecha como en FT8, a proposito. Alli el pais va
 * detras de los kilometros y el recorte del mensaje lo marca ESE, con la
 * reserva de UDG_COL_RESERVA -60 px, que a un numero de cinco cifras le
 * sobra-. Aqui el pais va justo detras del mensaje y 60 px no le llegan.
 * Con la columna fija, el mensaje se recorta donde tiene que recortarse.
 */
const int16_t ui_digi_cols_jtty[UI_DIGI_COLS_JTTY_N] = { 8, 62, 132, 176, 672 };

/* Ver el comentario de UI_DIGI_COLS_IDENT_N en ui_digi.h. */
const int16_t ui_digi_cols_ident[UI_DIGI_COLS_IDENT_N] = { 8, 120, 680 };

/*
 * AIS: quien es el barco, como se llama, donde esta y a que va.
 *
 * Al reves que en WSPR, aqui el problema SI es que quepa: el nombre son
 * veinte caracteres y la posicion lleva dos coordenadas con su letra. Los
 * anchos, medidos con gfx2_text_w() y la misma font_ui_14b:
 *
 *     "224123456"             81 px  /  "MMSI"      41  ->  MMSI      102
 *     veinte mayusculas      300 px  /  "Nombre"    60  ->  Nombre    310
 *     "43,371N 179,999W"     140 px  /  "Posición"  64  ->  Posicion  160
 *     "102,3"                 41 px  /  "Vel"       24  ->  Vel        70
 *     "359o"                  36 px  /  "Rumbo"     53  ->  Rumbo   el resto
 *     "9999"                  36 px  /  "N"         11  ->  N, a la derecha
 *
 * LOS 300 PX DEL NOMBRE son veinte "W" seguidas, o sea el peor caso
 * ABSOLUTO y no uno plausible: un nombre de barco normal ("MAERSK ESSEX")
 * mide 115. Reservar el peor absoluto se puede permitir porque los otros
 * cinco campos son cortos y sobra sitio; en la tabla de HFDL, donde no
 * sobraba, hubo que ir a medir los 356 nombres de verdad del fichero.
 * Cuando sobra, se reserva de mas y no se piensa mas en ello.
 *
 * LA VELOCIDAD Y EL RUMBO VAN SIN UNIDADES en el renglon: los nudos y los
 * grados estan en el titulo, y repetirlos catorce veces es ruido. Es la
 * misma decision que en la tabla de FT8.
 */
const int16_t ui_digi_cols_ais[UI_DIGI_COLS_AIS_N] = { 8, 110, 420, 580, 650, -792 };

/*
 * ALE: quien se ha oido, con que tipo de palabra, donde y como de bien.
 *
 * Cinco columnas. Medidos con gfx2_text_w() y font_ui_14b:
 *
 *     quince caracteres anchos 225 px / "Indicativo" 76 -> Indicativo 272
 *     "TWAS"                    44 px / "Tipo"       33 -> Tipo       120
 *     "14.109"                  50 px / "kHz"        28 -> kHz        120
 *     "48"                      18 px / "Cal."       29 -> Cal.    el resto
 *     "9999"                    36 px / "N"          11 -> N, a la derecha
 *
 * LOS QUINCE CARACTERES no son un tope inventado: es el maximo que admite
 * una direccion de ALE, cinco palabras de tres. Y son A-Z y 0-9, sin
 * minusculas, asi que quince "W" es el peor caso de verdad y no una
 * exageracion como en AIS.
 *
 * LA CALIDAD son los votos unanimes de la votacion 2 de 3, de 0 a 48. No es
 * un adorno: 48 quiere decir que las tres copias de los 48 bits llegaron
 * identicas, y por debajo de 30 el indicativo empieza a ser dudoso aunque
 * el Golay haya cuadrado. Es el unico numero que dice cuanto fiarse de un
 * renglon.
 */
const int16_t ui_digi_cols_ale[UI_DIGI_COLS_ALE_N] = { 8, 280, 400, 520, -792 };

/*
 * Hasta donde puede llegar el campo k sin pisar al siguiente.
 *
 * La RESERVA es para cuando el siguiente va pegado a la derecha: de ese solo
 * se sabe donde TERMINA, no donde empieza, asi que se le guarda sitio para lo
 * mas ancho que puede ser. 60 px con la tipografia de este panel son cinco
 * digitos y sobra - los kilometros mas largos, 99999, miden 45.
 */
#define UDG_COL_RESERVA 60

static int16_t limite(const ui_digi_state_t *st, uint8_t k)
{
    if ((uint8_t)(k + 1U) >= st->ncols) { return (int16_t)GFX2_W; }
    if (st->cols[k + 1U] >= 0) { return st->cols[k + 1U]; }
    return (int16_t)(-st->cols[k + 1U] - UDG_COL_RESERVA);
}

/* Pinta un renglon con campos separados por tabulador, cada uno en su
 * columna. Sin columnas, el renglon entero desde el margen. */
static void renglon(gfx2_surf_t *s, const ui_digi_state_t *st,
                    const char *txt, int16_t y, gfx2_rgba_t tinta)
{
    uint8_t k = 0U;
    const char *p = txt;

    if (!st->cols || st->ncols == 0U) {
        gfx2_text(s, UDG_PAD_X, y, txt, &font_ui_14b, tinta);
        return;
    }
    /*
     * UN RENGLON QUE CRUZA TODAS LAS COLUMNAS - 14/10/2026.
     *
     * Una tabla con columnas recorta cada campo a la suya, que es justo lo
     * que hay que hacer con datos de fuera (ver el comentario de abajo).
     * Pero un volcado en hexadecimal no es un campo: son setenta y dos
     * caracteres que no caben en ninguna columna y que no quieren caber,
     * porque lo que dicen es una tira de bytes y partirla la estropea.
     *
     * Asi que un renglon que EMPIEZA POR 0x01 se pinta entero desde el
     * margen, sin columnas. El 0x01 no es texto imprimible, asi que no
     * puede chocar con ninguna fila de verdad ni con nada que llegue del
     * aire: una tabla que no lo use no nota que esto existe.
     */
    if (txt[0] == '\x01') {
        gfx2_text(s, UDG_PAD_X, y, &txt[1], &font_ui_14b, tinta);
        return;
    }

    while (*p != '\0' && k < st->ncols) {
        char campo[40];
        uint8_t n = 0U;
        int16_t x = st->cols[k];

        while (*p != '\0' && *p != '\t' && n < (uint8_t)(sizeof campo - 1U)) {
            campo[n++] = *p++;
        }
        campo[n] = '\0';
        while (*p != '\0' && *p != '\t') { p++; }   /* lo que no cupo */
        if (*p == '\t') { p++; }

        if (n > 0U) {
            if (x < 0) {
                /* Pegado a la derecha: la x negativa dice donde TERMINA. */
                int16_t ancho = gfx2_text_w(campo, &font_ui_14b);
                gfx2_text(s, (int16_t)(-x - ancho), y, campo, &font_ui_14b, tinta);
            } else {
                /*
                 * RECORTADO A SU COLUMNA. 25/09/2026.
                 *
                 * Sin esto, el ancho de la tabla dependeria de lo largo que
                 * sea un mensaje, que es algo que decide la estacion de
                 * enfrente. Un mensaje de los largos que permite el protocolo
                 * -35 caracteres- se comeria la columna siguiente, y eso es
                 * un fallo que solo aparece cuando aparece: no se ve
                 * probando con los mensajes normales.
                 *
                 * Asi que la columna manda sobre el contenido, no al reves.
                 */
                /*
                 * EN UNA PASADA, NO QUITANDO LETRAS DE UNA EN UNA.
                 * 13/10/2026.
                 *
                 * Aqui habia esto:
                 *
                 *   while (n > 0 && x + gfx2_text_w(campo, f) > lim) {
                 *       campo[--n] = '\0';
                 *   }
                 *
                 * o sea: quita UNA letra y vuelve a medir la cadena ENTERA
                 * desde el principio. Y medir la cadena entera es
                 * recorrerla letra a letra buscando cada glifo en los 22
                 * rangos de la fuente. Es cuadratico, y encima esto vive
                 * dentro del dibujo, que corre una vez por BANDA: el panel
                 * de FT8 mide 318 px, o sea veinte.
                 *
                 * Medido en un repintado del panel con mensajes de los que
                 * SI hay que recortar: 58.420 letras medidas. Con mensajes
                 * cortos, que no se recortan, 5.580. O sea que el recorte
                 * costaba diez veces lo que costaba pintar.
                 *
                 * gfx2_text_cabe() lo hace en una pasada hacia delante,
                 * sumando el avance de cada glifo hasta pasarse.
                 */
                int16_t lim = limite(st, k);
                uint8_t cabe = gfx2_text_cabe(campo, &font_ui_14b,
                                              (int16_t)(lim - x), 0);

                if (cabe < n) { n = cabe; campo[n] = '\0'; }
                if (n > 0U) {
                    gfx2_text(s, x, y, campo, &font_ui_14b, tinta);
                }
            }
        }
        k++;
    }
}

/* Cuantos pixeles se come el renglon de titulos. 0 si no hay. */
static int16_t alto_titulos(const ui_digi_state_t *st)
{
    return st->titulos ? (int16_t)UDG_LINE_H : 0;
}

/*
 * Cual es el renglon "vivo": el ULTIMO QUE TIENE ALGO, no el ultimo de la
 * tabla. Esta en una funcion porque lo necesitan los DOS caminos -el
 * repintado entero y el de una sola fila- y antes cada uno lo calculaba a
 * su manera: texto() miraba el ultimo con contenido y una_fila() miraba
 * "i+1 == nfilas". Con el panel a medio llenar no coincidian, asi que el
 * renglon que se acababa de escribir se apagaba en cuanto llegaba el
 * siguiente caracter y volvia a encenderse al repintar el panel entero.
 */
static uint8_t fila_viva(const ui_digi_state_t *st)
{
    uint8_t i, n = nfilas(st), ultimo = 0xFFU;

    for (i = 0U; i < n; i++) {
        if (st->fila[i] && st->fila[i][0] != '\0') { ultimo = i; }
    }
    return ultimo;
}

/*
 * EL FONDO DE UNA FILA, con cebra o sin ella.
 *
 * Ademas de pintar el color que toque, esto es lo que BORRA la banda: ver
 * gfx2_render(), que la entrega en negro y vuelca el rectangulo entero.
 * Lo que no se pinte sale negro en pantalla, y en los temas claros eso se
 * ve como una raya.
 */
static void fondo_fila(gfx2_surf_t *s, const ui_digi_state_t *st,
                       uint8_t i, int16_t y)
{
    uint32_t c = (st->cebra && ((i & 1U) != 0U)) ? PAL_SURF_1 : PAL_SURF_0;

    gfx2_fill(s, 0, y, GFX2_W, UDG_LINE_H, gfx2_rgb(c));
}

static gfx2_rgba_t tinta_fila(const ui_digi_state_t *st, uint8_t i,
                              uint8_t ultimo, gfx2_rgba_t viva, gfx2_rgba_t vieja)
{
    if (st->fila_tinta[i] == UDG_T_LLAMA) {
        return gfx2_rgb(PAL_ACCENT);
    }
    return (i == ultimo) ? viva : vieja;
}

static void texto(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_rgba_t tinta = gfx2_rgb(PAL_INK);
    gfx2_rgba_t viejo = gfx2_rgb(PAL_INK_MUTE);
    uint8_t i;

    gfx2_fill(s, 0, st->y, GFX2_W, st->h, gfx2_rgb(PAL_SURF_0));
    cabecera(s, st);

    /*
     * Cual es el renglon "vivo": el ULTIMO QUE TIENE ALGO, no el octavo.
     *
     * Antes era el octavo a secas, y eso solo acertaba cuando el panel ya
     * estaba lleno. Recien abierto, con dos o tres renglones escritos, no se
     * destacaba ninguno y los tres pesaban igual. Y desde el 23/09/2026 hay
     * un segundo inquilino -el panel de RDS, que usa dos renglones de los
     * ocho- donde no se destacaba nunca nada.
     */
    if (st->titulos) {
        /* Los titulos, en gris y con una raya fina debajo: son una etiqueta,
         * no un dato, y tienen que pesar menos que los renglones. */
        renglon(s, st, st->titulos, (int16_t)(st->y + UDG_HDR_H),
                gfx2_rgb(PAL_INK_MUTE));
        gfx2_hline(s, 0, (int16_t)(st->y + UDG_HDR_H + UDG_LINE_H - 2),
                   GFX2_W, gfx2_rgb(PAL_LINE));
    }

    {
        uint8_t n = nfilas(st);
        uint8_t ultimo = fila_viva(st);
        /*
         * HASTA DONDE LLEGA EL PANEL - 30/09/2026.
         *
         * nfilas() topa en UDG_ROWS_MAX, que son 24, y eso no tiene nada que
         * ver con cuantas CABEN: con la altura de siempre (174 px, cabecera
         * de 30 y renglones de 18) caben ocho. Un inquilino que pidiera 24
         * -el campo `filas` es suyo y nadie lo comprobaba- pintaba doce
         * renglones por DEBAJO del panel, encima de la barra de acciones, y
         * gfx2 no se queja porque el recorte es a la PANTALLA, no a la caja
         * de este panel.
         *
         * No ha pasado todavia: hoy el que mas pide son los ocho de siempre
         * y el de RDS pide dos. Pero es un fallo que no avisa -se pinta y ya
         * esta- y se arregla con una resta, asi que se pone la resta. La
         * misma guarda va en ui_digi_draw_fila(), que pinta por indice.
         */
        int16_t y_tope = (int16_t)(st->y + st->h);
        for (i = 0U; i < n; i++) {
            int16_t y = (int16_t)(st->y + UDG_HDR_H + alto_titulos(st)
                                  + i * UDG_LINE_H);
            if ((int16_t)(y + UDG_LINE_H) > y_tope) { break; }
            fondo_fila(s, st, i, y);
            if (!st->fila[i] || st->fila[i][0] == '\0') { continue; }
            /* Los renglones viejos, mas apagados. Lo que acaba de llegar
             * esta abajo del todo y es lo que se esta leyendo; lo de arriba
             * es contexto. Sin esta diferencia todos pesan igual y hay que
             * buscar con la vista cual es el ultimo. */
            renglon(s, st, st->fila[i], y, tinta_fila(st, i, ultimo, tinta, viejo));
        }
    }
}

static void solo_cabecera(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /* El fondo de SU franja nada mas: lo de abajo lo pinta quien se queda
     * con el cuerpo. gfx2 entrega la banda en negro, asi que lo que no se
     * pinte sale negro - ver fondo_fila(). */
    gfx2_fill(s, 0, st->y, GFX2_W, UDG_HDR_H, gfx2_rgb(PAL_SURF_0));
    cabecera(s, st);
}

/* El panel entero en una banda que trae otro, para la captura de pantalla.
 * La misma funcion de dibujo que usa ui_digi_draw_texto(), que es la que
 * pinta el panel completo -cabecera, filas, barra y chapas-. */
/* Recortado a su hueco, igual que lo recorta su ventana en
 * ui_digi_draw_texto(). Ver spec_chrome_pinta_en() para por que hace falta. */
void ui_digi_pinta_en(gfx2_surf_t *s, const ui_digi_state_t *st)
{
    gfx2_surf_t sub = gfx2_sub_y(s, st->y, st->h);
    if (sub.h > 0) { texto(&sub, (void *)st); }
}

uint8_t ui_digi_draw_texto(const ui_digi_state_t *st)
{
    /*
     * DEVUELVE SI HA PINTADO - 13/10/2026, por el boton "Borrar" del panel
     * de CW que desaparecia despues de mucho rato.
     *
     * Este es EL repintado de una sola vez: quien lo llama lo hace porque
     * tiene una bandera de "hay que repintar el panel entero" y la baja
     * justo despues. Si la banda compositora estaba cogida -la captura de
     * pantalla se la queda veinticinco segundos- gfx2_render() se da la
     * vuelta sin dibujar, la bandera se baja igual, y la cabecera con su
     * boton se queda con lo que hubiera debajo hasta el siguiente cambio
     * de modo. Los repintados por renglon no sufren esto porque vuelven a
     * pasar; este no vuelve a pasar.
     */
    return gfx2_render(0, st->y, GFX2_W, st->h, texto, (void *)st);
}

/*
 * SOLO LA CABECERA, para quien se queda con el cuerpo - 30/09/2026.
 *
 * Con el mapa puesto, main.c pintaba el panel ENTERO con
 * ui_digi_draw_texto() -renglones vacios incluidos- y acto seguido el mapa
 * repintaba encima todo el cuerpo. El resultado eran 484.800 pixeles
 * enviados al panel para 254.400 que hacian falta: el 47,5 % de lo que se
 * manda por el bus en cada cuadro del mapa era trabajo tirado, y eso se
 * nota en el refresco justo donde mas duele, porque el mapa se repinta
 * entero cada vez que se arrastra un dedo o se toca el zoom.
 *
 * No es un fallo que se vea: sale lo correcto. Solo va mas despacio.
 */
void ui_digi_draw_cabecera(const ui_digi_state_t *st)
{
    gfx2_render(0, st->y, GFX2_W, UDG_HDR_H, solo_cabecera, (void *)st);
}

/*
 * UN solo renglon.
 *
 * Es lo que permite que llegar un caracter no cueste repintar el panel
 * entero: al teclear el decodificador solo cambia el renglon de abajo. La
 * version anterior afinaba mas -dibujaba UN glifo en su hueco- porque con
 * ancho fijo sabia exactamente donde caia; con tipografia proporcional no se
 * sabe sin medir la linea entera, asi que se redibuja la linea. Son 800x18
 * px por caracter, y a 20 palabras por minuto eso son diez caracteres por
 * segundo: nada al lado del espectro, que repinta 800x208 treinta veces.
 */
static int8_t s_fila = 0;

static void una_fila(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    uint8_t i = (uint8_t)s_fila;

    int16_t y = (int16_t)(st->y + UDG_HDR_H + alto_titulos(st) + i * UDG_LINE_H);

    fondo_fila(s, st, i, y);
    if (st->fila[i] && st->fila[i][0] != '\0') {
        renglon(s, st, st->fila[i], y,
                tinta_fila(st, i, fila_viva(st),
                           gfx2_rgb(PAL_INK), gfx2_rgb(PAL_INK_MUTE)));
    }
}

static uint8_t fila_pinta(const ui_digi_state_t *st, uint8_t i)
{
    int16_t y;

    if (i >= nfilas(st)) { return 0U; }
    y = (int16_t)(st->y + UDG_HDR_H + alto_titulos(st) + i * UDG_LINE_H);
    /* Y que ese renglon quepa en el panel: ver la guarda del bucle de
     * texto(). Aqui importa lo mismo y por lo mismo. */
    if ((int16_t)(y + UDG_LINE_H) > (int16_t)(st->y + st->h)) { return 0U; }
    s_fila = (int8_t)i;
    return gfx2_render(0, y, GFX2_W, UDG_LINE_H, una_fila, (void *)st);
}

void ui_digi_draw_fila(const ui_digi_state_t *st, uint8_t i)
{
    (void)fila_pinta(st, i);
}

/*
 * ======================================================================
 * SOLO LOS RENGLONES QUE HAN CAMBIADO - 19/10/2026
 * ======================================================================
 *
 * Pedido por el usuario: que la radio vaya mas rapido.
 *
 * ui_digi_draw_texto() repinta los 800 x 317 enteros. Medido con
 * sim/lcd_sim.c, que cuenta los accesos de verdad al bus del panel:
 *
 *     panel entero (800x317) .....  253.940 accesos   11,43 ms a 45 ns
 *     UN renglon  (800x18) .......   14.434 accesos    0,65 ms
 *     los OCHO renglones sueltos .  115.472 accesos    5,20 ms
 *
 * Mirese por donde se mire, eso esta mal. Repintar los ocho renglones UNO
 * POR UNO cuesta menos de la mitad que el repintado "entero", y la razon
 * es que el panel mide 317 px mientras que la cabecera, los titulos y los
 * ocho renglones ocupan 192: hay 125 px de FONDO VACIO -cien mil accesos
 * al bus, cuatro milisegundos y medio- que se repintan enteros cada vez
 * para no dibujar nada.
 *
 * Y encima casi nunca cambian los ocho. En el identificador la lista son
 * candidatos con su tanto por ciento: cambia uno, a veces dos.
 *
 * COMO SE DECIDE QUE HA CAMBIADO. Con una huella FNV-1a de 32 bits por
 * renglon, sobre el TEXTO y no sobre el puntero: los inquilinos de este
 * panel rehacen sus cadenas en el mismo buffer, asi que el puntero no se
 * mueve aunque el texto sea otro. La huella lleva ademas la tinta del
 * renglon y si es el renglon "vivo" -el ultimo con algo-, que es lo que
 * decide su color: cuando llega una linea nueva cambian DOS, la que deja
 * de ser la viva y la que lo pasa a ser, y las dos tienen que repintarse.
 *
 * Y UNA HUELLA DEL MARCO, aparte, con todo lo que no es el cuerpo de un
 * renglon: la geometria, los titulos, la chapa, los cinco botones, la
 * barra, la cebra, las columnas, cuantas filas hay... y el puntero de la
 * paleta, que es lo que cambia al cambiar de tema. Si el marco cambia se
 * repinta TODO, sin pensar. Asi cualquier cosa que no se me haya ocurrido
 * cae del lado seguro: el que repinta de mas, no el que deja basura.
 *
 * LO QUE NO SE APUNTA SI NO SE HA PINTADO. gfx2_render() devuelve 0
 * cuando la banda compositora esta cogida -la captura de pantalla se la
 * queda veinticinco segundos-. Si en ese caso se apuntara la huella
 * nueva, el renglon se quedaria con el texto viejo en pantalla y nadie
 * volveria a repintarlo nunca. Se apunta solo lo que de verdad se pinto.
 *
 * ui_digi_olvida() tira todo lo apuntado: el siguiente repintado sera
 * entero. Lo necesita quien sabe que los pixeles de debajo ya no son los
 * nuestros - el mapa encima del panel, un menu que se cierra, una foto.
 */
static uint32_t s_fir_fila[UDG_ROWS_MAX];
static uint32_t s_fir_caja;
static uint32_t s_fir_chapa;
static uint32_t s_fir_btn[3];  /* el primero, el segundo y el tercero */
static uint8_t  s_fir_hay;     /* 0 = lo de la pantalla no es nuestro */

#define FNV_INI  2166136261UL

static uint32_t fnv_b(uint32_t h, uint32_t v)
{
    h ^= (v & 0xFFUL);  h *= 16777619UL;
    h ^= ((v >> 8) & 0xFFUL);  h *= 16777619UL;
    h ^= ((v >> 16) & 0xFFUL); h *= 16777619UL;
    h ^= ((v >> 24) & 0xFFUL); h *= 16777619UL;
    return h;
}

static uint32_t fnv_t(uint32_t h, const char *t)
{
    if (t == 0) { return fnv_b(h, 0xA5A5A5A5UL); }
    while (*t != '\0') {
        h ^= (uint32_t)(uint8_t)*t++;
        h *= 16777619UL;
    }
    return fnv_b(h, 0x5A5A5A5AUL);   /* marca de fin: "ab"+"c" != "a"+"bc" */
}

/*
 * LA CAJA: lo que obliga a repintarlo TODO si cambia.
 *
 * Geometria, titulos, cuantas filas, la cebra, las columnas y la paleta.
 * Y la barra entera, que no se separa: el segundo boton y la barra
 * comparten el mismo hueco (UDG_BAR_X es UDG_BTN2_X) y el tercer boton se
 * coloca detras de donde acabe la barra, asi que mover la barra mueve a
 * los otros dos. Eso ya ha salido mal cinco veces en esta cabecera; aqui
 * se corta por lo sano.
 *
 * Lo que NO esta aqui -la chapa y los cinco botones- tiene cada uno su
 * propio repintado y su propia huella, mas abajo.
 */
static uint32_t firma_caja(const ui_digi_state_t *st)
{
    uint32_t h = FNV_INI;

    h = fnv_b(h, (uint32_t)(uint16_t)st->y);
    h = fnv_b(h, (uint32_t)(uint16_t)st->h);
    h = fnv_b(h, (uint32_t)nfilas(st));
    h = fnv_t(h, st->titulos);
    h = fnv_b(h, (uint32_t)st->barra_dcha);
    h = fnv_b(h, (uint32_t)st->barra_on);
    h = fnv_b(h, (uint32_t)(uint16_t)st->barra_w);
    h = fnv_b(h, (uint32_t)st->barra_v);
    h = fnv_b(h, (uint32_t)st->barra_max);
    h = fnv_t(h, st->barra_txt);
    h = fnv_t(h, st->barra_txt_dcha);
    h = fnv_b(h, (uint32_t)st->cebra);
    h = fnv_b(h, (uint32_t)st->ncols);
    /* El puntero de las columnas y el de la paleta: los dos se cambian
     * enteros, no se modifican por dentro (ver tema_aplicar() en main.c). */
    h = fnv_b(h, (uint32_t)(uintptr_t)st->cols);
    h = fnv_b(h, (uint32_t)(uintptr_t)g_pal);
    return h;
}

/*
 * LA FRANJA DE LA CHAPA, que no es solo la chapa.
 *
 * solo_chip() repinta la franja entera y vuelve a dibujar DENTRO el cuarto
 * y el quinto boton, porque viven ahi. Asi que la huella lleva los tres:
 * si cambia cualquiera de ellos, una sola llamada los deja bien. Y el
 * ancho del cuarto se mide contra donde empieza la chapa, o sea que el
 * texto de la chapa tambien lo mueve - otra razon para que vayan juntos.
 */
static uint32_t firma_chapa(const ui_digi_state_t *st)
{
    uint32_t h = FNV_INI;

    h = fnv_t(h, st->chip);
    h = fnv_b(h, (uint32_t)st->enganchado);
    h = fnv_t(h, st->btn4);  h = fnv_b(h, (uint32_t)st->btn4_press);
    h = fnv_t(h, st->btn5);  h = fnv_b(h, (uint32_t)st->btn5_press);
    return h;
}

static uint32_t firma_rotulo(const char *t, uint8_t press)
{
    return fnv_b(fnv_t(FNV_INI, t), (uint32_t)press);
}

static uint32_t firma_fila(const ui_digi_state_t *st, uint8_t i, uint8_t viva)
{
    uint32_t h = FNV_INI;

    h = fnv_b(h, (uint32_t)i);
    h = fnv_t(h, st->fila[i]);
    h = fnv_b(h, (uint32_t)st->fila_tinta[i]);
    h = fnv_b(h, (uint32_t)(i == viva));
    return h;
}

void ui_digi_olvida(void)
{
    s_fir_hay = 0U;
}

uint8_t ui_digi_draw_cambios(const ui_digi_state_t *st)
{
    uint32_t caja = firma_caja(st);
    uint8_t  n = nfilas(st);
    uint8_t  viva = fila_viva(st);
    uint8_t  i;

    if (!s_fir_hay || caja != s_fir_caja) {
        if (!ui_digi_draw_texto(st)) { return 0U; }
        s_fir_caja  = caja;
        s_fir_chapa = firma_chapa(st);
        s_fir_btn[0] = firma_rotulo(st->btn,  st->btn_press);
        s_fir_btn[1] = firma_rotulo(st->btn2, st->btn2_press);
        s_fir_btn[2] = firma_rotulo(st->btn3, st->btn3_press);
        for (i = 0U; i < (uint8_t)UDG_ROWS_MAX; i++) {
            s_fir_fila[i] = (i < n) ? firma_fila(st, i, viva) : 0U;
        }
        s_fir_hay = 1U;
        return 1U;
    }

    /*
     * LA CABECERA PRIMERO Y LOS RENGLONES DESPUES, que es el orden en que
     * los pinta texto(). No se tocan -viven en franjas distintas- pero el
     * orden se respeta para que lo parcial y lo entero sean la misma
     * pelicula y no solo la misma foto.
     */
    {
        uint32_t f = firma_chapa(st);
        if (f != s_fir_chapa) { ui_digi_draw_chip(st); s_fir_chapa = f; }
    }
    {
        uint32_t f = firma_rotulo(st->btn, st->btn_press);
        if (f != s_fir_btn[0]) { ui_digi_draw_boton(st);  s_fir_btn[0] = f; }
    }
    {
        uint32_t f = firma_rotulo(st->btn2, st->btn2_press);
        if (f != s_fir_btn[1]) { ui_digi_draw_boton2(st); s_fir_btn[1] = f; }
    }
    {
        uint32_t f = firma_rotulo(st->btn3, st->btn3_press);
        if (f != s_fir_btn[2]) { ui_digi_draw_boton3(st); s_fir_btn[2] = f; }
    }

    for (i = 0U; i < n; i++) {
        uint32_t f = firma_fila(st, i, viva);

        if (f == s_fir_fila[i]) { continue; }
        if (!fila_pinta(st, i)) { continue; }   /* no se apunta lo no pintado */
        s_fir_fila[i] = f;
    }
    return 1U;
}

/* Repintados sueltos. Los dos caen FUERA del trazo, asi que repintarlos no
 * pelea con spectrum_draw() y no parpadean. */
/*
 * LA FRANJA QUE SE LIMPIA AL REPINTAR LA CHAPA - 24/09/2026
 *
 * Los botones no, porque viven en la cabecera y la imagen empieza 8 px por
 * debajo. Pero la CHAPA si hacia algo raro, y desde antes de que hubiera
 * dos botones.
 *
 * La chapa va pegada al borde derecho y su ancho depende del texto, asi
 * que al acortarse el texto la chapa encoge Y SE MUEVE a la derecha. Se
 * repintaba solo su propio rectangulo, o sea que lo que quedaba a su
 * izquierda no lo borraba nadie: al aparecer la señal, "Esperando una
 * imagen" (211 px) pasa a "Martin 1 - 001/256" (173) y dejaba 38 px de
 * rectangulo redondeado colgados en mitad de la barra. Viniendo de "Modo
 * no soportado, VIS 123" eran 75.
 *
 * Se arregla limpiando una franja FIJA -desde donde acaba el segundo boton
 * hasta el borde- en vez de la que ocupe la chapa de turno. Cuesta blitear
 * unos 13.000 pixeles dos veces por segundo, que en este panel no se nota,
 * y a cambio da igual cuanto crezca o encoja el texto: la franja no
 * depende de el.
 */
/* Donde empieza la franja de la chapa. Desde el 25/09/2026 no cuelga del
 * segundo boton sino del final de la barra (ver UDG_BAR_W): los dos no
 * coinciden nunca -el que tiene barra no tiene botones- y la barra necesitaba
 * el sitio. Sigue sobrando de largo para la chapa mas ancha que hay,
 * "Esperando una imagen", que mide 211 px de los 354 que quedan. */
#define UDG_CHIP_X0  (UDG_BAR_X + UDG_BAR_W + UDG_BTN2_GAP)
#define UDG_CHIP_W   (GFX2_W - UDG_PAD_X - UDG_CHIP_X0)
_Static_assert(UDG_CHIP_X0 == UDG_CHIP_X0_F,
               "UDG_CHIP_X0 y la copia que usa el cuarto boton se han separado");

/* Donde empieza y cuanto mide la franja que se limpia. Es FIJA para una
 * cabecera dada -no depende del texto de la chapa, que es de lo que
 * avisaba el comentario de arriba- pero no es la misma para todas: cuando
 * la chapa esta sola empieza antes, porque tambien acaba antes a la
 * izquierda lo ultimo que hay dibujado. Ver chip_sola(). */
static int16_t chip_franja_x(const ui_digi_state_t *st)
{
    return chip_sola(st) ? (int16_t)UDG_CHIP_ANCHO_X0 : (int16_t)UDG_CHIP_X0;
}
static int16_t chip_franja_w(const ui_digi_state_t *st)
{
    return chip_sola(st) ? (int16_t)UDG_CHIP_ANCHO_W : (int16_t)UDG_CHIP_W;
}

static void solo_chip(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_fill(s, chip_franja_x(st), btn_y(st), chip_franja_w(st), UDG_BTN_H,
              gfx2_rgb(PAL_SURF_0));
    /*
     * Y LOS BOTONES QUE VIVEN DENTRO DE ESTA FRANJA, detras: el relleno de
     * arriba se los acaba de llevar.
     *
     * *** TERCERA VEZ. El tercero con solo_barra(), y el boton de
     * frecuencias desaparecido. El cuarto, aqui mismo. Y el 28/09/2026 el
     * quinto: el boton existia pero no se veia. Respondia al dedo
     * -la zona de toque estaba bien-; lo que no estaba era el dibujo.
     *
     * En FT8 la chapa es el contador de los quince segundos, o sea que
     * esto corre una vez por segundo: el boton se pintaba y se borraba
     * antes de que diera tiempo a verlo.
     *
     * La forma de que no haya una cuarta es que el banco lo mida, y ahora
     * lo mide: ver sim/digi.c, "sobrevive al repintado de la chapa".
     */
    pinta_boton4(s, st);
    pinta_boton5(s, st);
    pinta_chip(s, st);
}

void ui_digi_draw_chip(const ui_digi_state_t *st)
{
    if (!st->chip) { return; }
    gfx2_render(chip_franja_x(st), btn_y(st), chip_franja_w(st), UDG_BTN_H,
                solo_chip, (void *)st);
}

static void solo_boton(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;
    gfx2_fill(s, UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton(s, st);
}

void ui_digi_draw_boton(const ui_digi_state_t *st)
{
    if (!st->btn) { return; }
    gfx2_render(UDG_BTN_X, btn_y(st), UDG_BTN_W, UDG_BTN_H, solo_boton, (void *)st);
}

static void solo_boton3(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /* La banda llega en negro y se vuelca entera - ver gfx2_render(). El
     * boton es un rectangulo REDONDEADO, asi que sin este relleno las
     * cuatro esquinas se quedan sin pintar y salen negras. En el tema
     * oscuro no se nota; en el naranja y el oliva, si. */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton3(s, st);
}

void ui_digi_draw_boton3(const ui_digi_state_t *st)
{
    int16_t w = btn3_w(st);

    if (!st->btn3 || w == 0) { return; }
    gfx2_render(btn3_x(st), btn_y(st), w, UDG_BTN_H, solo_boton3, (void *)st);
}

uint8_t ui_digi_boton3_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    int16_t w = btn3_w(st);

    if (!st->btn3 || w == 0) { return 0U; }
    return (uint8_t)((int16_t)x >= btn3_x(st) - 4 &&
                     (int16_t)x <  btn3_x(st) + w + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

static void solo_boton4(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /* La banda llega en negro y se vuelca entera - ver gfx2_render(). El
     * boton es un rectangulo REDONDEADO, asi que sin este relleno las
     * cuatro esquinas se quedan sin pintar y salen negras. En el tema
     * oscuro no se nota; en el naranja y el oliva, si. */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton4(s, st);
}

void ui_digi_draw_boton4(const ui_digi_state_t *st)
{
    int16_t w = btn4_w(st);

    if (w == 0) { return; }
    gfx2_render(btn4_x(st), btn_y(st), w, UDG_BTN_H, solo_boton4, (void *)st);
}

uint8_t ui_digi_boton4_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    int16_t w = btn4_w(st);

    if (w == 0) { return 0U; }
    return (uint8_t)((int16_t)x >= btn4_x(st) - 4 &&
                     (int16_t)x <  btn4_x(st) + w + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

static void solo_boton5(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /* Ver solo_boton4(): sin este relleno las esquinas redondeadas salen
     * negras en los temas que no tienen el fondo negro. */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton5(s, st);
}

void ui_digi_draw_boton5(const ui_digi_state_t *st)
{
    int16_t w = btn5_w(st);

    if (w == 0) { return; }
    gfx2_render(btn5_x(st), btn_y(st), w, UDG_BTN_H, solo_boton5, (void *)st);
}

uint8_t ui_digi_boton5_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    int16_t w = btn5_w(st);

    if (w == 0) { return 0U; }
    return (uint8_t)((int16_t)x >= btn5_x(st) - 4 &&
                     (int16_t)x <  btn5_x(st) + w + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

/* Para el banco: que el quinto boton cabe de verdad en el hueco que dice
 * su comentario, medido con la geometria que usa el dibujo y no con una
 * copia de ella. Ver sim/digi.c. */
int16_t ui_digi_btn5_x_dbg(const ui_digi_state_t *st) { return btn5_x(st); }
int16_t ui_digi_btn5_w_dbg(const ui_digi_state_t *st) { return btn5_w(st); }
int16_t ui_digi_btn4_x_dbg(const ui_digi_state_t *st) { return btn4_x(st); }
int16_t ui_digi_btn4_w_dbg(const ui_digi_state_t *st) { return btn4_w(st); }
/* El del tercero faltaba, y es el que de verdad se queda en cero cuando
 * la barra ocupa todo el ancho. Ver sim/digi.c. */
int16_t ui_digi_btn3_w_dbg(const ui_digi_state_t *st) { return btn3_w(st); }
int16_t ui_digi_btn3_x_dbg(const ui_digi_state_t *st) { return btn3_x(st); }
int16_t ui_digi_barra_x_dbg(const ui_digi_state_t *st) { return bar_x(st); }
int16_t ui_digi_barra_w_dbg(const ui_digi_state_t *st) { return bar_w(st); }
int16_t ui_digi_chip_x_dbg(const ui_digi_state_t *st) { return chip_x(st); }
int16_t ui_digi_chip_w_dbg(const ui_digi_state_t *st) { return chip_w(st); }

static void solo_boton2(gfx2_surf_t *s, void *ctx)
{
    const ui_digi_state_t *st = (const ui_digi_state_t *)ctx;

    /* La banda llega en negro y se vuelca entera - ver gfx2_render(). El
     * boton es un rectangulo REDONDEADO, asi que sin este relleno las
     * cuatro esquinas se quedan sin pintar y salen negras. En el tema
     * oscuro no se nota; en el naranja y el oliva, si. */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    pinta_boton2(s, st);
}

void ui_digi_draw_boton2(const ui_digi_state_t *st)
{
    if (!st->btn2) { return; }
    gfx2_render(UDG_BTN2_X, btn_y(st), UDG_BTN2_W, UDG_BTN_H, solo_boton2, (void *)st);
}

uint8_t ui_digi_boton2_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    if (!st->btn2) { return 0U; }
    return (uint8_t)((int16_t)x >= UDG_BTN2_X - 4 &&
                     (int16_t)x <  UDG_BTN2_X + UDG_BTN2_W + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

/*
 * Y LA BARRA TAMBIEN SE TOCA, cuando es ella la que lleva lo que llevaba
 * la chapa - 13/10/2026.
 *
 * Y era cierto. en recordarlo: en FT8 ese toque es el que cuadra la
 * ranura -"el cero de la ranura es AHORA"- y vivia en la chapa porque era
 * la chapa quien decia el segundo. Al meter el segundo dentro de la barra
 * y apagar la chapa, el toque se habria quedado sin sitio donde caer: la
 * funcion no se pierde, se MUDA con el numero que la explica.
 *
 * La zona es el rectangulo de la barra con cuatro pixeles de aire, igual
 * que los botones. No hace falta darle mas: la barra de esta disposicion
 * mide 336 px y no comparte franja con nadie.
 */
uint8_t ui_digi_barra_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    if (!st->barra_on) { return 0U; }
    return (uint8_t)((int16_t)x >= bar_x(st) - 4 &&
                     (int16_t)x <  bar_x(st) + bar_w(st) + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

/*
 * La chapa de estado tambien se toca - 24/09/2026.
 *
 * La zona NO es el rectangulo de la chapa sino toda la franja desde donde
 * acaba el segundo boton hasta el borde: la chapa encoge y crece con su
 * texto, y una zona de toque que se mueve debajo del dedo es una zona que
 * unas veces responde y otras no. Ahi no hay nada mas que la chapa, asi
 * que darle sitio de sobra no le quita nada a nadie.
 */
uint8_t ui_digi_chip_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    /*
     * Desde donde acabe el ultimo boton que haya: dos zonas de toque
     * solapadas son una zona que hace la cosa equivocada.
     *
     * *** 28/09/2026: el quinto boton -el del mapa- se metio en medio de
     * esta zona y la chapa seguia respondiendo debajo de el. Lo pillo
     * sim/digi.c antes de flashear; en la radio habria salido como que
     * tocar "Mapa" en FT8 cuadraba tambien la ranura. ***
     */
    int16_t desde = (btn5_w(st) != 0)
                  ? (int16_t)(btn5_x(st) + btn5_w(st))
                  : ((btn4_w(st) != 0)
                     ? (int16_t)(btn4_x(st) + btn4_w(st))
                     : (int16_t)UDG_CHIP_X0);

    if (!st->chip) { return 0U; }
    return (uint8_t)((int16_t)x >= desde &&
                     (int16_t)x <  GFX2_W &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

uint8_t ui_digi_boton_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y)
{
    if (!st->btn) { return 0U; }
    /* Margen de 4 px, igual que el resto de zonas: este tactil pide fuerza y
     * un toque que cae justo en el borde y no hace nada se vive como que no
     * ha entrado. */
    return (uint8_t)((int16_t)x >= UDG_BTN_X - 4 &&
                     (int16_t)x <  UDG_BTN_X + UDG_BTN_W + 4 &&
                     (int16_t)y >= btn_y(st) - 4 &&
                     (int16_t)y <  btn_y(st) + UDG_BTN_H + 4);
}

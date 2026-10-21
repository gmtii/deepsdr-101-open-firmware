/*
 * LA CHAPA DE LAS TRES CABECERAS DE IMAGEN - 19/10/2026.
 *
 * WEFAX, SSTV y Feld-Hell no usan el panel de texto: se arman su propia
 * cabecera con los dos botones de la izquierda y la chapa de estado a la
 * derecha. Y esa chapa RECORTA POR DELANTE lo que no le cabe.
 *
 * Recortar por delante es lo correcto en un contador -que es para lo que
 * nacio- y lo peor posible aqui, porque lo de delante es la palabra que
 * dice en que fase va: "Esperando el tono de arranque - 120 lpm" se leia
 * "ono de arranque - 120 lpm". Y no avisa: no sale nada raro, sale menos.
 * Avisado por el usuario.
 *
 * ESTE BANCO MIDE DOS COSAS
 * -------------------------
 *   1. Que el peor caso de cada estado cabe entero en la chapa, con la
 *      cabecera tal y como la montan esas tres funciones: boton, segundo
 *      boton y nada mas.
 *   2. Que si queda puesto el quinto boton de otro modo -"Mapa", de FT8 o
 *      de JTTY- la chapa se encoge a su ancho reservado. Eso es correcto
 *      -lo reservado es lo reservado- y es justo por lo que main.c tiene
 *      que limpiar la cabecera antes de armarla (img_hdr_limpia()). Aqui
 *      se comprueba que la diferencia entre las dos configuraciones existe
 *      de verdad, o sea que el caso 1 no esta pasando por casualidad.
 *
 * Lo que se lee NO se calcula aqui: lo da ui_digi_chip_visible(), que es la
 * misma funcion que usa pinta_chip(). Un banco con una copia del recorte
 * mide su copia.
 *
 * Los estados tampoco estan copiados: los saca tools/gen_chapas.py de
 * main.c en cada "make comprueba".
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include "chapas_tabla.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

#define HDR_Y 104
#define HDR_H 30

/*
 * EL PEOR CASO QUE PUEDE LLEVAR DETRAS EL ESTADO.
 *
 * Lo arma fax_hdr_draw(): " - ", tres cifras de lpm, " lpm", un espacio, el
 * signo y cuatro cifras de correccion. Son los mismos campos que cuenta el
 * _Static_assert de s_fax_txt en main.c, y por la misma razon: quien anada
 * otro campo tiene que venir a los dos sitios.
 *
 * El de SSTV es mas corto ("*" + " - " + "999/999") y el de Feld-Hell no
 * lleva nada, asi que midiendo este se miden los tres.
 */
#define COLA " - 999 lpm +9999"

static void caso(const char *estado, const char *btn2, int con_btn5)
{
    ui_digi_state_t st;
    char txt[96];
    const char *visto;

    memset(&st, 0, sizeof st);
    st.y = HDR_Y;
    st.h = HDR_H;
    snprintf(txt, sizeof txt, "%s%s", estado, COLA);
    st.chip = txt;
    st.btn  = "Sincroniza";
    st.btn2 = btn2;
    if (con_btn5) { st.btn5 = "Mapa"; }

    visto = ui_digi_chip_visible(&st);

    if (!con_btn5) {
        if (strcmp(visto, txt) != 0) {
            printf("    RECORTADO  \"%s\"\n", txt);
            printf("               se lee \"%s\"  (%d px de %d)\n",
                   visto, gfx2_text_w(visto, &font_ui_14b),
                   gfx2_text_w(txt, &font_ui_14b));
            fallos++;
        }
    } else {
        /* Con el quinto boton puesto la chapa vale 140 px y esto NO cabe.
         * Si cupiera, el ancho reservado habria dejado de reservarse. */
        if (strcmp(visto, txt) == 0) {
            printf("    el quinto boton ya no reserva su sitio: \"%s\" cabe\n",
                   txt);
            fallos++;
        }
    }
}

int main(void)
{
    size_t i;
    const size_t n = sizeof k_chapas_estado / sizeof k_chapas_estado[0];

    printf("== la chapa de las cabeceras de imagen ==\n");
    printf("  %zu estados, peor caso \"<estado>%s\"\n", n, COLA);

    for (i = 0; i < n; i++) {
        /* Las dos cabeceras que existen: con segundo boton (WEFAX y SSTV,
         * que llevan el de guardar) y sin el (Feld-Hell). */
        caso(k_chapas_estado[i], "No guardar", 0);
        caso(k_chapas_estado[i], 0, 0);
    }
    /* Y uno solo con el boton de otro modo puesto, para que se vea que la
     * holgura de arriba no es que el tope haya desaparecido. */
    caso(k_chapas_estado[0], "No guardar", 1);

    /*
     * ======================================================================
     * Y QUE LA CABECERA DEL FAX NO SE QUEDE CON EL BOTON DE AYER
     * ======================================================================
     *
     * Aqui habia un caso que exigia que el TERCER boton del fax -el del
     * centrado- se dibujara de verdad. Nacio de una entrega en la que ese
     * boton existia, respondia al dedo y no se veia: btn3_w() de ui_digi.c
     * no es un numero, es una resta -lo que queda entre donde acaba la
     * zona de la barra y donde empieza la chapa-, y sin acortar la barra
     * con barra_w el hueco sale negativo y no se pinta nada.
     *
     * El boton se fue el 20/10/2026: el centrado se elige al acabar la
     * carta, con una linea vertical sobre la imagen entera.
     *
     * Pero el caso no se borra, se le da la vuelta, porque s_digi es UNA
     * estructura compartida: si al quitar el boton se hubiera quedado un
     * barra_w de 160 puesto por el modo anterior, la cabecera del fax
     * saldria con la barra corta y la chapa encogida, y eso NO salta a la
     * vista en una captura -se lee como que el texto de estado es corto-.
     * Lo que se exige ahora es lo contrario de antes: que no haya tercer
     * boton y que la chapa recupere su ancho entero.
     * ====================================================================== */
    printf("\n  la cabecera del fax, ya sin tercer boton\n");
    {
        ui_digi_state_t st, sucia;
        int16_t xc_limpia, xc_sucia;

        memset(&st, 0, sizeof st);
        st.y = HDR_Y;
        st.h = HDR_H;
        st.chip = "Recibiendo - 120 lpm - WFAX003 176";
        st.btn  = "Sincroniza";
        st.btn2 = "Guardando 003";
        /* btn3 y barra_w a cero: es lo que deja img_hdr_limpia() antes de
         * que fax_hdr_draw() monte la cabecera. */

        if (ui_digi_btn3_w_dbg(&st) > 0) {
            printf("    *** sigue habiendo un tercer boton ***\n");
            fallos++;
        } else {
            printf("    no hay tercer boton  ok\n");
        }

        xc_limpia = ui_digi_chip_x_dbg(&st);

        /*
         * Y LA PRUEBA DE QUE ESTO MIDE ALGO: la cabecera de ANTES, con su
         * tercer boton y su barra acortada a 160, montada aqui al lado. La
         * chapa empezaba mas a la derecha, o sea que era mas estrecha: eso
         * es lo que costaba el boton y lo que se recupera al quitarlo.
         *
         * Si alguien vuelve a poner un btn3 en el fax sin pensarlo, este
         * numero se mueve y el banco lo dice.
         */
        memcpy(&sucia, &st, sizeof st);
        sucia.btn3    = "-378 px";
        sucia.barra_w = 160;
        xc_sucia = ui_digi_chip_x_dbg(&sucia);
        printf("    la chapa empieza en %d; con el boton de antes, en %d\n",
               xc_limpia, xc_sucia);
        if (xc_limpia > xc_sucia) {
            printf("    *** la chapa no ha recuperado su ancho ***\n");
            fallos++;
        } else if (xc_limpia == xc_sucia) {
            printf("    *** el tercer boton no encogia la chapa: esta prueba no"
                   " mide lo que dice ***\n");
            fallos++;
        } else {
            printf("    la chapa recupera los %d px que costaba el boton  ok\n",
                   xc_sucia - xc_limpia);
        }
    }

    if (fallos) {
        printf("\n  *** %d ***\n", fallos);
        return 1;
    }
    printf("  todos caben enteros  ok\n");
    return 0;
}

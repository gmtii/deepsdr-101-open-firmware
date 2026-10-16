/*
 * QUE LA TABLA DE JTTY CABE EN SUS COLUMNAS - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * El mismo banco que sim/wspr_cols.c y sim/hfdl_cols.c y por la misma
 * razon, que aqui ademas acaba de pasar: las columnas se acaban de mover
 * TODAS a la vez -entra una quinta y se estrechan dos- y mirando la
 * pantalla solo se ve el caso que haya llegado ese minuto.
 *
 * Y EN JTTY HAY UNA COLUMNA QUE NO SALE DE LO QUE DECODIFICA LA RADIO: el
 * pais sale de dxcc.c, o sea de una tabla de 153 nombres que estan todos
 * escritos de antemano. No hay que adivinar cual es el peor caso: se
 * pueden medir los 153 y quedarse con el mas ancho, que es lo que hace la
 * ultima prueba. Un banco que probara "Francia" y diera la columna por
 * buena se comeria "Nueva Zelanda" el dia que pase un ZL.
 *
 * Se mide con gfx2_text_w() y la MISMA fuente con la que se dibuja, asi que
 * lo que diga esto es lo que va a pasar en el cristal.
 */
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include "dxcc.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

/* El ancho util de la columna k: hasta donde empieza la siguiente. La
 * ultima llega al borde de la pantalla. */
static int16_t hueco(uint8_t k)
{
    int16_t a = ui_digi_cols_jtty[k];
    int16_t b;

    if ((uint8_t)(k + 1U) >= (uint8_t)UI_DIGI_COLS_JTTY_N) {
        return (int16_t)(GFX2_W - a);
    }
    b = ui_digi_cols_jtty[k + 1U];
    if (b < 0) { return (int16_t)((-b) - a); }
    return (int16_t)(b - a);
}

static void cabe(uint8_t col, const char *txt, const char *que)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);
    int16_t h = hueco(col);
    int ok = (w <= h);

    printf("    col %u  %-14s %4d px en %4d   %s   %s\n",
           col, txt, w, h, ok ? "ok " : "MAL", que);
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\nanchos de la tabla de JTTY\n\n");

    /* Los titulos. */
    cabe(0U, "Hora",    "titulo");
    cabe(1U, "Desvío",  "titulo");
    cabe(2U, "Cal.",    "titulo");
    cabe(3U, "Mensaje", "titulo");
    cabe(4U, "País",    "titulo");

    printf("\n");

    /* Y los datos, en su peor caso. */
    cabe(0U, "23:58",   "la hora mas ancha");
    /*
     * El desvio: la ventana de busqueda son 64 medios bines de 15,625 Hz
     * alrededor de 1.500, o sea +-500 Hz. Se prueba con 500 y su signo.
     */
    cabe(1U, "-500 Hz", "el desvio mas ancho que cabe en la ventana");
    /* La calidad es la correlacion de sincronismo por cien: 0..100. */
    cabe(2U, "100",     "la calidad mas ancha");

    /*
     * EL PAIS: LOS 153 QUE HAY, no una muestra.
     *
     * dxcc.c tiene los nombres escritos, asi que el peor caso no se supone:
     * se recorre la tabla entera preguntando por todos los prefijos que
     * conoce y se mide el nombre mas ancho que salga.
     */
    printf("\n");
    {
        /*
         * No hay una funcion que devuelva el nombre numero N, asi que se
         * pregunta por indicativos hechos con cada prefijo posible de una
         * a tres letras/cifras. Es fuerza bruta y tarda un parpadeo, y a
         * cambio no depende de que yo elija bien la muestra.
         */
        static const char k_car[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const char *peor = "";
        int peor_w = 0, vistos = 0;
        unsigned a, b, c;
        char ind[16];

        for (a = 0U; a < sizeof k_car - 1U; a++) {
            for (b = 0U; b < sizeof k_car - 1U; b++) {
                for (c = 0U; c < sizeof k_car - 1U; c++) {
                    const char *p;
                    int w;

                    ind[0] = k_car[a]; ind[1] = k_car[b]; ind[2] = k_car[c];
                    /* Un sufijo cualquiera para que parezca un indicativo. */
                    ind[3] = '1'; ind[4] = 'A'; ind[5] = 'B'; ind[6] = 'C';
                    ind[7] = '\0';
                    p = dxcc_pais(ind);
                    if (p == 0) { continue; }
                    vistos++;
                    w = gfx2_text_w(p, &font_ui_14b);
                    if (w > peor_w) { peor_w = w; peor = p; }
                }
            }
        }
        printf("    se han probado %d prefijos de la tabla (%u conocidos)\n",
               vistos, dxcc_cuantos());
        printf("    el nombre de pais mas ancho es \"%s\"\n", peor);
        cabe(4U, peor, "el pais mas ancho de los 153 de dxcc.c");
    }

    /*
     * Y QUE AL MENSAJE LE QUEDE SITIO DE VERDAD. Es la columna por la que
     * se hizo todo esto -"o haces mas grande el campo de texto o lo haces
     * marquesina"- asi que si un dia otra columna crece a su costa, aqui
     * salta. Un mensaje tipico de los que salen en 14.090 tiene que caber
     * entero.
     */
    printf("\n");
    cabe(3U, "LZ1QZ DE F4JUO UR RST 599 009 OP JACQUE",
         "un mensaje entero de los de la banda");
    {
        int16_t h = hueco(3U);

        printf("    al mensaje le quedan %d px\n", h);
        if (h < 460) {
            printf("    menos de 460: alguna columna ha crecido a su costa"
                   "   MAL\n");
            fallos++;
        }
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

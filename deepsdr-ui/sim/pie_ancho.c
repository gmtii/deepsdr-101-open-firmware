/*
 * QUE EL PIE DE LA CELDA QUEPA EN LA CELDA - 06/10/2026.
 *
 * Pedido por el usuario.
 *
 * La primera era un desbordamiento del buffer y se arreglo contando mal: se
 * amplio el hueco a 40 bytes y se acorto el texto, y el segundo recorte
 * seguia ahi. Porque el limite que manda NO es el buffer: es el ANCHO DE LA
 * CELDA, 192 px, y la fuente es PROPORCIONAL. Por eso los dos recortes caen
 * en sitios distintos: "0dsc 0trd 1rep 75-255/1024" se corta en el caracter
 * 24 y "0d 0t 1r 75-256/1024" en el 19 - las letras no miden lo mismo.
 *
 * Contar caracteres no sirve. Hay que MEDIR, y para eso esta gfx2_text_w(),
 * que es exactamente lo que usa gfx2_text_in() para centrar y recortar.
 *
 * Este banco monta los pies de la fila de audio y de la zona alta con sus
 * valores mas largos y mide el ancho de verdad, con la fuente de verdad -la
 * misma font_ui_14b que pinta ese renglon-. Si no cabe, para.
 *
 * Lo que NO comprueba: las demas filas. Se añaden aqui cuando alguien les
 * ponga un pie.
 */
#include "gfx2.h"
#include "gfx2_font.h"
#include "ui_grid.h"
#include <stdio.h>
#include <string.h>

extern const gfx2_font_t font_ui_14b;

static int malos;

static void mide(const char *que, const char *txt)
{
    int16_t w = gfx2_text_w(txt, &font_ui_14b);

    printf("  %-22s %-34s %3d px de %d", que, txt, w, UIG_CELL_W);
    if (w > UIG_CELL_W) {
        printf("   *** NO CABE: se corta ***\n");
        malos++;
    } else {
        printf("   ok  (sobran %d)\n", UIG_CELL_W - w);
    }
}

int main(void)
{
    printf("QUE EL PIE DE LA CELDA QUEPA EN LA CELDA\n\n");
    printf("  la celda mide %d px y la fuente es PROPORCIONAL:\n", UIG_CELL_W);
    printf("  contar caracteres no sirve, hay que medir.\n\n");

    /*
     * La fila de audio. Los contadores son uint32_t y pie_num() los acota a
     * cinco cifras mas un '+', asi que el peor caso es "99999+". El margen
     * va en tanto por ciento, que no puede pasar de "100-100%".
     */
    mide("audio, recien puesto", "0 0 0 -");
    mide("audio, lo que leyo",   "0 0 1 7-25%");
    mide("audio, peor caso",     "999+ 999+ 999+ 99-99%");

    /* La zona alta: dir huecos/ranuras, libre kB. Los huecos y las ranuras
     * no pasan de 256 por construccion; lo libre, de 15360 kB con el chip
     * mas grande que admite el driver. */
    mide("zona alta, normal",    "dir 3/256, 1024 kB");
    mide("zona alta, peor caso", "dir 256/256, 15360 kB");

    /* El pie de la fila "USB del pincho" (INFO_USBPAT), que es el mas
     * largo que se ha escrito hasta ahora: 32 caracteres. Se mide aqui
     * porque contar caracteres no sirve - la fuente es proporcional - y
     * porque este pie sale justo cuando algo va raro, que es el peor
     * momento para que ademas salga cortado. */
    mide("usb, no llega",       "con el pincho puesto");
    mide("usb, llega",          "llega al USBFS");
    mide("usb, peor caso",      "eso no es un pincho");
    mide("usb, peor caso (en)", "that is not a stick");
    mide("usb, invita al toque", "pincho dentro \u00b7 toca");
    mide("usb, invita (en)",     "stick in \u00b7 one tap sweeps");
    mide("usb, barrido ok",      "ahi esta el conector");
    mide("usb, barrido vacio",   "no se mueve ninguna");
    mide("usb, barrido (en)",    "no free pin moves");
    mide("serie, apagado",      "un toque lo enciende");
    mide("serie, esperando",    "enchufa el cable al PC");
    mide("serie, sin reloj",    "el PLLSAI no engancha");
    mide("serie, abierto",      "CAT Kenwood TS-2000");
    mide("serie, con perdidas", "99999+ perdidos");
    mide("serie, con audio",    "audio 48k + CAT");
    mide("serie, huecos",       "99999+ huecos de audio");
    mide("serie, huecos (en)",  "99999+ audio gaps");
    mide("serie, desconectado", "otro toque lo engancha");
    mide("serie, descon. (en)", "tap again to plug in");
    mide("serie, abierto (en)", "plug the cable to the PC");

    if (malos) {
        printf("\n  HAY FALLOS (%d)\n", malos);
        printf("  Acorta el texto. Ampliar el buffer NO sirve: el limite\n");
        printf("  que manda es el ancho de la celda. Ver la cabecera.\n");
        return 1;
    }
    printf("\n  TODO CORRECTO\n");
    return 0;
}

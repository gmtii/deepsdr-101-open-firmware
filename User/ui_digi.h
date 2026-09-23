#ifndef UI_DIGI_H_INCLUDED
#define UI_DIGI_H_INCLUDED

#include <stdint.h>

/*
 * PANEL DE MODOS DIGITALES - etapa 20, 22/09/2026.
 *
 * El texto que sale de RTTY y de CW, con su boton de borrar y su chapa de
 * estado. Era lo ULTIMO que quedaba dibujado con gfx.c, o sea con la
 * tipografia de mapa de bits y los rectangulos planos de antes del rediseno,
 * y encima es la pantalla que mas se mira cuando estas decodificando.
 *
 * Como en el resto de gfx2, aqui no se sabe de RTTY ni de CW: llegan ocho
 * renglones ya compuestos y una chapa ya redactada. Quien decide que pone es
 * main.c, que es quien habla con los decodificadores.
 *
 * POR QUE EL TEXTO VA EN TIPOGRAFIA PROPORCIONAL
 * ----------------------------------------------
 * La version anterior dibujaba caracter a caracter en una rejilla de ancho
 * fijo, 12 px por hueco, porque la fuente de mapa de bits era de ancho fijo.
 * gfx2 no trae ninguna monoespaciada con letras -font_num_44 solo tiene
 * cifras-, y anadir una costaba unos 6 KB de flash por un detalle que nadie
 * pide: lo que se lee en este panel son indicativos y frases, no columnas.
 *
 * Asi que cada renglon se dibuja de una vez, proporcional. Sale mas legible,
 * cuesta cero flash, y de paso desaparece RTTY_TEXT_CHAR_W, que era un
 * numero copiado a mano de las tripas de gfx.c y que habria que haber
 * corregido a mano el dia que la fuente cambiara.
 */

#define UDG_ROWS 8

/*
 * CABECERA DEL PANEL - 22/09/2026, por el dueno del proyecto: "los ppm y el
 * boton borrar parpadean".
 *
 * Parpadeaban por donde estaban, no por como se dibujaban. Los dos vivian
 * ENCIMA del trazo, y spectrum_draw() repinta el trazo entero en cada cuadro:
 * cada frame los borraba y volvian a dibujarse justo despues. Eso es un
 * parpadeo por construccion, y no hay forma de quitarlo mientras sigan ahi.
 *
 * Asi que se mudan a una franja propia en lo alto del panel de texto, que
 * solo se repinta cuando cambia el texto. El trazo cede 30 px -le quedan
 * 142, de sobra para sintonizar- y a cambio el boton queda al lado del texto
 * que borra y la velocidad al lado de las letras que salen de ella.
 *
 * La geometria de los dos la decide ESTE fichero, que es quien los dibuja:
 * antes se la pasaba main.c y era otra copia de lo mismo en dos sitios.
 */
#define UDG_HDR_H   30
#define UDG_LINE_H  18
#define UDG_BTN_X   6
#define UDG_BTN_W  104
#define UDG_BTN_H   24

typedef struct {
    int16_t      y, h;              /* donde va el panel: cabecera + renglones */

    /* Los renglones, del mas viejo al mas nuevo. Un 0 es un renglon vacio. */
    const char  *fila[UDG_ROWS];

    /* Chapa de estado, a la derecha de la cabecera: a que velocidad va y si
     * el decodificador esta enganchado. */
    const char  *chip;              /* "21 ppm", "45 bd"... 0 = no dibujarla */
    uint8_t      enganchado;        /* 1 = punto de "esto tiene forma de Morse" */

    /* Boton de borrar el texto, a la izquierda de la cabecera. */
    const char  *btn;               /* rotulo, 0 = sin boton */
    uint8_t      btn_press;
} ui_digi_state_t;

void    ui_digi_draw_texto(const ui_digi_state_t *st);  /* el panel de renglones */
void    ui_digi_draw_fila(const ui_digi_state_t *st, uint8_t i); /* solo uno */
void    ui_digi_draw_chip(const ui_digi_state_t *st);   /* la chapa de estado */
void    ui_digi_draw_boton(const ui_digi_state_t *st);
uint8_t ui_digi_boton_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);

#endif /* UI_DIGI_H_INCLUDED */

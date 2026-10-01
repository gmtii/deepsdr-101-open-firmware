#ifndef UI_QTH_H_INCLUDED
#define UI_QTH_H_INCLUDED

#include <stdint.h>

/*
 * PANTALLA DEL QTH (localizador Maidenhead) - 29/09/2026, por el dueno.
 *
 * POR QUE EXISTE
 * --------------
 * Hasta hoy el localizador era un #define en ft8_decoder.c y valia "IL18",
 * que es Canarias: el ejemplo que traia el comentario original ("CHANGE
 * THIS to your own square before flashing") y que nadie cambio nunca. De
 * ese valor salen DOS cosas que se ven - el aspa de "donde estamos" en los
 * mapas de FT8, WSPR, AIS y JTTY, y la distancia de las lineas de CQ de
 * FT8-, asi que el aspa salia en Tenerife y todas las distancias se median
 * desde 1.800 km de donde tocaba. Sin un solo aviso: un localizador valido
 * que no es el tuyo se comporta igual que el bueno.
 *
 * SEIS CASILLAS Y NO UN TECLADO
 * -----------------------------
 * Un localizador no es texto libre: cada posicion tiene su alfabeto.
 *
 *     1-2  A..R   campo           3-4  0..9   cuadricula
 *     5-6  a..x   subcuadricula
 *
 * Con un teclado alfanumerico habria que dejar escribir cualquier cosa y
 * luego rechazarla, lo que pide una pantalla de error y un camino en el que
 * el usuario se queda con algo invalido a medio escribir. Con una casilla
 * por posicion y solo su alfabeto, LO INVALIDO NO SE PUEDE TECLEAR. No hay
 * validacion porque no hace falta.
 *
 * LAS COORDENADAS DEBAJO
 * ----------------------
 * No son decoracion. "IN80dk" y "IL18dk" se parecen mucho y estan a 1.796
 * km; la unica forma de ver de un vistazo que te has equivocado de letra es
 * leer la latitud y la longitud. Las calcula quien llama, no este fichero:
 * aqui no se sabe de Maidenhead, igual que ui_grid.c no sabe de ajustes.
 *
 * GEOMETRIA
 * ---------
 * La misma banda 104..421 que la rejilla y que ui_hora, para que al pasar
 * de una pantalla a otra no baile nada.
 */

#define UIQ_Y        104
#define UIQ_H        318                 /* 104..421, igual que la rejilla */
#define UIQ_PAD       14

#define UIQ_N          6                 /* caracteres del localizador */
#define UIQ_CAJA_W    96
#define UIQ_CAJA_H    88
#define UIQ_CAJA_GAP  12
#define UIQ_CAJA_Y   (UIQ_Y + 58)
#define UIQ_CAJA_X0  ((800 - (UIQ_N * UIQ_CAJA_W + (UIQ_N - 1) * UIQ_CAJA_GAP)) / 2)

#define UIQ_BTN_H     56
#define UIQ_BTN_W    176
#define UIQ_BTN_GAP   12
#define UIQ_BTN_Y    (UIQ_Y + UIQ_H - UIQ_BTN_H - 12)

/* Los cuatro botones de abajo, y luego las seis casillas: 0..3 son botones
 * y 4..9 son las casillas, o sea UIQ_HIT_CAJA + i. */
#define UIQ_HIT_NONE   (-1)
#define UIQ_HIT_MENOS    0
#define UIQ_HIT_MAS      1
#define UIQ_HIT_APLICAR  2
#define UIQ_HIT_SALIR    3
#define UIQ_BTN_N        4
#define UIQ_HIT_CAJA     4

typedef struct {
    const char *titulo;          /* "QTH" / "QTH" */
    const char *pie;             /* la linea pequena de la derecha */
    char        loc[UIQ_N + 1];  /* los seis caracteres, terminados en cero */
    const char *pista[UIQ_N];    /* "A-R", "0-9", "a-x" - el alfabeto de cada una */
    const char *coord;           /* "40,4375 N   3,7083 W", ya formateado */
    const char *rot[UIQ_BTN_N];  /* rotulos de los cuatro botones */
    uint8_t     sel;             /* casilla elegida, 0..UIQ_N-1 */
    uint8_t     cambiado;        /* 1 = distinto de lo guardado: "Aplicar" vivo */
    int8_t      pressed;         /* UIQ_HIT_*, o -1 */
} ui_qth_state_t;

void   ui_qth_draw(const ui_qth_state_t *st);
int8_t ui_qth_hit(uint16_t x, uint16_t y);

#endif /* UI_QTH_H_INCLUDED */

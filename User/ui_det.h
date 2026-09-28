#ifndef UI_DET_H_INCLUDED
#define UI_DET_H_INCLUDED

#include <stdint.h>
#include "ui_grid.h"   /* comparte la zona del menu: UIG_Y / UIG_H */

/*
 * Pantalla de un ajuste de rango continuo: volumen, brillo, silenciador,
 * ganancia, reduccion, suavizado, desplazamiento de RTTY y escala.
 *
 * Es la unica pantalla donde el mando tiene algo que hacer que no sea elegir
 * de una lista, asi que se diseña alrededor de eso: el valor grande en el
 * centro y dos objetivos enormes a los lados para el dedo. Nada mas. Los
 * limites y el paso los pone quien la abre.
 *
 * "-" y "+" son los objetivos mas grandes de toda la interfaz -170x130 px,
 * unos 29 x 22 mm- a proposito: es el unico sitio donde se pulsa muchas
 * veces seguidas, y en un panel resistivo eso es donde mas cansa fallar.
 */
#define UID_BTN_W   170
#define UID_BTN_H   130
#define UID_BTN_Y   (UIG_Y + 62)              /* 166 */
#define UID_BTN_X0  14
#define UID_BTN_X1  (800 - UID_BTN_W - 14)    /* 616 */
#define UID_PIE_H    34
#define UID_PIE_Y   (UIG_Y + UIG_H - UID_PIE_H - 4)   /* 384 */

/*
 * FILA DE AJUSTES RAPIDOS - 24/09/2026, a peticion del dueño del proyecto:
 * "debajo de los Hz, tres botones que eran los tres BW fijos".
 *
 * Es el reparto que hace util un mando continuo: el mando afina, y estos
 * tres devuelven de un toque a algo conocido. Sin ellos, volver a los 2,3
 * kHz de siempre son veinte pulsaciones del "+".
 *
 * Van en la franja que queda LIBRE entre los dos botones grandes y el pie.
 * Los grandes se llevan el toque hasta UID_BTN_Y + UID_BTN_H + 20 = 316 y
 * el pie empieza en UID_PIE_Y - 8 = 376, asi que aqui hay 60 px de toque
 * para un boton de 50 de alto: por encima de los 47 px de objetivo minimo
 * de dedo que usa el resto de la interfaz.
 */
#define UID_PRE_N     3
#define UID_PRE_Y    (UID_BTN_Y + UID_BTN_H + 24)     /* 320 */
#define UID_PRE_H     50
#define UID_PRE_X0   UID_BTN_X0                       /* alineados con los grandes */
#define UID_PRE_GAP   8
#define UID_PRE_W    (((UID_BTN_X1 + UID_BTN_W) - UID_BTN_X0 \
                       - (UID_PRE_N - 1) * UID_PRE_GAP) / UID_PRE_N)

#define UID_HIT_NONE   (-1)
#define UID_HIT_MENOS    0
#define UID_HIT_MAS      1
#define UID_HIT_ALT      2   /* "LO / HI", solo en escala */
#define UID_HIT_VOLVER   3
#define UID_HIT_PRE0     4   /* los tres ajustes rapidos, si los hay */
#define UID_HIT_COUNT    (UID_HIT_PRE0 + UID_PRE_N)

typedef struct {
    const char *titulo;
    const char *valor;     /* ya formateado, con unidad */
    const char *pie;       /* linea de contexto bajo el valor, o 0 */
    const char *alt;       /* rotulo del boton central, o 0 si no lo hay */
    uint8_t     alt_on;    /* 1 = el boton central esta "activo" */
    /* Los tres ajustes rapidos. Con preset[0] a 0 la fila no existe y la
     * pantalla queda exactamente como estaba. */
    const char *preset[UID_PRE_N];
    int8_t      preset_on; /* cual esta puesto ahora mismo, o -1 */
    int8_t      pressed;   /* UID_HIT_*, o -1 */
} ui_det_state_t;

void   ui_det_draw(const ui_det_state_t *st);
void   ui_det_draw_valor(const ui_det_state_t *st);   /* solo el numero */
void   ui_det_draw_one(const ui_det_state_t *st, int8_t i);
int8_t ui_det_hit(uint16_t x, uint16_t y);

#endif /* UI_DET_H_INCLUDED */

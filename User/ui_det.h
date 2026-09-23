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

#define UID_HIT_NONE   (-1)
#define UID_HIT_MENOS    0
#define UID_HIT_MAS      1
#define UID_HIT_ALT      2   /* "LO / HI", solo en escala */
#define UID_HIT_VOLVER   3
#define UID_HIT_COUNT    4

typedef struct {
    const char *titulo;
    const char *valor;     /* ya formateado, con unidad */
    const char *pie;       /* linea de contexto bajo el valor, o 0 */
    const char *alt;       /* rotulo del boton central, o 0 si no lo hay */
    uint8_t     alt_on;    /* 1 = el boton central esta "activo" */
    int8_t      pressed;   /* UID_HIT_*, o -1 */
} ui_det_state_t;

void   ui_det_draw(const ui_det_state_t *st);
void   ui_det_draw_valor(const ui_det_state_t *st);   /* solo el numero */
void   ui_det_draw_one(const ui_det_state_t *st, int8_t i);
int8_t ui_det_hit(uint16_t x, uint16_t y);

#endif /* UI_DET_H_INCLUDED */

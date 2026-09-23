#ifndef UI_ACT_H_INCLUDED
#define UI_ACT_H_INCLUDED

#include <stdint.h>

/*
 * ETAPA 4: la barra de acciones de abajo.
 *
 * QUE CAMBIA RESPECTO A LA DE ANTES
 * ---------------------------------
 * 1. Nombres en vez de siglas. "Ajustes" en lugar de "MENU", "Paso" en lugar
 *    de "STEP". Caben porque la fuente es proporcional: "Volumen" ocupa MENOS
 *    que "VOL" en la 5x7 escalada a 2, que es monoespaciada y gorda.
 *
 * 2. Cada boton dice su valor actual debajo del nombre. Antes, para saber en
 *    que paso estabas habia que mirar a otro sitio de la pantalla; el boton
 *    que lo cambia es el sitio natural para decirlo.
 *
 * 3. Mas grandes y pegados a los bordes. Los 60 px libres que quedan entre el
 *    final del waterfall (419) y el borde de la pantalla se reparten: 7 px de
 *    aire arriba -antes los botones salian pegados al waterfall, sin
 *    separacion-, 50 px de boton (8,5 mm) y 3 px abajo. A lo ancho, margen de
 *    2 px a cada lado en vez de 10 y 14: los botones llegan a los extremos y
 *    cada uno gana 5 px.
 *
 * 4. El estado pulsado se dibuja de verdad. En un tactil resistivo que pide
 *    tocar con ganas, ver que el toque ha entrado es lo que evita el segundo
 *    toque de mas.
 *
 * COMO SE TOCA
 * ------------
 * Igual que los chips de la barra de estado: la zona de toque sale de la
 * misma geometria que el dibujo (ui_act_hit), no de una segunda tabla de
 * rectangulos que haya que mantener en sintonia a mano.
 */

#define UI_ACT_Y      427      /* el waterfall acaba en 419: 7 px de aire */
#define UI_ACT_H       50      /* 427..476, y 3 px de margen hasta 480       */
#define UI_ACT_N        6
#define UI_ACT_MARGIN   2      /* a cada lado de la pantalla                 */
#define UI_ACT_GAP      8      /* entre botones                              */
#define UI_ACT_BTN_W  126      /* 2 + 6*126 + 5*8 + 2 = 800 exacto           */

typedef struct {
    /* Nombre fijo y valor actual de cada boton. value puede ser 0. */
    const char *name[UI_ACT_N];
    const char *value[UI_ACT_N];
    uint8_t     active[UI_ACT_N];   /* 1 = el ajuste esta encendido / es el destino del mando */
    int8_t      pressed;            /* indice pulsado ahora mismo, -1 = ninguno */
} ui_act_state_t;

void ui_act_draw(const ui_act_state_t *st);

/* Repinta un solo boton, para el retorno visual del toque sin volcar la
 * barra entera. */
void ui_act_draw_one(const ui_act_state_t *st, uint8_t i);

/* Indice del boton bajo (x, y), o -1. */
int8_t ui_act_hit(uint16_t x, uint16_t y);

#endif /* UI_ACT_H_INCLUDED */

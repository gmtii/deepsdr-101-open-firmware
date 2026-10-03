#ifndef UI_GRID_H_INCLUDED
#define UI_GRID_H_INCLUDED

#include <stdint.h>

/*
 * REJILLA PAGINADA, compartida por la pantalla de bandas y la de ajustes.
 *
 * POR QUE UN SOLO MOTOR
 * ---------------------
 * Las dos pantallas son la misma cosa: una cuadricula de celdas con un
 * titulo y uno o dos renglones debajo, paginada, con una celda señalada por
 * el mando y otra marcada como "la actual", y una fila de botones abajo.
 * Escribirla dos veces habria significado dos sitios donde arreglar el mismo
 * fallo de recorte, y dos aspectos que se separan poco a poco.
 *
 * Lo que NO vive aqui es de donde salen los textos: eso lo pone main.c, que
 * es quien sabe de bandas y de ajustes. Este fichero solo dibuja y resuelve
 * toques, para que el simulador pueda renderizarlo sin arrastrar el firmware.
 *
 * GEOMETRIA
 * ---------
 * Cuatro columnas por tres filas dentro de la zona del menu (y 104..421),
 * mas una cabecera con el titulo y el numero de pagina y un pie con tres
 * botones. Las celdas de 192x76 px son ~32 x 13 mm: de sobra para un dedo.
 */

/* Zona util del menu, la misma que usaban las pantallas anteriores. */
#define UIG_Y        104
#define UIG_H        318                    /* 104..421 */

#define UIG_HDR_H     32                    /* franja del titulo */
#define UIG_COLS       4
#define UIG_ROWS       3
#define UIG_CELLS    (UIG_COLS * UIG_ROWS)  /* 12 por pagina */
#define UIG_MARGIN     7
#define UIG_GAP        6
#define UIG_CELL_W   192                    /* 7 + 4*192 + 3*6 + 7 = 800 */
#define UIG_CELL_H    76
#define UIG_GRID_Y   (UIG_Y + UIG_HDR_H + 4)           /* 140 */
#define UIG_NAV_H     32
#define UIG_NAV_Y    (UIG_Y + UIG_H - UIG_NAV_H - 2)   /* 388 */

/* Indices que devuelve ui_grid_hit(), ademas de 0..UIG_CELLS-1. */
#define UIG_HIT_NONE   (-1)
#define UIG_HIT_PREV   (UIG_CELLS)
#define UIG_HIT_NEXT   (UIG_CELLS + 1)
#define UIG_HIT_COUNT  (UIG_CELLS + 2)

typedef struct {
    const char *l1;   /* titulo de la celda, en negrita: "49 m", "Silenciador" */
    const char *l2;   /* renglon de debajo: el rango, o el valor actual */
    const char *l3;   /* tercer renglon, o 0 si la celda solo tiene dos */
} ui_grid_cell_t;

typedef struct {
    const char     *titulo;               /* cabecera de la pagina */
    uint8_t         pagina;               /* 0..paginas-1 */
    uint8_t         paginas;
    uint8_t         n;                    /* celdas usadas en esta pagina */
    ui_grid_cell_t  cel[UIG_CELLS];
    int8_t          pressed;              /* -1 = ninguno */
    int8_t          cursor;               /* celda señalada por el mando, -1 = ninguna */
    uint8_t         marcada;              /* celda "esta es la activa", 0xFF si
                                           * ninguna */
    const char     *nav[2];               /* rotulos del pie */
    uint8_t         nav_on[2];            /* 1 = habilitado */
} ui_grid_state_t;

void ui_grid_draw(const ui_grid_state_t *st);
void ui_grid_draw_one(const ui_grid_state_t *st, int8_t i);
int8_t ui_grid_hit(uint16_t x, uint16_t y);

#endif /* UI_GRID_H_INCLUDED */

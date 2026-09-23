#ifndef UI_CFG_H_INCLUDED
#define UI_CFG_H_INCLUDED

#include <stdint.h>
#include "ui_grid.h"   /* comparte la zona del menu: UIG_Y / UIG_H */

/*
 * Pantalla de ajustes con COLUMNA DE CATEGORIAS a la izquierda.
 *
 * POR QUE ASI Y NO PAGINADA
 * -------------------------
 * Paginada, para ir de "Radio" a "Equipo" habia que pulsar Siguiente dos
 * veces y leer la cabecera para saber donde estabas. Con la columna, las
 * cuatro categorias estan SIEMPRE a la vista y cambiar es un toque: se ve
 * dónde estás y qué más hay sin tener que moverte para averiguarlo.
 *
 * Y sale gratis en sitio: la columna ocupa lo que ocupaba el pie de
 * navegacion, y las celdas pasan de 192x76 a 200x98 px (34 x 17 mm).
 *
 * EL VALOR ES LO GRANDE
 * ---------------------
 * En cada celda el nombre va arriba, pequeño, y el valor abajo, grande. Es
 * al reves de lo que suele hacerse y es a proposito: el nombre ya lo sabes
 * -lo estas buscando-, lo que quieres leer de un vistazo es cuanto vale.
 */

#define UIC_SIDE_X     6
#define UIC_SIDE_W   164
#define UIC_CATS       4
#define UIC_CAT_H     70
#define UIC_CAT_GAP    8
#define UIC_CAT_Y     (UIG_Y + 6)                      /* 110 */
/* El boton "Cerrar" se ha ido: se sale pulsando otra vez "Ajustes" en la
 * barra de abajo, que ya se resalta mientras la pantalla esta abierta. Lo que
 * ocupaba se reparte entre las cuatro categorias, que pasan de 58 a 70 px de
 * alto (164 x 70 = 28 x 12 mm). */

/*
 * DOS REPARTOS SOBRE LA MISMA PANTALLA
 * ------------------------------------
 * Ajustes son 9 por categoria como mucho y cada celda dice dos cosas (nombre
 * y valor): caben holgadas, 3 x 3 de 200 x 98.
 *
 * Bandas son hasta 16 por familia y cada celda dice tres (nombre, rango y
 * modo): 4 x 4 de 150 x 72. Mas pequeñas, pero siguen siendo 25 x 12 mm, y a
 * cambio NINGUNA familia necesita paginar - que es lo que de verdad cuesta
 * usar.
 *
 * El reparto lo elige quien rellena el estado, con `denso`. La columna de la
 * izquierda y el reparto de toques son los mismos en los dos.
 */
#define UIC_COLS       3
#define UIC_ROWS       3
#define UIC_CELL_W   200
#define UIC_CELL_H    98
#define UIC_GAP        8

#define UIC_DCOLS      4
#define UIC_DROWS      4
#define UIC_DCELL_W  150
#define UIC_DCELL_H   72
#define UIC_DGAP       5

#define UIC_CELLS     (UIC_DCOLS * UIC_DROWS)          /* 16, el mayor de los dos */
#define UIC_GRID_X   (UIC_SIDE_X + UIC_SIDE_W + 8)     /* 178 */
#define UIC_GRID_Y   (UIG_Y + 4)                       /* 108 */

#define UIC_HIT_NONE   (-1)
#define UIC_HIT_CAT0   (UIC_CELLS)                     /* 9..12 */
#define UIC_HIT_COUNT  (UIC_CELLS + UIC_CATS)

typedef struct {
    const char *nombre;
    const char *valor;     /* o "tocar" en las acciones */
    const char *extra;     /* tercer renglon (el modo, en bandas), o 0 */
} ui_cfg_cell_t;

typedef struct {
    const char     *cat[UIC_CATS];
    uint8_t         cat_sel;
    uint8_t         cats;                 /* cuantas categorias hay (<= UIC_CATS) */
    uint8_t         denso;                /* 1 = reparto 4x4 (bandas) */
    uint8_t         n;                    /* celdas usadas */
    uint8_t         marcada;              /* celda "esta es la activa", 0xFF si ninguna */
    ui_cfg_cell_t   cel[UIC_CELLS];
    int8_t          cursor;               /* celda señalada por el mando, -1 */
    int8_t          pressed;              /* UIC_HIT_*, -1 */
} ui_cfg_state_t;

void   ui_cfg_draw(const ui_cfg_state_t *st);
void   ui_cfg_draw_one(const ui_cfg_state_t *st, int8_t i);
int8_t ui_cfg_hit(uint16_t x, uint16_t y);

#endif /* UI_CFG_H_INCLUDED */

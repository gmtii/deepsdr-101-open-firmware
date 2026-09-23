#ifndef UI_KBD_H_INCLUDED
#define UI_KBD_H_INCLUDED

#include <stdint.h>

/*
 * TECLADO NUMERICO - etapa 19, 22/09/2026.
 *
 * Sustituye a las dos pantallas de teclado que quedaban del firmware de
 * antes del rediseno (meter la frecuencia a mano y poner la hora), que eran
 * lo unico que seguia pintandose con gfx.c: rectangulos planos, tipografia
 * de mapa de bits y rotulos en mayusculas inglesas -DEL, CLR, BACK, SET-
 * dentro de una interfaz que ya no se parecia en nada a eso.
 *
 * UN SOLO MODULO PARA LOS DOS
 * ---------------------------
 * La frecuencia y la hora son la misma pantalla: una lectura arriba y una
 * rejilla de 4x4 debajo. Lo unico que cambia son los rotulos de las cuatro
 * teclas de la derecha y como se formatea la lectura. Escribirlo dos veces
 * habria sido volver a tener dos aspectos que se separan poco a poco, que es
 * justo de lo que venimos.
 *
 * Aqui no se sabe ni de frecuencias ni de horas: llegan la lectura YA
 * formateada y los rotulos ya elegidos, igual que en ui_grid.c. Por eso el
 * simulador lo puede dibujar sin arrastrar el firmware.
 *
 * GEOMETRIA
 * ---------
 * Ocupa la misma zona que las demas pantallas de menu (y 104..421). Las
 * columnas son EXACTAMENTE las de ui_grid.h -7 de margen, cuatro de 192, 6
 * de hueco- para que al pasar de una pantalla a otra las cosas no bailen.
 * Las filas son mas bajas porque son cuatro en vez de tres y hay que dejar
 * sitio a la lectura.
 */

#define UIK_Y          104
#define UIK_H          318                  /* 104..421, igual que la rejilla */

#define UIK_READ_H      54                  /* franja de la lectura */
#define UIK_COLS         4
#define UIK_ROWS         4
#define UIK_KEYS       (UIK_COLS * UIK_ROWS)
#define UIK_MARGIN       7
#define UIK_GAP          6
#define UIK_CELL_W     192                  /* 7 + 4*192 + 3*6 + 7 = 800 */
#define UIK_CELL_H      59
#define UIK_GRID_Y     (UIK_Y + UIK_READ_H + 8)          /* 166 */
/* 166 + 4*59 + 3*6 = 420, o sea justo dentro de los 421 de la zona. */

#define UIK_HIT_NONE   (-1)

/*
 * Que ES cada tecla, solo para pintarla. La accion la decide quien la usa.
 *
 *   NADA     hueco, no se dibuja ni responde
 *   DIGITO   0-9 y el punto: el grueso del teclado, en tono neutro
 *   BORRA    quitar uno / quitarlo todo: ambar, porque destruyen algo
 *   ACEPTA   la que aplica lo escrito: acento, una sola por pantalla
 *   VUELVE   salir sin aplicar
 */
typedef enum {
    UIK_NADA = 0,
    UIK_DIGITO,
    UIK_BORRA,
    UIK_ACEPTA,
    UIK_VUELVE
} ui_kbd_tipo_t;

typedef struct {
    const char   *titulo;              /* "Frecuencia", "Hora" */
    const char   *lectura;             /* ya formateada; "" = aun no hay nada */
    const char   *unidad;              /* "kHz" a la derecha de la lectura, o 0 */
    const char   *pista;               /* renglon de ayuda bajo el titulo, o 0 */
    const char   *tecla[UIK_KEYS];     /* rotulo de cada celda */
    uint8_t       tipo[UIK_KEYS];      /* ui_kbd_tipo_t */
    int8_t        pressed;             /* -1 = ninguna */
    int8_t        cursor;              /* celda del mando, -1 = ninguna */
} ui_kbd_state_t;

void   ui_kbd_draw(const ui_kbd_state_t *st);
void   ui_kbd_draw_one(const ui_kbd_state_t *st, int8_t i);
void   ui_kbd_draw_lectura(const ui_kbd_state_t *st);  /* solo la franja de arriba */
int8_t ui_kbd_hit(uint16_t x, uint16_t y);

#endif /* UI_KBD_H_INCLUDED */

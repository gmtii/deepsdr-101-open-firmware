#ifndef UI_HORA_H_INCLUDED
#define UI_HORA_H_INCLUDED

#include <stdint.h>

/*
 * PANTALLA DE SINCRONIZAR LA HORA POR DCF77 - 23/09/2026.
 *
 * Como el resto de gfx2, aqui no se sabe nada de DCF77: llegan textos ya
 * redactados y dos numeros de 0 a 100 para las barras. Quien decide que pone
 * es main.c, que es quien habla con el decodificador.
 *
 * LA DECISION DE DISENO QUE IMPORTA: las dos barras.
 *
 * Una pantalla que solo dijera "sincronizando..." y luego "no se ha podido"
 * seria inutil, porque deja al usuario sin saber si el problema es que no
 * llega señal -antena, hora del dia, sitio- o es que el firmware falla. Y
 * son dos problemas que se arreglan en sitios muy distintos.
 *
 * Asi que se ensena lo que el decodificador esta VIENDO:
 *
 *   NIVEL     cuanta portadora hay en 77,5 kHz. Si esto esta pegado al
 *             suelo, no hay señal y no hay nada que decodificar: es la
 *             antena o la propagacion, no el programa.
 *
 *   MARCA     cuanto baja la portadora en cada segundo, que es donde vive
 *             la informacion. Puede haber nivel de sobra y marca nula: eso
 *             es ruido de banda ancha, o una portadora que no es la del
 *             DCF77. Con la señal buena esta barra late una vez por segundo.
 *
 * Con esas dos, en diez segundos se sabe si merece la pena esperar los tres
 * minutos que tarda una sincronizacion.
 */

#define UIH_Y      104
#define UIH_H      318                  /* 104..421, igual que la rejilla */

#define UIH_BTN_H   56
#define UIH_BTN_W  240
#define UIH_BTN_Y  (UIH_Y + UIH_H - UIH_BTN_H - 12)

#define UIH_HIT_NONE     (-1)
#define UIH_HIT_EMISORA    0
#define UIH_HIT_APLICAR    1
#define UIH_HIT_SALIR      2
#define UIH_BTN_N          3

typedef struct {
    const char *titulo;
    const char *estado;    /* "Buscando la señal", "Leyendo 23/59"... */
    const char *detalle;   /* segunda linea, mas pequena, o 0 */
    const char *hora;      /* "20:33" cuando la haya, o 0 */
    const char *fecha;     /* "lunes 15/06/26", o 0 */
    uint8_t     nivel;     /* 0..100, barra de portadora */
    uint8_t     marca;     /* 0..100, barra de profundidad de la marca */
    uint8_t     latido;    /* 1 mientras la portadora esta caida: la barra
                            * de marca se enciende, y eso es lo que se ve
                            * latir una vez por segundo con señal buena */
    const char *emisora;   /* rotulo del boton que cambia de emisora */
    uint8_t     puede_aplicar; /* 1 = el boton "Aplicar" esta vivo */
    int8_t      pressed;   /* UIH_HIT_*, o -1 */
} ui_hora_state_t;

void   ui_hora_draw(const ui_hora_state_t *st);
int8_t ui_hora_hit(uint16_t x, uint16_t y);

#endif /* UI_HORA_H_INCLUDED */

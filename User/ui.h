#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "gfx.h"

/*
 * Base de widgets, construida encima de gfx.c (sin framebuffer, redibuja
 * directo a GRAM). Preparada para tactil resistivo aunque todavia no
 * este soportado en hardware:
 *
 *   - Los widgets interactivos (por ahora solo ui_button_t) tienen
 *     estado propio (pressed) y un callback de eventos, en vez de ser
 *     solo "algo que se dibuja".
 *   - ui_screen_t es un registro de widgets de una pantalla. Cuando
 *     exista el driver de tactil (touch.c, pendiente - primero hay que
 *     ver si el panel tiene pin de IRQ o hay que hacer polling puro),
 *     lo unico que hara falta es llamar a ui_screen_touch(x, y, pressed)
 *     con cada lectura/evento. Esta funcion no asume nada sobre COMO se
 *     obtienen esas lecturas (polling en el bucle principal, timer, o
 *     flag desde una ISR de pin de IRQ que se consume fuera de la ISR).
 *   - El dispatcher solo redibuja el widget cuyo estado visual cambia
 *     (no la pantalla entera) porque cada redibujado es una ventana EXMC
 *     nueva - repintar todo en cada evento de touch seria caro.
 *
 * Todo con arrays estaticos de tamano fijo (UI_SCREEN_MAX_WIDGETS) para
 * no meter malloc/heap dinamico en un sistema con la RAM ya ajustada.
 */

#define UI_SCREEN_MAX_WIDGETS 16

/* Eventos que puede recibir el callback de un widget interactivo.
 * Modelo de un boton tactil tipico:
 *   - dedo baja dentro del boton      -> UI_EVENT_PRESS
 *   - dedo sube SIN haber salido      -> UI_EVENT_RELEASE (esto es "el click")
 *   - dedo sale del area sin soltar,
 *     o suelta fuera del area         -> UI_EVENT_CANCEL (no cuenta como click)
 */
typedef enum {
    UI_EVENT_PRESS,
    UI_EVENT_RELEASE,
    UI_EVENT_CANCEL,
} ui_event_t;

/* widget: puntero al ui_button_t (u otro widget interactivo futuro) que
 * genero el evento, para que un callback compartido entre varios botones
 * pueda distinguir cual fue. user_data es lo que se paso al registrar el
 * widget (p.ej. un id de pantalla o un puntero a estado de la app). */
typedef void (*ui_callback_t)(void *widget, ui_event_t event, void *user_data);

typedef struct {
    uint16_t x, y, w, h;
    uint16_t bg;
    uint16_t border; /* usar bg para "sin borde" */
    /* 1 = registrado en la pantalla (por su geometria y su orden) pero NO se
     * pinta. Mismo motivo y mismas reglas que ui_button_t::hidden: la franja
     * de arriba la dibuja ui_top.c, y el relleno plano de estos dos paneles
     * era un repintado de 800x64 + 800x40 encima de la cabecera nueva en cada
     * redibujado completo. VA AL FINAL: los inicializadores de main.c son
     * posicionales. */
    uint8_t  hidden;
} ui_panel_t;

typedef struct {
    uint16_t x, y;
    const char *text;
    uint16_t fg;
    uint16_t bg;
    uint8_t  text_scale;
} ui_label_t;

typedef struct ui_button_s {
    uint16_t x, y, w, h;
    const char *label;
    uint16_t fg;
    uint16_t bg;
    uint16_t border;
    uint8_t  text_scale;
    uint8_t  pressed;  /* estado visual actual, lo gestiona ui_screen_touch() */
    uint8_t  enabled;  /* 0 = no reacciona a toques (se sigue pintando, ver ui_button_draw) */
    ui_callback_t on_event; /* NULL si no se quiere callback (widget solo visual) */
    void *user_data;
    /*
     * 1 = NO se pinta, pero SIGUE siendo tocable. Para botones cuyo aspecto
     * lo dibuja ahora otro modulo (ui_top.c) y de los que solo interesa
     * conservar la zona de toque: sin esto, ui_button_draw() rellenaba su
     * rectangulo con su color de fondo - un recuadro cian - encima de la
     * barra de estado nueva cada vez que el boton se pulsaba o refrescaba.
     *
     * VA AL FINAL A PROPOSITO: los 43 inicializadores de este tipo en
     * main.c son POSICIONALES, asi que meter el campo en medio rompia todos
     * menos los dos que lo necesitaban. Al final, los que no lo mencionan
     * simplemente lo dejan a 0.
     */
    uint8_t  hidden;
} ui_button_t;

/* --- Dibujo de widgets sueltos (sin pasar por ui_screen_t) --- */
void ui_panel_draw(const ui_panel_t *panel);

/* --- Registro de pantalla + despacho de toques --- */
typedef enum {
    UI_WIDGET_PANEL,
    UI_WIDGET_LABEL,
    UI_WIDGET_BUTTON,
} ui_widget_type_t;

typedef struct {
    ui_widget_type_t type;
    void *widget; /* ui_panel_t* / ui_label_t* / ui_button_t* segun type */
} ui_widget_ref_t;

typedef struct {
    ui_widget_ref_t widgets[UI_SCREEN_MAX_WIDGETS];
    uint8_t count;
    int8_t  active_index; /* indice en widgets[] del boton bajo el dedo desde
                              el ultimo PRESS, o -1 si no hay ninguno */
} ui_screen_t;


/* Registran el widget (por puntero, el screen NO se queda con una copia)
 * y lo pintan en orden de insercion (los añadidos despues quedan
 * "encima" a la hora de hacer hit-test si se solapan). Devuelven 1 si se
 * pudo añadir, 0 si el screen ya esta lleno (UI_SCREEN_MAX_WIDGETS). */

/* Dibuja todos los widgets registrados, en orden de insercion. Llamar
 * una vez al construir la pantalla (equivalente a lo que hacia
 * demo_screen_draw() pintando cada widget a mano). */

/* Despacha un evento de toque a los botones registrados. x,y en
 * coordenadas de pantalla (mismas unidades que gfx.c). pressed=1
 * mientras el dedo sigue en contacto (llamar en cada lectura, no solo en
 * flancos), pressed=0 en el evento de "dedo levantado". Gestiona el
 * estado PRESS/RELEASE/CANCEL descrito arriba y solo redibuja el boton
 * cuyo estado visual cambia. */

#endif /* UI_H */


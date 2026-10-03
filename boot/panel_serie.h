/*
 * La puesta en marcha del panel, copiada del cargador de serie.
 * Ver la cabecera de panel_serie.c, que cuenta de donde sale.
 */
#ifndef PANEL_SERIE_H
#define PANEL_SERIE_H

#include <stdint.h>

typedef struct {
    uint16_t cmd;
    uint8_t  dat;
    uint8_t  tipo;   /* 0 comando+dato, 1 solo comando, 2 esperar ms, 3 tabla de color */
} paso_t;

/* Lee los dos identificadores del panel, elige rama como el de serie y la
 * reproduce. Hay que haber hecho antes el gpio/bus del EXMC y el reset. */
void panel_serie_init(void);

/* Lo que se leyo y por donde fue, para poder enseñarlo en pantalla. */
extern uint16_t g_panel_id_0a;
extern uint16_t g_panel_id_3a;
extern uint8_t  g_panel_rama;

#endif

/*
 * La puesta en marcha del panel, copiada del cargador de serie.
 * Ver la cabecera de panel_serie.c, que cuenta de donde sale.
 */
#ifndef PANEL_SERIE_H
#define PANEL_SERIE_H

#include <stdint.h>

/* paso_t vive en panel_tablas.h, que lo genera tools/captura_panel.py. */

/* Lee los dos identificadores del panel, elige rama como el de serie y la
 * reproduce. Hay que haber hecho antes el gpio/bus del EXMC y el reset. */
void panel_serie_init(void);

/* Lo que se leyo y por donde fue, para poder enseñarlo en pantalla. */
extern uint16_t g_panel_id_0a;
extern uint16_t g_panel_id_3a;
extern uint8_t  g_panel_rama;
/* 1 si los dos identificadores salieron iguales en todas las lecturas. */
extern uint8_t  g_panel_firme;

/* Ponlo a 'A', 'B' o 'C' ANTES de panel_serie_init() para arrancar con esa
 * rama en vez de con la que salga de los identificadores. Lo usa la pantalla
 * de prueba, que reinicia la radio para que lo que mide sea un arranque de
 * verdad y no un rearranque. 0 = elegir sola. */
extern uint8_t g_panel_forzar;

#endif

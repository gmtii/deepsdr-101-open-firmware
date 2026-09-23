#include "splash_screen.h"
#include "splash.h"

/*
 * Pantalla de arranque.
 *
 * Lo que habia aqui eran cuatro lineas de texto con la fuente 5x7, EN
 * MAYUSCULAS porque esa fuente no tiene minusculas legibles. Lo que hay ahora
 * es la lluvia de codigo verde y el titulo, y vive en splash.c - este fichero
 * se queda solo como el enganche con main(), que llama a
 * splash_screen_draw() y no tiene por que enterarse de nada mas.
 *
 * El reloj se le PASA a splash_run() en vez de que lo lea por su cuenta: asi
 * splash.c no depende de g_msticks y el simulador de host puede compilarlo
 * tal cual, que es lo que permite ver la animacion sin la placa delante.
 */
static uint32_t ahora_ms(void)
{
    extern volatile uint32_t g_msticks;
    return g_msticks;
}

void splash_screen_draw(void)
{
    splash_run(ahora_ms);
}

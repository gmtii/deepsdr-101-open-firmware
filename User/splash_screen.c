#include "splash_screen.h"
#include "splash.h"
#include "waterfall.h"
#include "ft8_shared_ram.h"

/*
 * Pantalla de arranque.
 *
 * Lo que habia aqui eran cuatro lineas de texto con la fuente 5x7, EN
 * MAYUSCULAS porque esa fuente no tiene minusculas legibles. Lo que hay ahora
 * es la lluvia de codigo y el titulo, y vive en splash.c - este fichero se
 * queda como el enganche con main(), que llama a splash_screen_draw() y no
 * tiene por que enterarse de nada mas.
 *
 * El reloj se le PASA a splash_run() en vez de que lo lea por su cuenta: asi
 * splash.c no depende de g_msticks y el simulador de host puede compilarlo
 * tal cual, que es lo que permite ver la animacion sin la placa delante.
 *
 * Y LA MEMORIA TAMBIEN SE LE PASA - 28/09/2026
 * --------------------------------------------
 * Las 44 columnas de la lluvia son 396 bytes y en .bss quedaban 196. Aqui se
 * le prestan del buffer de la cascada, que en este momento del arranque
 * cumple las tres condiciones que hacen que prestarlo sea seguro:
 *
 *   1. waterfall_init() ya ha corrido (main.c lo llama cuatrocientas lineas
 *      antes que a esto), asi que el buffer existe y esta a cero;
 *   2. nadie ha empujado todavia una sola linea de cascada, porque la radio
 *      no se pinta hasta que esto vuelve;
 *   3. y mientras esta prestado, waterfall_push_line() y waterfall_blit() no
 *      hacen nada - lo comprueba el dueño del buffer, no una regla que haya
 *      que acordarse de cumplir-. Ver waterfall.h.
 *
 * Se devuelve llamando otra vez a waterfall_init(), que es lo unico que deja
 * el buffer con pixeles de verdad; sin eso, la primera cascada saldria con
 * los restos de la lluvia pintados en la esquina.
 *
 * Si algun dia la union se hiciera mas pequeña que lo que pide la lluvia,
 * splash.c se queda con la pantalla fija en vez de salirse de la tabla: por
 * eso se le pasa tambien el tamaño y no solo el puntero.
 */
static uint32_t ahora_ms(void)
{
    extern volatile uint32_t g_msticks;
    return g_msticks;
}

void splash_screen_draw(void)
{
    waterfall_presta(1U);
    splash_run(ahora_ms, &g_ft8_shared_ram, (uint32_t)sizeof g_ft8_shared_ram);
    waterfall_presta(0U);
    waterfall_init();   /* devolverlo es dejarlo con pixeles otra vez */
}

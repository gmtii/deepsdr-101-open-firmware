#ifndef WATERFALL_H
#define WATERFALL_H

#include <stdint.h>

/*
 * Framebuffer parcial en RAM para la zona de waterfall (y, mas adelante,
 * espectro si se decide usar el mismo mecanismo para su traza).
 *
 * POR QUE UN BUFFER PARCIAL Y NO UN FRAMEBUFFER COMPLETO:
 *   480 * 800 * 2 bytes = 750KB, muy por encima de los 192KB de RAM
 *   principal (mas 64KB de TCM, ya casi agotados por pila/heap segun el
 *   linker script). Un framebuffer parcial de solo la altura visible del
 *   waterfall si cabe, y es la unica forma de hacer scroll vertical barato
 *   (mover filas en RAM es memmove; hacerlo directo en GRAM via EXMC
 *   implicaria releer+reescribir toda la ventana en cada frame).
 *
 * PRESUPUESTO DE RAM (actualizado 01/09/2026 - ver el propio comentario
 * de WATERFALL_WIDTH sobre el traslado de buffers DSP/FFT/espectro a
 * TCM RAM, lo que hizo viable este ensanche):
 *   tamano_buffer = WATERFALL_WIDTH * WATERFALL_ROWS * 1 byte
 *   Con los valores de HOY (756 x 72 x 1): 54.432 bytes.
 *
 *   ESTE PARRAFO DECIA 114.624 (~112 KB) Y ERA FALSO POR PARTIDA DOBLE,
 *   corregido el 28/09/2026: el ancho es 756 desde la etapa 3b -lo dice
 *   WATERFALL_WIDTH veinte lineas mas abajo- y se guarda UN byte por
 *   celda, no dos, porque el historial son indices de paleta -lo dice el
 *   bloque "CAMBIO DE ALMACENAMIENTO" cincuenta lineas mas abajo-. O sea
 *   que los dos datos que hacian falta para corregirlo ya estaban en este
 *   mismo fichero; lo que faltaba era rehacer la multiplicacion.
 *
 *   El numero de verdad se puede leer del binario sin creerse a nadie:
 *       arm-none-eabi-nm --print-size build/firmware.elf | grep shared_ram
 *       -> 0000d4a0 = 54.432
 *
 * MODELO DE SCROLL: el buffer se trata como un anillo logico simple:
 *   - waterfall_push_line() desplaza todas las filas una posicion hacia
 *     "abajo" (memmove) y escribe la fila nueva en la posicion 0 (arriba),
 *     de forma que las señales nuevas entran por arriba y bajan, que es
 *     la convencion habitual en waterfalls de SDR.
 *   - waterfall_blit() vuelca el buffer completo a la GRAM en una sola
 *     ventana EXMC (gfx_blit), en la posicion de pantalla indicada.
 *
 * El "colormap" (dB -> RGB565) NO esta aqui: waterfall_push_line() recibe
 * ya un array de uint16_t en RGB565, para no acoplar este modulo a una
 * escala de color concreta. La conversion FFT bin -> RGB565 se hara en la
 * capa de SDR/DSP mas adelante.
 */

/* ETAPA 3b: 796 -> 756. Los 40 px de la izquierda son ahora la canaleta de
 * los ejes (ver spec_chrome.h), que espectro y waterfall comparten para que
 * las dos vistas sigan empezando en la misma columna. 756 es divisible por
 * 4, requisito del marcador de +Fs/4. De paso libera 3 KB de RAM. */
#define WATERFALL_WIDTH  756  /* main display column width - full screen (800) minus a
                                  4px panel border (2px each side), see main.c's
                                  radio layout constants. Was 672 (screen minus a
                                  124px right-hand status column) until 01/09/2026,
                                  when that column moved into a horizontal strip
                                  under the top bar instead, freeing the full width
                                  for spectrum+waterfall - see main.c's
                                  STATUS_STRIP_Y/H comment. Only feasible RAM-wise
                                  after moving a set of CPU-only DSP/FFT/spectrum
                                  working buffers into TCM RAM (see fft.c's
                                  TCMRAM_BSS comment) to make room for this
                                  buffer's own ~18KB main-RAM growth. */
#define WATERFALL_ROWS   72    /* filas visibles simultaneamente, ver presupuesto de RAM arriba
                                  (672*72*2 = ~97KB - same budget as the old 800*60*2) */

/* Reserva el buffer (en .bss, RAM principal) y lo pone a negro. Llamar
 * una vez al arrancar, antes del primer waterfall_push_line(). */
void waterfall_init(void);

/* Mete `line` (WATERFALL_WIDTH pixeles RGB565) como fila superior nueva
 * y "desplaza" el resto. Desde el rediseño en anillo (30/07/2026) es
 * O(WATERFALL_WIDTH): solo mueve un indice y copia la fila nueva, sin
 * memmove del buffer completo. */
/*
 * CAMBIO DE ALMACENAMIENTO: el historial guarda INDICES de colormap de 8
 * bits, no RGB565 de 16. El color se aplica al volcar, atravesando la LUT
 * que devuelve spectrum_colormap_lut().
 *
 * Por que: 796 x 72 x 2 = 112 KB era la mayor reserva de RAM del firmware
 * con diferencia. A 1 byte por celda son 56 KB, y esos 56 KB son los que
 * pagan la banda compositora de gfx2 y aun sobran.
 *
 * Y de regalo, cambiar de paleta ahora repinta TODO el historial en vez de
 * solo las filas nuevas.
 */
void waterfall_push_line(const uint8_t *line);

/*
 * EL BUFFER ESTA PRESTADO. 25/09/2026.
 *
 * La cascada comparte sus bytes con el area de trabajo de FT8 (ver
 * ft8_shared_ram.h y el comentario largo de waterfall.c). Mientras FT8
 * decodifica, esos bytes son magnitudes, no pixeles: pintarlos saca basura
 * en pantalla Y, lo que es peor, escribir una linea nueva le corrompe los
 * datos al decodificador. Las dos cosas en silencio.
 *
 * Esto estuvo escrito como una REGLA -"cualquier modo que quiera pintar
 * cascada tiene que mirar antes si FT8 esta en marcha"-, y una regla que hay
 * que acordarse de cumplir en cada sitio nuevo es una regla que un dia no se
 * cumple. Ahora lo comprueba el duenno del buffer: mientras este prestado,
 * waterfall_push_line() y waterfall_blit() no hacen nada. El que lo pide
 * -ft8_modo.c- es tambien el que lo devuelve, y devolverlo pasa por
 * waterfall_init(), que es lo unico que deja el buffer con pixeles otra vez.
 */
void    waterfall_presta(uint8_t si);
uint8_t waterfall_prestada(void);

/* Vuelca el buffer completo a la GRAM en (x,y). No hace falta llamarlo en
 * cada push_line si se prefiere desacoplar tasa de actualizacion de datos
 * vs. tasa de refresco de pantalla. */
void waterfall_blit(uint16_t x, uint16_t y, const uint16_t *lut);

/* Acceso directo a una fila del buffer (0 = mas reciente/arriba), por si
 * se necesita pintar encima (cursores, marcadores de frecuencia, etc.)
 * sin pasar por waterfall_push_line(). NULL si row fuera de rango. */
uint8_t *waterfall_row(uint16_t row);

#endif /* WATERFALL_H */

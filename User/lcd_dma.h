#ifndef LCD_DMA_H
#define LCD_DMA_H

#include <stdint.h>

/*
 * EMPUJAR PIXELES A LA PANTALLA SIN LA CPU. 25/09/2026.
 *
 * El panel cuelga de un bus paralelo mapeado en memoria: escribir un pixel
 * es guardar 16 bits en una direccion FIJA (0x60020000). Un ciclo dura unos
 * 45 ns, y medido en la radio, empujar el espectro se lleva 10 ms de cada
 * fotograma con la CPU parada esperando al bus.
 *
 * Un DMA en modo memoria-a-memoria con el incremento de destino APAGADO
 * hace exactamente eso: lee el buffer avanzando y escribe siempre en la
 * misma direccion. El bus tarda lo mismo -eso no lo arregla nadie- pero la
 * CPU se queda libre mientras tanto, y puede ir montando la banda siguiente.
 * Ahi esta la ganancia: no en el bus, en el solape.
 *
 * DMA1 y no DMA0: el DMA0 lo usan el audio (canal 4) y la radio (canal 3), y
 * ademas en esta familia solo el DMA1 admite memoria-a-memoria. El DMA1
 * estaba entero sin usar.
 *
 * AUTOPRUEBA AL ARRANCAR. lcd_dma_init() hace una transferencia de mentira
 * -de un buffer a otro de RAM, con el destino fijo, la misma configuracion
 * exacta que luego se usa de verdad- y comprueba el resultado. Si algo no
 * cuadra, el modulo se queda apagado y todo sigue por el camino de la CPU.
 * Se hace asi a proposito: probar la configuracion contra el PANEL seria
 * descubrir que esta mal viendo la pantalla llena de basura, y el que lo
 * descubriria estaria durmiendo.
 *
 * Y se puede apagar a mano desde la ventana de informacion, que es como se
 * compara de verdad.
 */

/* Enciende el reloj del DMA1, configura el canal y hace la autoprueba. Una
 * vez, al arrancar. Si la autoprueba falla, lcd_dma_hay() devuelve 0. */
void lcd_dma_init(void);

/* 1 si el empuje por DMA esta disponible y encendido. */
uint8_t lcd_dma_hay(void);

/* Enciende (1) o apaga (0). */
void lcd_dma_pon(uint8_t on);

/* Arranca el envio de n pixeles de 16 bits desde px al puerto de datos del
 * panel. NO espera: vuelve enseguida para que el que llama pueda hacer otra
 * cosa. Devuelve 1 si arranco, 0 si no (apagado, buffer fuera de la SRAM,
 * n fuera de rango, u otro envio todavia en marcha).
 *
 * `px` tiene que seguir INTACTO hasta que lcd_dma_espera() devuelva.
 *
 * OJO: mientras esto esta en marcha NO se puede tocar el panel por ningun
 * otro sitio -ni un set_window, ni un pixel suelto-. El bus es uno. */
uint8_t lcd_dma_manda(const uint16_t *px, uint32_t n);

/* Espera a que termine el envio en curso. Devuelve 1 si termino bien, 0 si
 * se acabo el tiempo (y entonces deja el modulo apagado, porque un DMA que
 * no termina no va a empezar a terminar). Si no habia nada en marcha,
 * devuelve 1 sin hacer nada. */
uint8_t lcd_dma_espera(void);

/* Veces que hubo que rendirse desde el arranque. Deberia ser cero. */
uint16_t lcd_dma_fallos(void);

#endif /* LCD_DMA_H */

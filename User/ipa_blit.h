#ifndef IPA_BLIT_H
#define IPA_BLIT_H

#include <stdint.h>

/*
 * VOLCADO DE LA CASCADA CON EL IPA. 24/09/2026.
 *
 * El GD32F450 lleva un acelerador grafico llamado IPA (el mismo bloque que
 * en los STM32 se llama DMA2D). Entre otras cosas sabe hacer EXACTAMENTE lo
 * que hace el primer bucle de gfx2_wf_blit():
 *
 *     for (i = 0; i < w; i++) dp[i] = cmap[sp[i]];
 *
 * o sea, leer un byte que es un INDICE DE PALETA, buscarlo en una tabla de
 * 256 colores y escribir el color en RGB565. En el lenguaje del fabricante
 * eso es formato de origen L8, con tabla de consulta, y destino RGB565.
 *
 * LO QUE NO PUEDE HACER, dicho aqui para que nadie lo busque: pintar la
 * pantalla. El panel RM68120 no tiene un framebuffer en nuestra RAM - la
 * imagen vive dentro del panel y se le mete pixel a pixel por una direccion
 * fija del bus (0x60020000). El IPA escribe en memoria normal avanzando la
 * direccion, asi que ese segundo tramo no es suyo. De eso se encarga un DMA
 * con el incremento de destino apagado, que es otra cosa y va aparte.
 *
 * OTRA COSA QUE NO PUEDE: leer el TCM. El IPA es un maestro del bus AHB y
 * el TCM solo lo ve el nucleo. Origen y destino tienen que estar en la SRAM
 * principal. Los nuestros lo estan, pero se comprueba en cada llamada en
 * vez de confiar: si algun dia alguien mueve un buffer al TCM, esto se cae
 * con elegancia al camino de siempre en vez de sacar basura en pantalla.
 *
 * SE PUEDE APAGAR. ipa_blit_pon(0) devuelve el volcado al bucle de la CPU.
 * No es un adorno: es la unica forma de comparar de verdad, mirando los
 * milisegundos de la ventana de informacion con y sin el, en la misma
 * radio y con la misma senal.
 */

/* Enciende el reloj del IPA y lo deja en un estado conocido. Una vez, al
 * arrancar. Si el IPA no responde, deja el modulo apagado y ipa_blit_hay()
 * devuelve 0 - el resto del firmware sigue funcionando igual. */
void ipa_blit_init(void);

/* 1 si el volcado por IPA esta encendido y utilizable. */
uint8_t ipa_blit_hay(void);

/* Enciende (1) o apaga (0) el volcado por IPA. */
void ipa_blit_pon(uint8_t on);

/* Convierte h filas de w indices de paleta a RGB565.
 *
 *   src        origen, un byte por pixel (indice en cmap)
 *   src_salto  bytes de una fila a la siguiente en el origen
 *   dst        destino, RGB565
 *   dst_salto  pixeles de una fila a la siguiente en el destino
 *   cmap       256 colores RGB565
 *
 * Devuelve 1 si el IPA lo hizo, 0 si no pudo (apagado, buffers fuera de la
 * SRAM, o se acabo el tiempo de espera). Cuando devuelve 0 NO ha escrito
 * nada util y el que llama tiene que hacerlo por su cuenta. */
uint8_t ipa_blit_l8(const uint8_t *src, uint16_t src_salto,
                    uint16_t *dst, uint16_t dst_salto,
                    uint16_t w, uint16_t h, const uint16_t *cmap);

/* Veces que ipa_blit_l8() tuvo que rendirse desde el arranque. Deberia ser
 * cero; si no lo es, sale en la ventana de informacion. */
uint16_t ipa_blit_fallos(void);

#endif /* IPA_BLIT_H */

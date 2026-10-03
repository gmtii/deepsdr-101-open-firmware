#ifndef BMP_H
#define BMP_H

#include <stdint.h>

/*
 * Cabeceras de BMP para guardar las fotos en el pendrive. Etapa 39b,
 * 24/09/2026.
 *
 * POR QUE LA CABECERA OCUPA UN BLOQUE ENTERO
 * ------------------------------------------
 * El fichero se escribe por spi_flash_anade_trozo(), que anade de 4096 en
 * 4096 porque ese es el bloque de borrado del chip. Si la cabecera midiera
 * sus 14+40 bytes de verdad, el tamano total no seria multiplo de 4096 y
 * el ultimo bloque quedaria a medias: o el fichero declara mas bytes de
 * los que tiene -y eso fsck lo llama corrupcion- o declara menos que los
 * clusters que ocupa -y eso tambien-.
 *
 * Estirandola a un bloque entero las cuentas salen exactas y gratis:
 *
 *   24 bits  320*256*3 = 245.760 = 60 bloques justos  -> 61 con cabecera
 *   16 bits  320*256*2 = 163.840 = 40 bloques justos  -> 41 con cabecera
 *
 * Lo que sobra de la cabecera se queda a cero y ningun visor lo mira:
 * bfOffBits dice donde empiezan los puntos, y eso es lo unico que leen.
 *
 * Y de propina queda una propiedad util para el fax, donde la altura no se
 * sabe hasta el tono de parada: la cabecera es un bloque SUYO, asi que se
 * puede dejar sin escribir y programarla al final sin tocar ningun punto.
 */

#define BMP_CAB_BYTES  4096U

/* Los dos formatos que ofrece el boton de la ventana de SSTV. */
#define BMP_24   24U   /* tres bytes por punto, copia directa de sstv_linea() */
#define BMP_16   16U   /* RGB565, la mitad de sitio */
#define BMP_8     8U   /* gris con paleta, para el fax - un byte por punto */

/*
 * Rellena `b` (BMP_CAB_BYTES bytes) con la cabecera de una imagen de
 * `ancho` x `alto` puntos. Devuelve cuanto medira el fichero entero, que
 * siempre es multiplo de 4096 si `ancho` lo permite.
 *
 * La imagen va DE ARRIBA ABAJO (altura negativa en la cabecera, que es
 * como se dice eso en BMP). Un BMP normal se guarda del reves, empezando
 * por la fila de abajo, y eso obligaria a tener la foto entera en memoria
 * antes de escribir nada - que es justo lo que no hay: el decodificador
 * entrega renglon a renglon y no guarda la imagen.
 */
uint32_t bmp_cabecera(uint8_t *b, uint16_t ancho, uint16_t alto, uint8_t bits);

/* Cuanto ocupa una fila en el fichero, con el relleno a multiplo de 4 que
 * exige el formato. */
uint32_t bmp_fila_bytes(uint16_t ancho, uint8_t bits);

/* Pasa `n` ternas RGB a RGB565 little-endian, que es lo que espera un BMP
 * de 16 bits con mascaras. */
void bmp_fila565(const uint8_t *rgb, uint8_t *out, uint16_t n);

/* Y a 24 bits, que NO es una copia: un BMP guarda azul, verde y rojo en
 * ese orden. Escribirlo como RGB da una foto con los colores cambiados
 * -el cielo naranja- y es el fallo clasico de esta cabecera. Vive aqui y
 * no en quien llama para que el banco pruebe ESTA conversion y no otra
 * suya que se le parezca. */
void bmp_fila24(const uint8_t *rgb, uint8_t *out, uint16_t n);

/*
 * Y a 8 bits no hay conversion NINGUNA: el fax entrega un byte de gris
 * por punto y un BMP de 8 bits guarda indices de paleta. Con la paleta
 * que pone bmp_cabecera() -una rampa donde el indice N es el gris N- el
 * byte del decodificador ES el byte del fichero.
 *
 * No existe bmp_fila8() a proposito: una funcion que copia bytes solo
 * sirve para que alguien se pregunte que hace y para que un dia alguien
 * le meta algo dentro.
 */

#endif /* BMP_H */

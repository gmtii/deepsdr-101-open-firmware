/*
 * cabecera_app.c - los 16 bytes por los que el cargador decide si esta
 * imagen vale.  Rama "reload", 02/10/2026.
 *
 * LO QUE SUSTITUYE, Y POR QUE NO ES LO MISMO CON OTRO NOMBRE.
 *
 * El gestor de fabrica miraba ocho bytes constantes en 0x08060000:
 *
 *     memcmp((void *)0x0800be2c, (void *)0x08060000, 8)
 *
 * y nada mas.  Ni longitud, ni checksum, ni CRC.  Eso tiene dos problemas,
 * y el segundo es el gordo:
 *
 *  1. Obligaba a dejar ocho bytes clavados en medio de la flash, que es de
 *     donde salian la region partida y el desvan.
 *
 *  2. Una imagen grabada A MEDIAS pasaba la prueba.  Si la copia se corta
 *     -se va la luz, el pendrive se suelta, la cadena de clusters esta mal
 *     a partir de la mitad- los primeros 400 kB estan escritos, esos ocho
 *     bytes entre ellos, y el gestor arranca tan contento una imagen
 *     incompleta.  Lo que se ve desde fuera es una radio que se cuelga o
 *     se reinicia sola, sin ninguna pista de que lo que falla es que le
 *     falta un trozo.
 *
 * Esta cabecera arregla las dos.  Lleva la longitud y un CRC32 de la
 * imagen entera, asi que "esta completa y no esta corrompida" pasa a ser
 * una pregunta que el cargador SABE responder, y la responde en cada
 * arranque (son unos 18 ms sobre 320 kB).
 *
 *   +0x00  magia     0x52445344, que en un volcado se lee "DSDR"
 *   +0x04  longitud  bytes de la imagen, contados desde _app_base
 *   +0x08  crc32     de esos bytes, contando ESTE campo como cero
 *   +0x0C  version   la del firmware, para que el cargador la enseñe
 *
 * LOS CUATRO SALEN DE AQUI A CERO, Y ESO ES CORRECTO.  La longitud y el
 * CRC no se pueden saber hasta que el enlazador ha terminado, asi que
 * quien los rellena es tools/cabecera.py sobre el .bin ya hecho, en la
 * receta de update4 del Makefile.  La magia y la version si se podrian
 * poner aqui, pero van por el mismo camino a proposito: asi hay UN solo
 * sitio que escribe la cabecera y un solo sitio donde mirar si algo no
 * cuadra.
 *
 * Que salga a cero tambien es la red de seguridad: un .bin que no haya
 * pasado por cabecera.py tiene magia 0 y el cargador lo rechaza en vez de
 * intentar arrancarlo.
 *
 * NO HAY NI UNA REFERENCIA a esta variable en todo el firmware, y es
 * correcto: solo la lee el cargador, y la lee por direccion absoluta.  La
 * mantienen viva el KEEP() del enlazador y el "used" de aqui.
 */

#include <stdint.h>

volatile const uint32_t g_cabecera_app[4]
    __attribute__((used, section(".cabecera"))) = { 0U, 0U, 0U, 0U };

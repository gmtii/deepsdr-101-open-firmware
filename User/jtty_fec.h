#ifndef JTTY_FEC_H
#define JTTY_FEC_H

#include <stdint.h>

/*
 * JTTY: LA CAPA DE CORRECCION DE ERRORES - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "tambien tienes que investigar jtty que
 * es parecido a rtty". ***
 *
 * QUE ES JTTY. Un modo nuevo del equipo de WSJT-X, estrenado en la
 * version 3.2.0-rc1 de SEPTIEMBRE DE 2026. Es 4-GFSK a 31,25 baudios en
 * 127 Hz, pensado para concursos: unos 60 palabras por minuto. Y tiene
 * una diferencia de fondo con FT8 y FT4 que cambia el receptor entero:
 *
 *     NO ESTA SINCRONIZADO CON UTC. Las emisiones empiezan cuando el
 *     otro le da a la tecla y duran lo que duren.
 *
 * O sea que no hay ranuras que esperar: hay que buscar continuamente.
 *
 * LA TRAMA son 59 simbolos: 13 de sincronismo y 46 de datos. Un mensaje
 * son de una a dieciseis tramas seguidas, cada una con su sincronismo, su
 * correccion y su CRC, y la ultima marcada con un bit.
 *
 * ESTE FICHERO ES SOLO LA CORRECCION DE ERRORES: de 34 bits de mensaje a
 * 46 tonos y al reves. Ni la forma de onda ni el texto estan aqui.
 *
 * DE DONDE SALEN LOS NUMEROS. Del arbol de WSJT-X, etiqueta v3.2.0-rc1,
 * en lib/jtty/ - `tbcc.f90`, `jtty_tbcc_code_profile.f90` y
 * `jtty_fec_mod.f90`-. No de deducirlos ni de parecerse a FT4: un vector
 * de sincronismo o un polinomio adivinados dan un decodificador que no
 * puede funcionar y que ademas no dice por que.
 *
 * Y NO SE COMPRUEBA CONTRA SI MISMO. El banco compila el FORTRAN DE
 * VERDAD con gfortran, le saca vectores, y compara. Despues de lo del
 * localizador de WSPR -donde codificador y decodificador compartian el
 * mismo error y la ida y vuelta salia limpia durante semanas- esa es la
 * unica forma de comprobacion que cuenta para un formato de otro.
 */

#define JTTY_CARGA_BITS   34U   /* lo que lleva el mensaje */
#define JTTY_CRC_BITS     12U
#define JTTY_INFO_BITS    46U   /* carga + CRC */
#define JTTY_SIMBOLOS     46U   /* tasa 1/2, dos bits por simbolo */
#define JTTY_SYNC_SIM     13U
#define JTTY_TRAMA_SIM    59U   /* 13 + 46 */

/* El bit 33 de la carga va SIEMPRE a cero. No es relleno: el
 * decodificador lo usa como comprobacion extra ademas del CRC, y eso
 * divide aproximadamente por dos los decodificados falsos. */
#define JTTY_BIT_RESERVADO 33U  /* contando desde 1, como en el estandar */

/*
 * EL VECTOR DE SINCRONISMO. Trece simbolos de cuatro tonos al principio
 * de cada trama. Uno solo, al principio, y no cuatro grupos repartidos
 * como en FT4.
 *
 * Su autocorrelacion tiene el lobulo lateral mas alto en 2/13, que es lo
 * que permite encontrar el principio de una trama que puede empezar en
 * cualquier instante.
 */
extern const uint8_t k_jtty_sync[JTTY_SYNC_SIM];

/*
 * De 34 bits de carga a 46 tonos (0..3).
 *
 * Por dentro: CRC-12 detras de la carga -> 46 bits de informacion ->
 * convolucional TAIL-BITING de tasa 1/2 y K=10 -> 92 bits -> 46 simbolos
 * de dos bits con codigo Gray (00->0, 01->1, 11->2, 10->3).
 *
 * "Tail-biting" quiere decir que el registro NO empieza a cero: se
 * precarga con los ultimos nueve bits del propio mensaje. Asi no hacen
 * falta bits de cola -que en una trama de 46 serian un 20% de
 * desperdicio- y el trellis se cierra sobre si mismo.
 */
void jtty_fec_codifica(const uint8_t carga[JTTY_CARGA_BITS],
                       uint8_t tonos[JTTY_SIMBOLOS]);

/* El CRC-12 de la carga. Polinomio 0x80F con el termino principal
 * implicito, o sea x^12 + x^11 + x^3 + x^2 + x + 1. Empieza a cero, de
 * mas peso a menos, sin relleno de ceros y sin XOR final. */
uint16_t jtty_fec_crc12(const uint8_t carga[JTTY_CARGA_BITS]);

/*
 * LA MEMORIA DE TRABAJO DEL DECODIFICADOR, que la pone quien llama.
 *
 * Son 512 estados -K=10- y eso no cabe en la SRAM que queda libre en esta
 * radio (308 bytes cuando se escribio esto). Tiene que salir de la
 * memoria que se reparten FT8, HFDL, WSPR, AIS y ALE, y por eso JTTY
 * entra en ese grupo: se paran todos y se arranca uno.
 *
 * Son unos 7 kB: dos juegos de metricas de 512 y una tabla de vuelta
 * atras de 46 x 512 bits.
 */
#define JTTY_ESTADOS 512U

typedef struct {
    float   m_ant[JTTY_ESTADOS];
    float   m_act[JTTY_ESTADOS];
    uint8_t atras[(JTTY_SIMBOLOS * JTTY_ESTADOS) / 8U];
} jtty_fec_ram_t;

/*
 * De 46 x 4 energias de tono a 34 bits de carga.
 *
 * Es un WAVA: un Viterbi que da DOS vueltas al trellis en vez de una,
 * porque el codigo es tail-biting y no se sabe en que estado empieza. La
 * primera vuelta deja las metricas en un sitio razonable y la segunda
 * decide.
 *
 * Y es de LISTA: se guardan los `lista` mejores caminos que cierran el
 * circulo, no solo el mejor, y se acepta el primero que cumple el CRC Y
 * el bit reservado. Con lista 1 se pierden decodificados que estan ahi;
 * con lista 4 -lo que usa el estandar- se recuperan.
 *
 * Devuelve 1 si sale un mensaje bueno.
 */
uint8_t jtty_fec_descodifica(const float energias[JTTY_SIMBOLOS][4],
                             jtty_fec_ram_t *ram, uint8_t lista,
                             uint8_t carga[JTTY_CARGA_BITS]);

#endif /* JTTY_FEC_H */

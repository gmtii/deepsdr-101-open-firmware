#ifndef USB_CDC_ANILLO_H
#define USB_CDC_ANILLO_H

#include <stdint.h>

/*
 * EL ANILLO ENTRE LA INTERRUPCION DEL USB Y EL BUCLE DE LA INTERFAZ.
 *
 * Aqui no hay nada del GD32 a proposito: este fichero compila en el
 * ordenador y el banco lo machaca (ver sim/anillotest.c). Lo de abajo es la
 * unica pieza del CDC que se puede medir sin tener la radio enchufada, y
 * tambien es donde este proyecto se ha roto mas veces: once averias de
 * estado compartido van ya.
 *
 * LA DISCIPLINA, QUE ES LO QUE LO HACE SEGURO
 * -------------------------------------------
 * Un productor y un consumidor, y cada uno escribe UNA SOLA variable:
 *
 *     el que mete   escribe `cabeza` y solo LEE `cola`
 *     el que saca   escribe `cola`   y solo LEE `cabeza`
 *
 * No hay contador de ocupacion. Un contador lo tendrian que tocar los dos,
 * y `n++` desde una interrupcion mientras el bucle hace `n--` pierde
 * cuentas: son leer-modificar-escribir y no son atomicas. Con dos indices
 * que no se pisan no hace falta desactivar interrupciones en ningun sitio,
 * y eso importa porque el USB interrumpe a 1 kHz.
 *
 * Los indices son `volatile` y de 16 bits: en un Cortex-M4 una lectura o
 * escritura alineada de 16 o 32 bits no se parte por la mitad, asi que el
 * otro lado siempre ve un valor entero, viejo o nuevo, nunca a medias.
 *
 * El tamaño es potencia de dos para que el ajuste sea una mascara y no una
 * division -se hace dentro de la interrupcion- y SE DESPERDICIA UN HUECO:
 * con cabeza == cola el anillo esta vacio, asi que lleno tiene que ser
 * "cabeza + 1 == cola". Sin ese hueco los dos estados se escriben igual y
 * un anillo lleno se lee como vacio, que es la forma clasica de perder 256
 * bytes de golpe sin que nada avise.
 */

/*
 * EL SITIO LO PONE QUIEN LO USA, Y NO ESTA AQUI DENTRO - 09/10/2026.
 *
 * Empezo con un array fijo de 256 bytes porque solo habia uno, el del CAT.
 * Con el audio hacen falta dos de tamaños muy distintos: el del puerto
 * serie mueve ordenes de una docena de bytes, y el del audio 192 bytes
 * CADA MILISEGUNDO, asi que necesita diez veces mas para aguantar un
 * fotograma lento sin dejar un hueco que se oye.
 *
 * La alternativa era escribir un segundo anillo, y eso es exactamente lo
 * que este proyecto no hace: serian dos implementaciones de lo mismo, una
 * medida por un banco con dos hilos y la otra no.
 *
 * Asi que el buffer lo pone el que lo declara y la mascara se guarda al
 * ponerlo. Sigue teniendo que ser POTENCIA DE DOS -el ajuste se hace dentro
 * de la interrupcion y tiene que ser una mascara, no una division- y eso lo
 * comprueba anillo_pon().
 */
typedef struct {
    volatile uint16_t cabeza;
    volatile uint16_t cola;
    uint16_t          masc;    /* tamaño - 1 */
    uint8_t          *dato;
} anillo_t;

/* Le da su sitio. `tam` tiene que ser potencia de dos y caber en 16 bits.
 * Devuelve 0 si no lo es, y entonces el anillo queda inutil -con cero
 * hueco- en vez de funcionando a medias. */
uint8_t  anillo_pon(anillo_t *a, uint8_t *buf, uint16_t tam);

void     anillo_reset(anillo_t *a);

/* Cuantos bytes hay por leer y cuantos caben. No son exactos si el otro
 * lado esta trabajando: son una foto, y por eso el que mete solo puede
 * fiarse de `anillo_hueco` y el que saca solo de `anillo_lleno`. */
uint16_t anillo_lleno(const anillo_t *a);
uint16_t anillo_hueco(const anillo_t *a);

/* Mete hasta `n`. Devuelve cuantos entraron: si no caben todos, mete los
 * que quepan y lo dice. NUNCA pisa lo que no se ha leido. */
uint16_t anillo_mete(anillo_t *a, const uint8_t *src, uint16_t n);

/* Saca hasta `n`. Devuelve cuantos salieron. */
uint16_t anillo_saca(anillo_t *a, uint8_t *dst, uint16_t n);

#endif /* USB_CDC_ANILLO_H */

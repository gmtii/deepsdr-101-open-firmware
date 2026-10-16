#ifndef WSPR_MSG_H
#define WSPR_MSG_H

/*
 * WSPR: la capa de mensaje. 27/09/2026.
 *
 * QUE ES WSPR. "Weak Signal Propagation Reporter": balizas de aficionado
 * que dicen tres cosas -quien soy, donde estoy y con cuanta potencia
 * transmito- y nada mas. Cada emision dura 110,6 segundos, empieza un
 * segundo despues de un minuto PAR, y ocupa 6 Hz. Se decodifica muy por
 * debajo del ruido: unos -28 dB de relacion señal/ruido en 2,5 kHz, que
 * son 30 dB peor de lo que hace falta para oir a alguien hablando.
 *
 * COMO VA. 50 bits de mensaje -28 de indicativo, 15 de localizador, 7 de
 * potencia-, un codigo convolucional K=32 de tasa 1/2 que los convierte
 * en 162, un entrelazado, y encima de cada bit un vector de sincronismo
 * conocido de 162 posiciones. Cada simbolo vale 0..3:
 *
 *     simbolo[i] = sincro[i] + 2 * dato[i]
 *
 * y se transmite como 4-FSK con los tonos separados 1,4648 Hz, que es
 * exactamente la velocidad de simbolo (12000/8192 Hz). Por eso una
 * emision entera son 162 * 8192/12000 = 110,6 s.
 *
 * DE DONDE SALEN LAS CONSTANTES. El vector de sincronismo, los polinomios
 * y las reglas de empaquetado son el protocolo, no una eleccion nuestra:
 * comprobados el 27/09/2026 contra una implementacion de referencia. El
 * CODIGO de aqui es nuestro -la referencia es GPL y este proyecto no-,
 * pero los numeros son los que son.
 *
 * ESTE FICHERO NO TOCA HARDWARE NI AUDIO. Bits para arriba y bits para
 * abajo, y por eso el simulador lo compila tal cual y lo prueba de punta
 * a punta: se codifica un mensaje conocido, se decodifica, y tiene que
 * salir el mismo. Ver sim/wspr_pruebas.c.
 */

#include <stdint.h>

#define WSPR_SIMBOLOS   162U   /* simbolos por emision */
#define WSPR_BITS_MSG    50U   /* bits utiles del mensaje */
#define WSPR_BITS_COLA   31U   /* ceros de cola: K-1 */
#define WSPR_BITS_ENTRA  (WSPR_BITS_MSG + WSPR_BITS_COLA)   /* 81 */

/* El vector de sincronismo, 162 posiciones de 0 o 1. */
extern const uint8_t k_wspr_sincro[WSPR_SIMBOLOS];

/* Un mensaje ya desmenuzado. */
typedef struct {
    char    indicativo[8];   /* "EA4XYZ", terminado en cero */
    char    locutor[5];      /* "IN80", terminado en cero */
    uint8_t dbm;             /* potencia en dBm, 0..60 */
} wspr_msg_t;

/*
 * Mensaje -> 50 bits, empaquetados en 7 bytes (los 6 bits altos del
 * ultimo sobran y van a cero). Devuelve 0 si el indicativo o el
 * localizador no tienen la forma que WSPR admite.
 */
uint8_t wspr_empaqueta(const wspr_msg_t *m, uint8_t bits[7]);

/* Y al reves. Devuelve 0 si los campos no tienen sentido. */
uint8_t wspr_desempaqueta(const uint8_t bits[7], wspr_msg_t *m);

/*
 * 50 bits -> los 162 simbolos que se transmiten, sincronismo incluido.
 * Es el camino de TRANSMISION, y esta aqui porque es lo que permite
 * probar el de recepcion sin una antena: se codifica, se decodifica y
 * tiene que salir lo mismo.
 */
void wspr_codifica(const uint8_t bits[7], uint8_t simbolos[WSPR_SIMBOLOS]);

/* El entrelazado, que es el mismo en los dos sentidos: la posicion j se
 * obtiene dando la vuelta a los 8 bits del indice y saltandose los que
 * se salen de 162. */
void wspr_entrelaza(const uint8_t ent[WSPR_SIMBOLOS], uint8_t sal[WSPR_SIMBOLOS],
                    uint8_t deshacer);

#endif /* WSPR_MSG_H */

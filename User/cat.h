#ifndef CAT_H
#define CAT_H

#include <stdint.h>

/*
 * CAT: QUE UN PROGRAMA DEL ORDENADOR MANEJE LA RADIO.
 *
 * *** De lo que publica la radio comercial de referencia sobre su modo
 * "audio digital + CAT": se ve a la vez como tarjeta de sonido y como un
 * transceptor Kenwood TS-2000, y asi funciona con WSJT-X, JTDX y hamlib sin
 * que nadie escriba un driver. ***
 *
 * POR QUE UN TS-2000 Y NO UN PROTOCOLO NUESTRO
 * --------------------------------------------
 * Porque un protocolo nuestro no lo habla nadie. El de Kenwood lo hablan
 * hamlib -y por debajo de hamlib, WSJT-X, JTDX, fldigi, gpredict, CQRLOG y
 * media aficion-, y ademas es ASCII plano:
 *
 *     FA00007110000;     pon el VFO A en 7.110,000 kHz
 *     FA;                ¿en que frecuencia estas?
 *     MD1;               pon LSB
 *     IF;                dame el bloque de estado entero
 *
 * Ordenes en texto, terminadas en punto y coma, sin binario, sin sumas de
 * comprobacion y sin estado entre ordenes. Eso lo hace facil de implementar
 * y, sobre todo, facil de PROBAR: el banco le mete el rigctl de hamlib de
 * verdad por delante en vez de mi lectura del manual.
 *
 * LO QUE NO HACE, Y SE DICE
 * -------------------------
 * Esto es un RECEPTOR. Todo lo que mande transmitir -TX, potencia, VOX,
 * manipulador- se contesta con "?;", que es como un Kenwood dice "esa no la
 * conozco". No se contesta con silencio: un programa al otro lado espera
 * respuesta y el silencio le cuesta un tiempo de espera de varios segundos
 * por orden, asi que callarse es peor que decir que no.
 *
 * SIN DEPENDENCIAS DEL RESTO DE LA RADIO
 * --------------------------------------
 * Aqui dentro no se incluye nada del firmware. Lo que hace falta de la
 * radio -leer y poner frecuencia, modo, volumen, el S y el ancho- entra por
 * una tabla de punteros, cat_radio_t, que la rellena main.c. Asi este
 * fichero compila tal cual en el ordenador y el banco mide el codigo DE
 * VERDAD, no una copia suya.
 */

/* Los modos, en numeracion de Kenwood, que es la que viaja por el cable. */
enum {
    CAT_MODO_LSB = 1,
    CAT_MODO_USB = 2,
    CAT_MODO_CW  = 3,
    CAT_MODO_FM  = 4,
    CAT_MODO_AM  = 5,
    CAT_MODO_FSK = 6,   /* RTTY */
    CAT_MODO_CWR = 7,
    CAT_MODO_FSKR = 9
};

typedef struct {
    uint32_t (*freq_lee)(void);          /* Hz */
    void     (*freq_pon)(uint32_t hz);
    uint8_t  (*modo_lee)(void);          /* CAT_MODO_* */
    void     (*modo_pon)(uint8_t modo);
    uint8_t  (*smeter_lee)(void);        /* 0..30, como un Kenwood */
    uint8_t  (*vol_lee)(void);           /* 0..255 */
    void     (*vol_pon)(uint8_t v);
    uint16_t (*ancho_lee)(void);         /* Hz */
} cat_radio_t;

/* Lo mas largo que puede contestar esto es el bloque IF: 37 + ';' + fin. */
#define CAT_SALIDA_MAX  48U

void cat_init(const cat_radio_t *radio);

/*
 * Mete un byte de los que llegan del ordenador. Mientras no llegue el punto
 * y coma devuelve 0 y no escribe nada. Con el punto y coma procesa la orden
 * y devuelve cuantos bytes hay que mandar de vuelta, escritos en `salida`
 * (que tiene que medir CAT_SALIDA_MAX).
 *
 * No hay respuesta parcial ni estado a medias: o hay orden entera o no hay
 * nada. Si llega mas basura de la que cabe en una orden, se tira y se
 * empieza de nuevo en el siguiente punto y coma.
 */
uint8_t cat_byte(char c, char *salida);

/* Para las pruebas y para el reinicio del puerto. */
void cat_reset(void);

#endif /* CAT_H */

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

/*
 * ======================================================================
 * LA VUELTA DEL BUCLE POR EL PUERTO, PARA NO MEDIR CON FOTOS - 02/11/2026
 * ======================================================================
 *
 * Seis versiones persiguiendo por que la cascada se frena al girar el
 * mando, y todas las medidas de la radio han llegado leyendo numeros de
 * una foto de la pantalla de Informacion. Eso tiene dos fallos: la fila
 * enseña una media o un record -nunca la vuelta a vuelta- y no se puede
 * mirar mientras se gira, porque en esa pantalla el mando mueve el cursor.
 *
 * Con esto la radio lo cuenta sola por el puerto CAT, que ya existe y ya
 * esta conectado: una orden "DG;" devuelve, de todas las vueltas QUE HAN
 * DIBUJADO desde la consulta anterior, la media de cada uno de los quince
 * tramos del bucle, cuantas vueltas fueron, cuantos clics de mando hubo
 * mientras tanto y la carga de audio. Preguntando dos veces por segundo
 * sale la pelicula entera, girando y sin girar, y no se pierde ni una
 * vuelta: son SUMAS, no muestras.
 *
 * No es una orden de Kenwood. Un Kenwood de verdad contestaria "?;" y
 * hamlib no la manda nunca, asi que no estorba a nadie.
 */
#define CAT_DIAG_TRAMOS  18U

typedef struct {
    uint16_t vueltas;                    /* vueltas con dibujo desde la ultima consulta */
    uint16_t detentes;                   /* clics del mando en ese mismo rato */
    /*
     * El reductor de ruido, encendido o no, porque es la otra mitad de la
     * pregunta: la carga de audio es el divisor del cuadro, y el NR son
     * ocho puntos de esa carga. Sin este bit no se pueden comparar dos
     * tandas -el que mira la tabla no sabe con que estaba puesto-, y con
     * el salen cuatro casillas: quieto/girando por NR si/no.
     */
    uint8_t  nr;                         /* 1 = reductor de ruido encendido */
    uint8_t  carga;                      /* carga de audio, % */
    uint32_t vuelta_us;                  /* media de la vuelta entera */
    uint32_t tramo_us[CAT_DIAG_TRAMOS];  /* media de cada tramo, en orden */
} cat_diag_t;

typedef struct {
    uint32_t (*freq_lee)(void);          /* Hz */
    void     (*freq_pon)(uint32_t hz);
    uint8_t  (*modo_lee)(void);          /* CAT_MODO_* */
    void     (*modo_pon)(uint8_t modo);
    uint8_t  (*smeter_lee)(void);        /* 0..30, como un Kenwood */
    uint8_t  (*vol_lee)(void);           /* 0..255 */
    void     (*vol_pon)(uint8_t v);
    uint16_t (*ancho_lee)(void);         /* Hz */
    void     (*diag_lee)(cat_diag_t *d); /* "DG": la vuelta del bucle */
} cat_radio_t;

/*
 * Lo mas largo que puede contestar esto ya no es el bloque IF -37- sino
 * "DG": 2 + 4 + 4 + 1 + 3 + 5 + 18 x 5 + ';' = 110. El buffer vive en la
 * pila de usb_cdc_poll(), y el margen de pila medido son 2.048 bytes.
 */
#define CAT_SALIDA_MAX  112U

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

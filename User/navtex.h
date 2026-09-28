#ifndef NAVTEX_H
#define NAVTEX_H

#include <stdint.h>

/*
 * NAVTEX (SITOR-B / NBDP). Etapa 25, 24/09/2026.
 *
 * Avisos a la navegacion en texto: temporales, balizas apagadas, ejercicios
 * militares, partes meteorologicos. Se emiten en 518 kHz (internacional, en
 * ingles), 490 kHz (en idioma nacional) y 4209,5 kHz.
 *
 * POR QUE NO SE APROVECHA rtty.c. Parece el mismo problema -dos tonos, FSK-
 * y no lo es. El RTTY es ASINCRONO: cada caracter llega con su bit de
 * arranque y su bit de parada, y el decodificador se resincroniza en cada
 * uno. El SITOR-B es SINCRONO: un chorro continuo de caracteres de 7 bits
 * sin ninguna marca, donde el sincronismo se saca de que el codigo tiene
 * SIEMPRE cuatro unos y tres ceros. Son dos maquinas distintas; lo unico que
 * comparten es medir cuanta energia hay en dos frecuencias, que es una
 * formula, no una decision.
 *
 * COMO FUNCIONA, de abajo arriba:
 *
 *   1. Dos osciladores (nco.c) bajan a banda base las frecuencias de marca y
 *      espacio. La energia de cada una se integra sobre UN BIT ENTERO con
 *      una media movil rectangular.
 *
 *      Esto es lo que hace que funcione y por eso esta asi. Integrando sobre
 *      los 32 muestras del bloque -como hace el RTTY- la resolucion son 375
 *      Hz y los dos tonos estan a 170 Hz: el filtro de marca ve el tono de
 *      espacio a solo 3 dB por debajo, o sea que casi no los distingue.
 *      Integrando sobre un bit entero (120 muestras a 12 kHz) la resolucion
 *      baja a 100 Hz y esa diafonia cae a 16 dB. Es la diferencia entre
 *      decodificar y no decodificar.
 *
 *   2. Un lazo de reloj de bit, igual en forma que el del RDS: acumulador de
 *      fase, detector de error de Gardner, y la correccion de frecuencia
 *      SIEMPRE calculada del nominal, nunca acumulada encima de si misma
 *      (ver rds.c para la version larga de por que).
 *
 *   3. Sincronismo de CARACTER. El alfabeto CCIR 476 solo usa las 35
 *      combinaciones de 7 bits que tienen exactamente cuatro unos, de las
 *      128 posibles. Asi que se prueban los 7 desplazamientos posibles y se
 *      cuenta en cual salen codigos validos: el bueno da casi el 100% y los
 *      otros el 27% que sale por azar. No hace falta ninguna marca en la
 *      señal.
 *
 *   4. Diversidad temporal. Cada caracter se manda DOS veces, la segunda 35
 *      bits (cinco posiciones de caracter) despues de la primera. Si la
 *      primera copia llego rota y la segunda no, se usa la segunda. Es
 *      correccion de errores gratis: un chasquido que se carga una copia no
 *      puede cargarse la otra, que va 350 ms mas tarde.
 *
 * LO QUE ES DEPENDIENTE DEL RECEPTOR, igual que en el RTTY: las frecuencias
 * de audio de los dos tonos. Dependen de en que banda lateral se escucha y
 * de donde se sintonice, asi que el centro es ajustable en caliente y hay un
 * conmutador de polaridad. El valor por defecto es el habitual del SITOR.
 */

/* El bloque que sirve demod_am.c, el mismo que el RTTY (32 muestras a
 * 12 kHz). Se comprueba con _Static_assert en navtex.c. */
#define NAVTEX_BLOCK_SAMPLES 32U

#define NAVTEX_BAUD       100.0f
#define NAVTEX_SHIFT_HZ   170.0f
#define NAVTEX_CENTRO_HZ  1700.0f   /* el centro habitual del SITOR */

/* Los cuatro codigos de control del CCIR 476 que hay que conocer por su
 * nombre. Los demas salen de la tabla. */
#define NAVTEX_COD_LTRS   0x5AU     /* a letras */
#define NAVTEX_COD_FIGS   0x36U     /* a cifras */
#define NAVTEX_COD_ALFA   0x0FU     /* relleno, "alfa" */
#define NAVTEX_COD_REP    0x66U     /* peticion de repeticion, "beta" */

typedef struct {
    uint8_t  bit_sync;      /* 1 = el lazo de reloj de bit esta enganchado */
    uint8_t  car_sync;      /* 1 = ademas se sabe donde empieza cada caracter */
    float    calidad;       /* fraccion de codigos validos, 0..1 */
    uint32_t car_ok;        /* caracteres con codigo valido */
    uint32_t car_mal;       /* caracteres que no lo eran ni en su copia */
    uint32_t rescatados;    /* los que salvo la segunda copia */
    uint32_t mensajes;      /* cuantos ZCZC completos han llegado */
    uint8_t  tiene_cab;     /* 1 = la cabecera de abajo vale */
    char     cab[5];        /* B1B2B3B4 del ultimo ZCZC */
} navtex_info_t;

/* Arranca el decodificador. fs_hz es la tasa del audio que va a llegar por
 * navtex_process(), o sea 12000 en esta radio. */
void navtex_start(float fs_hz);
void navtex_stop(void);
uint8_t navtex_activo(void);

/* Un bloque de audio de banda lateral, crudo (antes de la reduccion de
 * ruido, por lo mismo que el RTTY). Solo lectura. Se llama desde la
 * interrupcion; n tiene que valer NAVTEX_BLOCK_SAMPLES. */
void navtex_process(const float *audio, uint32_t n);

/* Saca un caracter ya decodificado, o devuelve 0 si no hay ninguno. Se
 * llama desde el bucle principal, no desde la interrupcion. */
uint8_t navtex_get_char(char *out);

void navtex_info(navtex_info_t *out);

/* El centro de los dos tonos de audio, ajustable en caliente contra el
 * osciloscopio de sintonia (el mismo que ya usan RTTY y CW). La separacion
 * son siempre 170 Hz: eso si lo fija la norma. */
void  navtex_set_centro_hz(float hz);
float navtex_get_centro_hz(void);

/* Intercambia marca y espacio. Hace falta porque escuchar en la banda
 * lateral contraria pone los dos tonos del reves. */
void    navtex_set_invertido(uint8_t inv);
uint8_t navtex_get_invertido(void);

/* ----------------------------------------------------------------------
 * Solo para el banco del simulador.
 * ---------------------------------------------------------------------- */

/* 1 si esos 7 bits son uno de los 35 codigos validos del CCIR 476, o sea si
 * tienen exactamente cuatro unos. Es la unica deteccion de error que trae el
 * alfabeto, y el banco la llama en vez de reimplementarla. */
uint8_t navtex_codigo_valido(uint8_t cod);

/* El caracter que representa ese codigo, en letras o en cifras. Devuelve 0
 * si el codigo no imprime nada (los de control). El banco la usa para
 * FABRICAR la señal, asi que transmisor y receptor salen de la misma
 * tabla y no puede haber dos tablas que se separen. */
char navtex_cod_a_car(uint8_t cod, uint8_t figuras);

/* El codigo de ese caracter, o 0xFF si no esta en el alfabeto. Es la inversa
 * de navtex_cod_a_car() y la busca EN LA MISMA TABLA. */
uint8_t navtex_car_a_cod(char c, uint8_t figuras);

/* Estado de los lazos, para que el banco pueda ver si se estan yendo en vez
 * de deducirlo de que el texto empeora. */
void navtex_lazos_dbg(float *reloj_ppm, float *nivel, uint8_t *desp_bit);

#endif /* NAVTEX_H */

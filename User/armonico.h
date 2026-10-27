/*
 * EL AVISADOR DE ARMONICOS - 07/10/2026
 *
 * No lo hay. Lo que hay es el FT8 de 30 metros, en 10.136 kHz, entrando por
 * el TERCER ARMONICO del oscilador local: con el dial en 3.411 el LO esta en
 * 3.387, y 3 x 3.387 = 10.161, a 25 kHz de 10.136. Dentro del tramo.
 *
 * POR QUE PASA, Y POR QUE NO LO ARREGLA NINGUN FIRMWARE. El oscilador local
 * de esta radio es una señal CUADRADA, y una cuadrada mezcla tambien en sus
 * armonicos. Lo unico que podria impedirlo es un filtro delante del
 * mezclador, y rf_lpf.c tiene UN SOLO filtro para toda la HF -de 0 a 36 MHz-,
 * asi que nada impide que una señal de 10 MHz llegue al mezclador mientras
 * estas en 3,4. Eso es el hardware que hay.
 *
 * LO QUE SI SE PUEDE HACER ES DECIRLO. Y hace falta de verdad, porque un
 * armonico y una imagen I/Q se parecen: las dos van AL REVES que el
 * panorama, y por eso se confunden de un vistazo.
 *
 * ----------------------------------------------------------------------
 * *** LA TABLA, REHECHA EL 27/10/2026, Y LA BUENA NOTICIA QUE TRAE. ***
 *
 * Aqui habia una tabla que decia que el 3.er armonico y una imagen I/Q se
 * mueven IGUAL -los dos a -2x- y que por tanto mirandolas no se
 * distinguen. Eso era cierto con el modelo de entonces, y dejo de serlo
 * el 26/10, cuando se midio que los armonicos n%4==3 salen con la banda
 * lateral DADA LA VUELTA (ver arm_busca() y sim/qsd_armonico.c). Con el
 * giro dentro, las velocidades son otras:
 *
 *     recibida por              al mover el dial, respecto al panorama
 *     el fundamental            0x   (se queda quieta)
 *     una imagen I/Q            2x al reves
 *     el 2.o armonico           1x al reves  Y ADEMAS  3x al reves
 *     el 3.o armonico           4x al reves
 *     el 4.o armonico           3x al reves  Y ADEMAS  5x al reves
 *     el 5.o armonico           4x al reves
 *     el 7.o armonico           8x al reves
 *
 * Y la cuenta es inmediata: una señal fija en f sale en banda base a
 * f - n*LO, que al mover el dial se corre n veces mas deprisa, y el
 * panorama entero se corre 1. Los pares salen DOS veces -una a cada lado-
 * y por eso tienen dos velocidades.
 *
 * ASI QUE LA PRUEBA DEL MOVIMIENTO SI SIRVE, que es justo lo contrario de
 * lo que decia esta cabecera: una imagen va al DOBLE y el 3.er armonico
 * al CUADRUPLE. Lo que no se distingue es el 3.o del 5.o, que van los dos
 * a 4x.
 * ----------------------------------------------------------------------
 *
 * El usuario vio una señal que iba al reves y al doble de velocidad, y se
 * tardo un dia entero en averiguar que no era una imagen, con dos versiones
 * de firmware y un corrector de banda ancha por el camino. Esto lo habria
 * contestado en un segundo.
 *
 * ======================================================================
 * PERO SI SE DISTINGUEN, Y LA DIFERENCIA ES ENORME - 24/10/2026.
 * ======================================================================
 *
 * *** Avisado por el usuario, que lo vio antes que yo: "si voy disminuyendo
 * la frecuencia solo deberian de aparecerme señales por la izquierda, y
 * tambien me aparecen señales por la derecha". Y luego: "estoy escuchando
 * en lsb en 40m una señal que estoy casi seguro de que es de 20m y por lo
 * tanto deberia de ser usb". ***
 *
 * Lo de arriba daba por hecho que cada armonico produce UNA copia. No es
 * cierto, y la cuenta es de una linea: los dos osciladores van separados 90
 * grados, asi que en el armonico n la separacion vale n*90 grados.
 *
 *     n   desfase I/Q    lo que sale
 *     1    90   cuadratura   UNA copia, en su lado, en su modo
 *     2   180   CONTRAFASE   DOS copias simetricas. Arriba en su modo,
 *                            abajo con el espectro INVERTIDO (una de USB
 *                            se lee en LSB)
 *     3   270   cuadratura   UNA copia, en el lado CONTRARIO, invertida
 *          (= -90)
 *     4   360   EN FASE      DOS copias, como el 2.o
 *
 * Con 180 grados, I y Q llevan la misma señal con el signo cambiado. Eso ya
 * no es una señal compleja: es REAL, y una señal real tiene espectro a +f Y
 * a -f. De ahi las dos copias. El patron se repite cada cuatro: 5 se porta
 * como 1, 6 como 2, 7 como 3.
 *
 * Y ESO SI SEPARA UN ARMONICO DE UNA IMAGEN, que es la pregunta para la que
 * existe este modulo:
 *
 *     lo que se ve y se oye                        lo que es
 *     -------------------------------------------- ------------------------
 *     una copia, en su modo                        emisora de verdad
 *     una copia, en el modo contrario              imagen I/Q, o armonico
 *                                                  IMPAR (3.o, 5.o)
 *     DOS copias simetricas respecto al centro,    armonico PAR: otra banda
 *     una en USB y otra en LSB                     metiendose
 *
 * CUANTO ENTRA POR CADA UNO. Los IMPARES salen de la propia cuadrada y no
 * dependen de nada: el 3.o esta a 1/3, o sea -9,5 dB, y ahi se queda. Los
 * PARES dependen ENTERAMENTE del error de ciclo de trabajo, que lo fija el
 * umbral de conmutacion del mezclador. Medido en sim/qsd_armonico.c:
 *
 *     error de ciclo    nivel del 2.o armonico
 *          0,0 %        no existe
 *          0,5 %        -35 dB
 *          1,0 %        -32 dB
 *          2,0 %        -26 dB
 *          4,0 %        -21 dB
 *
 * O sea que el 2.o es mas flojo que el 3.o en promedio, pero no por mucho, y
 * depende del ejemplar. Por eso la lista NO se ordena por el numero del
 * armonico -el 2.o no es "el sospechoso principal" por ser el 2.o- sino por
 * el nivel que cabe esperar de cada uno.
 *
 * Y NO LLEVA TABLA PROPIA. Esa es la parte importante del diseño: la radio ya
 * tiene las 49 bandas escritas, con sus limites y sus nombres, y una lista
 * nueva aqui seria una lista que algun dia no mencionaria la banda que
 * acaban de añadir. Es el fallo que mas veces ha aparecido en este proyecto.
 * Asi que esto no sabe nada de bandas: quien llama le pasa una funcion que
 * recorre la tabla que YA existe.
 */
#ifndef ARMONICO_H
#define ARMONICO_H

#include <stdint.h>

/* Hasta el septimo. Mas arriba la cuadrada ya reparte muy poca energia y la
 * lista se llena de cosas que no se oyen. */
#define ARM_N_MIN   2U
#define ARM_N_MAX   7U

typedef struct {
    uint8_t     n;        /* que armonico */
    uint8_t     par;      /* 1 = sale por los DOS lados; ver la cabecera */
    uint32_t    f_hz;     /* n * LO */
    /*
     * A que distancia del centro cae la banda, EN EL EJE DE LA PANTALLA y
     * no en el de la radiofrecuencia - 26/10/2026. No es lo mismo: el 3.o
     * y el 7.o salen con la banda lateral dada la vuelta, asi que algo por
     * ENCIMA de n*LO aparece por DEBAJO del centro. Lo mide
     * sim/qsd_armonico.c y lo corrige arm_busca(); ver alli.
     *
     * Y si `par` vale 1 esto es el MODULO, porque entonces hay dos copias,
     * una a cada lado: quien lo imprima tiene que poner un ±.
     */
    int32_t     off_hz;
    const char *nombre;   /* el de la banda, tal y como lo escribe la tabla */
} arm_hit_t;

/*
 * Recorre una tabla de rangos que vive en otro sitio. Devuelve 0 cuando se
 * acaba. `lo`/`hi` en hercios; un rango con lo==hi vale, es una frecuencia
 * suelta.
 */
typedef uint8_t (*arm_rango_fn)(uint16_t i, uint32_t *lo, uint32_t *hi,
                                const char **nombre);

/*
 * Busca que rangos de la tabla caen encima de n*LO, para n de ARM_N_MIN a
 * ARM_N_MAX, con el tramo de `span_hz` centrado en n*LO.
 *
 * `span_hz` ES LA TASA DE MUESTREO CRUDA, NO EL TRAMO DIBUJADO. 24/10/2026:
 * aqui se le pasaba spec_zoom_full_span_hz(), que se encoge con el zoom de
 * pantalla. Un armonico entra por el MEZCLADOR y le da exactamente igual
 * como de cerca estes mirando: con el zoom a 8x el avisador contestaba
 * "ninguno" mientras el armonico seguia entrando igual.
 *
 * `tope_rf_hz` es el corte del filtro de entrada (rf_lpf). Un armonico por
 * encima de ese corte NO llega al mezclador, asi que no se cuenta: con el
 * dial en 24 MHz, 4*LO cae en la banda de FM comercial y el avisador la
 * nombraba, cuando el filtro de 36 MHz la rechaza. Cero = sin filtro, que
 * es como se portaba antes.
 *
 * Devuelve cuantos ha encontrado y escribe hasta `max` en `out`, ORDENADOS
 * POR EL NIVEL QUE CABE ESPERAR y no por el numero del armonico. Ver la
 * tabla de la cabecera: el 3.o esta a -9,5 dB siempre y el 2.o depende del
 * ciclo de trabajo, asi que "el 2.o por ser el 2.o" era un criterio
 * inventado.
 */
uint8_t arm_busca(uint32_t lo_hz, uint32_t span_hz, uint32_t tope_rf_hz,
                  arm_rango_fn rangos, uint16_t n_rangos,
                  arm_hit_t *out, uint8_t max);

/* El nivel que cabe esperar del armonico n, en decimas de dB NEGATIVAS
 * (95 = -9,5 dB). Para el orden de la lista y para el banco. */
uint16_t arm_nivel_dbx10(uint8_t n);

#endif /* ARMONICO_H */

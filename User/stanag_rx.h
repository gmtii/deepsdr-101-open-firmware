#ifndef STANAG_RX_H
#define STANAG_RX_H

#include <stdint.h>

/*
 * EL DEMODULADOR DE STANAG 4285 - 02/10/2026
 *
 * Esto saca TEXTO de la forma de onda. El otro modulo, stanag_det.c,
 * solo dice "aqui hay algo que se repite cada 106,67 ms"; este la
 * demodula de verdad.
 *
 * TODO lo que hay aqui esta medido antes en
 * dsdr-ui/tools/stanag4285_proto.py contra una grabacion de verdad, y
 * ahi esta escrito de donde sale cada numero y que paginas de la norma
 * lo dicen. Lo que sigue es el resumen; para el porque, ese fichero.
 *
 * LA CADENA
 *
 *   audio 12 kHz
 *     -> a banda base (portadora 1800 Hz) y filtro adaptado (raiz de
 *        coseno alzado, alfa 0,35)
 *     -> 5 muestras por simbolo EXACTAS (12000/2400), asi que no hace
 *        falta interpolador: basta elegir la fase, y la buena es +2
 *        respecto al pico de correlacion (medido, barrido de -3 a +8)
 *     -> sincronismo de trama correlando el preambulo de 80 simbolos
 *     -> desvio de frecuencia, aprovechando que el preambulo es una
 *        secuencia m de periodo 31: se compara su primera mitad con la
 *        segunda y la diferencia de fase partida por 31 lo da
 *     -> canal con cuatro anclas por trama: el preambulo y los tres
 *        bloques de 16 simbolos de sondeo
 *     -> 128 simbolos de datos por trama, se les quita el scrambler
 *     -> bits blandos
 *     -> desentrelazado (32 filas, anexo E)
 *     -> se suman las copias repetidas si la velocidad las lleva
 *     -> Viterbi K=7 tasa 1/2 (el de hfdl_viterbi.c, ya estaba)
 *     -> caracteres
 *
 * LA TRAMA. 256 simbolos, 106,67 ms:
 *
 *     80 de preambulo + [32 datos, 16 sondeo] x3 + 32 datos
 *
 * o sea 128 de datos y 48 de sondeo. Un simbolo de datos es "conocido"
 * -y por tanto sirve de ancla- si (i mod 48) >= 32 contando desde el
 * primero de datos.
 *
 * LAS VELOCIDADES (anexo E del 4285). De momento esta escrito el camino
 * de BPSK; QPSK y 8PSK quedan marcados en el codigo donde entran.
 *
 *     2400 bps  8PSK  2/3   1/2 perforada SALTANDO UNA FILA DE CADA
 *                           CUATRO a la salida del entrelazador
 *     1200 bps  QPSK  1/2   tal cual
 *      600 bps  BPSK  1/2   tal cual
 *      300 bps  BPSK  1/4   repetida 2 veces
 *      150 bps  BPSK  1/8   repetida 4
 *       75 bps  BPSK  1/16  repetida 8
 *
 * Y LA TRAMPA QUE ME COSTO UNA TARDE: las repeticiones van POR PAREJAS,
 * T1 T2 T1 T2, no T1 T1 T2 T2. La norma lo dice con todas las letras
 * ("the bits are repeated in pairs rather than repetitions of the
 * first, T1(x), followed by repetitions of the second") y yo lo habia
 * supuesto al reves. Probandolo bit a bit la comprobacion de paridad del
 * codigo daba 0,5 en las 3584 combinaciones que llegue a barrer;
 * probandolo por parejas dio 0,0000 a la primera.
 *
 * EL ENTRELAZADOR (anexo E, E-4 y E-5). Convolucional de 32 filas. El
 * incremento de retardo por fila:
 *
 *                       retardo total
 *                  10,24 s      0,853 s      (largo / corto)
 *     2400 bps        48            4
 *     1200 bps        24            2
 *     600 - 75 bps    12            1
 *
 * En el desentrelazador la fila r retrasa j*(31-r), la entrada es
 * secuencial y la SALIDA va por la secuencia (9*i) mod 32. Y el
 * sincronismo lo regala la norma: la fila 0 es siempre la primera de
 * cada trama, y cada trama lleva un multiplo entero de 32 bits de
 * datos, asi que no hay nada que buscar.
 *
 * CUIDADO CON EL LARGO: se come 372 grupos de 32 bits antes de soltar el
 * primero. En BPSK son 93 tramas, casi diez segundos. Una grabacion mas
 * corta que eso NO SE PUEDE decodificar, y no es un fallo: es el
 * entrelazador haciendo su trabajo.
 *
 * ARRANQUE Y FIN. SOM = 0x03873C3C delante, EOM = 0x4B65A5B2 detras, el
 * bit de mas a la izquierda primero.
 *
 * EL TEXTO: ASCII sincrono de 8 bits, LSB PRIMERO. La norma admite
 * ademas ITA2 con trama 5N1 -que es lo que usan las costeras francesas
 * en sus frecuencias barco-costera, y esas van EN CLARO- y ASCII
 * asincrono.
 */

/* Floats de trabajo que pide prestados. Como el detector, se los deja la
 * cascada mientras el modo esta activo. */
#define STANAG_RX_FLOATS    8192U    /* 32 kB, los mismos que pide el detector */

#define STANAG_RX_LINEAS    9U
#define STANAG_RX_LARGO     44U

/* Las velocidades, de menos a mas, que es como se recorren con el boton. */
enum {
    STANAG_RX_75 = 0,    /* BPSK  1/16, parejas x8 */
    STANAG_RX_150,       /* BPSK  1/8,  parejas x4 */
    STANAG_RX_300,       /* BPSK  1/4,  parejas x2 */
    STANAG_RX_600,       /* BPSK  1/2 */
    STANAG_RX_1200,      /* QPSK  1/2 */
    STANAG_RX_2400,      /* 8PSK  2/3, perforada saltando una fila de cada cuatro */
    STANAG_RX_VELOCIDADES
};

/* Como se enseñan los bits. ASCII sincrono es lo que usa el mensaje de
 * prueba del 4481; ITA2 5N1 es lo que mandan en claro las costeras
 * francesas en sus frecuencias barco-costera. */
enum { STANAG_RX_ASCII = 0, STANAG_RX_ITA2, STANAG_RX_HEX, STANAG_RX_FORMATOS };

const char *stanag_rx_velocidad_txt(uint8_t v);
const char *stanag_rx_formato_txt(uint8_t f);
void        stanag_rx_formato_pon(uint8_t f);
uint8_t     stanag_rx_formato(void);
/* Por que no ha podido arrancar, si stanag_rx_init() devolvio 0. */
const char *stanag_rx_porque(void);

typedef struct {
    uint8_t  engancha;    /* 1 si el preambulo esta sincronizado */
    uint8_t  sondeos;     /* % de simbolos de sondeo en su sitio */
    int16_t  desvio_dhz;  /* desvio de frecuencia en decimas de Hz */
    uint16_t tramas;      /* tramas vistas desde que se arranco */
    uint16_t grupos;      /* grupos de 32 bits ya desentrelazados */
    uint8_t  ber;         /* % de bits que no cuadran al recodificar */
    uint8_t  ber_vale;    /* 0 = ese % no mide nada (flujo degenerado) */
    /*
     * CUANTO FALTA PARA EL PRIMER CARACTER, Y POR QUE HAY QUE ENSEÑARLO.
     *
     * *** El dueño, 02/10/2026: "no lo hace, no sale". ***
     *
     * Y era verdad que no salia: con entrelazado largo hacen falta
     * j*31 grupos solo para llenarlo, mas 20*repeticiones para juntar
     * la primera ventana del Viterbi. A 600 bps son 392 grupos = 98
     * tramas = DIEZ SEGUNDOS con la pantalla en blanco. Desde fuera eso
     * es indistinguible de que no funcione.
     *
     * Asi que ahora se cuenta y se dice.
     */
    uint16_t grupos_hechos;
    uint16_t grupos_hacen;   /* 0 cuando ya esta soltando texto */
    uint16_t perdidas;       /* veces que la cola de audio se desbordo */
    /*
     * CUANTAS VECES SE HA BARRIDO LA PORTADORA. Es la cuenta del coste.
     *
     * *** El dueño, 02/10/2026: "va con bastante lag toda la radio ahora
     * mismo". *** Barrer once portadoras por 256 posiciones por 80
     * simbolos son millones de cuentas, y se hacia nueve veces por
     * segundo mientras no enganchara. El banco vigila este numero porque
     * un coste que se dispara no se ve en ninguna otra medida: todo
     * sigue dando el resultado correcto, solo que tarde.
     */
    uint16_t busquedas;
    uint16_t cola_ms;        /* cuanto audio aguanta la cola sin perder nada */
    /*
     * LO MEJOR QUE HA ENCONTRADO EL BARRIDO, AUNQUE NO LE VALGA.
     *
     * *** El dueño, 02/10/2026, despues de media docena de versiones:
     * "esto no decodifica nada de nada", y sin forma de grabar audio
     * desde el PC. ***
     *
     * Pues entonces el instrumento tiene que estar en la pantalla de la
     * radio. Estos dos numeros dicen de un vistazo en cual de los tres
     * sitios esta el problema:
     *
     *   corr por debajo de 15  -> no hay preambulo de 4285 ahi, punto
     *   corr 30-44             -> lo esta viendo pero no llega al umbral:
     *                             señal floja o fuera del barrido
     *   corr >= 45             -> engancha, el problema es de otro sitio
     *
     * Y corr_hz dice a que desvio lo vio, que es lo que hay que corregir
     * sintonizando si se sale del barrido.
     */
    uint8_t  corr;           /* la mejor correlacion x100 */
    int16_t  corr_hz;        /* a que desvio de portadora salio */
    uint16_t ancho_hz;       /* hasta donde esta barriendo ahora mismo */
    uint8_t  fondo;          /* a cuanto correlaciona el ruido de al lado */
    uint8_t  umbral;         /* el umbral de enganche, para no escribirlo a mano */
    /*
     * LA TRAMA, MEDIDA SIN CAMBIAR DE MODO.
     *
     * *** El dueño, 02/10/2026: "te dije que te pusieras todo lo que
     * podria hacerte falta". ***
     *
     * Y lo que mas falta hacia era esto: saber si en esa frecuencia hay
     * o no hay trama de 106,67 ms SIN tener que irse al identificador,
     * que comparte la memoria y lo reinicia todo. Es la autocorrelacion
     * de la señal en banda base a un retardo de una trama exacta: si hay
     * un 4285, la trama de ahora se parece a la de hace 106,67 ms.
     *
     *   trama por debajo de 20  -> aqui no hay nada con esa cadencia
     *   trama por encima de 40  -> hay 4285 (o un primo suyo) seguro
     *
     * Con esto y la correlacion del preambulo ya no hay que adivinar:
     * trama alta y preambulo bajo es "esta ahi pero no lo cojo"; las dos
     * bajas es "no hay señal".
     */
    uint8_t  trama;          /* autocorrelacion a una trama de retardo, x100 */
    uint16_t nivel;          /* nivel medio del audio que entra, x1000 */
    uint8_t  corr_max;       /* la mejor correlacion desde que se arranco */
    uint8_t  sondeos_max;    /* los mejores sondeos desde que se arranco */
    uint16_t tramas_bien;    /* tramas con los sondeos por encima del minimo */
    /*
     * LA BUSQUEDA AUTOMATICA DE VELOCIDAD Y ENTRELAZADO.
     *
     * *** El dueño, 07/10/2026: "ninguno de todos los que pruebo
     * decodifica nada en claro, y eso me parece rarisimo porque me he
     * encontra muuuuuchos". ***
     *
     * Ni la velocidad ni el entrelazado van en la señal, y son once
     * combinaciones. Esto las recorre solo. Ver stanag_rx.c.
     */
    uint8_t  busca;          /* 1 = la busqueda esta encendida */
    uint8_t  busca_fijo;     /* 1 = ya ha acertado y esta soltando texto */
    uint8_t  busca_cual;     /* candidato en curso, 1..busca_de */
    uint8_t  busca_de;       /* cuantos candidatos tiene la lista */
    uint8_t  busca_mejor;    /* el mejor ber de la pasada, 100 = ninguno todavia */
    uint8_t  busca_vueltas;  /* listas recorridas enteras sin acertar */
    uint16_t busca_falta;    /* grupos que faltan para poder juzgar este */
} stanag_rx_est_t;

/*
 * La busqueda automatica. Encenderla o apagarla cambia cuanto
 * desentrelazador hay que reservar, asi que el modo tiene que volver a
 * llamar a _init() detras; stanag_modo.c lo hace con recoloca().
 */
void     stanag_rx_auto_pon(uint8_t on);
uint8_t  stanag_rx_auto(void);
uint8_t  stanag_rx_auto_fijo(void);
/* La lista de candidatos y el mejor ber que ha sacado cada uno en la
 * pasada de ahora. Es lo que mide el MARGEN, y sin margen el acierto es
 * casualidad. 100 = todavia no se ha podido medir. */
uint8_t  stanag_rx_auto_cuantos(void);
uint8_t  stanag_rx_auto_ber(uint8_t i);
uint8_t  stanag_rx_auto_vel(uint8_t i);
uint8_t  stanag_rx_auto_largo(uint8_t i);

uint8_t  stanag_rx_init(float *trabajo, uint32_t n_floats);
void     stanag_rx_fin(void);
void     stanag_rx_velocidad_pon(uint8_t v);
uint8_t  stanag_rx_velocidad(void);
void     stanag_rx_largo_pon(uint8_t largo);   /* 1 largo, 0 corto */
uint8_t  stanag_rx_largo(void);

/* El audio decimado a 12 kHz, tal cual sale del demodulador de banda
 * lateral. No hace nada mas que copiarlo: el trabajo va en _paso(). */
void     stanag_rx_mete(const float *audio, uint32_t n);

/* Un trozo de trabajo. Se llama desde el bucle principal, NUNCA desde la
 * interrupcion de audio. Devuelve 1 si ha salido texto nuevo. */
uint8_t  stanag_rx_paso(void);

void        stanag_rx_estado(stanag_rx_est_t *out);
const char *stanag_rx_linea(uint8_t i);
uint8_t     stanag_rx_nlineas(void);

/* Vacia el texto y los contadores de mirar; mantiene el enganche. */
void        stanag_rx_borra(void);

#endif /* STANAG_RX_H */

/*
 * STANAG 4415 / MIL-STD-188-110A A 75 bps — ENGANCHE Y DESENSANCHADO
 *
 * *** El dueño: "adelante con los dos stanag". Y despues, cuando hizo falta
 * el estandar: "lo buscas en el historial que ya te lo pase hace dias". ***
 *
 * Lo que hace este fichero esta MEDIDO contra la grabacion del dueño, no
 * supuesto. El camino entero esta en docs/STANAG_4415_MEDIDO.md; aqui va el
 * resumen de lo que importa para leer el codigo.
 *
 * LA FORMA DE ONDA. Tono serie: una portadora a 1800 Hz con 2400 simbolos por
 * segundo, los mismos que el 4285. Lo que cambia es todo lo de encima.
 *
 * A 75 bps la informacion va en DIBITS -dos bits-, y cada dibit se transmite
 * como 32 simbolos (tabla XIV del estandar):
 *
 *      00 -> (0000) ocho veces      01 -> (0404) ocho veces
 *      10 -> (0044) ocho veces      11 -> (0440) ocho veces
 *
 * Los numeros son "tribits": 0 es fase 0 y 4 es fase 180, asi que esto es
 * BPSK. Cuatro formas de onda ortogonales. Esos 32 simbolos por dibit son 15
 * dB de ganancia de proceso, y es lo que hace que este modo pase por donde no
 * pasa ningun otro.
 *
 *      2400 Bd / 32 = 75 dibits/s = 150 bps codificados = 75 bps con FEC 1/2
 *
 * EL ALEATORIZADOR, que es lo que lo tapa todo. Encima de los tribits se suma
 * modulo 8 una secuencia de un registro de desplazamiento de 12 etapas que se
 * reinicia cada 160 simbolos. Sin quitarlo, la señal parece 8-PSK aleatoria y
 * no hay nada que correlar. Medido sobre la grabacion: la correlacion con la
 * forma ganadora pasa de 0,279 a 0,771 al quitarlo.
 *
 * EL PREAMBULO, que es por donde se engancha. Segmentos de 200 ms, cada uno
 * con 15 simbolos de tres bits:
 *
 *      0, 1, 3, 0, 1, 3, 1, 2, 0, D1, D2, C1, C2, C3, 0
 *
 * Los NUEVE PRIMEROS SON FIJOS. Eso son 288 simbolos completamente conocidos
 * -120 ms- y es la plantilla con la que se busca. Medido sobre la grabacion:
 * pico 0,624 contra una mediana de 0,050, o sea doce veces y media.
 *
 * Detras van D1 y D2, que dicen la velocidad y el entrelazado (tabla XV), y
 * C1, C2, C3, que son una CUENTA ATRAS de segmentos. Esa cuenta es la mejor
 * prueba que hay de que el enganche es bueno: tiene que bajar de uno en uno
 * hasta cero. En la grabacion del dueño salieron 23, 22, 21 ... 2, 1, 0, los
 * veinticuatro, sin un fallo.
 *
 * LO QUE ESTE FICHERO HACE Y LO QUE NO. Hace: engancha con el preambulo, lee
 * la velocidad, el entrelazado y la cuenta, y desensancha los datos a dibits.
 * NO hace: desentrelazar ni Viterbi, asi que todavia no saca texto. Queda
 * dicho aqui a proposito y no escondido en un comentario.
 *
 * Y lo de siempre: se demodula la forma de onda. Si lo que lleva dentro va
 * cifrado, saldran bytes y se dira que van cifrados. El cifrado no se toca.
 */
#ifndef S4415_H
#define S4415_H

#include <stdint.h>

#define S4415_FS_HZ      12000.0f
#define S4415_BD         2400.0f
#define S4415_SPS        5U          /* 12000/2400, exactas */
#define S4415_PORT_HZ    1800.0f

#define S4415_SET        32U         /* simbolos por dibit */
#define S4415_PRE_FIJOS  9U          /* simbolos conocidos del preambulo */
#define S4415_PLANT      (S4415_PRE_FIJOS * S4415_SET)   /* 288 */
#define S4415_SEG        15U         /* simbolos por segmento de 200 ms */
#define S4415_SEG_SIM    (S4415_SEG * S4415_SET)         /* 480 */
#define S4415_SCR_DAT    160U        /* periodo del aleatorizador de datos */

/* La ventana de texto que guarda el modulo. */
#define S4415_LINEAS     4U
#define S4415_COLS       40U

/* Los dos formatos de salida. HEX no supone nada del contenido, que es lo
 * normal: la mayoria del trafico de estas bandas va cifrado. */
#define S4415_ASCII      0U
#define S4415_HEX        1U
#define S4415_FORMATOS   2U

/*
 * LA RAM QUE PIDE, y por que tanta. El anillo del filtro adaptado va a 12 kHz
 * y no a simbolo -el porque esta en s4415.c, y es que decimar por cinco deja
 * la fase de muestreo donde caiga, hasta medio simbolo fuera-, asi que el
 * estado entero ronda los 39 kB. Sale del prestamo de la cascada (54.432
 * bytes), el mismo que usan HFDL, FT8, WSPR y el 4285, y por eso ninguno de
 * ellos puede estar a la vez que este. s4415_ram_bytes() lo dice exacto.
 */
#define S4415_FLOATS     11200U

typedef struct {
    uint8_t  engancha;    /* 1 = preambulo encontrado */
    uint8_t  corr;        /* calidad del ultimo enganche, 0..100 */
    uint8_t  bps_idx;     /* indice de velocidad, ver s4415_bps_txt() */
    uint8_t  largo;       /* 1 = entrelazado largo */
    uint8_t  cuenta;      /* la cuenta atras del ultimo segmento, 0..23 */
    uint16_t segmentos;   /* segmentos de preambulo vistos */
    uint16_t dibits;      /* dibits desensanchados desde el enganche */
    uint8_t  margen;      /* cuanto gana la forma elegida a la segunda, x10 */
    uint8_t  calidad;     /* calidad media de D1..C3, 0..100 */
    uint8_t  d1, d2;      /* lo leido, crudo, para poder discutirlo */
    uint8_t  excepcionales; /* sets excepcionales vistos = bordes de bloque */
    uint16_t bloques;     /* bloques desentrelazados y pasados por Viterbi */
    uint16_t bits;        /* bits de datos decodificados */
    uint8_t  eom;         /* veces que ha salido el 4B65A5B2 del estandar */
    uint8_t  desalineos;  /* sets excepcionales fuera de sitio */
    uint8_t  sin_marca;   /* bloques cerrados sin su set excepcional */
    uint8_t  fin_tx;      /* 1 = la emision termino (EOM + vaciado) */
    uint8_t  formato;     /* S4415_ASCII o S4415_HEX */
} s4415_est_t;

uint8_t  s4415_init(float *trabajo, uint32_t n_floats);
void     s4415_fin(void);
uint8_t  s4415_activo(void);
uint32_t s4415_ram_bytes(void);

/* Muestras que el barrido no llego a mirar porque el anillo se piso. Si esto
 * no es cero, el que llama no esta llamando a s4415_paso() bastante. */
uint32_t s4415_perdidas(void);
uint32_t s4415_tiradas(void);
void     s4415_borra(void);

/* Audio de banda lateral ya demodulado, a 12 kHz. */
void    s4415_mete(const float *audio, uint32_t n);
/* Un paso de trabajo; devuelve 1 si ha pasado algo que contar. */
uint8_t s4415_paso(void);
void    s4415_estado(s4415_est_t *out);

const char *s4415_bps_txt(uint8_t idx);

/* Los dibits desensanchados, crudos, para mirarlos de cerca. 0 si no hay
 * ninguno nuevo. */
uint16_t s4415_saca_dibits(uint8_t *out, uint16_t max);

/* El texto ya decodificado: desentrelazado, Viterbi y caracteres. */
void        s4415_formato(uint8_t f);
uint8_t     s4415_nlineas(void);
const char *s4415_linea(uint8_t i);

/* En que set de datos cayo el i-esimo set excepcional (0..3). 0xFFFF si no
 * ha habido tantos. Es la medida que dice donde esta el borde del bloque del
 * entrelazador, y el banco la imprime. */
uint16_t    s4415_excepcional(uint8_t i);

#endif /* S4415_H */

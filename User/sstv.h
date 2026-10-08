#ifndef SSTV_H
#define SSTV_H

#include <stdint.h>

/*
 * SSTV (television de barrido lento). Etapa 27, 24/09/2026.
 *
 * Fotos que se mandan los radioaficionados entre si, sobre todo en 14,230
 * MHz. Una imagen tarda entre uno y dos minutos en llegar.
 *
 * ES EL HERMANO DEL WEFAX y comparte con el la pieza cara: el discriminador
 * de frecuencia (disc_fm.c). El brillo va igual -1500 Hz negro, 2300 Hz
 * blanco- y lo que anade son tres cosas:
 *
 *   1. COLOR. Cada linea se manda tres veces, una por canal, en orden verde,
 *      azul, rojo. No es un capricho del formato: el verde lleva la mayor
 *      parte de la luminancia, asi que si la señal se corta a mitad de linea
 *      lo que se ha salvado es lo que mas se parece a la foto.
 *
 *   2. SINCRONISMO POR LINEA. Cada linea empieza con un pulso a 1200 Hz. Eso
 *      es una ventaja enorme sobre el fax: alli no habia marcas y el ritmo
 *      habia que adivinarlo de la señal de fase, con el riesgo de que la
 *      imagen saliera en diagonal. Aqui se reengancha en CADA linea, asi que
 *      la inclinacion no existe como problema.
 *
 *   3. CABECERA VIS. Antes de la imagen va un codigo de siete bits que dice
 *      QUE MODO viene. Con eso no hay que elegir nada a mano: el
 *      decodificador se entera solo.
 *
 * LA TABLA DE MODOS SE CALCULA, NO SE COPIA. Cada modo se describe con
 * cuatro tiempos (pulso de sincronismo, porche, separador y barrido) y una
 * familia, y de ahi salen los desplazamientos de cada canal y la duracion de
 * la linea. La alternativa -una tabla con los desplazamientos ya calculados-
 * es la misma informacion escrita dos veces, y se ve lo que pasa con eso en
 * la propia implementacion de referencia que se uso para sacar estos
 * numeros: sus constantes de Martin 1 no cuadran con su propia formula, y la
 * duracion de linea que trae escrita a mano da 113,2 s de imagen cuando el
 * modo dura 114,3.
 *
 * LO QUE NO ESTA: los modos Robot y PD. Robot 36 y 72 no mandan rojo, verde y
 * azul sino luminancia y color a media resolucion, alternando de una linea a
 * la siguiente; es otra maquina, no un modo mas de esta tabla. Se pueden
 * anadir despues.
 */

/* Todo se muestrea a este ancho, sea cual sea el del modo: el muestreador
 * promedia solo. Ver el comentario de la tabla de modos en sstv.c. */
#define SSTV_ANCHO 320U
#define SSTV_ALTO  256U

/* Como viene el color en una linea. */
#define SSTV_COLOR_GBR 0U   /* verde, azul y rojo, un barrido entero cada uno */
#define SSTV_COLOR_YUV 1U   /* luminancia y dos diferencias de color */

/*
 * EL PLAN DE UNA LINEA: donde cae cada canal y que significa. Sale calculado
 * de los pocos tiempos que describen el modo - ver sstv_plan().
 *
 * Esta en la cabecera porque el banco del simulador FABRICA la señal con el
 * mismo plan con el que el decodificador la lee. Dos tablas de tiempos, una
 * en cada lado, no comprobarian nada.
 */
typedef struct {
    uint8_t  n_ch;        /* canales por linea transmitida: 2, 3 o 4 */
    float    off[4];      /* donde empieza cada uno, desde el sincronismo */
    float    scan[4];     /* cuanto dura cada uno */
    float    linea;       /* la linea transmitida entera */
    float    sync;        /* el pulso de sincronismo */
    uint16_t ancho, alto; /* la resolucion NATIVA del modo */
    uint8_t  color;       /* SSTV_COLOR_* */
    uint8_t  lineas_img;  /* lineas de imagen por linea transmitida: 1 o 2 */
    uint8_t  alterna;     /* 1 = la croma alterna de linea en linea (Robot 36) */
    uint8_t  vdec;        /* una de cada cuantas lineas se pinta */
} sstv_plan_t;

uint8_t sstv_plan(uint8_t i, sstv_plan_t *pl);

typedef enum {
    SSTV_PARADO = 0,
    SSTV_BUSCA,      /* esperando la cabecera VIS */
    SSTV_IMAGEN
} sstv_estado_t;

typedef struct {
    uint8_t     estado;
    uint8_t     vis;          /* el codigo recibido, 0 si ninguno */
    const char *modo;         /* el nombre del modo, 0 si ninguno */
    uint16_t    linea;        /* por que linea va */
    uint32_t    imagenes;     /* imagenes empezadas desde el arranque */
    uint16_t    sincs_malos;  /* pulsos de sincronismo descartados */
    float       nivel;
    /* Las lineas que tiene ESTE modo, que no son siempre SSTV_ALTO: los
     * Robot son de 240 y los Martin y PD de 256. La pantalla enseñaba
     * "000/256" con un Robot 72 puesto - 06/10/2026, visto en un video del
     * dueño. Pequeño, pero es un numero que miente. */
    uint16_t    alto;
} sstv_info_t;

void sstv_start(float fs_hz);
void sstv_stop(void);
uint8_t sstv_activo(void);

/* Audio de banda lateral, crudo. Se llama desde la interrupcion. */
void sstv_process(const float *audio, uint32_t n);

/*
 * Si hay una linea terminada, devuelve un puntero a SSTV_ANCHO ternas RGB
 * (tres bytes por punto) y la da por consumida, dejando en *y en que renglon
 * de la imagen va. Si no hay nada, devuelve 0.
 */
const uint8_t *sstv_linea(uint16_t *y);

void sstv_info(sstv_info_t *out);

/* Empezar a pintar sin esperar la cabecera, con el modo que se diga. Es lo
 * que hace falta cuando te enganchas a mitad de imagen. */
void sstv_fuerza(uint8_t indice_modo);

/* ----------------------------------------------------------------------
 * Solo para el banco del simulador.
 * ---------------------------------------------------------------------- */
uint8_t     sstv_modos_n(void);
uint8_t     sstv_modo_vis(uint8_t i);
const char *sstv_modo_nombre(uint8_t i);

/* Los tiempos CALCULADOS de un modo, que es de donde salen tanto el
 * decodificador como el generador del banco. Devuelve 0 si el indice no
 * existe. off[] son los tres canales en orden verde, azul, rojo. */
uint8_t sstv_modo_tiempos(uint8_t i, float *linea_s, float *sync_s,
                          float *scan_s, float off_s[3]);

/* Por donde va el detector de la cabecera, para poder ver EN QUE PASO se
 * atasca en vez de deducirlo de que no sale nada. */
void sstv_vis_dbg(uint8_t *est, uint32_t *lider, uint32_t *c1200, float *hz);

/* La frecuencia de audio que le toca a un brillo, con la misma escala que usa
 * el decodificador. El banco fabrica la señal con esto. */
float sstv_hz_de_nivel(uint8_t v);

/* Cuanto se ha desviado la sintonia, en hercios, medido sobre el lider de la
 * cabecera (ver s_off_hz en sstv.c). Negativo = la señal llega baja. Util
 * para la pantalla y para el banco; cero mientras no se haya medido. */
float sstv_desvio_hz(void);

#endif /* SSTV_H */

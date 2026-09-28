#ifndef WEFAX_H
#define WEFAX_H

#include <stdint.h>

/*
 * WEFAX (facsimil meteorologico de onda corta). Etapa 26, 24/09/2026.
 *
 * Mapas del tiempo, cartas de superficie e imagenes de hielo emitidos en
 * onda corta con horario diario fijo. El DWD aleman emite desde Pinneberg en
 * 3,855 / 7,880 / 13,8825 MHz; entre esas tres frecuencias hay carta a casi
 * cualquier hora del dia desde España.
 *
 * LO QUE ES, EN UNA LINEA: un tono de audio cuya FRECUENCIA es el brillo.
 * 1500 Hz es negro, 2300 Hz es blanco, y lo que hay en medio son los grises.
 * El transmisor recorre la imagen linea a linea, normalmente 120 lineas por
 * minuto, o sea dos por segundo.
 *
 * DONDE SE GUARDA LA IMAGEN: EN EL PANEL.
 *
 * Esta es la decision que hace que esto quepa. Una carta de superficie son
 * unas 1200 lineas de 1809 puntos; guardarla en escala de grises son 2 MB, y
 * hasta reducida a lo que cabe en pantalla serian 150 kB de una RAM de 192
 * que ya esta casi entera ocupada. No hay forma.
 *
 * Pero es que no hace falta guardarla: el controlador del panel (RM68120)
 * tiene su propia memoria de video y se queda con lo que se le escribe. Si
 * cada linea se pinta en su sitio segun llega, la imagen la guarda el panel
 * y esta radio no guarda NADA - un solo renglon de 756 bytes que se
 * sobreescribe dos veces por segundo. Lo que costaba 150 kB cuesta cero.
 *
 * LO DELICADO ES LA INCLINACION. En la imagen no hay ninguna marca de
 * sincronismo: la emisora manda las lineas seguidas y el receptor tiene que
 * ir al mismo ritmo por su cuenta. Un error del 0,1 % en la velocidad de
 * linea desplaza cada linea un punto respecto a la anterior, y cien lineas
 * despues la carta sale en diagonal.
 *
 * Lo que arregla eso es la SEÑAL DE FASE que va delante de cada imagen: 30
 * segundos de lineas negras con un pulso blanco. Siguiendo ese pulso linea a
 * linea se sabe dos cosas a la vez - donde empieza la linea (la posicion del
 * pulso) y si vamos rapidos o lentos (cuanto se mueve el pulso de una linea
 * a la siguiente)-. Con 20 lineas de fase la velocidad sale con precision de
 * sobra para las 1200 que vienen detras.
 *
 * Y si te enganchas a mitad de carta, cuando la fase ya paso, esta el ajuste
 * a mano: el mismo mando que sintoniza, corrigiendo la inclinacion en vivo.
 * Es lo que hacen casi todos los equipos del mercado, y funciona.
 */

/* El ancho de la imagen en puntos. Es el mismo que el de la cascada para que
 * ocupe exactamente el mismo hueco del panel y las dos cosas se dibujen con
 * la misma geometria. La emisora manda 1809 puntos por linea (indice de
 * cooperacion 576) y aqui se promedian a estos. */
#define WEFAX_ANCHO 756U

/* Las velocidades de linea que existen. 120 es la normal; 60 y 90 las usan
 * algunas estaciones antiguas y 240 las de mayor resolucion. */
#define WEFAX_LPM_N 4U

/* Los tonos, que los fija la norma. */
#define WEFAX_NEGRO_HZ   1500.0f
#define WEFAX_BLANCO_HZ  2300.0f
#define WEFAX_CENTRO_HZ  1900.0f
#define WEFAX_ARRANQUE_HZ 300.0f   /* el tono de arranque, 5 s */
#define WEFAX_PARADA_HZ   450.0f   /* el de parada, 5 s */

typedef enum {
    WEFAX_PARADO = 0,
    WEFAX_BUSCA,      /* esperando el tono de arranque */
    WEFAX_FASE,       /* siguiendo el pulso de la señal de fase */
    WEFAX_IMAGEN      /* pintando */
} wefax_estado_t;

typedef struct {
    uint8_t  estado;        /* wefax_estado_t */
    uint16_t lpm;           /* la velocidad que se esta usando */
    float    ppm;           /* correccion de inclinacion en vigor */
    uint32_t lineas;        /* lineas emitidas desde el arranque */
    uint8_t  fase_n;        /* lineas de fase vistas, 0..16 */
    float    nivel;         /* cuanta señal hay */
} wefax_info_t;

void wefax_start(float fs_hz);
void wefax_stop(void);
uint8_t wefax_activo(void);

/* Audio de banda lateral, crudo. Se llama desde la interrupcion. */
void wefax_process(const float *audio, uint32_t n);

/*
 * Si hay una linea terminada, devuelve un puntero a sus WEFAX_ANCHO bytes de
 * gris (0 = negro, 255 = blanco) y la da por consumida. Si no, devuelve 0.
 * Se llama desde el bucle principal.
 */
const uint8_t *wefax_linea(void);

void wefax_info(wefax_info_t *out);

/* Velocidad de linea. Cambiarla reinicia el sincronismo: el ritmo anterior
 * no significa nada con otra velocidad. */
void     wefax_set_lpm(uint16_t lpm);
uint16_t wefax_get_lpm(void);
uint16_t wefax_lpm_opcion(uint8_t i);   /* la lista, para la interfaz */

/* El mismo ajuste por indice (0..WEFAX_LPM_N-1, creciente). Es lo que
 * recorre el mando y lo que se guarda en CONFIG.CSV; ver wefax.c. */
uint8_t  wefax_get_lpm_idx(void);
void     wefax_set_lpm_idx(uint8_t i);

/* Correccion de inclinacion a mano, en partes por millon sobre la velocidad
 * nominal. Positiva = ir mas deprisa. */
void  wefax_set_ppm(float ppm);
float wefax_get_ppm(void);

/* "La linea empieza AQUI": pone el contador de linea a cero en este instante.
 * Es el ajuste grueso cuando te enganchas a mitad de carta. */
void wefax_sincroniza(void);

/* Saltarse la espera del tono de arranque y pintar ya. */
void wefax_fuerza_imagen(void);

/* ----------------------------------------------------------------------
 * Solo para el banco del simulador.
 * ---------------------------------------------------------------------- */

/* El gris que le corresponde a un tono de audio, con el mismo recorte que usa
 * el decodificador. El banco FABRICA la señal con esto, asi que la escala de
 * grises es la misma en los dos lados por construccion. */
uint8_t wefax_gris_de_hz(float hz);
float   wefax_hz_de_gris(uint8_t gris);

/* Lo que mide el seguimiento de la señal de fase: el desplazamiento del
 * pulso por linea, en puntos. Es de donde sale la correccion. */
void wefax_fase_dbg(float *desliz_px_linea, float *pos_pulso);

#endif /* WEFAX_H */

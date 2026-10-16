#ifndef ANALIZADOR_H
#define ANALIZADOR_H

#include <stdint.h>
#include "fft.h"

/*
 * ANALIZADOR DEL AUDIO. 23/09/2026.
 *
 * QUE ES. El espectro de lo que sale por el altavoz, dibujado en el mismo
 * panel donde normalmente va el espectro de radiofrecuencia. No decodifica
 * nada: dice QUE HAY dentro del paso de banda.
 *
 * POR QUE HACIA FALTA. La noche del 23/09/2026 se fueron varias horas en
 * contestar tres preguntas que este analizador contesta de un vistazo:
 *
 *   - "¿Esa subportadora esta en 100 o en 125 Hz?" -que es lo que separa
 *     WWV de BPM-. Hubo que sacar el audio de un video grabado con el movil
 *     y hacerle una FFT de 0,17 Hz en un ordenador.
 *   - "¿Ese tono de 1 kHz es el tic de segundo o un batido?" La respuesta
 *     estaba en que la raya caia en 997,3 y no en 1000,0 exactos.
 *   - "¿Lo que oigo es la emisora o una interferencia de casa?"
 *
 * COMO FUNCIONA. Se reaprovecha entero lo que ya habia:
 *
 *   - La FFT es fft_compute_db(), de entrada real y 256 puntos, que llevaba
 *     en fft.c desde el port sin que la llamara nadie: se quedo sin uso
 *     cuando el espectro paso a entrada compleja.
 *   - El dibujo, la escala, la paleta, el toque y la CASCADA son los del
 *     panel de siempre. La cascada del audio es lo que hace visible un
 *     pulso por segundo: en el trazo es un parpadeo, en la cascada es un
 *     patron.
 *
 * EL ZOOM SALE DE DIEZMAR, no de otra FFT. Ocho etapas de division por dos,
 * cada una con el mismo filtro de 23 coeficientes; se toma la salida de la
 * etapa que haga falta. Asi la resolucion se multiplica por dos en cada
 * paso sin gastar ni un bin mas ni un byte mas de FFT.
 *
 * DONDE ACABA LA BANDA UTIL. En 0,8 de la frecuencia de Nyquist de la etapa,
 * no en 1,0. El filtro de diezmado empieza a dejar pasar alias a partir de
 * ahi, y enseñar esa zona seria enseñar señales que no estan. Lo que se
 * pinta es lo que se ha medido que es verdad: 47,8 dB de rechazo y 0,55 dB
 * de caida en el peor caso -ocho etapas en cascada-, medido en
 * sim/aniztest.c.
 */

/* Los cinco zoom. El numero es el tope de la banda util con audio a 96 kHz;
 * a 48 kHz sale la mitad, y por eso analiz_tope_hz() lo calcula en vez de
 * devolver una constante. */
typedef enum {
    ANALIZ_2400 = 0,   /* ~23 Hz por bin: vista general */
    ANALIZ_1200,       /* ~12 Hz: los tonos de 500, 600, 1000 y 1200 */
    ANALIZ_600,        /* ~5,9 Hz */
    ANALIZ_300,        /* ~2,9 Hz: 100 contra 125 Hz, con ocho bins entre medias */
    ANALIZ_150,        /* ~1,5 Hz: subportadoras con lupa */
    ANALIZ_N
} analiz_zoom_t;

/*
 * 1 mientras el analizador este en marcha. Lo pregunta demod_am.c antes de
 * alimentarlo: con el apagado no se gasta ni una multiplicacion.
 */
uint8_t analiz_activo(void);
void    analiz_stop(void);

/* fs_hz = a cuantas muestras por segundo se va a llamar a analiz_feed(). */
void analiz_start(float fs_hz);
void analiz_zoom(analiz_zoom_t z);
analiz_zoom_t analiz_zoom_actual(void);

/* Muestras de audio, tal cual salen del demodulador. Se llama desde la
 * interrupcion, una vez por bloque, con todo el bloque. */
void analiz_feed(const float *audio, uint32_t n);

/*
 * 1 si hay 256 muestras nuevas listas. Entonces analiz_bins() calcula la FFT
 * y deja FFT_BINS_USEFUL valores en dB. Se separa en dos porque la FFT NO
 * debe correr dentro de la interrupcion.
 */
uint8_t analiz_listo(void);
void    analiz_bins(float *db_out);

/* Hasta donde llega lo que se pinta, y cuanto vale un bin. */
float analiz_tope_hz(void);
float analiz_hz_por_bin(void);
/* Cuantos de los FFT_BINS_USEFUL bins caen dentro de la banda util. */
uint16_t analiz_bins_utiles(void);

#endif /* ANALIZADOR_H */

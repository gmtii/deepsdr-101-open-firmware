#ifndef ALE_RX_H
#define ALE_RX_H

/*
 * ALE 2G: del audio de banda lateral a las palabras. 27/09/2026.
 *
 * ALE (MIL-STD-188-141, "Automatic Link Establishment") es como se buscan
 * las estaciones militares y de gobierno en onda corta: cada una emite cada
 * pocos minutos un "sondeo" con su indicativo, barriendo canales, para que
 * las demas sepan por donde se la oye. Eso es lo que se decodifica aqui, y
 * es la mayor parte de lo que hay en el aire.
 *
 * LA CAPA FISICA, y de donde sale cada numero (FED-STD-1045A, que es el
 * mismo texto que el Apendice A de la MIL-STD):
 *
 *   8-FSK, tonos de 750 a 2.500 Hz cada 250 Hz.
 *   125 simbolos por segundo, 3 bits por simbolo -> 375 bits/s.
 *   El audio entra a 12 kHz, o sea NOVENTA Y SEIS muestras por simbolo
 *   EXACTAS, y los ocho tonos caen en bins exactos de una ventana de 96
 *   (12000/96 = 125 Hz de separacion, y los tonos estan a 250). Por eso
 *   aqui hay ocho Goertzel y no una FFT: sin fugas entre bins, ocho
 *   acumuladores dan lo mismo que una transformada y no hacen falta ni
 *   tabla de giros ni ventana.
 *
 * EL MAPA DE TONO A SIMBOLO ES GRAY Y NO LINEAL. El tono mas bajo no es el
 * simbolo 0, el siguiente el 1, y asi: la asignacion es
 *
 *     750 1000 1250 1500 1750 2000 2250 2500 Hz
 *       0    1    3    2    6    7    5    4
 *
 * o sea sim = rango ^ (rango >> 1). Ponerlo lineal da un decodificador que
 * funciona perfectamente y no acierta ni una palabra.
 *
 * LA REDUNDANCIA NO ESTA ALINEADA A SIMBOLO, y esta es la trampa gorda.
 * Cada palabra son 49 bits que se emiten TRES VECES seguidas -147 bits = 49
 * simbolos- pero la repeticion es a nivel de BIT:
 *
 *     aire[i] = palabra49[i mod 49]
 *
 * y 49 no es multiplo de 3, asi que la segunda copia NO empieza en frontera
 * de simbolo. Quien suponga "tres bloques de simbolos iguales" no decodifica
 * jamas.
 */

#include <stdint.h>

typedef struct {
    uint32_t simbolos;    /* leidos del aire */
    uint32_t palabras;    /* aceptadas */
    uint32_t enganches;   /* veces que ha encontrado el principio de palabra */
    uint8_t  enganchado;  /* 1 = sabe donde empieza cada palabra */
    uint8_t  nivel;       /* 0..99, sin calibrar */
    uint8_t  calidad;     /* votos unanimes de la ultima palabra, 0..48 */
} ale_rx_info_t;

void    ale_rx_start(float fs_hz);
void    ale_rx_stop(void);
uint8_t ale_rx_activo(void);

/* Audio de banda lateral a 12 kHz, desde la interrupcion del DMA. */
void    ale_rx_mete(const float *audio, uint32_t n);

/*
 * Saca la siguiente palabra de 24 bits que haya, o 0 si no hay ninguna.
 * `calidad` (puede ser NULL) recibe los votos unanimes, de 0 a 48.
 *
 * Se llama desde el bucle principal: quien las cierra es la interrupcion.
 */
uint8_t ale_rx_saca(uint32_t *palabra, uint8_t *calidad);

void    ale_rx_info(ale_rx_info_t *out);
void    ale_rx_cuentas_cero(void);

#endif /* ALE_RX_H */

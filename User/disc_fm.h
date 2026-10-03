#ifndef DISC_FM_H
#define DISC_FM_H

#include <stdint.h>
#include "nco.h"

/*
 * DISCRIMINADOR DE FRECUENCIA DE AUDIO. 24/09/2026.
 *
 * Dado un tono de audio que se mueve alrededor de una frecuencia central,
 * devuelve su frecuencia instantanea muestra a muestra. Es lo que necesitan
 * los modos que codifican informacion en la FRECUENCIA de un solo tono:
 *
 *   - WEFAX: 1500 Hz es negro y 2300 Hz es blanco.
 *   - SSTV: lo mismo para el brillo de cada canal de color, y ademas 1200 Hz
 *     para el sincronismo y 1100/1300 Hz para los bits de la cabecera.
 *
 * ESTA AQUI Y NO DENTRO DE CADA UNO porque no es una funcion cualquiera: son
 * tres decisiones medidas -la longitud de los promediadores, que sean dos en
 * cascada, y usar el producto cruzado en vez de un arco tangente- y cada una
 * tiene su razon escrita abajo. Copiadas en dos ficheros, el dia que una se
 * corrija se corregira en uno solo, y el otro seguira funcionando lo bastante
 * bien como para que nadie se entere.
 */

/* Longitud de los dos promediadores en cascada. Ver disc_fm_paso(). */
#define DISC_FM_LPF_N 12U

typedef struct {
    nco_t    nco;
    float    h1i[DISC_FM_LPF_N], h1q[DISC_FM_LPF_N];
    float    h2i[DISC_FM_LPF_N], h2q[DISC_FM_LPF_N];
    uint16_t idx;
    float    s1i, s1q, s2i, s2q;
    float    i_ant, q_ant;
    float    centro_hz;
    float    fs;
    float    nivel;      /* potencia media, para saber si hay señal */
} disc_fm_t;

void  disc_fm_init(disc_fm_t *d, float centro_hz, float fs_hz);

/* Una muestra de audio entra, la frecuencia instantanea en Hz sale. */
float disc_fm_paso(disc_fm_t *d, float x);

float disc_fm_nivel(const disc_fm_t *d);

#endif /* DISC_FM_H */

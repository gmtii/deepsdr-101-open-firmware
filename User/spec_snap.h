#ifndef SPEC_SNAP_H_INCLUDED
#define SPEC_SNAP_H_INCLUDED

#include <stdint.h>

/*
 * TOCAR UNA SEÑAL EN EL ESPECTRO Y CAER ENCIMA - etapa 21, 22/09/2026.
 *
 * Por el dueno del proyecto: "me gustaria hacer clickable el espectro, que yo
 * vea una señal, le clicke y vaya a ella".
 *
 * Tocar el espectro ya sintonizaba, pero caia donde apuntaba el dedo
 * redondeado al paso de sintonia: con el paso en 1 kHz, hasta 500 Hz de
 * error, mas lo que falle la punteria en un panel resistivo. En AM se nota
 * poco y en CW no vale para nada.
 *
 * Esto busca la señal de verdad alrededor del toque y devuelve DONDE ESTA,
 * no donde apuntaste.
 *
 * POR QUE VIVE FUERA DE main.c
 * ----------------------------
 * Porque asi se puede probar. El banco sim/snaptest.c le da espectros
 * inventados -una portadora de AM, un tono de CW junto a otro, una voz en
 * banda lateral, ruido solo- y comprueba a que bin cae. Sin eso, "engancha
 * bien" seria una opinion.
 */

typedef enum {
    /*
     * La señal tiene un pico claro donde hay que sintonizar: la portadora de
     * AM/SAM, el centro de FM, el tono de CW. Se cae en el pico.
     */
    SNAP_PICO = 0,
    /*
     * Banda lateral: no hay portadora, y el pico esta EN MEDIO de la voz.
     * Sintonizar ahi deja la mitad del audio fuera del filtro. Lo que hace
     * falta es el borde por donde empieza la señal: el de abajo en USB, el
     * de arriba en LSB.
     */
    SNAP_USB,
    SNAP_LSB
} snap_modo_t;

/* Cuanto se busca a cada lado del dedo, en bins. */
#define SNAP_VENTANA_BINS   6
/* Sobre cuantos bins se estima el suelo de ruido de alrededor. */
#define SNAP_SUELO_BINS    48
/* Cuanto tiene que destacar el pico sobre ese suelo para creerselo. */
#define SNAP_MIN_DB       8.0f
/* Donde se considera que acaba la señal, contando desde el suelo. */
#define SNAP_BORDE_DB     5.0f

/*
 * Devuelve el bin (con decimales) donde esta la señal mas cercana al toque,
 * o `bin_tap` tal cual si ahi no hay nada que merezca la pena.
 *
 * *encontrado, si no es 0, dice cual de las dos cosas ha pasado - quien
 * llama lo necesita: si NO se ha enganchado hay que redondear al paso de
 * sintonia como siempre, y si SI hay que respetar la frecuencia exacta de la
 * señal, que es justo lo que el redondeo estropearia.
 */
float spec_snap_bin(const float *db, uint32_t n, float bin_tap,
                    snap_modo_t modo, uint8_t *encontrado);

#endif /* SPEC_SNAP_H_INCLUDED */

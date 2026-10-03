#ifndef AUDIOFIL_H
#define AUDIOFIL_H

#include <stdint.h>

/*
 * EL FILTRO DE AUDIO, CON SUS DOS CORTES. Etapa 34, 24/09/2026.
 *
 * QUE CAMBIA
 * ----------
 * Hasta aqui el ancho de audio eran TRES filtros precalculados -4,0 / 2,3
 * / 1,8 kHz- y los tres eran paso BAJO: cortaban por arriba y por abajo
 * dejaban pasar todo. Eso vale para elegir "ancho, medio o estrecho" y no
 * vale para nada mas. Lo que hace falta de verdad en banda lateral es
 * mover los dos cortes por separado: subir el de abajo para quitarse el
 * retumbe y el ronroneo de la red, y bajar el de arriba para quitarse al
 * vecino de al lado, cada uno por su cuenta.
 *
 * Asi que el filtro se DISEÑA en marcha a partir de los dos cortes en vez
 * de elegirse de una lista. Son tres biquads: dos de paso bajo (Butterworth
 * de cuarto orden, que es exactamente lo que eran los tres de antes) y uno
 * de paso alto de segundo orden, que es el que no existia.
 *
 * POR QUE EL DE ABAJO ES DE SEGUNDO ORDEN Y EL DE ARRIBA DE CUARTO
 * ---------------------------------------------------------------
 * No es simetria mal hecha: es que los dos cortes no hacen el mismo
 * trabajo. El de arriba separa de una emisora que puede estar 20 dB por
 * encima, y ahi la pendiente es lo unico que hay. El de abajo quita
 * retumbe y zumbido de red, que son cosas anchas y suaves, y una pendiente
 * fuerte ahi solo añade retardo de grupo justo donde la voz tiene la
 * energia. Cuarto orden arriba, segundo abajo.
 *
 * NO DEPENDE DE NADA DEL GD32, para que el banco mida ESTE codigo. Lo que
 * devuelve son coeficientes en el convenio de CMSIS (a1 y a2 con el signo
 * ya cambiado), listos para arm_biquad_cascade_df1_init_f32().
 */

#define AUDIOFIL_ETAPAS 3U            /* 2 de paso bajo + 1 de paso alto */
#define AUDIOFIL_COEFS (AUDIOFIL_ETAPAS * 5U)

/* Los limites de lo que se puede pedir. El de abajo llega a 0, que quiere
 * decir "sin paso alto"; el de arriba lo recorta la propia tasa. */
#define AUDIOFIL_LO_MIN   0.0f
#define AUDIOFIL_LO_MAX   1000.0f
#define AUDIOFIL_HI_MIN   300.0f
#define AUDIOFIL_HI_MAX   6000.0f
#define AUDIOFIL_ANCHO_MIN 200.0f     /* hi - lo nunca baja de aqui */

/*
 * Diseña el filtro en `coef` (AUDIOFIL_COEFS floats). Recorta `lo` y `hi`
 * a lo posible en vez de fallar: un filtro que no se puede construir tiene
 * que acabar en el filtro mas parecido que si, no en silencio.
 *
 * Devuelve los cortes que de verdad se han usado por lo_usado y hi_usado
 * (los dos punteros pueden ir a cero si no interesan), para que quien lo llame pueda enseñar lo
 * que HAY y no lo que se pidio - que es la diferencia entre un mando que
 * informa y uno que miente en los extremos.
 */
void audiofil_disena(float lo_hz, float hi_hz, float fs_hz, float *coef,
                     float *lo_usado, float *hi_usado);

/* ----------------------------------------------------------------------
 * Para el banco: la respuesta de la cascada a una frecuencia, en dB.
 * ---------------------------------------------------------------------- */
float audiofil_respuesta_db(const float *coef, float hz, float fs_hz);

#endif /* AUDIOFIL_H */

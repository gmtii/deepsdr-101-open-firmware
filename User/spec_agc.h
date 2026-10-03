#ifndef SPEC_AGC_H
#define SPEC_AGC_H

#include <stdint.h>

/*
 * Autoescala del espectro: decide la ventana en dB que se dibuja.
 *
 * POR QUE ESTO ES UN MODULO Y NO VEINTE LINEAS DENTRO DE main.c
 * -------------------------------------------------------------
 * Porque estaba dentro y se rompio sin que se notara. La version de
 * antes cogia el MINIMO y el MAXIMO absolutos del cuadro y ponia la
 * ventana alrededor. Sobre el papel es lo obvio; delante de la radio da
 * esto: basta UN bin disparado -el pico de continua del centro- y otro
 * hundido -los de los bordes tras diezmar- para que la ventana se abra
 * de 1 a 120 dB. Con 119 dB de ventana, una senal que ocupa 20 queda
 * aplastada en dos colores y el relieve entre senales desaparece.
 *
 * O sea que la autoescala parecia no hacer nada, y lo que pasaba es que
 * hacia exactamente lo que se le habia pedido con dos muestras que no
 * representan nada.
 *
 * Ahora usa PERCENTILES en vez de extremos, que es la forma estandar de
 * no dejar que dos valores manden sobre quinientos. Y vive aqui, fuera
 * de main.c, por una razon concreta: asi el simulador puede alimentarlo
 * con cuadros sintéticos -ruido plano, ruido con senal, ruido con pico
 * de continua- y comprobar la ventana que sale. Ver sim/autoesc.c. Eso
 * dentro de main.c no se podia hacer.
 */

/*
 * Limites dentro de los que puede moverse la ventana, y los mismos que
 * respeta el ajuste manual de escala: main.c los usa de aqui, no tiene
 * copia propia. La tuvo, y tener el mismo numero en dos sitios es como
 * se acaba con dos techos distintos segun quien mire.
 *
 * EL TECHO SALE DE UNA CUENTA, no de un numero redondo. La FFT es de 256
 * puntos con ventana de Hann, y lo que se dibuja es 10*log10 de la
 * potencia del bin. La ventana de Hann suma N/2, asi que una senal a
 * fondo de escala del conversor -amplitud 32767- deja en su bin:
 *
 *   entrada I/Q (el espectro):   32767 * 256/2     = 4.194.176
 *                                10*log10(4194176^2) = 132,4 dB
 *
 *   entrada real (el osciloscopio de los modos digitales, que reparte
 *   la senal entre +f y -f):     32767 * 256/4     = 2.097.088
 *                                10*log10(2097088^2) = 126,4 dB
 *
 * O sea que el maximo que el numero puede valer por construccion son
 * 132,4 dB. El techo estaba en 120, DOCE dB por debajo de eso, asi que
 * una estacion local muy fuerte se aplastaba contra el limite en vez de
 * dibujarse. 135 queda por encima de lo alcanzable, que es donde tiene
 * que estar un tope: fuera del camino.
 */
#define SPEC_AGC_DB_FLOOR   (-30.0f)
#define SPEC_AGC_DB_CEIL     135.0f
#define SPEC_AGC_DB_MIN_GAP   10.0f

/* Margenes por debajo del suelo y por encima del techo elegidos, y
 * recorrido minimo garantizado aunque no haya senal ninguna. */
#define SPEC_AGC_FLOOR_MARGIN_DB  3.0f
#define SPEC_AGC_CEIL_MARGIN_DB   6.0f
#define SPEC_AGC_MIN_SPAN_DB     50.0f

/*
 * Percentiles que hacen de suelo y de techo.
 *
 * El de abajo descarta el 5 % mas bajo: los bins hundidos de los bordes
 * y algun cero suelto. El de arriba descarta el 0,5 % mas alto, que con
 * 512 bins son dos o tres - justo los que ocupa un pico de continua, y
 * bastantes menos de los que ocupa una senal de verdad, que son varias
 * decenas. Es la diferencia entre ignorar un artefacto e ignorar lo que
 * se quiere ver, y por eso los dos numeros no son simetricos.
 */
#define SPEC_AGC_PCT_LO   0.05f
#define SPEC_AGC_PCT_HI   0.995f

/* Suavizado del movimiento de la ventana, por cuadro. */
#define SPEC_AGC_SMOOTH_ALPHA 0.05f

/*
 * Mueve *db_min y *db_max un paso hacia donde pide el cuadro. Llamar
 * una vez por cuadro. No toca db[].
 */
void spec_agc_step(const float *db, uint32_t n, float *db_min, float *db_max);

/*
 * La ventana que pide un cuadro, sin suavizar ni recortar contra la
 * anterior. Esta separada de spec_agc_step() para poder comprobarla
 * desde el simulador en un solo paso, sin tener que simular la
 * convergencia.
 */
void spec_agc_target(const float *db, uint32_t n, float *out_min, float *out_max);

#endif /* SPEC_AGC_H */

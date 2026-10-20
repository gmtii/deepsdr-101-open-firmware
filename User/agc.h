#ifndef AGC_H
#define AGC_H

#include <stdint.h>

/*
 * EL CONTROL AUTOMATICO DE GANANCIA, UNO SOLO Y PARA LOS DOS CAMINOS.
 *
 * POR QUE EXISTE ESTE FICHERO
 * ---------------------------
 * Habia DOS bucles de AGC copiados en demod_am.c: el de AM/BLU/FM
 * estrecha y el de FM ancha. El mismo lazo, las mismas constantes, dos
 * sitios. Y el 05/10/2026 se optimizo UNO de los dos -se saco la division
 * de coma flotante de dentro del bucle, ver abajo- y el otro se quedo como
 * estaba. Nadie se entero, porque no hay nada que mire los dos a la vez.
 *
 * Lo malo no es el 1,3% de CPU que costaba eso. Lo malo es la forma: dos
 * copias del mismo lazo se separan solas, y el que se quedo atras es el de
 * FM ANCHA, que es justo el que va mas apretado de tiempo -512 muestras a
 * 192 kHz, el mismo presupuesto de 2,67 ms que los demas, y con un atan2f
 * por muestra en el discriminador-. Es la misma enfermedad de
 * copias_check.py, de manual_check.py y de copias_bin_check.py, por sexta
 * vez en este proyecto: una copia a mano de algo que ya existe es una
 * copia que un dia deja de coincidir.
 *
 * Asi que el lazo pasa a estar UNA vez, aqui, y los dos caminos lo llaman.
 * Lo que cambia por camino son cuatro numeros -el suelo del pico, la
 * relajacion, cuantas muestras y donde va la salida-, y eso son parametros,
 * no un fichero aparte.
 *
 * Y SE PUEDE MEDIR, que es lo que no se podia antes. El simulador no
 * compilaba ni una linea de la cadena de audio: todos los decodificadores
 * tienen banco y el AGC no tenia ninguno. Ahora `make agc` compila ESTE
 * fichero -el de verdad, no una copia- y lo compara muestra a muestra
 * contra un AGC de libro escrito en el propio banco. Ver sim/agc_test.c.
 *
 * LA ARITMETICA, Y POR QUE NO ES LA INGENUA
 * -----------------------------------------
 * Lo evidente seria `ganancia = AGC_TARGET / pico` por muestra. Eso son
 * 96.000 divisiones por segundo en AM y 192.000 en FM ancha, a 14 ciclos
 * cada una en este M4. Y el divisor casi nunca cambia de verdad: el pico
 * solo hace dos cosas, o baja multiplicandose por una constante -la
 * relajacion- o salta al valor de la muestra. Bajar por una constante es,
 * en el inverso, multiplicar por otra constante. Saltar si obliga a
 * dividir, pero eso pasa solo cuando la señal supera su propio maximo, que
 * con la relajacion puesta son unos pocos cientos de veces por segundo.
 *
 * Y SE RESINCRONIZA EXACTO EN CADA BLOQUE, a proposito: arrastrar un
 * inverso multiplicando millones de veces acumularia error de redondeo sin
 * que nadie se entere. Empezando cada bloque con la division de verdad, lo
 * mas que puede separarse son 512 multiplicaciones, o sea una parte en
 * cien mil -del orden de -90 dB, bastante por debajo del escalon del
 * int16-. El banco lo comprueba: compara contra el AGC de libro y exige
 * que no se separen ni un LSB.
 */

/*
 * El objetivo de amplitud a la salida -de los 32767 de fondo de escala- y
 * el techo de ganancia, que es lo que acota hasta donde se amplifica el
 * ruido de banda cuando no hay nada. El ataque es INSTANTANEO en todos los
 * perfiles (la comparacion incondicional de dentro del bucle); lo unico
 * que cambia entre perfiles es la relajacion.
 */
#define AGC_TARGET   18000.0f
#define AGC_GAIN_MAX   300.0f

/*
 * Un bloque de audio por el AGC, y a la salida estereo de int16 duplicando
 * el canal. Devuelve el pico nuevo, que el que llama guarda para el bloque
 * siguiente.
 *
 *   entra        las muestras de este bloque
 *   sale         2*n int16: la misma muestra en L y en R
 *   n            cuantas
 *   pico         el pico que dejo el bloque anterior
 *   relajacion   cuanto baja el pico POR MUESTRA (ver AGC_RELEASE_* en
 *                demod_am.c: depende del perfil Y de la frecuencia de
 *                muestreo, porque es por muestra y no por segundo)
 *   pico_min     el suelo del pico. NO es el mismo en los dos caminos:
 *                40 en AM/BLU/FM estrecha y 4000 en FM ancha, porque la
 *                escala de la señal a la salida del discriminador es otra
 *                -ver WFM_AGC_PEAK_MIN en demod_am.c, que lo cuenta
 *                entero-
 *   ganancia     el factor fijo de detras (DEMOD_AM_GAIN)
 */
float agc_bloque(const float *entra, int16_t *sale, uint32_t n,
                 float pico, float relajacion, float pico_min,
                 float ganancia);

/*
 * El perfil MANUAL: ni pico ni lazo, ganancia fija y a recortar. Se deja
 * aqui al lado y no en el que llama porque las dos mitades del "if" tienen
 * que escribir la salida EXACTAMENTE igual -mismo recorte, mismo
 * duplicado- y tenerlas separadas es como se separan las cosas.
 *
 * Ojo: a proposito NO toca el pico del que llama. Asi, si se vuelve a un
 * perfil automatico, el lazo sigue donde lo dejo en vez de arrancar de un
 * pico rancio o de cero. Ver el comentario del sitio donde se llama.
 */
void agc_manual(const float *entra, int16_t *sale, uint32_t n,
                float ganancia);

#endif /* AGC_H */

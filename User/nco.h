#ifndef NCO_H_INCLUDED
#define NCO_H_INCLUDED

#include <stdint.h>

/*
 * ============================================================================
 * NCO: UN OSCILADOR DE FRECUENCIA CUALQUIERA, PARA MEZCLAR EN BANDA BASE
 * ============================================================================
 * 23/09/2026.
 *
 * QUE PROBLEMA RESUELVE
 * ---------------------
 * Hasta hoy esta radio sabe desplazar el espectro digitalmente por UNA sola
 * frecuencia: Fs/4. A esa frecuencia exacta la rotacion e^(-j2pi n/4) pasa
 * por 1, -j, -1, +j, o sea que no hay que multiplicar nada -son cambios de
 * signo y un intercambio de I con Q-, y por eso se eligio (ver la nota de
 * LOW-IF TUNING de demod_am.h). Es gratis y es exacta.
 *
 * Lo que no es, es general. Y hacen falta dos cosas que piden justo eso:
 *
 *   - El NCO de sintonia. Hoy cada clic del mando reprograma el oscilador
 *     local, asi que el panorama entero se desplaza y el waterfall acumulado
 *     deja de corresponder con lo que hay debajo. Si el oscilador se queda
 *     quieto y lo que se mueve es SOLO el punto de demodulacion, el espectro
 *     y la cascada se quedan clavados. Pero entonces el desplazamiento
 *     digital deja de ser Fs/4: es la distancia, cualquiera, entre la
 *     frecuencia sintonizada y el oscilador aparcado.
 *
 *   - El RDS. La subportadora va a 57 kHz del centro de la señal de FM. Para
 *     bajarla a banda base hace falta mezclar por 57 kHz, que tampoco es
 *     Fs/4 de nada.
 *
 * Son dos cosas distintas que necesitan el mismo trozo de aritmetica, asi
 * que el trozo va aqui, en su propio fichero, y se prueba UNA vez con un
 * banco de verdad (sim/ncotest.c) en vez de dos veces a ojo.
 *
 * COMO ESTA HECHO Y POR QUE ASI
 * -----------------------------
 * Acumulador de fase de 32 bits. La frecuencia es un incremento entero, asi
 * que la fase NO acumula error: por muchos bloques que pasen, al cabo de un
 * segundo el oscilador ha dado exactamente los giros que le tocaban, con la
 * unica limitacion de que la frecuencia se redondea a la rejilla del
 * acumulador, que a 96 kHz vale 96000/2^32 = 0,0000224 Hz. Esa es la razon
 * de usar un entero y no un float que se va sumando: el float acumula, el
 * entero no.
 *
 * El seno sale de una tabla de 1024 puntos CON INTERPOLACION LINEAL.
 * MEDIDO en sim/ncotest.c, no estimado:
 *
 *     error de frecuencia            0 Hz en casi todas; 1e-5 Hz la peor
 *     deriva de fase en 104 s        0 LSB de 2^32 (el acumulador es entero)
 *     peor espurio, muestreo coherente  -119,9 dBc
 *     error total contra el seno exacto -109,2 dBc  (pico 4,7e-6)
 *
 * El error de pico coincide con lo que dice la teoria para interpolar un
 * seno con paso 2pi/1024 -(2pi/1024)^2/8 = 4,7e-6-, que es la comprobacion
 * de que no hay ningun otro error escondido encima.
 *
 * AVISO SOBRE ESOS NUMEROS, que costo aprenderlo: la primera medida dio
 * -63,6 dBc y casi se queda asi. No era el oscilador, era la VENTANA del
 * analisis - los lobulos laterales de Hann andan por ahi. Medir espurios de
 * un oscilador con una ventana cualquiera mide la ventana. El banco ahora usa
 * muestreo coherente (sin ventana) y ademas compara contra el seno exacto,
 * que no depende del metodo de analisis.
 *
 * La tabla se construye en el arranque en vez de venir escrita: son 1025
 * llamadas a sinf() una sola vez, y evita tener un fichero generado mas que
 * mantener.
 */

typedef struct {
    uint32_t fase;   /* acumulador, vuelta completa = 2^32 */
    uint32_t inc;    /* cuanto avanza por muestra */
} nco_t;

/* Construye la tabla. Idempotente: llamarla dos veces no hace nada malo.
 * Tiene que haberse llamado antes del primer nco_mezcla(). */
void nco_init(void);

/* Pone la frecuencia. Admite negativa (mezcla hacia el otro lado) y se
 * queda con el modulo de Fs, que es lo unico que significa algo. */
void nco_freq(nco_t *o, float hz, float fs_hz);

/* La frecuencia que ha quedado de verdad, despues de redondear al
 * incremento entero. Existe para poder ENSEÑARLA y para que el banco
 * compruebe el error en vez de suponerlo. */
float nco_freq_real(const nco_t *o, float fs_hz);

/* Arranca la fase de cero. Solo hace falta cuando importa el instante
 * inicial (el RDS, por ejemplo); para mezclar sintonia, no. */
void nco_fase_cero(nco_t *o);

/*
 * Multiplica el bloque complejo por e^(-j 2 pi f t), en el sitio.
 *
 * El signo es el MISMO que el de la rotacion de Fs/4 que ya hay en
 * demod_am.c: con f = +Fs/4 esta funcion produce exactamente la misma
 * secuencia 1, -j, -1, +j. Eso no es una coincidencia afortunada, es una
 * comprobacion del banco - si algun dia alguien le cambia el signo a esto,
 * ncotest lo dice antes de que se note como "la banda lateral sale al
 * reves".
 */
void nco_mezcla(nco_t *o, float *i_buf, float *q_buf, uint32_t n);

/* Solo el seno y el coseno de la fase actual, avanzando. Para quien
 * necesita el oscilador pero no tiene un par I/Q que multiplicar. */
void nco_paso(nco_t *o, float *sen, float *cos_);

/*
 * DONDE APARCAR EL OSCILADOR.
 *
 * Esta aqui y no en main.c porque es la unica parte del NCO de sintonia que
 * se puede equivocar en silencio -el resto o suena o no suena- y la unica
 * que se puede probar de verdad en el simulador. main.c se queda con una
 * llamada.
 *
 *   freq_hz       la frecuencia que se quiere sintonizar
 *   lo_actual_hz  el oscilador que hay programado ahora (0 = ninguno)
 *   off_ini_hz    la distancia con la que se aparca la PRIMERA vez (Fs/4,
 *                 o sea la de siempre: asi encender el NCO no mueve nada)
 *   off_min_hz    la distancia minima admitida
 *   off_max_hz    la maxima
 *
 * Devuelve el oscilador que hay que programar. Si devuelve lo_actual_hz es
 * que NO hay que tocar el hardware, que es el caso normal con el NCO puesto
 * y lo que hace que el panorama se quede quieto.
 *
 * LA DISTANCIA NUNCA ES NEGATIVA NI PEQUEÑA, y eso no es cosmetico: la fuga
 * del oscilador local se auto-mezcla a 0 Hz siempre, y manteniendo la
 * emisora a off_min o mas de el, esa fuga se queda FUERA del filtro de canal.
 * Es la misma razon por la que existe el desplazamiento de Fs/4 desde julio.
 * Si se permitiera pasear libremente por toda la ventana, al cruzar por
 * encima del oscilador se oiria una portadora que no esta en el aire.
 */
uint32_t nco_lo_aparcado(uint32_t freq_hz, uint32_t lo_actual_hz,
                         uint32_t off_ini_hz, uint32_t off_min_hz,
                         uint32_t off_max_hz);

#endif /* NCO_H_INCLUDED */

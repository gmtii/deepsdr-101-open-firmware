#ifndef NCO_H_INCLUDED
#define NCO_H_INCLUDED

#include <stdint.h>
#include "nco_tabla.h"   /* k_nco_sen[]: la usa nco_sen_cos_fase(), aqui abajo */

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
 * El seno y el coseno de UNA FASE CUALQUIERA en radianes, de la misma tabla.
 * Para los lazos cuya fase no avanza a paso fijo -un PLL la mueve su filtro
 * de lazo- y que si no tendrian que llamar a sinf() y cosf() por muestra.
 * Ver el comentario en nco.c: de ahi salio el modo SAM yendo a trompicones.
 */
void nco_sen_cos_rad(float rad, float *sen, float *cos_);

/*
 * LA MISMA TABLA, PERO CON LA FASE YA EN LA REJILLA DE 32 BITS - 05/10/2026.
 *
 * nco_sen_cos_rad() de aqui arriba cobra un peaje por recibir radianes: dos
 * bucles de vuelta a [0, 2pi), una multiplicacion y un truncado, y sobre todo
 * dos comparaciones en coma flotante, que en este Cortex-M4 se pagan pasando
 * por el registro de estado del coprocesador. Quien ya lleva la fase en un
 * acumulador entero -que es lo natural para un oscilador, porque no acumula
 * error- no necesita nada de eso.
 *
 * Va EN LA CABECERA y en linea a proposito: el que la usa la llama una vez
 * por muestra dentro de la interrupcion de audio, y a ese ritmo el prologo y
 * el epilogo de una llamada, mas los cuatro accesos a memoria para devolver
 * dos floats por puntero, cuestan tanto como la cuenta. En linea, el
 * compilador se queda los dos resultados en registros del coprocesador y no
 * los escribe en ninguna parte.
 *
 * Es la MISMA cuenta que hace nco.c: alli sen_cos() llama a esta, no hay dos
 * copias. El error esta medido en sim/ncotest.c -4,7e-6 de pico, que es
 * exactamente lo que la teoria da para interpolar un seno con paso
 * 2pi/1024-, y vale para esta igual porque es la misma linea de codigo.
 */
#define NCO_FASE_BITS   10U
#define NCO_FASE_FRAC   (32U - NCO_FASE_BITS)   /* bits de fraccion: 22 */
#define NCO_FASE_MASC   0x003FFFFFUL
#define NCO_FASE_FRAC_1 (1.0f / 4194304.0f)     /* 1 / 2^22 */
#define NCO_FASE_90     0x40000000UL            /* un cuarto de vuelta */

static inline void nco_sen_cos_fase(uint32_t fase, float *sen, float *cos_)
{
    uint32_t i;
    float f;

    i = fase >> NCO_FASE_FRAC;
    f = (float)(fase & NCO_FASE_MASC) * NCO_FASE_FRAC_1;
    *sen = k_nco_sen[i] + f * (k_nco_sen[i + 1U] - k_nco_sen[i]);

    fase += NCO_FASE_90;
    i = fase >> NCO_FASE_FRAC;
    f = (float)(fase & NCO_FASE_MASC) * NCO_FASE_FRAC_1;
    *cos_ = k_nco_sen[i] + f * (k_nco_sen[i + 1U] - k_nco_sen[i]);
}

/*
 * ======================================================================
 * EL PASO POR EL CENTRO: CUANTO MIDE Y QUIEN TIENE QUE SABERLO
 * 30/10/2026.
 * ======================================================================
 *
 * La ventana de +-NCO_CENTRO_HZ alrededor del oscilador es el unico sitio
 * del recorrido donde la emisora cae encima de la continua y encima de su
 * propia imagen. Ya no se evita -ver nco_lo_aparcado()- asi que hay que
 * atravesarla sin romper nada, y eso lo tienen que saber TRES sitios:
 *
 *   - nco_lo_aparcado(), que ya no reaparca ahi dentro;
 *   - demod_am.c, que CONGELA el lazo de continua de radiofrecuencia
 *     mientras se pasa, en vez de dejarlo medir la emisora y restarsela;
 *   - y main.c, que apaga ahi el aprendizaje del corrector de espejo, por
 *     la misma razon por la que ya lo apaga en FM ancha: con la señal en
 *     continua E[x^2] deja de promediar a cero y el estimador se envenena.
 *
 * El numero vive AQUI y no en main.c porque esos tres ficheros tienen que
 * usar el mismo, y hasta hoy era un #define de main.c que los otros dos no
 * podian ver. Son los mismos 6 kHz de siempre.
 */
#define NCO_CENTRO_HZ     6000UL

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
 *   off_min_hz    el margen que se deja al borde de la ventana al reaparcar
 *   off_max_hz    la distancia maxima admitida
 *
 * Devuelve el oscilador que hay que programar. Si devuelve lo_actual_hz es
 * que NO hay que tocar el hardware, que es el caso normal con el NCO puesto
 * y lo que hace que el panorama se quede quieto.
 *
 * ======================================================================
 * EL AGUJERO DEL CENTRO YA NO EXISTE - 30/10/2026, pedido por el usuario:
 * "que recorra los 192 sin saltar en absolutamente nada".
 * ======================================================================
 *
 * Aqui ponia que la distancia no podia ser ni negativa ni pequeña, porque
 * la fuga del oscilador local se auto-mezcla a 0 Hz y una emisora sentada
 * encima del centro se la comia. Esa razon SE CAYO EL 25/10/2026, cuando
 * demod_am.c empezo a RESTAR la continua de I y de Q antes de la bajada en
 * frecuencia (ver s_rf_dc_i): la fuga deja de estar ahi, y entonces lo
 * unico que quedaba del agujero era el salto de once kilohercios que pega
 * el panorama al cruzar el centro -justo lo que el NCO existe para evitar-.
 *
 * El comentario de NCO_OFF_MIN_HZ ya lo decia con todas las letras: "no se
 * arregla ensanchando este agujero... se arregla restando la continua".
 * Restada esta. Lo que queda del agujero es deuda.
 *
 * LO QUE SE PIERDE AL CRUZAR, Y SE DICE: en la ventana de +-off_min
 * alrededor del centro la emisora cae encima de su propia imagen -eso es
 * un receptor de frecuencia intermedia cero y no lo arregla ningun
 * corrector- y el lazo de continua no puede medir. Por eso demod_am.c
 * CONGELA ese lazo ahi dentro en vez de dejarlo comerse la señal, y por
 * eso main.c apaga ahi el aprendizaje del corrector de espejo. Son doce
 * kilohercios de paso, no un sitio donde quedarse a escuchar.
 *
 * Y AHORA SE REAPARCA AL OTRO EXTREMO, no a off_min. Con el centro
 * cerrado, salirse por arriba obligaba a volver a entrar por +off_min y el
 * recorrido entre saltos era off_max - off_min. Abierto el centro, el
 * sitio que deja toda la ventana por delante es el extremo CONTRARIO, con
 * el mismo margen de borde de siempre: el recorrido pasa de 84 kHz a 174
 * a 192 kHz de muestreo.
 */
uint32_t nco_lo_aparcado(uint32_t freq_hz, uint32_t lo_actual_hz,
                         uint32_t off_ini_hz, uint32_t off_min_hz,
                         uint32_t off_max_hz);

#endif /* NCO_H_INCLUDED */

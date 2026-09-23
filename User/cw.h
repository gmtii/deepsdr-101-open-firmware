#ifndef CW_H
#define CW_H

#include <stdint.h>

/*
 * Decodificador de CW (telegrafia Morse) - 21/09/2026.
 *
 * MISMA FORMA QUE rtty.h, A PROPOSITO
 * -----------------------------------
 * Este modulo no sabe nada de modos de demodulacion ni de diezmado: se
 * le entrega un bloque de CW_BLOCK_SAMPLES muestras de audio en float y
 * el decide. Igual que rtty.c, es un OYENTE PASIVO - no toca el audio.
 * Quien decide CUANDO llamarlo es demod_am.c (solo en USB/LSB, porque
 * el CW se recibe como un tono dentro de un paso de banda de banda
 * lateral; en AM/NFM/WFM no significa nada), y de DONDE sale el audio
 * es el mismo s_ssb_dec ya diezmado a 12 kHz que usa el RTTY, sin
 * diezmar dos veces.
 *
 * EN QUE SE PARECE Y EN QUE NO AL RTTY
 * ------------------------------------
 * Se parece en la deteccion: un Goertzel, un filtro de un solo bin, que
 * es exactamente la herramienta cuando solo importa una frecuencia
 * concreta y no el espectro entero.
 *
 * No se parece en nada mas, y conviene tenerlo claro porque es de donde
 * salen todas las decisiones de diseno de aqui abajo:
 *
 *   - El RTTY es sincrono. Tiene una velocidad FIJA conocida de
 *     antemano (45,45 o 50 baudios), un bit de arranque que resincroniza
 *     cada caracter y una longitud de caracter fija de 5 bits. Sabiendo
 *     donde empieza un caracter, se sabe donde esta cada bit.
 *
 *   - El CW es asincrono y lo manda una persona. La velocidad no se
 *     sabe, cambia dentro de la misma transmision, y la informacion
 *     NO esta en el valor de nada sino en las DURACIONES relativas: un
 *     punto, una raya que "deberia" durar tres puntos, un hueco entre
 *     elementos de un punto, entre letras de tres y entre palabras de
 *     siete. Nadie cumple esas proporciones con exactitud.
 *
 * Por eso aqui no hay maquina de bits ni tabla de arranque/parada: hay
 * un ESTIMADOR DE LA DURACION DEL PUNTO que se persigue a si mismo, y
 * todo lo demas se mide contra el. Esa es la parte dificil del CW; la
 * parte de senal es la facil.
 *
 * *** SIN VALIDAR CONTRA UNA SENAL REAL TODAVIA *** - validado si
 * contra CW sintetizado con ruido en el simulador de host, compilando
 * ESTE MISMO fichero, en un barrido de velocidad y de relacion senal-
 * ruido; ver sim/cwtest.c y la tabla de resultados que imprime. Lo que
 * esa prueba NO cubre es lo unico que importa de verdad aqui: el CW
 * hecho a mano, con proporciones irregulares. El sintetizador manda
 * proporciones perfectas.
 */

/*
 * Tamano de bloque que espera este modulo, igual que
 * RTTY_BLOCK_SAMPLES y que DEC_BLOCK_SAMPLES de demod_am.c: 32
 * muestras a 12 kHz, o sea 2,667 ms por bloque. No se incluye desde
 * demod_am.h, se comprueba con _Static_assert(), mismo desacople que
 * ya usan nr_ss.h y rtty.h.
 */
#define CW_BLOCK_SAMPLES 32U

/* Rango de velocidad que el estimador puede alcanzar. Por debajo de 5
 * PPM no hay nadie, y por encima de 60 PPM el punto dura 20 ms, que
 * con saltos de 2,667 ms son 7,5 bloques: ahi la resolucion temporal
 * ya empieza a ser el limite, no el ruido. */
#define CW_WPM_MIN   5.0f
#define CW_WPM_MAX  45.0f

/* Precalcula el coeficiente del Goertzel a partir del tono actual y
 * pone a cero todo el estado: envolvente, umbrales, temporizacion,
 * elemento a medias y buffer de salida. Llamar una vez al arrancar,
 * igual que rtty_init(). */
void cw_init(void);

/*
 * Procesa exactamente CW_BLOCK_SAMPLES muestras de audio, de solo
 * lectura: audio[] no se modifica. `n` tiene que valer
 * CW_BLOCK_SAMPLES en cada llamada, mismo contrato que
 * rtty_process(); si no, la llamada no hace nada, que es mejor que
 * decodificar medio bloque y sacar basura.
 */
void cw_process(const float *audio, uint32_t n);

/*
 * Saca un caracter ya decodificado. Devuelve 1 y escribe *out si
 * habia alguno pendiente, 0 si el buffer esta vacio. Llamar desde el
 * bucle principal, nunca desde la interrupcion, misma division que
 * todo lo demas en este proyecto.
 *
 * Los espacios entre palabras salen como ' ' normales, y el final de
 * cada letra no genera nada: el texto llega seguido, como se escribe.
 */
uint8_t cw_get_char(char *out);

/* Interruptor general, igual que rtty_set_enabled(). Apagado por
 * defecto; lo enciende el modo CW del selector de modos. */
void cw_set_enabled(uint8_t on);
uint8_t cw_get_enabled(void);

/*
 * Tono al que escucha el detector, en Hz. Es el tono que se quiere
 * OIR, y por tanto tambien el que hay que poner en el filtro: el
 * operador sintoniza hasta que el pitido suena a esta frecuencia y el
 * decodificador engancha. 600-800 Hz es lo habitual.
 *
 * Cambiarlo recalcula el coeficiente del Goertzel; no reinicia el
 * estado de temporizacion, asi que se puede mover con el mando
 * mientras se escucha sin perder la velocidad ya aprendida.
 */
void  cw_set_pitch_hz(float hz);
float cw_get_pitch_hz(void);

/*
 * El tono ajustado NO es donde hay que poner la senal: es por donde
 * empezar a buscarla. Hay nueve detectores repartidos cada 62,5 Hz a su
 * alrededor -250 Hz a cada lado- y el decodificador se engancha al que
 * mas energia sostenida ve. Ver la cabecera de cw.c para por que se
 * hizo asi: con un solo detector fijo habia que colocar la senal a mano
 * con precision de decenas de hercios, y si no se acertaba el
 * decodificador escribia texto plausible y sin sentido.
 *
 * cw_get_detect_hz() devuelve donde esta escuchando de verdad, que es
 * lo que hay que marcar en el osciloscopio. cw_get_offset_hz() dice
 * cuanto se aparta eso del tono preferido, o sea cuanto habria que
 * mover el mando para que el pitido suene como a uno le gusta - que
 * ahora es una comodidad y no un requisito.
 */
float cw_get_detect_hz(void);
float cw_get_offset_hz(void);

/* La busqueda se puede apagar, y entonces escucha solo en el tono
 * ajustado. Sirve para trabajar una frecuencia concreta en una banda
 * llena, donde engancharse a la estacion mas fuerte de al lado es
 * justo lo que no se quiere. */
void    cw_set_autotune(uint8_t on);
uint8_t cw_get_autotune(void);

/*
 * Siembra el estimador de velocidad. No es un ajuste que mande: el
 * estimador se mueve solo en cuanto llegan elementos. Sirve para
 * arrancar cerca y enganchar antes, y para desatascarlo si se ha ido
 * a un extremo con una senal mala.
 */
void  cw_set_wpm_hint(float wpm);

/* Velocidad estimada AHORA MISMO, en palabras por minuto, calculada
 * desde la duracion de punto que el decodificador esta usando. Es el
 * mejor indicador de si esta enganchado o no: si el numero baila, no
 * lo esta. */
float cw_get_wpm(void);

/* Diagnostico para la pantalla y para ajustar el tono contra una
 * senal real. Nivel instantaneo del tono, suelo de ruido y pico
 * seguidos por el umbral adaptativo, y estado actual de la tecla. */
float   cw_get_level(void);
float   cw_get_floor(void);   /* media baja: el ruido */
float   cw_get_peak(void);    /* media alta: la senal */
uint8_t cw_get_key(void);

/*
 * 1 cuando lo que esta llegando TIENE FORMA DE MORSE. No mide cuanta
 * senal hay -el nivel no distingue una senal debil del ruido, ver la
 * cabecera de cw.c- sino si las duraciones se agrupan como se agrupan
 * las del codigo: dos montones en relacion 3 a 1, y el hueco entre
 * elementos midiendo lo mismo que el punto.
 *
 * Con esto cerrado el decodificador CALLA. Es la diferencia entre un
 * decodificador que escribe basura plausible cuando no hay nada y uno
 * en el que se puede confiar en lo que escribe.
 */
uint8_t cw_get_decode_ok(void);

/* Error de ajuste medio de los ultimos elementos, 0 = perfecto. Sirve
 * para una barra en pantalla: se ve caer en cuanto engancha. */
float cw_get_quality(void);

#endif /* CW_H */

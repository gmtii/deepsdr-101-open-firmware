#ifndef SEMIBANDA_H
#define SEMIBANDA_H

#include <stdint.h>

/*
 * ======================================================================
 * EL DIEZMADOR DE MEDIO BANDA: 192 kHz -> 96 kHz. 23/10/2026.
 * ======================================================================
 *
 * POR QUE EXISTE. Pedido por el usuario: ver el doble de espectro. El
 * panadaptador enseña exactamente la tasa a la que se muestrea -el ancho
 * es la TASA, no el tamaño de la FFT-, asi que para ver 192 kHz hay que
 * muestrear a 192 kHz. Y eso la radio ya lo hace: la FM ancha corre a esa
 * tasa desde el 05/08 con el mismo codec, el mismo I2S y el mismo DMA.
 *
 * LO QUE NO SE PUEDE HACER es correr TODA la cadena de audio al doble.
 * Lo ultimo medido en la radio fueron 46 % de carga de audio; doblarlo no
 * cabe, y ademas obligaria a volver a calcular cada constante que depende
 * de la tasa -el filtro de canal, el Hilbert, el diezmador a 12 kHz, los
 * biquads, el AGC, el quitarruidos- en un fichero que ningun banco
 * compila. Eso no es ensanchar el espectro, es rehacer la radio.
 *
 * ASI QUE EL ANCHO SE QUEDA EN EL ESPECTRO Y NO LLEGA AL AUDIO. Entran
 * 192 kHz, el panadaptador los ve enteros, y justo despues de bajar en
 * frecuencia la señal que se quiere oir, esto diezma por dos. De aqui en
 * adelante la cadena sigue viendo 96 kHz EXACTOS, con sus mismas
 * constantes, sus mismos filtros y sus mismos quince decodificadores sin
 * tocar una linea.
 *
 * ---------------------------------------------------------------------
 * POR QUE MEDIO BANDA, Y NO UN PASO BAJO CUALQUIERA
 * ---------------------------------------------------------------------
 *
 * Un filtro de medio banda tiene su corte EXACTAMENTE en Fs/4 -aqui
 * 48 kHz- y, por como queda, TODAS sus tomas pares son cero menos la del
 * centro, que vale justo 1/2. Diezmando por dos eso se traduce en que de
 * treinta y cinco tomas solo se multiplican NUEVE -las demas son ceros, o
 * la del centro, que es un desplazamiento-. Un paso bajo corriente del
 * mismo rechazo costaria el triple.
 *
 * EL FILTRO: remez de 35 tomas, transicion de 48 +/- 8 kHz.
 *
 *     rizado 0..40 kHz      0,033 dB
 *     rechazo desde 56 kHz  54,5 dB   (a 72 kHz, 71 dB)
 *     retardo               17 muestras a 192 kHz = 88,5 us
 *
 * QUE HAY QUE RECHAZAR, Y POR QUE LA BANDA DE PASO LLEGA JUSTO A 40 kHz.
 * Diezmando por dos se suman las frecuencias que distan 96 kHz: lo que
 * haya a f-96 cae encima de f. Asi que la pregunta es hasta donde puede
 * estar la señal que se quiere oir, y la respuesta esta en main.c:
 * NCO_OFF_MAX_96K son 40.000 Hz. Con la señal en su sitio de siempre -la
 * frecuencia intermedia baja, 24 kHz- lo que le cae encima viene de
 * 72 kHz y esta 71 dB abajo; en el caso peor, con el NCO de sintonia
 * echado hasta el tope, viene de 56 kHz y esta 54 dB abajo.
 *
 * Una banda de paso mas estrecha habria salido mas barata y habria
 * dejado el tope del NCO sin proteger: la emisora de los 40 kHz tendria
 * un fantasma a 27 dB. Por eso los numeros son estos y no otros.
 *
 * DONDE VA, Y ESO TAMBIEN TIENE MOTIVO: entre el quitapulsos y la bajada
 * en frecuencia. Detras del quitapulsos porque un pulso dura menos
 * muestras cuanto mas deprisa se muestrea, y es ahi donde se caza; y
 * DELANTE de la bajada en frecuencia porque esa, a 96 kHz, es el giro a
 * Fs/4 que no cuesta ni una multiplicacion -solo cambios de signo-.
 * Ponerla detras obligaria a girar a 192 kHz y con NCO: el doble de
 * muestras y multiplicando. La bajada sigue costando cero.
 *
 * Y el retardo es EL MISMO en I y en Q, que es lo que de verdad importa:
 * un retardo distinto entre los dos raíles seria un error de fase, y el
 * rechazo de la banda imagen se iria al suelo. Son las mismas tomas
 * aplicadas a los dos.
 *
 * LO QUE CUESTA: nueve multiplicaciones por muestra de salida y por
 * rail, o sea 4.608 por bloque. Frente a los 533.333 ciclos que dura un
 * bloque, alrededor del 3 %.
 *
 * ---------------------------------------------------------------------
 * LO QUE NO ARREGLA, Y SE DICE
 * ---------------------------------------------------------------------
 *
 * El audio sigue oyendo 96 kHz de ancho, no 192. Se VE el doble y se
 * puede sintonizar en todo lo que se ve -la bajada en frecuencia va
 * delante de esto, a 192 kHz-, pero el ancho instantaneo que llega al
 * demodulador es el de siempre. Para los modos que decodifican una sola
 * señal eso da igual; no daria igual el dia que algo quisiera mirar dos
 * trozos lejanos del espectro a la vez.
 */

/* Las tomas del filtro. 27, de las que 15 no son cero y 7 se multiplican
 * de verdad -ver el comentario de arriba-. */
#define SEMIBANDA_TOMAS   35U

/* Cuantas muestras de historia hay que guardar entre bloques. */
#define SEMIBANDA_HIST    (SEMIBANDA_TOMAS - 1U)   /* 34 */

/* Vacia la historia. Hay que llamarla al cambiar de tasa o de modo: la
 * cola del bloque anterior es de otra señal y se oiria como un chasquido
 * en el primer bloque. */
void semibanda_reset(void);

/*
 * Diezma por dos, EN SITIO, los dos raíles.
 *
 * `i` y `q` traen 2*`n_sal` muestras cada uno y se quedan con `n_sal` en
 * sus primeras posiciones. El resto del array no se toca.
 *
 * EN SITIO DE VERDAD, y por eso se guardan aparte las primeras tomas:
 * la salida m se escribe en el indice m y necesita leer hasta el indice
 * 2m, que esta por delante y aun no se ha pisado... salvo para las
 * primeras muestras, donde la entrada que hace falta es mas vieja que lo
 * ya escrito. Esas se copian a un buffer de 34 antes de empezar. Es lo
 * que permite que esto no pida 4 kB de scratch en un camino de
 * interrupcion.
 */
void semibanda_diezma(float *i, float *q, uint32_t n_sal);

/*
 * ======================================================================
 * Y EL CAMINO DE VUELTA: 96 -> 192 kHz. 24/10/2026.
 * ======================================================================
 *
 * ESTO FALTABA, Y ES EL FALLO QUE SE LLEVO LA ENTREGA DE AYER.
 *
 * El convertidor de entrada y el de salida CUELGAN DEL MISMO RELOJ: el
 * codec manda un BCLK/WCLK para los dos. Asi que poniendo la entrada a
 * 192 kHz, la SALIDA tambien va a 192 kHz y pide 512 tramas por bloque.
 * Y el demodulador, detras del diezmador, solo produce 256.
 *
 * Lo que pasaba entonces: gd32_i2s_stream_write_half() copiaba 512 tramas
 * de un array que mide 256, o sea que la mitad de cada bloque de audio era
 * memoria de mas alla del final. Exactamente el mismo fallo que esta
 * contado en gd32_i2s.c con fecha 04/08/2026 -y que yo volvi a meter por
 * el otro lado: alli se quedo corto el buffer, aqui se quedo corta la
 * cuenta-.
 *
 * Asi que el audio sube otra vez a 192 kHz justo antes de entregarlo. Y
 * sale casi gratis, porque un interpolador de medio banda es medio
 * filtro:
 *
 *   LAS MUESTRAS IMPARES son una COPIA RETRASADA de la entrada. Con los
 *   ceros intercalados, la unica toma impar que no vale cero es la del
 *   centro, que vale 1/2, y el factor dos de la interpolacion la deja en
 *   uno. Ni una multiplicacion.
 *
 *   LAS PARES son las nueve multiplicaciones de siempre.
 *
 * O sea la mitad de trabajo que el diezmador, para el doble de muestras.
 *
 * `estereo` trae `n_ent` tramas L/R y se queda con 2*`n_ent`: tiene que
 * admitirlas. El audio es el mismo en los dos canales, asi que se
 * interpola uno y se escriben los dos.
 */
void semibanda_interpola(int16_t *estereo, uint32_t n_ent);

#endif /* SEMIBANDA_H */

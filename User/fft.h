#ifndef FFT_H
#define FFT_H

#include <stdint.h>

/*
 * Real-input FFT for the spectrum/waterfall display, self-contained
 * and with NO libm dependency (this project links with nano.specs and
 * no -lm - same constraint that shaped the I2S test tone code).
 * Classic radix-2 Cooley-Tukey, Hann window, power converted to "dB"
 * via a fast bit-manipulation log2 approximation (not exact, but more
 * than good enough for coloring a waterfall/spectrum display - not
 * intended as a calibrated measurement).
 */

/*
 * EL TAMAÑO SE PUEDE FORZAR DESDE FUERA, Y SOLO PARA MEDIR - 24/10/2026.
 *
 * *** Pedido por el usuario: "empieza a medir". ***
 *
 * A 192 kHz el bin son 750 Hz en vez de 375, porque la FFT sigue siendo de
 * 256 puntos sobre el doble de tramo. Una emisora de fonia cabe entonces en
 * tres o cuatro bins y sale con la punta plana -el usuario lo vio-. La
 * pregunta es si una FFT de 512 cabe en el presupuesto de la radio, y para
 * contestarla hay que poder COMPILAR el fichero de verdad con 512.
 *
 * El nombre es feo a proposito. Si esto se llamara FFT_SIZE a secas,
 * cualquier -DFFT_SIZE suelto en una linea de ordenes cambiaria la
 * transformada de toda la radio en silencio. Asi hay que pedirlo por su
 * nombre, y ese nombre dice para que es. Ver tools/fft_coste_check.py.
 */
#ifdef FFT_SIZE_BANCO
#define FFT_SIZE        FFT_SIZE_BANCO
#else
/*
 * ======================================================================
 * 512 OTRA VEZ, Y PARA TODOS - 24/10/2026.
 * ======================================================================
 *
 * *** Avisado por el usuario: "creo que en 192 estoy viendo señales que no
 * terminan en pico". Y tenia razon. ***
 *
 * HISTORIA, que explica por que esto ha ido y venido: eran 512 cuando todo
 * iba a 192 kHz, bajaron a 128 al mover AM/SSB/NFM a 48, y volvieron a 256
 * al asentarse en 96. La regla escrita era "siempre en paso con
 * SDR_RX_BLOCK_SAMPLES". Lo que paso el 23/10 es que los 192 volvieron
 * para todos los modos y el bloque paso a 512... y esto se quedo en 256.
 * La regla se rompio y nadie se entero, porque la guarda de main.c que la
 * defiende compara contra la constante de 96 kHz.
 *
 * LO QUE SE VEIA: a 192 kHz el bin son 750 Hz. La traza son 756 pixeles
 * para 256 bins, o sea ~3 columnas por bin, y una emisora de fonia de 2,5
 * kHz cabe en 3,3 bins: tres escalones y meseta. No es una averia, es que
 * no quedan puntos para dibujarla.
 *
 * ESTO ES EL TAMAÑO DE LAS TABLAS Y EL TOPE, no el de cada transformada.
 * El panadaptador elige el suyo en caliente -ver fft_tam_iq()- y lo pone
 * igual al bloque de la captura, que es la unica ventana que llega
 * SEGUIDA:
 *
 *     192 kHz   bloque 512   ->   375,0 Hz por bin
 *      96 kHz   bloque 256   ->   375,0 Hz por bin
 *      48 kHz   bloque 256   ->   187,5 Hz por bin
 *
 * El primer intento de esto puso 512 para todos y junto dos bloques a 96 y
 * a 48. Salio mal por partida doble -cadencia de cuadros a la mitad y la
 * ventana montada con dos trozos separados 20 ms-; esta contado con
 * detalle en fft_tam_iq(), en fft.c.
 *
 * Y DE DONDE SALE LA MEMORIA, que era el problema de verdad: de
 * rtty_scope.c, que llevaba UNA FFT DE 512 ENTERA Y DUPLICADA -sus propias
 * tablas de giro, ventana y tabla de bits, 9.216 bytes de TCM-. Ahora usa
 * esta. O sea que la radio ya tenia dentro la FFT de 512 que hacia falta;
 * solo que era del modulo equivocado. Ver fft_potencias().
 */
#define FFT_SIZE        512U   /* potencia de dos; ver el bloque de arriba */
#endif
#define FFT_BINS_USEFUL (FFT_SIZE / 2U)  /* real input: bins 0..Nyquist; the rest mirror these */
#define FFT_BINS_IQ     FFT_SIZE         /* complex I/Q input: every bin is meaningful */

/* Blank the DC bin (and its two Hann-spread neighbors) in the I/Q
 * spectrum, hiding the QSD/codec DC-offset spike that would otherwise
 * sit right on the VFO center line. Cosmetic only. */
#define FFT_IQ_DC_BLANK 1

/* Must be called once at startup (precomputes the twiddle table). */
void fft_init(void);

/*
 * EL ESPECTRO COMPLEJO, PARA QUIEN LO NECESITE - 19/10/2026.
 *
 * fft_compute_db_iq() calcula el espectro complejo y lo convierte a dB en el
 * mismo bucle, asi que la FASE solo existe durante unas lineas. Quien la
 * necesite se engancha aqui y recibe los dos arrays -parte real e
 * imaginaria, FFT_SIZE puntos, en ORDEN NATURAL (bin 0 = continua), no
 * desplazado- justo despues de las mariposas.
 *
 * Hoy lo usa la calibracion del corrector de imagen (iqbal_calibra), que
 * mide el cociente complejo entre una portadora y su bin espejo. Va por
 * puntero y no por llamada directa para que fft.c siga siendo una
 * transformada y nada mas: con la llamada escrita por su nombre, los bancos
 * que enlazan fft.c a solas dejan de enlazar.
 *
 * El gancho corre DENTRO del bucle de pantalla, una vez por FFT. No puede
 * bloquear ni llamar a nada que espere.
 */
void fft_set_bins_hook(void (*f)(const float *re, const float *im, uint32_t n));

/*
 * EL TAMAÑO DE LA TRANSFORMADA DEL PANADAPTADOR, EN CALIENTE - 24/10/2026.
 *
 * Afecta SOLO a fft_compute_db_iq() y fft_compute_db_iq_ex(): `n` puntos de
 * entrada y `n` bins de salida, en vez de FFT_SIZE. Los dos caminos de
 * entrada real -fft_compute_db() y fft_potencias(), que es el osciloscopio
 * de los nueve modos- siguen siendo de FFT_SIZE pase lo que pase.
 *
 * `n` tiene que ser potencia de dos, entre 64 y FFT_SIZE. Lo que no lo sea
 * se ignora y se sigue con el de antes. Las tablas no se recalculan: las de
 * FFT_SIZE valen para los tamaños menores, y por que, en fft.c.
 *
 * SE LLAMA DESDE EL BUCLE PRINCIPAL, nunca desde una interrupcion, y nunca
 * con una transformada a medias: cambia los arrays que el panadaptador esta
 * a punto de recorrer. Quien lo llame tiene que tirar el promedio del
 * cuadro que lleve acumulado, porque esta en el numero de bins viejo.
 */
void fft_tam_iq(uint32_t n);
uint32_t fft_tam_iq_actual(void);

/*
 * Input: `samples` (FFT_SIZE real samples, e.g. int16_t already
 * de-interleaved from sdr_rx). Output: `db_out` (FFT_BINS_USEFUL
 * values, uncalibrated relative "dB" - higher means more energy).
 * Applies a Hann window internally before transforming.
 */
void fft_compute_db(const int16_t *samples, float *db_out);

/*
 * La misma, con la entrada en float y la salida en POTENCIA en vez de
 * decibelios: es lo que quiere el osciloscopio de RTTY. Ver su comentario
 * en fft.c - venia con una FFT entera duplicada y lo unico distinto de
 * verdad era la carga y la salida.
 *
 * `muestras` son FFT_SIZE reales; `pot_out`, FFT_BINS_USEFUL potencias.
 * BUCLE PRINCIPAL, nunca desde una interrupcion: comparte s_re/s_im con el
 * panadaptador.
 */
void fft_potencias(const float *muestras, float *pot_out);

/*
 * Complex I/Q transform for the panadapter display. Input: FFT_SIZE
 * samples of I and Q (same length, same block). Output: FFT_BINS_IQ
 * values in FFTSHIFT order - index 0 is -Fs/2 (24kHz below the VFO at
 * 48kHz sampling - was 96kHz @ 192kHz before 04/08/2026, see
 * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment), index FFT_SIZE/2 is DC
 * (the VFO itself), the last index is just under +Fs/2. Feeding this
 * straight to spectrum_draw() yields the classic centered panadapter:
 * VFO in the middle, lower frequencies left, higher right.
 */
void fft_compute_db_iq(const int16_t *i_samples, const int16_t *q_samples,
                        float *db_out);

/*
 * LA MISMA, DICIENDO SI LAS MUESTRAS SON LAS CRUDAS - 24/10/2026.
 *
 * *** Encontrado revisando el aviso del usuario sobre las señales espejo. ***
 *
 * El gancho de arriba existe para UNA cosa: medir el espejo comparando cada
 * bin con el simetrico respecto al CENTRO, y el centro tiene que ser el
 * oscilador. Eso solo es cierto cuando lo que entra es el bloque crudo.
 *
 * Con el zoom del espectro puesto no lo es, por DOS motivos a la vez:
 *
 *   - zoom_process_block() REMEZCLA a la frecuencia sintonizada antes de
 *     diezmar, asi que el centro de esa FFT es el dial y no el oscilador.
 *     El "bin espejo" empareja entonces dos emisoras que no tienen nada
 *     que ver, y lo que se mide es la diferencia entre dos señales de
 *     verdad.
 *   - y va diezmado 2, 4 u 8 veces, asi que el eje de frecuencias esta
 *     comprimido respecto al de las muestras sobre las que el FIR actua:
 *     aunque el centro fuese el bueno, las tomas saldrian colocadas en la
 *     frecuencia equivocada.
 *
 * El resultado de cualquiera de los dos es un FIR que METE espejo. Por eso
 * la llamada del camino del zoom pasa crudo = 0 y no avisa al gancho.
 *
 * fft_compute_db_iq() es esta misma con crudo = 1, que es lo que hacia
 * siempre.
 */
void fft_compute_db_iq_ex(const int16_t *i_samples, const int16_t *q_samples,
                          float *db_out, uint8_t crudo);

#endif /* FFT_H */

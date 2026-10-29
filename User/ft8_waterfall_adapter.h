#ifndef FT8_WATERFALL_ADAPTER_H
#define FT8_WATERFALL_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "ft8/decode.h"

/*
 * Feeds our OWN ft8_fft.c (a dedicated 1024-point real-input FFT -
 * see that module's header for why it's separate from fft.c/the main
 * spectrum's own 256-point one) into ft8_lib's ftx_waterfall_t format,
 * without needing kiss_fft, monitor.c, or audio.c from ft8_lib at all
 * - decode.h's API expects a caller-filled magnitude buffer, nothing
 * more.
 *
 * ft8_lib decodes real, already-demodulated audio (like WSJT-X does
 * off a USB receiver's audio output), NOT raw I/Q - confirmed by
 * reading common/monitor.c and demo/decode_ft8.c.
 *
 * ======================================================================
 * LA BANDA ENTERA, DE 0 A 3.200 Hz - 28/10/2026
 * ======================================================================
 *
 * *** Las cifras de abajo -3.200 Hz, 1.024 puntos, 256 bins, 1.600 Hz de
 * ventana- son LAS DE ANTES. Se dejan porque explican de donde salio
 * cada una, pero hoy la cadena va al doble: 6.400 Hz de muestreo, FFT de
 * 2.048 y 512 bins, que son los 3.200 Hz enteros que entrega demod_am. ***
 *
 * El tope de "1.600 Hz es lo que cabe" que esta escrito mas abajo era
 * cierto CON OCHO BITS POR CELDA. Con cuatro (ver FT8_ADAPTER_MAG_BITS)
 * mag[] mide lo mismo con el doble de bins, y lo que sobraba pago la FFT
 * grande. Los numeros estan en ese mismo comentario.
 *
 * Lo que se midio al hacerlo, con sim/ft8_banda.c:
 *
 *     la ultima emision que se lee        1.500 Hz  ->  3.000 Hz
 *     de 25 repartidas por la banda           13    ->      20
 *     rejilla de medio bin            8 8 8 5 0 7 8 8  (igual)
 *     sensibilidad                     -18 dB 6 de 8  (igual)
 *
 * Y NO SE PROBO SOLO LO BARATO: con la FFT en 1.024 a 6.400 Hz -que no
 * costaba ni un byte- la banda entera tambien sale, pero la sensibilidad
 * se cae (el punto de -15 dB deja de leerse y la rejilla de medio bin baja
 * a 2 de 8). Los 2.048 puntos hacen falta de verdad.
 *
 * PARAMETER DERIVATION (09/2026 REVISION - widened from the original
 * 400Hz design, green-lighted by the project owner as step 2 of the
 * memory-budget plan below; LOS NUMEROS SON LOS DE ENTONCES, ver arriba):
 *
 *   sample_rate = 3200 Hz         (hoy 6.400 - see ft8_decimator.h for why
 *                                  this specific rate needed a real
 *                                  rational resampler, not a simple
 *                                  integer decimator like the original)
 *   symbol_period = 0.16s         (FT8_SYMBOL_PERIOD, fixed by the protocol)
 *   block_size  = sample_rate * symbol_period = 512 samples/symbol (hoy 1.024)
 *   time_osr    = 1               (A/B tested - see MEMORY BUDGET CONTEXT below)
 *   subblock_size = block_size / time_osr = 512 samples (=160ms) per feed
 *   freq_osr    = 1               (ERA 2 cuando se escribio esto, y este
 *                                  parrafo se quedo diciendolo DOS SEMANAS
 *                                  despues de bajarlo - ver el bloque del
 *                                  25/09/2026 justo debajo de FREQ_OSR y
 *                                  "LO QUE ESTO ES HOY" mas abajo)
 *   nfft        = block_size * 2 = 1024  <-- == ft8_fft.h's FT8_FFT_SIZE, exactly
 *                                  (el 2 es la geometria de la ventana de
 *                                  analisis, NO freq_osr: ver RAW_BIN_STEP)
 *   native bin spacing at 3200Hz/1024pt = 3200/1024 = 3.125 Hz = 6.25Hz / freq_osr
 *     -> same ratio as the original 800Hz/256pt design (quadrupled both
 *        fs and nfft proportionally) - our own FFT's raw bins still sit
 *        exactly on ft8_lib's oversampled-bin grid, no extra interpolation
 *        needed (see ft8_waterfall_feed_subblock() and FT8_ADAPTER_RAW_BIN_STEP).
 *   max_blocks  = FT8_SLOT_TIME / symbol_period = 15.0/0.16 = 93 (truncated,
 *                 matches ft8_lib's own demo/monitor.c formula exactly - unchanged)
 *
 * FT8_ADAPTER_NUM_BINS = 256 (256 * 6.25Hz = 1600Hz search span) - the
 * ENTIRE useful output of a 1024-pt real FFT at 3200Hz
 * (FT8_FFT_BINS_USEFUL=512 raw bins / RAW_BIN_STEP(2) = 256 6.25Hz-bins),
 * same "cover everything available, not a narrow pre-tuned window"
 * choice as the original 400Hz design.
 *
 * HOY SON 512 bins = 3.200 Hz, que es esa misma regla con la cadena al
 * doble: FT8_FFT_BINS_USEFUL=1.024 bins crudos / RAW_BIN_STEP(2). Ver el
 * bloque del 28/10/2026 arriba del todo.
 */
/*
 * ======================================================================
 * LO QUE ESTO ES HOY, MEDIDO Y NO CONTADO - 23/10/2026
 * ======================================================================
 *
 * *** Avisado: la radio decodifica menos estaciones de las que se oyen
 * en antena. Y hasta hoy no habia forma de contestar con un numero: toda
 * esta cadena no tenia ningun banco que la recorriera entera. Ahora lo
 * tiene -deepsdr-ui/sim/ft8_banda.c, que mete audio de 12 kHz por un lado
 * y cuenta mensajes por el otro, compilando ESTOS ficheros- y estas son
 * sus medidas. ***
 *
 * 1) LA VENTANA SE ACABA EN 1.600 Hz, Y SE ACABA DE GOLPE.
 *
 *    *** ESTO SE ARREGLO EL 28/10/2026: hoy la ventana son los 3.200 Hz
 *    enteros y de las veinticinco se leen veinte. Lo de abajo es el
 *    diagnostico que lo puso en numeros, y se queda porque es el que hizo
 *    falta para decidir que pagar. Ver el bloque de arriba del todo. ***
 *
 *    Con veinticinco
 *    emisiones repartidas de 300 a 2.700 Hz, el firmware lee TRECE: todas
 *    las de 300 a 1.500 y ninguna de 1.600 para arriba. No es que decodifique
 *    peor ahi: es que ahi no hay nada que mirar - el remuestreador entrega
 *    3.200 muestras por segundo y 1.600 Hz es su Nyquist.
 *
 *    O sea que "decodifica menos de lo que entra" es CIERTO y el numero es
 *    13 de 25. Y no es un fallo escondido: es el precio de la memoria, esta
 *    escrito aqui debajo desde el primer dia y la pantalla pinta la raya de
 *    la ventana para que se pueda sintonizar alrededor de ella.
 *
 * 2) HAY UN AGUJERO A MEDIO BIN, y este si costaba entenderlo. La ventana de
 *    analisis mide 1.024 muestras -DOS simbolos- asi que sus bins estan a
 *    3,125 Hz, y de ellos se guarda UNO DE CADA DOS (RAW_BIN_STEP). Una
 *    emision que caiga en un bin PAR se ve entera; una que caiga justo en
 *    medio, a 3,125 Hz, cae en el bin impar que no se guarda y se pierde
 *    6 dB. Medido al borde de la sensibilidad (-15 dB, ocho semillas):
 *
 *        +0,00 Hz  8/8      +3,12 Hz  0/8
 *        +0,78 Hz  8/8      +3,91 Hz  7/8
 *        +1,56 Hz  8/8      +4,69 Hz  8/8
 *        +2,34 Hz  5/8      +5,47 Hz  8/8
 *
 *    O sea: una de cada ocho frecuencias es un agujero y otra esta a medias.
 *
 * 3) Y LO EVIDENTE NO ARREGLA ESO. Se probo -compilando, no razonando- la
 *    alternativa obvia: muestrear a 6.400 y hacer la FFT sobre UN simbolo,
 *    con lo que los bins caen a 6,25 Hz naturales y el agujero desaparece.
 *    Desaparece, si. Y la sensibilidad empeora TRES dB en todas las
 *    frecuencias, porque un bin de 6,25 Hz recoge el doble de ruido que uno
 *    de 3,125:
 *
 *        hoy (dos simbolos, bins pares)   -15 dB: 8/8 ... -18 dB: 5/8
 *        a 6.400 (un simbolo)             -12 dB: 8/8 ... -15 dB: 0/8
 *
 *    Gana el de hoy en seis de las ocho frecuencias y pierde en una. Asi que
 *    NO SE CAMBIA. Lo que arreglaria las dos cosas a la vez es guardar los
 *    dos subbins (FREQ_OSR=2 de verdad), y eso dobla mag[]: 47.616 bytes
 *    donde hay 60.304 para todo FT8. No cabe, y por eso esta medido y
 *    escrito aqui en vez de intentado otra vez dentro de seis meses.
 *
 * 4) LO QUE NO ERA EL PROBLEMA, tambien medido, para que nadie lo vuelva a
 *    mirar: el tope de candidatos (140, se usan 20), el umbral de
 *    sincronismo (bajarlo de 10 a 3 no cambia NI UNA decodificacion), las
 *    iteraciones del LDPC (de 25 a 60 gana una de ocho a -18 dB y cuesta el
 *    doble de cuenta), el borrado de la ventana deslizante en cada ranura y
 *    el arranque del AGC en cada ranura (ninguno de los dos cambia nada).
 */
#define FT8_ADAPTER_SAMPLE_RATE_HZ 6400 /* era 3200, y antes 800 - ver ft8_decimator.h para el remuestreador que hace falta */
#define FT8_ADAPTER_TIME_OSR       1   /* A/B tested against the ft8_lib WAV corpus: TIME_OSR alone costs no measured decode sensitivity (still decodes the same reference signals as the original (2,2) design across 5 corpus files) */
/*
 * 2 -> 1, EL 25/09/2026, Y ES UN CAMBIO CON PRECIO. Lo elige el usuario del
 * proyecto despues de ver la radio en antena: oye 2,4 kHz de emisiones y
 * decodifica tres.
 *
 * Lo que se gana: FREQ_OSR multiplica el tamano de mag[], asi que bajarlo a
 * 1 deja sitio para el DOBLE de bins. La ventana paso de 600 a 1.200 Hz ese
 * dia, y a 1.600 cuando ademas se encontraron los 12.800 bytes de la banda
 * de dibujo (ver la escalera medida en NUM_BINS, mas abajo).
 *
 * Lo que se paga, dicho con el numero que hay y no con uno inventado: el
 * proyecto padre hizo el A/B contra el corpus de ft8_lib y con FREQ_OSR=1
 * perdio UNA decodificacion de las mas debiles (CT3IQ EI8GVB IO63, a -9 dB)
 * sobre cinco ficheros. O sea, algo menos de sensibilidad en lo mas flojo.
 *
 * Por que compensa AQUI: la ventana no es el limite de sensibilidad, es el
 * limite de cuantas estaciones se miran siquiera. Una senal fuerte fuera de
 * la ventana no se decodifica nunca, y eso es peor que una debil que se
 * decodifica peor. Es reversible: volver a 2 es cambiar este numero y bajar
 * NUM_BINS hasta que mag[] vuelva a caber (con la ventana de hoy serian 128
 * bins, o sea 800 Hz; con la de aquel dia eran 96).
 *
 * Y LO QUE CUESTA, MEDIDO AQUI Y NO HEREDADO - 23/10/2026: una de cada ocho
 * frecuencias se queda sin decodificar al borde de la sensibilidad. Ver el
 * bloque "LO QUE ESTO ES HOY" al principio de este fichero.
 */
#define FT8_ADAPTER_FREQ_OSR       1   /* ver el bloque de arriba (era 2) - A/B testing showed THIS is the axis that actually matters: reducing it to 1 (alone or combined with TIME_OSR=1) lost a real decode (CT3IQ EI8GVB IO63, -9dB) that the (2,2) and (1,2) configs both still get */
#define FT8_ADAPTER_BLOCK_SIZE     1024 /* = FT8_ADAPTER_SAMPLE_RATE_HZ * FT8_SYMBOL_PERIOD */
#define FT8_ADAPTER_SUBBLOCK_SIZE  1024 /* = FT8_ADAPTER_BLOCK_SIZE / FT8_ADAPTER_TIME_OSR - one snapshot per full symbol period (160ms), since TIME_OSR=1 leaves no time-subdivision to fill */
#define FT8_ADAPTER_NFFT           2048 /* MUST equal ft8_fft.h's FT8_FFT_SIZE, enforced in the .c file */

/*
 * RAW_BIN_STEP: how many of ft8_fft_compute_db()'s raw 3.125Hz-
 * spaced output bins make up one ft8_lib "native" 6.25Hz bin - a fixed
 * property of THIS sample rate/FFT size pair (3200Hz / 1024pt: 3200/
 * 1024 = 3.125Hz/bin, same ratio the original 800Hz/256pt design had),
 * NOT of FT8_ADAPTER_FREQ_OSR - kept as its own constant (rather than
 * using FREQ_OSR directly in the encode formula) because this project
 * also tested FREQ_OSR=1 at one point (see MEMORY BUDGET CONTEXT
 * below), where the two concepts genuinely diverge; using RAW_BIN_STEP
 * explicitly is what made that test possible without corrupting the
 * frequency mapping. See ft8_waterfall_adapter.c's encode loop for how
 * this is actually used: src_bin = MIN_RAW_BIN + bin*RAW_BIN_STEP +
 * freq_sub*(RAW_BIN_STEP/FREQ_OSR).
 */
#define FT8_ADAPTER_RAW_BIN_STEP   2

/*
 * MEMORY BUDGET CONTEXT (09/2026) - two-step plan, per the project
 * owner, to widen the search window beyond the original 400Hz without
 * mag[] becoming impossibly large (block_stride = time_osr*freq_osr*
 * num_bins):
 *
 * STEP 1 (done first, fs still 800Hz/nfft still 256/window still
 * 400Hz) - A/B tested which oversampling axis actually costs decode
 * sensitivity if reduced from ft8_lib's own (2,2) default to 1:
 *
 *   (2,2), NUM_BINS=64  (400Hz): 93*2*2*64  = 23808 bytes (~23.2KB) - the ORIGINAL design
 *   (1,1), NUM_BINS=64  (400Hz): 93*1*1*64  =  5952 bytes (~5.8KB)  - LOST a real decode (CT3IQ EI8GVB IO63, -9dB)
 *   (2,1), NUM_BINS=64  (400Hz): 93*2*1*64  = 11904 bytes (~11.6KB) - ALSO lost that decode - FREQ_OSR is the sensitive axis
 *   (1,2), NUM_BINS=64  (400Hz): 93*1*2*64  = 11904 bytes (~11.6KB) - kept the decode, tested against 5 corpus files - the config actually shipped
 *
 * Conclusion: TIME_OSR is the "free" axis (halves mag[] with no
 * measured cost), FREQ_OSR is not.
 *
 * STEP 2 (this revision) - spends the RAM step 1 freed on an actually
 * wider window, via ft8_decimator.h's new rational resampler +
 * ft8_fft.h's bigger FFT:
 *
 *   (1,2), NUM_BINS=256 (1600Hz, fs=3200Hz/nfft=1024): 93*1*2*256 = 47616 bytes (~46.5KB)
 *
 * i.e. a 4x wider window (400Hz -> 1600Hz) for about double the
 * original (2,2)/400Hz design's RAM (23.2KB -> 46.5KB) - only possible
 * because step 1 already halved the per-bin cost first.
 *
 * *** Y LO QUE SE ENTREGO NO FUE ESO: FREQ_OSR acabo en 1 (ver su propio
 * comentario, el del 25/09) y mag[] mide 23.808 bytes, no 47.616. Este
 * parrafo describe el plan, no el resultado; se deja porque explica de
 * donde salen los numeros de arriba, pero las dos cifras del renglon
 * "(1,2), NUM_BINS=256" no son las de esta radio. 23/10/2026. *** Confirmed
 * against real hardware: two live decodes (9dB, 11dB - see this
 * session's own log) at the (1,2)/400Hz stage before committing to
 * this wider revision.
 */
/*
 * Bloques del desvio - ver el comentario de `lado` en ft8_shared_ram.h.
 *
 * Son los que la captura puede escribir mientras dura una decodificacion, a
 * 160 ms por bloque: 24 son 3,84 segundos. La decodificacion tarda menos de
 * uno, asi que sobra margen; y si algun dia no sobrara, no se pierde en
 * silencio - lo cuenta ft8_waterfall_desbordes() y sale en la barra.
 */
#define FT8_ADAPTER_LADO_BLOQUES   24

#define FT8_ADAPTER_MAX_BLOCKS     93  /* = (int)(FT8_SLOT_TIME / FT8_SYMBOL_PERIOD) - unchanged */

/*
 * CUATRO BITS POR CELDA EN VEZ DE OCHO - 28/10/2026.
 *
 * mag[] guardaba 2*dB+240 en un byte, o sea medio dB por escalon y un
 * recorrido de 128 dB. Ninguna de las dos cosas hace falta: el
 * decodificador solo compara los ocho tonos de un simbolo ENTRE SI y
 * contra sus vecinos, y esas diferencias viven en unas pocas decenas de
 * dB dentro de un mismo bloque.
 *
 * Con cuatro bits quedan 16 escalones (FTX_MAG4_PASO_DB a partir de
 * FTX_MAG4_BASE_DB, en Ft8Lib/ft8/decode.h, que es quien los
 * descodifica). Las dos ramas -4 y 8 bits- se pueden compilar desde
 * fuera con -DFT8_ADAPTER_MAG_BITS y -DFTX_MAG_4BITS, que es como se
 * eligieron el paso y la base: barriendolos con sim/ft8_banda.c y
 * sim/ft8_snr.c y comparando contra los 8 bits de siempre.
 *
 * Medido en la configuracion que se entrega -6.400 Hz, FFT de 2.048, 512
 * bins-, 4 bits contra los 8 de siempre:
 *
 *                         8 bits          4 bits, -35 dB y paso 3,4
 *   rejilla medio bin   8 8 8 5 0 7 8 8     8 8 8 5 0 7 8 8   identica
 *   sensibilidad        -18 dB, 6 de 8      -18 dB, 6 de 8    identica
 *   banda llena (25)    22 decodificadas    20
 *   S/R, pendiente      1,09                0,98              mejor
 *   S/R, peor error     4,0 dB              1,0 dB            mejor
 *
 * EL PASO IMPORTA Y LA BASE CASI NO. Barrido con los dos bancos:
 *
 *     base  paso   S/R pendiente  peor     rejilla        banda llena
 *     -35   3,0        1,07       1,5 dB   8 8 8 5 0 7..       21
 *     -35   3,2        1,01       1,1      8 8 8 4 0 7..       20
 *     -35   3,4        0,98       1,0      8 8 8 5 0 7..       20
 *     -36   3,5        0,98       1,1      8 8 8 5 0 6..       20
 *     -40   3,0        1,15       6,0 MAL  8 8 8 5 0 7..       21
 *     -40   4,0        0,98       1,4      8 8 8 4 0 7..       19
 *     -40   5,0         -          -       sensibilidad 2 de 8 en -18 dB
 *
 * Con paso 5 o 6 dB la sensibilidad se cae y con 3 o 3,5 no. La base solo
 * tiene que dejar dentro el recorrido de verdad, que en esta radio va de
 * unos -35 a +6 dB a la salida de la FFT con el desplazamiento de
 * FT8_ADAPTER_DB_OFFSET.
 *
 * Y LA S/R NO EMPEORA -mejora- porque ft8_snr.c dejo de quedarse en el
 * centro del escalon: interpola la mediana DENTRO de el. Con cientos de
 * muestras de ruido eso devuelve la resolucion que el escalon tiraba. Sin
 * esa interpolacion el peor error era de 4,4 dB y la pendiente 1,15.
 *
 * LO QUE SE PAGA: dos de las 25 emisiones del banco de banda llena. Ese
 * banco mete 25 señales en 2.400 Hz, que es una densidad que la banda de
 * verdad no tiene ni de lejos.
 *
 * PARA QUE: para que quepan 512 bins -la banda entera de 0 a 3.200 Hz- en
 * la misma memoria que hoy ocupan 256. mag[] no crece ni un byte.
 *
 * Esto obliga a que FT8_ADAPTER_MAG_BITS y FTX_MAG_4BITS (decode.h) digan
 * lo mismo. No se deja al cuidado de nadie: ft8_waterfall_adapter.c tiene
 * un _Static_assert que no deja compilar si se separan.
 */
#ifndef FT8_ADAPTER_MAG_BITS
#define FT8_ADAPTER_MAG_BITS       4
#endif

/* Celdas por bloque, y los bytes que ocupan los dos buffers. La division
 * entre 8 es exacta con cualquier NUM_BINS par, que son todos los que esta
 * radio ha usado. */
#define FT8_ADAPTER_CELDAS_BLOQUE  (FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS)
#define FT8_ADAPTER_MAG_BYTES      ((FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_CELDAS_BLOQUE * FT8_ADAPTER_MAG_BITS) / 8)
#define FT8_ADAPTER_LADO_BYTES     ((FT8_ADAPTER_LADO_BLOQUES * FT8_ADAPTER_CELDAS_BLOQUE * FT8_ADAPTER_MAG_BITS) / 8)
/*
 * 64 -> 96 BINS, o sea 400 -> 600 Hz de ventana. 25/09/2026.
 *
 * Y es exactamente lo que tiene que pasar: en 40 m las emisiones de FT8 se
 * reparten por unos 2,4 kHz de audio, y el decodificador miraba 400. Se oye
 * todo y se lee un sexto.
 *
 * LO QUE FRENABA NO ERA LA RAM, y eso costo medirlo: era un _Static_assert
 * propio que exigia que el bloque de FT8 cupiera dentro del de la cascada,
 * porque waterfall_init() solo borraba ese. Atar el ancho de la ventana al
 * tamano de la cascada no tiene ningun sentido fisico; se arreglo haciendo
 * que waterfall_init() borre la union entera (ver su comentario) y entonces
 * el tope real aparecio: la SRAM libre.
 *
 * DONDE ESTA EL TOPE, MEDIDO COMPILANDO Y NO ESTIMADO. La escalera entera,
 * porque las tres cifras que importan salieron de aqui:
 *
 *     96 bins, FREQ_OSR 2 .....   600 Hz, 2.022 bytes de SRAM libres
 *    160 bins, FREQ_OSR 1 ..... 1.000 Hz, 6.694    (sin tocar nada mas)
 *    192 bins, FREQ_OSR 1 ..... 1.200 Hz, 3.938    (quitando la cascada de FT8)
 *    224 bins, FREQ_OSR 1 ..... NO CABE
 *    256 bins, FREQ_OSR 1 ..... NO CABE
 *
 * ...y ahi se quedo, hasta que aparecio de donde sacar los 12.800 bytes que
 * faltaban: la banda de dibujo de gfx2, de 16 filas a 8 (ver GFX2_BAND_H, que
 * lleva el coste medido). Con ellos:
 *
 *    256 bins, FREQ_OSR 1, banda de 8 filas ... 1.600 Hz, 4.850 libres
 *
 *** Y ESE TOPE SE MOVIO EL 28/10/2026, subiendo la FFT a 2.048 puntos y
 * el remuestreador a 6.400 Hz: hoy son 512 bins y 3.200 Hz. El parrafo de
 * abajo sigue valiendo, solo que con la cadena al doble. ***
 *
 * Y 256 no es "el ultimo que entra" sino EL TOPE DE VERDAD: 256 bins por 2
 * bins crudos son 512, que es FT8_FFT_BINS_USEFUL entero. 1.600 Hz es el
 * Nyquist del audio diezmado a 3.200, o sea toda la banda que existe despues
 * del remuestreo. Ensanchar mas no significa nada: no hay nada ahi.
 *
 * Y LO QUE FRENABA NO ERA LA RAM AL PRINCIPIO, que costo medirlo: era un
 * _Static_assert propio que exigia que el bloque de FT8 cupiera dentro del de
 * la cascada, porque waterfall_init() solo borraba ese. Atar el ancho de la
 * ventana al tamano de la cascada no tiene ningun sentido fisico; se arreglo
 * haciendo que waterfall_init() borre la union entera.
 *
 * DONDE QUEDA MARGEN SI ALGUN DIA HACE FALTA, aunque ya no sea para
 * ensanchar: cada bin cuesta 186 bytes con FREQ_OSR=1, 93 de mag y otros 93
 * de la copia que se hace al cerrar la ranura (ver mag_snapshot en
 * ft8_shared_ram.h). La copia es LA MITAD, y no hace falta entera: la captura
 * nueva solo puede pisar los primeros bloques mientras dura la
 * decodificacion -unos ocho, a 160 ms por bloque-, asi que copiando ocho en
 * vez de noventa y tres se liberarian unos 23 kB. Serviria, por ejemplo,
 * para volver a FREQ_OSR=2 sin estrechar la ventana. El precio es que
 * ft8_lib tendria que leer de dos sitios segun el bloque, o sea meterle mano
 * por dentro al decodificador. Se deja escrito, no hecho.
 */
#define FT8_ADAPTER_NUM_BINS      512 /* 3.200 Hz: LA BANDA ENTERA - ver arriba */
#define FT8_ADAPTER_MIN_RAW_BIN    0   /* first ft8_fft_compute_db() output bin included in the search window - 0 = start right at DC-adjacent content */

/*
 * LOS BINS CRUDOS QUE SE GUARDAN DE VERDAD - 28/10/2026.
 *
 * El adaptador lee s_dbout[] en src_bin = MIN_RAW_BIN + bin*RAW_BIN_STEP +
 * freq_sub*(RAW_BIN_STEP/FREQ_OSR), o sea de FT8_ADAPTER_DB_PASO en
 * FT8_ADAPTER_DB_PASO empezando en MIN_RAW_BIN. Los demas nunca se miran.
 *
 * Guardarlos costaba 4.096 bytes de la union -la mitad de un dbout de
 * 1.024 floats- y un logaritmo por cada uno. Ahora dbout mide
 * FT8_ADAPTER_DB_BINS y el indice es `bin*FREQ_OSR + freq_sub`, que es la
 * misma cuenta dividida por el paso.
 */
#define FT8_ADAPTER_DB_PASO  (FT8_ADAPTER_RAW_BIN_STEP / FT8_ADAPTER_FREQ_OSR)
#define FT8_ADAPTER_DB_BINS  (FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_FREQ_OSR)

/* ft8_fft_compute_db() uses the SAME formula as the original
 * fft.c-based design (10*log10(power) via a fast log2 approximation),
 * so the SAME calibration offset applies unchanged - see the session's
 * own dB-range measurements (real hardware: noise floor ~-104 to
 * -110dB, real signals topping out a few dB under 0dB after this
 * offset). If real decodes come up empty after any future hardware
 * change (gain staging, front-end revision, etc.), check the on-screen
 * "dB min/max" diagnostic - sale en la chapa de FT8 cuando no hay NINGUN
 * candidato, que es cuando dice algo; ver ft8_chapa() en main.c. (Hasta
 * el 23/10/2026 esta linea mandaba mirar un numero que no se pintaba en
 * ningun sitio: ft8_modo_db() existia y no la llamaba nadie.) - y
 * retune this - it's inherently an iterative, hardware-specific
 * calibration, not something derivable from first principles alone. */
#define FT8_ADAPTER_DB_OFFSET      -112.0f

/*
 * DEJA EL ADAPTADOR COMO RECIEN ENCENDIDO: sin desvio, sin bloques y sin
 * ranura a medias. Se llama UNA vez, al ARRANCAR el modo (ft8_modo_start()),
 * y nunca en el borde de ranura - para eso esta ft8_waterfall_reset(), que a
 * proposito respeta el desvio. Ver el comentario de la funcion para el fallo
 * que existe cuando no se llama.
 */
void ft8_waterfall_arranca(void);

/* Resets the sliding analysis window and the waterfall's block/time_sub
 * counters back to empty - call once at the START of each new 15s slot
 * capture, before the first ft8_waterfall_feed_subblock() call for
 * that slot. Does NOT need to re-zero the mag[] buffer itself (stale
 * old-slot data is harmless - it gets fully overwritten block by block
 * before num_blocks reaches it, and ftx_find_candidates()/
 * ftx_decode_candidate() only ever read blocks 0..num_blocks-1). */
void ft8_waterfall_reset(void);

/* Feed exactly FT8_ADAPTER_SUBBLOCK_SIZE new real audio samples,
 * captured at FT8_ADAPTER_SAMPLE_RATE_HZ (3200Hz, via
 * ft8_decimator.c's resampler) - call once per new subblock as audio
 * streams in (every 160ms), NOT once per full 15s slot: this is fully
 * incremental/streaming, so no separate raw-audio capture buffer is
 * needed anywhere upstream of this call - feed samples straight from
 * wherever the demod audio pipeline produces them at this rate.
 * Internally: shifts the new samples into a 1024-sample sliding
 * window, runs ft8_fft_compute_db() on it (Hann window applied
 * internally), and quantizes FT8_ADAPTER_NUM_BINS of the result into
 * the waterfall's mag[] array at the current (block, time_sub) slot,
 * using ft8_lib's own encoding convention (WF_ELEM_MAG_INT(x) = x,
 * decoded as x*0.5-120 - see decode.h) so ftx_find_candidates()/
 * ftx_decode_candidate() read it exactly as they would data from the
 * reference implementation.
 * A no-op past FT8_ADAPTER_MAX_BLOCKS blocks (the 15s slot is full) -
 * call ft8_waterfall_is_full() to know when to stop and decode. */
void ft8_waterfall_feed_subblock(const int16_t *audio_samples);

/* True once FT8_ADAPTER_MAX_BLOCKS blocks have been collected (the
 * full 15s slot's worth of analysis data is ready) - at that point,
 * pass ft8_waterfall_get() to ftx_find_candidates()/
 * ftx_decode_candidate() (see ft8/decode.h), then ft8_waterfall_reset()
 * before starting the next slot. */
bool ft8_waterfall_is_full(void);

/*
 * CERRAR LA RANURA Y DESVIAR LA CAPTURA. Se llama en el instante en que
 * is_full() da verdadero, y a partir de ahi mag[] queda CONGELADO: contiene
 * la ranura completa y el decodificador puede leerlo tranquilo mientras la
 * captura nueva se va a un buffer aparte. Ver `lado` en ft8_shared_ram.h.
 */
void ft8_waterfall_cierra_ranura(void);

/*
 * REINTEGRAR: se llama cuando la decodificacion ha terminado. Vuelca lo que
 * se capturo desviado al principio de mag[] y la captura sigue ahi. A partir
 * de aqui mag[] vuelve a moverse, asi que NO se puede llamar con una
 * decodificacion a medias.
 */
void ft8_waterfall_reintegra(void);

/* 1 mientras la captura esta desviada. */
uint8_t ft8_waterfall_desviada(void);

/*
 * CUANTOS BLOQUES LLEVA CAPTURADOS LA RANURA QUE ESTA EN CURSO.
 *
 * Existe porque una ranura NO TIENE POR QUE LLEGAR A LOS 93 para poder
 * decodificarse, y hasta el 28/10/2026 la que no llegaba se tiraba entera
 * sin decir nada. Ver el comentario de FT8_MIN_BLOQUES en ft8_modo.c.
 *
 * Desviada devuelve los de la ranura NUEVA -los del desvio-, que es lo que
 * de verdad se esta capturando; sin desviar, los de mag[].
 */
uint16_t ft8_waterfall_bloques(void);

/* Pone a cero la cuenta de desbordes. La llama ft8_modo_start(), para que
 * lo que sale en la chapa sea "desde que se encendio FT8" como promete
 * ft8_modo.h - ver el comentario de la funcion. */
void ft8_waterfall_reset_desbordes(void);

/* Bloques que no cupieron en el desvio, o sea audio perdido porque la
 * decodificacion tardo mas que FT8_ADAPTER_LADO_BLOQUES. Deberia ser 0. */
uint16_t ft8_waterfall_desbordes(void);

/* Returns a pointer to the internal, statically-allocated
 * ftx_waterfall_t - valid for as long as no further
 * ft8_waterfall_feed_subblock()/ft8_waterfall_reset() call has been
 * made (there is exactly one instance, no dynamic allocation
 * anywhere in this adapter). */
const ftx_waterfall_t *ft8_waterfall_get(void);

/* Snapshot mag[] before resetting - see ft8_waterfall_adapter.c's own
 * comment for the full "why" (fixing the structural per-slot timing
 * overrun that no scheduling logic alone could solve). Call the
 * instant ft8_waterfall_is_full() is true, BEFORE ft8_waterfall_reset(). */


/* Calibration diagnostics for tuning FT8_ADAPTER_DB_OFFSET - see its
 * comment. Reports the raw (pre-offset, pre-quantization) dB range
 * ft8_fft_compute_db() actually produced across the whole slot
 * just captured. Meant for bring-up/tuning (print these once, adjust
 * the offset, rebuild), not a runtime feature. */
void ft8_waterfall_get_db_range(float *out_min, float *out_max);

/* Live spectrum readout - the most recent subblock's
 * ft8_fft_compute_db() output over our search window
 * (FT8_ADAPTER_NUM_BINS bins, one value per bin, offset-corrected dB,
 * same units/scale as what gets quantized into mag[]), plus a counter
 * that increments once per ft8_waterfall_feed_subblock() call so a
 * caller can tell when new data has actually arrived (roughly every
 * 160ms) without polling the data itself. */

/* Increments only when the cascade actually pushed a new row (every
 * FT8_WF_ROW_PERIOD subblocks, not every one - see that macro's
 * comment in ft8_waterfall_adapter.c) - poll THIS, not
 * ft8_waterfall_get_update_counter(), to know when to redraw the
 * cascade; using the general update counter would redraw far more
 * often than the data actually changes. */

/* Scrolling cascade history - out_buf[row*out_cols + col], row 0 =
 * newest (most recent subblock), increasing row = further in the
 * past, values are the SAME quantized bytes stored in mag[] (0-255,
 * see decode.h's WF_ELEM_MAG for the dB this represents). Pointer is
 * to the internal static buffer - valid until the next
 * ft8_waterfall_feed_subblock()/ft8_waterfall_reset() call, same
 * lifetime contract as ft8_waterfall_get(). */

#endif /* FT8_WATERFALL_ADAPTER_H */

#ifndef FT8_SNR_H_INCLUDED
#define FT8_SNR_H_INCLUDED

/*
 * EL SNR DE FT8, EN LA REFERENCIA DE 2500 Hz - 13/10/2026.
 *
 * ERA CIERTO, Y EL NUMERO DE ANTES NO ERA UN SNR. ft8_decoder.c ponia
 *
 *     snr_i = (int)(cand->score * 0.5f);
 *
 * y `score` es la CORRELACION DEL PREAMBULO: cuanto se parece lo recibido al
 * patron de sincronismo de FT8. Mide cuanto me fio de haber encontrado una
 * señal ahi, no cuanta señal hay frente al ruido. Es positiva por
 * construccion -una correlacion negativa no seria un candidato- y no tiene
 * ancho de banda de referencia, asi que ni el signo ni la escala significan
 * nada. De ahi el "+11 de media" que se venia observando.
 *
 * El propio comentario de ft8_decoder.c lo decia desde el primer dia: es la
 * formula de relleno del demo de ft8_lib, marcada ALLI como un TODO de
 * "compute better approximation", y se arrastro tal cual.
 *
 * ---------------------------------------------------------------------
 * LA REFERENCIA DE 2500 Hz
 * ---------------------------------------------------------------------
 *
 * WSJT-X da el SNR referido a 2.500 Hz. Aqui cada bin mide 6,25 Hz -3.200 Hz
 * de muestreo / 1024 de FFT = 3,125 Hz por bin crudo, y el adaptador se queda
 * uno de cada dos (FT8_ADAPTER_RAW_BIN_STEP)-, que ademas es exactamente el
 * espaciado entre tonos de FT8. Asi que el termino de referencia es fijo:
 *
 *     10*log10(2500 / 6,25) = 26,02 dB
 *
 * Ese 26 es lo que hace que los numeros salgan negativos. Una señal
 * perfectamente audible tiene +18 dB en su propio bin y -8 dB en 2.500 Hz:
 * el mismo hecho con dos reglas distintas.
 *
 * ---------------------------------------------------------------------
 * DE DONDE SALEN SEÑAL Y RUIDO
 * ---------------------------------------------------------------------
 *
 * De la misma cascada que ya lee el decodificador, sin pedir nada nuevo:
 * 79 simbolos, un bloque cada uno, 8 bins de tono por bloque.
 *
 *   SEÑAL+RUIDO  el MAXIMO de los ocho bins de tono de cada simbolo. Lo
 *                ideal seria el bin del tono que se emitio, pero este
 *                Ft8Lib/ viene recortado y NO TRAE EL CODIFICADOR, asi que
 *                reconstruir los 79 tonos pediria el LDPC, el Gray y el
 *                vector de sincronismo. En una señal ya decodificada el
 *                maximo es el tono bueno casi siempre.
 *
 *   RUIDO        la MEDIANA de los bins vecinos, fuera de la señal y con una
 *                banda de guarda. Mediana y no media a proposito: con otra
 *                estacion al lado, la media del ruido se infla y el SNR sale
 *                bajo por una razon que no es ruido.
 *
 * DOS COSAS QUE SON DONDE SE METE LA PATA, Y QUE AQUI NO SE METEN:
 *
 * 1. HAY QUE VOLVER A LINEAL ANTES DE PROMEDIAR. La cascada guarda
 *    2*dB + 240 (WF_ELEM_MAG lo devuelve como x*0,5 - 120). Promediar dB es
 *    promediar logaritmos, y eso no es la potencia media.
 *
 *    La MEDIANA si se puede sacar en dB, y se saca: la mediana conmuta con
 *    cualquier funcion monotona, asi que la mediana de los dB ES la mediana
 *    de las potencias. Por eso el ruido se mide con un histograma de 256
 *    casillas sobre el byte crudo -sin ordenar nada, sin memoria extra- y se
 *    convierte a lineal UNA vez. Lo mismo con el maximo de los ocho.
 *
 * 2. LA MEDIANA NO ES LA MEDIA. Para ruido gaussiano, la potencia por bin se
 *    reparte como una exponencial, y ahi mediana = media * ln2. Asi que la
 *    media del ruido es mediana / ln2, y esa constante NO esta ajustada a
 *    ojo: sale de la distribucion. Es FT8_SNR_MEDIANA_A_MEDIA.
 *
 * ---------------------------------------------------------------------
 * EL SUELO, Y POR QUE CAE SOLO DONDE TIENE QUE CAER
 * ---------------------------------------------------------------------
 *
 * Los bins de tono traen señal MAS ruido, asi que la señal sola es la resta.
 * Si sale cero o negativa no hay nada que medir y se devuelve el suelo.
 *
 * Y hay un sesgo conocido que conviene tener escrito: con ruido solo, el
 * maximo de ocho potencias exponenciales vale 1+1/2+...+1/8 = 2,72 veces la
 * media. O sea que sin señal la resta da ~1,72*Pn y el SNR sale sobre
 * 10*log10(1,72) - 26 = -23,6 dB. Cae practicamente en el suelo de -24 que
 * usa WSJT-X, y no por casualidad: es la misma estadistica. No se corrige
 * nada por eso; se deja escrito y el banco lo mide.
 *
 * ---------------------------------------------------------------------
 * LO QUE NO QUEDA RESUELTO
 * ---------------------------------------------------------------------
 *
 * - El ruido de HF no es gaussiano blanco, y el del banco si.
 * - El AGC se cancela en el cociente SOLO si es constante durante los 12,6 s
 *   de la ranura, y no lo es del todo.
 *
 * Por eso el objetivo medido es +-2 dB con pendiente 1 en un barrido, no
 * clavar el numero de WSJT-X. Ver sim/ft8_snr.c.
 *
 * NADA DE ESTO TOCA LA DECODIFICACION: el SNR es una columna de la pantalla
 * y el decodificador no lo lee para nada.
 */

#include "ft8/decode.h"

/*
 * ---------------------------------------------------------------------
 * LAS DOS CONSTANTES, Y COMO SE MIDIERON - 13/10/2026
 * ---------------------------------------------------------------------
 *
 * La primera version de esto ponia 10*log10(2500/6,25) = 26,02 dB, dando por
 * hecho que un bin "mide" sus 6,25 Hz de espaciado. El banco dijo que no: el
 * barrido salia 4,3 dB bajo de punta a punta. NO SE LE SUMO 4,3. Se fue a
 * medir de donde salia, y son dos cosas separadas y medibles por separado:
 *
 * 1. EL ANCHO DE RUIDO DE UN BIN NO ES SU ESPACIADO. La ventana son 1024
 *    muestras a 3.200 Hz -3,125 Hz por bin crudo- y el adaptador se queda uno
 *    de cada dos para que el ESPACIADO sea 6,25. Pero cada bin sigue siendo
 *    un bin de 3,125 Hz, ensanchado por la ventana de Hann.
 *
 *    Medido: se mete un tono continuo de amplitud conocida -que calibra la
 *    escala arbitraria del adaptador, con su DB_OFFSET de -112 incluido- y
 *    despues ruido blanco de varianza conocida. La potencia media por bin
 *    dividida por la densidad del ruido da el ancho equivalente:
 *
 *        4,664 Hz    ->  10*log10(2500 / 4,664) = 27,29 dB
 *
 * 2. EL SIMBOLO OCUPA MEDIA VENTANA. La ventana es de 1024 muestras y un
 *    simbolo son 512: cada FFT ve medio simbolo nuevo y medio del anterior,
 *    asi que el tono no esta en toda la ventana y su bin lee menos.
 *
 *    Medido comparando el mismo tono continuo contra una secuencia de tonos
 *    que cambia cada 512 muestras, como la de verdad:
 *
 *        tono continuo        3,5481
 *        tonos cambiando      0,8913
 *        perdida              6,00 dB
 *
 *    Seis dB exactos, que es lo que se espera cuando la suma coherente se
 *    queda en la mitad. Que salga el numero redondo es la señal de que se ha
 *    medido lo que se creia medir y no otra cosa.
 *
 * EL TERMINO NETO son 27,29 - 6,00 = 21,29 dB. No es un ajuste: son dos
 * medidas independientes, y las valida un barrido de SIETE niveles de SNR
 * con una prueba de pendiente, que es lo que un solo punto no puede hacer.
 * Ver sim/ft8_snr.c.
 */

/* 10*log10(2500 / 4,664): el ancho de ruido MEDIDO de un bin, no su
 * espaciado. */
#define FT8_SNR_REF_DB        27.29f

/* Lo que pierde el bin porque el simbolo ocupa media ventana. Medido. */
#define FT8_SNR_CAPTURA_DB     6.00f

/* Exponencial: mediana = media * ln2. Sale de la distribucion, no de
 * ajustar nada. */
#define FT8_SNR_MEDIANA_A_MEDIA  1.442695f   /* 1/ln2 */

/* H8 = 1 + 1/2 + ... + 1/8. El maximo de ocho potencias exponenciales vale
 * esto por la media, asi que es lo que hay que restarle a señal+ruido y no
 * Pn a secas. Sale de la distribucion. */
#define FT8_SNR_MAX8           2.717857f

/* El suelo, el mismo que usa WSJT-X. Por debajo no hay medida, hay ruido. */
#define FT8_SNR_SUELO_DB      (-24.0f)
#define FT8_SNR_TECHO_DB      (60.0f)

/* Cuantos bins a cada lado se miran para el ruido, y cuantos se saltan
 * pegados a la señal. La guarda existe porque la señal se derrama en los
 * bins de al lado -ventana de Hann- y meterlos en el ruido lo infla. */
#define FT8_SNR_GUARDA         3
#define FT8_SNR_VECINOS       48

/*
 * El SNR en dB referido a 2.500 Hz de este candidato, o FT8_SNR_SUELO_DB si
 * no hay señal medible por encima del ruido.
 *
 * No modifica nada: solo lee wf->mag.
 */
float ft8_snr_db(const ftx_waterfall_t *wf, const ftx_candidate_t *cand);

#endif /* FT8_SNR_H_INCLUDED */

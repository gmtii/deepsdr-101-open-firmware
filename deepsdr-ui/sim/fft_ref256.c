/*
 * LA MISMA TRANSFORMADA, COMPILADA NATIVA A 256 PUNTOS.
 *
 * No tiene codigo propio a proposito: es User/fft.c otra vez, con
 * FFT_SIZE_BANCO puesto a 256 y los nombres publicos cambiados para que los
 * dos juegos convivan en el mismo programa. Asi el patron contra el que se
 * compara NO es una reimplementacion -que podria equivocarse igual- sino el
 * mismo fichero con sus propias tablas de 256.
 *
 * Ver sim/fft_tam.c, que es quien compara.
 */
#define FFT_SIZE_BANCO 256U

#define fft_init              ref_fft_init
#define fft_compute_db        ref_fft_compute_db
#define fft_potencias         ref_fft_potencias
#define fft_compute_db_iq     ref_fft_compute_db_iq
#define fft_compute_db_iq_ex  ref_fft_compute_db_iq_ex
#define fft_set_bins_hook     ref_fft_set_bins_hook
#define fft_tam_iq            ref_fft_tam_iq
#define fft_tam_iq_actual     ref_fft_tam_iq_actual

#include "fft.c"

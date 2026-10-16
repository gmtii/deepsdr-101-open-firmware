#ifndef NCO_TABLA_H
#define NCO_TABLA_H

/*
 * La tabla de senos del NCO, en flash. La genera tools/gen_nco_tabla.py y la
 * comprueba tools/nco_tabla_check.py en cada `make comprueba`.
 *
 * Esta en un fichero aparte por una razon practica: son 1025 numeros y
 * mezclados con el codigo de nco.c lo dejarian ilegible.
 */
#define NCO_TABLA_N 1024U

extern const float k_nco_sen[NCO_TABLA_N + 1U];

#endif /* NCO_TABLA_H */

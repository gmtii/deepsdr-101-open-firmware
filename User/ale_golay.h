#ifndef ALE_GOLAY_H
#define ALE_GOLAY_H

/*
 * Golay extendido (24,12,8), el FEC de ALE. 27/09/2026.
 *
 * Una palabra de ALE son 24 bits que se parten en dos mitades de 12, y cada
 * mitad se codifica aqui. Corrige hasta TRES errores de bit por mitad, que
 * es mucho: en HF, con desvanecimientos, es la diferencia entre sacar el
 * indicativo y no sacar nada.
 *
 * EL POLINOMIO ES 0xAE3, no 0xC75. Los dos aparecen en la literatura y son
 * reciprocos -uno es el otro con los bits al reves-, asi que elegir el
 * equivocado da un decodificador que compila, corre, y no acierta jamas. El
 * bueno sale del texto del estandar:
 *
 *     FED-STD-1045A, 5.2.3.2:  g(x) = x11 + x9 + x7 + x6 + x5 + x + 1
 *
 * que es 1010 1110 0011 = 0xAE3.
 *
 * COMO SE CONSTRUYE la palabra de 24 bits, verificado contra la tabla del
 * decodificador de referencia (LinuxALE, golay.c) vector base a vector base:
 *
 *     r      = resto de (datos * x^11) mod g(x)          -> 11 bits
 *     pari   = (r << 1) | paridad_global(datos, r)       -> 12 bits
 *     codigo = (datos << 12) | pari                      -> 24 bits
 *
 * o sea un codigo ciclico [23,12] extendido con un bit de paridad global
 * que va el ULTIMO. La comprobacion que lo ata: la distribucion de pesos de
 * las 4.096 palabras sale {0:1, 8:759, 12:2576, 16:759, 24:1}, que es la
 * del Golay extendido y de nada mas.
 */

#include <stdint.h>

/* Los 12 bits de paridad de `datos` (12 bits). */
uint16_t ale_golay_paridad(uint16_t datos);

/* La palabra de 24 bits: (datos << 12) | paridad. */
uint32_t ale_golay_codifica(uint16_t datos);

/*
 * Corrige y devuelve los 12 bits de datos.
 *
 * `errores` (puede ser NULL) recibe cuantos bits se han corregido, o
 * ALE_GOLAY_NO si la palabra tiene mas de tres errores y no hay nada que
 * hacer. Cuando devuelve ALE_GOLAY_NO el valor de retorno no vale.
 */
#define ALE_GOLAY_NO 0xFFU
uint16_t ale_golay_decodifica(uint32_t palabra, uint8_t *errores);

#endif /* ALE_GOLAY_H */

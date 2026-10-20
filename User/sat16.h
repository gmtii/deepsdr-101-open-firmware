#ifndef SAT16_H
#define SAT16_H

#include <stdint.h>

/*
 * RECORTE A 16 BITS, EN UNA INSTRUCCION.
 *
 * POR QUE EXISTE (19/10/2026). Escrito como dos comparaciones, el compilador
 * NO lo reconoce como saturacion: emite cmp/it/mov dos veces mas un sxth, que
 * en el Cortex-M4 son ademas dos saltos condicionales. El nucleo tiene SSAT,
 * que hace exactamente eso en un ciclo y sin saltos.
 *
 * Medido en agc.c, que recorta las 256 muestras de cada bloque a 96 kHz: el
 * cuerpo del bucle pasa de 17 a 13 instrucciones por muestra.
 *
 * El resultado es IDENTICO: SSAT(x, 16) recorta a [-32768, 32767], que es lo
 * que hacian las dos comparaciones. No cambia ni un bit de audio.
 *
 * Se escribe a mano en vez de traer core_cm4.h porque ese arrastra medio
 * CMSIS hasta el simulador y alli se pelea con las intrinsecas del anfitrion
 * (ver deepsdr-ui/hostinc/arm_math.h, que cuenta esa historia entera).
 *
 * OJO con el paso por int32: quien llame tiene que saber que su valor cabe.
 * Viniendo de un float, una conversion fuera de rango es comportamiento
 * indefinido en C aunque en el ARM la instruccion sature; los sitios que lo
 * usan tienen el rango acotado por construccion y lo dicen.
 */
#if defined(__ARM_FEATURE_SAT) && (__ARM_FEATURE_SAT == 1)
static inline int16_t sat16(int32_t v)
{
    int32_t r;
    __asm volatile ("ssat %0, #16, %1" : "=r" (r) : "r" (v));
    return (int16_t)r;
}
#else
static inline int16_t sat16(int32_t v)
{
    if (v >  32767)  { v =  32767; }
    if (v < -32768)  { v = -32768; }
    return (int16_t)v;
}
#endif

#endif /* SAT16_H */

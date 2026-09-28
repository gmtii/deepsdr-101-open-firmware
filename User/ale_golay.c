#include "ale_golay.h"

/*
 * LA MATRIZ B: la paridad de cada bit de datos suelto.
 *
 * B[i] es la paridad que genera el dato (1 << i). Como el codigo es lineal,
 * la paridad de cualquier dato es el XOR de las B[i] de sus bits a uno - y
 * eso es todo lo que hace falta para codificar, sin divisiones de
 * polinomios en tiempo de ejecucion.
 *
 * Estos doce numeros son el codigo entero. Salen de dividir por g(x)=0xAE3 y
 * coinciden uno a uno con la tabla del decodificador de referencia. Fijarse
 * en el ultimo: B[11] = 0xAE3 es el propio generador.
 */
static const uint16_t k_b[12] = {
    0x5C7U, 0xB8DU, 0x2DEU, 0x5BCU, 0xB78U, 0x337U,
    0x66DU, 0xCD9U, 0xC76U, 0xD2BU, 0xF92U, 0xAE3U
};

static uint8_t pesa(uint32_t v)
{
    uint8_t n = 0U;

    while (v != 0U) { v &= (v - 1U); n++; }
    return n;
}

uint16_t ale_golay_paridad(uint16_t datos)
{
    uint16_t p = 0U;
    uint8_t  i;

    for (i = 0U; i < 12U; i++) {
        if ((datos >> i) & 1U) { p ^= k_b[i]; }
    }
    return (uint16_t)(p & 0xFFFU);
}

uint32_t ale_golay_codifica(uint16_t datos)
{
    datos = (uint16_t)(datos & 0xFFFU);
    return ((uint32_t)datos << 12) | (uint32_t)ale_golay_paridad(datos);
}

/*
 * EL DECODIFICADOR: buscar el patron de error de menos peso que cuadre.
 *
 * POR QUE ASI Y NO CON LA TABLA DE SINDROMES. La forma clasica es una tabla
 * de 4.096 entradas que va del sindrome al error; son 12 kB de flash, y en
 * esta radio 12 kB es dinero. La forma clasica "elegante" -el algoritmo de
 * los cuatro pasos con la matriz B- exige que B sea SIMETRICA, y en la
 * convencion de bits de este codigo no lo es, asi que habria que
 * reordenarla y ahi es donde se cuelan los fallos silenciosos.
 *
 * Esto es lo tonto y lo seguro: si el error tiene peso <= 3, esta en los
 * datos, en la paridad, o repartido. Se prueban todos los repartos posibles
 * por el lado de los datos -peso 0, 1, 2 y 3 sobre doce bits: 1+12+66+220 =
 * 299 casos- y para cada uno se mira cuanto error queda en la paridad. El
 * primero cuyo peso total no pase de tres ES la respuesta, y es UNICA: la
 * distancia minima del Golay extendido es 8, asi que dos patrones distintos
 * de peso <= 3 no pueden dar el mismo sindrome.
 *
 * Como se prueba en orden de peso creciente, el primero que sale es ademas
 * el de menos peso, que es lo que hay que devolver.
 *
 * COSTE. 299 vueltas de un XOR y un contador de bits, dos veces por palabra
 * candidata. El sincronismo de ALE prueba UNA fase por simbolo y hay 125
 * simbolos por segundo: 75.000 vueltas por segundo en un micro de 200 MHz.
 * No se nota. La tabla habria sido mas rapida y no hace falta que lo sea.
 */
uint16_t ale_golay_decodifica(uint32_t palabra, uint8_t *errores)
{
    uint16_t d = (uint16_t)((palabra >> 12) & 0xFFFU);
    uint16_t p = (uint16_t)(palabra & 0xFFFU);
    uint16_t s = (uint16_t)(p ^ ale_golay_paridad(d));
    uint8_t  i, j, k;

    /* Peso 0: el error, si lo hay, esta todo en la paridad. */
    if (pesa(s) <= 3U) {
        if (errores != (uint8_t *)0) { *errores = pesa(s); }
        return d;
    }

    /* Peso 1 en los datos. */
    for (i = 0U; i < 12U; i++) {
        uint16_t s1 = (uint16_t)(s ^ k_b[i]);

        if (pesa(s1) <= 2U) {
            if (errores != (uint8_t *)0) { *errores = (uint8_t)(1U + pesa(s1)); }
            return (uint16_t)(d ^ (1U << i));
        }
    }

    /* Peso 2. */
    for (i = 0U; i < 12U; i++) {
        uint16_t si = (uint16_t)(s ^ k_b[i]);

        for (j = (uint8_t)(i + 1U); j < 12U; j++) {
            uint16_t s2 = (uint16_t)(si ^ k_b[j]);

            if (pesa(s2) <= 1U) {
                if (errores != (uint8_t *)0) {
                    *errores = (uint8_t)(2U + pesa(s2));
                }
                return (uint16_t)(d ^ (1U << i) ^ (1U << j));
            }
        }
    }

    /* Peso 3: los tres errores en los datos y la paridad limpia. */
    for (i = 0U; i < 12U; i++) {
        uint16_t si = (uint16_t)(s ^ k_b[i]);

        for (j = (uint8_t)(i + 1U); j < 12U; j++) {
            uint16_t sj = (uint16_t)(si ^ k_b[j]);

            for (k = (uint8_t)(j + 1U); k < 12U; k++) {
                if ((uint16_t)(sj ^ k_b[k]) == 0U) {
                    if (errores != (uint8_t *)0) { *errores = 3U; }
                    return (uint16_t)(d ^ (1U << i) ^ (1U << j) ^ (1U << k));
                }
            }
        }
    }

    if (errores != (uint8_t *)0) { *errores = ALE_GOLAY_NO; }
    return d;
}

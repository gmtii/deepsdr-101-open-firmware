#include "rds_pi.h"

/*
 * La lista. Ordenada por codigo, que es como se lee y como el banco puede
 * comprobar que no hay repetidos.
 *
 * Ver rds_pi.h para de donde sale cada uno y por que esta tabla solo vale
 * para Espana.
 */
static const struct { uint16_t pi; const char *nombre; } k_pi[] = {
    { 0xE211U, "RNE 1" },
    { 0xE212U, "RNE Clásica" },
    { 0xE213U, "RNE 3" },
    { 0xE235U, "LOS40" },
    { 0xE237U, "Cadena Dial" },
    { 0xE239U, "Cadena SER" },
    /* Medido en la radio por el dueno del proyecto, 25/09/2026: la suya
     * decodifico E256 y el la identifico como Intereconomia. Es el primero
     * de esta tabla que no sale de una lista publicada sino de una escucha
     * propia, y por eso queda dicho de donde viene. */
    { 0xE256U, "Intereconomía" },
    { 0xE2CAU, "COPE" },
};

#define PI_N ((uint16_t)(sizeof k_pi / sizeof k_pi[0]))

const char *rds_pi_nombre(uint16_t pi)
{
    uint16_t i;
    /* Busqueda lineal: son siete. Una binaria aqui seria mas codigo que
     * datos, y esto corre una vez por cambio de emisora. */
    for (i = 0U; i < PI_N; i++) {
        if (k_pi[i].pi == pi) { return k_pi[i].nombre; }
    }
    return 0;
}

uint16_t rds_pi_cuantas(void) { return PI_N; }
uint16_t rds_pi_codigo(uint16_t i) { return (i < PI_N) ? k_pi[i].pi : 0U; }
const char *rds_pi_nombre_i(uint16_t i) { return (i < PI_N) ? k_pi[i].nombre : 0; }

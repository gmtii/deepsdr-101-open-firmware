#ifndef JTTY_MSG_H
#define JTTY_MSG_H

#include <stdint.h>
#include "jtty_fec.h"

/*
 * JTTY: DE LOS 34 BITS AL TEXTO - 28/09/2026.
 *
 * Cada trama de JTTY lleva 34 bits de mensaje, y eso es MUY poco: un
 * indicativo entero ya se come 28. Por eso el modo no manda texto sino
 * ATOMOS, y un mensaje largo son varias tramas seguidas -hasta dieciseis-
 * de las que solo la ultima lleva el bit de final.
 *
 * Los 34 bits se reparten asi:
 *
 *   bits 1-32   la gramatica
 *   bit 33      reservado, SIEMPRE cero (ver JTTY_BIT_RESERVADO)
 *   bit 34      fin de mensaje: 1 solo en el ultimo atomo
 *
 * Y la gramatica tiene cuatro formas, que se distinguen por los bits
 * 31-32:
 *
 *   0 y 1   un indicativo (28 bits) mas una accion: CQ, TU, AGN?...
 *   2       un intercambio de concurso estructurado (STRUCT30)
 *   3       cinco caracteres sueltos de seis bits (TEXT5)
 *
 * QUE UN CAMPO NO ASIGNADO INVALIDA LA TRAMA ENTERA. El estandar lo dice
 * y aqui se cumple: familias 101-111, tipos de intercambio reservados,
 * frases de control por encima de la 17, localizadores por encima de
 * AA00..RR99, relleno distinto de cero... todo eso NO se enseña. En un
 * modo que busca continuamente y sin ranuras, enseñar un atomo que no se
 * entiende del todo es exactamente como se llena la pantalla de basura
 * con pinta de buena.
 *
 * Fuente: lib/jtty/jtty_source_encoding.txt de WSJT-X v3.2.0-rc1, que es
 * el documento normativo. Los diez vectores de ejemplo que trae son los
 * que comprueba el banco.
 */

/* Lo que sale de un atomo. */
typedef struct {
    char    texto[48];   /* ya renderizado: "CQ K1ABC CQ", "599 123"... */
    uint8_t fin;         /* 1 si era el ultimo atomo del mensaje */
} jtty_atomo_t;

/*
 * Convierte 34 bits en texto. Devuelve 0 -y deja el texto vacio- si el
 * atomo no es valido segun las reglas del estandar.
 */
uint8_t jtty_msg_lee(const uint8_t carga[JTTY_CARGA_BITS], jtty_atomo_t *out);

#endif /* JTTY_MSG_H */

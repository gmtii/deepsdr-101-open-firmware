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
    /*
     * 1 SI ES UN TEXT5, Y POR QUE HAY QUE SABERLO - 28/09/2026.
     *
     * *** De una foto de la pantalla con la radio en antena: "DE G1 HGD..
     * ..... WE RE ALLY NEED EO ON". ***
     *
     * "WE RE" · "ALLY " · "NEED " · "EO ON" son trozos de CINCO CARACTERES
     * EXACTOS de una sola cadena. Los demas atomos son frases enteras -"CQ
     * K1ABC CQ", "599 123"- y entre frase y frase va un espacio.
     *
     * Quien los junta tiene que tratarlos distinto: los TEXT5 se pegan SIN
     * NADA EN MEDIO -si no, "WE REALLY NEED" sale "WE RE ALLY NEED"- y los
     * demas con un espacio. Sin este campo, quien junta no puede saberlo.
     */
    uint8_t texto5;
} jtty_atomo_t;

/*
 * PEGA UN ATOMO AL FINAL DE UN MENSAJE, con la regla que toque.
 *
 * Vive aqui y no en jtty_modo.c -que es quien monta los mensajes- porque
 * jtty_modo.c no lo puede compilar el simulador: arrastra la radio entera.
 * Esta regla se equivoco una vez en antena; que se pueda medir sin placa
 * es lo que impide que se vuelva a equivocar en silencio.
 *
 * `dst` es el mensaje que lleva montado, `max` lo que cabe contando el
 * cero, y `ant5` si el atomo ANTERIOR era un TEXT5.
 */
void jtty_msg_pega(char *dst, uint32_t max, const jtty_atomo_t *a, uint8_t ant5);

/*
 * EL INDICATIVO DE UN MENSAJE YA MONTADO - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "estaria bien poner una columna en jtty
 * con el pais". ***
 *
 * En FT8 esto no hace falta: el decodificador sabe que campo es un
 * indicativo porque el protocolo se lo dice, y de ahi sale la columna de
 * pais que ya tiene. En JTTY no hay tal cosa - un mensaje es texto libre
 * pegado de varios atomos, "LZ1QZ DE F4JUO UR RST 599 009 OP JACQUE"- asi
 * que el indicativo hay que RECONOCERLO por su forma.
 *
 * SE COGE EL ULTIMO, que es la misma regla que usa FT8 en esta radio: el
 * primero es a quien va dirigido y el ultimo es quien transmite. En
 * "W5CHA DE IK3CHK" el que esta a la tecla es IK3CHK.
 *
 * LA FORMA, y es estricta a proposito:
 *
 *     prefijo   1 o 2 caracteres alfanumericos con AL MENOS UNA LETRA
 *     luego     UN digito
 *     luego     de 1 a 4 letras, y ahi se acaba
 *
 * Eso acepta F4JUO, IK3CHK, 7M5OAP, 4X1ABC, W5CHA... y rechaza lo que de
 * verdad aparece alrededor en un mensaje de teclado: "599", "001", "RST",
 * "3W,", "JO10" -un localizador acaba en digito-, "JO20HI" -y uno de seis
 * tiene digito en medio- y "3CHK", que es un trozo de IK3CHK partido por
 * una trama perdida y que sin exigir letra en el prefijo colaria.
 *
 * Si el testigo lleva barras se prueba CADA TROZO por separado pero se
 * devuelve ENTERO, para que dxcc_pais() vea "DL/EA4XYZ" y conteste
 * Alemania, que es lo que el operador esta diciendo. Ver dxcc.h.
 *
 * Devuelve 1 y deja el indicativo en `out` (que tiene que admitir al menos
 * 16 bytes), o 0 si no hay ninguno. Un hueco es una respuesta honrada; un
 * pais sacado de "599" no.
 */
uint8_t jtty_msg_indicativo(const char *texto, char *out);

/* Y al cerrar: los espacios de la derecha del mensaje entero sobran. */
void jtty_msg_cierra(char *dst);

/*
 * Convierte 34 bits en texto. Devuelve 0 -y deja el texto vacio- si el
 * atomo no es valido segun las reglas del estandar.
 */
uint8_t jtty_msg_lee(const uint8_t carga[JTTY_CARGA_BITS], jtty_atomo_t *out);

#endif /* JTTY_MSG_H */

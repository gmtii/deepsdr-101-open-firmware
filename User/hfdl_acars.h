#ifndef HFDL_ACARS_H
#define HFDL_ACARS_H

#include <stdint.h>

/*
 * ACARS DENTRO DE HFDL - 14/10/2026.
 *
 * *** Un probador: "it has been receiving for quite a while on a frequency
 * with extremely high traffic activity, yet hasn't decoded a single set of
 * coordinates... With other firmware, on the other hand, logs coordinates
 * for practically every transmission". ***
 *
 * Y la respuesta estaba en los bytes, no en el razonamiento. El volcado en
 * crudo de la pantalla -ver hfdl_modo_linea_bytes()- enseño esto en una
 * trama de verdad, en 17.967 kHz:
 *
 *   FF FF 01 32 AE C1 B6 AD 45 4F 46 15 51 B0 B9 02 D3 32 B5 C1
 *         ^  ^  \________________/ ^  \___/ ^  ^
 *        SOH '2'    matricula     NAK  "Q0" '9' STX
 *
 * y al lado, la unica que SI daba posicion:
 *
 *   FF D1 51 54 52 38 37 51 ...
 *      ^  \_______________/
 *    0xD1     "QTR87Q"
 *
 * O sea: el sobre 0xFF esta, el tipo va DETRAS DE EL -en el byte 1, no en
 * el 10 como decia un comentario mio de hace un mes-, y las tramas que no
 * daban posicion son de tipo 0xFF, que es ENVELOPED_DATA: ACARS. Este
 * firmware solo entendia el 0xD1, asi que las tiraba todas.
 *
 * LA PARIDAD ES LO QUE LO HACIA PARECER BASURA. ACARS va en ASCII de siete
 * bits con el octavo de paridad impar, asi que "AE C1 B6 AD 45 4F 46" no
 * son bytes raros: son ".A6-EOF" con el bit alto puesto. Quitandolo sale
 * texto.
 *
 * EL FORMATO, verificado contra esa trama y no copiado de memoria:
 *
 *   +0   SOH (0x01)
 *   +1   modo, un caracter
 *   +2   matricula del avion, SIETE caracteres (se rellena con puntos)
 *   +9   acuse tecnico: ACK (0x06) o NAK (0x15)
 *   +10  etiqueta, DOS caracteres ("Q0", "H1", "_d"...)
 *   +12  identificador de bloque, un caracter
 *   +13  STX (0x02) y detras el texto hasta ETX (0x03) o ETB (0x17)
 *
 * Si no hay STX el mensaje no lleva texto, que es legal y pasa a menudo.
 */

#define HFDL_ACARS_TEXTO_MAX 64U

typedef struct {
    char    matricula[8];   /* 7 + NUL, sin los puntos de relleno */
    char    etiqueta[3];    /* 2 + NUL */
    char    modo;
    char    bloque;
    uint8_t ack;            /* 1 si ACK, 0 si NAK */
    char    texto[HFDL_ACARS_TEXTO_MAX + 1U];
    uint8_t texto_n;
} hfdl_acars_t;

/*
 * `buf`/`len` empiezan en el SOH, o sea DETRAS del octeto de tipo 0xFF.
 * Devuelve 1 si la trama tiene forma de ACARS. No inventa: si el SOH no
 * esta, o la matricula o la etiqueta no son imprimibles, dice que no.
 */
uint8_t hfdl_acars_parse(const uint8_t *buf, uint32_t len, hfdl_acars_t *out);

/*
 * La posicion que lleve el texto, en centesimas de grado. Devuelve 1 solo
 * si la encuentra Y pasa las comprobaciones de cordura. Ver el comentario
 * de la funcion: aqui se rechaza mucho a proposito.
 */
uint8_t hfdl_acars_posicion(const hfdl_acars_t *a, int32_t *lat_e4, int32_t *lon_e4);

#endif /* HFDL_ACARS_H */

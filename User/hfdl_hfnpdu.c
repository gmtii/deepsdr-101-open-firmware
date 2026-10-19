#include "hfdl_hfnpdu.h"

#define PERFORMANCE_DATA_HFNPDU_LEN 47u /* from HFNPDU envelope byte through the last freq-change-code byte - see dumphfdl's own PERFORMANCE_DATA_HFNPDU_LEN */
#define HFNPDU_ENVELOPE_MARKER 0xFFu
#define HFNPDU_TYPE_PERFORMANCE_DATA 0xD1u
/*
 * Y EL 0xD5, QUE LLEVA LOS MISMOS CAMPOS - 14/10/2026.
 *
 * La cabecera de este fichero decia "FREQUENCY_DATA (0xD5) ... no se
 * tratan". Se trata ahora, porque lleva identificativo de vuelo y posicion
 * exactamente en los mismos sitios que el 0xD1 -comprobado contra cinco
 * tramas reales de la grabacion de referencia- y porque es el que manda el
 * avion al darse de alta, que es la trama mas frecuente que hay en bajada.
 *
 * No se afloja nada al admitirlo: pasa las mismas cuatro pruebas que el
 * 0xD1 -que quepa el HFNPDU entero, latitud en +-90, longitud en +-180 y
 * una hora que sea una hora- sobre bytes que no elige nadie.
 */
#define HFNPDU_TYPE_FREQUENCY_DATA   0xD5u

/*
 * Cuantos bytes se miran buscando el octeto de tipo. Ver el comentario
 * largo de abajo: la cabecera de este fichero dice que en la unica trama
 * real verificada el par 0xFF 0xD1 estaba en los bytes 10-11, asi que 24
 * deja margen de sobra sin abrir la puerta a buscar por toda la trama.
 */
#define HFNPDU_BUSQUEDA_MAX 24u

static bool perf_campos(const uint8_t *buf, hfdl_hfnpdu_perf_data_t *out, uint8_t tipo);

static uint16_t extract_uint16(const uint8_t *buf)
{
    /* little-endian 16-bit, same as dumphfdl's extract_uint16_t() macro */
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/*
 * Ported verbatim (in effect) from dumphfdl's parse_coordinate() in
 * util.c, which uses a GCC bitfield trick (`struct { int32_t coord:20; }`)
 * to sign-extend a 20-bit value - written out explicitly here instead,
 * to not depend on that extension being available/working the same
 * way on this project's ARM toolchain. `raw` must already have only
 * its low 20 bits meaningful (the two call sites below already mask
 * it that way while assembling it, same as dumphfdl's own callers do).
 */
static double parse_coordinate(uint32_t raw)
{
    int32_t signed_val;
    if (raw & 0x80000u) { /* bit 19 set - negative in 20-bit two's complement */
        signed_val = (int32_t)raw - 0x100000; /* same as (int32_t)(raw | 0xFFF00000u) but without relying on the shift/cast being well-defined for the top bits */
    } else {
        signed_val = (int32_t)raw;
    }
    return (double)signed_val * 180.0 / (double)0x7FFFF; /* 0x7FFFF = 2^19-1, the max positive 20-bit value - same divisor dumphfdl uses */
}

static struct hfdl_hfnpdu_time parse_utc_time(uint32_t t)
{
    struct hfdl_hfnpdu_time result;
    result.hour = (uint8_t)(t / 3600u);
    result.min = (uint8_t)((t % 3600u) / 60u);
    result.sec = (uint8_t)(t % 60u);
    return result;
}

bool hfdl_hfnpdu_parse_performance_data(const uint8_t *buf, uint32_t len, hfdl_hfnpdu_perf_data_t *out)
{
    if (buf == (void *)0 || out == (void *)0) {
        return false;
    }
    /*
     * ======================================================================
     * LAS DOS FORMAS DE ENMARCARLO - 10/10/2026
     * ======================================================================
     *
     * *** Nos han dicho: "Flight position is not decoded in HFDL mode". ***
     *
     * Y esto no tenia NI UN BANCO. Ningun fichero de sim/ compilaba
     * hfdl_hfnpdu.c: el unico decodificador del arbol que convierte bytes
     * del aire en una latitud y una longitud no se habia probado nunca.
     * Ahora lo prueba sim/hfdl_posicion.c, y lo primero que saco fue esto.
     *
     * LA CONTRADICCION, que estaba escrita en este mismo modulo y en dos
     * sitios que no pueden ser los dos ciertos:
     *
     *   - el codigo exigia el par 0xFF 0xD1 en buf[0] y buf[1];
     *   - y la cabecera de hfdl_hfnpdu.h dice, de la unica trama real
     *     contra la que se verifico esto: "bytes 10-11 of the LPDU user
     *     data were 0xFF 0xD1".
     *
     * Diez no es cero. O una de las dos frases describe otra cosa -el LPDU
     * entero en vez de sus datos, por ejemplo- o la comprobacion nunca
     * cuadro con la trama que dice haber verificado. Sin los bytes de
     * aquella trama no se puede saber cual, y adivinar es justo lo que no
     * se hace aqui.
     *
     * LO QUE SI SE PUEDE HACER SIN ADIVINAR: aceptar las dos formas que
     * este proyecto tiene documentadas, que son
     *
     *   0xFF 0xD1 ...   el mensaje dentro de un sobre (lo que exigia antes)
     *   0xD1 ...        el tipo a pelo, que es como lo entrega dumphfdl
     *                   cuando el HFNPDU no viene envuelto
     *
     * y en la segunda correr todos los campos un byte, que es lo unico que
     * cambia entre las dos. Esto solo puede AÑADIR decodificaciones: lo que
     * antes pasaba, sigue pasando igual.
     *
     * Y COMO NO ESTOY SEGURO DE CUAL MANDA, LO QUE SALE SE MIRA. Una
     * latitud fuera de +-90 o una longitud fuera de +-180 no son una
     * posicion: son la prueba de que el marco elegido no era el bueno. Se
     * rechaza en vez de pintar un avion en mitad de ninguna parte, que es
     * peor que no pintar ninguno. Ver el final de esta funcion.
     */
    /*
     * ======================================================================
     * SE BUSCA EL TIPO, NO SE EXIGE EN UN SITIO - 10/10/2026 (la tarde)
     * ======================================================================
     *
     * Pedido por el usuario.
     *
     * Por la manana se habia aceptado el tipo en dos sitios -con sobre y
     * sin el- y seguia sin salir nada. Con tres USER DATA y cero
     * posiciones, "no ha llegado ninguna" deja de ser la explicacion comoda
     * y pasa a ser poco creible.
     *
     * Y LA PISTA LLEVA AHI DESDE EL 16/09, en la cabecera de este fichero:
     *
     *     "bytes 10-11 of the LPDU user data were 0xFF 0xD1"
     *
     * Diez no es cero. Si eso es verdad -y es la UNICA trama real contra la
     * que esto se ha verificado nunca- entonces delante del HFNPDU hay diez
     * bytes de algo, y exigir el tipo en el byte 0 o en el 1 falla en TODAS
     * las tramas. Que es exactamente lo que se esta viendo.
     *
     * NO SE PUEDE SABER QUE SON ESOS DIEZ BYTES sin la trama, y adivinar es
     * lo que no se hace aqui. Pero no hace falta saberlo: se BUSCA el tipo
     * en los primeros bytes en vez de exigirlo en uno.
     *
     * LO QUE HACE QUE BUSCAR NO SEA ADIVINAR es lo que viene detras. Un
     * 0xD1 suelto en medio de un ACARS no da una posicion valida por
     * casualidad: detras tienen que caber 46 bytes mas, la latitud tiene
     * que estar en +-90, la longitud en +-180 y la hora tiene que ser una
     * hora. Son cuatro condiciones a la vez sobre bytes que no se eligen.
     * Se prueba cada candidato ENTERO y se acepta el primero que las pasa.
     *
     * Y esto SOLO PUEDE ANADIR decodificaciones: el offset 0 y el 1 siguen
     * siendo los dos primeros que se prueban, asi que lo que antes salia,
     * sigue saliendo igual.
     *
     * El sobre 0xFF no necesita rama propia: si el par es 0xFF 0xD1, la
     * busqueda encuentra el 0xD1 y los campos empiezan detras igual.
     */
    {
        uint32_t off;
        uint32_t tope = (len < HFNPDU_BUSQUEDA_MAX) ? len : HFNPDU_BUSQUEDA_MAX;

        for (off = 0U; off < tope; off++) {
            if (buf[off] != HFNPDU_TYPE_PERFORMANCE_DATA &&
                buf[off] != HFNPDU_TYPE_FREQUENCY_DATA) { continue; }
            /*
             * Y SI NO ES EL PRIMERO, EL SOBRE DELANTE.
             *
             * No es un apaño para bajar los falsos: es lo que la cabecera
             * de este fichero documenta de la trama real, "bytes 10-11
             * were 0xFF 0xD1". El tipo a pelo solo se admite en el byte 0,
             * que es como lo entrega dumphfdl cuando no viene envuelto.
             * Sin esto, un 0xD1 cualquiera en medio de un ACARS es un
             * candidato, y eso se midio: 301 de cada 1000.
             */
            if (off != 0U && buf[off - 1U] != HFNPDU_ENVELOPE_MARKER) {
                continue;
            }
            /* Desde el octeto de tipo hasta el final tiene que caber un
             * HFNPDU de datos de rendimiento entero. */
            if ((len - off) < (PERFORMANCE_DATA_HFNPDU_LEN - 1U)) { continue; }
            if (perf_campos(buf + off + 1U, out, buf[off])) { return true; }
        }
        return false;
    }
}

/*
 * Los campos, ya con `buf` apuntando al PRIMER BYTE DETRAS DEL TIPO. Esto
 * estaba dentro de la funcion de arriba; se saca para poder probar varios
 * candidatos sin repetir el codigo, que es donde se cuela que uno se quede
 * con los indices viejos.
 */
static bool perf_campos(const uint8_t *buf, hfdl_hfnpdu_perf_data_t *out, uint8_t tipo)
{
    uint32_t coord;


    out->flight_id[0] = (char)buf[0];
    out->flight_id[1] = (char)buf[1];
    out->flight_id[2] = (char)buf[2];
    out->flight_id[3] = (char)buf[3];
    out->flight_id[4] = (char)buf[4];
    out->flight_id[5] = (char)buf[5];
    out->flight_id[6] = '\0';

    /* Coordinate packing is NOT byte-aligned - each 20-bit value spans
     * parts of 3 bytes, with lon's low nibble sharing byte[8] with
     * lat's top nibble (decia byte[10] hasta el 10/10/2026, y el codigo
     * de las dos lineas de abajo dice que es el 8: lat usa 6/7/8 y lon
     * usa 8/9/10. Quien verifique los desplazamientos contra dumphfdl
     * partiendo del comentario viejo se equivoca de byte). Copied exactly from dumphfdl's own bit
     * shifts/masks - see this file's header comment for why this is
     * trusted rather than re-derived. */
    coord = (uint32_t)buf[6] | ((uint32_t)buf[7] << 8) | (((uint32_t)buf[8] & 0xFu) << 16);
    out->location.lat = parse_coordinate(coord);
    coord = (((uint32_t)buf[8] & 0xF0u) >> 4) | ((uint32_t)buf[9] << 4) | ((uint32_t)buf[10] << 12);
    out->location.lon = parse_coordinate(coord);

    out->utc_time = parse_utc_time(2u * (uint32_t)extract_uint16(buf + 11));
    out->version = buf[13];
    out->flight_leg = buf[14];
    out->gs_id = (uint8_t)(buf[15] & 0x7Fu);

    /*
     * Y LA COMPROBACION DE CORDURA. Las dos coordenadas salen de veinte
     * bits escalados por 180/(2^19-1), asi que por construccion caen en
     * +-180: una latitud de 150 grados no existe, y si sale es que los
     * campos se han leido corridos.
     *
     * Rechazar es mejor que pintar: un avion en una posicion inventada se
     * cree -es un punto en un mapa, no dice de donde salio- y manda al
     * usuario a buscar un fallo de propagacion que no existe. Ver la cabecera
     * de esta funcion para por que hay dos marcos posibles.
     */
    if (out->location.lat < -90.0 || out->location.lat > 90.0) { return false; }
    if (out->location.lon < -180.0 || out->location.lon > 180.0) { return false; }

    /*
     * Y LA HORA, que desde que se BUSCA el tipo hace mas falta que antes.
     *
     * Con el tipo exigido en un sitio fijo, las dos coordenadas bastaban
     * para cazar un campo corrido. Buscandolo, cada 0xD1 que haya en medio
     * de un ACARS es un candidato, asi que hay que apretar: la hora sale de
     * dos bytes por dos segundos, o sea 0..131070, y casi todo eso NO es
     * una hora del dia. Pasar las tres a la vez por casualidad es mucho
     * pedirle a unos bytes que no se eligen.
     */
    if (out->utc_time.hour > 23U || out->utc_time.min > 59U
        || out->utc_time.sec > 59U) {
        return false;
    }

    /*
     * Y LA ESTACION DE TIERRA TIENE QUE EXISTIR. Son diecisiete en todo el
     * mundo y van numeradas del 1 al 17 (el 12 no existe). Un numero fuera
     * de ahi no es una estacion: es un byte que se ha leido donde no era.
     *
     * SOLO PARA EL 0xD1 - 14/10/2026. En el 0xD5 ese byte no es la
     * estacion: en las cinco tramas reales medidas vale 0x00 con todo lo
     * demas correcto -vuelo legible, coordenadas en ruta y hora buena-, o
     * sea que exigirselo tiraba las cinco. Lo que se pone en su lugar no es
     * nada: es la prueba de abajo, que aprieta donde el 0xD5 si tiene
     * estructura.
     */
    if (tipo == HFNPDU_TYPE_PERFORMANCE_DATA) {
        if (out->gs_id < 1U || out->gs_id > 17U) { return false; }
    } else {
        /*
         * EL IDENTIFICATIVO DE VUELO, que la norma deja en ASCII con el
         * bit 8 a cero (Figura 9). Seis bytes seguidos legibles no salen
         * por casualidad: o son seis caracteres de vuelo, o son seis ceros
         * -que es lo que manda un avion que todavia no lo ha declarado, y
         * esa tambien es buena-. Cualquier otra cosa es un campo corrido.
         */
        uint8_t c, imp = 0U, cero = 0U;
        for (c = 0U; c < 6U; c++) {
            uint8_t v = (uint8_t)out->flight_id[c];
            if (v >= 0x20U && v <= 0x5FU) { imp++; }
            else if (v == 0U) { cero++; }
        }
        if (imp != 6U && cero != 6U) { return false; }
    }

    return true;
}

uint8_t hfdl_hfnpdu_tipo(const uint8_t *buf, uint32_t len, uint32_t *contenido)
{
    if (buf == (const uint8_t *)0 || len == 0U) { return 0U; }
    /*
     * CON SOBRE O SIN EL, y nada mas. Ver el comentario de la cabecera:
     * las dos tramas reales que hay enseñan el tipo en el byte 1, detras
     * del sobre 0xFF. El byte 0 se admite porque es como lo entrega
     * dumphfdl cuando el HFNPDU no viene envuelto.
     *
     * Ya NO se busca por los primeros 24 bytes: aquella busqueda nacio de
     * creer que el tipo estaba en el 10, y buscar donde no hay que buscar
     * es como un 0xD1 suelto en medio de un ACARS acaba pareciendo una
     * posicion.
     */
    if (buf[0] == HFNPDU_ENVELOPE_MARKER && len > 1U) {
        if (contenido) { *contenido = 2U; }
        return buf[1];
    }
    if (contenido) { *contenido = 1U; }
    return buf[0];
}

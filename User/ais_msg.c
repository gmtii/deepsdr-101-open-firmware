#include "ais_msg.h"
#include <string.h>

/*
 * UN CAMPO DEL MENSAJE: `n` bits a partir del bit `p`, de mas a menos peso.
 *
 * COMPRUEBA LOS LIMITES, y no por escrupulo: un mensaje cortado es lo
 * NORMAL, no la excepcion. Una rafaga se pierde a mitad y lo que queda es
 * un trozo; leer mas alla de `nbits` seria leer lo que hubiera detras en el
 * buffer, o sea inventarse una posicion. Los bits que faltan valen cero y
 * el campo sale bajo, que es una respuesta mala pero acotada.
 */
static uint32_t campo(const uint8_t *b, uint16_t nbits, uint16_t p, uint8_t n)
{
    uint32_t v = 0U;
    uint8_t  i;

    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)(p + i);
        uint32_t bit = 0U;

        if (k < nbits) {
            bit = (uint32_t)((b[k >> 3] >> (7U - (k & 7U))) & 1U);
        }
        v = (v << 1) | bit;
    }
    return v;
}

/* Lo mismo con signo, en complemento a dos sobre `n` bits. */
static int32_t campo_s(const uint8_t *b, uint16_t nbits, uint16_t p, uint8_t n)
{
    uint32_t v = campo(b, nbits, p, n);

    if ((n < 32U) && ((v & (1UL << (n - 1U))) != 0U)) {
        v |= ~((1UL << n) - 1UL);
    }
    return (int32_t)v;
}

/*
 * EL ALFABETO DE SEIS BITS.
 *
 * AIS mete el texto a seis bits por caracter, o sea 64 simbolos: de 0 a 31
 * son '@' a '_' -ahi caen las mayusculas y las cifras no- y de 32 a 63 son
 * ' ' a '?', que es donde estan el espacio, las cifras y los signos.
 *
 * Es la misma tabla que el "armored payload" del NMEA, pero aqui NO hay que
 * desarmar nada: los bits llegan crudos de la radio. Desarmarlos otra vez
 * seria el fallo clasico de quien porta codigo escrito para leer ficheros.
 */
static char seis(uint32_t v)
{
    v &= 0x3FU;
    return (char)((v < 32U) ? ('@' + (char)v) : (char)v);
}

/*
 * Saca `ncar` caracteres de seis bits y los deja limpios.
 *
 * Los nombres vienen rellenos por la derecha con '@', no con espacios: sin
 * quitarlos la tabla ensena "MAERSK ESSEX@@@@@@@@". Y de paso se quitan los
 * espacios del final, que tambien los hay.
 */
static void texto(const uint8_t *b, uint16_t nbits, uint16_t p, uint8_t ncar,
                  char *out, uint8_t max)
{
    uint8_t i, n = 0U;

    if (ncar > (uint8_t)(max - 1U)) { ncar = (uint8_t)(max - 1U); }

    for (i = 0U; i < ncar; i++) {
        char c = seis(campo(b, nbits, (uint16_t)(p + (uint16_t)i * 6U), 6U));

        if (c == '@') { break; }      /* relleno: de aqui no hay mas */
        out[n++] = c;
    }
    while ((n > 0U) && (out[n - 1U] == ' ')) { n--; }
    out[n] = '\0';
}

/*
 * LA POSICION, DE MINUTOS DIEZMILESIMOS A GRADOS DIEZMILESIMOS.
 *
 * AIS guarda la longitud y la latitud en 1/10.000 de MINUTO, o sea en
 * 1/600.000 de grado. Para ensenarlas en grados con cuatro decimales hay
 * que dividir por 60 - no por 600.000, que es el error que deja todo en el
 * golfo de Guinea.
 *
 * La division entera de C trunca hacia cero, que para esto vale: media
 * diezmilesima de grado son cinco metros.
 *
 * NO DISPONIBLE: la longitud 181 grados y la latitud 91. Salen mucho mas a
 * menudo de lo que parece -cualquier barco con el GPS sin fijar- y sin
 * comprobarlo aparecen barcos navegando por el polo.
 */
#define AIS_LON_NADA  (181L * 600000L)
#define AIS_LAT_NADA  ( 91L * 600000L)

static uint8_t posicion(const uint8_t *b, uint16_t nbits,
                        uint16_t p_lon, uint16_t p_lat, ais_msg_t *o)
{
    int32_t lon = campo_s(b, nbits, p_lon, 28U);
    int32_t lat = campo_s(b, nbits, p_lat, 27U);

    if ((lon == AIS_LON_NADA) || (lat == AIS_LAT_NADA)) { return 0U; }
    if ((lon > 180L * 600000L) || (lon < -180L * 600000L)) { return 0U; }
    if ((lat >  90L * 600000L) || (lat <  -90L * 600000L)) { return 0U; }

    o->lon_e4 = lon / 60L;
    o->lat_e4 = lat / 60L;
    o->hay_pos = 1U;
    return 1U;
}

/* Velocidad y rumbo sobre el fondo. 1023 y 3600 son "no disponible". */
static void velocidad(const uint8_t *b, uint16_t nbits,
                      uint16_t p_sog, uint16_t p_cog, ais_msg_t *o)
{
    uint32_t sog = campo(b, nbits, p_sog, 10U);
    uint32_t cog = campo(b, nbits, p_cog, 12U);

    if ((sog == 1023U) || (cog >= 3600U)) { return; }
    o->sog_d = (uint16_t)sog;
    o->cog_d = (uint16_t)cog;
    o->hay_vel = 1U;
}

uint16_t ais_msg_bits_min(uint8_t tipo)
{
    switch (tipo) {
    case 1: case 2: case 3: case 4: case 18: return 168U;
    case 5:  return 424U;
    case 19: return 312U;
    case 24: return 160U;   /* la parte A son 160; la B, 168 */
    default: return 0U;
    }
}

uint8_t ais_msg_saca(const uint8_t *bits, uint16_t nbits, ais_msg_t *out)
{
    uint8_t tipo;

    if ((bits == (const uint8_t *)0) || (out == (ais_msg_t *)0)) { return 0U; }
    memset(out, 0, sizeof *out);

    if (nbits < 38U) { return 0U; }   /* ni el tipo y el MMSI caben */

    tipo = (uint8_t)campo(bits, nbits, 0U, 6U);
    out->tipo = tipo;
    out->mmsi = campo(bits, nbits, 8U, 30U);

    /*
     * EL MMSI DE NUEVE CIFRAS ES EL UNICO FILTRO BARATO QUE HAY.
     *
     * AIS no lleva mas comprobacion que el CRC de 16 bits del HDLC, y con
     * 16 bits una de cada 65.536 tramas de ruido lo pasa por casualidad. En
     * una tarde de escucha eso son varias.
     *
     * Un MMSI de verdad esta entre 100.000.000 y 999.999.999 (los primeros
     * tres digitos son el pais). Fuera de ahi no es un barco: es ruido que
     * ha tenido suerte. Tira nueve de cada diez falsos positivos por una
     * comparacion.
     */
    if ((out->mmsi < 100000000UL) || (out->mmsi > 999999999UL)) { return 0U; }

    {
        uint16_t pide = ais_msg_bits_min(tipo);

        if (pide == 0U) { return 0U; }      /* tipo que no desempaquetamos */
        if (nbits < pide) { return 0U; }    /* llego cortado */
    }

    switch (tipo) {
    case 1: case 2: case 3:
        (void)posicion(bits, nbits, 61U, 89U, out);
        velocidad(bits, nbits, 50U, 116U, out);
        break;

    case 4:
        /*
         * ESTACION BASE COSTERA: trae la HORA UTC.
         *
         * Es lo unico de todo AIS que sirve para algo que no sea mirar
         * barcos: una estacion costera manda su reloj cada diez segundos,
         * y esta radio no tiene internet para ponerse en hora. Que el
         * firmware lo aproveche o no es cosa de ais_modo.c; aqui se saca
         * porque esta ahi.
         */
        (void)posicion(bits, nbits, 79U, 107U, out);
        out->ano     = (uint16_t)campo(bits, nbits, 38U, 14U);
        out->mes     = (uint8_t) campo(bits, nbits, 52U,  4U);
        out->dia     = (uint8_t) campo(bits, nbits, 56U,  5U);
        out->hora    = (uint8_t) campo(bits, nbits, 61U,  5U);
        out->minuto  = (uint8_t) campo(bits, nbits, 66U,  6U);
        out->segundo = (uint8_t) campo(bits, nbits, 72U,  6U);
        out->hay_hora = (uint8_t)((out->ano >= 2000U) && (out->ano <= 2100U) &&
                                  (out->mes >= 1U) && (out->mes <= 12U) &&
                                  (out->dia >= 1U) && (out->dia <= 31U) &&
                                  (out->hora <= 23U) && (out->minuto <= 59U) &&
                                  (out->segundo <= 59U));
        break;

    case 5:
        /* Datos estaticos del viaje: el nombre esta en el bit 112. */
        texto(bits, nbits, 112U, 20U, out->nombre, (uint8_t)sizeof out->nombre);
        out->hay_nombre = (uint8_t)(out->nombre[0] != '\0');
        break;

    case 18:
        (void)posicion(bits, nbits, 57U, 85U, out);
        velocidad(bits, nbits, 46U, 112U, out);
        break;

    case 19:
        /* Clase B extendido: posicion Y nombre en el mismo mensaje. */
        (void)posicion(bits, nbits, 57U, 85U, out);
        velocidad(bits, nbits, 46U, 112U, out);
        texto(bits, nbits, 143U, 20U, out->nombre, (uint8_t)sizeof out->nombre);
        out->hay_nombre = (uint8_t)(out->nombre[0] != '\0');
        break;

    case 24:
        /*
         * Clase B estatico, y viene en DOS PARTES. La A lleva el nombre y
         * la B las dimensiones y el indicativo. Solo interesa la A: la B
         * no tiene nada que ensenar en esta tabla.
         */
        if (campo(bits, nbits, 38U, 2U) == 0U) {
            texto(bits, nbits, 40U, 20U, out->nombre,
                  (uint8_t)sizeof out->nombre);
            out->hay_nombre = (uint8_t)(out->nombre[0] != '\0');
        }
        break;

    default:
        return 0U;
    }

    return 1U;
}

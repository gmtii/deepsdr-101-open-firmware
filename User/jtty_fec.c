#include "jtty_fec.h"
#include <string.h>

/*
 * EL CODIGO, EN CUATRO NUMEROS (lib/jtty/jtty_tbcc_code_profile.f90:34-45
 * de WSJT-X v3.2.0-rc1):
 *
 *   memoria nu = 9, o sea K = 10 y 512 estados
 *   g0 = 1167 octal = 0x277
 *   g1 = 1545 octal = 0x365
 *   polinomio del CRC = 0x80F, con el termino principal implicito
 *
 * Y el registro es de DIEZ bits (nu+1): el bit que entra va al de menos
 * peso y los generadores se aplican como mascara sobre esos diez.
 */
#define NU        9U
#define ESTADOS   512U           /* 2^nu */
#define MASCARA_E 511U           /* estados - 1 */
#define MASCARA_R 1023U          /* 2^(nu+1) - 1 */
#define G0        0x277U         /* 1167 octal */
#define G1        0x365U         /* 1545 octal */
#define CRC_POLI  0x80FU
#define CRC_TOPE  0x800U
#define CRC_MASC  0xFFFU

const uint8_t k_jtty_sync[JTTY_SYNC_SIM] = {
    0U, 2U, 2U, 3U, 0U, 0U, 3U, 2U, 1U, 3U, 1U, 2U, 0U
};

/*
 * EL MAPA DE BITS A TONOS ES GRAY, y eso no es un detalle de estilo: dos
 * tonos vecinos se confunden mucho mas que dos lejanos, y con Gray esa
 * confusion cuesta UN bit en vez de dos.
 *
 *   00 -> tono 0      01 -> tono 1      11 -> tono 2      10 -> tono 3
 */
static const uint8_t k_gray[4] = { 0U, 1U, 3U, 2U };

static uint8_t paridad(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1U);
}

uint16_t jtty_fec_crc12(const uint8_t carga[JTTY_CARGA_BITS])
{
    uint16_t r = 0U;
    uint8_t i;

    for (i = 0U; i < (uint8_t)JTTY_CARGA_BITS; i++) {
        r ^= (uint16_t)((carga[i] & 1U) << (JTTY_CRC_BITS - 1U));
        if ((r & CRC_TOPE) != 0U) { r = (uint16_t)((r << 1) ^ CRC_POLI); }
        else                      { r = (uint16_t)(r << 1); }
        r &= CRC_MASC;
    }
    return r;
}

/* Los 46 bits de informacion: la carga y detras su CRC, de mas peso a
 * menos. */
static void info_bits(const uint8_t carga[JTTY_CARGA_BITS],
                      uint8_t info[JTTY_INFO_BITS])
{
    uint16_t crc = jtty_fec_crc12(carga);
    uint8_t i;

    for (i = 0U; i < (uint8_t)JTTY_CARGA_BITS; i++) { info[i] = (uint8_t)(carga[i] & 1U); }
    for (i = 0U; i < (uint8_t)JTTY_CRC_BITS; i++) {
        info[JTTY_CARGA_BITS + i] =
            (uint8_t)((crc >> (JTTY_CRC_BITS - 1U - i)) & 1U);
    }
}

void jtty_fec_codifica(const uint8_t carga[JTTY_CARGA_BITS],
                       uint8_t tonos[JTTY_SIMBOLOS])
{
    uint8_t info[JTTY_INFO_BITS];
    uint32_t estado = 0UL;
    uint8_t t;

    info_bits(carga, info);

    /*
     * TAIL-BITING: el registro NO empieza a cero, se precarga con los
     * ULTIMOS nueve bits del propio mensaje. Asi el estado de partida es
     * el mismo al que se llegaria dando la vuelta al final, y el trellis
     * se cierra sobre si mismo sin gastar bits de cola - que en una trama
     * de 46 serian nueve, un 20% de desperdicio.
     */
    for (t = 0U; t < (uint8_t)NU; t++) {
        uint8_t b = info[JTTY_INFO_BITS - NU + t];

        estado = ((estado << 1) | b) & MASCARA_E;
    }

    for (t = 0U; t < (uint8_t)JTTY_SIMBOLOS; t++) {
        uint8_t b = info[t];
        uint32_t reg = ((estado << 1) | b) & MASCARA_R;
        uint8_t b0 = paridad(reg & G0);
        uint8_t b1 = paridad(reg & G1);

        tonos[t] = k_gray[(uint8_t)(2U * b0 + b1)];
        estado = ((estado << 1) | b) & MASCARA_E;
    }
}

/* ------------------------------------------------------------------ */

/*
 * QUE TONO LLEGA A CADA ESTADO. Para un estado destino `s` y el bit que
 * se cayo por arriba (0 o 1), el registro completo era s | (bit << nu), y
 * de ahi salen los dos bits de salida y su tono.
 *
 * Se calcula al vuelo en vez de guardarse en una tabla de 1024 bytes: en
 * esta radio la RAM esta mas apretada que los ciclos, y son dos paridades
 * de una palabra.
 */
static uint8_t tono_de(uint16_t s, uint8_t caido)
{
    uint32_t reg = (uint32_t)s | ((uint32_t)caido << NU);
    uint8_t b0 = paridad(reg & G0);
    uint8_t b1 = paridad(reg & G1);

    return k_gray[(uint8_t)(2U * b0 + b1)];
}

static void atras_pon(jtty_fec_ram_t *r, uint8_t t, uint16_t s, uint8_t v)
{
    uint32_t i = (uint32_t)t * ESTADOS + s;

    if (v) { r->atras[i >> 3] |= (uint8_t)(1U << (i & 7U)); }
    else   { r->atras[i >> 3] &= (uint8_t)~(1U << (i & 7U)); }
}

static uint8_t atras_lee(const jtty_fec_ram_t *r, uint8_t t, uint16_t s)
{
    uint32_t i = (uint32_t)t * ESTADOS + s;

    return (uint8_t)((r->atras[i >> 3] >> (i & 7U)) & 1U);
}

#define LISTA_MAX 4U

uint8_t jtty_fec_descodifica(const float energias[JTTY_SIMBOLOS][4],
                             jtty_fec_ram_t *ram, uint8_t lista,
                             uint8_t carga[JTTY_CARGA_BITS])
{
    float   met[LISTA_MAX];
    uint8_t bits[LISTA_MAX][JTTY_INFO_BITS];
    uint8_t vuelta, t, l, i;
    uint16_t s;

    if (ram == 0 || carga == 0) { return 0U; }
    if (lista == 0U) { lista = 1U; }
    if (lista > (uint8_t)LISTA_MAX) { lista = (uint8_t)LISTA_MAX; }

    for (s = 0U; s < (uint16_t)ESTADOS; s++) { ram->m_ant[s] = 0.0f; }
    for (l = 0U; l < lista; l++) { met[l] = -1.0e30f; }

    /*
     * DOS VUELTAS AL TRELLIS (WAVA), y esa es toda la gracia del asunto.
     *
     * Con un codigo normal se sabe que el registro empieza a cero y se
     * arranca el Viterbi desde ahi. Con tail-biting NO se sabe en que
     * estado empieza -depende del propio mensaje-, asi que se arranca con
     * todos los estados igual de probables y se da una vuelta entera; las
     * metricas que quedan al final son una buena estimacion de por donde
     * iba, y con ellas se da la SEGUNDA vuelta, que es la que decide.
     *
     * El estandar usa dos vueltas siempre (lib/jtty/tbcc.f90:146).
     */
    for (vuelta = 0U; vuelta < 2U; vuelta++) {
        for (t = 0U; t < (uint8_t)JTTY_SIMBOLOS; t++) {
            for (s = 0U; s < (uint16_t)ESTADOS; s++) {
                uint16_t p0 = (uint16_t)((s >> 1) & MASCARA_E);
                uint16_t p1 = (uint16_t)(p0 | (1U << (NU - 1U)));
                float m0 = ram->m_ant[p0] + energias[t][tono_de(s, 0U)];
                float m1 = ram->m_ant[p1] + energias[t][tono_de(s, 1U)];

                if (m0 > m1) { ram->m_act[s] = m0; atras_pon(ram, t, s, 0U); }
                else         { ram->m_act[s] = m1; atras_pon(ram, t, s, 1U); }
            }
            for (s = 0U; s < (uint16_t)ESTADOS; s++) { ram->m_ant[s] = ram->m_act[s]; }
        }
    }

    /*
     * LA VUELTA ATRAS SE HACE DESDE LOS 512 ESTADOS, no desde el mejor.
     *
     * Y se queda solo con los caminos que CIERRAN EL CIRCULO: los que
     * acaban en el mismo estado en que empezaron. Esa es la condicion del
     * tail-biting, y es la que separa un camino posible de uno que no
     * podria haber existido nunca.
     */
    for (s = 0U; s < (uint16_t)ESTADOS; s++) {
        uint8_t tmp[JTTY_INFO_BITS];
        uint16_t cur = s;
        uint8_t k;

        for (k = (uint8_t)JTTY_SIMBOLOS; k > 0U; k--) {
            uint16_t p;

            tmp[k - 1U] = (uint8_t)(cur & 1U);
            p = (uint16_t)((cur >> 1) & MASCARA_E);
            if (atras_lee(ram, (uint8_t)(k - 1U), cur)) {
                p = (uint16_t)(p | (1U << (NU - 1U)));
            }
            cur = p;
        }
        if (cur != s) { continue; }

        for (l = 0U; l < lista; l++) {
            if (ram->m_act[s] > met[l]) {
                uint8_t j;

                for (j = (uint8_t)(lista - 1U); j > l; j--) {
                    met[j] = met[j - 1U];
                    memcpy(bits[j], bits[j - 1U], JTTY_INFO_BITS);
                }
                met[l] = ram->m_act[s];
                memcpy(bits[l], tmp, JTTY_INFO_BITS);
                break;
            }
        }
    }

    /*
     * Y AHORA EL FILTRO, que son DOS cosas y no una:
     *
     *   1. el CRC de los 46 bits tiene que dar cero;
     *   2. y el bit 33 de la carga tiene que ser cero.
     *
     * El segundo parece de adorno y no lo es: un candidato equivocado
     * tiene que acertar ademas ese bit por casualidad, o sea que los
     * decodificados falsos se quedan aproximadamente en la mitad. En un
     * modo sin ranuras, que busca continuamente, eso importa mucho mas
     * que en FT8.
     */
    for (l = 0U; l < lista; l++) {
        uint16_t r = 0U;

        if (met[l] <= -1.0e29f) { continue; }
        if (bits[l][JTTY_BIT_RESERVADO - 1U] != 0U) { continue; }
        for (i = 0U; i < (uint8_t)JTTY_INFO_BITS; i++) {
            r ^= (uint16_t)((bits[l][i] & 1U) << (JTTY_CRC_BITS - 1U));
            if ((r & CRC_TOPE) != 0U) { r = (uint16_t)((r << 1) ^ CRC_POLI); }
            else                      { r = (uint16_t)(r << 1); }
            r &= CRC_MASC;
        }
        if (r == 0U) {
            memcpy(carga, bits[l], JTTY_CARGA_BITS);
            return 1U;
        }
    }
    return 0U;
}

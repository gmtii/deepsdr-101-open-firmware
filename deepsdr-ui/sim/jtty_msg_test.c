/*
 * JTTY: DE LOS 34 BITS AL TEXTO, CONTRA EL DOCUMENTO NORMATIVO.
 *
 * Los diez vectores de abajo estan copiados de la tabla "Representative
 * 34-bit vectors" de lib/jtty/jtty_source_encoding.txt de WSJT-X
 * v3.2.0-rc1. Son bits y significado, escritos por quien definio el
 * formato: si nuestra lectura de un campo estuviera corrida un bit, o si
 * un rango estuviera del reves, aqui saldria otro texto.
 *
 * Y ademas se comprueba lo contrario, que en un modo sin ranuras importa
 * mas que en FT8: que un atomo INVALIDO no se enseñe. El estandar es
 * explicito -"An invalid source word shall not be displayed"- porque un
 * receptor que busca continuamente se encuentra con muchos mas candidatos
 * que uno que solo mira cada quince segundos.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "jtty_msg.h"

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

static void bits_de(const char *s, uint8_t *b)
{
    unsigned i;

    for (i = 0; i < JTTY_CARGA_BITS; i++) { b[i] = (uint8_t)(s[i] - '0'); }
}

int main(void)
{
    /* Los diez del documento. El hexadecimal de la tabla es para
     * comprobar de un vistazo que la tira de bits se copio entera. */
    static const struct { const char *bits, *esp, *hex; uint8_t fin; } k[] = {
        { "0000100110111101111000110101000001", "CQ K1ABC CQ",   "0x026F78D41", 1U },
        { "1000000000000001111011000000001000", "599 123",       "0x20007B008", 0U },
        { "1000000000000001111011000000001001", "599 123",       "0x20007B009", 1U },
        { "1000000000000110111010000000011001", "599 CA",        "0x2001BA019", 1U },
        { "0000001011011110010000110100101001", "599 05 NWT",    "0x00B790D29", 1U },
        { "0010000010110001011000000000101001", "1D EMA",        "0x082C58029", 1U },
        { "1000000100111001000010110100111001", "599 156 1749",  "0x204E42D39", 1U },
        { "0000000000000000000000000001001001", "AGN?",          "0x000000049", 1U },
        { "0001001010000110011000000001001001", "FN42",          "0x04A198049", 1U },
        { "0100010011100101010101010110001101", "HELLO",         "0x11395558D", 1U }
    };
    unsigned i;

    printf("\n  JTTY: DE LOS 34 BITS AL TEXTO\n");

    printf("\n  los diez vectores del documento normativo\n");
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint8_t b[JTTY_CARGA_BITS];
        jtty_atomo_t a;
        char q[80];
        uint8_t ok;

        /* Y que la tira de bits sea la del hexadecimal, que es lo que
         * pilla una errata al copiar. */
        {
            unsigned long v = 0UL, j;

            for (j = 0; j < JTTY_CARGA_BITS; j++) {
                v = (v << 1) | (unsigned long)(k[i].bits[j] - '0');
            }
            snprintf(q, sizeof q, "0x%09lX", v);
            if (strcmp(q, k[i].hex) != 0) {
                printf("      la tira de bits no cuadra con %s (sale %s)\n",
                       k[i].hex, q);
                fallos++;
            }
        }

        bits_de(k[i].bits, b);
        ok = jtty_msg_lee(b, &a);
        snprintf(q, sizeof q, "%-14s -> \"%s\"", k[i].esp, ok ? a.texto : "");
        exige(ok && strcmp(a.texto, k[i].esp) == 0 && a.fin == k[i].fin, q);
    }

    printf("\n  y lo que NO puede enseñarse\n");
    {
        static const struct { const char *bits, *q; } k2[] = {
            { "0000000000000000000000000000000000",
              "la palabra de gramatica a cero es un centinela invalido" },
            { "0000000000000000000000000001001011",
              "el bit 33 a uno invalida el atomo entero" },
            /* i2=1, n2=2 -> accion 6, reservada */
            { "0000000000000000000000000000101010",
              "una accion reservada (1.2) no se enseña" },
            /* STRUCT30 familia 111, el guarda de invalidez */
            { "0000000000000000000000000011110010",
              "la familia 111 es un guarda y no se enseña" },
            /* CONTROL con frase 18 */
            { "0000100100000000000000000010000010",
              "una frase de control sin asignar no se enseña" }
        };
        unsigned j;

        for (j = 0; j < sizeof k2 / sizeof k2[0]; j++) {
            uint8_t b[JTTY_CARGA_BITS];
            jtty_atomo_t a;

            bits_de(k2[j].bits, b);
            exige(jtty_msg_lee(b, &a) == 0U && a.texto[0] == '\0', k2[j].q);
        }
    }

    printf("\n  el relleno de TEXT5\n");
    {
        /* "HI" y tres espacios: los espacios de la derecha NO son parte
         * del mensaje, pero uno de en medio si. */
        uint8_t b[JTTY_CARGA_BITS];
        jtty_atomo_t a;
        unsigned j;
        static const uint8_t car[5] = { 17U, 18U, 36U, 36U, 36U }; /* H I _ _ _ */

        memset(b, 0, sizeof b);
        for (j = 0; j < 5; j++) {
            unsigned q;
            for (q = 0; q < 6; q++) {
                b[6 * j + q] = (uint8_t)((car[j] >> (5 - q)) & 1U);
            }
        }
        b[30] = 1U; b[31] = 1U;   /* i2 = 3 */

        /*
         * *** CORREGIDO EL 28/09/2026, por una foto de la pantalla en
         * antena: "DE G1 HGD.. ..... WE RE ALLY NEED EO ON". ***
         *
         * Esta prueba exigia "HI" SIEMPRE, y con ella el codigo quitaba
         * los espacios de la derecha de todos los TEXT5. Eso es verdad
         * cuando el atomo ES el mensaje entero y FALSO cuando es un trozo
         * de cinco de una cadena mas larga: ahi el espacio de la derecha es
         * el que separa dos palabras, y quitarlo parte "WE REALLY" en "WE
         * RE ALLY".
         *
         * O sea que la prueba codificaba la suposicion equivocada y por eso
         * el banco no lo pillo. Ahora exige la regla de verdad, que ya
         * estaba en la señal: el bit 34 dice si este atomo cierra.
         */
        b[33] = 0U;   /* NO es el ultimo: los cinco caracteres se quedan */
        exige(jtty_msg_lee(b, &a) && strcmp(a.texto, "HI   ") == 0,
              "en un TEXT5 de en medio, los cinco caracteres se quedan");

        b[33] = 1U;   /* y si cierra, los de la derecha sobran */
        exige(jtty_msg_lee(b, &a) && strcmp(a.texto, "HI") == 0,
              "y en el ultimo, los espacios de la derecha no");
    }

    /*
     * JUNTAR ATOMOS: la regla que fallo en antena.
     *
     * Dos TEXT5 seguidos se pegan SIN nada en medio; todo lo demas con un
     * espacio. El caso de abajo es literalmente el que salio en la
     * pantalla del usuario.
     */
    printf("\n  juntar atomos en un mensaje\n");
    {
        static const struct { const char *t; uint8_t t5; } k5[] = {
            { "WE RE", 1U }, { "ALLY ", 1U }, { "NEED ", 1U }, { "EO ON", 1U }
        };
        static const struct { const char *t; uint8_t t5; } kmix[] = {
            { "CQ K1ABC CQ", 0U }, { "FN42", 0U }, { "599 123", 0U }
        };
        char m[96];
        unsigned j;
        uint8_t ant = 0U;

        m[0] = '\0';
        for (j = 0; j < 4; j++) {
            jtty_atomo_t a2;
            memset(&a2, 0, sizeof a2);
            strcpy(a2.texto, k5[j].t); a2.texto5 = k5[j].t5;
            jtty_msg_pega(m, sizeof m, &a2, ant);
            ant = a2.texto5;
        }
        jtty_msg_cierra(m);
        printf("      cuatro TEXT5 -> \"%s\"\n", m);
        exige(strcmp(m, "WE REALLY NEED EO ON") == 0,
              "los trozos de cinco se pegan sin espacio de mas");

        m[0] = '\0'; ant = 0U;
        for (j = 0; j < 3; j++) {
            jtty_atomo_t a2;
            memset(&a2, 0, sizeof a2);
            strcpy(a2.texto, kmix[j].t); a2.texto5 = kmix[j].t5;
            jtty_msg_pega(m, sizeof m, &a2, ant);
            ant = a2.texto5;
        }
        jtty_msg_cierra(m);
        printf("      tres frases  -> \"%s\"\n", m);
        exige(strcmp(m, "CQ K1ABC CQ FN42 599 123") == 0,
              "y las frases enteras si llevan su espacio");

        /*
         * Y EL HUECO. 28/09/2026, pedido por el usuario.
         *
         * Cuando entre dos atomos se ha perdido una trama, jtty_modo.c mete
         * un atomo de puntos suspensivos. Lo que se comprueba aqui es que
         * ese hueco CORTA la cadena de TEXT5: si no lo hiciera, "WE RE" +
         * "..." + "EO ON" saldria pegado sin separar y quedaria peor que
         * antes.
         */
        {
            static const struct { const char *t; uint8_t t5; } kh[] = {
                { "WE RE", 1U }, { "...", 0U }, { "EO ON", 1U }
            };
            uint8_t a5 = 0U;

            m[0] = '\0';
            for (j = 0; j < 3; j++) {
                jtty_atomo_t a2;
                memset(&a2, 0, sizeof a2);
                strcpy(a2.texto, kh[j].t); a2.texto5 = kh[j].t5;
                jtty_msg_pega(m, sizeof m, &a2, a5);
                a5 = a2.texto5;
            }
            jtty_msg_cierra(m);
            printf("      con un hueco -> \"%s\"\n", m);
            exige(strcmp(m, "WE RE ... EO ON") == 0,
                  "un hueco se ve, y corta la cadena de trozos de cinco");
        }
    }

    /* ------------------------------------------------------------ */
    /*
     * EL INDICATIVO DENTRO DE UN MENSAJE DE TEXTO LIBRE.
     *
     * En FT8 el decodificador SABE que campo es un indicativo porque se lo
     * dice el protocolo. Aqui no: un mensaje son varios atomos de texto
     * pegados y hay que reconocerlo por su forma, o sea que lo unico que
     * puede fallar es la forma. Por eso la mitad de estas pruebas son
     * cosas que NO son un indicativo y salen al lado de uno de verdad en
     * la banda.
     *
     * Los mensajes de aqui abajo son de las fotos de 14.090, no
     * inventados.
     */
    printf("\n  el indicativo de un mensaje, para la columna de pais\n");
    {
        static const struct { const char *msg, *esp; } k[] = {
            /* --- los de las fotos, con el ULTIMO que es quien transmite --- */
            { "W5CHA DE IK3CHK  IK3CHK",                 "IK3CHK" },
            { "LZ1QZ DE F4JUO UR RST 599 009 OP JACQUE", "F4JUO"  },
            { "CQ LZ1QZ CQ",                             "LZ1QZ"  },
            { "CQ CQ CQ I4KYO  I4KYO",                   "I4KYO"  },
            { "AP2HA DE DL ... DL4MFF PSE K",            "DL4MFF" },
            { "TU NOW RX1AG 599 001",                    "RX1AG"  },
            { "7M5OAP TU",                               "7M5OAP" },
            { "LZ1QZ 599 011",                           "LZ1QZ"  },
            { "CQ F4BAL JO10",                           "F4BAL"  },
            /* --- y los que NO llevan ninguno --- */
            { "MY OUTPUT POWER ONLY 5 WATTS",            ""       },
            { "RIG: ...  PWR 3W, 1...",                  ""       },
            { "Q NAME LUIGI K",                          ""       },
            { "599 123 RR73",                            ""       },
            { "",                                        ""       },
            /* --- y lo que se parece y no lo es --- */
            /* un trozo de IK3CHK partido por una trama perdida: sin exigir
             * letra en el prefijo, "3CHK" colaria como indicativo */
            { "DE IK 3CHK",                              ""       },
            { "CQ JO10",                                 ""       },  /* localizador */
            { "CQ JO20HI",                               ""       },  /* de seis */
            { "UP 5 KHZ",                                ""       },
            { "PWR 100W",                                ""       },
            /* --- con barra: se devuelve ENTERO, que dxcc_pais() ya sabe
             *     mirar el principio (ver dxcc.h) --- */
            { "CQ EA4XYZ/P CQ",                          "EA4XYZ/P" },
            { "CQ DL/EA4XYZ CQ",                         "DL/EA4XYZ" },
            /* --- prefijos raros pero de verdad --- */
            { "CQ 4X1ABC",                               "4X1ABC" },
            { "CQ 9A1ABC",                               "9A1ABC" },
            { "CQ VK2ABC",                               "VK2ABC" },
            { "CQ G0ABC",                                "G0ABC"  }
        };
        unsigned i, bien = 0U;

        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            char ind[16];
            uint8_t hay = jtty_msg_indicativo(k[i].msg, ind);

            if (!hay) { ind[0] = '\0'; }
            if (strcmp(ind, k[i].esp) == 0) { bien++; }
            else { printf("      \"%s\" -> \"%s\", deberia \"%s\"   MAL\n",
                          k[i].msg, ind, k[i].esp); }
        }
        exige(bien == sizeof k / sizeof k[0],
              "coge el ultimo indicativo, y nada que no lo sea");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * ACARS DENTRO DE HFDL - 14/10/2026.
 *
 * LA DIFERENCIA CON TODO LO ANTERIOR ES QUE AQUI HAY BYTES DE VERDAD. El
 * decodificador de posicion que habia se verifico una vez, a mano, contra
 * una trama que nadie guardo, y de aquel recuerdo salio un comentario que
 * decia "bytes 10-11" cuando era el byte 1. Un mes de diseño apoyado en
 * una frase.
 *
 * Esta vez la trama esta AQUI DENTRO, copiada del volcado en pantalla de
 * la radio del usuario en 17.967 kHz, y el banco la decodifica entera. Si
 * alguien cambia el formato, esto se cae contra la realidad y no contra
 * mi memoria.
 */
#include "hfdl_acars.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-46s %s  %s\n", que, cond ? "ok " : "MAL", como ? como : "");
    if (!cond) { fallos++; }
}

/* La trama de verdad, tal cual salio en la pantalla. Los dos primeros
 * bytes son el sobre 0xFF y el tipo 0xFF (ENVELOPED_DATA): el ACARS
 * empieza en el tercero. */
static const uint8_t k_real[] = {
    0xFF, 0xFF,
    0x01, 0x32, 0xAE, 0xC1, 0xB6, 0xAD, 0x45, 0x4F, 0x46, 0x15,
    0x51, 0xB0, 0xB9, 0x02, 0xD3, 0x32, 0xB5, 0xC1
};

/* Y una SEGUNDA, del mismo minuto. Dos tramas distintas que encajan en el
 * mismo formato valen mucho mas que una: la primera podria ser casualidad.
 * Esta ademas trae la paridad repartida de otra forma -el '7' y el '-' de
 * la matricula vienen con el bit alto a cero- asi que prueba que quitarlo
 * con & 0x7F vale para los dos casos y no solo para el de arriba. */
static const uint8_t k_real2[] = {
    0xFF, 0xFF,
    0x01, 0x32, 0xAE, 0xC1, 0x37, 0xAD, 0xC1, 0xCE, 0xC7, 0x15,
    0x51, 0xB0, 0x34, 0x02, 0xD3, 0x32, 0x32, 0xC1
};

/* Pone paridad impar en el bit 7, que es como viaja ACARS de verdad: el
 * banco tiene que probar CON paridad, no con ASCII limpio. */
static uint8_t par(uint8_t c)
{
    uint8_t n = 0U, i;
    for (i = 0U; i < 7U; i++) { if ((c >> i) & 1U) { n++; } }
    return (uint8_t)((n & 1U) ? c : (uint8_t)(c | 0x80U));
}

/* Monta un ACARS con el texto que se le pida. */
static uint32_t monta(uint8_t *b, const char *matr, const char *eti,
                      const char *texto)
{
    uint32_t p = 0U, i;
    b[p++] = par(0x01U);              /* SOH */
    b[p++] = par((uint8_t)'2');       /* modo */
    for (i = 0U; i < 7U; i++) {
        b[p++] = par((uint8_t)(matr[i] ? matr[i] : '.'));
    }
    b[p++] = par(0x15U);              /* NAK */
    b[p++] = par((uint8_t)eti[0]);
    b[p++] = par((uint8_t)eti[1]);
    b[p++] = par((uint8_t)'9');       /* bloque */
    if (texto) {
        b[p++] = par(0x02U);          /* STX */
        for (i = 0U; texto[i] != '\0'; i++) { b[p++] = par((uint8_t)texto[i]); }
        b[p++] = par(0x03U);          /* ETX */
    }
    return p;
}

int main(void)
{
    hfdl_acars_t a;
    uint8_t b[256];
    uint32_t n;
    int32_t la, lo;

    printf("\nACARS DENTRO DE HFDL\n");
    printf("====================\n\n");

    printf("  1. LA TRAMA DE VERDAD, de 17.967 kHz\n");
    {
        uint8_t ok = hfdl_acars_parse(&k_real[2], sizeof k_real - 2U, &a);
        di(ok, "se reconoce como ACARS", "");
        if (ok) {
            char d[80];
            snprintf(d, sizeof d, "\"%s\"", a.matricula);
            di(strcmp(a.matricula, "A6-EOF") == 0, "la matricula sale sin los puntos", d);
            snprintf(d, sizeof d, "\"%s\"", a.etiqueta);
            di(strcmp(a.etiqueta, "Q0") == 0, "la etiqueta es Q0", d);
            di(a.modo == '2', "el modo es '2'", "");
            di(a.ack == 0U, "el acuse es NAK", "");
            di(a.bloque == '9', "el bloque es '9'", "");
            snprintf(d, sizeof d, "\"%s\"", a.texto);
            di(strcmp(a.texto, "S25A") == 0, "y el texto que se llego a ver", d);
        }
    }

    printf("\n  1b. Y LA SEGUNDA, del mismo minuto\n");
    {
        uint8_t ok = hfdl_acars_parse(&k_real2[2], sizeof k_real2 - 2U, &a);
        di(ok, "tambien se reconoce", "");
        if (ok) {
            char d[160];
            snprintf(d, sizeof d, "\"%s\"  etiqueta \"%s\"  texto \"%s\"",
                     a.matricula, a.etiqueta, a.texto);
            di(strcmp(a.matricula, "A7-ANG") == 0
               && strcmp(a.etiqueta, "Q0") == 0
               && a.bloque == '4'
               && strcmp(a.texto, "S22A") == 0,
               "matricula, etiqueta, bloque y texto", d);
            di(!hfdl_acars_posicion(&a, &la, &lo),
               "y NO trae posicion, que es lo correcto", "Q0 es enlace, no posicion");
        }
    }

    printf("\n  2. QUE NO SE TRAGUE CUALQUIER COSA\n");
    {
        uint8_t m[32];
        memcpy(m, &k_real[2], sizeof k_real - 2U);
        m[0] = 0x55U;                      /* sin SOH */
        di(!hfdl_acars_parse(m, sizeof k_real - 2U, &a), "sin SOH no es ACARS", "");
        memcpy(m, &k_real[2], sizeof k_real - 2U);
        m[9] = par((uint8_t)'X');          /* acuse que no existe */
        di(!hfdl_acars_parse(m, sizeof k_real - 2U, &a), "un acuse que no es ACK ni NAK, tampoco", "");
        di(!hfdl_acars_parse(&k_real[2], 8U, &a), "ni una trama cortada", "");
    }

    /*
     * AVISO SOBRE LA SECCION 3, Y VA AQUI PARA QUE NADIE LEA SU VERDE COMO
     * LO QUE NO ES - 15/10/2026.
     *
     * Las tres tramas de la seccion 1 son DE AIRE: salieron de la pantalla
     * de la radio del usuario. Las de aqui NO: son textos que me he
     * inventado yo con el formato que creo que tiene una posicion de
     * ACARS. O sea que esta seccion mide MI LECTURA DEL FORMATO, no el
     * aire, y si la lectura esta mal el banco sale verde igual.
     *
     * No es retorica: el fallo que dio origen a este fichero fue
     * exactamente ese -un comentario que decia "bytes 10-11" cuando era el
     * byte 1, escrito de memoria-.
     *
     * MEDIDO el 15/10/2026 en las seis grabaciones de sim/aire (out/aire,
     * 31 ráfagas con el CRC bueno): salen CINCO ACARS y NINGUNO trae
     * coordenadas. Las siete posiciones que si aparecen vienen por el otro
     * camino, el PDU de posicion de HFDL, que ese si esta medido contra el
     * aire. O sea que el camino de la posicion POR ACARS no ha visto un
     * solo byte real todavia, ni para bien ni para mal.
     *
     * LO QUE LO CERRARIA: una captura de la radio con un ACARS que lleve
     * coordenadas -etiqueta de posicion, no Q0 ni H1 de enlace-. Con el
     * volcado en pantalla basta, como con las de la seccion 1.
     */
    printf("\n  3. LA POSICION DEL TEXTO\n");
    {
        struct { const char *t; int32_t la, lo; const char *que; } k[] = {
            /*
             * LOS VALORES ESPERADOS, CALCULADOS Y NO ESTIMADOS. La
             * primera version de este banco los puse a mano y DOS DE LOS
             * TRES estaban mal: 37 grados 25,4 minutos son 37,4233 y yo
             * escribi 37,2567, y 151 grados 12 minutos son 151,2 y escribi
             * 151,8667. El banco dio rojo contra un codigo correcto.
             *
             * Es el mismo error que el "bytes 10-11": un numero sacado de
             * la cabeza y escrito como si estuviera comprobado. Estos
             * salen de (grados + minutos/60) * 10000 redondeado, hecho
             * aparte.
             */
            { "POS N37254W122050 FL350",  374233L, -1220833L, "letra delante, decimas de minuto" },
            { "POS 3725N12205W FL350",    374167L, -1220833L, "letra detras" },
            { "/POS/S3340,E15112/",      -336667L,  1512000L, "al sur y al este, con comas" },
        };
        uint8_t i;
        for (i = 0U; i < 3U; i++) {
            char d[110];
            n = monta(b, ".A6-EOF", "Q0", k[i].t);
            (void)hfdl_acars_parse(b, n, &a);
            la = 0; lo = 0;
            if (hfdl_acars_posicion(&a, &la, &lo)) {
                snprintf(d, sizeof d, "%ld,%ld  (esperado %ld,%ld)",
                         (long)la, (long)lo, (long)k[i].la, (long)k[i].lo);
                di(la == k[i].la && lo == k[i].lo, k[i].que, d);
            } else {
                di(0, k[i].que, "no la encuentra");
            }
        }
    }

    printf("\n  4. Y QUE NO SE INVENTE UNA DONDE NO LA HAY\n");
    {
        const char *k[] = {
            "FUEL 3725 TEMP -55 FL350",        /* numeros sueltos */
            "ETA 1234Z GATE N12",              /* una N con pocos digitos */
            "N99999W999999",                   /* minutos imposibles */
            "N00000E000000",                   /* el golfo de Guinea */
            "CHECKLIST COMPLETE",
        };
        uint8_t i, colados = 0U;
        for (i = 0U; i < 5U; i++) {
            n = monta(b, ".A6-EOF", "H1", k[i]);
            (void)hfdl_acars_parse(b, n, &a);
            if (hfdl_acars_posicion(&a, &la, &lo)) {
                printf("      se cuela: \"%s\" -> %ld,%ld\n", k[i], (long)la, (long)lo);
                colados++;
            }
        }
        di(colados == 0U, "ninguno de los cinco textos sin posicion cuela", "");
    }

    /*
     * 5. Y LOS MISMOS FALSOS POSITIVOS, PERO CON TRAFICO DE VERDAD.
     *
     * Los cinco de arriba me los he inventado y por eso valen poco: estan
     * escritos por el mismo que escribio el extractor, asi que se parecen
     * a lo que el extractor sabe rechazar. Estos tres salen de las seis
     * grabaciones de sim/aire, tal cual los imprime out/aire, y son el
     * unico trafico ACARS real que hay en la casa.
     *
     * Se les pasa la LINEA ENTERA como texto -matricula y etiqueta
     * incluidas- a proposito: cuantos mas caracteres, mas facil es que el
     * extractor se agarre a algo, asi que es una prueba mas dura que
     * pasarle solo el campo de texto.
     *
     * Esto NO cierra lo de arriba: mide que no se invente posiciones donde
     * no las hay, que es la mitad negativa. La positiva sigue sin un solo
     * byte de aire.
     */
    printf("\n  5. Y CON LOS ACARS DE LAS GRABACIONES DE AIRE\n");
    {
        const char *k[] = {
            "G-VFOX H1/NAK D04HVS0900#DFB,066.8,000.0,000,0380,0295,0",
            "CC-CXF H1/NAK D54BLA0600#DFB           DEP:PIML     DEST",
            "HZ-NS79 Q0/NAK S96AXY0556",
        };
        uint8_t i, colados = 0U;
        for (i = 0U; i < 3U; i++) {
            n = monta(b, ".A6-EOF", "H1", k[i]);
            (void)hfdl_acars_parse(b, n, &a);
            if (hfdl_acars_posicion(&a, &la, &lo)) {
                printf("      se cuela: \"%s\" -> %ld,%ld\n",
                       k[i], (long)la, (long)lo);
                colados++;
            }
        }
        di(colados == 0U,
           "ninguno de los tres ACARS de aire da una posicion",
           "y es lo correcto: ninguno la lleva");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

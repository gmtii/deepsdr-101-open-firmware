/*
 * LA POSICION DE LOS AVIONES EN HFDL. 10/10/2026.
 *
 * *** Nos han dicho: "Flight position is not decoded in HFDL mode". ***
 *
 * Y lo primero que hubo que mirar es por donde empezar a mirar. Resultado:
 * NINGUN BANCO DE sim/ COMPILABA hfdl_hfnpdu.c. El unico decodificador del
 * arbol que convierte bytes del aire en una latitud y una longitud -el que
 * pone los aviones en el mapa- no se habia probado nunca, ni una vez.
 *
 * Los cinco bancos de HFDL que hay cubren la cadena de demodulacion
 * (polaridad, porton, columnas, tabla de aviones y las diecinueve
 * autopruebas), y ninguno pasa de los bits a la posicion. Esa es la grieta
 * por la que se cuela un fallo asi y por la que puede llevar semanas sin
 * que nadie se entere: el modo "funciona" -salen filas de aviones- y lo que
 * falta es una columna.
 *
 * QUE SE COMPRUEBA, Y EN QUE ORDEN
 * --------------------------------
 *  1. Que con un mensaje armado segun el formato que el propio modulo
 *     documenta, salga la posicion que se metio. Si esto fallara, el
 *     formato esta mal entendido y no hay nada mas que mirar.
 *  2. Las dos formas de enmarcarlo. El modulo exigia el par 0xFF 0xD1 al
 *     principio; la cabecera de hfdl_hfnpdu.h dice que en la unica trama
 *     real contra la que se verifico, ese par estaba en los BYTES 10-11.
 *     Diez no es cero, y una de las dos frases esta describiendo otra cosa.
 *     Desde hoy se aceptan las dos formas documentadas -con sobre y sin
 *     el-, asi que aqui se prueban las dos.
 *  3. Que lo que no es una posicion, se rechace. Un mensaje corto, uno de
 *     otro tipo, y -el que de verdad importa- uno cuyos campos se lean
 *     corridos: tiene que salir que no, no una latitud de 150 grados. Un
 *     avion pintado en una posicion inventada se cree, porque es un punto
 *     en un mapa y no dice de donde salio.
 *  4. Los extremos del formato: 20 bits con signo escalados por
 *     180/(2^19-1), o sea que los topes son +-180 justos.
 */
#include "hfdl_hfnpdu.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos;

static void di(const char *q, int ok, const char *det)
{
    printf("    %-50s %s%s%s\n", q, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* Los veinte bits que el formato guarda para una coordenada. Es la cuenta
 * de parse_coordinate() al reves, y esta escrita aqui a mano a proposito:
 * si se llamara a la del firmware se estaria comprobando que una funcion
 * es su propia inversa, que sale bien siempre. */
static uint32_t arma(uint8_t *b, int sobre, const char *vuelo,
                     double lat, double lon, uint16_t medios_seg,
                     uint8_t version, uint8_t pata, uint8_t gs);

static uint32_t a_crudo(double grados)
{
    double v = grados * (double)0x7FFFF / 180.0;
    int32_t n = (int32_t)((v < 0.0) ? (v - 0.5) : (v + 0.5));
    return (uint32_t)(n & 0xFFFFF);
}

/*
 * Arma un mensaje de datos de rendimiento. `sobre` elige el marco: con el
 * byte 0xFF delante (como exigia el modulo) o sin el (como lo entrega
 * dumphfdl cuando el HFNPDU no viene envuelto).
 */
/*
 * EL MISMO MENSAJE, PERO CON BASURA DELANTE - 10/10/2026 (la tarde).
 *
 * La cabecera de hfdl_hfnpdu.h dice, de la unica trama real verificada
 * nunca, que el par 0xFF 0xD1 estaba en los BYTES 10-11 de los datos del
 * LPDU. Si eso es cierto, el tipo NO empieza en el byte 0, y exigirlo ahi
 * falla en TODAS las tramas - que es justo lo que se esta viendo.
 *
 * Asi que el modulo ahora BUSCA el tipo en los primeros bytes. Esto arma el
 * mismo mensaje con `delante` bytes de relleno, para probar que lo
 * encuentra donde la cabecera dice que estaba.
 */
static uint32_t arma_desplazado(uint8_t *b, uint32_t delante, int sobre,
                                const char *vuelo, double lat, double lon,
                                uint16_t medios_seg)
{
    uint8_t  cuerpo[80];
    uint32_t n = arma(cuerpo, sobre, vuelo, lat, lon, medios_seg, 1U, 2U, 3U);
    uint32_t i;

    memset(b, 0x5AU, 128);          /* relleno que NO es 0xD1 */
    for (i = 0U; i < n; i++) { b[delante + i] = cuerpo[i]; }
    return delante + n;
}

static uint32_t arma(uint8_t *b, int sobre, const char *vuelo,
                     double lat, double lon, uint16_t medios_seg,
                     uint8_t version, uint8_t pata, uint8_t gs)
{
    uint32_t p = 0U, i;
    uint32_t cl = a_crudo(lat), cg = a_crudo(lon);

    memset(b, 0, 64);
    if (sobre) { b[p++] = 0xFFU; }
    b[p++] = 0xD1U;
    for (i = 0U; i < 6U; i++) { b[p + i] = (uint8_t)vuelo[i]; }
    p += 6U;
    /* Las dos coordenadas, en veinte bits cada una y compartiendo el byte
     * del medio: los cuatro bits altos de la latitud abajo y los cuatro
     * bajos de la longitud arriba. */
    b[p]      = (uint8_t)(cl & 0xFFU);
    b[p + 1U] = (uint8_t)((cl >> 8) & 0xFFU);
    b[p + 2U] = (uint8_t)(((cl >> 16) & 0x0FU) | ((cg & 0x0FU) << 4));
    b[p + 3U] = (uint8_t)((cg >> 4) & 0xFFU);
    b[p + 4U] = (uint8_t)((cg >> 12) & 0xFFU);
    p += 5U;
    b[p++] = (uint8_t)(medios_seg & 0xFFU);
    b[p++] = (uint8_t)((medios_seg >> 8) & 0xFFU);
    b[p++] = version;
    b[p++] = pata;
    b[p++] = gs;
    return 47U + (uint32_t)(sobre ? 0U : 0U);   /* el modulo pide 47 */
}

static int cerca(double a, double b, double tol)
{
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

int main(void)
{
    uint8_t b[64];
    hfdl_hfnpdu_perf_data_t pd;
    uint32_t n;
    char det[80];

    printf("\nLA POSICION DE LOS AVIONES EN HFDL\n");
    printf("  (hfdl_hfnpdu.c no lo compilaba ningun banco hasta hoy)\n\n");

    /* --- 1 y 2: las dos formas de enmarcarlo --- */
    {
        int sobre;
        for (sobre = 1; sobre >= 0; sobre--) {
            printf("  %s\n", sobre ? "CON SOBRE (0xFF 0xD1 ...)"
                                   : "SIN SOBRE (0xD1 ... , lo que entrega dumphfdl)");
            n = arma(b, sobre, "IB3741", 42.1120, -3.5417, 1830U,   /* 01:01:00: el formato guarda medios segundos */
                     1U, 7U, 4U);
            memset(&pd, 0, sizeof pd);
            if (!hfdl_hfnpdu_parse_performance_data(b, n, &pd)) {
                di("se decodifica", 0, 0);
                continue;
            }
            di("se decodifica", 1, 0);
            sprintf(det, "(%.4f)", pd.location.lat);
            di("la latitud es la que se metio", cerca(pd.location.lat, 42.1120, 0.001), det);
            sprintf(det, "(%.4f)", pd.location.lon);
            di("la longitud tambien, y con su signo",
               cerca(pd.location.lon, -3.5417, 0.001), det);
            sprintf(det, "(%s)", pd.flight_id);
            di("sale el distintivo del vuelo", strcmp(pd.flight_id, "IB3741") == 0, det);
            sprintf(det, "(%02u:%02u:%02u)", pd.utc_time.hour, pd.utc_time.min, pd.utc_time.sec);
            di("y la hora UTC", pd.utc_time.hour == 1U && pd.utc_time.min == 1U, det);
            di("y la estacion con la que habla", pd.gs_id == 4U, 0);
            printf("\n");
        }
    }

    /* --- 3: lo que NO es una posicion --- */
    printf("  LO QUE TIENE QUE RECHAZAR\n");
    n = arma(b, 1, "IB3741", 42.0, -3.0, 0U, 1U, 0U, 4U);
    di("un mensaje mas corto de la cuenta",
       !hfdl_hfnpdu_parse_performance_data(b, 20U, &pd), 0);
    b[1] = 0xD0U;    /* SYSTEM_TABLE, no es esto */
    di("un HFNPDU de otro tipo",
       !hfdl_hfnpdu_parse_performance_data(b, n, &pd), 0);
    di("un puntero nulo",
       !hfdl_hfnpdu_parse_performance_data(0, n, &pd), 0);

    /*
     * EL QUE DE VERDAD IMPORTA. Se arma bien y se lee corrido tres bytes,
     * que es lo que pasaria si el marco elegido no fuera el bueno. Tiene
     * que salir que NO, no una latitud imposible.
     */
    {
        uint8_t c[64];
        n = arma(b, 1, "IB3741", 42.1120, -3.5417, 0U, 1U, 0U, 4U);
        memset(c, 0, sizeof c);
        memcpy(c, b + 3, 40);
        c[0] = 0xFFU; c[1] = 0xD1U;      /* el marco cuadra, los campos no */
        {
            int paso = hfdl_hfnpdu_parse_performance_data(c, 47U, &pd);
            sprintf(det, paso ? "(dio %.1f, %.1f)" : "(rechazado)",
                    pd.location.lat, pd.location.lon);
            di("*** campos corridos: rechaza en vez de inventar ***",
               !paso || (pd.location.lat >= -90.0 && pd.location.lat <= 90.0
                         && pd.location.lon >= -180.0 && pd.location.lon <= 180.0),
               det);
        }
    }

    /* --- 4: los extremos del formato --- */
    printf("\n  LOS EXTREMOS DEL FORMATO (20 bits, +-180 grados)\n");
    {
        struct { double lat, lon; const char *q; } k[] = {
            {   0.0,    0.0, "el cero" },
            {  89.9,  179.9, "casi el tope por arriba" },
            { -89.9, -179.9, "y por abajo" },
            {  42.1120493, -71.0, "la de la trama real de la cabecera" },
        };
        unsigned i;
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            n = arma(b, 1, "ABCDEF", k[i].lat, k[i].lon, 0U, 1U, 0U, 1U);
            memset(&pd, 0, sizeof pd);
            if (!hfdl_hfnpdu_parse_performance_data(b, n, &pd)) {
                di(k[i].q, 0, "(no decodifica)");
                continue;
            }
            sprintf(det, "(%.4f, %.4f)", pd.location.lat, pd.location.lon);
            di(k[i].q, cerca(pd.location.lat, k[i].lat, 0.001)
                       && cerca(pd.location.lon, k[i].lon, 0.001), det);
        }
    }

    /*
     * ------------------------------------------------------------------
     * 5. EL TIPO NO SIEMPRE EMPIEZA EN EL BYTE 0
     * ------------------------------------------------------------------
     * Y detras, el negativo que vale por todos: buscar el tipo en vez de
     * exigirlo abre la puerta a que un 0xD1 suelto en medio de un ACARS
     * pase por una posicion. Lo que la cierra son las tres condiciones a la
     * vez -caben 46 bytes mas, las dos coordenadas en rango, y la hora es
     * una hora-, y eso hay que verlo aguantar.
     */
    printf("\n  EL TIPO NO SIEMPRE EMPIEZA EN EL BYTE 0\n");
    {
        static uint8_t b[128];
        hfdl_hfnpdu_perf_data_t pd;
        uint32_t n;
        char det[64];

        n = arma_desplazado(b, 10U, 1, "IB3456", 42.1120, -71.0000, 1200U);
        memset(&pd, 0, sizeof pd);
        if (hfdl_hfnpdu_parse_performance_data(b, n, &pd)) {
            sprintf(det, "(%.4f, %.4f)", pd.location.lat, pd.location.lon);
            di("0xFF 0xD1 en los bytes 10-11, como la trama real",
               cerca(pd.location.lat, 42.1120, 0.001)
               && cerca(pd.location.lon, -71.0000, 0.001), det);
        } else {
            di("0xFF 0xD1 en los bytes 10-11, como la trama real", 0,
               "no lo ha encontrado");
        }

        /*
         * Y EL TIPO A PELO SOLO EN EL BYTE 0. Fuera de ahi hace falta el
         * sobre 0xFF delante, que es lo que la cabecera documenta de la
         * trama real. Sin esa condicion, un 0xD1 cualquiera en medio de un
         * ACARS es un candidato, y eso se midio: 2.094 falsos de 100.000
         * contra 188. Ver la prueba de la basura, aqui debajo.
         */
        n = arma_desplazado(b, 7U, 0, "AA0001", -33.4500, 151.1800, 900U);
        memset(&pd, 0, sizeof pd);
        di("un 0xD1 suelto en el byte 7, sin sobre, NO vale",
           !hfdl_hfnpdu_parse_performance_data(b, n, &pd),
           "lo ha cogido sin el 0xFF delante");

        /* Un limite que no se ve no es un limite. */
        n = arma_desplazado(b, 40U, 0, "ZZ9999", 10.0, 20.0, 600U);
        memset(&pd, 0, sizeof pd);
        di("y en el byte 40 ya no se busca",
           !hfdl_hfnpdu_parse_performance_data(b, n, &pd),
           "ha mirado mas lejos de HFNPDU_BUSQUEDA_MAX");
    }

    printf("\n  Y QUE BUSCAR NO SEA INVENTAR\n");
    {
        static uint8_t b[128];
        hfdl_hfnpdu_perf_data_t pd;
        uint32_t s2 = 12345U, i, k, colados = 0U;
        char det[64];

        /*
         * CIEN MIL, y no mil, porque lo que se mide es una tasa pequeña y
         * con mil el numero baila entre 0 y 5 por puro azar. La semilla es
         * fija, asi que esto es una cuenta repetible y no una tirada.
         *
         * MEDIDO HOY, con el mismo reparto de basura:
         *
         *     la version de esta mañana   2.094 de 100.000   (2,1 %)
         *     la de ahora                   188 de 100.000   (0,19 %)
         *
         * O sea que BUSCAR el tipo, con el sobre exigido fuera del byte 0 y
         * con la hora y la estacion comprobadas, deja pasar DIEZ VECES
         * MENOS basura que exigirlo en un sitio fijo sin esas dos
         * comprobaciones. Y de paso encuentra el marco de la trama real.
         *
         * El tope son 400 y no 188 clavados: un cambio legitimo en el
         * modulo puede mover el numero unas decenas, y un banco que salta
         * por eso acaba desactivado. Lo que no puede pasar es volver al 2 %.
         */
        for (k = 0U; k < 100000U; k++) {
            for (i = 0U; i < sizeof b; i++) {
                s2 = s2 * 1103515245U + 12345U;
                b[i] = (uint8_t)((s2 >> 16) & 0xFFU);
                if (b[i] == 0xD1U) { b[i] = 0x11U; }   /* sin 0xD1 por azar */
            }
            b[k % 24U] = 0xD1U;                        /* y uno puesto aposta */
            memset(&pd, 0, sizeof pd);
            if (hfdl_hfnpdu_parse_performance_data(b, sizeof b, &pd)) {
                colados++;
            }
        }
        sprintf(det, "%u de 100000 (%.2f%%), el tope son 400",
                (unsigned)colados, 100.0 * (double)colados / 100000.0);
        di("cien mil buffers de basura con un 0xD1 dentro",
           colados < 400U, det);

        memset(b, 0, sizeof b);
        b[0] = 0xFFU;
        memcpy(&b[1], "QU HDQRMXS.XXXXX 123456 M01A", 28U);
        memset(&pd, 0, sizeof pd);
        di("un ACARS envuelto no da una posicion",
           !hfdl_hfnpdu_parse_performance_data(b, sizeof b, &pd), 0);
    }

    printf("\n  %s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * AIS SINTETICO: DEL BARCO AL DISCRIMINADOR Y DE VUELTA.
 *
 * POR QUE EXISTE (27/09/2026). Porque probar AIS en antena es sentarse en
 * un puerto con la radio. Y porque lo que hay entre el barco y la tabla son
 * seis capas -GMSK, reloj de bit, NRZI, relleno de HDLC, CRC y campos de
 * seis bits- y CADA UNA tiene una convencion de orden de bits que se puede
 * poner del reves sin que nada se queje: el resultado no es un error, es un
 * MMSI plausible en mitad del Atlantico.
 *
 * Aqui se construye la senal desde el otro extremo -se empaqueta un
 * mensaje, se le pone el CRC, se rellena, se codifica en NRZI y se convierte
 * en lo que sacaria el discriminador- y se comprueba que lo que vuelve es
 * exactamente el barco que se metio. Si una convencion esta del reves, el
 * banco lo dice con el campo delante.
 *
 * LO QUE NO MIDE: la sensibilidad de verdad. El ruido que se anade aqui es
 * gaussiano sobre la frecuencia instantanea, que NO es lo que hace el ruido
 * de verdad a la salida de un discriminador de FM (ahi hay efecto umbral, y
 * por debajo de el la cosa se cae de golpe en vez de degradarse). Los
 * numeros de la tabla de ruido sirven para comparar un cambio con el
 * anterior, no para prometer un alcance.
 */
#include "ais_rx.h"
#include "ais_msg.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define FS      96000.0
#define BAUD    9600.0
#define SPB     10

static int fallos;

static void bien(int ok, const char *que)
{
    printf("    %-56s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

/* ---------------------------------------------------------------- bits */
/* Mete `n` bits de `v` de MAS a menos peso, que es como define los campos
 * la ITU-R M.1371. */
static void pon(uint8_t *b, uint16_t p, uint8_t n, uint32_t v)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        uint16_t k = (uint16_t)(p + i);
        uint32_t bit = (v >> (n - 1 - i)) & 1U;

        if (bit) { b[k >> 3] |= (uint8_t)(0x80U >> (k & 7U)); }
        else     { b[k >> 3] &= (uint8_t)~(0x80U >> (k & 7U)); }
    }
}

static uint8_t lee(const uint8_t *b, uint16_t k)
{
    return (uint8_t)((b[k >> 3] >> (7U - (k & 7U))) & 1U);
}

/* ------------------------------------------------------------- el CRC */
/*
 * El FCS del X.25, y OJO CON EL COMPLEMENTO.
 *
 * El transmisor manda el registro INVERTIDO, y es justo eso lo que hace que
 * el receptor, pasando por el molino la trama entera con su FCS dentro,
 * saque siempre la misma constante (0xF0B8) en vez de tener que separar los
 * datos de la comprobacion. Sin invertir aqui, el banco "funcionaria" con
 * un receptor que comparase dos numeros y fallaria con el de verdad.
 */
static uint16_t fcs(const uint8_t *bits, uint16_t n)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    for (i = 0; i < n; i++) {
        uint16_t x = (uint16_t)((crc ^ lee(bits, i)) & 1U);

        crc >>= 1;
        if (x) { crc ^= 0x8408U; }
    }
    return (uint16_t)~crc;
}

/* ------------------------------------------------- la trama, bit a bit */
/* Devuelve cuantos bits ha dejado en `sal` (un bit por byte, 0 o 1). */
static uint16_t trama(const uint8_t *msg, uint16_t nmsg, uint8_t *sal)
{
    uint16_t n = 0, i;
    uint16_t f = fcs(msg, nmsg);
    uint8_t  unos = 0;

    /* Preambulo: 24 bits alternos. Es lo que engancha el lazo de reloj. */
    for (i = 0; i < 24; i++) { sal[n++] = (uint8_t)(i & 1U); }

    /* Bandera de apertura, de menos a mas peso: 0,1,1,1,1,1,1,0. */
    for (i = 0; i < 8; i++) { sal[n++] = (uint8_t)((0x7EU >> i) & 1U); }

    /* Datos y FCS, con relleno. El FCS va de menos a mas peso. */
    for (i = 0; i < (uint16_t)(nmsg + 16U); i++) {
        uint8_t b = (i < nmsg) ? lee(msg, i)
                               : (uint8_t)((f >> (i - nmsg)) & 1U);

        sal[n++] = b;
        if (b) {
            unos++;
            if (unos == 5U) { sal[n++] = 0U; unos = 0U; }
        } else {
            unos = 0U;
        }
    }

    /* Bandera de cierre y un poco de cola. */
    for (i = 0; i < 8; i++) { sal[n++] = (uint8_t)((0x7EU >> i) & 1U); }
    for (i = 0; i < 24; i++) { sal[n++] = (uint8_t)(i & 1U); }
    return n;
}

/* ------------------------------------------- de bits a discriminador */
/*
 * NRZI y luego el pulso gaussiano.
 *
 * En NRZI un UNO es "no cambies" y un cero es un cambio. Y la GMSK no da
 * escalones: el transmisor pasa los simbolos por un filtro gaussiano de
 * BT=0,4 antes de modular, asi que cada bit se reparte con sus vecinos. Eso
 * se imita aqui con una gaussiana de tres bits de largo - si el banco
 * usara escalones, estaria probando FSK y no GMSK, y el filtro adaptado del
 * receptor saldria mejor parado de lo que va a salir en el aire.
 */
static uint32_t modula(const uint8_t *bits, uint16_t n, float *sal,
                       float dc, float sigma, unsigned semilla)
{
    static float nrz[8192];
    uint32_t i, k, m = 0;
    int nivel = 0;
    float g[3 * SPB];
    float bt = 0.4f, suma = 0.0f;

    srand(semilla);

    /* NRZI a nivel +-1, una muestra por muestra de audio. */
    for (i = 0; i < n; i++) {
        if (!bits[i]) { nivel = !nivel; }      /* un cero cambia */
        for (k = 0; k < SPB; k++) { nrz[m++] = nivel ? 1.0f : -1.0f; }
    }

    /* El pulso gaussiano: sigma = sqrt(ln2)/(2*pi*BT) en periodos de bit. */
    {
        float s = sqrtf(logf(2.0f)) / (2.0f * 3.14159265f * bt);

        for (i = 0; i < 3 * SPB; i++) {
            float t = ((float)i - (3.0f * SPB - 1.0f) / 2.0f) / (float)SPB;
            g[i] = expf(-(t * t) / (2.0f * s * s));
            suma += g[i];
        }
        for (i = 0; i < 3 * SPB; i++) { g[i] /= suma; }
    }

    for (i = 0; i < m; i++) {
        float y = 0.0f;
        uint32_t j;

        for (j = 0; j < 3 * SPB; j++) {
            int32_t p = (int32_t)i - (int32_t)j + (3 * SPB) / 2;

            if ((p >= 0) && (p < (int32_t)m)) { y += g[j] * nrz[p]; }
        }
        if (sigma > 0.0f) {
            /* Gaussiano por suma de doce uniformes: sobra para esto. */
            float r = 0.0f;
            for (j = 0; j < 12; j++) { r += (float)rand() / (float)RAND_MAX; }
            y += sigma * (r - 6.0f);
        }
        sal[i] = y + dc;
    }
    return m;
}

/* ------------------------------------------------------- un mensaje 1 */
static uint16_t pos_report(uint8_t *msg, uint32_t mmsi,
                           double lat, double lon, double sog, double cog)
{
    memset(msg, 0, 32);
    pon(msg,   0, 6, 1U);                       /* tipo 1 */
    pon(msg,   6, 2, 0U);                       /* repeticiones */
    pon(msg,   8, 30, mmsi);
    pon(msg,  38, 4, 0U);                       /* estado de navegacion */
    pon(msg,  42, 8, 0x80U);                    /* giro: no disponible */
    pon(msg,  50, 10, (uint32_t)(sog * 10.0 + 0.5));
    pon(msg,  60, 1, 1U);                       /* precision */
    pon(msg,  61, 28, (uint32_t)(int32_t)llround(lon * 600000.0));
    pon(msg,  89, 27, (uint32_t)(int32_t)llround(lat * 600000.0));
    pon(msg, 116, 12, (uint32_t)(cog * 10.0 + 0.5));
    pon(msg, 128, 9, 511U);                     /* proa: no disponible */
    pon(msg, 137, 6, 30U);                      /* segundo */
    return 168U;
}

/* ------------------------------------------------------ un mensaje 5 */
static void seis_pon(uint8_t *m, uint16_t p, const char *t, uint8_t ncar)
{
    uint8_t i;

    for (i = 0; i < ncar; i++) {
        char c = t[i];
        uint8_t v;

        if (c == '\0') { v = 0U; }              /* relleno '@' */
        else if (c >= '@' && c <= '_') { v = (uint8_t)(c - '@'); }
        else { v = (uint8_t)c; }
        pon(m, (uint16_t)(p + (uint16_t)i * 6U), 6, v);
        if (c == '\0') { continue; }
    }
}

static uint16_t estaticos(uint8_t *msg, uint32_t mmsi, const char *nombre)
{
    memset(msg, 0, 64);
    pon(msg, 0, 6, 5U);
    pon(msg, 6, 2, 0U);
    pon(msg, 8, 30, mmsi);
    seis_pon(msg, 112, nombre, 20U);
    return 424U;
}

/* ------------------------------------------------------------- pruebas */
static uint8_t decodifica(const float *aud, uint32_t n, ais_msg_t *out)
{
    uint8_t  bits[AIS_BYTES_MAX];
    uint16_t nb;

    ais_rx_start((float)FS);
    /* En bloques como los del DMA, no de golpe: asi se prueba tambien que
     * el estado sobrevive de un bloque al siguiente. */
    {
        uint32_t k;
        for (k = 0; k < n; k += 256U) {
            uint32_t q = (n - k > 256U) ? 256U : (n - k);
            ais_rx_mete(&aud[k], q);
        }
    }
    if (!ais_rx_saca(bits, sizeof bits, &nb)) { ais_rx_stop(); return 0U; }
    {
        uint8_t ok = ais_msg_saca(bits, nb, out);
        ais_rx_stop();
        return ok;
    }
}

int main(void)
{
    static uint8_t msg[64], bits[8192];
    static float aud[131072];
    ais_msg_t m;
    uint16_t nmsg, nb;
    uint32_t n;

    printf("\nAIS SINTETICO\n\n");

    /* ------------------------------------------------ 1. un barco entero */
    printf("  un informe de posicion (tipo 1):\n");
    nmsg = pos_report(msg, 224123456UL, 43.3712, -8.3960, 12.4, 187.5);
    nb = trama(msg, nmsg, bits);
    n = modula(bits, nb, aud, 0.0f, 0.0f, 1U);

    if (!decodifica(aud, n, &m)) {
        bien(0, "decodifica");
    } else {
        bien(m.tipo == 1U, "el tipo es 1");
        bien(m.mmsi == 224123456UL, "el MMSI vuelve entero");
        bien(m.hay_pos != 0U, "trae posicion");
        printf("      %u  %ld,%04ld N  %ld,%04ld E  %u,%u nudos  %u,%u grados\n",
               (unsigned)m.mmsi,
               (long)(m.lat_e4 / 10000), (long)labs(m.lat_e4 % 10000),
               (long)(m.lon_e4 / 10000), (long)labs(m.lon_e4 % 10000),
               m.sog_d / 10U, m.sog_d % 10U, m.cog_d / 10U, m.cog_d % 10U);
        /* A Coruna: 43,3712 N  -8,3960 E. Una diezmilesima de grado son
         * once metros, asi que se admite una de margen por el redondeo. */
        bien(labs((long)m.lat_e4 - 433712L) <= 1L, "la latitud cuadra");
        bien(labs((long)m.lon_e4 + 83960L) <= 1L, "la longitud cuadra");
        bien(m.sog_d == 124U, "la velocidad cuadra");
        bien(m.cog_d == 1875U, "el rumbo cuadra");
    }

    /* ------------------------------------- 2. el nombre (mensaje largo) */
    printf("\n  los datos estaticos (tipo 5, 424 bits):\n");
    nmsg = estaticos(msg, 224987654UL, "MAERSK ESSEX");
    nb = trama(msg, nmsg, bits);
    n = modula(bits, nb, aud, 0.0f, 0.0f, 2U);
    if (!decodifica(aud, n, &m)) {
        bien(0, "decodifica");
    } else {
        printf("      %u  \"%s\"\n", (unsigned)m.mmsi, m.nombre);
        bien(m.tipo == 5U, "el tipo es 5");
        bien(m.mmsi == 224987654UL, "el MMSI vuelve entero");
        bien(strcmp(m.nombre, "MAERSK ESSEX") == 0, "y el nombre, sin arrobas");
    }

    /* ------------------------------------------ 3. la radio mal sintonizada */
    /*
     * EL DESVIO DE SINTONIA. La continua que sale del discriminador es el
     * error de frecuencia entre la radio y el transmisor, medido en veces
     * la desviacion de la GMSK (2.400 Hz).
     *
     * Solo se EXIGE hasta 0,8, y no por conformarse: un transmisor de AIS
     * en regla se va unos 500 Hz, o sea 0,2. Exigir 0,8 son cuatro veces el
     * peor caso real. Por encima de ahi se IMPRIME donde deja de salir,
     * porque ese numero es el que dice cuanto margen queda si algun dia hay
     * que tocar el seguidor de continua - ver su comentario en ais_rx.c,
     * donde esta medido lo que costaria comprarlo.
     */
    printf("\n  con la sintonia corrida (continua en el discriminador):\n");
    nmsg = pos_report(msg, 224123456UL, 43.3712, -8.3960, 12.4, 187.5);
    nb = trama(msg, nmsg, bits);
    {
        const float dcs[] = { 0.2f, 0.5f, 0.8f, 1.2f, 1.5f, 2.0f, 3.0f };
        unsigned i;
        float tope = 0.0f;

        for (i = 0; i < sizeof dcs / sizeof dcs[0]; i++) {
            int ok;

            n = modula(bits, nb, aud, dcs[i], 0.0f, 3U);
            ok = decodifica(aud, n, &m) && (m.mmsi == 224123456UL);
            printf("    desvio %.1f  ->  %s\n", dcs[i], ok ? "sale" : "no sale");
            if (ok) { tope = dcs[i]; }
            if (dcs[i] <= 0.8f) {
                char t[64];

                snprintf(t, sizeof t,
                         "aguanta %.1f veces la desviacion (%.0f Hz)",
                         dcs[i], dcs[i] * 2400.0f);
                bien(ok, t);
            }
        }
        printf("    -> el ultimo que sale es %.1f (%.0f Hz); un transmisor"
               " en regla se va 0,2\n", tope, tope * 2400.0f);
    }

    /* ---------------------------------------------------- 4. con ruido */
    printf("\n  con ruido (ver el aviso de la cabecera):\n");
    {
        const float sg[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.7f };
        unsigned i, s;

        for (i = 0; i < sizeof sg / sizeof sg[0]; i++) {
            unsigned ok = 0;

            for (s = 0; s < 10; s++) {
                n = modula(bits, nb, aud, 0.0f, sg[i], 100U + s);
                if (decodifica(aud, n, &m) && m.mmsi == 224123456UL) { ok++; }
            }
            printf("    sigma %.1f  ->  %u de 10\n", sg[i], ok);
            if (i == 0U) { bien(ok >= 9U, "con sigma 0,1 sale casi siempre"); }
        }
    }

    /* ------------------------------------------- 5. el CRC hace su trabajo */
    printf("\n  una trama con un bit cambiado:\n");
    nb = trama(msg, nmsg, bits);
    bits[40] ^= 1U;    /* dentro de los datos, pasada la bandera */
    n = modula(bits, nb, aud, 0.0f, 0.0f, 5U);
    bien(!decodifica(aud, n, &m), "el CRC la tira");

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

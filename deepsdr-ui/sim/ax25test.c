/*
 * Banco del AX.25 / APRS.
 *
 * Compila User/ax25.c DEL FIRMWARE y construye las tramas con SU MISMA
 * comprobacion (ax25_fcs()): si el banco tuviera su propio CRC, una trama
 * mal comprobada y un decodificador mal comprobado se cancelarian y el banco
 * diria que todo va bien.
 */
#include "ax25.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define FS 48000.0

/*
 * EL TRANSMISOR, PARAMETRIZADO - 07/10/2026.
 *
 * Hasta hoy este banco solo sabia emitir Bell 202 a 1200 baudios y 48 kHz,
 * porque era el unico caso que habia. Con el paquete de HF (300 baudios,
 * 200 Hz de desplazamiento, 12 kHz) el MISMO ax25.c atiende dos modos, y
 * un banco que solo prueba uno de los dos no esta probando el modulo: esta
 * probando la mitad que ya funcionaba.
 *
 * Los tres numeros salen a variables globales en vez de a parametros para
 * no reescribir la firma de las seis funciones del transmisor, que es
 * donde se cuelan los errores al tocar un banco que ya pasaba.
 */
static double g_fs    = FS;
static double g_baud  = AX25_BAUD;
static double g_marca = AX25_MARCA_HZ;
static double g_espacio = AX25_ESPACIO_HZ;

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* --------------------------------------------------------- montar tramas */

/* Una direccion AX.25: seis caracteres corridos un bit y el SSID. */
static void dir(uint8_t *d, const char *ind, int ssid, int ultima)
{
    int i, fin = 0;
    /* Se rellena con espacios a partir del final de la cadena, y se deja de
     * LEERLA ahi. La primera version hacia "ind[i] ? ind[i] : ' '", que con un
     * indicativo de menos de seis letras lee mas alla del final de la cadena:
     * el destino "APRS" salia como "APRSE" con una letra sacada de la memoria
     * de al lado. El fallo era del banco, no del decodificador. */
    for (i = 0; i < 6; i++) {
        char c;
        if (!fin && ind[i] == '\0') { fin = 1; }
        c = fin ? ' ' : ind[i];
        d[i] = (uint8_t)(c << 1);
    }
    d[6] = (uint8_t)(0x60 | ((ssid & 0x0F) << 1) | (ultima ? 1 : 0));
}

static uint16_t trama_ui(uint8_t *d, const char *orig, int so,
                         const char *dest, int sd,
                         const char *via, const char *info)
{
    uint16_t n = 0;
    uint16_t fcs;

    dir(&d[n], dest, sd, 0); n += 7;
    dir(&d[n], orig, so, via ? 0 : 1); n += 7;
    if (via) { dir(&d[n], via, 1, 1); n += 7; }
    d[n++] = 0x03;                 /* UI */
    d[n++] = 0xF0;                 /* sin capa 3 */
    while (*info) { d[n++] = (uint8_t)*info++; }

    fcs = ax25_fcs(d, n);
    /* Se transmite complementado y con el byte bajo primero: asi, al pasar la
     * trama entera por el molino, sale siempre la misma constante. */
    fcs = (uint16_t)(fcs ^ 0xFFFFU);
    d[n++] = (uint8_t)(fcs & 0xFFU);
    d[n++] = (uint8_t)(fcs >> 8);
    return n;
}

/* ------------------------------------------------------------- la señal */
typedef struct {
    double fase;
    double ppm;
    unsigned long semilla;
    double sigma;
    int    unos;       /* para el relleno */
    int    nrzi;       /* nivel actual */
    long   n;
    double t;
} tx_t;

static double ruido(tx_t *t)
{
    double a, b;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    a = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    b = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    return a + b;
}

static void emite_bit_nrzi(tx_t *t, int bit)
{
    long objetivo;
    double hz;

    /* NRZI: un CERO cambia de tono, un UNO se queda. */
    if (!bit) { t->nrzi = !t->nrzi; }
    hz = t->nrzi ? g_espacio : g_marca;

    t->t += 1.0 / g_baud;
    objetivo = (long)(t->t * g_fs * (1.0 - t->ppm * 1e-6) + 0.5);
    while (t->n < objetivo) {
        static float buf[64];
        static int j = 0;
        t->fase += 2.0 * M_PI * hz / g_fs;
        if (t->fase > 2.0 * M_PI) { t->fase -= 2.0 * M_PI; }
        buf[j++] = (float)(sin(t->fase) + t->sigma * ruido(t));
        t->n++;
        if (j == 64) { ax25_process(buf, 64U); j = 0; }
    }
}

/* Un bit de datos, con el relleno que manda HDLC. */
static void emite_dato(tx_t *t, int bit)
{
    emite_bit_nrzi(t, bit);
    if (bit) {
        t->unos++;
        if (t->unos == 5) { emite_bit_nrzi(t, 0); t->unos = 0; }
    } else {
        t->unos = 0;
    }
}

static void emite_bandera(tx_t *t)
{
    int i;
    emite_bit_nrzi(t, 0);
    for (i = 0; i < 6; i++) { emite_bit_nrzi(t, 1); }
    emite_bit_nrzi(t, 0);
    t->unos = 0;
}

static void emite_trama(tx_t *t, const uint8_t *d, uint16_t n, int banderas)
{
    int i; uint16_t k;
    for (i = 0; i < banderas; i++) { emite_bandera(t); }
    t->unos = 0;
    for (k = 0; k < n; k++) {
        int b;
        for (b = 0; b < 8; b++) { emite_dato(t, (d[k] >> b) & 1); }
    }
    for (i = 0; i < 3; i++) { emite_bandera(t); }
}

static char g_txt[4096];
static int  g_n;

static void recoge(void)
{
    char c;
    while (ax25_get_char(&c)) { if (g_n < 4000) { g_txt[g_n++] = c; } }
    g_txt[g_n] = '\0';
}

int main(void)
{
    static uint8_t d[AX25_TRAMA_MAX];
    uint16_t n;

    printf("banco del AX.25\n");

    printf("\n1. la comprobacion de trama\n");
    {
        n = trama_ui(d, "EA4HEW", 9, "APRS", 0, "WIDE1",
                     "!4025.12N/00341.45W>Prueba");
        printf("    trama de %u bytes\n", n);
        comprueba(ax25_fcs(d, n) == 0xF0B8U,
                  "la trama entera, comprobacion incluida, da la constante");
        d[5] ^= 0x02U;
        comprueba(ax25_fcs(d, n) != 0xF0B8U, "y con un bit cambiado, ya no");
        d[5] ^= 0x02U;
    }

    printf("\n2. el formateo del texto (sin pasar por la radio)\n");
    {
        ax25_start((float)FS);
        g_n = 0;
        ax25_trama_dbg(d, n);
        recoge();
        printf("    %s", g_txt);
        comprueba(strncmp(g_txt, "EA4HEW-9>APRS,WIDE1-1:", 22) == 0,
                  "origen, destino, repetidor y SSID salen en su sitio");
        comprueba(strstr(g_txt, "!4025.12N/00341.45W>Prueba") != NULL,
                  "y los datos salen enteros");
        ax25_stop();
    }

    printf("\n3. una trama por la antena, señal limpia\n");
    {
        tx_t t; ax25_info_t in;
        memset(&t, 0, sizeof t); t.semilla = 7UL; t.sigma = 0.01;
        ax25_start((float)FS);
        g_n = 0;
        emite_trama(&t, d, n, 30);
        recoge();
        ax25_info(&in);
        printf("    %s", g_txt);
        printf("    banderas %u, tramas buenas %u, malas %u\n",
               in.banderas, in.tramas_ok, in.tramas_mal);
        comprueba(in.tramas_ok == 1U, "llega una trama y pasa la comprobacion");
        comprueba(strstr(g_txt, "EA4HEW-9>APRS,WIDE1-1:") != NULL,
                  "y el texto es el que se mando");
        ax25_stop();
    }

    printf("\n4. varias tramas seguidas\n");
    {
        tx_t t; ax25_info_t in;
        uint16_t n2;
        static uint8_t d2[AX25_TRAMA_MAX];
        int i;

        memset(&t, 0, sizeof t); t.semilla = 99UL; t.sigma = 0.01;
        ax25_start((float)FS);
        g_n = 0;
        for (i = 0; i < 4; i++) {
            n2 = trama_ui(d2, "EB1XYZ", i, "APDR16", 0, 0,
                          "=4133.00N/00042.00E-Movil");
            emite_trama(&t, d2, n2, 20);
            ax25_info(&in);
            { float ppm, niv; ax25_lazo_dbg(&ppm, &niv);
              printf("      tras la trama %d: buenas %u malas %u  (lazo %+.0f ppm)\n",
                     i + 1, in.tramas_ok, in.tramas_mal, (double)ppm); }
        }
        recoge();
        ax25_info(&in);
        ax25_stop();
        printf("    buenas %u de 4, malas %u\n", in.tramas_ok, in.tramas_mal);
        comprueba(in.tramas_ok == 4U, "las cuatro salen");
    }

    printf("\n5. con error de reloj del transmisor\n");
    printf("    (un 2%% son 20.000 ppm, muchisimo mas de lo que se desvia ningun\n"
           "     equipo real: el lazo no estima la velocidad, solo persigue el\n"
           "     borde en cada cruce, y por eso le da igual)\n");
    {
        static const double k_ppm[] = { -20000.0, -5000.0, 0.0, 5000.0, 20000.0 };
        unsigned i;
        for (i = 0; i < sizeof k_ppm / sizeof k_ppm[0]; i++) {
            tx_t t; ax25_info_t in;
            memset(&t, 0, sizeof t); t.semilla = 5UL; t.sigma = 0.01;
            t.ppm = k_ppm[i];
            ax25_start((float)FS);
            emite_trama(&t, d, n, 30);
            ax25_info(&in);
            ax25_stop();
            printf("    %+6.0f ppm: buenas %u  malas %u\n",
                   k_ppm[i], in.tramas_ok, in.tramas_mal);
            comprueba(in.tramas_ok == 1U, "el lazo de reloj sigue al transmisor");
        }
    }

    printf("\n6. con ruido\n");
    printf("    (la señal es un seno de amplitud 1 y el ruido va en multiplos\n"
           "     de eso. Aqui no hay medias tintas: una trama con un solo bit\n"
           "     mal no pasa la comprobacion, asi que o sale entera o no sale.)\n");
    {
        static const double k_sig[] = { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 };
        unsigned i;
        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            tx_t t; ax25_info_t in;
            int j, buenas = 0;
            for (j = 0; j < 8; j++) {
                memset(&t, 0, sizeof t); t.semilla = 11UL + (unsigned long)j * 977UL;
                t.sigma = k_sig[i];
                ax25_start((float)FS);
                emite_trama(&t, d, n, 30);
                ax25_info(&in);
                ax25_stop();
                buenas += (in.tramas_ok > 0U);
            }
            printf("    ruido %4.2f:  %d de 8 tramas\n", k_sig[i], buenas);
            if (k_sig[i] <= 2.0) {
                comprueba(buenas == 8, "hasta el doble de la señal no se pierde ninguna");
            }
        }
    }

    printf("\n7. paquete de HF: 300 baudios, 200 Hz, por banda lateral\n");
    printf("    El MISMO ax25.c con otros tres numeros (ver PKT_HF_* en\n");
    printf("    ax25.h). Lo que aqui se mide no es si decodifica -eso ya se\n");
    printf("    sabe, es el mismo codigo- sino CUANTA SENSIBILIDAD CUESTA\n");
    printf("    que los dos tonos dejen de ser ortogonales:\n\n");
    printf("      Bell 202   desplazamiento 1000 Hz / 1200 baudios = 0,83\n");
    printf("      paquete HF desplazamiento  200 Hz /  300 baudios = 0,67\n\n");
    printf("    Por debajo de 1 los dos correladores se pisan, y el de HF se\n");
    printf("    pisa mas. El numero de abajo dice cuanto.\n\n");
    {
        uint8_t d[AX25_TRAMA_MAX];
        uint16_t n = trama_ui(d, "EA4ABC", 0, "CQ", 0, NULL, "prueba de paquete en onda corta");
        static const double k_sig[] = { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 };
        double limite_hf = 0.0;
        unsigned i;

        g_fs = PKT_HF_FS_HZ; g_baud = PKT_HF_BAUD;
        g_marca = PKT_HF_MARCA_HZ; g_espacio = PKT_HF_ESPACIO_HZ;

        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            tx_t t; ax25_info_t in;
            int j, buenas = 0;
            for (j = 0; j < 8; j++) {
                memset(&t, 0, sizeof t); t.semilla = 11UL + (unsigned long)j * 977UL;
                t.sigma = k_sig[i];
                ax25_start_fsk((float)g_fs, (float)g_baud,
                               (float)g_marca, (float)g_espacio);
                emite_trama(&t, d, n, 30);
                ax25_info(&in);
                ax25_stop();
                buenas += (in.tramas_ok > 0U);
            }
            printf("    ruido %4.2f:  %d de 8 tramas\n", k_sig[i], buenas);
            if (buenas == 8) { limite_hf = k_sig[i]; }
        }
        printf("\n    aguanta hasta ruido %.2f con las ocho tramas\n", limite_hf);
        /*
         * EL LISTON. Bell 202 aguanta 2,00 en la prueba 6. Aqui se exige
         * 1,00, o sea la mitad: los tonos se pisan mas y eso se paga, pero
         * si se pagase MAS de la mitad el detector de dos correladores no
         * seria el adecuado para este modo y habria que cambiarlo, no
         * bajar el liston. Esto esta aqui para enterarse de eso, no para
         * pasar.
         */
        comprueba(limite_hf >= 1.0,
                  "el paquete de HF aguanta al menos la mitad que el de VHF");

        /* Y que la velocidad NO se quede pegada del caso anterior: si
         * ax25_start_fsk() se olvidara de alguno de los tres numeros, la
         * prueba de arriba podria pasar por casualidad con los de Bell 202
         * y nadie se enteraria. Se vuelve al modo de VHF y se comprueba
         * que sigue saliendo. */
        {
            tx_t t; ax25_info_t in;
            uint8_t d2[AX25_TRAMA_MAX];
            uint16_t n2 = trama_ui(d2, "EA4ABC", 0, "APRS", 0, NULL, "vuelta al modo de VHF");
            g_fs = FS; g_baud = AX25_BAUD;
            g_marca = AX25_MARCA_HZ; g_espacio = AX25_ESPACIO_HZ;
            memset(&t, 0, sizeof t); t.semilla = 4242UL;
            ax25_start((float)FS);
            emite_trama(&t, d2, n2, 20);
            ax25_info(&in);
            ax25_stop();
            comprueba(in.tramas_ok > 0U,
                      "y volver al de VHF lo deja como estaba");
        }
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

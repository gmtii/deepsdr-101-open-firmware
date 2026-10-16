/*
 * JTTY EN EL AIRE: del audio al texto, con ruido de verdad.
 *
 * Pedido por el usuario.
 *
 * LO QUE MIDE ESTO, Y POR QUE NO VALE NADA MENOS
 * -----------------------------------------------
 * jtty_fec.c y jtty_msg.c ya estaban comprobados contra el Fortran de
 * WSJT-X y contra los diez vectores normativos. Lo que NO estaba
 * comprobado es que alguien sepa sacarles tonos de un audio, que es donde
 * estan todas las decisiones que pueden salir mal en silencio: la
 * frecuencia de mezcla, el diezmado, donde caen los bines, en que instante
 * empieza un simbolo y como se encuentra una trama que no avisa.
 *
 * Asi que aqui se GENERA la señal -modulador 4-GFSK completo, con su
 * filtro gaussiano y su fase continua-, se le suma ruido blanco medido, y
 * se le da al receptor de verdad, el mismo jtty_rx.c que corre en la
 * radio. Si sale el texto que entro, la cadena entera esta bien.
 *
 * Y SE PRUEBA LO QUE DE VERDAD DIFERENCIA A ESTE MODO: que la emision
 * empieza cuando le da la gana. Cada prueba mete un numero DISTINTO de
 * muestras de silencio por delante -y ninguno multiplo del simbolo- para
 * que el receptor tenga que encontrar el arranque el solo. Un banco que
 * empezara siempre en la muestra cero probaria un receptor que en antena
 * no valdria para nada.
 */
#include "jtty_rx.h"
#include "jtty_fec.h"
#include "jtty_msg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* ---------------------------------------------------------- el aire */

#define FS        12000.0f
#define BAUD      31.25f
#define TONO_HZ   31.25f
#define CENTRO    1500.0f
#define MS_SIM    ((int)(FS / BAUD))    /* 384 */

/* Ruido gaussiano por Box-Muller, con semilla propia: que el banco no
 * dependa de que rand() sea el mismo en dos maquinas. */
static uint32_t s_sem = 12345u;
static float uni(void)
{
    s_sem = s_sem * 1103515245u + 12345u;
    return (float)((s_sem >> 8) & 0xFFFFFFu) / 16777216.0f;
}
static float gauss(void)
{
    float u1 = uni(), u2 = uni();
    if (u1 < 1e-7f) { u1 = 1e-7f; }
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318531f * u2);
}

/*
 * EL MODULADOR. 4-GFSK: la secuencia de tonos se pasa por un filtro
 * gaussiano y la fase se integra, que es lo que hace que no haya saltos.
 *
 * BT = 1,0 y el filtro abarca tres simbolos, que es lo que usa el modo. No
 * es un detalle estetico: sin el filtro, una FSK de fase continua a secas
 * reparte energia fuera de los cuatro bines y el receptor sale MEJOR
 * parado de lo que saldria en antena. Un banco que se lo pone facil al
 * codigo que prueba miente hacia arriba, que es la peor direccion.
 */
static float gauss_pulso(float t, float bt)
{
    /* Respuesta del filtro gaussiano a un pulso rectangular de un simbolo,
     * normalizada para que su suma valga 1 al integrarla. */
    float a = 2.0f * 3.14159265f * bt / sqrtf(logf(2.0f));
    return 0.5f * (erff(a * (t + 0.5f) / 1.41421356f)
                 - erff(a * (t - 0.5f) / 1.41421356f));
}

/* Donde se pone la emision dentro de la ventana de busqueda. */
static float g_desvio = 0.0f;

static int modula(const uint8_t *tonos, int n, float *out, int max,
                  int retardo, float amp)
{
    int i, m, k = 0;
    float fase = 0.0f;
    static float inst[64 * 384];

    if (retardo + n * MS_SIM > max) { return 0; }
    for (i = 0; i < retardo; i++) { out[k++] = 0.0f; }

    /* La frecuencia instantanea, simbolo a simbolo, suavizada. */
    for (i = 0; i < n * MS_SIM; i++) { inst[i] = 0.0f; }
    for (i = 0; i < n; i++) {
        /* Los tonos van centrados: -1,5 a +1,5 veces la separacion. */
        float f = ((float)tonos[i] - 1.5f) * TONO_HZ;

        for (m = 0; m < n * MS_SIM; m++) {
            float t = ((float)m - ((float)i + 0.5f) * (float)MS_SIM)
                      / (float)MS_SIM;
            if (t > -2.5f && t < 2.5f) { inst[m] += f * gauss_pulso(t, 1.0f); }
        }
    }
    for (m = 0; m < n * MS_SIM; m++) {
        fase += 6.28318531f * (CENTRO + g_desvio + inst[m]) / FS;
        if (fase > 6.28318531f) { fase -= 6.28318531f; }
        out[k++] = amp * cosf(fase);
    }
    return k;
}

/* Una trama entera: 13 de sincronismo y 46 de datos. */
static void trama(const uint8_t carga[JTTY_CARGA_BITS], uint8_t tonos[JTTY_TRAMA_SIM])
{
    uint8_t datos[JTTY_SIMBOLOS];
    uint8_t i;

    jtty_fec_codifica(carga, datos);
    for (i = 0U; i < (uint8_t)JTTY_SYNC_SIM; i++) { tonos[i] = k_jtty_sync[i]; }
    for (i = 0U; i < (uint8_t)JTTY_SIMBOLOS; i++) {
        tonos[JTTY_SYNC_SIM + i] = datos[i];
    }
}

static void bits_de(const char *txt, uint8_t carga[JTTY_CARGA_BITS])
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)JTTY_CARGA_BITS; i++) {
        carga[i] = (uint8_t)(txt[i] == '1');
    }
}

/* ---------------------------------------------------------- */

static jtty_rx_ram_t g_ram;
#define AUDIO_MAX (12000 * 6)
static float g_audio[AUDIO_MAX];

/*
 * Corre una emision entera por el receptor y devuelve cuantos atomos han
 * salido buenos; `sale` recibe el texto del primero.
 */
static int pasa(const uint8_t carga[JTTY_CARGA_BITS], int retardo,
                float snr_db, char *sale, int *hz)
{
    uint8_t tonos[JTTY_TRAMA_SIM];
    int n, i, buenos = 0;
    float amp;
    /*
     * El nivel: la señal es una senoide de amplitud `amp` -potencia
     * amp^2/2- y el ruido tiene varianza 1 en TODA la banda de 6 kHz. La
     * relacion que se declara aqui es la clasica de estos modos: en 2.500
     * Hz de ancho de banda.
     */
    amp = sqrtf(2.0f * powf(10.0f, snr_db / 10.0f) * 2500.0f / 6000.0f);

    trama(carga, tonos);
    memset(g_audio, 0, sizeof g_audio);
    n = modula(tonos, (int)JTTY_TRAMA_SIM, g_audio, AUDIO_MAX, retardo, amp);
    if (n == 0) { return -1; }
    /* Y un poco de cola, para que la trama acabe dentro del anillo. */
    n += 12000;
    if (n > AUDIO_MAX) { n = AUDIO_MAX; }
    for (i = 0; i < n; i++) { g_audio[i] += gauss(); }

    jtty_rx_start(&g_ram, FS);
    /* En bloques, como llega de verdad del diezmador. */
    for (i = 0; i + 64 <= n; i += 64) {
        jtty_rx_r_t r;

        jtty_rx_mete(&g_audio[i], 64U);
        while (jtty_rx_saca(&r)) {
            if (buenos == 0 && sale) { strcpy(sale, r.atomo.texto); }
            if (buenos == 0 && hz)   { *hz = r.hz; }
            buenos++;
        }
    }
    jtty_rx_stop();
    return buenos;
}

int main(void)
{
    uint8_t carga[JTTY_CARGA_BITS];
    char sale[48];
    int hz = 0, n;

    printf("\n  JTTY DEL AIRE AL TEXTO\n\n");
    printf("    %d muestras por simbolo a 12 kHz, %d simbolos por trama\n",
           MS_SIM, (int)JTTY_TRAMA_SIM);

    /* ---- 1. sin ruido, y empezando en una muestra cualquiera ---- */
    printf("\n  sin ruido, con la emision empezando donde le da la gana\n");
    bits_de("0000100110111101111000110101000001", carga);   /* "CQ K1ABC CQ" */
    {
        static const int k_ret[] = { 0, 137, 384, 1000, 4211 };
        unsigned r;
        int bien = 0, dobles = 0;

        for (r = 0U; r < sizeof k_ret / sizeof k_ret[0]; r++) {
            sale[0] = '\0';
            n = pasa(carga, k_ret[r], 40.0f, sale, &hz);
            printf("    retardo %5d muestras -> %d atomos, \"%s\" (%+d Hz)\n",
                   k_ret[r], n, sale, hz);
            if (n >= 1 && strcmp(sale, "CQ K1ABC CQ") == 0) { bien++; }
            if (n > 1) { dobles++; }
        }
        /* La primera version ponia exige(1 || fallos == 0, ...), que es una
         * condicion que no puede ser falsa: el banco imprimia "bien"
         * pasara lo que pasara. Aqui hay dos veredictos y los dos miden. */
        exige(bien == (int)(sizeof k_ret / sizeof k_ret[0]),
              "sale a la primera empiece la emision donde empiece");
        exige(dobles == 0,
              "y sale UNA vez, no dos: el sincronismo vale en dos filas");
    }

    /* ---- 2. los diez vectores normativos, por el aire ---- */
    printf("\n  los diez vectores normativos, modulados y demodulados\n");
    {
        static const struct { const char *bits, *esp; } k[] = {
            { "0000100110111101111000110101000001", "CQ K1ABC CQ"  },
            { "1000000000000001111011000000001000", "599 123"      },
            { "1000000000000001111011000000001001", "599 123"      },
            { "1000000000000110111010000000011001", "599 CA"       },
            { "0000001011011110010000110100101001", "599 05 NWT"   },
            { "0010000010110001011000000000101001", "1D EMA"       },
            { "1000000100111001000010110100111001", "599 156 1749" },
            { "0000000000000000000000000001001001", "AGN?"         },
            { "0001001010000110011000000001001001", "FN42"         },
            { "0100010011100101010101010110001101", "HELLO"        }
        };
        unsigned i, bien = 0U;

        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            bits_de(k[i].bits, carga);
            sale[0] = '\0';
            n = pasa(carga, 200 + (int)i * 61, 20.0f, sale, &hz);
            if (n >= 1 && strcmp(sale, k[i].esp) == 0) { bien++; }
            else { printf("      %-14s -> %d atomos, \"%s\"   MAL\n",
                          k[i].esp, n, sale); }
        }
        printf("      %u de %u\n", bien, (unsigned)(sizeof k / sizeof k[0]));
        exige(bien == sizeof k / sizeof k[0],
              "los diez salen del aire igual que entraron");
    }

    /* ---- 3. y en cualquier sitio de la ventana ---- */
    printf("\n  y donde sea dentro de la ventana de busqueda\n");
    {
        static const float k_des[] = { -180.0f, -100.0f, -33.0f, 0.0f,
                                        47.0f, 100.0f, 180.0f };
        unsigned i, bien = 0U;

        bits_de("0000100110111101111000110101000001", carga);
        for (i = 0U; i < sizeof k_des / sizeof k_des[0]; i++) {
            g_desvio = k_des[i];
            sale[0] = '\0';
            n = pasa(carga, 500 + (int)i * 53, 20.0f, sale, &hz);
            printf("      puesta en %+6.0f Hz -> %d atomos, dice %+4d Hz\n",
                   (double)k_des[i], n, hz);
            /*
             * Medio bin es 15,6 Hz, asi que el desvio que se puede decir
             * va en saltos de eso: 8 Hz de tolerancia es lo mejor que la
             * rejilla permite, y pedir menos seria pedirle al banco que
             * mintiera.
             */
            if (n >= 1 && strcmp(sale, "CQ K1ABC CQ") == 0
                && hz > (int)k_des[i] - 9 && hz < (int)k_des[i] + 9) { bien++; }
        }
        g_desvio = 0.0f;
        exige(bien == sizeof k_des / sizeof k_des[0],
              "la encuentra en toda la ventana y dice bien donde estaba");
    }

    /*
     * ---- 4. UNA TRAMA, UN ATOMO: los duplicados en frecuencia ----
     *
     * *** 28/09/2026, de una foto de la pantalla con la radio en antena:
     * "WA4MB WA4MB DE IK DE IK 3CHK 3CHK IK3CH IK3CHK". Cada atomo salia
     * DOS veces. ***
     *
     * La guarda de entonces miraba fila y bin, y se llevaba el duplicado en
     * el tiempo pero no el de FRECUENCIA: una señal que cae JUSTO ENTRE dos
     * medios bines puntua igual de bien en los dos, son dos bases
     * distintas, y la guarda no las relacionaba.
     *
     * Aqui se pone a proposito en los sitios peores -medio medio bin, que
     * son 7,8 Hz- y se exige UN atomo, ni cero ni dos. Con la guarda vieja
     * salen dos.
     */
    printf("\n  una trama tiene que dar UN atomo, no dos\n");
    {
        static const float k_mal[] = { 7.8f, -7.8f, 23.4f, -23.4f, 39.1f };
        unsigned i, bien = 0U;

        bits_de("0000100110111101111000110101000001", carga);
        for (i = 0U; i < sizeof k_mal / sizeof k_mal[0]; i++) {
            g_desvio = k_mal[i];
            sale[0] = '\0';
            n = pasa(carga, 400 + (int)i * 71, 30.0f, sale, &hz);
            printf("      entre dos bines (%+5.1f Hz) -> %d atomos\n",
                   (double)k_mal[i], n);
            if (n == 1) { bien++; }
        }
        g_desvio = 0.0f;
        exige(bien == sizeof k_mal / sizeof k_mal[0],
              "ni la que cae entre dos bines sale duplicada");
    }

    /* ---- 5. la sensibilidad ---- */
    printf("\n  hasta donde llega: 20 intentos por nivel\n");
    {
        static const float k_snr[] = { 0.0f, -5.0f, -10.0f, -13.0f, -15.0f, -17.0f, -20.0f };
        unsigned i;
        float peor_bueno = 99.0f;

        bits_de("0000100110111101111000110101000001", carga);
        for (i = 0U; i < sizeof k_snr / sizeof k_snr[0]; i++) {
            int rep, bien = 0;

            for (rep = 0; rep < 20; rep++) {
                sale[0] = '\0';
                if (pasa(carga, 300 + rep * 37, k_snr[i], sale, &hz) >= 1
                    && strcmp(sale, "CQ K1ABC CQ") == 0) { bien++; }
            }
            printf("      %6.1f dB  %2d de 20\n", (double)k_snr[i], bien);
            if (bien >= 10 && k_snr[i] < peor_bueno) { peor_bueno = k_snr[i]; }
        }
        printf("      el 50%% se mantiene hasta %.1f dB\n", (double)peor_bueno);
        exige(peor_bueno <= -10.0f,
              "aguanta al menos hasta -10 dB, como el modo promete");
    }

    /* ---- 4. y que NO invente con ruido solo ---- */
    printf("\n  con ruido y nada mas, no puede salir nada\n");
    {
        int i, salidos = 0;

        jtty_rx_start(&g_ram, FS);
        for (i = 0; i < AUDIO_MAX; i++) { g_audio[i] = gauss(); }
        for (i = 0; i + 64 <= AUDIO_MAX; i += 64) {
            jtty_rx_r_t r;

            jtty_rx_mete(&g_audio[i], 64U);
            while (jtty_rx_saca(&r)) { salidos++; }
        }
        printf("      %lu candidatos pasaron el sincronismo, %d llegaron a texto\n",
               (unsigned long)jtty_rx_probados(), salidos);
        jtty_rx_stop();
        /*
         * Que pasen candidatos es NORMAL y ademas hace falta: desde que el
         * umbral bajo a 0,45 el sincronismo se dispara unas cuatro veces
         * por segundo con ruido, y eso es lo que permite coger las tramas
         * debiles (ver JTTY_SYNC_UMBRAL). Quien de verdad decide es el CRC
         * de 12 bits mas el bit reservado mas las reglas de gramatica.
         *
         * Asi que lo que se exige aqui es lo unico que importa: que de seis
         * segundos de ruido puro no salga NI UN ATOMO. Medido aparte sobre
         * diez minutos de ruido: cero.
         */
        exige(salidos == 0, "seis segundos de ruido no dan ni un atomo");
        exige(jtty_rx_probados() > 0UL,
              "y que el sincronismo se dispare es lo que da la sensibilidad");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

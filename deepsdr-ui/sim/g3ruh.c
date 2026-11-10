/*
 * 9600 G3RUH: QUE EL CAMINO DIRECTO DECODIFIQUE DE VERDAD.
 *
 * POR QUE EXISTE - 09/11/2026.
 *
 * En 145,825 MHz hay mas de una cosa. La ISS repite a 1200 baudios con los
 * tonos de Bell 202, que es lo que este decodificador sabe hacer desde la
 * etapa 28. Varios satelites pequenos van a 9.600, y eso es otra forma de
 * onda: FSK directa con la banda base aleatorizada. No hay dos tonos que
 * correlar, asi que el detector de siempre no sirve - no es que decodifique
 * peor: es que no tiene nada que medir-.
 *
 * El camino directo que se le ha anadido a ax25.c no es un decodificador
 * nuevo: quita la continua, promedia un bit, corta por cero, deshace el
 * aleatorizador y le da los bits al MISMO NRZI, al MISMO HDLC y a la MISMA
 * comprobacion de siempre.
 *
 * ESTE BANCO FABRICA LA SENAL ENTERA, de la trama a las muestras del
 * discriminador, y exige que salga la trama que entro. No mide "se parece":
 * compara el texto decodificado con el que se puso.
 *
 * LO QUE MIDE, Y POR QUE CADA COSA:
 *
 *   1. Limpio: que la cadena entera cierre. Si esto falla, lo demas sobra.
 *   2. CON LA RADIO DESINTONIZADA. Es la prueba que de verdad importa de
 *      este camino: lo que sale del discriminador es la desviacion MAS el
 *      desvio de sintonia, asi que medio kilohercio fuera mueve la senal
 *      entera para un lado y un cortador de cero sin memoria decide siempre
 *      lo mismo. De ahi el seguidor de continua.
 *   3. Con ruido, subiendo hasta que deja de salir. No es un aprobado: es
 *      un numero con el que comparar el dia que alguien toque el lazo.
 *   4. Con el reloj del transmisor corrido. Los satelites no llevan un
 *      patron de rubidio y el efecto Doppler mueve el ritmo de bit.
 *   5. Y que los diecisiete primeros bits salgan mal y no importe, que es
 *      la propiedad entera del aleatorizador autosincronizante: se
 *      engancha solo, sin que nadie le diga en que estado esta.
 *
 * LO QUE NO MIDE, y hay que decirlo con todas las letras: SI ESTO FUNCIONA
 * EN ANTENA. Aqui la senal se fabrica limpia y se le echa ruido blanco. En
 * la radio de verdad hay que atravesar el filtro de canal -que tiene que
 * ser el ancho, el del AIS, porque el de NFM se come los flancos-, el
 * limitador, y el Doppler de un satelite que pasa. Un argumento sobre el
 * camino de radiofrecuencia no entra sin una medida en antena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ax25.h"

#define FS        96000.0f
#define BAUD      9600.0f
#define SPB       10          /* 96000 / 9600, exactas */

static int g_fallos;

static void bien(const char *que, int ok, const char *dato)
{
    printf("    %-50s %s", que, ok ? "bien" : "MAL ");
    if (dato) { printf("  %s", dato); }
    printf("\n");
    if (!ok) { g_fallos++; }
}

/* ---------------------------------------------------------------------
 * EL TRANSMISOR
 * --------------------------------------------------------------------- */

static void dir(uint8_t *d, const char *ind, int ssid, int ultima)
{
    int i, n = (int)strlen(ind);

    for (i = 0; i < 6; i++) {
        char c = (i < n) ? ind[i] : ' ';
        d[i] = (uint8_t)(((uint8_t)c) << 1);
    }
    d[6] = (uint8_t)(0x60U | ((uint8_t)ssid << 1) | (ultima ? 1U : 0U));
}

static uint16_t trama_ui(uint8_t *d, const char *orig, int so,
                         const char *dest, int sd, const char *txt)
{
    uint16_t n = 0, fcs;
    int i;

    dir(&d[n], dest, sd, 0); n = (uint16_t)(n + 7);
    dir(&d[n], orig, so, 1); n = (uint16_t)(n + 7);
    d[n++] = 0x03U;                      /* UI */
    d[n++] = 0xF0U;                      /* sin capa 3 */
    for (i = 0; txt[i] != '\0'; i++) { d[n++] = (uint8_t)txt[i]; }
    fcs = (uint16_t)(ax25_fcs(d, n) ^ 0xFFFFU);
    d[n++] = (uint8_t)(fcs & 0xFFU);
    d[n++] = (uint8_t)(fcs >> 8);
    return n;
}

/*
 * EL ALEATORIZADOR DEL TRANSMISOR: x^17 + x^12 + 1, realimentado con su
 * PROPIA SALIDA. Esa es la mitad que hace que el receptor se enganche solo:
 * el de alla deshace la cuenta con los bits que le llegan, sin saber nada
 * del estado de este.
 */
typedef struct {
    uint32_t lfsr;
    int      nrzi;        /* nivel actual del NRZI */
    int      unos;        /* para el relleno de HDLC */
    float   *buf;
    int      n;
    int      max;
    double   fase;        /* para el error de reloj del transmisor */
    double   paso;
    float    amp;
    float    dc;          /* el desvio de sintonia, en unidades de nivel */
    float    ant;         /* el nivel que lleva, para redondear el flanco */
    float    suave;       /* 0,55 = FSK;  0,25 = GFSK, el flanco mas lento */
} tx_t;

static int scram(tx_t *t, int bit)
{
    int s = bit ^ (int)((t->lfsr >> 11) & 1U) ^ (int)((t->lfsr >> 16) & 1U);
    t->lfsr = (t->lfsr << 1) | (uint32_t)s;   /* realimenta la SALIDA */
    return s;
}

/* Un bit ya aleatorizado, a muestras del discriminador. El flanco no es
 * vertical: el filtro de canal de la radio redondea, asi que aqui se
 * redondea tambien -media muestra de subida- para no medir un caso que no
 * existe. */
static void emite_nivel(tx_t *t, int nivel)
{
    float obj = nivel ? t->amp : -t->amp;
    int i;

    for (i = 0; i < SPB; i++) {
        t->fase += t->paso;
        while (t->fase >= 1.0) {
            t->fase -= 1.0;
        }
        /*
         * EL FLANCO NO ES VERTICAL, y cuanto se redondea es el parametro
         * que separa las dos formas de onda que hay ahi arriba:
         *
         *   FSK  (KNACKSAT-2, 145,825)  el flanco lo redondea solo el
         *                               filtro de canal del receptor
         *   GFSK (Marina 145,925 y      el transmisor YA lo manda
         *         GRBBeta 145,935)      redondeado, con un filtro
         *                               gaussiano
         *
         * Un transitorio mas lento es un ojo mas cerrado para una media
         * movil de un bit, asi que la GFSK es el caso DIFICIL de los dos y
         * por eso se mide aparte.
         */
        t->ant += t->suave * (obj - t->ant);
        if (t->n < t->max) { t->buf[t->n++] = t->ant + t->dc; }
    }
}

static void emite_bit(tx_t *t, int bit)
{
    /* NRZI: un cero es un cambio de nivel, un uno es quedarse. */
    if (!bit) { t->nrzi = !t->nrzi; }
    emite_nivel(t, scram(t, t->nrzi));
}

static void emite_dato(tx_t *t, int bit)
{
    emite_bit(t, bit);
    if (bit) {
        t->unos++;
        if (t->unos == 5) { emite_bit(t, 0); t->unos = 0; }
    } else {
        t->unos = 0;
    }
}

static void emite_bandera(tx_t *t)
{
    int i;
    emite_bit(t, 0);
    for (i = 0; i < 6; i++) { emite_bit(t, 1); }
    emite_bit(t, 0);
    t->unos = 0;
}

static void emite_trama(tx_t *t, const uint8_t *d, uint16_t n, int banderas)
{
    int i, b;
    for (i = 0; i < banderas; i++) { emite_bandera(t); }
    for (i = 0; i < (int)n; i++) {
        for (b = 0; b < 8; b++) { emite_dato(t, (d[i] >> b) & 1); }
    }
    for (i = 0; i < 3; i++) { emite_bandera(t); }
}

/* ---------------------------------------------------------------------
 * EL RUIDO
 * --------------------------------------------------------------------- */

static uint32_t s_rnd;
static float uni(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)((s_rnd >> 8) & 0xFFFFFu) * (1.0f / 1048576.0f) - 0.5f;
}
static float gauss(void)
{
    /* Suma de doce uniformes: el teorema central del limite a mano, que es
     * de sobra para esto y no necesita ni logaritmo ni coseno. */
    int i; float s = 0.0f;
    for (i = 0; i < 12; i++) { s += uni(); }
    return s;
}

/* ---------------------------------------------------------------------
 * EL BANCO
 * --------------------------------------------------------------------- */

#define MAX_MUESTRAS  200000
static float g_audio[MAX_MUESTRAS];
static char  g_texto[4096];

/* Corre una tanda y devuelve cuantas tramas buenas han salido. */
static uint32_t corre_f(float ruido, float dc, double ppm, unsigned semilla,
                        float suave, const char *txt_esperado)
{
    uint8_t d[AX25_TRAMA_MAX];
    uint16_t n;
    tx_t t;
    ax25_info_t in;
    uint32_t i;
    int gi = 0;
    char c;

    memset(&t, 0, sizeof t);
    t.buf = g_audio; t.max = MAX_MUESTRAS;
    t.amp = 1.0f; t.dc = dc;
    t.suave = (suave > 0.0f) ? suave : 0.55f;
    t.paso = 1.0 + ppm * 1e-6;
    t.lfsr = 0x1A79Fu;          /* un estado cualquiera: el receptor no lo sabe */
    s_rnd = semilla;

    n = trama_ui(d, "EA4HEW", 0, "APRS", 0, txt_esperado);
    emite_trama(&t, d, n, 20);

    if (ruido > 0.0f) {
        for (i = 0; i < (uint32_t)t.n; i++) { g_audio[i] += ruido * gauss(); }
    }

    if (!ax25_start_g3ruh(FS)) { return 0U; }

    for (i = 0; i + 64U <= (uint32_t)t.n; i += 64U) {
        ax25_process(&g_audio[i], 64U);
    }

    g_texto[0] = '\0';
    while (ax25_get_char(&c) && gi < (int)sizeof g_texto - 1) {
        g_texto[gi++] = c;
    }
    g_texto[gi] = '\0';

    ax25_info(&in);
    ax25_stop();
    return in.tramas_ok;
}

/* El de siempre: FSK, flanco de FSK. */
static uint32_t corre(float ruido, float dc, double ppm, unsigned semilla,
                      const char *txt_esperado)
{
    return corre_f(ruido, dc, ppm, semilla, 0.55f, txt_esperado);
}

int main(void)
{
    char dato[160];

    printf("== 9600 G3RUH por el camino directo ==\n\n");
    printf("  %d muestras por bit a %.0f Hz, %.0f baudios\n\n",
           SPB, (double)FS, (double)BAUD);

    printf("  1. la cadena entera, sin ruido\n");
    {
        uint32_t ok = corre(0.0f, 0.0f, 0.0, 1u, "PRUEBA DE NUEVE MIL SEIS");
        sprintf(dato, "%u trama(s) buena(s)", ok);
        bien("sale la trama", ok >= 1U, dato);
        bien("y el texto es el que se puso",
             strstr(g_texto, "PRUEBA DE NUEVE MIL SEIS") != NULL,
             g_texto[0] ? g_texto : "(nada)");
    }

    printf("\n  2. con la radio desintonizada (lo que mata a un cortador\n");
    printf("     de cero sin memoria)\n");
    {
        static const float k_dc[] = { 0.2f, 0.5f, 0.9f, 1.5f };
        int i;
        for (i = 0; i < 4; i++) {
            uint32_t ok = corre(0.0f, k_dc[i], 0.0, 7u, "DESINTONIZADA");
            sprintf(dato, "continua %.1f de la desviacion -> %u trama(s)",
                    (double)k_dc[i], ok);
            bien("aguanta el desvio de sintonia", ok >= 1U, dato);
        }
    }

    printf("\n  3. con ruido, subiendo\n");
    {
        static const float k_r[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.7f, 1.0f };
        int i, ultimo_bueno = -1;
        for (i = 0; i < 7; i++) {
            int s, buenas = 0;
            for (s = 0; s < 8; s++) {
                if (corre(k_r[i], 0.0f, 0.0, (unsigned)(100 + s * 37),
                          "CON RUIDO") >= 1U) { buenas++; }
            }
            printf("      sigma %.1f de la desviacion   %d de 8\n",
                   (double)k_r[i], buenas);
            if (buenas == 8) { ultimo_bueno = i; }
        }
        sprintf(dato, "hasta sigma %.1f",
                (ultimo_bueno >= 0) ? (double)k_r[ultimo_bueno] : 0.0);
        /* 0,2 es un ojo ya bastante cerrado: lo que se exige es que no se
         * caiga antes de ahi, no un record. */
        bien("ocho de ocho al menos hasta sigma 0,2", ultimo_bueno >= 1, dato);
    }

    printf("\n  4. con el reloj del transmisor corrido (Doppler)\n");
    {
        static const double k_ppm[] = { -500.0, -200.0, 200.0, 500.0 };
        int i;
        for (i = 0; i < 4; i++) {
            uint32_t ok = corre(0.0f, 0.0f, k_ppm[i], 21u, "DOPPLER");
            sprintf(dato, "%+.0f ppm -> %u trama(s)", k_ppm[i], ok);
            bien("el lazo de reloj lo sigue", ok >= 1U, dato);
        }
    }

    printf("\n  5. el aleatorizador se engancha solo\n");
    {
        /* Tres estados iniciales distintos del transmisor. El receptor
         * arranca siempre a cero y no se entera: esa es la propiedad. */
        static const uint32_t k_est[] = { 0x00000u, 0x1FFFFu, 0x0AAAAu };
        int i;
        for (i = 0; i < 3; i++) {
            uint8_t d[AX25_TRAMA_MAX];
            uint16_t n;
            tx_t t;
            ax25_info_t in;
            uint32_t j;

            memset(&t, 0, sizeof t);
            t.buf = g_audio; t.max = MAX_MUESTRAS;
            t.amp = 1.0f; t.paso = 1.0; t.lfsr = k_est[i];
            t.suave = 0.55f;
            n = trama_ui(d, "EA4HEW", 0, "APRS", 0, "SIN SINCRONIZAR");
            emite_trama(&t, d, n, 20);

            (void)ax25_start_g3ruh(FS);
            for (j = 0; j + 64U <= (uint32_t)t.n; j += 64U) {
                ax25_process(&g_audio[j], 64U);
            }
            ax25_info(&in);
            ax25_stop();
            sprintf(dato, "estado 0x%05X -> %u trama(s)",
                    (unsigned)k_est[i], in.tramas_ok);
            bien("sale igual sin saber el estado del transmisor",
                 in.tramas_ok >= 1U, dato);
        }
    }

    printf("\n  6. GFSK, que es lo que mandan Marina y GRBBeta\n");
    {
        /*
         * HASTA DONDE AGUANTA, Y QUE SE EXIGE.
         *
         * El parametro es la constante del flanco: el tiempo de subida de
         * 10 a 90 % sale 2,2/suave muestras, y un bit son diez. O sea que
         * 0,55 es un flanco de 0,4 bits -FSK con el redondeo del receptor-
         * y 0,25 es de 0,9 bits, que es del orden de lo que reparte una
         * GFSK de verdad.
         *
         * Se EXIGE hasta 0,25 y se mide hasta donde llega. Por debajo de
         * ahi el flanco dura mas de un bit entero, que ya no es una GFSK:
         * es otra cosa. El numero no es un aprobado, es una frontera con
         * la que comparar el dia que alguien toque el lazo.
         */
        static const float k_s[] = { 0.55f, 0.40f, 0.30f, 0.25f, 0.20f, 0.15f };
        int i, ultimo = -1;
        for (i = 0; i < 6; i++) {
            uint32_t ok = corre_f(0.0f, 0.0f, 0.0, 31u, k_s[i], "GFSK");
            printf("      flanco %.2f  (%.1f bits de subida)   %u trama(s)\n",
                   (double)k_s[i], 2.2 / (double)k_s[i] / (double)SPB, ok);
            if (ok >= 1U) { ultimo = i; }
        }
        sprintf(dato, "aguanta hasta %.2f",
                (ultimo >= 0) ? (double)k_s[ultimo] : 0.0);
        bien("el flanco redondeado sale al menos hasta 0,25",
             ultimo >= 3, dato);
        /* Y lo de siempre junto: GFSK, desintonizado y con ruido. */
        {
            uint32_t ok = corre_f(0.2f, 0.6f, 200.0, 33u, 0.25f, "GFSK");
            sprintf(dato, "GFSK + continua + ruido + Doppler -> %u", ok);
            bien("y las cuatro cosas a la vez", ok >= 1U, dato);
        }
    }

    printf("\n  7. y a 48 kHz dice que NO, en vez de no decodificar nada\n");
    {
        uint8_t arranca = ax25_start_g3ruh(48000.0f);
        ax25_stop();
        sprintf(dato, "ax25_start_g3ruh(48000) devuelve %u", arranca);
        bien("se niega a arrancar con cinco muestras por bit",
             arranca == 0U, dato);
    }

    printf("\n");
    if (g_fallos) {
        printf("G3RUH: HAY FALLOS (%d)\n", g_fallos);
        return 1;
    }
    printf("TODO CORRECTO (0 fallos)\n");
    return 0;
}

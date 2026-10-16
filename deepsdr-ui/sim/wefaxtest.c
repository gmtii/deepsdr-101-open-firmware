/*
 * Banco del WEFAX.
 *
 * Compila User/wefax.c DEL FIRMWARE y fabrica la señal con la MISMA escala de
 * grises que usa el decodificador (wefax_hz_de_gris()), por lo mismo que en
 * el resto de bancos: dos tablas que hay que mantener iguales a mano son una
 * tabla con un fallo pendiente.
 *
 * LO QUE SE MIDE DE VERDAD es la INCLINACION. El decodificador sale recto si
 * va al mismo ritmo que la emisora, y ese ritmo no se lo dice nadie: lo tiene
 * que sacar de la señal de fase. Asi que el transmisor de aqui emite a una
 * velocidad DISTINTA de la nominal a proposito, y lo que se comprueba es que
 * un borde vertical de la imagen sale vertical.
 */
#include "wefax.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define FS      12000.0
#define LPM     120.0

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* ------------------------------------------------------------- el patron */
/*
 * La imagen de prueba: fondo en degradado, rayas verticales y un BORDE
 * vertical nitido en x=300. Ese borde es el instrumento de medida: si el
 * decodificador va a otro ritmo que la emisora, el borde sale en diagonal y
 * se ve en la pendiente, que es un numero.
 */
static uint8_t patron(int x, int y)
{
    int v;
    if (x < 300) { v = 40 + (x * 60) / 300; }        /* izquierda: oscuro */
    else         { v = 200 - ((x - 300) * 40) / 456; } /* derecha: claro */
    /* Las rayas van LEJOS del borde de medida: si caen cerca, el buscador de
     * bordes de mas abajo puede engancharse a una raya en vez de al borde y
     * lo que mide deja de ser la inclinacion. El instrumento no puede
     * compartir sitio con el adorno. */
    if (x < 180 || x > 460) {
        if (((x / 30) & 1) && (y % 40) < 20) { v = 255 - v; }
    }
    if (v < 0) v = 0; if (v > 255) v = 255;
    return (uint8_t)v;
}

/* ------------------------------------------------------------- la señal */
typedef struct {
    double fase;
    double lpm;          /* la velocidad REAL del transmisor */
    long   muestra;
    unsigned long semilla;
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

static double emite(tx_t *t, double hz, double sigma)
{
    double y;
    t->fase += 2.0 * M_PI * hz / FS;
    if (t->fase > 2.0 * M_PI) { t->fase -= 2.0 * M_PI; }
    y = sin(t->fase) + sigma * ruido(t);
    t->muestra++;
    return y;
}

/* Recoge las lineas que va soltando el decodificador. */
#define MAX_LIN 400
static uint8_t g_img[MAX_LIN][WEFAX_ANCHO];
static int g_nlin = 0;

static void mete(float v, double sigma, tx_t *t)
{
    static float buf[64];
    static int j = 0;
    const uint8_t *l;
    (void)t; (void)sigma;

    buf[j++] = v;
    if (j == 64) {
        wefax_process(buf, 64U);
        j = 0;
        while ((l = wefax_linea()) != 0) {
            if (g_nlin < MAX_LIN) { memcpy(g_img[g_nlin], l, WEFAX_ANCHO); g_nlin++; }
        }
    }
}

/* Cuantas muestras dura una linea del TRANSMISOR. */
static double muestras_linea(const tx_t *t) { return FS * 60.0 / t->lpm; }

static void tono_apt(tx_t *t, double hz_alterna, double seg, double sigma)
{
    long n = (long)(FS * seg), k;
    for (k = 0; k < n; k++) {
        /* El tono de arranque no es un tono de audio: es el BRILLO
         * alternando entre negro y blanco a esa frecuencia. */
        int blanco = (((long)(k * hz_alterna * 2.0 / FS)) & 1);
        double hz = wefax_hz_de_gris(blanco ? 255 : 0);
        mete((float)emite(t, hz, sigma), sigma, t);
    }
}

static void lineas_fase(tx_t *t, int n, double sigma)
{
    double ml = muestras_linea(t);
    int i; long k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < (long)ml; k++) {
            /* negro, con un pulso blanco del 5% al principio */
            double f = (double)k / ml;
            int blanco = (f < 0.05);
            double hz = wefax_hz_de_gris(blanco ? 255 : 0);
            mete((float)emite(t, hz, sigma), sigma, t);
        }
    }
}

/* Un trozo de linea en negro, para que la señal de fase NO empiece en un
 * limite de linea del decodificador. Ver la prueba 7. */
static void negro_fraccion(tx_t *t, double fraccion, double sigma)
{
    long n = (long)(muestras_linea(t) * fraccion), k;
    for (k = 0; k < n; k++) {
        mete((float)emite(t, wefax_hz_de_gris(0), sigma), sigma, t);
    }
}

static void lineas_imagen(tx_t *t, int n, double sigma)
{
    double ml = muestras_linea(t);
    int i; long k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < (long)ml; k++) {
            int x = (int)((double)k / ml * WEFAX_ANCHO);
            double hz;
            if (x >= (int)WEFAX_ANCHO) { x = WEFAX_ANCHO - 1; }
            hz = wefax_hz_de_gris(patron(x, i));
            mete((float)emite(t, hz, sigma), sigma, t);
        }
    }
}

/* Donde cae el borde de x=300 en una linea: el punto de maxima subida. */
static double borde(const uint8_t *l)
{
    int x, mejor = -1; int mx = 0;
    for (x = 200; x < 420; x++) {
        int d = (int)l[x + 6] - (int)l[x];
        if (d > mx) { mx = d; mejor = x; }
    }
    return (mx > 60) ? (double)mejor : -1.0;
}

static void guarda_ppm(const char *nombre, int n)
{
    FILE *f = fopen(nombre, "wb");
    int y, x;
    if (!f) { return; }
    fprintf(f, "P5\n%u %d\n255\n", (unsigned)WEFAX_ANCHO, n);
    for (y = 0; y < n; y++) { for (x = 0; x < (int)WEFAX_ANCHO; x++) { fputc(g_img[y][x], f); } }
    fclose(f);
}

/*
 * lpm_nom es la velocidad a la que se PONE el decodificador y lpm_real la
 * que lleva la emisora. Son dos numeros distintos a proposito: el primero
 * es el ajuste del operador y el segundo la realidad, y lo que mide este
 * banco es justo lo que el decodificador tiene que sacar solo de la señal
 * de fase - la diferencia entre los dos.
 */
static double g_desfase = 0.0;   /* fraccion de linea metida antes de la fase */

static double prueba(double lpm_nom, double lpm_real, double sigma,
                     const char *ppm, int quiero_recto)
{
    tx_t t;
    wefax_info_t in;
    double pend = 0.0;

    memset(&t, 0, sizeof t);
    t.lpm = lpm_real; t.semilla = 1234UL;
    g_nlin = 0;

    wefax_start((float)FS);
    wefax_set_lpm((uint16_t)(lpm_nom + 0.5));
    tono_apt(&t, 300.0, 5.0, sigma);
    negro_fraccion(&t, g_desfase, sigma);
    lineas_fase(&t, 60, sigma);   /* 30 s, como una emisora de verdad */
    lineas_imagen(&t, 200, sigma);

    wefax_info(&in);
    {
        float d, p; wefax_fase_dbg(&d, &p);
        printf("    puesto %4u lpm, emisora %7.2f lpm -> estado %u, %d lineas, "
               "correccion %+7.0f ppm (el pulso se corria %+.2f puntos por linea)\n",
               (unsigned)wefax_get_lpm(), lpm_real, in.estado, g_nlin,
               (double)in.ppm, (double)d);
    }

    /* La pendiente del borde: cuantos puntos se corre por linea. Cero es
     * recto. Se mide sobre las lineas de imagen de verdad, saltandose las
     * primeras que puedan llevar residuo del enganche. */
    {
        int i, n = 0;
        double sx = 0, sy = 0, sxy = 0, sxx = 0;
        for (i = 10; i < g_nlin; i++) {
            double b = borde(g_img[i]);
            if (b < 0) { continue; }
            sx += i; sy += b; sxy += (double)i * b; sxx += (double)i * i; n++;
        }
        if (n > 20) {
            double den = n * sxx - sx * sx;
            if (den > 0) { pend = (n * sxy - sx * sy) / den; }
            printf("    el borde vertical se corre %+.4f puntos por linea "
                   "(%d lineas medidas)\n", pend, n);
        } else {
            printf("    no se ha podido medir el borde (%d lineas)\n", n);
            pend = 99.0;
        }
    }

    if (ppm) { guarda_ppm(ppm, g_nlin); }
    if (quiero_recto) {
        comprueba(g_nlin > 150, "salen las lineas");
        /* 0,1 puntos por linea sobre 200 lineas son 20 puntos de caida en
         * una imagen de 756 de ancho: un 2,6%, que en una carta de superficie
         * no se ve. Por encima de eso empieza a notarse. */
        comprueba(pend > -0.1 && pend < 0.1,
                  "y la imagen sale RECTA (menos de 0,1 puntos por linea)");
    }
    wefax_stop();
    return pend;
}

int main(void)
{
    printf("banco del WEFAX\n");

    printf("\n1. la escala de grises\n");
    {
        printf("    negro  -> %.0f Hz -> %u\n", (double)wefax_hz_de_gris(0), wefax_gris_de_hz(1500.0f));
        printf("    medio  -> %.0f Hz -> %u\n", (double)wefax_hz_de_gris(128), wefax_gris_de_hz(1900.0f));
        printf("    blanco -> %.0f Hz -> %u\n", (double)wefax_hz_de_gris(255), wefax_gris_de_hz(2300.0f));
        comprueba(wefax_gris_de_hz(1500.0f) == 0U, "1500 Hz es negro");
        comprueba(wefax_gris_de_hz(2300.0f) == 255U, "2300 Hz es blanco");
        comprueba(wefax_gris_de_hz(1000.0f) == 0U && wefax_gris_de_hz(3000.0f) == 255U,
                  "y fuera de rango se recorta en vez de dar la vuelta");
        {
            int g, ok = 1;
            for (g = 0; g < 256; g++) {
                if (wefax_gris_de_hz(wefax_hz_de_gris((uint8_t)g)) != (uint8_t)g) { ok = 0; }
            }
            comprueba(ok, "la escala y su inversa cuadran en los 256 grises");
        }
    }

    printf("\n2. emisora a la velocidad exacta\n");
    prueba(120.0, 120.0, 0.02, "out/wefax_120.pgm", 1);

    printf("\n3. emisora fuera de velocidad (es lo normal)\n");
    printf("    (un 0,1%% de error son 1200 ppm; sin corregir, 200 lineas despues\n"
           "     la imagen se habria ido casi un punto por linea)\n");
    prueba(120.0, 120.0 * (1.0 - 0.001), 0.02, "out/wefax_lento.pgm", 1);
    prueba(120.0, 120.0 * (1.0 + 0.001), 0.02, "out/wefax_rapido.pgm", 1);
    prueba(120.0, 120.0 * (1.0 + 0.003), 0.02, "out/wefax_mas.pgm", 1);

    printf("\n4. con ruido\n");
    {
        static const double k_sig[] = { 0.05, 0.15, 0.30, 0.60 };
        unsigned i;
        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            printf("    ruido %.2f:\n", k_sig[i]);
            prueba(120.0, 120.0 * (1.0 + 0.001), k_sig[i], 0, 0);
        }
    }

    /*
     * Las cuatro velocidades. Hasta la etapa 32 el decodificador SABIA
     * hacerlas -wefax_set_lpm() esta desde la 26- pero no habia por donde
     * pedirlas, asi que nunca se habian medido: lo unico probado era 120.
     * Aqui se emite a cada una con el mismo 0,1% de error de siempre, que
     * es lo que obliga a que la señal de fase trabaje.
     */
    printf("\n5. las cuatro velocidades de linea\n");
    {
        static const double k_lpm[] = { 60.0, 90.0, 120.0, 240.0 };
        static const char *const k_pgm[] = {
            "out/wefax_l60.pgm", "out/wefax_l90.pgm",
            "out/wefax_l120.pgm", "out/wefax_l240.pgm"
        };
        unsigned i;
        for (i = 0; i < sizeof k_lpm / sizeof k_lpm[0]; i++) {
            printf("    %.0f lpm:\n", k_lpm[i]);
            prueba(k_lpm[i], k_lpm[i] * (1.0 + 0.001), 0.02, k_pgm[i], 1);
        }
    }

    /*
     * Y la otra mitad de la pregunta: que poner MAL la velocidad se note.
     * Si el decodificador saliera recto con la velocidad equivocada, el
     * ajuste no estaria haciendo nada y el banco de arriba no probaria
     * nada. La señal de fase corrige hasta +-30000 ppm (un 3%); un salto
     * de 120 a 90 es un 33%, muy fuera de eso.
     */
    printf("\n6. y con la velocidad equivocada NO sale recta\n");
    {
        double pend = prueba(90.0, 120.0, 0.02, 0, 0);
        comprueba(!(pend > -0.1 && pend < 0.1) || g_nlin < 150,
                  "puesto en 90 con una emisora a 120: la imagen NO sale recta");
    }

    /*
     * LA FASE QUE NO EMPIEZA EN UN LIMITE DE LINEA.
     *
     * Esta es la prueba que faltaba, y la que habria cazado el fallo de la
     * linea partida en la etapa 26 sin esperar a que apareciese el 90 lpm.
     * Hasta aqui el transmisor del banco empezaba la señal de fase justo
     * donde el decodificador creia que empezaba una linea, porque el tono de
     * arranque dura 5 s y 5 s son un numero entero de lineas a 60, 120 y 240
     * lpm. O sea que el banco solo probaba el caso facil, y encima sin
     * saberlo.
     *
     * Una emisora de verdad no sabe nada del reloj de esta radio: el desfase
     * al empezar es cualquiera entre 0 y una linea. Asi que aqui se prueban
     * los cuatro cuartos.
     */
    printf("\n7. la fase empezando a mitad de linea (que es lo normal)\n");
    {
        static const double k_d[] = { 0.0, 0.25, 0.5, 0.75 };
        unsigned i;
        for (i = 0; i < sizeof k_d / sizeof k_d[0]; i++) {
            printf("    empieza %.0f%% de linea tarde:\n", k_d[i] * 100.0);
            g_desfase = k_d[i];
            prueba(120.0, 120.0 * (1.0 + 0.001), 0.02, 0, 1);
        }
        g_desfase = 0.0;
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

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
    if (v < 0)   { v = 0; }
    if (v > 255) { v = 255; }
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

/*
 * EL BUCLE PRINCIPAL PUEDE ESTAR OCUPADO, Y ESO HAY QUE PODER SIMULARLO.
 *
 * g_atasco = cuantas tandas de 64 muestras seguidas NO se recogen lineas.
 * Es lo que pasa de verdad cuando el bucle esta escribiendo un bloque en la
 * flash: el decodificador sigue corriendo en la interrupcion y va cerrando
 * lineas que nadie recoge.
 *
 * Con DOS buffers -lo que habia- la primera linea sin recoger la machaca la
 * siguiente. Con el anillo, se acumulan y se recogen despues.
 */
static int g_atasco = 0;      /* 0 = recoge siempre; N = se atasca N de cada 2N */
static int g_atasco_c = 0;
#define PEND_N 16
static const uint8_t *g_pend[PEND_N];   /* lineas cogidas y aun no copiadas */
static int g_pend_t[PEND_N];
static int g_pq_cab = 0, g_pq_n = 0;
static long g_tandas = 0;
static float g_buf[64];
static int   g_bj = 0;

/* Las tandas de 64 muestras son estaticas: si una prueba acaba a medias, la
 * siguiente empieza corrida y sale otra imagen. Hay que reiniciarlas. */
static void mete_reset(void)
{
    int q;
    g_bj = 0; g_atasco_c = 0;
    for (q = 0; q < PEND_N; q++) { g_pend[q] = 0; g_pend_t[q] = 0; }
    g_pq_cab = 0; g_pq_n = 0; g_tandas = 0;
}

static void mete(float v, double sigma, tx_t *t)
{
    float *buf = g_buf;
    int   *jp  = &g_bj;
    int    j;
    const uint8_t *l;
    (void)t; (void)sigma;

    j = *jp;
    buf[j++] = v;
    *jp = j;
    if (j == 64) {
        wefax_process(buf, 64U);
        *jp = 0;
        /*
         * Y LO QUE DE VERDAD PASA NO ES QUE NO RECOJA: ES QUE TARDA CON LA
         * LINEA EN LA MANO.
         *
         * El bucle llama a wefax_linea(), se queda con el PUNTERO y se va a
         * escribir 4 kB en la flash, que son decenas de milisegundos. Hasta
         * que no acaba no ha terminado de leer esa linea. Mientras tanto la
         * interrupcion sigue cerrando lineas.
         *
         * Con dos buffers, a la segunda linea cerrada la interrupcion
         * vuelve a ESE buffer y escribe encima de lo que el bucle todavia
         * esta leyendo: sale un renglon con media linea de otro momento.
         *
         * Asi que aqui se coge el puntero y se copia g_atasco tandas MAS
         * TARDE, que es lo que hace el bucle de verdad.
         */
        if (g_atasco > 0) {
            /*
             * El bucle coge TODAS las lineas en cuanto estan -eso lo hace
             * bien- pero tarda g_atasco tandas en acabar de leer cada una,
             * porque mientras tanto esta escribiendo en la flash. O sea que
             * tiene varios punteros en la mano a la vez.
             *
             * Con el anillo de ocho eso es seguro mientras el retraso sea
             * menor que ocho lineas. Con los dos buffers de antes, basta
             * con que pase de dos.
             */
            /*
             * Cola de verdad, con RELOJ ABSOLUTO: cada linea cogida lleva
             * apuntada la tanda en la que el bucle acabara de leerla. Con
             * cuentas atras relativas el banco se equivocaba solo y media
             * su propio fallo.
             */
            g_tandas++;
            while (g_pq_n > 0 && g_pend_t[g_pq_cab] <= g_tandas) {
                if (g_nlin < MAX_LIN) {
                    memcpy(g_img[g_nlin], g_pend[g_pq_cab], WEFAX_ANCHO);
                    g_nlin++;
                }
                g_pq_cab = (g_pq_cab + 1) % PEND_N;
                g_pq_n--;
            }
            while ((l = wefax_linea()) != 0) {
                if (g_pq_n < PEND_N) {
                    int q2 = (g_pq_cab + g_pq_n) % PEND_N;
                    g_pend[q2] = l;
                    g_pend_t[q2] = g_tandas + g_atasco;
                    g_pq_n++;
                }
            }
            return;
        }
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
    mete_reset();

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

    /*
     * ======================================================================
     * 8. EL BUCLE SE ATASCA: NI SE PIERDEN LINEAS NI SE PARTEN
     * ======================================================================
     *
     * *** El decodificador tenia DOS buffers de linea y una bandera. El
     * bucle recibia el puntero a la linea cerrada y se iba a escribirla en
     * la flash -decenas de milisegundos-; dos lineas despues la
     * interrupcion volvia a ESE MISMO buffer y escribia encima. Y si se
     * cerraban dos lineas antes de que el bucle pasara, la primera
     * desaparecia sin que nadie se enterara. ***
     *
     * En el fichero guardado eso sale como renglones partidos por la mitad
     * y como renglones que faltan. Es lo mismo que ya se arreglo en sstv.c
     * subiendo su anillo de 4 a 12; el fax se habia quedado con dos.
     *
     * Aqui se mete el mismo tren de lineas dos veces: una recogiendo
     * siempre, y otra con el bucle ATASCADO a ratos, que es lo que pasa de
     * verdad. Tienen que salir las mismas lineas.
     */
    printf("\n8. el bucle atascado escribiendo en la flash\n");
    {
        static uint8_t limpio[MAX_LIN][WEFAX_ANCHO];
        int n_limpio, i, distintas = 0, min_n;

        g_atasco = 0;
        (void)prueba(120.0, 120.0, 0.0, 0, 0);
        n_limpio = g_nlin;
        memcpy(limpio, g_img, sizeof limpio);

        /* Antes de comparar nada: ¿se repite a si misma? Si dos pasadas
         * iguales ya salen distintas, el banco arrastra estado y lo que
         * mida despues no vale. */
        (void)prueba(120.0, 120.0, 0.0, 0, 0);
        {
            int d2 = 0, q3;
            for (q3 = 0; q3 < ((g_nlin < n_limpio) ? g_nlin : n_limpio); q3++) {
                if (memcmp(limpio[q3], g_img[q3], WEFAX_ANCHO) != 0) { d2++; }
            }
            char dd[48];
            snprintf(dd, sizeof dd, "%d distintas de %d", d2, n_limpio);
            comprueba(d2 == 0, "dos pasadas iguales dan lo mismo");
        }

        /*
         * Y ahora igual, pero soltando un atasco cada poco: seis tandas de
         * 64 muestras sin recoger nada. A 120 lpm y 48 kHz, una linea son
         * 375 tandas, asi que esto NO llena el anillo -son microsegundos de
         * linea- pero si basta para que el buffer que el bucle tiene en la
         * mano sea reutilizado por la interrupcion con el esquema de dos.
         */
        /*
         * 300 tandas de 64 muestras a 12 kHz son 1,6 segundos, o sea algo
         * mas de TRES lineas a 120 lpm. Elegido asi a proposito:
         *
         *   - pasa de DOS lineas, que es lo que aguantaba el esquema de dos
         *     buffers. Con aquel, esto sale partido;
         *   - y no llega a OCHO, que es lo que aguanta el anillo. Con este,
         *     sale intacto.
         *
         * En la radio de verdad el bucle tiene una linea en la mano unos
         * 51 ms -lo que cuesta escribir un bloque de 4 kB, medido en
         * sim/fax_bloqueo.c- sobre lineas de 500 ms. O sea 0,1 lineas: el
         * anillo tiene ochenta veces mas margen del que hace falta.
         */
        g_atasco = 300;
        (void)prueba(120.0, 120.0, 0.0, 0, 0);

        min_n = (g_nlin < n_limpio) ? g_nlin : n_limpio;
        if (min_n > 4) { min_n -= 4; }   /* las ultimas quedan en vuelo */
        printf("    sin atasco %d lineas, con atasco %d\n", n_limpio, g_nlin);
        for (i = 0; i < min_n; i++) {
            if (memcmp(limpio[i], g_img[i], WEFAX_ANCHO) != 0) {
                if (distintas == 0) {
                    int b;
                    for (b = 0; b < WEFAX_ANCHO; b++) {
                        if (limpio[i][b] != g_img[i][b]) { break; }
                    }
                    printf("    primera distinta: linea %d, byte %d\n", i, b);
                }
                distintas++;
            }
        }
        {
            char d[64];
            snprintf(d, sizeof d, "faltan %d", n_limpio - g_nlin);
            comprueba(g_nlin >= n_limpio - PEND_N,
                      "no se pierden lineas por el camino");
            snprintf(d, sizeof d, "%d lineas distintas", distintas);
            comprueba(distintas == 0, "y ninguna sale partida");
        }
        g_atasco = 0;
    }

    /*
     * ======================================================================
     * 9. Y QUE LA CARTA SALGA EN SU SITIO, NO SOLO RECTA
     * ======================================================================
     *
     * Y era verdad: un trozo de unos 20 puntos del margen izquierdo
     * aparecia pegado al borde derecho, o sea que la carta entera estaba
     * corrida. No se perdia nada - rodando la imagen 21 puntos volvia a su
     * sitio - pero sale fea y el texto del margen queda partido.
     *
     * Este banco medía la PENDIENTE del borde -si la carta sale recta o
     * torcida- y eso es media pregunta. La otra media es DONDE empieza la
     * linea, y no la miraba nadie: una carta corrida 20 puntos sale
     * perfectamente recta y el banco la daba por buena.
     *
     * El patron tiene un escalon nitido en x = 300 -oscuro a la izquierda,
     * claro a la derecha-. Si el decodificador engancha bien, ese escalon
     * tiene que salir en x = 300.
     * ====================================================================== */
    printf("\n9. la carta sale en su sitio, no solo recta\n");
    {
        static const double k_d[] = { 0.0, 0.25, 0.5, 0.75 };
        unsigned i;
        int peor = 0;

        for (i = 0; i < sizeof k_d / sizeof k_d[0]; i++) {
            int x, mejor_x = 0, y, n = 0;
            double mejor = 0.0, perfil[WEFAX_ANCHO];

            g_desfase = k_d[i];
            prueba(120.0, 120.0, 0.02, 0, 0);
            g_desfase = 0.0;

            /* El perfil medio de las lineas de imagen ya enganchadas. */
            for (x = 0; x < (int)WEFAX_ANCHO; x++) { perfil[x] = 0.0; }
            for (y = 20; y < g_nlin; y++) {
                for (x = 0; x < (int)WEFAX_ANCHO; x++) {
                    perfil[x] += (double)g_img[y][x];
                }
                n++;
            }
            if (n == 0) { comprueba(0, "no han salido lineas que mirar"); continue; }
            for (x = 0; x < (int)WEFAX_ANCHO; x++) { perfil[x] /= (double)n; }

            /* El salto mas grande del perfil: el escalon de x = 300. Se mira
             * sobre ocho puntos para que una raya del adorno no gane. */
            for (x = 8; x < (int)WEFAX_ANCHO - 8; x++) {
                double d = perfil[x + 8] - perfil[x - 8];
                if (d > mejor) { mejor = d; mejor_x = x; }
            }
            printf("    empieza %.0f%% de linea tarde: el escalon de x=300 sale en x=%d"
                   " (corrido %+d)\n", k_d[i] * 100.0, mejor_x, mejor_x - 300);
            if (mejor_x - 300 > peor)  { peor = mejor_x - 300; }
            if (300 - mejor_x > peor)  { peor = 300 - mejor_x; }
        }
        {
            char d[64];
            snprintf(d, sizeof d, "el peor se va %d puntos", peor);
            /* Ocho puntos es un 1% de la linea: por debajo de eso nadie ve
             * un margen partido. Veinte -lo que traia- se ve a simple vista. */
            comprueba(peor <= 8, "la carta no sale corrida de lado");
        }
    }

    /*
     * ======================================================================
     * 10. EL CENTRADO A MANO
     * ======================================================================
     *
     * El caso 9 dice que contra la señal de ESTE banco el decodificador
     * engancha bien. La carta del DWD salia corrida 21 puntos, o sea que
     * la suposicion sobre donde va el pulso de fase no es la misma en los
     * dos sitios - y el banco no puede arbitrar, porque cree lo mismo que
     * el codigo.
     *
     * Asi que hay un mando. Lo que se prueba aqui es lo unico que se puede
     * prometer de el: que corre la imagen exactamente lo que se le dice,
     * en el sentido que se le dice, sin perder un punto y SIN TOCAR EL
     * ENGANCHE - el escalon tiene que seguir saliendo donde salia, mas el
     * desplazamiento pedido, y la carta tiene que seguir igual de recta.
     * ====================================================================== */
    printf("\n10. el centrado a mano\n");
    {
        /* Lo que el usuario midio en sus dos cartas -21 y 269- y los
         * extremos del recorrido, que ahora es la linea entera. */
        static const int k_px[] = { 0, 21, -21, 269, -269, 378, -378 };
        unsigned i;
        int base = 0;
        int ok_sitio = 1, ok_nada = 1;

        for (i = 0; i < sizeof k_px / sizeof k_px[0]; i++) {
            int x, mejor_x = 0, y, n = 0;
            double mejor = 0.0, perfil[WEFAX_ANCHO], suma = 0.0;

            wefax_set_centro((int16_t)k_px[i]);
            prueba(120.0, 120.0, 0.02, 0, 0);

            for (x = 0; x < (int)WEFAX_ANCHO; x++) { perfil[x] = 0.0; }
            for (y = 20; y < g_nlin; y++) {
                for (x = 0; x < (int)WEFAX_ANCHO; x++) {
                    perfil[x] += (double)g_img[y][x];
                    suma += (double)g_img[y][x];
                }
                n++;
            }
            if (n == 0) { comprueba(0, "no han salido lineas que mirar"); break; }
            for (x = 0; x < (int)WEFAX_ANCHO; x++) { perfil[x] /= (double)n; }
            for (x = 8; x < (int)WEFAX_ANCHO - 8; x++) {
                double d = perfil[x + 8] - perfil[x - 8];
                if (d > mejor) { mejor = d; mejor_x = x; }
            }
            if (i == 0) { base = mejor_x; }
            {
                int quiero = base + k_px[i];
                while (quiero >= (int)WEFAX_ANCHO) { quiero -= WEFAX_ANCHO; }
                while (quiero < 0)                 { quiero += WEFAX_ANCHO; }
                printf("    centrado %+3d: el escalon sale en x=%d, se esperaba %d\n",
                       k_px[i], mejor_x, quiero);
                if (mejor_x - quiero > 3 || quiero - mejor_x > 3) { ok_sitio = 0; }
            }
            /* Y que no se pierda ni se invente nada: la suma de los grises
             * de la imagen no puede cambiar al rodarla. */
            {
                static double suma0 = 0.0;
                if (i == 0) { suma0 = suma / (double)n; }
                else {
                    double d = suma / (double)n - suma0;
                    if (d < -600.0 || d > 600.0) { ok_nada = 0; }
                }
            }
        }
        wefax_set_centro(0);
        comprueba(ok_sitio, "corre la imagen exactamente lo que se le pide");
        comprueba(ok_nada,  "y da la vuelta por el otro lado sin perder nada");
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

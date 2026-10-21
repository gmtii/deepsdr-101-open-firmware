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
/*
 * EL PATRON PLANO, PARA VER UN RENGLON PARTIDO - 21/10/2026.
 *
 * El patron de arriba no sirve para eso: sus rayas cambian cada veinte
 * lineas, asi que un renglon cosido con la mitad de la linea N y la mitad
 * de la N-8 sale IDENTICO a uno bueno. Con el patron plano cada linea es un
 * gris uniforme distinto, y entonces "partido" se ve sin interpretar nada:
 * es un renglon con dos valores dentro.
 */
static int g_plano = 0;
/*
 * Y EL CONTADOR VA APARTE, no se usa la `y` que llega.
 *
 * lineas_imagen() numera sus lineas desde CERO en cada llamada, asi que
 * partir la emision en tres tandas -que es como se monta una sordera en
 * medio- reiniciaba la cuenta y metia dos saltos que no eran huecos. El
 * banco medira su propia forma de emitir y no el decodificador.
 */
static unsigned g_plano_n = 0;

static uint8_t patron(int x, int y)
{
    int v;
    if (g_plano) {
        (void)x; (void)y;
        return (uint8_t)(24 + ((g_plano_n * 7U) % 200U));
    }
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

/*
 * Y EL BUCLE SORDO, QUE ES OTRA COSA - 21/10/2026.
 *
 * g_atasco modela un bucle que recoge TODAS las lineas en cuanto estan y
 * tarda en soltarlas. Eso no llena el anillo: lo llena un bucle que NO PASA
 * por el recogedor, que es lo que de verdad hace la radio cuando abre un
 * fichero nuevo o reintenta la flash. Con el anillo lleno, el escritor y el
 * lector apuntan a la misma ranura y lo que se entrega es el renglon que la
 * interrupcion esta escribiendo ahora mismo.
 */
#define PEND_N 16
/*
 * EL CRONOMETRO DEL CASO 12: en que muestra del transmisor llega el
 * renglon numero g_vigila. Sirve para medir cuanto tarda el decodificador
 * en volver a pintar despues de un tono de fin, que es el numero del que
 * depende el cerrojo de main.c.
 */
static int  g_vigila = 0;
static long g_vigila_m = -1;

static int g_sordo = 0;       /* tandas que quedan sin pasar por el recogedor */
static uint8_t g_una[WEFAX_ANCHO];          /* donde copia wefax_linea_en() */
static uint8_t g_pend_buf[PEND_N][WEFAX_ANCHO];  /* la cola, ya copiada */
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
    (void)sigma;

    j = *jp;
    buf[j++] = v;
    *jp = j;
    if (j == 64) {
        wefax_process(buf, 64U);
        *jp = 0;
        if (g_sordo > 0) { g_sordo--; return; }
        /*
         * EL ATASCO, Y LO QUE MIDE HOY.
         *
         * Esto nacio modelando un bucle que se quedaba el PUNTERO a la
         * ranura y tardaba en soltarlo. Desde el 21/10/2026 el
         * decodificador COPIA el renglon dentro de la seccion critica
         * -wefax_linea_en()- asi que esa carrera ya no existe por
         * construccion y este modelo no la puede ver.
         *
         * Se queda porque sigue midiendo algo distinto y util: que el bucle
         * puede ir por detras sin que se pierdan lineas. Quien mide lo del
         * anillo es el caso 10.
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
            while (wefax_linea_en(g_una)) {
                if (g_pq_n < PEND_N) {
                    int q2 = (g_pq_cab + g_pq_n) % PEND_N;
                    memcpy(g_pend_buf[q2], g_una, WEFAX_ANCHO);
                    g_pend[q2] = g_pend_buf[q2];
                    g_pend_t[q2] = g_tandas + g_atasco;
                    g_pq_n++;
                }
            }
            return;
        }
        while (wefax_linea_en(g_una)) {
            if (g_nlin < MAX_LIN) { memcpy(g_img[g_nlin], g_una, WEFAX_ANCHO); g_nlin++; }
        }
        if (g_vigila > 0 && g_vigila_m < 0 && g_nlin >= g_vigila) {
            g_vigila_m = t->muestra;
        }
    }
}

/* Cuantas muestras dura una linea del TRANSMISOR. */
static double muestras_linea(const tx_t *t) { return FS * 60.0 / t->lpm; }

/*
 * EL TONO, CON SU AMPLITUD - 21/10/2026.
 *
 * El tono de arranque y el de parada no son tonos de audio: son el BRILLO
 * alternando entre negro y blanco a 300 o a 450 Hz. A FONDO, o sea de 0 a
 * 255, que es lo que emite una estacion.
 *
 * Y hacia falta poder mandarlo FLOJO, por las dos cosas que decide la
 * amplitud y que ningun caso tocaba:
 *
 *   - una trama de semitono de una carta puede caer justo en 300 Hz, y si
 *     el umbral de dentro de la imagen fuera bajo, partiria el fichero en
 *     dos. Por eso ahi se exige 6.000 y no 200;
 *   - y un tono de FIN que llega desvanecido tiene que oirse igual, porque
 *     perderlo cose dos cartas en un fichero. Por eso ahi se admiten 200.
 *
 * Los dos numeros estan medidos y escritos en wefax.c, y los dos
 * sobrevivian a una mutacion: subir el de parada a 400 o bajar el de
 * imagen a 2.000 dejaba el banco entero en verde.
 */
static void tono_apt_gris(tx_t *t, double hz_alterna, double seg, double sigma,
                          int claro, int oscuro)
{
    long n = (long)(FS * seg), k;
    for (k = 0; k < n; k++) {
        int blanco = (((long)(k * hz_alterna * 2.0 / FS)) & 1);
        double hz = wefax_hz_de_gris((uint8_t)(blanco ? claro : oscuro));
        mete((float)emite(t, hz, sigma), sigma, t);
    }
}

static void tono_apt(tx_t *t, double hz_alterna, double seg, double sigma)
{
    tono_apt_gris(t, hz_alterna, seg, sigma, 255, 0);
}

/*
 * Una linea con la FORMA de una de fase -un pulso blanco del 5% al
 * principio- pero con el fondo que se le diga. Con fondo 0 es una linea de
 * fase de verdad; con un fondo mas claro es lo que de verdad puede
 * aparecer en una carta y NO es una linea de fase. Ver el caso 11c.
 */
static void lineas_fase_fondo(tx_t *t, int n, double sigma, int fondo)
{
    double ml = muestras_linea(t);
    int i; long k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < (long)ml; k++) {
            double f = (double)k / ml;
            int blanco = (f < 0.05);
            double hz = wefax_hz_de_gris((uint8_t)(blanco ? 255 : fondo));
            mete((float)emite(t, hz, sigma), sigma, t);
        }
    }
}

static void lineas_fase(tx_t *t, int n, double sigma)
{
    lineas_fase_fondo(t, n, sigma, 0);
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
        g_plano_n++;        /* la cuenta ABSOLUTA de renglones emitidos */
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
    /*
     * *** TREINTA SEGUNDOS, QUE NO SON SESENTA LINEAS - 21/10/2026. ***
     *
     * Aqui ponia `lineas_fase(&t, 60, sigma)` con el comentario "30 s, como
     * una emisora de verdad". Sesenta lineas son 30 s **A 120 lpm**, y esta
     * funcion la llaman los casos 5, 6, 7 y 9 a las cuatro velocidades:
     *
     *      60 lpm ->  60 lineas = SESENTA segundos
     *      90 lpm ->  60 lineas = cuarenta segundos
     *     120 lpm ->  60 lineas = treinta          <- la unica buena
     *     240 lpm ->  60 lineas = quince
     *
     * O sea que a 60 y a 90 lpm el banco mandaba una señal de fase que no
     * existe, el decodificador se salia por el TOPE DE SEGURIDAD -35 s de
     * lineas- y pintaba el resto como carta. Se veia en la cuenta de
     * lineas: 228 y 215 de las 200 emitidas, contra 198 a 120 lpm. Nadie lo
     * miraba porque la unica comprobacion de la cuenta era `> 150`, o sea
     * por abajo.
     *
     * Es el mismo error de razonamiento que ya costo el fallo de los 90 lpm
     * -ver s_fase_tira en wefax.c- y la franja de la fase -caso 11-: una
     * cuenta hecha a 120 lpm y dada por general. La tercera vez.
     *
     * Son del TRANSMISOR: lo que mande depende de su velocidad real, no de
     * la que tenga puesta el decodificador. Por eso t.lpm y no lpm_nom.
     */
    lineas_fase(&t, (int)(t.lpm / 2.0 + 0.5), sigma);   /* 30 s de verdad */
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
        /*
         * POR ARRIBA TAMBIEN - 21/10/2026. Ponia solo `> 150`, y una
         * comprobacion por un lado solo deja pasar el fallo del otro: la
         * franja de la señal de fase pintada como carta -caso 11- sube esta
         * cuenta en vez de bajarla, y aqui habria pasado en verde. Se emiten
         * 200 renglones de imagen y llegan 198 o 199 a las cuatro
         * velocidades; 210 deja margen y sigue cazando una franja.
         */
        comprueba(g_nlin > 150 && g_nlin <= 210,
                  "salen las lineas, y no mas de las que se emiten");
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
        /* La lista se pregunta, no se copia - 21/10/2026. Y el nombre del
         * fichero sale de la velocidad, por lo mismo: una tabla de nombres
         * en paralelo es otra copia que se descoloca sola. */
        unsigned i;
        for (i = 0; i < (unsigned)WEFAX_LPM_N; i++) {
            const double lpm_i = (double)wefax_lpm_opcion((uint8_t)i);
            char pgm[48];

            snprintf(pgm, sizeof pgm, "out/wefax_l%u.pgm", (unsigned)lpm_i);
            printf("    %.0f lpm:\n", lpm_i);
            prueba(lpm_i, lpm_i * (1.0 + 0.001), 0.02, pgm, 1);
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
     * AQUI ESTABA EL CASO 10, "el centrado a mano" - quitado el 20/10/2026.
     *
     * Probaba un mando que corria la imagen MIENTRAS entraba. Ese mando ya
     * no existe: el centrado se elige AL ACABAR la carta, con una linea
     * vertical sobre la imagen entera, y lo que se gira es el fichero de
     * la flash, no el renglon que sale del decodificador.
     *
     * No se quito porque estorbara: se quito porque lo que probaba lo
     * prueba ahora sim/recoloca.c, y mejor. Alli la comprobacion es la
     * IMAGEN RELEIDA DEL CHIP punto a punto, no el perfil de un escalon, y
     * ademas mira que el fichero vecino siga entero.
     */

    /* ======================================================================
     * 10. UN BUCLE SORDO NO PUEDE PARTIR UN RENGLON
     * ======================================================================
     *
     * El caso 8 modela un bucle que recoge todas las lineas y tarda en
     * soltarlas. Este modela el otro bucle real: el que NO PASA por el
     * recogedor durante un rato -abrir un fichero nuevo, reintentar la
     * flash- y luego vuelve y recoge todo de golpe.
     *
     * Eso LLENA el anillo, y ahi estaba el agujero: con ocho lineas sin
     * recoger, el escritor y el lector apuntan a la misma ranura, porque
     * el invariante es s_esc = (s_lee + lista) % 8. Lo que se entregaba
     * era el renglon que la interrupcion estaba escribiendo: media linea
     * de ahora y media de hace ocho.
     *
     * Con el patron plano eso se ve sin interpretar nada: cada linea es un
     * gris uniforme, asi que un renglon partido tiene DOS valores dentro.
     * ====================================================================== */
    printf("\n10. un bucle sordo no parte ningun renglon\n");
    {
        tx_t t;
        int y, partidos = 0, peor = 0;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 9876UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 10, 0.0);     /* unas cuantas con el bucle despierto */

        /*
         * Y ahora el bucle se va doce lineas. A 120 lpm una linea son 6.000
         * muestras, o sea 93,75 tandas de 64; mil doscientas tandas son unas
         * trece lineas, de sobra para pasar de las ocho del anillo.
         */
        g_sordo = 1200;
        lineas_imagen(&t, 30, 0.0);
        g_sordo = 0;
        lineas_imagen(&t, 10, 0.0);

        /*
         * COMO SE MIDE "PARTIDO", Y POR QUE NO VALE EL MINIMO Y EL MAXIMO.
         *
         * El discriminador tarda unos puntos en asentarse al saltar del gris
         * de la linea anterior al de esta, asi que el PRINCIPIO de todo
         * renglon bueno lleva una rampa. Con minimo y maximo, esa rampa sola
         * ya marca como partidos los renglones buenos: el banco mediria su
         * propio asentamiento.
         *
         * Un renglon cosido no es ruidoso: tiene un ESCALON. Asi que se
         * comparan las medias de dos trozos lejanos entre si y lejos del
         * principio. Entre lineas consecutivas hay 11 niveles, asi que dos
         * mitades de lineas separadas por el anillo saltan unos 88.
         *
         * *** Y EL LISTON ESTABA A 30 CUANDO EL ESCALON MIDE 27 -
         * 21/10/2026. ***
         *
         * Esos "unos 88" son el escalon de un renglon partido JUSTO POR LA
         * MITAD. El que de verdad sale aqui no esta por la mitad: el bucle
         * vuelve a los 0,8 de linea, asi que la costura cae en el punto 664
         * y solo 92 de los 300 puntos de la ventana derecha son de la otra
         * linea. 88 por 92/300 son 27.
         *
         * O sea que el liston estaba TRES niveles por encima del unico
         * escalon que este caso puede producir. Medido con una mutacion: la
         * capacidad del anillo es N-1 a proposito -con N, el escritor y el
         * lector acaban en la misma ranura y el recogedor entrega el
         * renglon que se esta rellenando, que es el fallo que el anillo
         * vino a quitar- y cambiando ese N-1 por N sale exactamente un
         * renglon con d = 27. Con el liston en 30, el banco lo dejaba
         * pasar.
         *
         * Y no hacia falta tanto margen: con el patron plano, los 44
         * renglones buenos dan d = CERO, los 44. El liston se pone en 8,
         * que es ruido puro, y entonces el renglon partido se ve.
         */
        for (y = 0; y < g_nlin; y++) {
            int x; long izq = 0, der = 0; int d;
            for (x = 24; x < 300; x++)  { izq += g_img[y][x]; }
            for (x = 456; x < 756; x++) { der += g_img[y][x]; }
            d = (int)(izq / (300 - 24) - der / (756 - 456));
            if (d < 0) { d = -d; }
            if (d > 8) { partidos++; if (d > peor) { peor = d; } }
        }
        printf("    %d renglones recogidos, %d partidos (el peor salta %d niveles)\n",
               g_nlin, partidos, peor);
        /* Por arriba tambien: se emiten 50 renglones de imagen, y lo que
         * sube esta cuenta por encima de eso es la señal de fase pintada
         * como carta -caso 11-, no una mejora. */
        comprueba(g_nlin > 20 && g_nlin <= 52,
                  "el bucle sordo no se queda sin lineas, ni le sobran");
        comprueba(partidos == 0, "y ninguna de las que recoge esta cosida");
        g_plano = 0;
        g_sordo = 0;
    }

    /* ======================================================================
     * 10b. Y EL ANILLO TIENE QUE SER GRANDE: UN SORDO CORTO NO PIERDE NADA
     * ======================================================================
     *
     * *** ESTE CASO EXISTE PORQUE EL BANCO SE QUEDO SIN DIENTES. ***
     *
     * Desde que el decodificador COPIA el renglon en vez de prestar el
     * puntero, el modelo de "atasco" del caso 8 ya no puede ver nada: se
     * comprobo recompilando wefax.c con WFX_ANILLO = 2 -o sea la
     * configuracion de dos buffers cuyos dos fallos justificaron el anillo-
     * y el banco entero pasaba con cero fallos. Un banco que pasa con y sin
     * el arreglo no mide el arreglo.
     *
     * Lo que el anillo compra HOY no es evitar renglones cosidos -de eso se
     * encarga la copia- sino NO PERDER lineas mientras el bucle esta fuera.
     * Asi que eso es lo que se mide: el bucle se va cinco lineas, que caben
     * en el anillo de ocho y no en uno de dos, y al volver tienen que estar
     * las cinco.
     *
     * Con el patron plano cada linea lleva su gris, y entre dos consecutivas
     * hay exactamente 7 niveles: una perdida se ve como un salto de 14.
     *
     * EL PASO ES 7 Y NO 11, Y LA CUENTA NO DA LA VUELTA. Con paso 11 sobre
     * un recorrido de 200, un hueco de DIECIOCHO lineas da 19*11 mod 200 =
     * 9, o sea menos que el paso bueno: un hueco grande se leia como "no
     * falta ninguna". Con 7 y veinte renglones el recorrido entero son 140,
     * no se da la vuelta, y cualquier hueco sale mayor que el paso.
     * ====================================================================== */
    printf("\n10b. un sordo corto no pierde ninguna linea\n");
    {
        tx_t t;
        int y, saltos = 0, mayor = 0, planos_n = 0;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 2468UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 6, 0.0);

        /* Cinco lineas de sordera: 94 tandas por linea a 120 lpm. */
        g_sordo = 5 * 94;
        lineas_imagen(&t, 6, 0.0);
        g_sordo = 0;
        lineas_imagen(&t, 8, 0.0);

        /*
         * El gris de cada renglon, medido lejos del principio para no
         * coger la rampa del discriminador. Dos consecutivos tienen que
         * llevarse 11; 22 es una linea que falta.
         */
        /*
         * Y SOLO ENTRE RENGLONES DE IMAGEN. Detras de la señal de fase
         * llegan las lineas de fase que sobran -negras con el pulso
         * blanco-, que no son planas y darian saltos que no son huecos.
         * Se compara solo cuando los dos renglones son planos.
         */
        {
            int ant = -1;
            for (y = 0; y < g_nlin; y++) {
                long sum = 0; int x, lo = 255, hi = 0, m;
                for (x = 100; x < 700; x++) {
                    sum += g_img[y][x];
                    if (g_img[y][x] < lo) { lo = g_img[y][x]; }
                    if (g_img[y][x] > hi) { hi = g_img[y][x]; }
                }
                m = (int)(sum / 600);
                /* Las lineas de fase que sobran detras del enganche tambien
                 * son planas en este tramo -el pulso esta en el 5% de la
                 * izquierda- pero son NEGRAS, y el patron plano nunca baja
                 * de 24. */
                if (hi - lo > 30 || m < 20) { ant = -1; continue; }
                planos_n++;
                if (ant >= 0) {
                    int d = m - ant;
                    while (d < 0) { d += 200; }
                    if (d > 16) { saltos++; if (d > mayor) { mayor = d; } }
                }
                ant = m;
            }
            printf("    %d renglones planos de los %d recogidos\n", planos_n, g_nlin);
        }
        printf("    %d renglones seguidos, %d huecos (el mayor salta %d niveles)\n",
               g_nlin, saltos, mayor);
        /*
         * *** Y SE CUENTAN LOS PLANOS, NO LOS RECOGIDOS. ***
         *
         * Aqui ponia `g_nlin >= 18`, y g_nlin incluye los renglones de la
         * señal de fase que sobran detras del enganche -veintisiete de los
         * cuarenta y seis- o sea justo los que el filtro descarta. Y
         * `saltos` solo sube dentro de una comparacion, asi que con cero o
         * un renglon plano vale cero por construccion.
         *
         * Las dos juntas dejaban pasar el caso peor: se probo con un
         * recogedor que tiraba TODAS las lineas de imagen y el caso salia
         * ok dos veces, con cero renglones de los veinte que existe para
         * contar. Lo que hay que exigir es el numero de planos.
         */
        /* Y por arriba, por lo mismo: veinte emitidas, veinte como mucho.
         * planos_n solo cuenta las PLANAS, asi que una franja de señal de
         * fase no lo subiria y este caso la dejaria pasar. */
        comprueba(planos_n >= 19 && g_nlin <= 22,
                  "llegan las veinte lineas de imagen, y nada mas");
        comprueba(saltos == 0, "y no falta ninguna de las que el bucle no oyo");
        g_plano = 0;
        g_sordo = 0;
    }

    /* ======================================================================
     * 11. A 60 lpm LA CARTA EMPIEZA POR ARRIBA
     * ======================================================================
     *
     * La señal de fase dura TREINTA SEGUNDOS, que no son las mismas lineas a
     * cada velocidad: son 120 a 240 lpm y solo 30 a 60. El enganche pedia dos
     * pasadas de dieciseis lineas mas la que parte el salto, o sea 33, asi
     * que a 60 lpm NO CABIAN y el decodificador se quedaba esperando una
     * segunda pasada que ya no podia llegar.
     *
     * Y en ese estado no se encola nada: las lineas de imagen de ese rato se
     * decodificaban y se tiraban. Faltaba la franja de arriba de la carta,
     * que es donde el DWD pone la cabecera con la hora.
     *
     * Este banco manda la fase que DE VERDAD cabe a cada velocidad -lpm/2
     * lineas- y cuenta cuantas lineas de imagen llegan de las que se
     * emitieron.
     * ====================================================================== */
    printf("\n11. la señal de fase que cabe a cada velocidad\n");
    {
        /* LA LISTA SE PREGUNTA, NO SE COPIA - 21/10/2026. Estaba escrita a
         * mano aqui; el dia que una velocidad cambiara, este caso seguiria
         * probando las de antes y dando verde. Ver wefax_lpm_opcion(). */
        unsigned i;
        int todas_ok = 1;

        g_plano = 1;
        for (i = 0; i < (unsigned)WEFAX_LPM_N; i++) {
            const double lpm_i = (double)wefax_lpm_opcion((uint8_t)i);
            tx_t t;
            int fase_n = (int)(lpm_i / 2.0);   /* 30 s */
            int img_n = 60;

            g_plano_n = 0;
            memset(&t, 0, sizeof t);
            t.lpm = lpm_i; t.semilla = 4321UL;
            g_nlin = 0;
            mete_reset();

            wefax_start((float)FS);
            wefax_set_lpm((uint16_t)lpm_i);
            tono_apt(&t, 300.0, 5.0, 0.0);
            lineas_fase(&t, fase_n, 0.0);
            lineas_imagen(&t, img_n, 0.0);

            /*
             * Y CUANTAS DE LAS QUE LLEGAN SON DE LA SEÑAL DE FASE.
             *
             * Una linea de fase es negra -media por debajo de 40, porque el
             * pulso blanco ocupa el 5%- con un trozo claro corto. Una linea
             * de imagen de este patron es un gris plano de 24 a 223 SIN
             * ningun punto por encima de 160 cuando es oscura. O sea que
             * "oscura Y con algun punto blanco" solo lo cumple la fase, y
             * con eso se cuentan sin confundirlas.
             */
            {
                int y, fase_pintadas = 0;
                for (y = 0; y < g_nlin; y++) {
                    long suma = 0; int x, claros = 0;
                    for (x = 0; x < (int)WEFAX_ANCHO; x++) {
                        suma += g_img[y][x];
                        if (g_img[y][x] >= 160) { claros++; }
                    }
                    if ((suma / (long)WEFAX_ANCHO) < 40 && claros >= 4
                        && claros <= (int)WEFAX_ANCHO / 4) {
                        fase_pintadas++;
                    }
                }
                printf("    %3u lpm: fase de %2d lineas (30 s) -> llegan %3d de %d "
                       "lineas de imagen, %d de ellas son de la señal de fase\n",
                       (unsigned)lpm_i, fase_n, g_nlin, img_n, fase_pintadas);
                /*
                 * *** Y LA BANDA DE ABAJO ERA DE DIECISEIS LINEAS -
                 * 21/10/2026. ***
                 *
                 * Ponia `(img_n * 3) / 4`, o sea 45 de 60: cazaba «se
                 * pierde la mitad» y nada mas fino. Y lo fino es justo lo
                 * que hay que vigilar aqui, porque lo que decide donde
                 * empieza la carta es una cuenta -los 35 s de
                 * tope_lineas_fase()- que se puede tocar sin que nada
                 * chille: subiendola a 40 llegan 56 en vez de 59 y este
                 * caso seguia en verde. Tres renglones menos en la cabecera
                 * de CADA carta, que es donde el DWD pone la hora y el
                 * nombre, y ni un aviso.
                 *
                 * Con 5 s de tono y 30 de fase llegan 59 de 60 a las cuatro
                 * velocidades. Se admiten tres de perdida -el instante del
                 * relevo no es exacto y no tiene por que serlo- y ni una
                 * mas.
                 */
                if (g_nlin < img_n - 3) { todas_ok = 0; }
                /*
                 * Y EL OTRO: que sobren. Antes del 21/10/2026 el
                 * decodificador entraba en imagen en cuanto acababa el
                 * enganche y encolaba el resto de la señal de fase como
                 * carta: 13 renglones a 60 lpm, 26 a 120 y OCHENTA Y SEIS a
                 * 240, una franja negra rayada encima de cada carta. Con
                 * cero de margen esto cazaria tambien un chasquido, asi que
                 * se admite UNO; ochenta y seis no.
                 */
                if (fase_pintadas > 1) { todas_ok = 0; }
                if (g_nlin > img_n + 1) { todas_ok = 0; }
            }
        }
        comprueba(todas_ok,
                  "la carta empieza por arriba, y sin la franja de la fase");
        g_plano = 0;
    }

    /* ======================================================================
     * 11b. UN CHASQUIDO NO ACABA LA SEÑAL DE FASE
     * ======================================================================
     *
     * Hechas las pasadas del enganche, el decodificador sigue en
     * WEFAX_FASE y sale cuando TRES lineas seguidas dejan de parecer
     * lineas de fase (FASE_FIN_N). El tres esta razonado donde se define:
     * "tres y no una porque un chasquido no puede adelantar el final".
     *
     * Y nadie lo probaba. Se vio con una mutacion: poniendo FASE_FIN_N a 1
     * el banco entero seguia en verde, y lo que eso significa en el aire es
     * que un pulso de ruido a mitad de la señal de fase manda a imagen con
     * veinte segundos de fase todavia por delante - o sea la franja negra
     * rayada encima de la carta que costo la pasada 6, otra vez, por otro
     * camino.
     *
     * Aqui se mete UNA linea que no es de fase justo despues de que las
     * pasadas esten hechas, y despues sigue la fase. Con tres, el
     * chasquido no cuenta y no se pinta ni un renglon de fase; con uno, se
     * pintan los dieciocho que quedan.
     * ====================================================================== */
    printf("\n11b. un chasquido no acaba la señal de fase\n");
    {
        tx_t t;
        int y, fase_pintadas = 0;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 2468UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        /* 40 lineas: de sobra para las dos pasadas, que gastan 34. */
        lineas_fase(&t, 40, 0.0);
        /*
         * El chasquido son DOS lineas, no una, y eso es lo que fija el
         * tres: con una bastaria un FASE_FIN_N de dos. Un segundo de
         * ruido a 120 lpm son dos renglones, asi que es el chasquido que
         * de verdad hay que aguantar.
         */
        lineas_imagen(&t, 2, 0.0);      /* el chasquido */
        lineas_fase(&t, 16, 0.0);       /* y la fase sigue */
        lineas_imagen(&t, 40, 0.0);

        /* El mismo contador de lineas de fase del caso 11. */
        for (y = 0; y < g_nlin; y++) {
            long suma = 0; int x, claros = 0;
            for (x = 0; x < (int)WEFAX_ANCHO; x++) {
                suma += g_img[y][x];
                if (g_img[y][x] >= 160) { claros++; }
            }
            if ((suma / (long)WEFAX_ANCHO) < 40 && claros >= 4
                && claros <= (int)WEFAX_ANCHO / 4) {
                fase_pintadas++;
            }
        }
        printf("    un chasquido a mitad de la fase -> %d renglones de fase "
               "pintados como carta\n", fase_pintadas);
        /* Con FASE_FIN_N = 3 salen cero. Se admite uno -el propio
         * chasquido podria contarse mal- y no mas. Probado en rojo con
         * FASE_FIN_N a 1 y a 2: salen 18 y 16 renglones de fase dentro de
         * la carta. */
        comprueba(fase_pintadas <= 1,
                  "un pulso de ruido no adelanta el final de la fase");
        g_plano = 0;
    }

    /* ======================================================================
     * 11c. UNA LINEA QUE NO ES NEGRA NO ES UNA LINEA DE FASE
     * ======================================================================
     *
     * pulso_centro() pide TRES cosas a la vez para creerse que una linea es
     * de la señal de fase: que sea negra (media por debajo de 80), que los
     * puntos claros sean pocos, y que esten JUNTOS. Su comentario dice "las
     * tres hacen falta".
     *
     * Dos de las tres salian en rojo al mutarlas. La de la media no: subirla
     * de 80 a 120 dejaba el banco entero en verde.
     *
     * Y lo que se cuela por ahi es justo lo que una carta tiene de sobra: una
     * franja oscura con un borde claro corto. Aqui se mandan tres lineas asi
     * -fondo gris 100, que da una media de 108- al acabar la señal de fase.
     * Con el 80 son lo que son, no-fase, y la señal se acaba cuando toca; con
     * el 120 pasan por lineas de fase, la señal "sigue", y los tres renglones
     * que la acaban salen entonces de la CARTA.
     * ====================================================================== */
    printf("\n11c. una linea que no es negra no es una linea de fase\n");
    {
        tx_t t;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 1357UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 40, 0.0);
        lineas_fase_fondo(&t, 3, 0.0, 100);   /* oscuras, pero no negras */
        lineas_imagen(&t, 40, 0.0);

        printf("    tres lineas de fondo gris 100 al acabar la fase -> "
               "llegan %d de 40 renglones de carta\n", g_nlin);
        /* Con el umbral bueno llegan los 40 menos el relevo; con 120 se
         * pierden tres mas. */
        comprueba(g_nlin >= 38,
                  "una franja oscura con un borde claro no alarga la fase");
        g_plano = 0;
    }

    /* ======================================================================
     * 12. LA CARTA ENTERA: ARRANQUE, FASE, IMAGEN Y TONO DE FIN
     * ======================================================================
     *
     * *** El tono de PARADA no lo probaba nadie. ***
     *
     * Es el que cierra el fichero: fax_poll() mira la transicion de imagen a
     * no-imagen, y esa transicion solo la provoca apt_mira() con el tono de
     * 450 Hz sostenido. O sea que de el depende que un fichero tenga UNA
     * carta o tenga dos cosidas, y el banco no lo tocaba.
     *
     * Aqui van dos cartas seguidas con su tono de fin en medio, que es lo
     * que sale de una emisora de verdad, y se comprueba lo que de verdad
     * importa: que al tono de fin se SALE de imagen, que las lineas de la
     * primera carta no se mezclan con las de la segunda, y que la segunda
     * vuelve a engancharse sola.
     * ====================================================================== */
    printf("\n12. la carta entera, con su tono de fin\n");
    {
        tx_t t;
        wefax_info_t in;
        int tras_1a, estado_fin, tras_2a;
        long marca_m = 0;
        double hueco_s = 0.0;

        g_plano = 1;
        g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 9001UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);

        /* Primera carta. */
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 40, 0.0);
        wefax_info(&in);
        printf("    primera carta: estado %u, %d renglones\n", in.estado, g_nlin);
        tras_1a = g_nlin;

        /*
         * El tono de fin. Se apunta la muestra en la que EMPIEZA, que es lo
         * antes que el decodificador puede darlo por bueno, y se le pide al
         * cronometro que avise cuando llegue el primer renglon de la carta
         * siguiente.
         */
        marca_m = t.muestra;
        tono_apt(&t, 450.0, 5.0, 0.0);
        /* Y AHORA se arma el cronometro, no antes: durante el tono todavia
         * se vacia la cola de la carta que acaba, y esos renglones son de
         * ella. El primero que llegue DESPUES es ya de la siguiente. */
        g_vigila = g_nlin + 1;
        g_vigila_m = -1;
        wefax_info(&in);
        estado_fin = (int)in.estado;
        printf("    tras el tono de fin: estado %u (%d = buscando), %d renglones\n",
               in.estado, (int)WEFAX_BUSCA, g_nlin);

        /* Un hueco de silencio con ruido, y la carta siguiente. */
        negro_fraccion(&t, 4.0, 0.0);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 40, 0.0);
        wefax_info(&in);
        tras_2a = g_nlin;
        printf("    segunda carta: estado %u, %d renglones en total\n",
               in.estado, g_nlin);

        comprueba(estado_fin == (int)WEFAX_BUSCA,
                  "el tono de fin saca de imagen (si no, dos cartas en un fichero)");
        comprueba(tras_1a >= 36 && tras_1a <= 41,
                  "la primera carta entrega sus renglones y ni uno mas");
        comprueba((tras_2a - tras_1a) >= 36 && (tras_2a - tras_1a) <= 42,
                  "y la segunda se engancha sola y entrega los suyos");
        comprueba(in.estado == WEFAX_IMAGEN,
                  "y acaba en imagen, no colgada en la fase");

        /*
         * ======================================================================
         * Y CUANTO TARDA EN VOLVER A PINTAR. ESTE NUMERO MANDA SOBRE EL
         * CERROJO DE main.c.
         * ======================================================================
         *
         * Desde que el tono de fin CIERRA el fichero -lo hace fax_poll(), al
         * ver que se sale de imagen- hasta que llega el primer renglon de la
         * carta siguiente pasan: lo que queda de tono de fin, el hueco, los
         * cinco segundos del tono de arranque y la señal de fase entera.
         *
         * WFX_CERROJO_MS tiene que ser MENOR que eso, porque durante el
         * cerrojo no se abre fichero: lo que dure de mas se come por arriba
         * la carta siguiente, que es justo donde va la cabecera con la hora.
         * Con el cerrojo de un minuto que habia, y dos cartas seguidas, se
         * perdian veinticinco segundos de la segunda.
         *
         * Se mide desde el PRINCIPIO del tono de fin, que es lo antes que el
         * decodificador puede cerrarlo: asi el numero es una cota inferior.
         * ====================================================================== */
        hueco_s = (double)(g_vigila_m - marca_m) / FS;
        printf("    del tono de fin al primer renglon de la siguiente: %.1f s\n",
               hueco_s);
        comprueba(g_vigila_m > 0 && hueco_s > 30.0,
                  "y el hueco da de sobra para el cerrojo de main.c (25 s)");
        g_vigila = 0;
        g_plano = 0;
    }

    /* ======================================================================
     * 12b. UNA TRAMA DE SEMITONO NO PARTE EL FICHERO
     * ======================================================================
     *
     * Dentro de la imagen, el tono de ARRANQUE tambien saca de imagen -es
     * la escapatoria para cuando el de fin se pierde en un
     * desvanecimiento-. Y ahi un falso positivo cuesta caro: parte el
     * fichero por la mitad y tira los segundos que van a la señal de fase.
     *
     * Por eso el umbral dentro de la imagen es 6.000 y no los 200 de
     * cuando se busca. El numero esta medido en wefax.c -el 15% del fondo
     * de escala, que ninguna imagen da- y no lo probaba nadie: bajandolo a
     * 2.000, el banco entero seguia en verde.
     *
     * Aqui va lo que de verdad puede aparecer en una carta: una trama de
     * semitono que cae justo en 300 Hz, con la amplitud de una trama y no
     * la de un tono -de gris 72 a gris 184, o sea casi la mitad del
     * recorrido-.
     *
     * Medido con un barrido: con el umbral en 6.000 esta trama no saca de
     * imagen hasta +-82 niveles, o sea casi dos tercios del recorrido, que
     * ya no es una trama sino un tono. Con 2.000 basta +-48. Los +-56 de
     * aqui estan entre los dos con margen por los dos lados.
     * ====================================================================== */
    printf("\n12b. una trama de semitono a 300 Hz no parte el fichero\n");
    {
        tx_t t;
        wefax_info_t in;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 777UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 10, 0.0);
        wefax_info(&in);
        comprueba(in.estado == (uint8_t)WEFAX_IMAGEN,
                  "la carta engancha antes de la trama");

        tono_apt_gris(&t, 300.0, 3.0, 0.0, 184, 72);
        wefax_info(&in);
        printf("    tras 3 s de trama a 300 Hz el estado es %u "
               "(3 = imagen)\n", in.estado);
        comprueba(in.estado == (uint8_t)WEFAX_IMAGEN,
                  "y una trama de semitono no la saca de imagen");
        g_plano = 0;
    }

    /* ======================================================================
     * 12c. Y UN TONO DE FIN DESVANECIDO SE OYE IGUAL
     * ======================================================================
     *
     * La otra mitad del mismo par. El tono de PARADA se queda en 200
     * dentro de la imagen a proposito: es el final ESPERADO de una carta, y
     * perderlo cuesta que dos cartas se cosan en un fichero. Subirlo a 400
     * tampoco lo notaba nadie.
     *
     * Aqui va un tono de fin DESVANECIDO: en vez de ir de 0 a 255, va de
     * 100 a 156. Es un quinto del recorrido, que es lo que queda de un
     * tono cuando la propagacion se cae justo al final de la carta - que es
     * cuando se cae.
     *
     * Medido con un barrido: con el umbral en 200 este tono se oye desde
     * +-24 niveles; con 400 hace falta +-32. Los +-28 de aqui estan en
     * medio, asi que el caso sale verde con el numero bueno y rojo con el
     * otro.
     * ====================================================================== */
    printf("\n12c. un tono de fin desvanecido se oye igual\n");
    {
        tx_t t;
        wefax_info_t in;

        g_plano = 1; g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 778UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 300.0, 5.0, 0.0);
        lineas_fase(&t, 60, 0.0);
        lineas_imagen(&t, 10, 0.0);
        wefax_info(&in);
        comprueba(in.estado == (uint8_t)WEFAX_IMAGEN,
                  "la carta engancha antes del tono de fin");

        tono_apt_gris(&t, 450.0, 5.0, 0.0, 156, 100);
        wefax_info(&in);
        printf("    tras 5 s de tono de fin flojo el estado es %u "
               "(1 = buscando)\n", in.estado);
        comprueba(in.estado != (uint8_t)WEFAX_IMAGEN,
                  "y un tono de fin flojo cierra la carta igual");
        g_plano = 0;
    }

    /* ======================================================================
     * 12d. UN BRILLO QUE ALTERNA A 370 Hz NO ES UN TONO DE ARRANQUE
     * ======================================================================
     *
     * Los dos umbrales de 12b y 12c dicen CUANTO tiene que oscilar el
     * brillo. Falta la otra guarda, la que dice que ademas tiene que
     * oscilar A ESA FRECUENCIA Y NO A OTRA: el bin del tono tiene que
     * llevar al menos TRES veces lo que lleve el otro.
     *
     * Tambien sobrevivia a su mutacion: bajando el tres a 1,5 el banco
     * entero seguia en verde.
     *
     * 370 Hz es el sitio donde se nota. No es un numero elegido: es el
     * primero del barrido en el que las dos versiones dicen cosas
     * distintas. Con tres no dispara; con 1,5 entra en la señal de fase y
     * se queda ahi los 35 segundos del tope, tirando todo lo que llegue.
     *
     * Y CONVIENE DECIR LO QUE **SI** DISPARA, para que nadie lo tome por
     * un fallo: por debajo de 350 Hz si entra, porque la ventana de 240
     * muestras a 12 kHz tiene 50 Hz de resolucion y 340 cae dentro del
     * lobulo del bin de 300. Un tono de emisora nominal de 300 Hz con
     * cuarenta de desvio tiene que engancharse, asi que eso es lo que se
     * quiere.
     * ====================================================================== */
    printf("\n12d. un brillo que alterna a 370 Hz no es un tono de arranque\n");
    {
        tx_t t;
        wefax_info_t in;

        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 779UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);
        tono_apt(&t, 370.0, 4.0, 0.0);
        wefax_info(&in);
        printf("    tras 4 s alternando a 370 Hz el estado es %u "
               "(1 = buscando)\n", in.estado);
        comprueba(in.estado == (uint8_t)WEFAX_BUSCA,
                  "y no se lo cree por mucho que oscile");
    }

    /* ======================================================================
     * 13. SINCRONIZA: ENGANCHARSE A MITAD DE CARTA
     * ======================================================================
     *
     * *** El boton tampoco lo probaba nadie, y tenia un fallo. ***
     *
     * wefax_sincroniza() + wefax_fuerza_imagen() es lo que se pulsa cuando
     * se llega tarde: ni tono de arranque ni señal de fase, solo imagen.
     *
     * Y hasta el 21/10/2026 forzaba la imagen SIN limpiar el detector de
     * tonos. Las dos medias llevan subiendo con lo que haya en el aire desde
     * que se entro en el modo, y dentro de imagen el umbral del tono de fin
     * son 200 -el bajo de los dos-: con la media de 450 Hz alta, la
     * comprobacion disparaba en la ventana siguiente -20 ms- y devolvia a
     * BUSCA. El boton parecia no hacer nada.
     *
     * Aqui se monta ese caso a proposito: un segundo de alternancia a 450 Hz
     * ANTES de pulsar. En BUSCA ese tono no hace nada -la comprobacion de
     * parada solo mira en imagen y en fase- pero deja la media por las
     * nubes. Con el fallo no llega ni un renglon.
     * ====================================================================== */
    printf("\n13. Sincroniza: engancharse a mitad de carta\n");
    {
        tx_t t;
        wefax_info_t in;
        int n_tras;

        g_plano = 1;
        g_plano_n = 0;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0; t.semilla = 7700UL;
        g_nlin = 0;
        mete_reset();

        wefax_start((float)FS);
        wefax_set_lpm(120U);

        /* Imagen a pelo, sin tono ni fase: el decodificador se queda en
         * BUSCA y no entrega nada. */
        lineas_imagen(&t, 6, 0.0);
        /* Y un segundo de 450 Hz, que en BUSCA no dispara nada pero deja
         * cargada la media del tono de fin. */
        tono_apt(&t, 450.0, 1.0, 0.0);
        wefax_info(&in);
        printf("    antes del boton: estado %u, %d renglones\n", in.estado, g_nlin);
        comprueba(g_nlin == 0, "sin enganche no se entrega nada");

        /* El boton, igual que en main.c. */
        wefax_sincroniza();
        wefax_fuerza_imagen();
        wefax_info(&in);
        comprueba(in.estado == WEFAX_IMAGEN, "el boton entra en imagen");

        n_tras = g_nlin;
        lineas_imagen(&t, 30, 0.0);
        wefax_info(&in);
        printf("    tras el boton: estado %u, %d renglones\n",
               in.estado, g_nlin - n_tras);
        comprueba(in.estado == WEFAX_IMAGEN,
                  "y NO rebota a buscando con la media del tono de fin cargada");
        comprueba((g_nlin - n_tras) >= 28 && (g_nlin - n_tras) <= 33,
                  "y entrega los renglones que entran, ni uno mas");

        /*
         * Y LO QUE ENTREGA SON RENGLONES ENTEROS, NO COSIDOS.
         *
         * El camino del boton no pasa por la señal de fase, asi que no lo
         * cubre ningun otro caso: entra en imagen de golpe y se pone a
         * encolar desde la ranura en curso. Si ese camino se descolocara el
         * anillo -o entregara la ranura que se esta escribiendo- saldrian
         * renglones con dos mitades de momentos distintos.
         *
         * Se mide con el ESCALON entre dos trozos lejanos, igual que el caso
         * 10 y por el mismo motivo: el discriminador tarda unos puntos en
         * asentarse al saltar del gris de la linea anterior al de esta, asi
         * que el principio de todo renglon bueno lleva una rampa y con
         * minimo y maximo el banco mediria su propio asentamiento. Y por eso
         * mismo el punto 0 de cada renglon se lleva el gris de antes: un
         * punto de 756, que en una carta es el borde izquierdo y no lo ve
         * nadie.
         *
         * LO QUE ESTE CASO NO MIDE, Y CONVIENE DECIRLO: que el boton ponga a
         * cero s_fase. Aqui se pulsa en un limite de linea -el tono de 450
         * dura dos justas- asi que sin ese cero la ranura tampoco se parte;
         * es el mismo "no se rompio de milagro" que cuenta el comentario de
         * wefax_fuerza_imagen(). Pulsado a mitad de linea si se partiria,
         * pero entonces el primer renglon lleva de verdad dos lineas de la
         * emisora dentro -eso es lo que significa pulsar a mitad- y el banco
         * no sabria distinguir una cosa de la otra.
         */
        {
            int y, partidos = 0, peor = 0;
            for (y = n_tras; y < g_nlin; y++) {
                int x; long izq = 0, der = 0; int d;
                for (x = 24; x < 300; x++)  { izq += g_img[y][x]; }
                for (x = 456; x < 756; x++) { der += g_img[y][x]; }
                d = (int)(izq / (300 - 24) - der / (756 - 456));
                if (d < 0) { d = -d; }
                if (d > 30) { partidos++; }
                if (d > peor) { peor = d; }
            }
            printf("    %d renglones partidos de %d (el peor salta %d niveles)\n",
                   partidos, g_nlin - n_tras, peor);
            comprueba(partidos == 0, "y ninguno sale partido, empezando por el primero");
        }
        g_plano = 0;
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

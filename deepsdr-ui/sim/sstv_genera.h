#ifndef SSTV_GENERA_H
#define SSTV_GENERA_H
/*
 * EL GENERADOR DE SEÑAL DE SSTV, COMPARTIDO POR LOS BANCOS - 11/10/2026.
 *
 * Estaba dentro de sim/sstvtest.c y lo necesita tambien sim/sstv_auto.c, que
 * comprueba la deteccion automatica del modo. Copiarlo habria dejado dos
 * generadores que se pueden desincronizar, y entonces un banco mediria una
 * señal que el otro no emite.
 *
 * NO LLEVA NI UN TIEMPO PROPIO: los saca todos de sstv_plan(), que es de
 * donde los saca tambien el decodificador. Lo unico que sabe de su cuenta es
 * QUE significa cada canal.
 *
 * Es una cabecera con funciones `static` a proposito: son cinco funciones y
 * dos arrays, los dos bancos son programas de un solo fichero objeto, y asi
 * no hay que tocar las reglas de enlazado de ninguno.
 *
 * Quien lo incluya tiene que haber definido FS (la tasa de muestreo del
 * banco) y haber incluido sstv.h, stdio.h, string.h y math.h.
 */
/* ------------------------------------------------------------- el patron */
/*
 * Un patron con los tres colores bien separados, para que un intercambio de
 * canales se vea a la primera y se pueda MEDIR, no mirar:
 *   - franja izquierda roja pura, centro verde puro, derecha azul puro
 *   - y una banda gris en las lineas de en medio, para ver el brillo
 */
static void patron(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (y >= 120 && y < 136) { *r = *g = *b = (uint8_t)(x * 255 / 319); return; }
    /* Anchos DISTINTOS a proposito: con tres franjas iguales, cambiar los
     * canales de sitio y desplazar la imagen un tercio dan la misma foto, y
     * el banco no podria decir cual de los dos fallos esta viendo. */
    if (x < 60)       { *r = 230; *g = 20;  *b = 20; }
    else if (x < 200) { *r = 20;  *g = 230; *b = 20; }
    else              { *r = 20;  *g = 20;  *b = 230; }
    if ((y % 32) < 4) { *r = 255; *g = 255; *b = 255; }
}

/*
 * EL DESVIO DE SINTONIA DEL BANCO - 06/10/2026.
 *
 * Se suma a TODOS los tonos, cabecera incluida, que es justo lo que hace una
 * radio mal sintonizada: no toca los tiempos, solo mueve las frecuencias. Por
 * eso el fallo del usuario era tan dificil de ver desde fuera -el periodo de
 * linea salia perfecto y la radio no arrancaba-.
 */
static double g_off = 0.0;

/* ------------------------------------------------------------- la señal */
typedef struct {
    double fase;
    double ppm;           /* error de reloj del transmisor */
    unsigned long semilla;
    double sigma;
} tx_t;

static uint8_t g_img[SSTV_ALTO][SSTV_ANCHO][3];
static int g_max_y = -1;

static double ruido(tx_t *t)
{
    double a, b;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    a = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    b = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    return a + b;
}

/*
 * Emite `seg` segundos del tono `hz`.
 *
 * El tiempo se acumula y de ahi sale CUANTAS MUESTRAS LLEVAMOS EN TOTAL; no
 * se redondea la duracion de cada tramo por separado. La primera version
 * hacia lo segundo y un punto de imagen de Martin 1 dura 5,49 muestras a 12
 * kHz: redondeado a 5, el barrido entero salia un 9% mas corto de lo que el
 * propio modo dice que dura. El decodificador leia cada canal donde no
 * estaba y la foto salia con seis franjas de color en vez de tres.
 *
 * Y el fallo era del BANCO, no del decodificador. Un generador que no emite
 * lo que dice emitir convierte cualquier medida en una opinion.
 */
static long   g_n = 0;      /* muestras emitidas */
static double g_t = 0.0;    /* tiempo que deberian sumar */

/*
 * El medio bloque que se queda sin mandar al acabar una prueba, y que se
 * colaba al principio de la siguiente. Con una sola prueba por ejecucion no
 * se notaba; encadenando varias con desvios distintos, la segunda arrancaba
 * con muestras de la primera y no enganchaba. 06/10/2026.
 */
static void emite_reinicia(void);

static float g_buf[64];
static int   g_j = 0;
static void emite_reinicia(void) { g_j = 0; }

/*
 * EL AUDIO QUE NO LLEGA - 13/10/2026.
 *
 * Pedido por el usuario.
 *
 * En la radio eso pasa de verdad y tiene nombre: si la interrupcion del
 * audio llega tarde -mas de media vuelta del DMA- las dos banderas estan
 * puestas a la vez, la mitad vieja ya la esta pisando el DMA y SE PIERDE.
 * Lo cuenta sdr_rx_get_tarde(). La señal del aire sigue siendo perfecta;
 * lo que tiene un agujero es lo que el decodificador llega a ver.
 *
 * Aqui se imita exactamente eso: `g_perder` bloques de 64 muestras se
 * GENERAN -la fase sigue corriendo, como en el aire- pero no se le dan al
 * decodificador. Poner ceros en su sitio seria otra cosa distinta y mas
 * benigna: el tiempo no se pararia.
 */
static long g_perder = 0;     /* bloques de 64 que no se entregan */
static long g_perdidos = 0;   /* cuantos se han tirado ya */

static void emite(tx_t *t, double hz, double seg)
{
    float *buf = g_buf;
    int   *pj  = &g_j;
    int    j   = *pj;
    const uint8_t *l;
    uint16_t y;
    long objetivo;

    g_t += seg;
    objetivo = (long)(g_t * FS * (1.0 - t->ppm * 1e-6) + 0.5);

    while (g_n < objetivo) {
        t->fase += 2.0 * M_PI * (hz + g_off) / FS;
        if (t->fase > 2.0 * M_PI) { t->fase -= 2.0 * M_PI; }
        buf[j++] = (float)(sin(t->fase) + t->sigma * ruido(t));
        g_n++;
        if (j == 64) {
            if (g_perder > 0L) { g_perder--; g_perdidos++; }
            else               { sstv_process(buf, 64U); }
            j = 0;
            while ((l = sstv_linea(&y)) != 0) {
                if (y < SSTV_ALTO) {
                    memcpy(g_img[y], l, SSTV_ANCHO * 3U);
                    if ((int)y > g_max_y) { g_max_y = (int)y; }
                }
            }
        }
    }
    *pj = j;
}

static void cabecera_vis(tx_t *t, uint8_t vis)
{
    int i, par = 0;

    emite(t, 1900.0, 0.300);
    emite(t, 1200.0, 0.010);
    emite(t, 1900.0, 0.300);
    emite(t, 1200.0, 0.030);            /* arranque */
    for (i = 0; i < 7; i++) {
        int b = (vis >> i) & 1;
        par ^= b;
        emite(t, b ? 1100.0 : 1300.0, 0.030);
    }
    emite(t, par ? 1100.0 : 1300.0, 0.030);   /* paridad PAR */
    emite(t, 1200.0, 0.030);            /* parada */
}

/* De rojo, verde y azul a luminancia y diferencias, que es lo que mandan las
 * familias Robot y PD. La inversa de la del decodificador. */
static void rgb_a_yuv(uint8_t r, uint8_t g, uint8_t b, uint8_t *y, uint8_t *cr, uint8_t *cb)
{
    int yy = (299 * r + 587 * g + 114 * b) / 1000;
    int rr = 128 + (1000 * (r - yy)) / 1402;
    int bb = 128 + (1000 * (b - yy)) / 1772;
    if (rr < 0) { rr = 0; }
    if (rr > 255) { rr = 255; }
    if (bb < 0) { bb = 0; }
    if (bb > 255) { bb = 255; }
    *y = (uint8_t)yy; *cr = (uint8_t)rr; *cb = (uint8_t)bb;
}

/*
 * Una linea transmitida, construida con el MISMO plan que usa el
 * decodificador. El generador no sabe de familias: recorre los canales por
 * orden de tiempo, rellena los huecos entre ellos y emite cada barrido con lo
 * que le toque a ese canal. Lo unico que sabe aparte es QUE significa cada
 * canal, que es lo que dice el plan.
 */
static void linea(tx_t *t, uint8_t modo, int y)
{
    sstv_plan_t pl;
    int orden[4] = { 0, 1, 2, 3 };
    int i, j, x, n;
    double reloj;

    sstv_plan(modo, &pl);
    n = pl.n_ch;

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (pl.off[orden[j]] < pl.off[orden[i]]) {
                int k = orden[i]; orden[i] = orden[j]; orden[j] = k;
            }
        }
    }

    emite(t, 1200.0, pl.sync);
    reloj = pl.sync;

    for (i = 0; i < n; i++) {
        int c = orden[i];

        /* El hueco hasta este canal. En Robot 36 ese hueco DICE que croma
         * viene: 1500 Hz para R-Y y 2300 para B-Y. */
        if (pl.off[c] > reloj) {
            double hz = 1500.0;
            if (pl.alterna && c == 1) { hz = (y & 1) ? 2300.0 : 1500.0; }
            emite(t, hz, pl.off[c] - reloj);
        }

        for (x = 0; x < (int)SSTV_ANCHO; x++) {
            uint8_t r, g, b, v = 0;
            int yy = y;

            /* PD manda dos lineas de imagen: el canal 3 es la segunda. */
            if (pl.lineas_img == 2 && c == 3) { yy = y + 1; }
            patron(x, yy, &r, &g, &b);

            if (pl.color == SSTV_COLOR_GBR) {
                v = (c == 0) ? g : ((c == 1) ? b : r);
            } else {
                uint8_t ly, cr, cb;
                rgb_a_yuv(r, g, b, &ly, &cr, &cb);
                if (c == 0 || c == 3) { v = ly; }
                else if (pl.alterna)  { v = (y & 1) ? cb : cr; }
                else                  { v = (c == 1) ? cr : cb; }
            }
            emite(t, sstv_hz_de_nivel(v), pl.scan[c] / (double)SSTV_ANCHO);
        }
        reloj = pl.off[c] + pl.scan[c];
    }

    if (pl.linea > reloj) { emite(t, 1500.0, pl.linea - reloj); }
}


#endif /* SSTV_GENERA_H */

/*
 * CUANTO BAILA EL BORDE IZQUIERDO: LA CALIDAD DEL SINCRONISMO DE LINEA,
 * MEDIDA EN COLUMNAS.
 *
 * POR QUE EXISTE.
 *
 * "El sincronismo podria ir mejor" no es una medida. Lo que se ve en la foto
 * cuando el sincronismo baila es que las lineas no quedan alineadas: un
 * borde vertical de la imagen sale como una escalera. Eso SI se puede
 * contar.
 *
 * El patron del generador tiene un borde vertical limpio en x=60 - la franja
 * roja acaba y empieza la verde -. Como la señal se fabrica con el mismo
 * plan con el que se lee, ese borde TIENE que caer en la misma columna en
 * todas las lineas. Lo que se desvie es, exactamente, el error de fase del
 * sincronismo de esa linea convertido a columnas.
 *
 * Se mide por el cruce por cero de (verde - rojo) entre dos columnas, con
 * interpolacion lineal, asi que la resolucion no es de una columna entera:
 * un desplazamiento de un tercio de columna se ve.
 *
 * LO QUE SE PUBLICA ES LA DISPERSION, NO EL DESVIO MEDIO. Que la imagen
 * entera salga corrida media columna no es un fallo de sincronismo sino una
 * constante del muestreador; lo que estropea la foto es que cada linea salga
 * corrida DISTINTO. Por eso el numero que manda es la desviacion respecto a
 * la media, y el maximo.
 *
 * NO LLEVA VALORES ESPERADOS: es una herramienta de medir, no una
 * comprobacion, y por eso no cuelga de `make comprueba`.
 *
 *   make fase
 */
#include "sstv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS 12000.0
#include "sstv_genera.h"

/* El pozo de RAM prestada: copiado de hfdl_ram.h, no escrito de memoria.
 * Ver el comentario de sim/sstvaire.c. */
static unsigned char g_ram_pozo[0xD4A0];
void           hfdl_ram_coge(void) { }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { }

/*
 * Donde cae el borde rojo->verde de la linea `y`, en columnas, o -1 si en
 * esa linea no hay borde que medir.
 *
 * Las lineas blancas del patron (y % 32 < 4) no tienen borde: son blancas
 * enteras. Hay que saltarlas o se mide ruido.
 */
static double borde(int y)
{
    int x, mejor = -1;
    double dmax = 0.0;

    if ((y % 32) < 4)            { return -1.0; }
    if (y >= 120 && y < 136)     { return -1.0; }   /* la banda de grises */

    /* El salto mas grande de (verde - rojo) en la zona del borde. */
    for (x = 40; x < 85; x++) {
        double a = (double)g_img[y][x][1]     - (double)g_img[y][x][0];
        double b = (double)g_img[y][x + 1][1] - (double)g_img[y][x + 1][0];
        if ((b - a) > dmax) { dmax = b - a; mejor = x; }
    }
    if (mejor < 0 || dmax < 60.0) { return -1.0; }

    /* Y el cruce por cero entre esas dos columnas, interpolado. */
    {
        double a = (double)g_img[y][mejor][1]     - (double)g_img[y][mejor][0];
        double b = (double)g_img[y][mejor + 1][1] - (double)g_img[y][mejor + 1][0];
        if (b == a) { return (double)mejor + 0.5; }
        return (double)mejor + (0.0 - a) / (b - a);
    }
}

static void mide(uint8_t modo, int lineas, double sigma, double ppm)
{
    tx_t t;
    sstv_info_t in;
    sstv_plan_t pl;
    double v[SSTV_ALTO];
    int n = 0, y;
    double media = 0.0, rms = 0.0, peor = 0.0;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL; t.ppm = ppm; t.sigma = sigma;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1;
    g_n = 0; g_t = 0.0;

    emite_reinicia();
    sstv_start((float)FS);
    cabecera_vis(&t, sstv_modo_vis(modo));
    sstv_plan(modo, &pl);
    for (y = 0; y < lineas; y += pl.lineas_img) { linea(&t, modo, y); }
    sstv_info(&in);

    for (y = 2; y <= g_max_y; y++) {
        double b = borde(y * (int)pl.vdec);
        if (b > 0.0) { v[n++] = b; }
    }
    sstv_stop();

    if (n < 8) {
        printf("    %-10s ruido %.2f  ppm %+5.0f   NO HAY LINEAS QUE MEDIR (%d)\n",
               sstv_modo_nombre(modo), sigma, ppm, n);
        return;
    }

    for (y = 0; y < n; y++) { media += v[y]; }
    media /= (double)n;
    for (y = 0; y < n; y++) {
        double d = v[y] - media;
        rms += d * d;
        if (fabs(d) > peor) { peor = fabs(d); }
    }
    rms = sqrt(rms / (double)n);

    printf("    %-10s ruido %.2f  ppm %+5.0f   lineas %3d   borde medio %6.2f   "
           "baile %.3f col (peor %.2f)   sincs malos %u\n",
           sstv_modo_nombre(modo), sigma, ppm, n, media, rms, peor,
           (unsigned)in.sincs_malos);
}

/*
 * ==========================================================================
 * EL UMBRAL DE SEÑAL/RUIDO: HASTA DONDE DECODIFICA EL SSTV - 05/11/2026.
 * ==========================================================================
 *
 * Pedido por un usuario de fuera: saber a que relacion señal/ruido deja de
 * decodificar.
 *
 * Y LA RESPUESTA NO ES UN NUMERO, SON TRES, porque no hay una sola cosa que
 * se rompa. En orden de fragilidad:
 *
 *   1. LA CABECERA VIS, que dice que modo viene. Se juega en tramos de 30 ms
 *      y es lo primero que cae. Peor aun: no falla callandose, falla
 *      diciendo OTRO MODO -un bit mal leido y Martin 1 pasa a Martin 2-.
 *   2. EL MODO POR EL RITMO, que es la red de debajo: sin cabecera, mide la
 *      distancia entre sincronismos y deduce el modo.
 *   3. LA IMAGEN. Aguanta mucho mas que las dos de arriba, porque cada punto
 *      promedia varias muestras y el sincronismo va linea a linea.
 *
 * COMO SE CUENTA LA RELACION SEÑAL/RUIDO, Y POR QUE SE MIDE EN VEZ DE
 * CALCULARLA. La señal es un seno de amplitud 1 -potencia 1/2- y el ruido es
 * `sigma` por la suma de dos uniformes. Se puede sacar con lapiz que la
 * varianza de esa suma es 1/6, pero un numero publicado que sale de mi lapiz
 * y no del generador es justo lo que este proyecto no admite: aqui se MIDE
 * el ruido que el generador fabrica de verdad, con su misma semilla y su
 * misma funcion.
 *
 * Y SE DAN DOS CIFRAS, no una, porque una sola no significa nada sin decir
 * en que ancho de banda:
 *
 *   - EN TODA LA BANDA MUESTREADA (0 a 6 kHz a 12 kHz de muestreo). Es la
 *     honrada para comparar bancos entre si.
 *   - EN 3 kHz, que es el ancho de un filtro de banda lateral y es la cifra
 *     que usa cualquiera que hable de esto en una radio. El ruido es blanco,
 *     asi que en la mitad de la banda hay la mitad de la potencia: son 3 dB
 *     mas. Esa diferencia va dicha y no escondida.
 *
 * LO QUE ESTO NO ES. El ruido de aqui es casi gaussiano y plano. El de la
 * banda de verdad no: lleva impulsos, portadoras y desvanecimientos, y en
 * sim/sstvtest.c ya hay una nota sobre que eso hace que los numeros del
 * banco y los del aire no sean el mismo numero. Estos umbrales valen para
 * COMPARAR -una version contra otra, un modo contra otro-, no para
 * prometerle a nadie cuantos dB necesita su antena.
 */
static double ruido_rms(void)
{
    /* El ruido que el generador fabrica DE VERDAD, no el que yo creo que
     * fabrica: su misma funcion y su misma semilla. */
    tx_t t;
    double s = 0.0;
    long i, n = 400000L;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL;
    for (i = 0; i < n; i++) { double r = ruido(&t); s += r * r; }
    return sqrt(s / (double)n);
}

/* dB de señal/ruido para un `sigma` dado, con el ruido medido. La señal es
 * un seno de amplitud 1, cuyo valor eficaz es 1/raiz(2) por definicion. */
static double snr_db(double sigma, double nrms, int en_3k)
{
    double ps = 0.5;                       /* potencia del seno */
    double pn = (sigma * nrms) * (sigma * nrms);
    if (pn <= 0.0) { return 99.0; }
    if (en_3k) { pn *= 0.5; }              /* medio ancho de banda, medio ruido */
    return 10.0 * log10(ps / pn);
}

/*
 * UNA FILA DEL BARRIDO.
 *
 * `forzar` separa las dos preguntas, y sin eso el barrido no mide lo que
 * dice medir:
 *
 *   forzar = 0  -> la radio se entera del modo por la cabecera, como en el
 *                  aire. Mide LA CABECERA.
 *   forzar = 1  -> el modo se pone a mano con sstv_fuerza(). Mide LA IMAGEN,
 *                  con la deteccion de modo fuera de la ecuacion.
 *
 * POR QUE HACE FALTA: en la primera pasada, en cuanto la cabecera falla el
 * error de imagen se dispara a 37 de 255 CON EL BAILE DEL BORDE EN 0,11
 * COLUMNAS. Un sincronismo perfecto y una imagen mala a la vez no tiene
 * sentido... hasta que se mira por que: sin cabecera, el decodificador
 * engancha a mitad de imagen y el renglon 0 de lo decodificado NO es el
 * renglon 0 del patron. El error que se mide ahi es el desplazamiento
 * vertical, no la calidad. Por eso el error solo se publica cuando la
 * referencia esta bien puesta.
 */
static void umbral(uint8_t modo, double sigma, double nrms, uint8_t forzar)
{
    tx_t t;
    sstv_info_t in;
    sstv_plan_t pl;
    double v[SSTV_ALTO];
    int n = 0, y, lineas = 96;
    double rms = 0.0, media = 0.0, err;
    const char *leido;
    char vis_ok;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL; t.ppm = 0.0; t.sigma = sigma;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1;
    g_n = 0; g_t = 0.0;

    emite_reinicia();
    sstv_start((float)FS);
    cabecera_vis(&t, sstv_modo_vis(modo));

    /* Que dijo la cabecera, ANTES de mandar la imagen: eso es lo que se
     * esta midiendo. Despues, el modo podria haberlo corregido el ritmo. */
    sstv_info(&in);
    leido = in.modo;
    vis_ok = (char)(leido && strcmp(leido, sstv_modo_nombre(modo)) == 0);

    /* Y si se mide la imagen, el modo se pone a mano DESPUES de leer que
     * dijo la cabecera, para no borrar el dato que acabamos de recoger. */
    if (forzar) { sstv_fuerza(modo); }

    sstv_plan(modo, &pl);
    for (y = 0; y < lineas; y += pl.lineas_img) { linea(&t, modo, y); }
    sstv_info(&in);

    for (y = 2; y <= g_max_y; y++) {
        double b = borde(y * (int)pl.vdec);
        if (b > 0.0) { v[n++] = b; }
    }
    err = error_medio(g_max_y > 0 ? g_max_y : 1, pl.vdec);
    sstv_stop();

    if (n >= 4) {
        for (y = 0; y < n; y++) { media += v[y]; }
        media /= (double)n;
        for (y = 0; y < n; y++) { double d = v[y] - media; rms += d * d; }
        rms = sqrt(rms / (double)n);
    }

    printf("   %5.1f %5.1f  %-10s %-9s %4d  ",
           snr_db(sigma, nrms, 0), snr_db(sigma, nrms, 1),
           sstv_modo_nombre(modo),
           vis_ok ? "bien" : (leido ? leido : "(nada)"),
           g_max_y + 1);
    /* El error solo vale con la referencia bien puesta - ver arriba. */
    if (forzar || vis_ok) { printf("%5.0f", err); } else { printf("    -"); }
    if (n >= 4) { printf("%7.2f", rms); } else { printf("      -"); }
    printf("  %5u\n", (unsigned)in.sincs_malos);
}

/*
 * ==========================================================================
 * CUANTAS VECES MIENTE LA CABECERA - 05/11/2026.
 * ==========================================================================
 *
 * El barrido de arriba enseño algo que no se buscaba: justo por debajo de su
 * umbral, el VIS NO SE CALLA, dice otro modo. Y con una sola semilla eso es
 * una anecdota, no una medida. Esto lo cuenta con veinte semillas por punto
 * y reparte el resultado en tres cajas:
 *
 *   BIEN   - leyo el modo que venia
 *   NADA   - no leyo ninguno (la paridad no cuadro, o no llego a los bits)
 *   MIENTE - leyo OTRO modo, con su paridad cuadrando
 *
 * Y LA TERCERA ES LA QUE IMPORTA, porque no es "una de cal y otra de arena":
 * un modo que viene del VIS marca s_info.como = SSTV_COMO_VIS, y eso
 * PROHIBE que la correccion por ritmo lo cambie (ver auto_pulso()). O sea
 * que una cabecera mal leida no se corrige sola nunca: la foto entera sale
 * mal y no hay salida. Una cabecera que no se lee, en cambio, deja trabajar
 * al ritmo.
 *
 * Por eso MIENTE es peor que NADA, y por eso se cuentan por separado.
 */
static void miente(uint8_t modo, double sigma, double nrms, int semillas)
{
    int bien = 0, nada = 0, mal = 0, k;
    char lista[80];
    int  li = 0;

    lista[0] = '\0';
    for (k = 0; k < semillas; k++) {
        tx_t t;
        sstv_info_t in;

        memset(&t, 0, sizeof t);
        t.semilla = (unsigned long)(1000 + k * 7919);   /* primo, para que no se repitan */
        t.ppm = 0.0; t.sigma = sigma;
        g_n = 0; g_t = 0.0; g_max_y = -1;
        emite_reinicia();
        sstv_start((float)FS);
        cabecera_vis(&t, sstv_modo_vis(modo));
        sstv_info(&in);
        sstv_stop();

        if (!in.modo)                                        { nada++; }
        else if (strcmp(in.modo, sstv_modo_nombre(modo)) == 0) { bien++; }
        else {
            mal++;
            if (li < 60) {
                li += snprintf(&lista[li], sizeof lista - (size_t)li, "%s%s",
                               li ? "," : "", in.modo);
            }
        }
    }
    printf("   %5.1f %5.1f  %-10s  %3d  %3d  %3d   %s\n",
           snr_db(sigma, nrms, 0), snr_db(sigma, nrms, 1),
           sstv_modo_nombre(modo), bien, nada, mal, lista);
}

/*
 * ==========================================================================
 * Y COMO ACABA LA FOTO: LA PRUEBA DE QUE NO BLOQUEAR SIRVE - 05/11/2026.
 * ==========================================================================
 *
 * El apartado 6 solo manda la cabecera, asi que mide cuantas veces miente
 * pero NO si eso se puede arreglar despues: auto_pulso() necesita
 * sincronismos, y ahi no hay ninguno.
 *
 * Esto manda cabecera Y imagen, con las mismas veinte semillas, y mira el
 * modo AL FINAL. La diferencia entre las dos tablas es, exactamente, lo que
 * hace el arreglo: una cabecera mentirosa que antes era definitiva ahora la
 * desautoriza el ritmo.
 */
static void acaba(uint8_t modo, double sigma, double nrms, int semillas)
{
    int bien = 0, nada = 0, mal = 0, k;
    double err_tot = 0.0;
    int    err_n = 0;

    for (k = 0; k < semillas; k++) {
        tx_t t;
        sstv_info_t in;
        sstv_plan_t pl;
        int y;

        memset(&t, 0, sizeof t);
        t.semilla = (unsigned long)(1000 + k * 7919);
        t.ppm = 0.0; t.sigma = sigma;
        memset(g_img, 0, sizeof g_img);
        g_n = 0; g_t = 0.0; g_max_y = -1;
        emite_reinicia();
        sstv_start((float)FS);
        cabecera_vis(&t, sstv_modo_vis(modo));
        sstv_plan(modo, &pl);
        for (y = 0; y < 64; y += pl.lineas_img) { linea(&t, modo, y); }
        sstv_info(&in);

        if (!in.modo)                                          { nada++; }
        else if (strcmp(in.modo, sstv_modo_nombre(modo)) == 0) {
            bien++;
            err_tot += error_medio(g_max_y > 0 ? g_max_y : 1, pl.vdec);
            err_n++;
        }
        else { mal++; }
        sstv_stop();
    }
    printf("   %5.1f %5.1f  %-10s  %3d  %3d  %3d   ",
           snr_db(sigma, nrms, 0), snr_db(sigma, nrms, 1),
           sstv_modo_nombre(modo), bien, nada, mal);
    if (err_n) { printf("%5.0f\n", err_tot / (double)err_n); }
    else       { printf("    -\n"); }
}

int main(void)
{
    static const double k_sig[] = { 0.0, 0.10, 0.25, 0.50, 1.00 };
    unsigned i;

    printf("BAILE DEL BORDE IZQUIERDO (columnas de 320). Menos es mejor.\n\n");

    printf("1. Martin 1, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(0U, 128, k_sig[i], 0.0); }

    printf("\n2. Scottie 1, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(2U, 128, k_sig[i], 0.0); }

    printf("\n3. PD120, 128 lineas\n");
    for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) { mide(9U, 128, k_sig[i], 0.0); }

    printf("\n4. Martin 1 con el reloj del transmisor desviado\n");
    mide(0U, 128, 0.25, -500.0);
    mide(0U, 128, 0.25,  500.0);

    {
        static const double k_sig[] = {
            0.02, 0.05, 0.08, 0.12, 0.16, 0.20, 0.25, 0.32,
            0.40, 0.50, 0.63, 0.80, 1.00, 1.30, 1.60
        };
        static const uint8_t k_modo[] = { 0U, 2U, 9U };
        double nrms = ruido_rms();
        unsigned i, m;

        printf("\n5. EL UMBRAL DE SEÑAL/RUIDO\n\n");
        printf("   ruido del generador medido: %.4f eficaz por unidad de sigma\n",
               nrms);
        printf("   (la señal es un seno de amplitud 1; su eficaz es %.4f)\n\n",
               1.0 / sqrt(2.0));
        printf("   (a) LA CABECERA MANDA, como en el aire\n\n");
        printf("   S/N dB          que dijo             error  baile  sincs\n");
        printf("   total   3kHz  modo       la cabecera lin  /255    col   malos\n");
        printf("   --------------------------------------------------------------\n");
        for (m = 0; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
                umbral(k_modo[m], k_sig[i], nrms, 0U);
            }
            printf("   --------------------------------------------------------------\n");
        }

        printf("\n   (b) EL MODO PUESTO A MANO: la imagen sola, sin la cabecera\n");
        printf("       de por medio. Es el caso de engancharse a mitad de foto\n");
        printf("       con el modo ya elegido.\n\n");
        printf("   S/N dB          que dijo             error  baile  sincs\n");
        printf("   total   3kHz  modo       la cabecera lin  /255    col   malos\n");
        printf("   --------------------------------------------------------------\n");
        for (m = 0; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
                umbral(k_modo[m], k_sig[i], nrms, 1U);
            }
            printf("   --------------------------------------------------------------\n");
        }
    }

    {
        static const double k_sig[] = { 0.12, 0.16, 0.20, 0.25, 0.32, 0.40, 0.50 };
        static const uint8_t k_modo[] = { 0U, 2U, 9U };
        double nrms = ruido_rms();
        unsigned i, m;

        printf("\n6. CUANTAS VECES MIENTE LA CABECERA (20 semillas por punto)\n\n");
        printf("   S/N dB                   veces de 20\n");
        printf("   total   3kHz  modo        bien nada MIENTE  que dijo\n");
        printf("   -----------------------------------------------------------\n");
        for (m = 0; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
                miente(k_modo[m], k_sig[i], nrms, 20);
            }
            printf("   -----------------------------------------------------------\n");
        }
    }

    {
        static const double k_sig[] = { 0.12, 0.16, 0.20, 0.25, 0.32 };
        static const uint8_t k_modo[] = { 0U, 2U, 9U };
        double nrms = ruido_rms();
        unsigned i, m;

        printf("\n7. COMO ACABA LA FOTO, con cabecera Y imagen (20 semillas)\n\n");
        printf("   S/N dB                  el modo AL FINAL    error\n");
        printf("   total   3kHz  modo        bien nada MAL     /255\n");
        printf("   ----------------------------------------------------\n");
        for (m = 0; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
                acaba(k_modo[m], k_sig[i], nrms, 20);
            }
            printf("   ----------------------------------------------------\n");
        }
    }

    return 0;
}

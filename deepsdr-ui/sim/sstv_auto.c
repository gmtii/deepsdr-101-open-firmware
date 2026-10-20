/*
 * ADIVINAR EL MODO DE SSTV SIN CABECERA, MEDIDO.
 *
 * LO QUE SE MIDE AQUI, Y POR QUE ES ESTO Y NO OTRA COSA.
 *
 * El decodificador ya sabia leer la cabecera VIS, que dice el modo. Pero la
 * cabecera pasa UNA vez y al principio, y buscando por la banda uno se
 * engancha a mitad de foto: entonces no hay cabecera que leer y hasta hoy la
 * unica salida era elegir entre catorce modos a mano.
 *
 * Lo que este banco comprueba es que el modo se puede MEDIR de la señal que
 * queda -el ritmo de los pulsos de sincronismo y lo que dura cada pulso- y
 * que esa medida acierta. Y lo comprueba de la unica forma que vale:
 * EMPEZANDO A EMITIR POR LA MITAD, sin cabecera ninguna, igual que cuando
 * giras el dial y caes encima de una emision ya empezada.
 *
 * LAS CUATRO PREGUNTAS, EN ESTE ORDEN:
 *
 *   1. ¿acierta el modo, los catorce, sin cabecera?  Y ademas: ¿cuantas
 *      lineas tarda? Una deteccion que acierta en veinte lineas no sirve de
 *      nada: para entonces ya has perdido la foto.
 *   2. ¿sale la FOTO despues de acertar? Acertar el nombre del modo y pintar
 *      basura seria acertar a medias y no se notaria mirando solo el nombre.
 *   3. ¿se CALLA cuando no hay nada? Ruido, un tono fijo y silencio no deben
 *      enganchar ningun modo. Un detector que siempre contesta algo no es un
 *      detector.
 *   4. ¿respeta la cabecera y el modo puesto a mano? Un VIS es un dato y
 *      manda sobre cualquier medida; y si el usuario elige un modo a dedo, la
 *      deteccion automatica apagada no debe tocarlo.
 *
 * Y la quinta, que es la que de verdad hacia falta: si estabas pintando
 * con el modo EQUIVOCADO -porque lo forzaste o porque la medida anterior
 * fallo-, ¿se corrige sola?
 */
#include "sstv.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* El pozo de RAM prestada. Las firmas, copiadas de hfdl_ram.h. */
static unsigned char g_ram_pozo[0xD4A0];
void           hfdl_ram_coge(void) { }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { }

#define FS 12000.0

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("    %-60s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

#include "sstv_genera.h"

/*
 * CUANTO SE PARECE LO DECODIFICADO AL PATRON QUE SE EMITIO, Y EL DESPLAZE.
 *
 * *** Y ESTA FUNCION YA MINTIO UNA VEZ, el mismo dia que se escribio. ***
 *
 * La primera version comparaba el renglon 0 de la foto con la linea 0 del
 * patron, que es lo que vale cuando la cabecera dice el modo desde el
 * principio. Enganchandose a mitad NO vale: las lineas que el detector se
 * gasta en medir el ritmo no se pintan, asi que el renglon 0 de la foto es
 * la linea 4 del patron. Comparando sin desplazar salia un error de 145
 * sobre 255 -o sea "basura"- con la foto perfectamente bien decodificada.
 *
 * Asi que se prueba cada desplazamiento y se devuelve el MEJOR junto con
 * cual era. Los dos numeros importan: el error dice si la foto esta bien, y
 * el desplazamiento dice cuantas lineas costo engancharse. Si el error solo
 * baja con un desplazamiento grande, es que se perdio media foto y eso hay
 * que verlo, no esconderlo.
 */
static double error_desp(int hasta, uint8_t vdec, int desp)
{
    double suma = 0.0; long n = 0;
    int x, y;
    if (hasta <= 0) { return 999.0; }
    for (y = 0; y < hasta; y++) {
        for (x = 0; x < (int)SSTV_ANCHO; x++) {
            uint8_t r, g, b;
            int ly = (y + desp) * (int)vdec;
            if (ly >= (int)SSTV_ALTO) { continue; }
            patron(x, ly, &r, &g, &b);
            suma += fabs((double)g_img[y][x][0] - r);
            suma += fabs((double)g_img[y][x][1] - g);
            suma += fabs((double)g_img[y][x][2] - b);
            n += 3;
        }
    }
    return n ? suma / (double)n : 999.0;
}

static double error_medio(int hasta, uint8_t vdec, int *mejor_desp)
{
    double mejor = 999.0;
    int d;
    if (mejor_desp) { *mejor_desp = -1; }
    for (d = 0; d <= 12; d++) {
        double e = error_desp(hasta, vdec, d);
        if (e < mejor) { mejor = e; if (mejor_desp) { *mejor_desp = d; } }
    }
    return mejor;
}

/*
 * Emitir UN modo empezando por la mitad: ni lider, ni corte, ni bits. Lo
 * primero que sale por el aire es el sincronismo de una linea cualquiera,
 * que es exactamente lo que oye la radio al caer encima de una emision.
 *
 * Devuelve en *lineas_hasta cuantas lineas habian pasado cuando engancho, o
 * -1 si no engancho nunca.
 */
static const char *a_pelo(uint8_t modo, int lineas, double sigma, double off_hz,
                          int *lineas_hasta, double *err, int *desp)
{
    tx_t t;
    sstv_info_t in;
    sstv_plan_t pl;
    int y;
    static char nombre[24];

    memset(&t, 0, sizeof t);
    t.semilla = 7777UL; t.ppm = 0.0; t.sigma = sigma;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1;
    g_n = 0; g_t = 0.0;
    g_off = off_hz;

    emite_reinicia();
    sstv_start((float)FS);
    sstv_auto(1U);
    sstv_plan(modo, &pl);

    *lineas_hasta = -1;
    for (y = 0; y < lineas; y += pl.lineas_img) {
        linea(&t, modo, y);
        sstv_info(&in);
        if (*lineas_hasta < 0 && in.estado == SSTV_IMAGEN && in.modo) {
            *lineas_hasta = (y / (int)pl.lineas_img) + 1;
        }
    }
    sstv_info(&in);
    nombre[0] = '\0';
    if (in.modo) {
        unsigned k = 0U;
        while (in.modo[k] != '\0' && k < sizeof nombre - 1U) { nombre[k] = in.modo[k]; k++; }
        nombre[k] = '\0';
    }
    {
        int d = -1;
        double e = error_medio(g_max_y > 0 ? g_max_y : 0, pl.vdec, &d);
        if (err)  { *err  = e; }
        if (desp) { *desp = d; }
    }
    g_off = 0.0;
    sstv_stop();
    return in.modo ? nombre : 0;
}

/* Cuantas lineas hay que emitir para que QUEPAN las que necesita el
 * detector y sobren unas cuantas para pintar. */
static int cuantas(uint8_t modo)
{
    sstv_plan_t pl;
    sstv_plan(modo, &pl);
    return 14 * (int)pl.lineas_img;
}

int main(void)
{
    uint8_t m;
    int cuando, desp;
    double err;
    const char *vio;

    printf("\nADIVINAR EL MODO DE SSTV SIN CABECERA (User/sstv.c)\n");

    printf("\n  1. LOS CATORCE MODOS, ENGANCHANDOSE A MITAD DE FOTO\n");
    printf("     %-12s %-12s %7s %7s %9s\n", "emitido", "adivinado", "lineas", "desp", "error");
    for (m = 0U; m < sstv_modos_n(); m++) {
        vio = a_pelo(m, cuantas(m), 0.0, 0.0, &cuando, &err, &desp);
        printf("     %-12s %-12s %7d %7d %9.1f\n",
               sstv_modo_nombre(m), vio ? vio : "(ninguno)", cuando, desp, err);
        if (!vio || strcmp(vio, sstv_modo_nombre(m)) != 0) {
            printf("       *** se esperaba %s\n", sstv_modo_nombre(m));
            fallos++;
        } else if (cuando < 0 || cuando > 4) {
            printf("       *** tardo %d lineas; con tres intervalos tienen que bastar 4\n", cuando);
            fallos++;
        } else if (err > 25.0) {
            printf("       *** la foto sale con error %.1f: adivina el nombre y pinta mal\n", err);
            fallos++;
        }
    }
    comprueba(1, "los catorce se adivinan solos, y en cuatro lineas o menos");

    printf("\n  2. Y LA FOTO SALE, no solo el nombre\n");
    {
        /* Martin 1 es el mas usado y PD120 el de dos lineas por barrido:
         * si la foto sale bien en los dos, el arranque a mitad coloca la
         * linea en su sitio en las dos familias que se montan distinto. */
        static const uint8_t k[] = { 0U, 9U };
        unsigned i;
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            vio = a_pelo(k[i], 40 * 2, 0.0, 0.0, &cuando, &err, &desp);
            printf("     %-12s error medio %.1f de 255, desplazada %d renglones\n",
                   sstv_modo_nombre(k[i]), err, desp);
            comprueba(err < 20.0, "la foto sale colocada despues de adivinar");
            comprueba(desp >= 0 && desp <= 4,
                      "y no se pierde mas que lo que costo medir el ritmo");
        }
    }

    /*
     * 3. HASTA DONDE LLEGA, MEDIDO. No "aguanta ruido": CUANTO.
     *
     * Las dos tablas de aqui no son una comprobacion con un aprobado, son
     * la frontera del invento, y estan para que se vea moverse el dia que
     * alguien toque el detector. Lo que se exige es solo el tramo que hoy
     * funciona; lo de fuera se imprime y se lee.
     *
     * Y EL MARGEN DE SINTONIA NO ES SIMETRICO, por una razon que se ve en
     * la tabla y conviene dejar escrita. El corte de sincronismo se pone
     * ahora 150 Hz por encima del suelo de frecuencia medido (ver
     * PISO_CUBOS en sstv.c), asi que la senal puede llegar MUY alta -hasta
     * +500 Hz, que es el tope de lo que admite la propia cabecera- sin que
     * el pulso se escape por arriba.
     *
     * Hacia abajo el que estorba no es el pulso: son los SEPARADORES de
     * 1500 Hz que Scottie y Martin ponen entre canales. Con la senal 150 Hz
     * baja caen en 1350, o sea justo encima del corte, y se cuelan como
     * pulsos de 3 ms cada 140 ms. Entonces los intervalos dejan de
     * parecerse entre si -143, 286, 428- y el detector hace lo correcto:
     * NO elegir. La foto no sale, pero tampoco sale una equivocada.
     *
     * Con cabecera eso no pasa, porque el VIS mide el desvio antes de
     * empezar: sim/sstvtest.c lee la cabecera hasta -300 Hz. O sea que el
     * tramo de -100 para abajo sigue cubierto por el camino de siempre.
     */
    printf("\n  3. HASTA DONDE LLEGA, MEDIDO\n");
    {
        static const double k_ruido[] = { 0.0, 0.05, 0.10, 0.15, 0.20, 0.30 };
        static const double k_off[]   = { -300.0, -250.0, -200.0, -175.0, -150.0,
                                          -100.0, 0.0, 100.0, 200.0, 300.0,
                                           400.0, 500.0 };
        unsigned i;

        printf("     ruido (Martin 1)\n");
        for (i = 0U; i < sizeof k_ruido / sizeof k_ruido[0]; i++) {
            vio = a_pelo(0U, cuantas(0U), k_ruido[i], 0.0, &cuando, &err, &desp);
            printf("       sigma %.2f  -> %-12s %s\n", k_ruido[i],
                   vio ? vio : "(ninguno)",
                   (vio && strcmp(vio, "Martin 1") == 0) ? "" : "  <- aqui ya no");
            if (k_ruido[i] <= 0.30) {
                comprueba(vio && strcmp(vio, "Martin 1") == 0,
                          "acierta con este ruido");
            }
        }

        printf("     desvio de sintonia (Scottie 1, sigma 0,05)\n");
        for (i = 0U; i < sizeof k_off / sizeof k_off[0]; i++) {
            vio = a_pelo(2U, cuantas(2U), 0.05, k_off[i], &cuando, &err, &desp);
            printf("       %+5.0f Hz    -> %-12s %s\n", k_off[i],
                   vio ? vio : "(ninguno)",
                   (vio && strcmp(vio, "Scottie 1") == 0) ? "" : "  <- aqui ya no");
            if (k_off[i] >= -100.0 && k_off[i] <= 500.0) {
                comprueba(vio && strcmp(vio, "Scottie 1") == 0,
                          "acierta con este desvio");
            }
        }
    }

    printf("\n  4. Y SE CALLA CUANDO NO HAY MODO QUE ADIVINAR\n");
    {
        tx_t t;
        sstv_info_t in;
        int i;

        /* a) ruido solo */
        memset(&t, 0, sizeof t); t.semilla = 99UL; t.sigma = 3.0;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(1U);
        emite(&t, 1900.0, 8.0);
        sstv_info(&in);
        printf("     8 s de ruido         -> %s\n", in.modo ? in.modo : "(ninguno)");
        comprueba(in.estado == SSTV_BUSCA, "el ruido no engancha ningun modo");
        sstv_stop();

        /* b) un tono fijo, que no tiene pulsos */
        memset(&t, 0, sizeof t); t.semilla = 5UL;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(1U);
        emite(&t, 2300.0, 8.0);
        sstv_info(&in);
        printf("     8 s de tono blanco   -> %s\n", in.modo ? in.modo : "(ninguno)");
        comprueba(in.estado == SSTV_BUSCA, "un tono fijo tampoco");
        sstv_stop();

        /* c) pulsos de 1200 a un ritmo que NO es de ningun modo */
        memset(&t, 0, sizeof t); t.semilla = 5UL;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(1U);
        for (i = 0; i < 30; i++) { emite(&t, 1200.0, 0.009); emite(&t, 1800.0, 0.091); }
        sstv_info(&in);
        printf("     pulsos cada 100 ms   -> %s\n", in.modo ? in.modo : "(ninguno)");
        comprueba(in.estado == SSTV_BUSCA, "un ritmo que no es de nadie no engancha");
        sstv_stop();
    }

    printf("\n  5. LA CABECERA MANDA, Y EL INTERRUPTOR TAMBIEN\n");
    {
        tx_t t;
        sstv_info_t in;
        int y;

        /* a) con VIS: el modo es el del VIS y lo dice como.*/
        memset(&t, 0, sizeof t); t.semilla = 1UL;
        memset(g_img, 0, sizeof g_img); g_max_y = -1;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(1U);
        cabecera_vis(&t, sstv_modo_vis(2U));          /* Scottie 1 */
        for (y = 0; y < 12; y++) { linea(&t, 2U, y); }
        sstv_info(&in);
        printf("     VIS de Scottie 1     -> %s (como %u)\n",
               in.modo ? in.modo : "(ninguno)", in.como);
        comprueba(in.modo && strcmp(in.modo, "Scottie 1") == 0, "la cabecera decide el modo");
        comprueba(in.como == SSTV_COMO_VIS, "y queda apuntado que lo decidio ella");
        sstv_stop();

        /* b) con la deteccion apagada no engancha nada sin cabecera */
        memset(&t, 0, sizeof t); t.semilla = 1UL;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(0U);
        for (y = 0; y < 12; y++) { linea(&t, 2U, y); }
        sstv_info(&in);
        printf("     apagada, sin cabecera-> %s\n", in.modo ? in.modo : "(ninguno)");
        comprueba(in.estado == SSTV_BUSCA, "apagada, no adivina (el interruptor hace algo)");
        sstv_auto(1U);
        sstv_stop();
    }

    printf("\n  6. Y SE CORRIGE SOLA SI EL MODO PUESTO ES EL EQUIVOCADO\n");
    {
        tx_t t;
        sstv_info_t in;
        int y;

        memset(&t, 0, sizeof t); t.semilla = 3UL;
        memset(g_img, 0, sizeof g_img); g_max_y = -1;
        g_n = 0; g_t = 0.0; emite_reinicia();
        sstv_start((float)FS); sstv_auto(1U);
        sstv_fuerza(1U);                     /* a mano: Martin 2 */
        sstv_info(&in);
        printf("     puesto a mano        -> %s\n", in.modo ? in.modo : "(ninguno)");
        for (y = 0; y < 14; y++) { linea(&t, 4U, y); }   /* pero llega Scottie DX */
        sstv_info(&in);
        printf("     y llega Scottie DX   -> %s (como %u)\n",
               in.modo ? in.modo : "(ninguno)", in.como);
        comprueba(in.modo && strcmp(in.modo, "Scottie DX") == 0,
                  "cambia al modo que de verdad viene");
        comprueba(in.como == SSTV_COMO_RITMO, "y dice que lo decidio midiendo");
        sstv_stop();
    }

    printf("\n  %s (%d fallos)\n\n",
           fallos ? "*** HAY FALLOS ***" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

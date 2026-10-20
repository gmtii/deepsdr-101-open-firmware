/*
 * Banco del SSTV.
 *
 * Compila User/sstv.c DEL FIRMWARE y fabrica la señal con SUS MISMOS tiempos
 * (sstv_modo_tiempos()) y SU MISMA escala de brillo (sstv_hz_de_nivel()). Si
 * el banco tuviera su propia tabla de tiempos, lo unico que se comprobaria es
 * que dos copias coinciden.
 *
 * LO QUE ESO SI DEJA COMPROBAR, y es lo que importa: que la FORMULA que saca
 * los desplazamientos de cada canal a partir de los cuatro tiempos del modo
 * es la correcta, que el codigo VIS se lee bien, que el sincronismo se
 * reengancha linea a linea, y que los colores no salen cambiados de sitio -
 * que es el fallo que no revienta nada y parece un problema de antena.
 */
#include "sstv.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/*
 * EL POZO DE RAM PRESTADA, PARA EL BANCO - 07/10/2026.
 *
 * Desde hoy la cola de salida de sstv.c no es un array permanente sino un
 * prestamo de hfdl_ram.h: once kilobytes y medio que solo son de SSTV
 * mientras SSTV esta puesto. Aqui se le da un pozo del tamaño real.
 *
 * Las firmas estan COPIADAS DE hfdl_ram.h, no escritas de memoria. Un dia se
 * escribieron de memoria en helltest.c, enlazaron sin rechistar -C no
 * comprueba firmas entre ficheros- y hell_start() leyo capacidad cero: el
 * banco decia "0 columnas de 105" y nada apuntaba a la causa.
 *
 * Y se cuentan los prestamos: si sstv_start() coge y sstv_stop() no suelta,
 * el siguiente modo que lo pida se queda sin memoria y no hay forma de que
 * eso salga en una prueba que solo mire la foto.
 */
static unsigned g_ram_cog, g_ram_sue;
static unsigned char g_ram_pozo[0xD4A0];
void           hfdl_ram_coge(void) { g_ram_cog++; }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { g_ram_sue++; }

#define FS 12000.0

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

#include "sstv_genera.h"

static void guarda_ppm(const char *nombre, int n)
{
    FILE *f = fopen(nombre, "wb");
    int y, x, c;
    if (!f) { return; }
    fprintf(f, "P6\n%u %d\n255\n", (unsigned)SSTV_ANCHO, n);
    for (y = 0; y < n; y++)
        for (x = 0; x < (int)SSTV_ANCHO; x++)
            for (c = 0; c < 3; c++) fputc(g_img[y][x][c], f);
    fclose(f);
}

/*
 * Error medio por componente entre lo decodificado y el patron.
 *
 * El renglon `y` de lo decodificado corresponde a la linea `y * vdec` de la
 * imagen de verdad: los modos PD grandes tienen mas lineas de las que caben y
 * se pinta una de cada vdec. Comparar sin eso mediria una imagen contra otra
 * desplazada y daria un error enorme aunque todo estuviera bien.
 */
static double error_medio(int n, uint8_t vdec)
{
    double e = 0.0; long m = 0;
    int y, x, c;
    for (y = 2; y < n; y++) {
        for (x = 4; x < (int)SSTV_ANCHO - 4; x++) {
            uint8_t p[3];
            patron(x, y * (int)vdec, &p[0], &p[1], &p[2]);
            for (c = 0; c < 3; c++) {
                double d = (double)g_img[y][x][c] - (double)p[c];
                e += (d < 0 ? -d : d); m++;
            }
        }
    }
    return m ? e / m : 999.0;
}

static void prueba(uint8_t modo, int lineas, double ppm, double sigma,
                   const char *ppmfile, int exigir, double umbral)
{
    tx_t t;
    sstv_info_t in;
    int y;
    double err;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL; t.ppm = ppm; t.sigma = sigma;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1;
    g_n = 0; g_t = 0.0;

    emite_reinicia();
    sstv_start((float)FS);
    cabecera_vis(&t, sstv_modo_vis(modo));
    sstv_info(&in);
    {
        uint8_t est; uint32_t li, c12; float hz;
        static const char *k_est[] = { "lider1", "corte", "lider2", "arranque", "bits" };
        sstv_vis_dbg(&est, &li, &c12, &hz);
        printf("    VIS %u -> modo \"%s\"   (el detector se quedo en \"%s\", "
               "lider %lu ms, 1200 %lu ms, hz %.0f)\n",
               sstv_modo_vis(modo), in.modo ? in.modo : "(ninguno)",
               k_est[est < 5 ? est : 0],
               (unsigned long)(li * 1000 / (unsigned long)FS),
               (unsigned long)(c12 * 1000 / (unsigned long)FS), (double)hz);
    }
    if (exigir) {
        comprueba(in.modo != 0 && strcmp(in.modo, sstv_modo_nombre(modo)) == 0,
                  "la cabecera VIS dice que modo viene");
    }

    {
        sstv_plan_t pl;
        sstv_plan(modo, &pl);
        for (y = 0; y < lineas; y += pl.lineas_img) { linea(&t, modo, y); }
    }

    sstv_info(&in);
    {
        sstv_plan_t pl;
        sstv_plan(modo, &pl);
        err = error_medio(g_max_y > 0 ? g_max_y : 1, pl.vdec);
    }
    printf("    %d lineas decodificadas, error medio %.1f de 255, sincs malos %u\n",
           g_max_y + 1, err, in.sincs_malos);
    if (ppmfile) { guarda_ppm(ppmfile, g_max_y > 0 ? g_max_y : 1); }
    if (exigir) {
        {
            sstv_plan_t pl;
            sstv_plan(modo, &pl);
            comprueba(g_max_y >= (lineas / (int)pl.vdec) - 3, "llegan todas las lineas");
        }
        comprueba(err < umbral, "y los colores estan en su sitio");
        /*
         *
         * sstv_info().vis significa UNA cosa: "llego un VIS valido de un
         * modo que no sabemos hacer". modo_aplica() tambien le metia el VIS
         * del modo que SI acababa de aplicar, y entonces quien lo lee no
         * podia distinguirlos: al acabar una foto de Martin 1 la cabecera
         * veia un 44 y anunciaba que Martin 1 no esta soportado. Martin 1
         * es la PRIMERA entrada de la tabla.
         *
         * Aqui se acaba de decodificar una imagen ENTERA del modo `modo`,
         * o sea que por definicion esta soportado. Si vis no es cero, algo
         * le esta dando ese segundo significado otra vez.
         */
        comprueba(in.vis == 0U,
                  "y no se anuncia como no soportado un modo que acaba de decodificar");
    }
    sstv_stop();
}

int main(void)
{
    uint8_t i;


    printf("banco del SSTV\n");

    printf("\n1. la tabla de modos, calculada\n");
    printf("    (la duracion de imagen sale de linea x alto / lineas por linea\n"
           "     transmitida, y se compara con la que publican las tablas de\n"
           "     modos. Es la comprobacion mas fuerte que hay de que los tiempos\n"
           "     estan bien: un solo numero mal y la duracion se va.)\n");
    printf("    %-11s %-4s %5s %9s %4s %2s %6s %6s\n",
           "modo", "VIS", "ancho", "linea", "alto", "li", "imagen", "debe");
    {
        /* Duraciones publicadas de cada modo, en segundos. NO salen de la
         * misma formula que se esta comprobando: salen de las tablas de
         * modos de SSTV, que es lo que las hace una comprobacion y no un
         * espejo. */
        static const struct { const char *n; double s; } k_dur[] = {
            { "Martin 1",   114.3 }, { "Martin 2",    58.1 },
            { "Scottie 1",  109.6 }, { "Scottie 2",   71.1 },
            { "Scottie DX", 268.9 },
            { "Robot 36",    36.0 }, { "Robot 72",    72.0 },
            { "PD50",        49.7 }, { "PD90",        90.0 },
            { "PD120",      126.1 }, { "PD160",      160.9 },
            { "PD180",      187.0 }, { "PD240",      248.0 },
            { "PD290",      288.7 }
        };
        int malos = 0;
        for (i = 0; i < sstv_modos_n(); i++) {
            sstv_plan_t pl;
            double dur, debe = 0.0;
            unsigned k;
            sstv_plan(i, &pl);
            dur = (double)pl.linea * (double)pl.alto / (double)pl.lineas_img;
            for (k = 0; k < sizeof k_dur / sizeof k_dur[0]; k++) {
                if (strcmp(k_dur[k].n, sstv_modo_nombre(i)) == 0) { debe = k_dur[k].s; }
            }
            printf("    %-11s %-4u %5u %8.5fs %4u %2u %6.1fs %6.1fs%s\n",
                   sstv_modo_nombre(i), sstv_modo_vis(i), pl.ancho,
                   (double)pl.linea, pl.alto, pl.lineas_img, dur, debe,
                   (debe > 0.0 && (dur < debe * 0.99 || dur > debe * 1.01)) ? "  <-- MAL" : "");
            if (debe <= 0.0 || dur < debe * 0.99 || dur > debe * 1.01) { malos++; }
        }
        comprueba(malos == 0, "los catorce modos duran lo que las tablas dicen");
    }

    printf("\n2. Martin 1, señal limpia\n");
    prueba(0U, 60, 0.0, 0.01, "out/sstv_m1.ppm", 1, 12.0);

    /*
     * SOLO RUIDO, SIN SEÑAL - 24/09/2026
     *
     * En la radio, con Martin 1 forzado sobre ruido de banda, la cuenta de
     * lineas subia y se pintaba confeti; con Scottie 2 no se movia, y
     * parecia que el boton estaba roto.
     *
     * Mi primera explicacion fue que una linea solo se cierra cuando
     * llegan DOS pulsos de sincronismo separados por lo que dura una
     * linea con un 20% de margen, asi que el ancho de esa ventana -que es
     * proporcional a la duracion de la linea- decidiria cuantos aciertan
     * por casualidad. Suena bien y NO ES LO QUE PASA: aqui, con ruido
     * gaussiano, los cinco modos cierran CERO lineas, Martin 1 incluido.
     *
     * Asi que lo que esta prueba demuestra no es la diferencia entre
     * modos, sino algo mas util: que con ruido no sale una imagen. Un
     * decodificador que pinta 256 lineas de confeti es uno que pintaria
     * cualquier cosa. Que Scottie 2 no arrancara era el comportamiento
     * CORRECTO; lo raro era lo otro.
     *
     * Lo que hacia distinto al ruido de la radio no se sabe desde aqui -el
     * ruido de banda no es gaussiano, lleva impulsos y portadoras, y el
     * filtro de audio estaba en 0-1,8 kHz, que corta el blanco de SSTV a
     * 2300 Hz y deja pasar el sincronismo de 1200-. Se deja escrito en vez
     * de inventar una explicacion que cuadre.
     */
    printf("\n2b. con SOLO ruido, sin señal, 30 s por modo\n");
    printf("    (lo que se mide es cuantas lineas cierra por casualidad:\n"
           "     depende del ancho de la ventana de sincronismo, que es\n"
           "     proporcional a lo que dura una linea del modo)\n");
    {
        static const uint8_t k_m[] = { 0U, 1U, 2U, 3U, 4U };
        unsigned j;
        for (j = 0; j < sizeof k_m / sizeof k_m[0]; j++) {
            tx_t t;
            sstv_info_t in;
            float linea_s = 0.0f;
            int k;

            memset(&t, 0, sizeof t);
            t.sigma = 20.0;   /* la portadora queda al 5%: esto ya es ruido */
            g_max_y = -1;
            g_t = 0.0;
            g_n = 0;
            sstv_start((float)FS);
            sstv_fuerza(k_m[j]);
            for (k = 0; k < 3000; k++) { emite(&t, 1900.0, 0.010); }
            sstv_info(&in);
            (void)sstv_modo_tiempos(k_m[j], &linea_s, 0, 0, 0);
            printf("    %-11s linea %.3f s, ventana %.3f s -> %3d lineas, "
                   "%u sincs descartados\n",
                   sstv_modo_nombre(k_m[j]), linea_s, linea_s * 0.4,
                   g_max_y + 1, in.sincs_malos);
            comprueba(g_max_y + 1 < (int)SSTV_ALTO,
                      "el ruido NO llega a pintar una imagen entera");
        }
        g_max_y = -1;
        g_t = 0.0;
        g_n = 0;
    }

    printf("\n3. Scottie 1 (el sincronismo va en otro sitio)\n");
    prueba(2U, 60, 0.0, 0.01, "out/sstv_s1.ppm", 1, 12.0);

    printf("\n4. Martin 2\n");
    prueba(1U, 60, 0.0, 0.01, 0, 1, 12.0);

    printf("\n5. con error de reloj del transmisor\n");
    printf("    (el sincronismo por linea tiene que comerselo sin despeinarse)\n");
    prueba(0U, 60, 2000.0, 0.01, 0, 1, 12.0);
    prueba(0U, 60, -2000.0, 0.01, 0, 1, 12.0);

    printf("\n5b. las familias nuevas\n");
    printf("    (Robot y PD no mandan rojo, verde y azul: mandan luminancia y\n"
           "     dos diferencias de color a media resolucion, asi que el error\n"
           "     de color es mayor POR DISEÑO - es lo que el formato tira para\n"
           "     que la imagen quepa en la mitad de tiempo)\n");
    printf("    Robot 36 (la croma alterna de linea en linea):\n");
    prueba(5U, 60, 0.0, 0.01, "out/sstv_r36.ppm", 1, 26.0);
    printf("    Robot 72 (las dos cromas en cada linea):\n");
    prueba(6U, 60, 0.0, 0.01, "out/sstv_r72.ppm", 1, 26.0);
    printf("    PD90 (dos lineas de imagen por linea transmitida):\n");
    prueba(8U, 60, 0.0, 0.01, "out/sstv_pd90.ppm", 1, 26.0);
    printf("    PD120 (640 por 496, que hay que reducir para que quepa):\n");
    prueba(9U, 60, 0.0, 0.01, "out/sstv_pd120.ppm", 1, 26.0);

    /*
     * 5c. CON LA SINTONIA DESVIADA.
     *
     * Le llegaba todo 190 Hz bajo -13 ppm de cristal sin calibrar sobre 14,23
     * MHz-. Con las bandas fijas que tenia clasifica(), el bit de arranque de
     * 1200 caia en la banda del 1100 y los ceros de 1300 tambien: la cabecera
     * no podia completarse ni por casualidad, emision tras emision.
     *
     * Lo que se mide aqui es que la cabecera engancha Y que la foto sale, con
     * la señal corrida en los dos sentidos. Sin la correccion del lider esta
     * prueba no pasa ni a 100 Hz.
     */
    printf("\n5c. con la sintonia desviada (lo del 06/10/2026)\n");
    printf("    (se corre la señal entera, cabecera incluida. El decodificador\n"
           "     tiene que medir el desvio sobre el lider y corregirse solo.)\n");
    {
        /*
         * EL MARGEN SUBE A -300..+300 - 06/10/2026, tercera vuelta.
         *
         * Antes llegaba a -190 por los pelos, porque el lider se reconocia por
         * caer encima de 1600 Hz. Ahora se reconoce por estar QUIETO, asi que
         * el limite ya no es la banda sino el discriminador: por debajo de
         * -300 el tono del "uno" (1100) se va a 790 Hz y disc_fm deja de
         * medirlo. -300 Hz en 14 MHz son 21 ppm, el triple de lo que tiene
         * esta radio.
         */
        static const double k_off[] = { -300.0, -250.0, -190.0, -150.0, -100.0,
                                        0.0, 100.0, 200.0, 250.0, 300.0 };
        unsigned o;
        for (o = 0U; o < sizeof k_off / sizeof k_off[0]; o++) {
            char que[64];
            g_off = k_off[o];
            printf("    %+6.0f Hz:", g_off);
            prueba(0U, 24, 0.0, 0.0, 0, 20, 12.0);
            (void)snprintf(que, sizeof que,
                           "a %+.0f Hz el desvio medido se parece al real", g_off);
            comprueba(fabs((double)sstv_desvio_hz() - g_off) < 25.0, que);
        }
        g_off = 0.0;
    }

    printf("\n6. con ruido\n");
    printf("    (la señal es un seno de amplitud 1 y el ruido va en multiplos de eso.\n"
           "     Lo que se rompe primero NO es la imagen: es la CABECERA. La imagen\n"
           "     sale limpia mientras la cabecera se lea, porque cada punto promedia\n"
           "     varias muestras y la cabecera se juega en tramos cortos.)\n");
    {
        static const double k_sig[] = { 0.02, 0.05, 0.10, 0.20, 0.35, 0.60 };
        unsigned j;
        for (j = 0; j < sizeof k_sig / sizeof k_sig[0]; j++) {
            printf("    ruido %.2f:\n", k_sig[j]);
            prueba(0U, 40, 0.0, k_sig[j], 0, 0, 12.0);
            if (k_sig[j] <= 0.10) {
                sstv_info_t in2;
                (void)in2;
                comprueba(g_max_y >= 35, "hasta 0,10 la cabecera se lee y la imagen sale");
            }
        }
    }

    /*
     * Y EL PRESTAMO, QUE SE DEVUELVE.
     *
     * Sin esto, el pozo de arriba seria decoracion: el banco pasaria igual de
     * verde con un sstv_stop() que no suelta, y el fallo saldria en antena el
     * dia que alguien se fuera de SSTV a HFDL y HFDL se quedara sin memoria
     * sin decir por que. Se prueba el ciclo entero -arrancar, parar- y que
     * despues de parar la cola no entrega nada.
     */
    printf("\n  el prestamo de RAM se devuelve\n");
    {
        unsigned c0 = g_ram_cog, s0 = g_ram_sue;
        sstv_start(12000.0f);
        comprueba(g_ram_cog == c0 + 1U, "arrancar coge el prestamo una vez");
        sstv_stop();
        comprueba(g_ram_sue == s0 + 1U, "parar lo suelta");
        comprueba(g_ram_cog - c0 == g_ram_sue - s0, "cogidos y sueltos cuadran");
        comprueba(sstv_linea(0) == 0, "parado, la cola no entrega nada");
        /* Y dos paradas seguidas no sueltan dos veces: soltar un prestamo que
         * ya no se tiene se lo quita al siguiente que lo pida. */
        s0 = g_ram_sue;
        sstv_stop();
        comprueba(g_ram_sue == s0, "parar dos veces no suelta dos veces");
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

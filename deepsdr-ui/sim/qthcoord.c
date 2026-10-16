/*
 * EL LOCALIZADOR A COORDENADAS, con la conversion DE VERDAD - 30/09/2026.
 *
 * Pedido por el usuario. Y no lo estaban: "IN80fp", que es Madrid
 * (40,6458 N 3,5417 W), salia como 46,4573 N 0,5831 E, o sea Francia.
 *
 * POR QUE PASO, QUE ES LO QUE DE VERDAD IMPORTA
 * ---------------------------------------------
 * Habia DOS conversiones. La de ft8_decoder.c -la que mueve el aspa de los
 * mapas y mide las distancias de los CQ- estaba bien. La pantalla del QTH de
 * main.c tenia su propia copia en enteros, y ahi la subcuadricula iba
 * multiplicada por diez: 8333 donde tocaba 833. O sea que el unico sitio
 * equivocado era la pantalla que existe PARA DARTE CUENTA de que te has
 * equivocado de letra.
 *
 * Y tools/qth_check.py no lo vio, ni podia: ese comprueba que el localizador
 * COMPILADO sea el de la radio, y hace la conversion EN PYTHON. Un banco que
 * reimplementa lo que mide aprueba su propia idea, no el codigo. Este enlaza
 * User/ft8_decoder.c del firmware, el mismo fichero que corre en la radio.
 *
 * QUE SE MIDE
 * -----------
 *  1. Localizadores conocidos, con la cifra exacta puesta a mano aqui. Con
 *     las constantes viejas ninguno cuadra.
 *  2. Las cuatro esquinas del mundo (AA00aa y RR99xx), que es donde se nota
 *     si el origen o el paso estan mal.
 *  3. Que el centro del cuadrado de CASA cae cerca de Madrid DE VERDAD, en
 *     kilometros. Los numeros de arriba se pueden copiar mal los dos a la
 *     vez; un mapa no.
 *  4. Que lo invalido devuelve false y NO TOCA las salidas: la pantalla
 *     enseña el hueco en blanco en vez de una coordenada inventada.
 *
 * Y las cifras estan puestas A MANO, calculadas aparte, no copiadas de lo
 * que saca el codigo. La primera vez tres de ellas estaban mal - las mias,
 * no las del firmware - y el banco las canto. Un banco que se rellena con
 * la salida del programa solo comprueba que el programa no cambie.
 */
#include "ft8_decoder.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* --- lo que ft8_decoder.c necesita para enlazar y este banco no usa --- */
#include "rtc_hw.h"
void rtc_hw_get(rtc_hw_datetime_t *o) { if (o) { memset(o, 0, sizeof *o); } }
const char *dxcc_pais(const char *ind) { (void)ind; return 0; }
const float *ft8_waterfall_get(void) { return 0; }
int  ftx_find_candidates(void *a, void *b, int c) { (void)a; (void)b; (void)c; return 0; }
bool ftx_decode_candidate(void *a, void *b, int c, void *d, void *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
int  ftx_message_decode(void *a, void *b, char *c)
{ (void)a; (void)b; (void)c; return -1; }

static int fallos;
#define TOL 2   /* diezmilesimas de grado: 22 m. El fallo desviaba 700 km. */

static void exige(int cond, const char *que)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "*** MAL ***");
    if (!cond) { fallos++; }
}

static void mira(const char *g, int32_t lat_sp, int32_t lon_sp)
{
    int32_t lat = 0, lon = 0;
    char q[96];
    int ok = ft8_decoder_grid_e4(g, (int)strlen(g), &lat, &lon);

    sprintf(q, "%-8s -> %7d , %8d  (espera %7d , %8d)",
            g, (int)lat, (int)lon, (int)lat_sp, (int)lon_sp);
    exige(ok && labs((long)lat - lat_sp) <= TOL
             && labs((long)lon - lon_sp) <= TOL, q);
}

static void txt(const char *g, const char *esperado)
{
    char o[32];
    char q[110];
    int ok = ft8_decoder_grid_coord_txt(g, (int)strlen(g), o, sizeof o);

    sprintf(q, "%-8s -> \"%s\"", g, ok ? o : "(nada)");
    exige(ok && strcmp(o, esperado) == 0, q);
    if (ok && strcmp(o, esperado) != 0) {
        printf("        esperaba \"%s\"\n", esperado);
    }
}

static void no_vale(const char *g, int len)
{
    int32_t lat = 123456L, lon = 654321L;   /* centinelas */
    char q[96];
    int ok = ft8_decoder_grid_e4(g, len, &lat, &lon);

    sprintf(q, "\"%s\" (len %d) no vale y no toca las salidas", g, len);
    exige(!ok && lat == 123456L && lon == 654321L, q);
}

static double km(double la1, double lo1, double la2, double lo2)
{
    double p1 = la1 * M_PI / 180.0, p2 = la2 * M_PI / 180.0;
    double h = sin((p2 - p1) / 2) * sin((p2 - p1) / 2)
             + cos(p1) * cos(p2)
               * sin((lo2 - lo1) * M_PI / 360.0) * sin((lo2 - lo1) * M_PI / 360.0);
    return 2.0 * 6371.0 * asin(sqrt(h));
}

int main(void)
{
    printf("LOCALIZADOR -> COORDENADAS (User/ft8_decoder.c del firmware)\n\n");

    printf("  CONOCIDOS, en diezmilesimas de grado\n");
    mira("IN80fp",  406458,  -35417);   /* al nordeste de Madrid */
    mira("IN80",    405000,  -30000);   /* el mismo cuadrado a 4 caracteres */
    mira("IL18dk",  284375, -177083);   /* Canarias, el valor viejo del #define */
    mira("JO20HI",  503542,   46250);   /* mayusculas en la subcuadricula */
    mira("FN42",    425000, -710000);   /* Boston */
    mira("PM95",    355000, 1390000);   /* Tokio */

    printf("\n  LAS ESQUINAS\n");
    mira("AA00aa", -899792, -1799583);
    mira("RR99xx",  899792,  1799583);

    printf("\n  DONDE CAE DE VERDAD\n");
    /*
     * Esto NO es "el QTH de la radio": desde el 30/09 no hay ninguno en el
     * codigo (ver tools/qth_check.py). Es un vector de prueba: IN80fp es un
     * cuadrado conocido y aqui se comprueba en KILOMETROS que cae donde
     * cae, porque las cifras de arriba se pueden copiar mal las dos a la vez
     * y un mapa no.
     */
    {
        int32_t lat = 0, lon = 0;
        double d;
        (void)ft8_decoder_grid_e4("IN80fp", 6, &lat, &lon);
        d = km(lat / 10000.0, lon / 10000.0, 40.4168, -3.7038);
        printf("      IN80fp esta a %.1f km de la Puerta del Sol\n", d);
        exige(d < 50.0, "IN80fp cae en la Comunidad de Madrid (< 50 km)");
        /* Con las constantes viejas salia 46,4573 N 0,5831 E: 700 km. */
    }

    printf("\n  EL RENGLON QUE SE PEGA EN UN MAPA\n");
    /*
     * La CADENA exacta, no el numero. Pedido por el usuario. El renglon del QTH existe para pegarlo en un mapa y
     * comprobar que el localizador teclado es el tuyo, y con coma decimal
     * eso no se puede: un mapa lee la coma como el separador entre las dos
     * cifras, asi que "40,6458 3,5417" lo entiende como "latitud 40,
     * longitud 6458" y se cae a una busqueda de texto. Le paso, y encima le
     * hizo dudar de la cifra, que era correcta.
     *
     * Asi que el formato es parte de la respuesta y se mide igual que el
     * numero: punto decimal, coma y espacio de separador, menos para sur y
     * oeste, y ni una letra N/S/E/W.
     */
    txt("IN80fp", "40.6458, -3.5417");
    txt("IN80dk", "40.4375, -3.7083");
    txt("IN80",   "40.5000, -3.0000");   /* sin subcuadricula: el centro */
    txt("FN42",   "42.5000, -71.0000");  /* longitud de dos cifras */
    txt("PM95",   "35.5000, 139.0000");  /* este: sin signo */
    txt("AA00aa", "-89.9792, -179.9583");/* el peor caso de largo: 19 */
    txt("RR99xx", "89.9792, 179.9583");
    {
        char o[32];
        o[0] = 'x';
        exige(!ft8_decoder_grid_coord_txt("ZZ99xx", 6, o, sizeof o)
              && o[0] == '\0',
              "un localizador que no vale deja el renglon vacio");
        exige(!ft8_decoder_grid_coord_txt("AA00aa", 6, o, 8U),
              "si no cabe dice que no, en vez de recortar");
    }

    printf("\n  LO QUE NO VALE\n");
    no_vale("ZZ99xx", 6);
    no_vale("IN8", 3);
    no_vale("IN80f", 5);
    no_vale("IN80fy", 6);   /* la subcuadricula llega hasta la x */
    no_vale("RR73", 4);     /* eso no es un cuadrado, es un mensaje */

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

/*
 * ======================================================================
 * LA FOTO SE CORROMPE A TROZOS CON LA SEÑAL PERFECTA - 13/10/2026
 * ======================================================================
 *
 * En el video las barras de color salen rectas y en su sitio -o sea que el
 * reloj de linea esta bien- pero hay RENGLONES SUELTOS estropeados,
 * repartidos por la foto.
 *
 * LA HIPOTESIS, y es comprobable: lo que se pierde no es señal, son
 * MUESTRAS. Si la interrupcion del audio llega tarde, las dos banderas del
 * DMA estan puestas a la vez, la mitad vieja ya la esta pisando el DMA y se
 * tira (sdr_rx.c lo cuenta en sdr_rx_get_tarde()). Y la radio tiene motivos
 * para llegar tarde justo en SSTV: pinta la cinta de la imagen renglon a
 * renglon por el bus del panel, con la CPU parada.
 *
 * LO QUE HACE QUE ESO DUELA TANTO es el tamaño de las piezas:
 *
 *     el pulso de sincronismo de Martin 1   4,862 ms
 *     media vuelta del DMA a 12 kHz          5,33 ms   (64 muestras)
 *
 * O sea que UN SOLO hueco de audio puede tragarse el pulso de sincronismo
 * ENTERO. Y entonces el decodificador no ve la marca de la linea.
 *
 * Y AHI ESTA LO QUE SE MIDE AQUI, que es la parte que se puede arreglar:
 * que pasa DESPUES de tragarse un sincronismo. Si lo unico que se pierde es
 * el renglon del hueco, el fallo es del tamaño del hueco y poco se puede
 * hacer. Si se pierden varios, o la imagen se desplaza, es del
 * decodificador y si se puede.
 */
#include "sstv.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/*
 * El pozo de RAM prestada, igual que en sim/sstvtest.c y por lo mismo: la
 * cola de salida de sstv.c es un prestamo de hfdl_ram.h. Las firmas van
 * COPIADAS de esa cabecera, no escritas de memoria.
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
#include "sstv_genera.h"

static int fallos = 0;
static void exige(int ok, const char *que)
{
    printf("    %-62s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

/* El error de UN renglon contra el patron, de 0 a 255. */
static double error_fila(int y, uint8_t vdec)
{
    double e = 0.0; long m = 0;
    int x, c;
    for (x = 4; x < (int)SSTV_ANCHO - 4; x++) {
        uint8_t p[3];
        patron(x, y * (int)vdec, &p[0], &p[1], &p[2]);
        for (c = 0; c < 3; c++) {
            double d = (double)g_img[y][x][c] - (double)p[c];
            e += (d < 0 ? -d : d); m++;
        }
    }
    return m ? e / m : 999.0;
}

static void guarda(const char *nombre, int n)
{
    FILE *f = fopen(nombre, "wb");
    int y;
    if (!f) { return; }
    fprintf(f, "P6\n%u %d\n255\n", (unsigned)SSTV_ANCHO, n);
    for (y = 0; y < n; y++) { fwrite(g_img[y], 1, SSTV_ANCHO * 3U, f); }
    fclose(f);
}

/*
 * Decodifica `lineas` renglones de Martin 1 perdiendo `bloques` bloques de
 * 64 muestras justo al llegar al renglon `donde`. Devuelve cuantos
 * renglones salen con un error por encima de `umbral`, y por referencia el
 * peor y los sincronismos malos.
 */
static int corre(int lineas, int donde, long bloques, double umbral,
                 int *peor_y, double *peor_e, unsigned *sincs,
                 const char *ppmfile)
{
    tx_t t;
    sstv_info_t in;
    sstv_plan_t pl;
    int y, malos = 0;

    memset(&t, 0, sizeof t);
    t.semilla = 2024UL;
    memset(g_img, 0, sizeof g_img);
    g_max_y = -1; g_n = 0; g_t = 0.0;
    g_perder = 0; g_perdidos = 0;

    emite_reinicia();
    sstv_start((float)FS);
    cabecera_vis(&t, sstv_modo_vis(0));     /* 0 = Martin 1 */
    sstv_plan(0, &pl);

    for (y = 0; y < lineas; y += pl.lineas_img) {
        if (y == donde) { g_perder = bloques; }
        linea(&t, 0, y);
    }

    sstv_info(&in);
    *sincs = in.sincs_malos;
    *peor_y = -1; *peor_e = 0.0;
    for (y = 2; y <= g_max_y; y++) {
        double e = error_fila(y, pl.vdec);
        if (e > umbral) { malos++; }
        if (e > *peor_e) { *peor_e = e; *peor_y = y; }
    }
    if (ppmfile) { guarda(ppmfile, g_max_y > 0 ? g_max_y : 1); }
    sstv_stop();
    return malos;
}

int main(void)
{
    int peor_y; double peor_e; unsigned sincs;
    int base_malos;

    printf("\n== LA FOTO CON HUECOS EN EL AUDIO ==\n");

    /* ---------------------------------------------------------------- */
    printf("\n  1) sin hueco: el suelo contra el que se compara\n\n");
    {
        base_malos = corre(160, -1, 0, 40.0, &peor_y, &peor_e, &sincs,
                           "out/sstv_hueco_limpio.ppm");
        printf("     renglones con error > 40: %d   peor %.0f (renglon %d)"
               "   sincs malos %u\n", base_malos, peor_e, peor_y, sincs);
        exige(base_malos == 0, "con la señal entera no hay ni un renglon malo");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2) UN hueco de 64 muestras (5,33 ms), que es media vuelta\n"
           "     del DMA y mas largo que el sincronismo de Martin 1\n\n");
    {
        int malos = corre(160, 40, 1, 40.0, &peor_y, &peor_e, &sincs,
                          "out/sstv_hueco_1.ppm");
        printf("     se tiraron %ld bloques = %.1f ms\n",
               g_perdidos, (double)g_perdidos * 64.0 * 1000.0 / FS);
        printf("     renglones con error > 40: %d   peor %.0f (renglon %d)"
               "   sincs malos %u\n", malos, peor_e, peor_y, sincs);
        printf("     (el hueco cae en el renglon 40 de 160)\n");
        exige(malos <= 1,
              "un hueco de un renglon estropea UN renglon, no varios");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3) y con huecos mas grandes\n\n");
    {
        static const long k[] = { 2, 4, 8 };
        unsigned i;
        int peor_malos = 0;
        unsigned peor_sincs = 0U;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            int malos = corre(160, 40, k[i], 40.0, &peor_y, &peor_e, &sincs,
                              (i == 2) ? "out/sstv_hueco_8.ppm" : 0);
            printf("     %ld bloques (%.0f ms): %d renglones malos, "
                   "peor %.0f, sincs malos %u\n",
                   k[i], (double)k[i] * 64.0 * 1000.0 / FS,
                   malos, peor_e, sincs);
            if (malos > peor_malos)   { peor_malos = malos; }
            if (sincs > peor_sincs)   { peor_sincs = sincs; }
        }
        /*
         * EL NUMERO QUE IMPORTA ES EL SEGUNDO. Un hueco de 43 ms se come
         * un renglon por fuerza: son 43 ms de una linea de 446. Lo que NO
         * puede pasar es que se coma ocho sincronismos, porque eso ya no es
         * el hueco, es la cascada - y la cascada es lo que se arreglo.
         */
        exige(peor_malos <= 2,
              "un hueco estropea como mucho dos renglones, por grande que sea");
        exige(peor_sincs <= 2,
              "y pierde UN sincronismo, no ocho: no hay cascada");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4) huecos repartidos, como en el video\n\n");
    {
        /* Cuatro huecos en sitios distintos: lo que de verdad hace una
         * radio que va justa de tiempo mientras pinta. */
        tx_t t;
        sstv_info_t in;
        sstv_plan_t pl;
        int y, malos = 0;
        double e;

        memset(&t, 0, sizeof t);
        t.semilla = 2024UL;
        memset(g_img, 0, sizeof g_img);
        g_max_y = -1; g_n = 0; g_t = 0.0;
        g_perder = 0; g_perdidos = 0;
        emite_reinicia();
        sstv_start((float)FS);
        cabecera_vis(&t, sstv_modo_vis(0));
        sstv_plan(0, &pl);
        for (y = 0; y < 160; y += pl.lineas_img) {
            if (y == 20 || y == 55 || y == 90 || y == 125) { g_perder = 1; }
            linea(&t, 0, y);
        }
        sstv_info(&in);
        for (y = 2; y <= g_max_y; y++) {
            e = error_fila(y, pl.vdec);
            if (e > 40.0) { malos++; printf("       renglon %3d  error %.0f\n", y, e); }
        }
        printf("     cuatro huecos de 5,3 ms -> %d renglones malos, "
               "sincs malos %u\n", malos, in.sincs_malos);
        guarda("out/sstv_hueco_4.ppm", g_max_y > 0 ? g_max_y : 1);
        sstv_stop();
        exige(malos <= 4, "cuatro huecos estropean cuatro renglones, no una zona");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

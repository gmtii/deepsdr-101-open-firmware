/*
 * ======================================================================
 * QUE LA TIERRA SE RELLENE TAMBIEN AL ACERCARSE - 15/10/2026
 * ======================================================================
 *
 * sim/mapa_tierra.c mide el relleno DEL ZOOM 1, y estaba verde mientras la
 * radio enseñaba el mapa sin rellenar en cuanto te acercabas. No mentia: es
 * que al zoom 1 el par-impar arranca solo -toda fila empieza en el borde
 * del mundo, que es mar- y al acercarse no hay borde del que partir.
 *
 * Lo que se ha añadido es LA PARIDAD DE PARTIDA: un bit por fila en el
 * borde izquierdo de cada casilla, dentro del .map. Esto lo mide, y mide
 * las dos cosas que pueden salir mal con una semilla:
 *
 *   1. QUE ACIERTE. Ocho sitios del mundo que cualquiera puede comprobar
 *      en un atlas -Madrid es tierra, el Atlantico medio es agua- a los
 *      zooms 4 y 8. Es la unica comprobacion de este banco que NO sale de
 *      mi propio codigo: si el empaquetador y el dibujante se equivocan
 *      los dos igual, esto lo caza y la coherencia interna no.
 *
 *   2. QUE NO SE INVIERTA AL CRUZAR UNA CASILLA. Es el fallo propio de
 *      sembrar: si la paridad se siembra una sola vez en el borde de la
 *      pantalla, basta una isla que la simplificacion se haya comido para
 *      que todo lo que tenga a la derecha salga del color contrario, y eso
 *      se ve como medio continente de mar. Aqui el mismo punto del mundo
 *      se mira desde DIECISIETE posiciones de ventana distintas, que lo
 *      pasean por toda la pantalla y por varios bordes de casilla: tiene
 *      que salir del mismo color las diecisiete veces.
 *
 * El .map lo fabrica el objetivo `anillos` llamando a mapa_pack.py, asi
 * que esto mide la cadena entera: empaquetador, lector y dibujante.
 *
 * Y SE ABRE POR FLASH, no con mapa_bin_pon(): el bloque de paridad NO se
 * carga con la ventana -son 45 bytes por casilla y en esta radio quedan 72
 * libres en toda la SRAM-, se lee de la flash cuando hace falta. Un banco
 * que lo tuviera entero en memoria no estaria midiendo ese camino.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include "mapa_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

#define ALTO 288

static void di(const char *que, int ok, const char *det)
{
    printf("    %-52s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

static uint16_t a565(uint32_t hex)
{
    return (uint16_t)((((hex >> 16) & 0xF8U) << 8)
                    | (((hex >> 8)  & 0xFCU) << 3)
                    |  ((hex        & 0xF8U) >> 3));
}

/* ---------------------------------------------- el .map, desde el fichero */
static FILE *g_f;
static long  g_tam;
static void lee(uint32_t a, uint8_t *b, uint32_t n)
{
    memset(b, 0, n);
    if ((long)a >= g_tam) { return; }
    fseek(g_f, (long)a, SEEK_SET);
    if (fread(b, 1, n, g_f) != n) { /* el relleno de ceros ya esta */ }
}

static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &g_caja);
}

/*
 * Deja (lon,lat) lo mas cerca que pueda del punto (cx,cy) de la pantalla.
 * Se arrastra en vez de calcular el desplazamiento porque mapa_arrastra()
 * es lo que hace el dedo del usuario, topes incluidos: calcularlo mediria un
 * camino que la radio no usa.
 */
static void mira(uint8_t esc, float lon, float lat, int16_t cx, int16_t cy)
{
    int it;

    memset(&g_caja, 0, sizeof g_caja);
    mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);
    mapa_zoom(&g_caja, esc, (int16_t)(GFX2_W / 2), (int16_t)(ALTO / 2));
    for (it = 0; it < 6; it++) {
        int16_t px = 0, py = 0;
        if (!mapa_grados_a_px(&g_caja, lon, lat, &px, &py)) { break; }
        if (px == cx && py == cy) { break; }
        /* arrastra() mueve el MAPA, no la ventana: off -= d. Para traer el
         * punto de px a cx hay que subir off_x en (px-cx), o sea pasar
         * (cx-px). El signo al reves deja el punto a tres mundos de aqui. */
        mapa_arrastra(&g_caja, (int16_t)(cx - px), (int16_t)(cy - py));
    }
    mapa_ventana(&g_caja);
    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);
}

/* 1 = tierra, 0 = mar, -1 = ni una cosa ni otra (una linea encima, o el
 * punto se ha quedado fuera). Se mira una caja de 9x9 y manda la mayoria:
 * un pixel suelto puede ser la costa, que se pinta DESPUES del relleno. */
static int g_t, g_m;          /* lo ultimo que conto que_hay() */

static int que_hay(int16_t px, int16_t py, uint16_t c_tierra, uint16_t c_mar)
{
    const uint16_t *fb = lcd_sim_fb();
    int dx, dy, t = 0, m = 0;

    for (dy = -4; dy <= 4; dy++) {
        for (dx = -4; dx <= 4; dx++) {
            int x = px + dx, y = py + dy;
            uint16_t v;
            if (x < 0 || x >= GFX2_W || y < 0 || y >= ALTO) { continue; }
            v = fb[(size_t)y * GFX2_W + (size_t)x];
            if (v == c_tierra) { t++; }
            else if (v == c_mar) { m++; }
        }
    }
    g_t = t; g_m = m;
    if (t == 0 && m == 0) { return -1; }
    if (t > m * 3) { return 1; }
    if (m > t * 3) { return 0; }
    return -1;
}

typedef struct { const char *n; float lon, lat; int tierra; } sitio_t;

/*
 * OCHO SITIOS QUE SE COMPRUEBAN EN UN ATLAS, y ninguno encima de la
 * rejilla -que va cada 30 grados- ni del ecuador, que se pintan sobre el
 * relleno y ensuciarian la cuenta.
 */
static const sitio_t k_sitios[] = {
    { "Madrid",            -3.7f,  40.4f, 1 },   /* cy=2 */
    { "Atlantico medio",  -30.0f,  35.0f, 0 },
    { "Sahara",            10.0f,  25.0f, 1 },
    { "Pacifico",        -140.0f,   5.0f, 0 },
    { "Amazonia",         -60.0f,  -5.0f, 1 },
    { "Indico",            80.0f, -20.0f, 0 },
    { "Australia centro", 134.0f, -25.0f, 1 },
    { "Siberia",           85.0f,  55.0f, 1 },
};
#define N_SITIOS (sizeof k_sitios / sizeof k_sitios[0])

int main(int argc, char **argv)
{
    /*
     * EL MAPA DE ANTES, Y A PROPOSITO - 16/10/2026.
     *
     * Este banco mide la SIEMBRA por tabla de paridad, que es como se
     * rellenaba al acercarse hasta hoy. Un .map recien hecho ya no trae
     * tabla -cada casilla lleva sus anillos cerrados, ver User/mapa.c- y
     * la radio ni la mira, asi que apuntar aqui a out/mapas_tmp seria
     * medir un camino que el fichero nuevo no recorre.
     *
     * Se queda porque los .map que ya estan grabados en las radios SI son
     * asi. Los genera el objetivo `mapasviejos` con --viejo.
     */
    const char *ruta = (argc > 1) ? argv[1] : "out/mapas_viejos/015.map";
    mapa_tinte_t t;
    uint16_t c_tierra, c_mar;
    unsigned i;
    static const uint8_t k_esc[] = { 4U, 8U };
    unsigned e;

    printf("\n== LA TIERRA, AL ACERCARSE ==\n\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) { printf("  (no esta %s: me salto el banco)\n", ruta); return 0; }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);

    mapa_bin_fuente(lee, 0UL);
    if (mapa_bin_abre_flash(lee, 0UL, (uint32_t)g_tam) != MAPA_BIN_OK) {
        printf("  el .map no abre\n"); return 1;
    }
    mapa_tinte(&t);
    c_tierra = a565(t.tierra);
    c_mar    = a565(t.mar);

    printf("  el fichero: %s   estilo %u\n", ruta, (unsigned)mapa_bin_estilo());

    /* 0. Que el bloque este, porque todo lo demas depende de el. */
    mira(4U, 0.0f, 0.0f, (int16_t)(GFX2_W / 2), (int16_t)(ALTO / 2));
    {
        char d[64];
        snprintf(d, sizeof d, "(%ux%u casillas, %u B cada una)",
                 (unsigned)mapa_bin_nx(), (unsigned)mapa_bin_ny(),
                 (unsigned)mapa_bin_par_bytes());
        di("el nivel troceado trae su bloque de paridad",
           mapa_bin_par_hay() != 0U, d);
    }

    /* 1. Que acierte, en los dos niveles troceados. */
    for (e = 0U; e < sizeof k_esc / sizeof k_esc[0]; e++) {
        int mal = 0;
        printf("\n  1.%u ZOOM x%u: ocho sitios del mundo\n",
               e + 1U, (unsigned)k_esc[e]);
        for (i = 0U; i < N_SITIOS; i++) {
            int16_t px = (int16_t)(GFX2_W / 2), py = (int16_t)(ALTO / 2);
            int r;

            mira(k_esc[e], k_sitios[i].lon, k_sitios[i].lat, px, py);
            if (!mapa_grados_a_px(&g_caja, k_sitios[i].lon, k_sitios[i].lat,
                                  &px, &py)) {
                printf("      %-18s no cae en pantalla\n", k_sitios[i].n);
                mal++;
                continue;
            }
            r = que_hay(px, py, c_tierra, c_mar);
            printf("      %-18s sale %-6s y es %-6s  (%d px de tierra, %d de mar)%s\n",
                   k_sitios[i].n,
                   (r == 1) ? "tierra" : (r == 0) ? "mar" : "ni uno",
                   k_sitios[i].tierra ? "tierra" : "mar", g_t, g_m,
                   (r == k_sitios[i].tierra) ? "" : "   <-- MAL");
            if (r != k_sitios[i].tierra) { mal++; }
        }
        di("los ocho salen del color que les toca", mal == 0,
           "si falla, la semilla no dice la verdad del mundo");
    }

    /*
     * 2. Y QUE NO CAMBIE AL PASEARLO POR LA PANTALLA.
     *
     * Este es el que mide lo que se decidio: sembrar en CADA casilla y no
     * una vez por ventana. Si se sembrara una sola vez, mover la ventana
     * cambiaria cuantas costas quedan entre la semilla y el punto, y un
     * trazo perdido invertiria el color a partir de ahi.
     */
    printf("\n  2. EL MISMO PUNTO, DESDE DIECISIETE VENTANAS\n");
    {
        int mal = 0, peor_i = -1, peor_n = 0;

        for (i = 0U; i < N_SITIOS; i++) {
            int k, cambios = 0, nt = 0, nm = 0, may;
            int v[17];

            for (k = 0; k < 17; k++) {
                int16_t cx = (int16_t)(40 + k * ((GFX2_W - 80) / 16));
                v[k] = -1;
                int16_t px = cx, py = (int16_t)(ALTO / 2);
                int r;

                mira(k_esc[0], k_sitios[i].lon, k_sitios[i].lat, cx, py);
                if (!mapa_grados_a_px(&g_caja, k_sitios[i].lon,
                                      k_sitios[i].lat, &px, &py)) { continue; }
                r = que_hay(px, py, c_tierra, c_mar);
                v[k] = r;
                if (r == 1) { nt++; } else if (r == 0) { nm++; }
            }
            /*
             * SE CUENTA CONTRA LA MAYORIA Y NO CONTRA LA PRIMERA.
             * Contra la primera, una sola ventana mala hacia que las otras
             * dieciseis se contaran como "cambios" y el numero decia
             * dieciseis donde el fallo era uno. El numero que importa es
             * cuantas ventanas se salen del color que dan las demas.
             */
            may = (nt >= nm) ? 1 : 0;
            for (k = 0; k < 17; k++) {
                if (v[k] >= 0 && v[k] != may) { cambios++; }
            }
            if (cambios != 0) {
                mal++;
                if (cambios > peor_n) { peor_n = cambios; peor_i = (int)i; }
            }
            printf("      %-18s %2d cambios de color en 17 ventanas%s\n",
                   k_sitios[i].n, cambios, cambios ? "   <-- MAL" : "");
        }
        {
            char d[80];
            if (peor_i >= 0) {
                snprintf(d, sizeof d, "el peor, %s con %d",
                         k_sitios[peor_i].n, peor_n);
            } else {
                snprintf(d, sizeof d, "la siembra por casilla aguanta");
            }
            di("mover la ventana no le cambia el color a un sitio",
               mal == 0, d);
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

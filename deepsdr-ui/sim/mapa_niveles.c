/*
 * LOS TRES NIVELES Y LA VENTANA (User/mapa_bin.c).
 *
 * LO QUE SE MIDE, y por que cada cosa:
 *
 *  1. Que el x1 se carga entero al abrir el fichero, sin tocar la flash.
 *     Es el que marca el tamano del buffer.
 *  2. Que al pedir zoom 4 y 8 se carga una VENTANA y cabe en el buffer.
 *     Este es el numero que decide si el diseno vale.
 *  3. QUE ARRASTRAR NO RECARGA EN CADA FOTOGRAMA. Se simula un arrastre
 *     de punta a punta y se cuentan las recargas. Si fueran una por
 *     fotograma, el margen no estaria sirviendo para nada y el mapa daria
 *     tirones.
 *  4. Que la ventana trae lo que se ve: un punto conocido dentro de la
 *     pantalla tiene que aparecer entre los trazos cargados.
 *  5. Y que volver al zoom 1 recarga el nivel entero, que es el caso que
 *     mas facil se olvida: el buffer tiene una ventana de otro nivel.
 */
#include "mapa_bin.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-52s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

/* El .map entero en memoria, haciendo de flash. */
static uint8_t  s_map[512U * 1024U];
static uint32_t s_map_n;
static uint32_t s_lecturas, s_bytes_leidos;

static void lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    s_lecturas++; s_bytes_leidos += len;
    if (addr + len > s_map_n) { memset(buf, 0, len); return; }
    memcpy(buf, &s_map[addr], len);
}

/* La pantalla del mapa, en centesimas de grado, para una escala y una
 * esquina. 800x318 px, 2,222 px por grado al zoom 1. */
static void pantalla(uint8_t esc, int32_t lon_c, int32_t lat_c,
                     int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t w = (int32_t)(800.0 * 100.0 / (2.2222 * esc));
    int32_t h = (int32_t)(318.0 * 100.0 / (2.2222 * esc));

    *x0 = lon_c - w / 2; *x1 = lon_c + w / 2;
    *y0 = lat_c - h / 2; *y1 = lat_c + h / 2;
}

static uint32_t cuenta_bytes(void)
{
    mapa_bin_iter_t it;
    int32_t lo, la;
    uint32_t p = 0UL;

    mapa_bin_primero(&it);
    while (mapa_bin_trazo(&it)) { while (mapa_bin_punto(&it, &lo, &la)) { p++; } }
    return p;
}

int main(void)
{
    FILE *f;

    printf("\nLOS TRES NIVELES Y LA VENTANA (User/mapa_bin.c)\n");

    f = fopen("datos/prueba.map", "rb");
    if (f == 0) { printf("  no esta datos/prueba.map\n"); return 1; }
    s_map_n = (uint32_t)fread(s_map, 1U, sizeof s_map, f);
    fclose(f);

    /* ---------------------------------------------------------------- */
    printf("\n  1. EL x1 SE CARGA ENTERO AL ABRIR\n");
    {
        mapa_bin_r_t r;
        uint32_t pt;

        s_lecturas = 0U;
        r = mapa_bin_pon(s_map, s_map_n);
        di("el fichero vale", r == MAPA_BIN_OK, mapa_bin_porque_txt(r));
        di("y no ha hecho falta leer la flash", s_lecturas == 0U, 0);
        pt = cuenta_bytes();
        di("trae el mundo entero", pt > 3000UL, 0);
        printf("       %u trazos, %u puntos, nivel x%u\n",
               (unsigned)mapa_bin_trazos(), (unsigned)pt,
               (unsigned)mapa_bin_nivel());
    }

    mapa_bin_fuente(lee, 0UL);

    /* ---------------------------------------------------------------- */
    printf("\n  2. LA VENTANA DEL x4 Y DEL x8 CABE EN EL BUFFER\n");
    {
        int32_t x0, y0, x1, y1;
        uint8_t esc;

        for (esc = 4U; esc <= 8U; esc *= 2U) {
            uint32_t pt;
            char t[80];

            /* Europa, que es donde el usuario estaba mirando. */
            pantalla(esc, 1000L, 4500L, &x0, &y0, &x1, &y1);
            s_bytes_leidos = 0U;
            (void)mapa_bin_ventana(esc, x0, y0, x1, y1);
            pt = cuenta_bytes();
            sprintf(t, "nivel x%u, %u trazos, %u puntos, %u B leidos",
                    (unsigned)mapa_bin_nivel(), (unsigned)mapa_bin_trazos(),
                    (unsigned)pt, (unsigned)s_bytes_leidos);
            printf("     zoom %u -> %s\n", (unsigned)esc, t);
            di("sube de nivel", mapa_bin_nivel() == esc, 0);
            di("y trae trazos", mapa_bin_trazos() > 10U, 0);
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3. ARRASTRAR NO RECARGA EN CADA FOTOGRAMA\n");
    {
        int32_t x0, y0, x1, y1;
        uint16_t antes = mapa_bin_recargas(), n;
        int32_t lon;
        int pasos = 0;

        /* De punta a punta del mundo al zoom 4, un pixel por paso, que es
         * lo mas rapido que puede ir un dedo. */
        for (lon = -17000L; lon <= 17000L; lon += 45L) {
            pantalla(4U, lon, 4500L, &x0, &y0, &x1, &y1);
            (void)mapa_bin_ventana(4U, x0, y0, x1, y1);
            pasos++;
        }
        n = (uint16_t)(mapa_bin_recargas() - antes);
        printf("     %d pasos de arrastre -> %u recargas (%.1f%% de los pasos)\n",
               pasos, (unsigned)n, 100.0 * n / pasos);
        di("recarga mucho menos que una vez por paso", n < (uint16_t)(pasos / 4), 0);
        di("pero alguna si (si no, no estaria mirando)", n > 0U, 0);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4. LA VENTANA TRAE LO QUE SE VE\n");
    {
        int32_t x0, y0, x1, y1, lo, la;
        mapa_bin_iter_t it;
        int dentro = 0;

        pantalla(8U, 1000L, 4500L, &x0, &y0, &x1, &y1);
        (void)mapa_bin_ventana(8U, x0, y0, x1, y1);
        mapa_bin_primero(&it);
        while (mapa_bin_trazo(&it)) {
            while (mapa_bin_punto(&it, &lo, &la)) {
                if (lo >= x0 && lo <= x1 && la >= y0 && la <= y1) { dentro++; }
            }
        }
        printf("     %d puntos caen dentro de la pantalla\n", dentro);
        di("hay mundo donde se esta mirando", dentro > 50, 0);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5. VOLVER AL ZOOM 1 RECARGA EL NIVEL ENTERO\n");
    {
        int32_t x0, y0, x1, y1;
        uint32_t pt;

        pantalla(1U, 0L, 0L, &x0, &y0, &x1, &y1);
        (void)mapa_bin_ventana(1U, x0, y0, x1, y1);
        pt = cuenta_bytes();
        di("vuelve al x1", mapa_bin_nivel() == 1U, 0);
        di("y con el mundo entero otra vez", pt > 3000UL, 0);
        /*
         * Y QUE LA CUENTA QUE ENSEÑA LA RADIO SEA LA DE AHORA. La primera
         * version solo recontaba en el camino troceado: al volver del
         * zoom 8 al 1 la fila de Informacion decia 373 trazos -los de la
         * ventana anterior- con los puntos ya buenos. Una cuenta que no
         * cambia cuando cambia lo que cuenta no esta contando.
         */
        di("y la cuenta de trazos es la de AHORA, no la de antes",
           mapa_bin_trazos() == 376U, 0);
        printf("       %u trazos, %u puntos\n",
               (unsigned)mapa_bin_trazos(), (unsigned)pt);
    }

    printf(fallos ? "\n  HAY FALLOS: %d\n\n" : "\n  TODO CORRECTO\n\n", fallos);
    return fallos ? 1 : 0;
}

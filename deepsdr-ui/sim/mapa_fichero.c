/*
 * EL FICHERO .map: QUE EL LECTOR DE LA RADIO Y EL EMPAQUETADOR DEL PC
 * ENTIENDAN LO MISMO.
 *
 * Este banco mira DOS cosas distintas y las dos hacen falta:
 *
 *  1. QUE EL LECTOR AGUANTE. Se fabrica aqui mismo un .map con trazos
 *     conocidos y se comprueba que salen por el otro lado EXACTAMENTE
 *     esos. Y despues se rompe de todas las formas en que un fichero se
 *     rompe -marca mala, version de otro dia, suma que no cuadra, mas
 *     grande que la RAM, sin trazos, y el punto del color del mar- para
 *     ver que cada una da su veredicto y NINGUNA pasa por buena.
 *
 *  2. QUE LOS DOS LADOS SE ENTIENDAN. Lo de arriba lo pasaria un lector
 *     que entendiera un formato inventado por el, porque el banco escribe
 *     con las mismas manos con las que lee. Por eso ademas se lee un
 *     fichero DE VERDAD -datos/prueba.map, generado por
 *     tools/mapa_pack.py- y se comprueba que trae un mundo con sentido.
 *     Si algun dia el empaquetador y el lector se separan, salta aqui.
 */
#include "mapa_bin.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-56s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

/* --------------------------------------------------- fabricar un .map */
static uint8_t s_f[32768];
static uint32_t s_n;

static void p16(int v) { s_f[s_n++] = (uint8_t)(v & 0xFF); s_f[s_n++] = (uint8_t)((v >> 8) & 0xFF); }
static void p32(uint32_t v)
{
    s_f[s_n++] = (uint8_t)(v); s_f[s_n++] = (uint8_t)(v >> 8);
    s_f[s_n++] = (uint8_t)(v >> 16); s_f[s_n++] = (uint8_t)(v >> 24);
}
static uint32_t fnv(const uint8_t *b, uint32_t n)
{
    uint32_t v = 0x811C9DC5UL, i;
    for (i = 0; i < n; i++) { v ^= b[i]; v *= 16777619UL; }
    return v;
}

/* Los trazos de prueba, en 1/32 de grado. El segundo lleva a proposito un
 * salto que NO cabe en un byte, para que se use la via de escape. */
#define T1_N 5
#define T2_N 3
static const int t1[T1_N][2] = { {0,0}, {10,-20}, {-5,7}, {120,120}, {-1,-1} };
static const int t2[T2_N][2] = { {-3000,1000}, {400,-400}, {1,1} };

static void fabrica(uint32_t marca_col, uint32_t mar_col, int romper_suma,
                    const char *marca_txt, uint8_t version, int vacio,
                    int gordo)
{
    uint32_t i, off, ini;
    s_n = 0;
    memcpy(&s_f[s_n], marca_txt, 8); s_n += 8;
    s_f[s_n++] = version;
    s_f[s_n++] = 1;                      /* un nivel */
    s_f[s_n++] = 0; s_f[s_n++] = 0;
    for (i = 0; i < 15U; i++) {
        uint32_t c = (i == 10U) ? marca_col : ((i == 0U) ? mar_col : 0x808080UL);
        s_f[s_n++] = (uint8_t)(c >> 16); s_f[s_n++] = (uint8_t)(c >> 8); s_f[s_n++] = (uint8_t)c;
    }
    s_f[s_n++] = 0; s_f[s_n++] = 0; s_f[s_n++] = 0;      /* reservado -> 60 */
    /*
     * LA TABLA DE NIVELES PASA DE 12 A 16 BYTES POR ENTRADA en el v2
     * (12/10/2026): ahora lleva tambien nx y ny -las casillas- y el
     * numero de piezas. Este banco los fabrica a mano, asi que el
     * numero vive aqui tambien; si se vuelven a desincronizar, este
     * bloque se pone en rojo entero, que es lo que acaba de pasar.
     */
    off = 60U + 4U * MAPA_BIN_NIV_CAB;
    ini = s_n;
    /* la tabla de niveles: zoom, trazos, donde, cuanto (se rellena luego) */
    while (s_n < off) { s_f[s_n++] = 0; }
    {
        uint32_t cuerpo = s_n;
        if (!vacio) {
            p16(T1_N); p16(t1[0][0]); p16(t1[0][1]);
            for (i = 1; i < T1_N; i++) {
                s_f[s_n++] = (uint8_t)(int8_t)(t1[i][0] - t1[i-1][0]);
                s_f[s_n++] = (uint8_t)(int8_t)(t1[i][1] - t1[i-1][1]);
            }
            p16(T2_N); p16(t2[0][0]); p16(t2[0][1]);
            for (i = 1; i < T2_N; i++) {
                int dx = t2[i][0] - t2[i-1][0], dy = t2[i][1] - t2[i-1][1];
                if (dx < -127 || dx > 127 || dy < -127 || dy > 127) {
                    s_f[s_n++] = 0x80U; p16(dx); p16(dy);
                } else {
                    s_f[s_n++] = (uint8_t)(int8_t)dx; s_f[s_n++] = (uint8_t)(int8_t)dy;
                }
            }
            p16(0);
        }
        if (gordo) { while ((s_n - cuerpo) < (MAPA_BIN_RAM + 16U)) { p16(0x1111); } }
        /* ahora si: la entrada del nivel, en el formato v2 de 16 bytes:
         *   +0 u16 zoom   +2 u8 nx   +3 u8 ny   +4 u32 off
         *   +8 u32 tam   +12 u16 piezas   +14 u16 reservado
         * nx=0 es "sin trocear", que es lo que tiene que ser el primero. */
        s_f[ini + 0] = 1; s_f[ini + 1] = 0;       /* zoom 1 */
        s_f[ini + 2] = 0; s_f[ini + 3] = 0;       /* nx, ny: entero */
        s_f[ini + 4] = (uint8_t)cuerpo; s_f[ini + 5] = (uint8_t)(cuerpo >> 8);
        s_f[ini + 6] = 0; s_f[ini + 7] = 0;
        { uint32_t t = s_n - cuerpo;
          s_f[ini + 8] = (uint8_t)t; s_f[ini + 9] = (uint8_t)(t >> 8);
          s_f[ini + 10] = (uint8_t)(t >> 16); s_f[ini + 11] = (uint8_t)(t >> 24); }
        s_f[ini + 12] = 2; s_f[ini + 13] = 0;     /* piezas, informativo */
        s_f[ini + 14] = 0; s_f[ini + 15] = 0;
    }
    { uint32_t su = fnv(s_f, s_n); if (romper_suma) { su ^= 1UL; } p32(su); }
}

static int cuadra_trazos(void)
{
    mapa_bin_iter_t it;
    int32_t lon, lat;
    int i, mal = 0;

    mapa_bin_primero(&it);
    if (!mapa_bin_trazo(&it)) { return 0; }
    for (i = 0; i < T1_N; i++) {
        if (!mapa_bin_punto(&it, &lon, &lat)) { return 0; }
        if (lon != (t1[i][0] * 25) / 8 || lat != (t1[i][1] * 25) / 8) { mal++; }
    }
    if (mapa_bin_punto(&it, &lon, &lat)) { mal++; }     /* no debe haber mas */
    if (!mapa_bin_trazo(&it)) { return 0; }
    for (i = 0; i < T2_N; i++) {
        if (!mapa_bin_punto(&it, &lon, &lat)) { return 0; }
        if (lon != (t2[i][0] * 25) / 8 || lat != (t2[i][1] * 25) / 8) { mal++; }
    }
    if (mapa_bin_trazo(&it)) { mal++; }                  /* ni mas trazos */
    return mal == 0;
}

int main(void)
{
    char t[64];
    printf("\nEL FICHERO .map (User/mapa_bin.c)\n");

    printf("\n  1. UNO BUENO\n");
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 0, MAPA_BIN_MARCA, MAPA_BIN_VERSION, 0, 0);
    di("lo acepta", mapa_bin_pon(s_f, s_n) == MAPA_BIN_OK, mapa_bin_porque_txt(mapa_bin_estado()));
    di("y dice que hay mapa", mapa_bin_hay() != 0U, 0);
    snprintf(t, sizeof t, "%u trazos, %u puntos", mapa_bin_trazos(), mapa_bin_puntos());
    di("cuenta los trazos y los puntos", mapa_bin_trazos() == 2U
       && mapa_bin_puntos() == (T1_N + T2_N), t);
    di("los puntos salen EXACTAMENTE los que entraron", cuadra_trazos(), 0);
    snprintf(t, sizeof t, "%06lX", (unsigned long)mapa_bin_color(MAPA_COL_MARCA, 0));
    di("y los colores tambien", mapa_bin_color(MAPA_COL_MARCA, 0) == 0x46E0A0UL
       && mapa_bin_color(MAPA_COL_MAR, 0) == 0x0B1A2AUL, t);

    printf("\n  2. Y LOS ROTOS, UNO POR UNO\n");
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 0, "NOSOYMAP", MAPA_BIN_VERSION, 0, 0);
    di("marca mala", mapa_bin_pon(s_f, s_n) == MAPA_BIN_MARCA_MAL, 0);
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 0, MAPA_BIN_MARCA, 99U, 0, 0);
    di("version de otro dia", mapa_bin_pon(s_f, s_n) == MAPA_BIN_VERSION_MAL, 0);
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 1, MAPA_BIN_MARCA, MAPA_BIN_VERSION, 0, 0);
    di("la suma no cuadra", mapa_bin_pon(s_f, s_n) == MAPA_BIN_SUMA_MAL, 0);
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 0, MAPA_BIN_MARCA, MAPA_BIN_VERSION, 1, 0);
    di("sin trazos", mapa_bin_pon(s_f, s_n) == MAPA_BIN_VACIO, 0);
    fabrica(0x46E0A0UL, 0x0B1A2AUL, 0, MAPA_BIN_MARCA, MAPA_BIN_VERSION, 0, 1);
    di("mas grande que la RAM", mapa_bin_pon(s_f, s_n) == MAPA_BIN_GRANDE, 0);
    /* El punto casi del color del mar: el fallo que mentiria sobre el
     * receptor, porque las marcas desaparecen y parece que no decodifica. */
    fabrica(0x0C1C2CUL, 0x0B1A2AUL, 0, MAPA_BIN_MARCA, MAPA_BIN_VERSION, 0, 0);
    di("el punto no se veria sobre su mar",
       mapa_bin_pon(s_f, s_n) == MAPA_BIN_SIN_CONTRASTE, 0);
    di("y despues de uno malo NO queda mapa puesto", mapa_bin_hay() == 0U, 0);

    printf("\n  3. Y UNO DE VERDAD, DEL EMPAQUETADOR DEL PC\n");
    {
        /*
         * 256 kB Y NO 64: el .map v2 trae tres niveles y mide unos 70 kB.
         * Con los 64 de antes este bloque leia un fichero a medias y
         * decia "no tiene trazos", que es verdad de lo que le dieron y
         * mentira del fichero. Un banco que acota lo que mide por un
         * numero suyo deja de medir en cuanto lo medido crece.
         */
        FILE *f = fopen("datos/prueba.map", "rb");
        static uint8_t b[256U * 1024U];
        size_t n = f ? fread(b, 1, sizeof b, f) : 0U;
        if (f) { fclose(f); }
        di("esta el fichero", n > 0U, 0);
        if (n > 0U) {
            mapa_bin_r_t r = mapa_bin_pon(b, (uint32_t)n);
            snprintf(t, sizeof t, "%s", mapa_bin_porque_txt(r));
            di("lo lee el lector de la radio", r == MAPA_BIN_OK, t);
            snprintf(t, sizeof t, "%u trazos, %u puntos, %u bytes",
                     mapa_bin_trazos(), mapa_bin_puntos(), (unsigned)n);
            di("y trae un mundo con sentido",
               mapa_bin_trazos() > 20U && mapa_bin_puntos() > 1000U, t);
            /* Que los puntos caigan DENTRO del mundo: un fallo de signo o
             * de escala en los saltos se ve aqui y no mirando la foto. */
            {
                mapa_bin_iter_t it;
                int32_t lon, lat;
                int fuera = 0;
                mapa_bin_primero(&it);
                while (mapa_bin_trazo(&it)) {
                    while (mapa_bin_punto(&it, &lon, &lat)) {
                        if (lon < -18000 || lon > 18000 ||
                            lat < -7200  || lat > 7200) { fuera++; }
                    }
                }
                snprintf(t, sizeof t, "%d fuera", fuera);
                di("ni un punto cae fuera del mundo", fuera == 0, t);
            }
        }
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

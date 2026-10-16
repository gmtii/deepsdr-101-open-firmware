/*
 * LAS RAYAS DEL MAPA AL ACERCARSE.
 *
 * Pedido por el usuario: "a partir del zoom 2 empiezan a salir rayas".
 *
 * QUE ES UNA RAYA. Un renglon horizontal, de uno o dos pixeles de alto,
 * del color del mar cruzando la tierra o del color de la tierra cruzando
 * el mar, con lo mismo encima y debajo. Sale de que el relleno se pinta
 * por par-impar y la paridad arranca al reves.
 *
 * POR QUE HACIA FALTA ESTE BANCO Y NO VALIA EL QUE HABIA. sim/mapa_pant.c
 * ya buscaba rayas, pero solo en la tabla de serie k_mapa_pts y solo a
 * escala 1: nunca miraba un .map del disco ni ninguna otra escala. O sea
 * que miraba justo donde el fallo no estaba, y por eso aprobaba mientras
 * la radio rayaba. Un banco que no mira el codigo prueba su propia copia.
 *
 * COMO SE MIDE, Y POR QUE ASI. Contar cuanta tierra hay en cada fila y
 * compararla con las vecinas no sirve: donde la costa sube o baja
 * deprisa, las vecinas ya son distintas de por si. Asi que se mira PIXEL
 * A PIXEL: cuenta los que son de un color de relleno, tienen el OTRO
 * encima y debajo, y arriba y abajo coinciden. Ese pixel no puede estar
 * bien de ninguna manera. Se mira a una fila de distancia y a dos, para
 * pillar tambien las rayas de dos pixeles de alto, y hace falta una
 * carrera de RAYA_MIN seguidos para que cuente: dos pixeles sueltos son
 * una costa picada, no una raya.
 *
 * SE PROBO QUE SE PONE ROJO. Con los .map de antes del 16/10/2026 -los
 * que se rellenaban con la tabla de paridad, ver tools/mapa_pack.py
 * --viejo- este banco cuenta 33 rayas al x4 y 10 al x8 en el mapa 036
 * centrado en Europa, que son exactamente las de la foto.
 *
 *
 * Y HACE UNA SEGUNDA COSA, QUE ESTABA AQUI ANTES - 14/10/2026.
 *
 * Con dos argumentos -un .map y un fichero de salida- no cuenta nada:
 * vuelca los pixeles de un barrido del mundo en las escalas que rellenan.
 * Sirve para compilar DOS binarios, el del firmware y uno de referencia
 * con MAPA_CRUCES_MAX alto, y exigir que pinten EL MISMO PIXEL. Asi se
 * ve si el tope de cruces del firmware esta cortando filas.
 *
 * Se comparan los PIXELES y no las rayas contadas: contarlas dio en su
 * dia un barrido NO MONOTONO -42 cruces mejor que 44- porque dos filas
 * caidas pegadas no las ve el detector.
 *
 * La referencia va a 128 y no mas arriba: s_ncruce[] es uint8_t, asi que
 * un tope de 256 no se alcanza NUNCA -la comparacion `>= 256` es siempre
 * falsa- y el barrido escribiria fuera de la tabla.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include "mapa_bin.h"
#include <stdio.h>
#include <string.h>

/* El panel del simulador, tal cual: un pixel es un uint16_t RGB565. */
static uint16_t px_de(int x, int y)
{
    return lcd_sim_fb()[(size_t)y * LCD_W + (size_t)x];
}

const palette_t *g_pal = &k_pal_oscura;

#define RAYA_MIN   40      /* pixeles seguidos para que cuente como raya */
#define MAPA_Y0    42
#define MAPA_Y1   438
#define MAPA_X0      2
#define MAPA_X1    798

static int fallos;
static uint8_t F[400000];
static uint32_t L;
static mapa_caja_t g_c;

static void lee(uint32_t o, uint8_t *d, uint32_t n)
{
    if (o + n <= L) { memcpy(d, F + o, n); }
    else            { memset(d, 0, n); }
}

static void pinta(gfx2_surf_t *s, void *ctx) { (void)ctx; mapa_dibuja(s, &g_c); }

/*
 * Los dos colores mas vistos dentro del mapa son el mar y la tierra. Si
 * el segundo no llega al 5% de la superficie NO ES UN RELLENO: son las
 * costas, o el borde del panel, y entonces aqui no hay nada que mirar.
 * Sin esta guarda, un mapa que no rellena daba por "tierra" cualquier
 * cosa y el banco se inventaba rayas que no existen.
 */
#define RELLENO_MIN_PCT 5

static void dos_colores(uint16_t *mar, uint16_t *tie)
{
    static uint16_t cuenta[1 << 16];
    int x, y;
    uint32_t a = 0U, b = 1U;

    memset(cuenta, 0, sizeof cuenta);
    for (y = MAPA_Y0; y < MAPA_Y1; y++) {
        for (x = MAPA_X0; x < MAPA_X1; x++) {
            uint16_t c = px_de(x, y);
            if (cuenta[c] < 0xFFFFU) { cuenta[c]++; }
        }
    }
    for (x = 0; x < (1 << 16); x++) {
        if (cuenta[x] > cuenta[a]) { b = a; a = (uint32_t)x; }
        else if ((uint32_t)x != a && cuenta[x] > cuenta[b]) { b = (uint32_t)x; }
    }
    if ((uint32_t)cuenta[b] * 100UL
        < (uint32_t)(MAPA_Y1 - MAPA_Y0) * (uint32_t)(MAPA_X1 - MAPA_X0)
          * RELLENO_MIN_PCT) {
        b = a;                       /* no hay relleno: mar == tierra */
    }
    *mar = (uint16_t)a; *tie = (uint16_t)b;
}

/*
 * LA REJILLA NO ES UNA RAYA. Los paralelos y el ecuador se pintan ENCIMA
 * del relleno y cruzan el mapa de punta a punta, asi que en un tipo donde
 * su color cae en el mismo RGB565 que la tierra son indistinguibles de
 * una raya mirando pixeles. Se saltan por donde se pintan, preguntandole
 * al dibujante mismo en que fila cae cada paralelo -no por el color, que
 * es justo lo que se confunde-.
 */
static int es_rejilla(int y)
{
    int g;

    for (g = -60; g <= 60; g += 30) {
        int16_t px, py;

        if (!mapa_grados_a_px(&g_c, 0.0f, (float)g, &px, &py)) { continue; }
        if (y >= py - 2 && y <= py + 2) { return 1; }
    }
    return 0;
}

static int cuenta_rayas(void)
{
    uint16_t mar, tie;
    int alto, y, x, n = 0;

    dos_colores(&mar, &tie);
    if (mar == tie) { return 0; }
    for (alto = 1; alto <= 2; alto++) {
        for (y = MAPA_Y0 + alto; y < MAPA_Y1 - alto; y++) {
            int carrera = 0, visto = 0;

            if (es_rejilla(y) || es_rejilla(y - alto) || es_rejilla(y + alto)) {
                continue;
            }

            for (x = MAPA_X0; x <= MAPA_X1; x++) {
                int malo = 0;

                if (x < MAPA_X1) {
                    uint16_t a = px_de(x, y - alto);
                    uint16_t b = px_de(x, y);
                    uint16_t c = px_de(x, y + alto);
                    int k, segui = 1;

                    for (k = 0; k < alto; k++) {
                        if (px_de(x, y + k) != b) { segui = 0; }
                    }
                    malo = segui && a == c && a != b
                        && (a == mar || a == tie) && (b == mar || b == tie);
                }
                if (malo) { carrera++; }
                else { if (carrera >= RAYA_MIN) { visto = 1; } carrera = 0; }
            }
            if (visto) { n++; }
        }
    }
    return n;
}

/* Sitios del mundo con costa picada, que es donde esto se rompe. */
static const struct { float lon, lat; const char *n; } k_sitios[] = {
    {  15.0f,  50.0f, "Europa"    }, {  23.0f,  38.0f, "Egeo"      },
    { -80.0f,  25.0f, "Caribe"    }, { 120.0f,  10.0f, "Filipinas" },
    {  -5.0f,  58.0f, "Escocia"   }, { 140.0f,  38.0f, "Japon"     },
    { 100.0f,   2.0f, "Indonesia" }, {  14.0f,  60.0f, "Baltico"   },
    { -70.0f, -45.0f, "Patagonia" }, {-123.0f,  50.0f, "Vancouver" },
    { 179.0f,  -5.0f, "Antimerid" }, {   0.0f,   0.0f, "Golfo"     },
    { -45.0f,  65.0f, "Groenland" }, {  58.0f,  25.0f, "Persico"   }
};
static const uint8_t k_zoom[] = { 1U, 2U, 4U, 8U };

static int mira(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    unsigned s, z;
    int total = 0;

    if (!f) { return -1; }
    L = (uint32_t)fread(F, 1U, sizeof F, f);
    fclose(f);
    if (mapa_bin_abre_flash(lee, 0UL, L) != MAPA_BIN_OK) { return -1; }
    if (mapa_bin_estilo() == MAPA_ESTILO_CONTORNO) { return -2; }  /* no rellena */

    for (z = 0U; z < sizeof k_zoom / sizeof k_zoom[0]; z++) {
        for (s = 0U; s < sizeof k_sitios / sizeof k_sitios[0]; s++) {
            int16_t px, py;
            int n;

            memset(&g_c, 0, sizeof g_c);
            mapa_pon(&g_c, 0, 40, 800, 400);
            mapa_zoom(&g_c, k_zoom[z], 400, 240);
            if (mapa_grados_a_px(&g_c, k_sitios[s].lon, k_sitios[s].lat,
                                 &px, &py)) {
                mapa_arrastra(&g_c, (int16_t)(400 - px), (int16_t)(240 - py));
            }
            mapa_ventana(&g_c);
            lcd_sim_reset(0x1083U);
            gfx2_render_screen(pinta, 0);
            n = cuenta_rayas();
            if (n != 0) {
                printf("    %s  x%u  %-10s  %d RAYAS\n",
                       ruta, k_zoom[z], k_sitios[s].n, n);
                total += n;
            }
        }
    }
    return total;
}

/*
 * EL VOLCADO: los pixeles de un barrido, para comparar dos binarios.
 * Un fichero por cada (zoom, sitio), uno detras de otro.
 */
static int vuelca(const char *ruta_map, const char *salida)
{
    FILE *f = fopen(salida, "wb");
    unsigned s, z;

    if (!f) { printf("  no puedo escribir %s\n", salida); return 1; }
    { FILE *m = fopen(ruta_map, "rb");
      if (!m) { fclose(f); printf("  no esta %s\n", ruta_map); return 1; }
      L = (uint32_t)fread(F, 1U, sizeof F, m);
      fclose(m); }
    if (mapa_bin_abre_flash(lee, 0UL, L) != MAPA_BIN_OK) {
        fclose(f); printf("  %s no se deja abrir\n", ruta_map); return 1;
    }
    for (z = 0U; z < sizeof k_zoom / sizeof k_zoom[0]; z++) {
        for (s = 0U; s < sizeof k_sitios / sizeof k_sitios[0]; s++) {
            int16_t px, py;

            memset(&g_c, 0, sizeof g_c);
            mapa_pon(&g_c, 0, 40, 800, 400);
            mapa_zoom(&g_c, k_zoom[z], 400, 240);
            if (mapa_grados_a_px(&g_c, k_sitios[s].lon, k_sitios[s].lat,
                                 &px, &py)) {
                mapa_arrastra(&g_c, (int16_t)(400 - px), (int16_t)(240 - py));
            }
            mapa_ventana(&g_c);
            lcd_sim_reset(0x1083U);
            gfx2_render_screen(pinta, 0);
            if (fwrite(lcd_sim_fb(), 2U, (size_t)LCD_W * LCD_H, f)
                != (size_t)LCD_W * LCD_H) {
                fclose(f); printf("  no he podido escribir entero\n"); return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    /* Con dos argumentos, el modo volcado. Ver la cabecera. */
    if (argc >= 3) { return vuelca(argv[1], argv[2]); }

    /* Uno de cada estilo que rellena: tierra rellena, de puntos, silueta. */
    /* Los tres estilos que rellenan, y de cada uno un par de paletas
     * distintas: el color no cambia la geometria, pero si cambia lo que
     * el contador ve como relleno, y eso tambien se mide. */
    static const char *k_mapas[] = {
        "../mapas/002.map", "../mapas/003.map", "../mapas/005.map",
        "../mapas/015.map", "../mapas/028.map", "../mapas/036.map",
        "../mapas/072.map", "../mapas/100.map"
    };
    unsigned i;
    int vistos = 0, total = 0;

    printf("  RAYAS DEL MAPA\n");
    for (i = 0U; i < sizeof k_mapas / sizeof k_mapas[0]; i++) {
        int n = mira(k_mapas[i]);

        if (n == -1) {
            printf("    %-22s no esta, me lo salto\n", k_mapas[i]);
            continue;
        }
        if (n == -2) { continue; }       /* de contorno: no rellena */
        vistos++;
        total += n;
    }
    if (vistos == 0) {
        printf("    NO HE MIRADO NINGUN MAPA QUE RELLENE. Un banco que no\n"
               "    prueba nada no aprueba: hacen falta los .map en ../mapas.\n");
        fallos++;
    } else {
        printf("    %-58s %s\n",
               "ni una raya en ningun mapa, zoom ni ventana",
               total == 0 ? "bien" : "MAL");
        if (total != 0) { fallos++; }
    }
    printf("  %s\n", fallos ? "*** MAL ***" : "todo correcto");
    return fallos ? 1 : 0;
}

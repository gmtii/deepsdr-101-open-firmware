/*
 * ======================================================================
 * QUE LA TIERRA SE RELLENE, Y SOLO DONDE TOCA - 14/10/2026
 * ======================================================================
 *
 * Esto NO mide lo que costaria -eso es sim/mapa_relleno.c, que se hizo
 * antes de escribir una linea de firmware y dijo razon 0,19-. Esto mide que
 * EL DIBUJANTE DE LA RADIO lo haga, y que lo haga bien, pintando el mapa
 * con el codigo de verdad de User/mapa.c y contando los pixeles que salen.
 *
 * LAS TRES COSAS QUE COMPRUEBA, y cada una caza una averia distinta:
 *
 *   1. UN MAPA DE RELLENO SE RELLENA. Si el bit del anillo no llega, o el
 *      estilo se lee del byte equivocado, aqui sale cero tierra.
 *
 *   2. CUANTA. Es el mismo numero que ya daba el empaquetador -la tierra es
 *      el 23 % de la ventana- pero contado AL OTRO LADO: alli sobre los
 *      anillos en grados, aqui sobre los pixeles que acaban en pantalla. Si
 *      el relleno se derrama -un anillo que no cierra, un cruce que falta-
 *      sale el mundo entero, que es exactamente lo que ocurrio en el
 *      empaquetador y por lo que ese numero esta en los dos sitios.
 *
 *   3. UN MAPA DE CONTORNO NO SE RELLENA. El estilo manda por los dos
 *      lados: rellenar un mapa que se llama "contorno" es tan fallo como no
 *      rellenar uno que se llama "tierra rellena".
 *
 * Los dos .map los fabrica el propio banco llamando a mapa_pack.py, asi que
 * mide la cadena entera: empaquetador, lector y dibujante.
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
    printf("    %-46s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

static uint16_t a565(uint32_t hex)
{
    return (uint16_t)((((hex >> 16) & 0xF8U) << 8)
                    | (((hex >> 8)  & 0xFCU) << 3)
                    |  ((hex        & 0xF8U) >> 3));
}

static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &g_caja);
}

static uint8_t g_buf[256U * 1024U];

/* Carga un .map y devuelve el tanto por ciento del panel que acaba del
 * color de la tierra. -1 si el fichero no se puede ni leer. */
static double pinta_y_cuenta(const char *ruta, unsigned *estilo,
                             unsigned *racha_max)
{
    FILE *f = fopen(ruta, "rb");
    size_t n = f ? fread(g_buf, 1, sizeof g_buf, f) : 0U;
    mapa_tinte_t t;
    const uint16_t *fb;
    uint16_t c_tierra, c_mar;
    unsigned i, tierra = 0U, mar = 0U;

    if (f) { fclose(f); }
    if (n == 0U) { return -1.0; }
    if (mapa_bin_pon(g_buf, (uint32_t)n) != MAPA_BIN_OK) { return -1.0; }
    *estilo = mapa_bin_estilo();

    memset(&g_caja, 0, sizeof g_caja);
    mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);

    mapa_tinte(&t);
    /* gfx2 trabaja en RGBA y la pantalla en RGB565: para comparar pixeles
     * hay que bajar a 565 con la misma cuenta que usa el panel. */
    c_tierra = a565(t.tierra);
    c_mar    = a565(t.mar);

    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);

    fb = lcd_sim_fb();
    for (i = 0U; i < (unsigned)GFX2_W * ALTO; i++) {
        if (fb[i] == c_tierra) { tierra++; }
        else if (fb[i] == c_mar) { mar++; }
    }

    /*
     * Y LA RACHA HORIZONTAL MAS LARGA, que es lo que de verdad distingue
     * un relleno de una linea.
     *
     * La primera version contaba solo el tanto por ciento, y el mapa de
     * contorno daba 3,1 % de "tierra". No era un relleno: en esa paleta el
     * color de la tierra COINCIDE en RGB565 con el de la rejilla, asi que
     * se estaban contando las lineas de la rejilla. Un banco que cuenta
     * pixeles sueltos no sabe distinguir un color de una forma.
     *
     * Un relleno deja rachas de cientos de pixeles seguidos; una rejilla
     * vertical deja rachas de uno. Eso no depende de que dos colores de la
     * paleta se parezcan.
     */
    {
        unsigned y, x, racha = 0U;
        *racha_max = 0U;
        for (y = 0U; y < (unsigned)ALTO; y++) {
            racha = 0U;
            for (x = 0U; x < (unsigned)GFX2_W; x++) {
                if (fb[y * GFX2_W + x] == c_tierra) {
                    racha++;
                    if (racha > *racha_max) { *racha_max = racha; }
                } else {
                    racha = 0U;
                }
            }
        }
    }
    /*
     * El tanto por ciento se saca sobre MAR+TIERRA y no sobre el panel
     * entero: encima hay rejilla, ecuador y costas, y esos pixeles no son
     * ni una cosa ni la otra. Contarlos como mar haria que el numero
     * bajara al cambiar el grosor de una linea, que no tiene nada que ver
     * con lo que se esta midiendo.
     */
    return (tierra + mar) ? (100.0 * tierra / (double)(tierra + mar)) : 0.0;
}

int main(int argc, char **argv)
{
    const char *relleno = (argc > 1) ? argv[1] : "out/mapas_tmp/015.map";
    const char *contorno = (argc > 2) ? argv[2] : "out/mapas_tmp/014.map";
    unsigned est = 99U, racha_r = 0U, racha_c = 0U;
    double pct_r, pct_c;
    char det[200];

    printf("\nQUE LA TIERRA SE RELLENE (y solo donde toca)\n");
    printf("============================================\n\n");

    printf("  1. un mapa de TIERRA RELLENA: %s\n", relleno);
    pct_r = pinta_y_cuenta(relleno, &est, &racha_r);
    if (pct_r < 0.0) {
        di("se puede leer", 0, "no esta o no lo acepta el lector");
    } else {
        snprintf(det, sizeof det, "(el fichero dice %u)", est);
        di("el estilo dice relleno", est == MAPA_ESTILO_RELLENO, det);

        snprintf(det, sizeof det, "(%.1f %% del agua+tierra del panel)", pct_r);
        di("se rellena algo", pct_r > 1.0, det);
        /*
         * EL MARGEN, Y POR QUE ES ESTE. El empaquetador mide 29,7 % de la
         * ventana de +-71,5 grados. El panel son 288 px a 2,22 px por
         * grado, o sea +-64,8: recorta por arriba y por abajo, donde hay
         * mucha tierra (Siberia, Canada, Escandinavia), asi que aqui el
         * numero tiene que salir ALGO MENOR, y sale: 27,2 %. 12-32 % deja
         * sitio para esa diferencia y sigue cazando las dos averias de
         * verdad: cero tierra, o el mundo entero relleno.
         *
         * Y la coincidencia con el 29 % conocido de tierra del planeta es
         * eso, una coincidencia util pero no exacta: ese 29 es de area
         * REAL y esto son grados cuadrados, que estiran las latitudes
         * altas, y ademas falta casi toda la Antartida.
         */
        snprintf(det, sizeof det, "(%.1f %%, el empaquetador dice 29,7 en "
                 "su ventana, mas ancha)", pct_r);
        di("y lo que se rellena es tierra, no el mundo",
           pct_r >= 12.0 && pct_r <= 32.0, det);

        snprintf(det, sizeof det, "(%u px seguidos)", racha_r);
        di("y son rachas, o sea relleno y no lineas", racha_r > 40U, det);
    }

    printf("\n  2. un mapa de CONTORNO: %s\n", contorno);
    pct_c = pinta_y_cuenta(contorno, &est, &racha_c);
    if (pct_c < 0.0) {
        di("se puede leer", 0, "no esta o no lo acepta el lector");
    } else {
        snprintf(det, sizeof det, "(el fichero dice %u)", est);
        di("el estilo dice contorno", est == MAPA_ESTILO_CONTORNO, det);
        /*
         * AQUI NO VALE UN UMBRAL FIJO, Y SE DESCUBRIO MIDIENDO.
         *
         * La primera version pedia "menos del 1 % de pixeles de tierra" y
         * el mapa de contorno daba 3,1 %. No era un relleno: en la paleta
         * "gris FMDX" el color de la tierra y el de la COSTA son el mismo
         * -8A8A8A los dos-, asi que cada pixel de costa cuenta como tierra.
         *
         * Y eso no es un defecto de la paleta: de los 51 tipos que
         * rellenan, tres tienen tierra = costa, y son justo los de estilo
         * silueta y los grises. Una silueta es eso: la tierra y su borde
         * del mismo color. Poner una regla de contraste los habria
         * rechazado por hacer lo que su nombre promete.
         *
         * Asi que lo que se compara son LOS DOS DIBUJOS, que comparten
         * paleta y geometria y solo se diferencian en el byte de estilo.
         * Un umbral entre los dos no necesita saber nada de colores.
         */
        snprintf(det, sizeof det, "(%.1f %% contra %.1f %% del relleno; "
                 "rachas %u contra %u)", pct_c, pct_r, racha_c, racha_r);
        di("no se rellena nada", pct_c * 4.0 < pct_r && racha_c * 4U < racha_r,
           det);
    }

    lcd_sim_write_ppm("out/mapa_tierra.ppm");
    printf("\n  foto en out/mapa_tierra.ppm\n");
    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

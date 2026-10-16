/*
 * ======================================================================
 * LO QUE COSTARIA RELLENAR TAMBIEN AL ACERCARSE - 14/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Hoy el relleno se ve al zoom 1 y no al acercarse. El motivo esta escrito
 * en mapa.c y en mapa_pack.py: el x1 guarda los trazos ENTEROS y el
 * empaquetador marca cuales son anillos; los niveles troceados parten cada
 * trazo por las casillas, y un anillo partido ya no cierra.
 *
 * ANTES DE ESCRIBIR UN DIBUJANTE, LOS NUMEROS. Es lo que se hizo con el
 * relleno del x1 -sim/mapa_relleno.c- y recorto el trabajo un 99 %.
 *
 * Y HAY UNA PREGUNTA QUE PUEDE MATAR EL TRABAJO ENTERO, asi que va primera:
 *
 *   ¿De verdad hace falta cerrar por casilla?
 *
 * El relleno par-impar no cuenta anillos: cuenta CRUCES de cada linea de
 * barrido contra las aristas. Si un trazo se parte en dos por el borde de
 * una casilla y las DOS piezas se dibujan en el mismo fotograma, las dos
 * comparten el punto del corte y el juego de aristas es el MISMO que si el
 * trazo fuera entero. El corte es interior y al barrido le da igual.
 *
 * O sea que el troceado por casillas NO rompe el relleno por si mismo. Lo
 * que lo rompe son los extremos que se quedan colgando en el borde de la
 * VENTANA -una costa que entra y sale de la pantalla-, y eso pasa igual al
 * zoom 1 y ya esta resuelto alli cerrando contra el rectangulo.
 *
 * Si eso es cierto, el trabajo no es "cerrar casilla por casilla" -que es
 * lo que decia la nota- sino "cerrar contra la ventana", que es mucho
 * menos. Este banco lo comprueba contando, sobre la geometria de verdad:
 *
 *   1. CRUCES POR FILA. Si pasan de CRUCES_HOY la fila se descarta y
 *      el relleno sale a rayas. Es el numero que decide si cabe en RAM.
 *   2. EXTREMOS SUELTOS. Puntas que no las toca ningun otro segmento. Son
 *      las que habria que cerrar. Si al acercarse salen pocas, el trabajo
 *      es pequeño; si salen cientos, hay que cambiar de enfoque.
 *
 * Lo que este banco NO hace es dibujar nada. Mide.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-52s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

/* CRUCES_HOY vive dentro de mapa.c, no en la cabecera: se repite aqui
 * el valor para poder compararlo, y si alguien lo cambia alli este banco
 * habla de otro numero. Es un acoplamiento feo pero VISIBLE, que es mejor
 * que no poder comparar contra nada. */
#define CRUCES_HOY 56
#define ALTO     288
#define MAX_SEG  60000

typedef struct { int16_t x0, y0, x1, y1; } seg_t;
static seg_t  g_seg[MAX_SEG];
static int    g_nseg;
static int    g_capturando;

/*
 * COSTA O FRONTERA. El relleno solo puede contar costas: una frontera de
 * pais no separa tierra de agua. El iterador lo dice en todos los niveles
 * -it.frontera-, y en la salida de gfx2_line() los dos se distinguen por
 * el COLOR, que es lo unico que llega hasta aqui.
 */
static gfx2_rgba_t g_col_costa, g_col_front;
static int igual(gfx2_rgba_t a, gfx2_rgba_t b)
{ return a.r == b.r && a.g == b.g && a.b == b.b; }
static int g_n_costa, g_n_front;

void __real_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c);

void __wrap_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c)
{
    if (g_capturando) {
        if (igual(c, g_col_costa)) { g_n_costa++; }
        else if (igual(c, g_col_front)) { g_n_front++; }
    }
    if (g_capturando && g_nseg < MAX_SEG) {
        g_seg[g_nseg].x0 = x0; g_seg[g_nseg].y0 = y0;
        g_seg[g_nseg].x1 = x1; g_seg[g_nseg].y1 = y1;
        g_nseg++;
    }
    __real_gfx2_line(s, x0, y0, x1, y1, c);
}

/*
 * COSTA O FRONTERA. El relleno solo puede contar costas: una frontera de
 * pais no separa tierra de agua. El iterador lo dice en todos los niveles
 * -it.frontera-, asi que esto se sabe tambien al acercarse. Se cuenta
 * interceptando el COLOR con que se dibuja cada segmento, que es lo unico
 * que distingue a los dos en la salida de gfx2_line().
 */
static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx) { (void)ctx; mapa_dibuja(s, &g_caja); }

/* --- 1. cruces por fila ------------------------------------------- */
static int g_cruces[ALTO];

static void cuenta_cruces(void)
{
    int i, y;
    memset(g_cruces, 0, sizeof g_cruces);
    for (i = 0; i < g_nseg; i++) {
        int ylo = g_seg[i].y0, yhi = g_seg[i].y1;
        if (ylo == yhi) { continue; }            /* horizontal: no cruza */
        if (ylo > yhi) { int t = ylo; ylo = yhi; yhi = t; }
        for (y = ylo; y < yhi; y++) {            /* [ylo, yhi), como mapa.c */
            if (y >= 0 && y < ALTO) { g_cruces[y]++; }
        }
    }
}

/* --- 2. extremos sueltos ------------------------------------------ */
/*
 * Una punta esta "suelta" si ningun OTRO segmento toca ese mismo punto. Se
 * compara por coordenada entera de pantalla, que es como llegan: los trazos
 * contiguos comparten el punto exacto porque el que dibuja los encadena.
 */
static int toca(int16_t x, int16_t y, int salvo)
{
    int i;
    for (i = 0; i < g_nseg; i++) {
        if (i == salvo) { continue; }
        if ((g_seg[i].x0 == x && g_seg[i].y0 == y) ||
            (g_seg[i].x1 == x && g_seg[i].y1 == y)) { return 1; }
    }
    return 0;
}

static void cuenta_puntas(int *sueltas, int *en_borde)
{
    int i, s = 0, b = 0;
    for (i = 0; i < g_nseg; i++) {
        int16_t px[2], py[2]; int k;
        px[0] = g_seg[i].x0; py[0] = g_seg[i].y0;
        px[1] = g_seg[i].x1; py[1] = g_seg[i].y1;
        for (k = 0; k < 2; k++) {
            if (toca(px[k], py[k], i)) { continue; }
            s++;
            if (px[k] <= 1 || px[k] >= GFX2_W - 2 || py[k] <= 1 || py[k] >= ALTO - 2) { b++; }
        }
    }
    *sueltas = s; *en_borde = b;
}

int main(void)
{
    static const uint8_t escalas[] = { 1U, 2U, 4U, 8U };
    unsigned e;
    int peor_cruces = 0;

    printf("\n== RELLENAR TAMBIEN AL ACERCARSE: LOS NUMEROS PRIMERO ==\n");
    printf("\n  un fotograma de 800x%d, geometria capturada de gfx2_line()\n", ALTO);
    printf("  (CRUCES_HOY vale %d hoy)\n\n", CRUCES_HOY);

    for (e = 0; e < sizeof escalas / sizeof escalas[0]; e++) {
        int y, maxc = 0, filas_pasadas = 0, con_cruces = 0;
        int sueltas = 0, en_borde = 0;
        long suma = 0;

        memset(&g_caja, 0, sizeof g_caja);
        mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);
        if (escalas[e] > 1U) {
            (void)mapa_zoom(&g_caja, escalas[e], GFX2_W / 2, ALTO / 2);
        }

        { mapa_tinte_t t; mapa_tinte(&t);
          g_col_costa = gfx2_rgb(t.costa); g_col_front = gfx2_rgb(t.frontera); }
        g_n_costa = 0; g_n_front = 0;
        g_nseg = 0; g_capturando = 1;
        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);
        g_capturando = 0;

        cuenta_cruces();
        for (y = 0; y < ALTO; y++) {
            if (g_cruces[y] > maxc) { maxc = g_cruces[y]; }
            if (g_cruces[y] > CRUCES_HOY) { filas_pasadas++; }
            if (g_cruces[y] > 0) { con_cruces++; }
            suma += g_cruces[y];
        }
        if (maxc > peor_cruces) { peor_cruces = maxc; }
        cuenta_puntas(&sueltas, &en_borde);

        printf("    escala %u\n", (unsigned)escalas[e]);
        printf("      segmentos dibujados                  %7d   (costa %d, frontera %d)\n",
               g_nseg, g_n_costa, g_n_front);
        printf("      cruces por fila: maximo %3d   media %5.1f   filas con costa %d\n",
               maxc, con_cruces ? (double)suma / con_cruces : 0.0, con_cruces);
        printf("      filas que se pasarian de %2d          %7d\n",
               CRUCES_HOY, filas_pasadas);
        printf("      extremos sueltos  %4d   de ellos pegados al borde  %4d  (%d%%)\n",
               sueltas, en_borde, sueltas ? (100 * en_borde) / sueltas : 0);
        printf("\n");
    }

    printf("  LO QUE DICEN ESTOS NUMEROS\n\n");
    di(peor_cruces <= CRUCES_HOY,
       "la tabla de cruces de hoy aguanta todas las escalas",
       "hay que subir CRUCES_HOY, y eso cuesta .bss");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

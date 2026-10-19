/*
 * EL ESPECTRO, TEMA POR TEMA, MEDIDO SOBRE LOS PIXELES - 17/10/2026.
 *
 * De un issue que pedia fondo claro para el espectro con los temas claros.
 * El arreglo fue que spectrum.c pasara a leer la paleta (campo
 * espectro_claro de palette.h); esto es lo que comprueba que el arreglo
 * sirve y, sobre todo, QUE NO SE HA LLEVADO POR DELANTE A NADIE.
 *
 * POR QUE MIDE PIXELES Y NO CONSTANTES. Porque leer los #define contesta
 * a otra pregunta: diria que los colores que el codigo PIENSA usar
 * contrastan. Lo que hay que saber es de que color acaba cada pixel
 * despues de pasar por el estilo, la LUT, el tinte de la banda y el orden
 * de pintado -siete prioridades que se pisan unas a otras-. Asi que esto
 * renderiza la escena de verdad, con la geometria de verdad, y lee el
 * marco.
 *
 * LAS DOS COSAS QUE COMPRUEBA
 *
 *   1. LOS OCHO DE ANTES NO SE HAN MOVIDO. Es la mitad que importa. El
 *      fondo que le sale a cada tema sin espectro_claro tiene que ser
 *      EXACTAMENTE el de siempre -negro en HEATMAP, el azul marino de
 *      SPEC_LINE_BG en LINE/OUTLINE-. Un tema oscuro que cambie un solo
 *      bit aqui es haberle cambiado la pantalla a quien no pidio nada,
 *      que es el fallo que este proyecto ya ha pagado dos veces (ver
 *      mapa_sigue en palette.h).
 *
 *   2. LOS QUE PIDEN ESPECTRO CLARO SE VEN. Traza, rejilla, marca de
 *      demodulacion y punto de pico, cada uno contra EL FONDO QUE LE SALE
 *      A ESE TEMA, no contra un fondo supuesto.
 *
 * LOS UMBRALES, Y DE DONDE SALEN. Son los mismos de spec_rotulos.c, que
 * ya los tenia medidos contra paneles de verdad:
 *
 *   traza y marca   >= 3,0:1   lo que separa "se ve" de "hay que buscarlo"
 *   rejilla          entre 1,04 y 2,2:1
 *
 * La rejilla es la unica con techo Y suelo, y es a proposito: su
 * comentario en palette.h dice "recesiva a proposito". Una rejilla que
 * pasa de 2,2:1 sobre papel deja de ser una guia y se convierte en un
 * enrejado por delante de la señal. Que se salga por arriba es tan fallo
 * como que no se vea.
 */

#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_chrome.h"
#include "specdata.h"
#include "temas_tabla.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

const palette_t *g_pal = &k_pal_oscura;

#define FFT_BINS 796

static float g_db[FFT_BINS];

/* El negro de HEATMAP y el azul marino de LINE, tal y como estaban antes
 * de tocar nada. Si uno de los dos cambia, este banco tiene que gritar. */
#define BG_HEATMAP_DE_SIEMPRE 0x0000U
#define BG_LINE_DE_SIEMPRE    0x0043U

/*
 * LA CASCADA DE CADA TEMA, TAL Y COMO ESTABA. Los dos extremos de la
 * rampa -el frio, que es el ruido de fondo, y el caliente, que es la
 * señal- de la paleta que k_temas le empareja a cada tema. Medidos del
 * arbol, no escritos a ojo.
 *
 * Esta aqui por lo mismo que BG_HEATMAP_DE_SIEMPRE: los diez temas
 * nuevos no pueden haberle cambiado la cascada a ninguno de los ocho
 * de antes. Cambiar una LUT es de una linea y se nota en todos.
 *
 * DOS QUE PARECEN AL REVES Y NO LO ESTAN. Oliva y Chemistry son temas
 * claros de interfaz y ya llevaban SMOKE -frio blanco, caliente negro-
 * desde antes de todo esto. Y Naranja, que tambien es claro, lleva FIRE,
 * que empieza en negro: queda una cascada oscura bajo una interfaz
 * clara. Eso es asi desde antes y NO se toca aqui, porque Naranja no
 * pide espectro claro y nadie ha pedido cambiarselo. Queda escrito para
 * que la proxima vez que alguien lo vea sepa que ya se habia visto.
 */
static const struct { const char *nombre; uint16_t frio, caliente; }
k_wf_de_siempre[] = {
    { "Oscura",      0x0004U, 0x4800U },
    { "Contrastada", 0x3087U, 0x7820U },
    { "Ámbar",      0x0000U, 0xFFF4U },
    { "Fría",       0x400AU, 0xFF24U },
    { "Oliva",       0xFFFFU, 0x0000U },
    { "Naranja",     0x0000U, 0xFFFFU },
    { "Fósforo",    0x0000U, 0xEFFEU },
    { "Chemistry",   0xFFFFU, 0x0000U },
};

static double lum_canal(double v)
{
    v /= 255.0;
    return (v <= 0.03928) ? (v / 12.92) : pow((v + 0.055) / 1.055, 2.4);
}

/* Luminancia relativa WCAG de un pixel del panel, que esta en 565. */
static double lum565(uint16_t c)
{
    double r = lum_canal((double)(((c >> 11) & 0x1FU) * 255U / 31U));
    double g = lum_canal((double)(((c >> 5)  & 0x3FU) * 255U / 63U));
    double b = lum_canal((double)((c & 0x1FU) * 255U / 31U));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static double contraste(uint16_t a, uint16_t b)
{
    double la = lum565(a), lb = lum565(b);
    if (la < lb) { double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}

/* El 0xRRGGBB de la paleta, en el 565 del panel: la misma cuenta que hace
 * pal565() en spectrum.c. */
static uint16_t tema565(uint32_t hex)
{
    return (uint16_t)((((hex >> 16) & 0xFFU) >> 3) << 11 |
                      (((hex >> 8)  & 0xFFU) >> 2) << 5  |
                      (((hex)       & 0xFFU) >> 3));
}

static uint16_t pix(int x, int y)
{
    return lcd_sim_fb()[(uint32_t)y * GFX2_W + (uint32_t)x];
}

/*
 * El color que MAS se repite en una franja. Es como se saca el fondo sin
 * fiarse de un pixel suelto: en una columna cualquiera puede caer la
 * traza, la rejilla o el tinte de la banda, pero el fondo es, por
 * definicion, lo que mas hay.
 */
static uint16_t dominante(int x0, int y0, int w, int h)
{
    uint16_t mejor = 0U;
    int      cuenta_mejor = -1;
    int      i, j, k, l;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            uint16_t c = pix(x0 + i, y0 + j);
            int      n = 0;
            for (l = 0; l < h; l++) {
                for (k = 0; k < w; k++) {
                    if (pix(x0 + k, y0 + l) == c) { n++; }
                }
            }
            if (n > cuenta_mejor) { cuenta_mejor = n; mejor = c; }
        }
    }
    return mejor;
}

/* El pixel mas lejano del fondo dentro de la franja: la traza. */
static uint16_t mas_lejos(int x0, int y0, int w, int h, uint16_t fondo)
{
    uint16_t mejor = fondo;
    double   peor = 0.0;
    int      i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            uint16_t c = pix(x0 + i, y0 + j);
            double   d = contraste(c, fondo);
            if (d > peor) { peor = d; mejor = c; }
        }
    }
    return mejor;
}

/*
 * TERCER ERROR DE MEDIDA, Y EL MAS TONTO DE LOS TRES. Aqui ponia
 * demod_px = SPC_TRACE_W / 2, como si fuera la COLUMNA de la
 * demodulacion. No lo es: spectrum_draw() recibe un DESPLAZAMIENTO
 * desde el centro del panel -se llama center_mark_offset_px- y lo
 * suma a w/2. Asi que pasarle 378 pedia la columna 756, que la
 * funcion recorta a la 755: la raya se iba al borde derecho, fuera
 * de la ventana donde este banco la buscaba, y los diez temas
 * claros salian con "la marca no se ve" midiendo fondo contra
 * fondo. La radio llama con el desplazamiento de verdad (ver la
 * llamada de main.c), que con la demodulacion centrada es 0.
 */
/*
 * DOS ESCENAS, Y LA SEGUNDA ES LA QUE MIDE DE VERDAD.
 *
 * ESCENA_SEÑAL es la de la radio: picos, banda de paso marcada, todo
 * encima de todo. Vale para medir la traza y para nada mas.
 *
 * ESCENA_PLANA es un suelo de ruido constante y sin banda de paso. Ahi
 * la traza se queda pegada al borde de abajo y el resto del panel solo
 * tiene tres cosas: fondo, rejilla y la raya de la demodulacion. Cada
 * una se puede medir sin que ninguna otra la tape.
 *
 * QUINTO ERROR DE MEDIDA DE ESTE BANCO, y el que mas tiempo costo ver.
 * Todo se medía sobre la escena con señal, y la columna del centro -la
 * de la raya- es justo donde el pico de la señal sube hasta arriba del
 * panel. Asi que lo que la ventana de 3 px llamaba "marca" era el
 * relleno de la señal pasando por delante. Se noto al romperlo a
 * proposito: cambiando el acento de Lino a casi blanco, la marca
 * SEGUIA dando 4,28:1, el mismo numero que antes. Un numero que no se
 * mueve cuando mueves lo que mide es un numero de otra cosa.
 */
#define ESCENA_SENAL 0
#define ESCENA_PLANA 1

static void pinta(int estilo, spectrum_palette_t wf, int escena)
{
    int16_t bw_px = (int16_t)(SPC_TRACE_W * 4000U / 96000U);
    int     pass, k;

    lcd_sim_reset(0x0000);
    spectrum_init();
    spectrum_set_style((spectrum_style_t)estilo);
    spectrum_set_palette(wf);

    for (pass = 0; pass < 24; pass++) {
        if (escena == ESCENA_PLANA) {
            for (k = 0; k < FFT_BINS; k++) { g_db[k] = -108.0f; }
            spectrum_draw(g_db, FFT_BINS,
                          SPC_TRACE_X, SPC_TRACE_Y, SPC_TRACE_W, SPC_TRACE_H,
                          -110.0f, -20.0f, 0,
                          0U, 0, 0);
        } else {
            specdata_fill(g_db, FFT_BINS, 1u);
            spectrum_draw(g_db, FFT_BINS,
                          SPC_TRACE_X, SPC_TRACE_Y, SPC_TRACE_W, SPC_TRACE_H,
                          -110.0f, -20.0f, 0,
                          1U, (int16_t)(-bw_px), bw_px);
        }
    }
}

int main(void)
{
    int fallos = 0;
    int i;

    printf("\nEL ESPECTRO, TEMA POR TEMA\n");
    printf("  %-13s %-9s %6s %8s %8s %8s %8s\n",
           "tema", "espectro", "fondo", "señal", "trazo", "rejilla", "marca");

    for (i = 0; i < SIM_TEMA_COUNT; i++) {
        const sim_tema_t *t = &k_sim_temas[i];
        uint16_t fondo, traza, rejilla, marca;
        double   c_traza, c_trazo, c_rej, c_marca;
        int      malo = 0;
        int      bw_px = (int)(SPC_TRACE_W * 4000U / 96000U);

        g_pal = t->pal;

        /*
         * EL FONDO, LA REJILLA Y LA MARCA, sobre la escena plana, donde
         * no hay nada mas que ellas tres. El fondo es lo que mas se
         * repite arriba; la rejilla, lo que mas se aparta del fondo
         * LEJOS del centro; la marca, lo que mas se aparta en la
         * columna del centro.
         */
        pinta((int)SPECTRUM_STYLE_HEATMAP, t->wf, ESCENA_PLANA);
        fondo   = dominante(SPC_TRACE_X + 40, SPC_TRACE_Y + 6, 60, 10);
        rejilla = mas_lejos(SPC_TRACE_X, SPC_TRACE_Y + 4,
                            (SPC_TRACE_W / 2) - bw_px - 20, 12, fondo);
        marca   = mas_lejos(SPC_TRACE_X + (SPC_TRACE_W / 2) - 1,
                            SPC_TRACE_Y + 4, 3, 12, fondo);

        /*
         * Y LA SEÑAL, sobre la escena de la radio, en la mitad IZQUIERDA
         * del panel: ni la banda de paso ni la raya de la demodulacion
         * llegan hasta ahi, asi que lo que mas se aparta del fondo solo
         * puede ser la señal.
         *
         * SE LLAMA "SEÑAL" Y NO "TRAZA" A PROPOSITO. Ahi dentro hay tres
         * cosas pintadas una encima de otra -el relleno de la barra, el
         * punto de pico y el trazo- y esta medida coge la que mas
         * destaque, sin saber cual es. Vale para lo que vale: decir que
         * la señal se ve.
         *
         * SEXTO ERROR DE MEDIDA, y el ultimo. Esto se llamaba "traza" y
         * daba 17,82:1 en Lino. Al poner el trace de Lino del color
         * exacto del fondo -la traza invisible del todo- seguia dando
         * 17,82:1. Lo que medía era el PUNTO DE PICO, que va en warn y
         * es marron oscuro: el trazo podia desaparecer entero sin que
         * este numero se moviera un decimal.
         *
         * Asi que ademas se mide el trazo por su cuenta, en la columna
         * de al lado: el color que el TEMA reserva para la traza,
         * contra el fondo que de verdad sale. El fondo sigue siendo
         * medido en pixeles; lo que se le compara es lo que el tema
         * manda pintar. Si alguien pone un trace que no se ve sobre su
         * propio fondo, salta aqui aunque el pico lo tape en pantalla.
         */
        pinta((int)SPECTRUM_STYLE_HEATMAP, t->wf, ESCENA_SENAL);
        traza  = mas_lejos(SPC_TRACE_X, SPC_TRACE_Y,
                           (SPC_TRACE_W / 2) - bw_px - 20, SPC_TRACE_H, fondo);
        c_trazo = contraste(tema565(t->pal->trace), fondo);

        c_traza = contraste(traza, fondo);
        c_rej   = contraste(rejilla, fondo);
        c_marca = contraste(marca, fondo);

        printf("  %-13s %-9s  %04X  %6.2f:1 %6.2f:1 %6.2f:1 %6.2f:1",
               t->nombre, t->pal->espectro_claro ? "claro" : "de antes",
               fondo, c_traza, c_trazo, c_rej, c_marca);

        if (!t->pal->espectro_claro) {
            /* LO QUE NO SE PUEDE HABER MOVIDO. */
            if (fondo != BG_HEATMAP_DE_SIEMPRE) {
                printf("   *** EL FONDO HA CAMBIADO (era %04X) ***",
                       BG_HEATMAP_DE_SIEMPRE);
                malo = 1;
            }
        } else {
            if (c_traza < 3.0) { printf("   *** LA SEÑAL NO SE VE ***"); malo = 1; }
            if (c_trazo < 3.0) { printf("   *** EL TRAZO NO SE VE ***"); malo = 1; }
            if (c_marca < 3.0) { printf("   *** LA MARCA NO SE VE ***"); malo = 1; }
            if (c_rej > 2.2) { printf("   *** REJILLA DEMASIADO FUERTE ***"); malo = 1; }
            if (c_rej < 1.04) { printf("   *** REJILLA INVISIBLE ***"); malo = 1; }
        }
        printf("\n");
        fallos += malo;
    }

    /* Y el estilo LINE, que tiene su propio fondo y su propia historia. */
    printf("\n  y el estilo LINE, cuyo fondo era otro\n");
    for (i = 0; i < SIM_TEMA_COUNT; i++) {
        const sim_tema_t *t = &k_sim_temas[i];
        uint16_t fondo;

        g_pal = t->pal;
        pinta((int)SPECTRUM_STYLE_LINE, t->wf, ESCENA_PLANA);
        fondo = dominante(SPC_TRACE_X + 40, SPC_TRACE_Y + 6, 60, 10);

        if (!t->pal->espectro_claro && (fondo != BG_LINE_DE_SIEMPRE)) {
            printf("    %-13s fondo %04X, era %04X   *** HA CAMBIADO ***\n",
                   t->nombre, fondo, BG_LINE_DE_SIEMPRE);
            fallos++;
        } else if (t->pal->espectro_claro && (fondo == BG_LINE_DE_SIEMPRE)) {
            printf("    %-13s sigue en el azul de antes   *** NO SIGUE AL TEMA ***\n",
                   t->nombre);
            fallos++;
        }
    }
    printf("    los que no piden espectro claro pintan el azul de siempre\n");

    /*
     * Y LA CASCADA, que es el otro medio panel. Un tema claro con el
     * espectro blanco y la cascada en negro justo debajo no esta
     * arreglado: esta a medias. Por eso los diez nuevos llevan SMOKE,
     * que es la unica de las dieciseis que empieza en blanco.
     *
     * Se mide en la LUT y no en pixeles a proposito: la cascada pinta
     * UNA fila nueva por fotograma y desplaza las demas, asi que lo que
     * se ve en pantalla depende de cuantas vueltas lleve dada. La rampa,
     * en cambio, es el tema entero en dos colores.
     */
    printf("\n  LA CASCADA, DE FRIO A CALIENTE\n");
    for (i = 0; i < SIM_TEMA_COUNT; i++) {
        const sim_tema_t *t = &k_sim_temas[i];
        const uint16_t   *lut;
        uint16_t          frio, caliente;
        double            recorrido, lo = 9.0, hi = -1.0;
        size_t            k;
        int               j;

        spectrum_init();
        spectrum_set_palette(t->wf);
        lut      = spectrum_colormap_lut();
        frio     = lut[0];
        caliente = lut[255];

        /*
         * CUARTO ERROR DE MEDIDA DE ESTE BANCO, Y VA. Aqui ponia
         * contraste(frio, caliente) con un minimo de 3:1, y marcaba
         * fallo en Oscura (1,26:1) y Contrastada (1,47:1), que son dos
         * de los ocho de antes y llevan años funcionando.
         *
         * Estaba mal la medida, no las paletas. CLASSIC va de un azul
         * casi negro a un rojo oscuro: los dos EXTREMOS tienen casi la
         * misma luminancia, pero por el camino pasa por el blanco del
         * indice 91. Mirar solo las puntas de una rampa de color es
         * como juzgar un arcoiris por el rojo y el violeta.
         *
         * Lo que de verdad hace util una cascada es el RECORRIDO: que
         * entre lo mas oscuro y lo mas claro de la rampa entera haya
         * sitio para ver la señal crecer. Medido asi, la mas floja de
         * las dieciseis es VIRIDIS con 12,32:1.
         */
        for (j = 0; j < 256; j++) {
            double v = lum565(lut[j]);
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
        recorrido = (hi + 0.05) / (lo + 0.05);

        printf("    %-13s %04X -> %04X   recorrido %5.2f:1", t->nombre,
               frio, caliente, recorrido);

        /* Una rampa que no recorre nada no deja ver crecer la señal. */
        if (recorrido < 6.0) {
            printf("   *** LA CASCADA NO SEPARA ***");
            fallos++;
        }
        /* En los claros, ademas, el ruido de fondo tiene que ser claro. */
        if (t->pal->espectro_claro && (lum565(frio) < 0.45)) {
            printf("   *** CASCADA OSCURA BAJO TEMA CLARO ***");
            fallos++;
        }
        /* Y a los de antes no se les ha podido mover ni un bit. */
        for (k = 0; k < sizeof(k_wf_de_siempre) / sizeof(k_wf_de_siempre[0]); k++) {
            if (strcmp(k_wf_de_siempre[k].nombre, t->nombre) == 0) {
                if ((frio != k_wf_de_siempre[k].frio) ||
                    (caliente != k_wf_de_siempre[k].caliente)) {
                    printf("   *** HA CAMBIADO (era %04X -> %04X) ***",
                           k_wf_de_siempre[k].frio, k_wf_de_siempre[k].caliente);
                    fallos++;
                }
                break;
            }
        }
        printf("\n");
    }

    if (fallos) {
        printf("\n  *** %d FALLOS ***\n", fallos);
        return 1;
    }
    printf("\nTODO CORRECTO (0 fallos)\n");
    return 0;
}

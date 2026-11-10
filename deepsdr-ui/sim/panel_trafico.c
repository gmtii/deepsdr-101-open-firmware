/*
 * CUANTOS PUNTOS SE LE ESCRIBEN AL PANEL, POR CONFIGURACION.
 *
 * POR QUE EXISTE - 06/11/2026.
 *
 * El siseo de la FM resulto no ser audio perdido: los cuatro contadores de
 * averia salieron CERO en las dos pantallas y el margen del buffer de
 * salida plano al 45%. Lo que si lo mueve, probado de oido por el usuario,
 * es la velocidad de la cascada; el brillo no.
 *
 * Y un barrido del tiempo de dato del bus EXMC, de 4 a 15 ciclos, dejo cada
 * escritura 2,4 veces mas larga y la vuelta un 45% mas lenta SIN CAMBIAR EL
 * SISEO. O sea que lo que cuenta no son los microsegundos que el bus este
 * ocupado: son LAS ESCRITURAS POR SEGUNDO.
 *
 * Asi que la magnitud que hay que mirar es esa, y hasta hoy nadie la habia
 * contado. Esto la cuenta, para cada cosa que se dibuja y para cada modo de
 * cascada, usando el chivato de gfx2 -el mismo gancho que ya vigila el
 * repintado parcial- y el codigo de dibujo DE VERDAD del firmware.
 *
 * NO LLEVA VALORES ESPERADOS para el espectro: es una herramienta de medir.
 * Lo que SI comprueba es la unica relacion que tiene que cumplirse por
 * construccion - que la cascada en bucle escriba exactamente una fila donde
 * la desplazada escribe WATERFALL_ROWS -, porque si eso deja de ser verdad
 * el arreglo ha dejado de arreglar y nadie se enteraria.
 *
 *   make trafico
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "waterfall.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BINS  756
#define PX      0
#define PY     40
#define PW    756
#define PH    180
#define WF_Y  240

/*
 * SE CUENTA EN rm68120_write_data(), NO EN EL CHIVATO - y es importante.
 *
 * La primera version contaba por el chivato de gfx2, que es el gancho del
 * repintado parcial. Y daba CERO para el espectro: spectrum_draw() no pasa
 * por gfx2, va por el camino de vendor/gfx.c, asi que el chivato no lo ve.
 * Un contador que no ve la mitad de lo que se dibuja habria dicho que la
 * cascada es el 100% del trafico, que es la conclusion que yo esperaba -y
 * ese es exactamente el momento de desconfiar-.
 *
 * lcd_sim.c ya cuenta los accesos de 16 bits al bus en
 * rm68120_write_data(), que es por donde pasan LOS DOS caminos. Esa es la
 * magnitud buena y ya estaba contada.
 */
static unsigned long g_antes;

static void cero(void) { g_antes = (unsigned long)lcd_sim_cost().bus_writes; }
static unsigned long leidos(void)
{
    return (unsigned long)lcd_sim_cost().bus_writes - g_antes;
}

/* El IPA no existe en el host: waterfall.c cae solo al camino de CPU, que
 * es el que pasa por el chivato. Se dice aqui porque significa que lo que
 * se cuenta es el camino de respaldo; el del IPA escribe LOS MISMOS puntos
 * -es la conversion lo que cambia de sitio, no el volcado-. */
uint8_t ipa_blit_hay(void) { return 0U; }
uint8_t ipa_blit_l8(const uint8_t *src, uint16_t src_salto,
                    uint16_t *dst, uint16_t dst_salto,
                    uint16_t w, uint16_t h, const uint16_t *lut)
{
    (void)src; (void)src_salto; (void)dst; (void)dst_salto;
    (void)w; (void)h; (void)lut;
    return 0U;
}

/* La paleta, como en sim/espectro_oro.c y por lo mismo. */
const palette_t *g_pal = &k_pal_oscura;

static float g_db[BINS];

/*
 * LA SEÑAL TIENE QUE PARECERSE A LA DE LA RADIO, Y ESO NO ES RUIDO NUEVO
 * CADA CUADRO.
 *
 * La primera version sorteaba los 756 bins enteros en cada fotograma. Con
 * eso el atajo del repintado parcial de spectrum_draw() -el que en la radio
 * se salta 42 bandas de 52- no puede saltarse NI UNA, porque todo ha
 * cambiado. Y entonces el banco decia que el espectro escribe 86.027
 * accesos por cuadro, mas que la cascada, y la conclusion entera se daba la
 * vuelta.
 *
 * Era el banco el que estaba mal. En el aire el espectro cambia POCO de un
 * fotograma al siguiente: el suelo de ruido se mueve unas decimas y la
 * emisora sigue donde estaba. Asi que aqui la señal se mueve poco: un
 * fondo fijo con una emisora encima y un temblor pequeño.
 *
 * COMO SE SABE QUE AHORA SI VALE: el banco imprime cuantas bandas se salta,
 * y hay que compararlo con lo que dijo la radio por el cable - 42 de 52 -.
 * Si no se parece, la medida no vale y el numero no se publica.
 */
static void senal(unsigned long semilla)
{
    int i;
    for (i = 0; i < BINS; i++) {
        float base = -95.0f + 8.0f * (float)((i * 37) % 11);
        float pico = (i > 370 && i < 386) ? 55.0f : 0.0f;
        float temb;

        semilla = semilla * 1103515245UL + 12345UL;
        temb = 0.02f * ((float)((semilla >> 16) & 0x7FFF) / 32767.0f - 0.5f);
        g_db[i] = base + pico + temb;
    }
}

static void linea_cascada(uint8_t *fila, unsigned long semilla)
{
    int i;
    for (i = 0; i < WATERFALL_WIDTH; i++) {
        semilla = semilla * 1103515245UL + 12345UL;
        fila[i] = (uint8_t)((semilla >> 16) & 0xFF);
    }
}

int main(void)
{
    static uint8_t fila[WATERFALL_WIDTH];
    const uint16_t *lut;
    unsigned long esp1, espn, wfd, wfb;
    int k, fallos = 0;

    printf("ESCRITURAS AL BUS DEL PANEL, POR CONFIGURACION\n");
    printf("(el panel es de 800 x 480 = 384.000 puntos)\n\n");

    lcd_sim_reset(0x0000);
    spectrum_init();
    waterfall_init();
    lut = spectrum_colormap_lut();

    /* --- 1. el espectro solo --- */
    senal(2024UL);
    cero();
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, -110.0f, -20.0f,
                  (int16_t)(PW / 2), 0U, 0, 0);
    esp1 = leidos();
    printf("  1. EL ESPECTRO\n");
    printf("     primer cuadro (entero) ......... %7lu escrituras al bus\n", esp1);

    /* El segundo, con el atajo del repintado parcial ya en marcha: es el
     * que de verdad corre en la radio fotograma tras fotograma. */
    cero();
    for (k = 0; k < 10; k++) {
        senal((unsigned long)(3000 + k));
        spectrum_draw(g_db, BINS, PX, PY, PW, PH, -110.0f, -20.0f,
                      (int16_t)(PW / 2), 0U, 0, 0);
    }
    espn = leidos() / 10UL;
    {
        uint32_t salt = 0U, tot = 0U;
        spectrum_parcial_diag(&salt, &tot);
        printf("     a cuadro entero ................ %7lu escrituras por cuadro\n",
               espn);
        printf("     el atajo se salto .............. %u bandas de %u\n",
               (unsigned)salt, (unsigned)tot);
        printf("\n");
        printf("     *** ESTE NUMERO NO ES EL DE LA RADIO, Y VA DICHO. ***\n");
        printf("     En la radio el atajo del repintado parcial se salta 42\n");
        printf("     bandas de 52 (medido por cable el 06/11), o sea que\n");
        printf("     escribe alrededor de UNA QUINTA PARTE de esto. Aqui no\n");
        printf("     se consigue: se probaron temblores de 0,02 a 0,30 dB y\n");
        printf("     el atajo se salta 6 de 44 en todos, asi que lo que\n");
        printf("     invalida las bandas en el banco no es la señal. No se\n");
        printf("     fuerza un numero bonito: la autoridad para el reparto\n");
        printf("     entre espectro y cascada son los microsegundos que la\n");
        printf("     radio dio por el cable, no esto.\n\n");
    }

    /* --- 2. la cascada desplazada, la de siempre --- */
    waterfall_bucle_pon(0U);
    cero();
    for (k = 0; k < 10; k++) {
        linea_cascada(fila, (unsigned long)(5000 + k));
        waterfall_push_line(fila);
        waterfall_blit(0U, WF_Y, lut);
    }
    wfd = leidos() / 10UL;
    printf("  2. LA CASCADA\n");
    printf("     desplazada (la de siempre) ..... %7lu escrituras por linea nueva\n", wfd);

    /* --- 3. y en bucle --- */
    waterfall_bucle_pon(1U);
    /*
     * EL PRIMER VOLCADO EN BUCLE ES ENTERO, A PROPOSITO.
     *
     * waterfall_bucle_pon() pone la bandera de "esta vez, entero" (ver
     * waterfall.c): en bucle solo se escribe la fila nueva, asi que al
     * entrar en el modo hay que pintar las 72 una vez o se queda lo que
     * hubiera debajo. El banco lo canto cuando se añadio - la media de diez
     * salia con un volcado entero dentro -.
     *
     * Asi que se gasta ese primero FUERA de la cuenta. No es trampa: lo que
     * se quiere medir es lo que cuesta cada linea EN REGIMEN, y el entero
     * del arranque pasa una vez por modo, no por linea.
     */
    linea_cascada(fila, 4999UL);
    waterfall_push_line(fila);
    waterfall_blit(0U, WF_Y, lut);

    cero();
    for (k = 0; k < 10; k++) {
        linea_cascada(fila, (unsigned long)(7000 + k));
        waterfall_push_line(fila);
        waterfall_blit(0U, WF_Y, lut);
    }
    wfb = leidos() / 10UL;
    printf("     en bucle ....................... %7lu escrituras por linea nueva\n", wfb);

    /*
     * Y EL CASO DE "MUY RAPIDA": DOS LINEAS POR VOLCADO - 08/11/2026.
     *
     * El bucle principal, con la cascada a "Muy rapida", empuja DOS lineas
     * y vuelca UNA sola vez (camino `dos` de main.c). La desplazada no se
     * entera porque repinta las 72 filas; la de bucle dibujaba SOLO LA
     * ULTIMA, asi que la mitad de las lineas no llegaba a la pantalla.
     *
     * Era un fallo mio de la V6.65 y no habia banco que lo viera, porque
     * este empujaba una linea por volcado. Ahora lo hay.
     */
    {
        unsigned long dos;
        int m;
        cero();
        for (m = 0; m < 10; m++) {
            linea_cascada(fila, (unsigned long)(9000 + m * 2));
            waterfall_push_line(fila);
            linea_cascada(fila, (unsigned long)(9001 + m * 2));
            waterfall_push_line(fila);
            waterfall_blit(0U, WF_Y, lut);
        }
        dos = leidos() / 10UL;
        printf("     en bucle, DOS lineas por volcado %7lu escrituras\n", dos);
        if (dos < wfb * 2UL - wfb / 2UL) {
            printf("     *** MAL: con dos lineas tiene que escribir las DOS filas,"
                   " y escribe %lu ***\n", dos);
            fallos++;
        } else {
            printf("     las dos filas se dibujan, no solo la ultima  ok\n");
        }
    }
    waterfall_bucle_pon(0U);

    /*
     * LA UNICA RELACION QUE TIENE QUE CUMPLIRSE POR CONSTRUCCION.
     * Si esto deja de ser verdad, el arreglo ha dejado de arreglar.
     */
    /*
     * Se comprueba LA RAZON y no el numero clavado: cada volcado lleva
     * ademas los accesos de abrir la ventana del panel, que no son pixeles.
     * Se publican, que es lo honrado, pero lo que tiene que cumplirse es
     * que en bucle se escriba una fila donde antes setenta y dos.
     */
    printf("     (de esos, %lu y %lu no son pixeles sino abrir ventana)\n",
           wfd - (unsigned long)WATERFALL_WIDTH * (unsigned long)WATERFALL_ROWS,
           wfb - (unsigned long)WATERFALL_WIDTH);
    {
        unsigned long razon = wfd / (wfb ? wfb : 1UL);
        if (razon < (unsigned long)(WATERFALL_ROWS - 3) ||
            razon > (unsigned long)WATERFALL_ROWS) {
            printf("     *** MAL: la razon es x%lu y tendria que rondar x%u ***\n",
                   razon, (unsigned)WATERFALL_ROWS);
            fallos++;
        } else {
            printf("     la razon entre las dos es x%lu, que son las filas  ok\n\n",
                   razon);
        }
    }

    /*
     * --- 4. Y LO QUE SIGNIFICA POR SEGUNDO ---
     *
     * A 40 cuadros por segundo, que es lo que daba la tanda del 06/11
     * (cuadros 39 por media tanda de medio segundo). El espectro se pinta
     * en todos; la cascada, segun su velocidad - "Muy rapida" vuelca en
     * todos y "Muy lenta" en uno de cada ocho -.
     */
    /*
     * --- 4. LO QUE SIGNIFICA POR SEGUNDO ---
     *
     * El espectro se toma de LA RADIO y no de aqui, por lo de arriba: el
     * tramo `emp` de "DD;" mide exactamente lo que se fue en empujar
     * pixeles del espectro, y en la tanda del 06/11 fueron 564 us por
     * cuadro contra los 3.230 us de bus de la cascada. Esa es la razon
     * buena, medida con el atajo trabajando: la cascada es el 85% del
     * trafico al panel y el espectro el 15%.
     *
     * La cascada SI sale de aqui, porque aqui se cuenta exacta y no
     * depende de ningun atajo.
     */
    printf("  4. POR SEGUNDO, a 40 cuadros\n");
    printf("     (cascada medida aqui; espectro, en la proporcion que dio\n");
    printf("      la radio: emp 564 us contra 3.230 us de bus de cascada)\n\n");
    {
        unsigned long c_rapida = wfd * 40UL;              /* vuelca en todos */
        unsigned long c_lenta  = wfd * 5UL;               /* uno de cada ocho */
        unsigned long c_bucle  = wfb * 40UL;
        unsigned long espectro = (c_rapida * 15UL) / 85UL;
        unsigned long base     = espectro + c_rapida;
        struct { const char *n; unsigned long v; } k_c[] = {
            { "espectro + cascada desplazada rapida", espectro + c_rapida },
            { "espectro + cascada desplazada lenta ", espectro + c_lenta  },
            { "espectro + cascada EN BUCLE rapida  ", espectro + c_bucle  },
            { "espectro solo, sin cascada          ", espectro            },
            { "nada (el menu abierto)              ", 0UL                 }
        };
        unsigned i;
        printf("     configuracion                        escrituras/s   %%\n");
        printf("     ----------------------------------------------------\n");
        for (i = 0; i < sizeof k_c / sizeof k_c[0]; i++) {
            printf("     %s %9lu   %5.1f\n", k_c[i].n, k_c[i].v,
                   base ? (100.0 * (double)k_c[i].v / (double)base) : 0.0);
        }
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

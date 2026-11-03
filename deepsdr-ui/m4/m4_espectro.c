/*
 * LO QUE CUESTA EL ESPECTRO EN LA RADIO, EN INSTRUCCIONES.
 *
 * El caso que importa y por el que existe este banco: MOVER LA MARCA
 * CENTRAL. Son dos columnas de 756, y tiraban el repintado parcial del
 * cuadro ENTERO, porque la firma que decide si la plantilla ha cambiado se
 * calculaba de una sola vez sobre las 756.
 *
 * Y la marca se mueve en CADA clic del mando: con el NCO puesto es lo
 * unico que se mueve en toda la pantalla. Que eso hiciera ir lento a todo
 * lo demas se dijo antes que ningun banco, y no tenia sentido.
 *
 *     con la firma de una pieza .... 13.329 us moviendo la marca
 *     con la firma por tramos ......  8.479
 *     con las lonjas de columnas ...  4.404
 *
 * LO QUE ENSEÑO ESTE BANCO DESPUES, y que es de donde sale lo ultimo: el
 * reparto por fases dice que el 90 % del cuadro se va en el bucle de
 * COLUMNAS de compon(), y ese bucle no cuesta por pixel sino por columna y
 * BANDA. Con bandas de cuatro filas cada columna se miraba cincuenta y dos
 * veces para aprovechar cuatro. Ver "LA BANDA, MAS ALTA Y MENOS ANCHA" y
 * "LOS TRAMOS QUE NO LLEGAN A ESTAS FILAS" en User/spectrum.c.
 *
 * Ver tambien s_firma_tramo alli.
 */
#include "semi.h"
void pila_di(unsigned long maximo);
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "waterfall.h"
#include "spec_chrome.h"

int g_fallos;
const palette_t *g_pal = &k_pal_oscura;
static float   s_db[1024];
static uint8_t s_lin[800];

static void fondo(gfx2_surf_t *s, void *c){ (void)c;
    gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }

static void traza(int16_t marca)
{
    spectrum_draw(s_db,1024,SPC_TRACE_X,SPC_Y,SPC_TRACE_W,SPC_TRACE_H,
                  -120.0f,-20.0f,marca,1,-40,40);
}

/* Lo mismo pero con la chapa del filtro moviendose con la marca, que es lo
 * que pasa de verdad: el ancho de banda se pinta alrededor del punto de
 * demodulacion y lo sigue. */
static void traza_mb(int16_t marca, int16_t lo, int16_t hi)
{
    spectrum_draw(s_db,1024,SPC_TRACE_X,SPC_Y,SPC_TRACE_W,SPC_TRACE_H,
                  -120.0f,-20.0f,marca,1,lo,hi);
}

/* Y el ruido de una banda de verdad: la traza no se esta quieta nunca.
 * Sin esto el banco mide un caso que en la radio no existe -el usuario
 * tiene 13 ms quieto y el banco daba 7.522 us de banco, o sea 12,8: casi
 * cuadra, pero por casualidad-. */
static unsigned long s_vai2 = 0x5EED1234u;
static void menea(void)
{
    unsigned i;
    for (i = 0; i < 1024u; i++) {
        s_vai2 = s_vai2 * 1103515245u + 12345u;
        s_db[i] += (float)((long)((s_vai2 >> 18) & 7u) - 3) * 0.25f;
    }
}

int main(void)
{
    unsigned long a,b,v; int r; unsigned i;
    unsigned long vai=12345u;

    reloj_on();
    for(i=0;i<1024;i++){ vai=vai*1103515245u+12345u;
        s_db[i]=-110.0f+(float)((vai>>16)%40u); }
    for(i=0;i<800;i++){ vai=vai*1103515245u+12345u; s_lin[i]=(uint8_t)(vai>>20); }
    gfx2_render_screen(fondo,0);
    spectrum_init(); waterfall_init();

    pr("\nEL ESPECTRO, EN UN CORTEX-M4 DE VERDAD\n\n");

    for(r=0;r<3;r++) traza(0);
    a=tic(); for(r=0;r<10;r++) traza(0); b=tic();
    v=mide("traza QUIETA                   ", a,b,10);
    tope("quieta", v, 1000000UL);

    a=tic(); for(r=0;r<10;r++){
        for(i=0;i<1023;i++) s_db[i]=s_db[i+1];
        s_db[1023]=-110.0f+(float)((r*7)%40);
        traza(0); } b=tic();
    v=mide("traza DESPLAZANDOSE            ", a,b,10);
    tope("desplazandose", v, 1030000UL);

    /* EL CASO. La marca se mueve y el espectro no. */
    a=tic(); for(r=0;r<10;r++) traza((int16_t)(r*3)); b=tic();
    v=mide("traza con la MARCA moviendose  ", a,b,10);
    tope("marca moviendose", v, 1000000UL);

    /*
     * EL CASO DE GIRAR EL MANDO. Lo que mata al atajo del repintado
     * parcial no es que el espectro cambie: es que ALGUIEN HAYA PINTADO
     * dentro del panel. gfx_vigilancia() sube, el cuadro no puede fiarse
     * del anterior y se pinta entero. Mientras el mando gira eso pasa en
     * TODOS los cuadros, asi que esta es la medida que hay que mirar.
     */
    a=tic(); for(r=0;r<10;r++){ spectrum_invalida(); traza(0); } b=tic();
    v=mide("traza SIN atajo (mando girando)", a,b,10);
    tope("sin atajo", v, 1130000UL);

    /*
     * EL CASO DE LA RADIO, Y NO LOS DE ANTES.
     *
     * La fila Fotograma del usuario decia 13 ms de traza quieto y de 31 a
     * 42 girando, con el empuje al bus en 0 y 2: o sea CPU, no bus.
     *
     * Los casos de arriba no lo reproducen porque los dos mienten: la
     * traza quieta no existe -una banda real tiene ruido y se mueve en
     * todos los cuadros- y la marca sola se mueve tres pixeles, cuando en
     * la radio se mueve la marca Y la chapa del filtro, unos dieciseis por
     * cuadro a cuatro detentes.
     *
     * Asi que aqui va ese: ruido en la traza y marca+chapa moviendose.
     *
     * LA CUENTA QUE RELACIONA ESTE BANCO CON LA RADIO. Quieto, el banco
     * daba 7.724 us y la radio 13 ms: factor 1,68. Ese factor NO es el
     * emulador yendo rapido, es el AUDIO comiendose la CPU - estos
     * numeros son instrucciones de dibujo y los de la radio son reloj de
     * pared, con las interrupciones de audio dentro. Con la carga de
     * audio en el 41 % que marca la propia radio, 7,7 / (1 - 0,41) da
     * 13,1 ms. Por eso el NR se nota aunque no pinte nada: no suma
     * dibujo, sube el divisor.
     */
    /*
     * Y CON LOS AJUSTES DE LA RADIO, NO CON LOS DE SERIE.
     *
     * El banco venia midiendo el estilo de serie -mapa de calor- y el
     * usuario tiene "Linea" con contorno 1. Son dos dibujos distintos:
     * el de calor pinta una barra por columna con el color del nivel, el
     * de linea pinta un relleno liso Y un contorno encima. Medir uno y
     * hablar del otro es como no medir.
     */
    spectrum_set_style(SPECTRUM_STYLE_LINE);
    spectrum_set_line_smooth(1U);

    for(r=0;r<3;r++){ menea(); traza(0); }
    a=tic(); for(r=0;r<10;r++){ menea(); traza(0); } b=tic();
    v=mide("con RUIDO, el dial quieto      ", a,b,10);

    a=tic(); for(r=0;r<10;r++){ menea();
                                traza_mb((int16_t)(r*16), (int16_t)(r*16-40),
                                         (int16_t)(r*16+40)); } b=tic();
    v=mide("con RUIDO y el dial MOVIENDOSE ", a,b,10);
    { unsigned long sa,to; spectrum_parcial_diag(&sa,&to);
      pr("        bandas saltadas "); prn(sa); pr(" de "); prn(to); pr("\n"); }

    /* Y lo mismo pintando ENTERO a proposito, que es la comparacion que
     * decide si el atajo esta estorbando. */
    a=tic(); for(r=0;r<10;r++){ menea(); spectrum_invalida();
                                traza_mb((int16_t)(r*16), (int16_t)(r*16-40),
                                         (int16_t)(r*16+40)); } b=tic();
    v=mide("lo mismo pero SIN atajo        ", a,b,10);

    for(r=0;r<3;r++) traza(0);

    /* Y con la ESCALA moviendose, que es lo que hace la autoescala cuando
     * el contenido del espectro cambia -o sea, cuando se gira el dial-. */
    a=tic(); for(r=0;r<10;r++){
        spectrum_draw(s_db,1024,SPC_TRACE_X,SPC_Y,SPC_TRACE_W,SPC_TRACE_H,
                      -120.0f+(float)r*0.7f,-20.0f+(float)r*0.3f,0,1,-40,40); } b=tic();
    (void)mide("traza con la ESCALA moviendose ", a,b,10);
    { unsigned long sa,to; spectrum_parcial_diag(&sa,&to);
      pr("        bandas saltadas "); prn(sa); pr(" de "); prn(to); pr("\n"); }

    a=tic(); for(r=0;r<10;r++){ waterfall_push_line(s_lin); } b=tic();
    (void)mide("waterfall_push_line            ", a,b,10);

    pr("\n");
    pila_di(4096UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); }
    else          { pr("  TODO CORRECTO\n\n"); }
    salir(); return 0;
}

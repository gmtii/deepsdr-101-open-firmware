/*
 * ======================================================================
 * EL REPINTADO POR RENGLONES SUCIOS, CONTRA EL REPINTADO ENTERO
 * 19/10/2026
 * ======================================================================
 *
 * Pedido por el usuario: que la radio vaya mas rapido.
 *
 * ui_digi_draw_cambios() solo repinta los renglones cuya huella ha
 * cambiado. Eso ahorra mucho -los numeros estan en su comentario- y abre
 * la puerta al fallo clasico de los repintados parciales: que lo que
 * queda en pantalla NO sea lo que habria quedado repintando entero. Y ese
 * fallo no avisa: no sale nada raro, sale lo de antes.
 *
 * ASI QUE ESTE BANCO NO MIRA EL AHORRO, MIRA LOS PIXELES. Para cada
 * escena:
 *
 *   1. se pinta el estado A entero, se cambia al estado B y se pinta con
 *      ui_digi_draw_cambios();
 *   2. se pinta el estado B entero, de cero, en otro lienzo;
 *   3. y se comparan los dos lienzos PIXEL A PIXEL.
 *
 * Si sobra un renglon viejo, si falta uno nuevo, o si la cabecera se
 * queda atras, los dos no coinciden y el banco lo dice con el numero de
 * pixeles distintos y donde esta el primero.
 *
 * LAS ESCENAS son los casos que de verdad pasan en la radio y los que me
 * dan miedo: un renglon que cambia, una lista que entra por arriba y
 * empuja a las demas, el renglon VIVO que se mueve -que cambia el color
 * de dos renglones, no de uno-, la chapa, un boton pulsado, menos filas
 * que antes, el cambio de tema, y el caso de la captura, en el que la
 * banda esta cogida y no se puede pintar nada.
 *
 * Y de propina se cuenta lo que cuesta cada escena en accesos al bus, que
 * es de donde sale el ahorro.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

#define PY 104
#define PH 317

static uint16_t lienzo_a[GFX2_W * GFX2_H];
static uint16_t lienzo_b[GFX2_W * GFX2_H];

static void fondo(gfx2_surf_t *s, void *c)
{
    (void)c;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

static void guarda(uint16_t *d)
{
    memcpy(d, lcd_sim_fb(), (size_t)GFX2_W * GFX2_H * sizeof(uint16_t));
}

/*
 * Cuantos pixeles se diferencian DENTRO DEL PANEL, y donde el primero.
 *
 * Solo el panel, y no la pantalla entera, por una razon de banco y no de
 * firmware: la escena del cambio de tema pinta el fondo de los dos
 * caminos con paletas distintas -en el primero el tema cambia DESPUES de
 * pintarlo- y los 130.400 pixeles de fuera saldrian distintos sin que
 * eso diga nada de ui_digi. Que el panel no se salga de su caja lo
 * vigila sim/tapado.c, que es su banco.
 */
static long difiere(int *px, int *py)
{
    long n = 0; int x, y;
    *px = -1; *py = -1;
    for (y = PY; y < PY + PH; y++) {
        for (x = 0; x < GFX2_W; x++) {
            int i = y * GFX2_W + x;
            if (lienzo_a[i] != lienzo_b[i]) {
                if (n == 0) { *px = x; *py = y; }
                n++;
            }
        }
    }
    return n;
}

static void base(ui_digi_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->y = PY; st->h = PH; st->filas = 8;
    st->titulos = "Hora\tVelocidad\tModulacion\tEntrelazado\tDato";
    st->chip = "12 medidas";
    st->btn  = "Borrar";
    st->btn2 = "Siguiente";
}

/*
 * Una escena. `antes` deja el estado A, `despues` lo lleva al B.
 * Devuelve los accesos al bus que costo el repintado por cambios.
 */
static unsigned long long escena(const char *nombre,
                                 void (*antes)(ui_digi_state_t *),
                                 void (*despues)(ui_digi_state_t *),
                                 unsigned long long *entero_out)
{
    ui_digi_state_t st;
    unsigned long long parcial, entero;
    lcd_cost_t c;
    long d; int px, py;

    /* --- camino 1: A entero, B por cambios --- */
    base(&st); antes(&st);
    lcd_sim_reset(0);
    gfx2_render_screen(fondo, 0);
    ui_digi_olvida();
    (void)ui_digi_draw_cambios(&st);     /* el primero siempre es entero */
    despues(&st);
    lcd_sim_cost_reset();
    (void)ui_digi_draw_cambios(&st);
    c = lcd_sim_cost(); parcial = c.bus_writes;
    guarda(lienzo_a);

    /* --- camino 2: B entero y de cero --- */
    lcd_sim_reset(0);
    gfx2_render_screen(fondo, 0);
    lcd_sim_cost_reset();
    (void)ui_digi_draw_texto(&st);
    c = lcd_sim_cost(); entero = c.bus_writes;
    guarda(lienzo_b);

    d = difiere(&px, &py);
    printf("    %-34s %8llu / %8llu accesos  %3.0f%%",
           nombre, parcial, entero,
           entero ? (100.0 * (double)parcial / (double)entero) : 0.0);
    if (d == 0) {
        printf("   ok\n");
    } else {
        printf("   *** %ld PIXELES DISTINTOS, el primero en (%d,%d) ***\n",
               d, px, py);
        fallos++;
    }
    if (entero_out) { *entero_out = entero; }
    return parcial;
}

/* ---------------- las escenas ---------------- */

static const char *k_lista[8] = {
    "14:02:11  4481 Bd  PSK  corto  1/2",
    "14:02:09  3000 Bd  QAM  largo  3/4",
    "14:02:05  2400 Bd  PSK  corto  1/2",
    "14:01:58  1200 Bd  PSK  largo  1/2",
    "14:01:44   600 Bd  PSK  corto  1/2",
    "14:01:30   300 Bd  PSK  largo  1/2",
    "14:01:12   150 Bd  PSK  corto  1/2",
    "14:00:51    75 Bd  PSK  largo  1/2",
};

static void llena(ui_digi_state_t *st)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) { st->fila[i] = k_lista[i]; }
}

static void un_renglon(ui_digi_state_t *st)
{
    st->fila[3] = "14:01:58  1200 Bd  PSK  largo  1/2  (nuevo)";
}

static void entra_por_arriba(ui_digi_state_t *st)
{
    uint8_t i;
    for (i = 7U; i > 0U; i--) { st->fila[i] = st->fila[i - 1U]; }
    st->fila[0] = "14:02:20  4800 Bd  PSK  corto  1/2";
}

static void media_lista(ui_digi_state_t *st)
{
    uint8_t i;
    for (i = 0U; i < 4U; i++) { st->fila[i] = k_lista[i]; }
}

static void crece_la_lista(ui_digi_state_t *st)
{
    /* el renglon VIVO se mueve del 3 al 4: cambian DOS de color */
    st->fila[4] = k_lista[4];
}

static void mengua_la_lista(ui_digi_state_t *st)
{
    st->fila[3] = 0;   /* el vivo retrocede y hay que borrar el de abajo */
}

static void cambia_chapa(ui_digi_state_t *st)
{
    st->chip = "137 medidas";
}

static void pulsa_boton(ui_digi_state_t *st)
{
    st->btn_press = 1U;
}

static void menos_filas(ui_digi_state_t *st)
{
    st->filas = 3U;
}

static void nada(ui_digi_state_t *st)
{
    (void)st;
}

static void tema_claro_pon(ui_digi_state_t *st)
{
    (void)st;
    g_pal = &k_pal_ambar;
}

int main(void)
{
    unsigned long long entero = 0, p;

    printf("\n== EL REPINTADO POR RENGLONES SUCIOS ==\n\n");
    printf("  lo que queda en pantalla tiene que ser EXACTAMENTE lo mismo\n");
    printf("  que repintando entero; el tanto por ciento es lo que cuesta\n\n");
    printf("    escena                              bus parcial / entero\n");
    printf("    ---------------------------------------------------------\n");

    escena("un renglon cambia",        llena,        un_renglon,       &entero);
    escena("entra una linea por arriba", llena,      entra_por_arriba, 0);
    escena("la lista crece (el vivo se mueve)", media_lista, crece_la_lista, 0);
    escena("la lista mengua",          llena,        mengua_la_lista,  0);
    escena("cambia la chapa",          llena,        cambia_chapa,     0);
    escena("se pulsa un boton",        llena,        pulsa_boton,      0);
    escena("bajan las filas de 8 a 3", llena,        menos_filas,      0);
    p = escena("no cambia nada",       llena,        nada,             0);
    escena("cambio de tema",           llena,        tema_claro_pon,   0);
    g_pal = &k_pal_oscura;

    if (p != 0ULL) {
        printf("\n    *** sin cambios no se puede tocar el bus, y se "
               "tocaron %llu veces ***\n", p);
        fallos++;
    } else {
        printf("\n    sin cambios no se toca el bus ni una vez  ok\n");
    }

    /*
     * Y EL CASO DE LA CAPTURA: con la banda cogida no se puede pintar, y
     * lo que NO puede pasar es que se apunte la huella nueva como si se
     * hubiera pintado. Si pasara, ese renglon se quedaria con el texto
     * viejo en pantalla para siempre.
     */
    {
        ui_digi_state_t st;
        long d; int px, py;

        base(&st); llena(&st);
        lcd_sim_reset(0); gfx2_render_screen(fondo, 0);
        ui_digi_olvida();
        (void)ui_digi_draw_cambios(&st);
        un_renglon(&st);

        (void)gfx2_banda_coge();            /* la captura se la queda */
        (void)ui_digi_draw_cambios(&st);    /* no puede pintar nada */
        gfx2_banda_suelta();

        (void)ui_digi_draw_cambios(&st);    /* ahora si, y tiene que pintar */
        guarda(lienzo_a);

        lcd_sim_reset(0); gfx2_render_screen(fondo, 0);
        (void)ui_digi_draw_texto(&st);
        guarda(lienzo_b);

        d = difiere(&px, &py);
        if (d == 0) {
            printf("    el renglon que no se pudo pintar se pinta despues"
                   "  ok\n");
        } else {
            printf("    *** el renglon se perdio: %ld pixeles distintos, "
                   "el primero en (%d,%d) ***\n", d, px, py);
            fallos++;
        }
    }

    printf("\n");
    if (fallos == 0) {
        printf("TODO CORRECTO: lo parcial pinta lo mismo que lo entero.\n\n");
        return 0;
    }
    printf("%d FALLOS\n\n", fallos);
    return 1;
}

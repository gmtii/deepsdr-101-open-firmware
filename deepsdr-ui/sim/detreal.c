/*
 * Render y comprobacion de la pantalla de detalle, compilando el
 * User/ui_det.c DEL FIRMWARE y no la copia de gfx2/.
 *
 * La razon es la de siempre en este proyecto: una copia que nadie obliga
 * a estar al dia acaba mintiendo, y ya ha pasado tres veces. El render
 * viejo -sim/detalle.c- compila gfx2/ui_det.c, que es una copia; si la
 * fila de ajustes rapidos de la etapa 34 se mirase ahi, se estaria
 * mirando codigo que a lo mejor no es el que corre.
 *
 * Lo que comprueba, ademas de dejar el PPM para MIRARLO:
 *
 *   - que la fila nueva no se pise con los dos botones grandes ni con el
 *     pie. Las tres franjas se tocan, asi que un pixel de mas en
 *     cualquiera se lo come otra;
 *   - que cada boton de la fila siga siendo un objetivo de dedo decente
 *     (47 px, el mismo minimo que usa el resto de la interfaz);
 *   - que ui_det_hit() devuelva de verdad cada uno de los tres en su
 *     sitio, y NADA en el aire entre ellos. Un boton que se dibuja y no
 *     responde es peor que no ponerlo.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_det.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

static void fondo(gfx2_surf_t *s, void *c)
{
    (void)c; gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(void)
{
    ui_det_state_t st = {0};
    int i;

    printf("pantalla de detalle con la fila de ajustes rapidos\n\n");

    printf("  geometria: botones %dx%d en y=%d; fila %dx%d en y=%d; pie en y=%d\n",
           UID_BTN_W, UID_BTN_H, UID_BTN_Y,
           UID_PRE_W, UID_PRE_H, UID_PRE_Y, UID_PIE_Y);

    /* 1. las tres franjas no se pisan. */
    comprueba(UID_PRE_Y >= UID_BTN_Y + UID_BTN_H + 20,
              "la fila empieza por debajo del toque de los botones grandes");
    comprueba(UID_PRE_Y + UID_PRE_H <= UID_PIE_Y - 8,
              "y acaba por encima del toque del pie");
    comprueba(UID_PRE_H >= 47,
              "cada boton es un objetivo de dedo (47 px de alto o mas)");
    comprueba(UID_PRE_X0 + UID_PRE_N * UID_PRE_W
              + (UID_PRE_N - 1) * UID_PRE_GAP <= GFX2_W - UID_PRE_X0,
              "los tres caben a lo ancho");

    /* 2. el reparto de toques. */
    {
        int bien = 1;
        for (i = 0; i < UID_PRE_N; i++) {
            int16_t x0 = (int16_t)(UID_PRE_X0 + i * (UID_PRE_W + UID_PRE_GAP));
            int16_t cy = (int16_t)(UID_PRE_Y + UID_PRE_H / 2);
            if (ui_det_hit((uint16_t)(x0 + 2), (uint16_t)cy) != UID_HIT_PRE0 + i) { bien = 0; }
            if (ui_det_hit((uint16_t)(x0 + UID_PRE_W / 2), (uint16_t)cy) != UID_HIT_PRE0 + i) { bien = 0; }
            if (ui_det_hit((uint16_t)(x0 + UID_PRE_W - 2), (uint16_t)cy) != UID_HIT_PRE0 + i) { bien = 0; }
        }
        comprueba(bien, "los tres responden en su izquierda, centro y derecha");
    }
    {
        int bien = 1;
        for (i = 0; i < UID_PRE_N - 1; i++) {
            int16_t hueco = (int16_t)(UID_PRE_X0 + i * (UID_PRE_W + UID_PRE_GAP)
                                      + UID_PRE_W + UID_PRE_GAP / 2);
            if (ui_det_hit((uint16_t)hueco,
                           (uint16_t)(UID_PRE_Y + UID_PRE_H / 2)) != UID_HIT_NONE) {
                bien = 0;
            }
        }
        comprueba(bien, "y el aire entre ellos no responde a nada");
    }
    comprueba(ui_det_hit((uint16_t)400, (uint16_t)(UID_BTN_Y + 40)) == UID_HIT_NONE,
              "el numero de arriba sigue sin ser tocable");
    comprueba(ui_det_hit((uint16_t)100, (uint16_t)(UID_BTN_Y + 40)) == UID_HIT_MENOS,
              "y los botones grandes siguen respondiendo");
    comprueba(ui_det_hit((uint16_t)600, (uint16_t)(UID_PIE_Y + 10)) == UID_HIT_VOLVER,
              "y el pie tambien");

    /* 3. pintarla, que es media medida. */
    st.titulo = "Filtro: corte alto";
    st.valor  = "2700 Hz";
    st.alt    = "Cambiar a LO / HI";
    st.alt_on = 1U;
    st.pressed = -1;
    /* De menor a mayor, que es como se pintan - ver k_fil_rapidos[] en
     * main.c. */
    st.preset[0] = "1,8 kHz";
    st.preset[1] = "2,3 kHz";
    st.preset[2] = "4,0 kHz";
    st.preset_on = 1;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_det_draw(&st);
    lcd_sim_write_ppm("out/detreal.ppm");
    printf("  pintada en out/detreal.ppm\n");

    /* Y la misma sin fila, para ver que sigue igual que antes. */
    st.titulo = "Volumen"; st.valor = "-12,0 dB";
    st.alt = 0; st.alt_on = 0U;
    st.preset[0] = 0; st.preset[1] = 0; st.preset[2] = 0; st.preset_on = -1;
    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_det_draw(&st);
    lcd_sim_write_ppm("out/detreal_sinfila.ppm");
    printf("  y sin fila en out/detreal_sinfila.ppm\n");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

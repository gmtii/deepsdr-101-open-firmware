/*
 * Render y comprobacion de anchos de la barra de acciones (etapa 4).
 *
 * Los textos se miden con la fuente real contra el ancho real del boton,
 * con el valor mas largo de cada uno - no con un caso cualquiera.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_act.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

static int fallos = 0;

static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    if (!ok) { fallos++; }
    printf("  %-34s %4d px en %4d  %s\n", que, w, hueco, ok ? "OK" : "NO CABE");
}

/* peor caso de cada boton */
/* Mismo orden que k_act_slots[] en main.c */
static const char *k_name[UI_ACT_N]  = { "Func", "Modo", "Paso",
                                         "Volumen", "Ruido", "Ajustes" };
/* El valor de "Func" es el nombre del ajuste enganchado, asi que su peor
 * caso no se puede escribir a mano: hay que medir TODOS los enganchables y
 * quedarse con el mas ancho. La primera version de este banco decia que era
 * "Silenciador" y se equivocaba - "Desplazamiento" es mas largo y tambien se
 * engancha. Los que ajuste_enganchar() rechaza (Reposo, las dos calibraciones
 * e Informacion) no estan porque nunca llegan al boton. */
static const char *k_func[] = {
    "AGC", "Ancho (BW)", "Volumen", "Silenciador", "Reducción", "Ganancia",
    "AGC de RF", "Atenuador", "Escala", "Autoescala", "Zoom", "Paleta",
    "Estilo", "Traza", "Suavizado", "Contorno", "Tema", "Brillo", "Altavoz",
    "Táctil", "Muestreo", "Filtro FM", "Desplazamiento", "Baudios",
    "Inversión", "Tono CW", "Velocidad CW", "Autoenganche"
};
#define K_FUNC_N ((int)(sizeof k_func / sizeof k_func[0]))

/* El peor valor que puede mostrar cada uno. El de Func lo rellena main(). */
static const char *k_worst[UI_ACT_N] = { 0, "RTTY-U", "12,5 kHz",
                                         "-47,5 dB", "apagado", 0 };

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(void)
{
    ui_act_state_t st = {0};
    uint8_t i;
    /* margen interior: 6 px a cada lado para que el texto no toque el borde
     * redondeado */
    const int16_t hueco = UI_ACT_BTN_W - 12;


    printf("Barra de acciones: %d botones de %dx%d, y=%d..%d, x=%d..%d\n\n",
           UI_ACT_N, UI_ACT_BTN_W, UI_ACT_H, UI_ACT_Y, UI_ACT_Y + UI_ACT_H - 1,
           UI_ACT_MARGIN,
           UI_ACT_MARGIN + (UI_ACT_N - 1) * (UI_ACT_BTN_W + UI_ACT_GAP)
             + UI_ACT_BTN_W - 1);

    /* Peor nombre enganchable: se mide la lista entera y gana el mas ancho.
     * Asi, anadir un ajuste con nombre largo rompe este banco y no la
     * pantalla del usuario. */
    {
        int j, mejor = 0;
        int16_t wmax = 0;
        for (j = 0; j < K_FUNC_N; j++) {
            int16_t w = gfx2_text_w(k_func[j], &font_ui_14);
            if (w > wmax) { wmax = w; mejor = j; }
        }
        k_worst[0] = k_func[mejor];
        printf("Func: %d ajustes enganchables, el mas ancho \"%s\" (%d px)\n\n",
               K_FUNC_N, k_func[mejor], wmax);
    }

    for (i = 0; i < UI_ACT_N; i++) {
        char q[64];
        snprintf(q, sizeof q, "nombre \"%s\"", k_name[i]);
        cabe(q, gfx2_text_w(k_name[i], &font_ui_14b), hueco);
        if (k_worst[i]) {
            snprintf(q, sizeof q, "  peor valor \"%s\"", k_worst[i]);
            cabe(q, gfx2_text_w(k_worst[i], &font_ui_14), hueco);
        }
    }

    /* la barra entera tiene que caber a lo ancho y no salirse por abajo */
    cabe("la barra completa",
         (int16_t)(2 * UI_ACT_MARGIN + UI_ACT_N * UI_ACT_BTN_W
                   + (UI_ACT_N - 1) * UI_ACT_GAP), GFX2_W);
    cabe("borde inferior", (int16_t)(UI_ACT_Y + UI_ACT_H), GFX2_H);
    /* y tiene que dejar aire con el waterfall, que acaba en 419 */
    cabe("aire sobre el waterfall (>=4 px)", (int16_t)(419 + 4), UI_ACT_Y);
    /* el alto minimo razonable para un dedo en este panel: 8 mm */
    cabe("alto minimo de dedo (47 px = 8 mm)", 47, UI_ACT_H);

    /* --- render --- */
    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    for (i = 0; i < UI_ACT_N; i++) {
        st.name[i]   = k_name[i];
        st.value[i]  = k_worst[i];
        st.active[i] = (uint8_t)(i == 4U);   /* Ruido encendido */
    }
    st.value[4] = "activo";
    st.pressed = 1;                          /* Volumen pulsado */
    ui_act_draw(&st);

    lcd_sim_write_ppm("out/acttest.ppm");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

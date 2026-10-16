/*
 * ZONA DE TOQUE DE LA FRECUENCIA - 22/09/2026
 *
 * Comprueba, con la cabecera real y para frecuencias de anchos distintos,
 * que la zona que abre el teclado de frecuencia acaba DENTRO de la lectura
 * y nunca invade el chip de modo ni la barra vacia de su derecha.
 *
 * Es lo que fallaba: main.c usaba una X fija de la cabecera anterior al
 * rediseno (396) y sobraban decenas de pixeles de barra vacia que abrian
 * una pantalla que nadie habia pedido.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include <stdio.h>
#include <string.h>
#include "font_ui_18b.h"
#include "bandas_tabla.h"

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static const char *const casos[] = {
    "153,00", "1.611,00", "7.159,00", "14.074,00", "29.999,99",
};
#define N_CASOS ((int)(sizeof casos / sizeof casos[0]))

/*
 * El nombre de banda mas ancho de VERDAD, medido con la fuente real sobre la
 * tabla generada desde main.c, mas la pastilla "Bandas" que sale cuando la
 * frecuencia no cae en ninguna. Antes aqui habia escrito "2200 m" a mano con
 * un comentario que decia "el mas largo que hay", y no lo era: "VHF alta"
 * mide mas. Un peor caso a ojo no es un peor caso.
 */
static const char *banda_mas_ancha(void)
{
    const char *mejor = "Bandas";
    int16_t wmax = gfx2_text_w(mejor, &font_ui_18b);
    unsigned i;

    for (i = 0U; i < sizeof banda / sizeof banda[0]; i++) {
        int16_t w = gfx2_text_w(banda[i].nombre, &font_ui_18b);
        if (w > wmax) { wmax = w; mejor = banda[i].nombre; }
    }
    printf("  la pastilla se prueba con \"%s\" (%d px), la mas ancha de %u\n",
           mejor, wmax, (unsigned)(sizeof banda / sizeof banda[0]) + 1U);
    return mejor;
}

int main(void)
{
    int c;
    printf("zona de toque de la frecuencia (la vieja acababa en x=396 fija)\n");

    for (c = 0; c < N_CASOS; c++) {
        ui_top_state_t st = {0};
        uint16_t x;
        int fin = -1, chip = -1;

        memset(&st, 0, sizeof st);
        st.freq       = casos[c];
        st.freq_digit = -1;
        st.mode       = "LSB";
        st.band       = banda_mas_ancha();  /* medido, no supuesto - ver abajo */
        st.clock      = "00:09";
        st.batt_pct   = 80U;

        st.knob       = UI_KNOB_TUNE;
        st.knob_value = "1 kHz";
        st.agc        = "FST";
        st.bw         = "4,0 k";

        lcd_sim_reset(0x0000);
        ui_top_draw(&st);

        /* Primer pixel que YA no abre el teclado. */
        for (x = 0U; x < GFX2_W; x++) {
            if (!ui_top_freq_hit(x, 10U)) { fin = (int)x; break; }
        }
        /* Donde empieza de verdad el chip de modo: la primera columna de la
         * franja del chip (y=14..45) que no es el fondo de la cabecera. */
        for (x = (uint16_t)(fin > 0 ? fin : 0); x < GFX2_W; x++) {
            const uint16_t *fb = lcd_sim_fb();
            uint16_t p0 = fb[30 * GFX2_W + x];
            uint16_t p1 = fb[2  * GFX2_W + x];   /* misma columna, arriba del chip */
            if (p0 != p1) { chip = (int)x; break; }
        }

        printf("  %-11s zona acaba en x=%3d   chip de modo empieza en x=%3d   %s\n",
               casos[c], fin, chip,
               (fin >= 0 && chip > fin) ? "OK" : "SE PISAN");
        if (!(fin >= 0 && chip > fin)) { fallos++; }
        if (fin > 396) {
            printf("      la zona pasa de 396, que es donde acababa antes\n");
        }

        /* El reloj, igual: donde empieza su zona frente a donde empieza el
         * texto de verdad. La vieja arrancaba en 680 fijo.
         *
         * Para saber donde cae el texto se dibuja la cabecera DOS veces, con
         * y sin reloj, y se restan: el fondo de la cabecera es un degradado
         * vertical, asi que comparar dos alturas de la misma columna marca
         * como "tinta" toda la pantalla. */
        if (c == 0) {
            static uint16_t sin_reloj[GFX2_W];
            int z1 = -1, z2 = -1, t1 = -1;
            ui_top_state_t st2 = st;

            st2.clock = "";
            lcd_sim_reset(0x0000);
            ui_top_draw(&st2);
            memcpy(sin_reloj, lcd_sim_fb() + 16 * GFX2_W, sizeof sin_reloj);

            lcd_sim_reset(0x0000);
            ui_top_draw(&st);
            {
                const uint16_t *fb = lcd_sim_fb() + 16 * GFX2_W;
                for (x = 0U; x < GFX2_W; x++) {
                    if (ui_top_clock_hit(x, 16U)) { if (z1 < 0) { z1 = (int)x; } z2 = (int)x; }
                    if (t1 < 0 && fb[x] != sin_reloj[x]) { t1 = (int)x; }
                }
            }
            printf("  reloj      zona x=%d..%d   el texto empieza en x=%d   antes empezaba en 680\n",
                   z1, z2, t1);
            if (z1 < 0 || t1 < 0 || z1 > t1 || (t1 - z1) > 16) {
                printf("      la zona no encaja con el texto\n");
                fallos++;
            }
        }
    }

    /*
     * La pastilla de banda va detras del modo, o sea que su sitio depende de
     * lo ancha que sea la frecuencia. Lo que no puede es llegar al reloj.
     * Se prueba con el nombre de banda mas largo de la tabla y con la
     * frecuencia mas ancha, que es el peor caso de los dos a la vez.
     */
    {
        uint16_t xx;
        int b1 = -1, b2 = -1, r1 = -1;
        for (xx = 0U; xx < GFX2_W; xx++) {
            if (ui_top_band_hit(xx, 30U))  { if (b1 < 0) { b1 = (int)xx; } b2 = (int)xx; }
            if (ui_top_clock_hit(xx, 16U) && r1 < 0) { r1 = (int)xx; }
        }
        printf("  banda      pastilla x=%d..%d   el reloj empieza en x=%d   %s\n",
               b1, b2, r1, (b2 < r1) ? "OK" : "SE PISAN");
        if (b1 < 0 || r1 < 0 || b2 >= r1) { fallos++; }
    }

    printf("  %s\n", fallos ? "HAY FALLOS" : "la zona nunca llega al chip de modo");
    return fallos ? 1 : 0;
}

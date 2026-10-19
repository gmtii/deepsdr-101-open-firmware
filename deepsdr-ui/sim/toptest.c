/* Etapa 2: cabecera + barra de estado, compilando el MISMO ui_top.c que ira
 * en la radio. Lo que salga en el PNG es lo que saldra en el panel. */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

int main(void)
{
    /* A CERO. La estructura crecio (el chip de ancho de filtro, etapa 4) y
     * esta escena no lo rellenaba: el campo nuevo quedaba con basura de la
     * pila y el dibujo se iba a leer una cadena que no existia. Con {0} un
     * campo que se anada manana sale simplemente apagado, no revienta. */
    ui_top_state_t st = {0};

    st.freq       = "7.200,00";
    st.freq_digit = 4;            /* el digito de 1 kHz */
    st.mode       = "AM";
    st.clock      = "21:47";
    st.batt_pct   = 78;

    st.s_units    = 10;           /* por encima de S9 */
    st.over_db    = 14;
    st.dbm        = -53;
    st.dbm_valid  = 1;
    st.knob       = UI_KNOB_TUNE;
    st.knob_value = "1 kHz";
    st.agc        = "MED";
    st.bw         = "4,0 k";
    st.att        = "OFF";
    st.ovr        = 0;
    st.spk_muted  = 0;
    st.sam_ppm    = 0;

    lcd_sim_reset(0);
    ui_top_draw(&st);
    /* debajo, un relleno para ver donde acaba la zona que toca esta etapa */
    {
        gfx2_surf_t dummy;
        (void)dummy;
    }
    lcd_sim_write_ppm("out/10_top.ppm");

    /* segunda variante: mando en otra cosa, sobrecarga activa, AGC apagado */
    st.knob       = UI_KNOB_PGA;
    st.knob_value = "24,0 dB";
    st.dbm_valid  = 0;
    st.agc        = "OFF";
    st.bw         = "2,3 k";
    st.att        = "-06";
    st.ovr        = 1;
    st.s_units    = 6;
    st.mode       = "USB";
    st.batt_pct   = 18;
    st.spk_muted  = 1;
    st.sam_ppm    = "+1,4 PPM";
    lcd_sim_reset(0);
    ui_top_draw(&st);
    lcd_sim_write_ppm("out/11_top_b.ppm");

    /* --- comprobacion del peor caso, medida y no estimada --- */
    {
        /*
         * Cada destino del mando con los valores que ESE destino puede
         * mostrar, no todos contra todos: "Silenciador 12,5 kHz" no existe,
         * y dimensionar la franja para un caso imposible obliga a quitarle
         * sitio a los chips, que si existen.
         */
        struct { const char *nombre; const char *peor; } pares[] = {
            { "Sintonía",    "12,5 kHz" },   /* pasos de sintonia */
            { "Volumen",     "-47,5 dB" },
            { "Brillo",      "100"      },
            { "Escala mín.", "-110"     },
            { "Escala máx.", "-110"     },
            { "Silenciador", "-96 dB"   },
            { "Suavizado",   "0,95"     },
            { "Ganancia",    "-47,5 dB" },
            { "Reducción",   "100"      },
            { "RTTY",        "170 Hz"   },
        };
        int16_t peor = 0, i;
        const char *quien = "";
        for (i = 0; i < (int16_t)(sizeof pares / sizeof pares[0]); i++) {
            int16_t w = (int16_t)(gfx2_text_w(pares[i].nombre, &font_ui_14b)
                                  + gfx2_text_w(pares[i].peor, &font_ui_14b) + 44 + 8);
            if (w > peor) { peor = w; quien = pares[i].nombre; }
        }
        printf("pastilla del mando, el mas ancho es \"%s\"\n", quien);
        printf("pastilla del mando, peor caso: %d px (hueco disponible 204)  %s\n",
               peor, peor <= 204 ? "OK" : "SE SALE");
        /*
         * 146 Y NO 150 - 19/10/2026. La franja de la lectura se encogio al
         * correr la pastilla del mando a la izquierda para que cupiera el
         * chip ATT; ver ST_READ_W en ui_top.c. Y el peor caso se mide con
         * "-127", que es el dBm mas ancho que puede salir, no con "-105".
         */
        {
            int16_t lec = (int16_t)(gfx2_text_w("S9+60", &font_ui_18b) + 12
                                    + gfx2_text_w("-127", &font_ui_14b) + 3
                                    + gfx2_text_w("dBm", &font_ui_14));
            printf("lectura S+dBm, peor caso:      %d px (hueco disponible 146)  %s\n",
                   (int)lec, lec <= 146 ? "OK" : "SE SALE");
            if (lec > 146) { return 1; }
        }
    }
    /* variante C: NR encendido y AGC en MED, sin sobrecarga */
    st.ovr = 0; st.spk_muted = 0; st.sam_ppm = 0; st.att = "-12";
    st.agc = "MED"; st.bw = "4,0 k"; st.mode = "AM"; st.s_units = 7; st.dbm_valid = 1;
    st.dbm = -78; st.knob = UI_KNOB_VOLUME; st.knob_value = "18";
    st.batt_pct = 60;
    lcd_sim_reset(0); ui_top_draw(&st); lcd_sim_write_ppm("out/12_top_c.ppm");

    /* variante D: el mando en el destino de nombre mas largo */
    st.ovr = 0; st.spk_muted = 0; st.sam_ppm = 0; st.att = "OFF";
    st.agc = "MED"; st.bw = "1,8 k"; st.mode = "LSB"; st.s_units = 4; st.dbm_valid = 1;
    st.dbm = -92; st.knob = UI_KNOB_SQUELCH; st.knob_value = "-96 dB";
    st.batt_pct = 90;
    lcd_sim_reset(0); ui_top_draw(&st); lcd_sim_write_ppm("out/13_top_d.ppm");

    /* ================================================================
     * EL TANTO POR CIENTO DENTRO DE LA BATERIA - 13/10/2026
     * ================================================================
     *
     * Pedido por el usuario.
     *
     * Dos cosas, y las dos leyendo PIXELES del lienzo, no repitiendo las
     * restas de ui_top.c:
     *
     *   1) QUE EL NUMERO ESTA DENTRO Y NO TOCA LOS BORDES. Una letra de
     *      catorce pixeles en una caja de quince -lo que habia- cabe segun
     *      la resta y sale pegada al contorno segun el ojo.
     *
     *   2) QUE SE LEE EN LAS OCHO PALETAS, sobre el relleno Y sobre el
     *      hueco. El relleno es un tinte precisamente por esto, y "es un
     *      tinte" es una afirmacion sobre el color compuesto, que es algo
     *      que se mide.
     */
    {
        static const struct { const char *nombre; const palette_t *p; } pal[] = {
            { "oscura",     &k_pal_oscura    },
            { "contraste",  &k_pal_contraste },
            { "ambar",      &k_pal_ambar     },
            { "fria",       &k_pal_fria      },
            { "oliva",      &k_pal_oliva     },
            { "naranja",    &k_pal_naranja   },
            { "fosforo",    &k_pal_fosforo   },
            { "chemistry",  &k_pal_chemistry }
        };
        /* La caja, tal y como la pinta ui_top.c. */
        const int16_t bw = 48, bh = 20;
        const int16_t bx = (int16_t)(790 - bw - 2), by = 34;
        unsigned k;
        int malos = 0;

        printf("\nla bateria con el %% dentro\n");

        for (k = 0; k < sizeof pal / sizeof pal[0]; k++) {
            int16_t x, y;
            int16_t x0 = 9999, x1 = -1, y0 = 9999, y1 = -1;
            long peor = 999999L;
            int16_t peor_x = 0;

            g_pal = pal[k].p;
            st.batt_pct = 47;          /* el relleno parte el numero por medio */
            lcd_sim_reset(0);
            ui_top_draw(&st);

            /*
             * La caja de la tinta: el color del texto es PAL_INK y el de
             * todo lo demas de ahi dentro no, asi que se busca ese color
             * exacto. Es la unica cosa de la caja que lo lleva.
             */
            {
                uint16_t tinta = gfx2_to565(gfx2_rgb(PAL_INK).r,
                                            gfx2_rgb(PAL_INK).g,
                                            gfx2_rgb(PAL_INK).b);
                for (y = (int16_t)(by + 2); y < (int16_t)(by + bh - 2); y++) {
                    for (x = (int16_t)(bx + 2); x < (int16_t)(bx + bw - 2); x++) {
                        if (lcd_sim_fb()[y * LCD_W + x] == tinta) {
                            if (x < x0) { x0 = x; }
                            if (x > x1) { x1 = x; }
                            if (y < y0) { y0 = y; }
                            if (y > y1) { y1 = y; }
                        }
                    }
                }
            }

            /*
             * Y EL CONTRASTE, columna a columna: para cada columna del
             * numero, la tinta contra el color que tiene debajo justo al
             * lado. Asi se mide sobre el relleno y sobre el hueco sin
             * tener que saber donde acaba uno y empieza el otro.
             */
            for (x = (int16_t)(bx + 3); x < (int16_t)(bx + bw - 3); x++) {
                uint16_t c = lcd_sim_fb()[(by + 3) * LCD_W + x];
                long lf = (long)(((c >> 11) & 0x1F) * 8) * 54L
                        + (long)(((c >> 5) & 0x3F) * 4) * 183L
                        + (long)((c & 0x1F) * 8) * 19L;
                long li = (long)gfx2_rgb(PAL_INK).r * 54L
                        + (long)gfx2_rgb(PAL_INK).g * 183L
                        + (long)gfx2_rgb(PAL_INK).b * 19L;
                long d = (lf > li) ? (lf - li) : (li - lf);

                if (d < peor) { peor = d; peor_x = x; }
            }

            printf("    %-10s tinta en x %d..%d  y %d..%d"
                   "   aire arriba %d abajo %d   contraste minimo %ld (x=%d)\n",
                   pal[k].nombre, (int)x0, (int)x1, (int)y0, (int)y1,
                   (int)(y0 - by), (int)(by + bh - 1 - y1),
                   peor / 256L, (int)peor_x);

            if (x1 < 0) { printf("      *** MAL: no hay numero ***\n"); malos++; continue; }
            /* Dentro del contorno de 2 px, con al menos un pixel de aire. */
            if (x0 <= bx + 2 || x1 >= bx + bw - 3) {
                printf("      *** MAL: el numero toca los lados ***\n"); malos++;
            }
            if (y0 <= by + 2 || y1 >= by + bh - 3) {
                printf("      *** MAL: el numero toca arriba o abajo ***\n"); malos++;
            }
            /* Y CENTRADO DE VERDAD: el aire de arriba y el de abajo, con
             * un pixel de tolerancia por la paridad. Esto es lo que caza
             * que el alto de una CIFRA no es el ascent de la fuente. */
            {
                int arriba = (int)(y0 - by), abajo = (int)(by + bh - 1 - y1);
                int d = arriba - abajo;

                if (d < 0) { d = -d; }
                if (d > 1) {
                    printf("      *** MAL: descentrado %d px ***\n", d);
                    malos++;
                }
            }
            /* 40 de 255 en luminancia es la frontera por debajo de la cual
             * dos colores dejan de distinguirse de un vistazo a esta
             * distancia; es el mismo criterio de ETI_CONTRASTE en
             * mapa_eti.c, que se eligio midiendo las mismas ocho paletas. */
            if (peor / 256L < 40L) {
                printf("      *** MAL: el numero no se lee sobre el relleno ***\n");
                malos++;
            }
        }
        g_pal = &k_pal_oscura;
        st.batt_pct = 47;
        lcd_sim_reset(0); ui_top_draw(&st);
        lcd_sim_write_ppm("out/14_top_bateria.ppm");
        printf("    %s   (foto en out/14_top_bateria.ppm)\n",
               malos ? "*** HAY FALLOS ***" : "se lee en las ocho paletas   ok");
        if (malos) { return 1; }
    }

    printf("ok\n");
    return 0;
}

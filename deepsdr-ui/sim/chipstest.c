/*
 * PEOR CASO DE LA BARRA DE ESTADO - 23/09/2026.
 *
 * Los chips de la derecha (SOBRECARGA / AGC / BW / NR / NCO / MUDO) se
 * dibujan de derecha a izquierda y el que no cabe entre el borde y la
 * pastilla del mando se SALTA. Eso hace imposible el solape por
 * construccion, que es lo que se buscaba... pero tiene la otra cara: un
 * indicador que desaparece es un indicador que miente por omision.
 *
 * Este banco recorre todas las combinaciones del peor caso -el nombre de
 * mando mas largo, con y sin sobrecarga, con y sin altavoz mudo- y cuenta
 * cuantos chips sobreviven de verdad, preguntandole a ui_top_hit() pixel a
 * pixel en vez de fiarse de las cuentas. Si al anadir un chip mas alguno
 * empieza a caerse, aqui se ve; antes solo se veia en la placa.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include <stdio.h>
#include <string.h>
#include "peores.h"
#include "rds_pi.h"

const palette_t *g_pal = &k_pal_oscura;
static void fondo(gfx2_surf_t *s, void *c){(void)c;gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0));}

static const char *nombre_chip(int id)
{
    switch (id) {
    case UI_TOP_HIT_AGC: return "AGC";
    case UI_TOP_HIT_BW:  return "BW";
    case UI_TOP_HIT_ATT: return "ATT";
    case UI_TOP_HIT_NCO: return "NCO";
    case UI_TOP_HIT_JMP: return "JMP";
    case UI_TOP_HIT_SPK: return "MUDO";
    case UI_TOP_HIT_OVR: return "SOBRECARGA";
    default:             return "?";
    }
}

/* Los ids distintos que hay dibujados, barriendo la franja a lo ancho. */
static int presentes(char *texto, size_t n)
{
    int visto[8] = {0};
    int x, cuantos = 0;
    texto[0] = '\0';
    for (x = 0; x < 800; x++) {
        int id = (int)ui_top_hit((uint16_t)x, (uint16_t)(UI_STATUS_Y + 20));
        if (id > 0 && id < 8 && !visto[id]) {
            visto[id] = 1;
            cuantos++;
            if (strlen(texto) + 12 < n) {
                strcat(texto, nombre_chip(id));
                strcat(texto, " ");
            }
        }
    }
    return cuantos;
}

int main(void)
{
    ui_top_state_t t;
    unsigned k;
    int ovr, mudo, fallos = 0, peor_n = 99;
    const char *peor_nombre = "";
    char lista[80];

    lcd_sim_reset(0);
    gfx2_render_screen(fondo, 0);
    printf("peor caso de los chips de la barra de estado\n");
    printf("  (el mando se prueba con los %d ajustes que se le pueden enganchar,\n"
           "   cada uno con SU peor valor - ver sim/peores.h)\n",
           (int)(sizeof k_peores / sizeof k_peores[0]));

    for (k = 0; k < sizeof k_peores / sizeof k_peores[0]; k++) {
        for (ovr = 0; ovr < 2; ovr++) {
            for (mudo = 0; mudo < 2; mudo++) {
                int n, minimo;
                memset(&t, 0, sizeof t);
                t.freq = "7.200,00"; t.mode = "AM"; t.band = "40 m";
                t.clock = "23:41";
                t.s_units = 12; t.over_db = 20; t.dbm = -79; t.dbm_valid = 1;
                t.agc = "MED"; t.bw = "4,0 k";
                t.att = "-12"; t.nco_on = 1;
                t.knob = UI_KNOB_TUNE;
                t.knob_name = k_peores[k].nombre;
                t.knob_value = k_peores[k].peor;
                t.ovr = (uint8_t)ovr; t.spk_muted = (uint8_t)mudo;

                ui_top_draw_status(&t);
                n = presentes(lista, sizeof lista);
                /*
                 * CINCO desde el 15/10/2026, con el chip JMP.
                 *
                 * AGC, BW, NR, NCO y JMP son estado de la radio, no avisos:
                 * los cinco tienen que estar siempre. JMP ademas cambia lo
                 * que hace el MANDO, asi que si se cae, el usuario gira y la
                 * frecuencia hace algo que no entiende y no tiene donde
                 * mirar por que. MUDO si puede caerse, y se dice cuando.
                 */
                minimo = 5;
                if (n < minimo) {
                    printf("  mando \"%s\" = \"%s\"  ovr=%d mudo=%d  ->  %d chips: %s\n",
                           k_peores[k].nombre, k_peores[k].peor, ovr, mudo, n, lista);
                    printf("      *** se ha caido uno de los cinco fijos ***\n");
                    fallos++;
                }
                if (n < peor_n) { peor_n = n; peor_nombre = k_peores[k].nombre; }
                if (!mudo && !ovr && k == 0) {
                    printf("  ejemplo (mando \"%s\"): %s\n", k_peores[k].nombre, lista);
                }
            }
        }
    }
    printf("  el caso mas apretado es el mando en \"%s\": %d chips\n",
           peor_nombre, peor_n);
    /* ------------------------------------------------------------------ */
    /* ------------------------------------------------------------------ */
    /*
     * QUE EL CHIP DEL ATENUADOR NO MUEVA A LOS DEMAS - 19/10/2026.
     *
     * *** El dueño: "para que la chapa att siempre tenga el mismo tamaño y
     * no se muevan las cosas de sitio". ***
     *
     * Los chips se colocan de DERECHA A IZQUIERDA, asi que si ATT cambia de
     * ancho al cambiar de estado, todo lo que tiene a su izquierda -los
     * demas chips y la pastilla del mando- da un saltito cada vez que lo
     * tocas. Con "-06" en vez de "-6" los dos encendidos ya miden igual;
     * los 2 px que quedaban contra "OFF" los quita el ancho reservado de
     * chip_w().
     *
     * No se comprueba la cuenta: se dibujan los TRES estados y se mira
     * donde empieza cada chip preguntandole a ui_top_hit() pixel a pixel,
     * que es lo que de verdad se ve y se toca.
     */
    printf("\nel chip ATT no mueve nada al cambiar de estado\n");
    {
        static const char *const estados[3] = { "OFF", "-06", "-12" };
        int16_t izq[3][8];
        int e, id, malos = 0;

        for (e = 0; e < 3; e++) {
            int x;
            for (id = 0; id < 8; id++) { izq[e][id] = -1; }
            memset(&t, 0, sizeof t);
            t.freq = "7.200,00"; t.mode = "AM"; t.band = "40 m";
            t.clock = "23:41";
            t.s_units = 7; t.dbm = -79; t.dbm_valid = 1;
            t.agc = "MED"; t.bw = "4,0 k";
            t.att = estados[e]; t.nco_on = 1;
            t.knob = UI_KNOB_TUNE; t.knob_value = "1 kHz";
            ui_top_draw_status(&t);
            for (x = 0; x < 800; x++) {
                int q = (int)ui_top_hit((uint16_t)x, (uint16_t)(UI_STATUS_Y + 20));
                if (q > 0 && q < 8 && izq[e][q] < 0) { izq[e][q] = (int16_t)x; }
            }
        }
        for (id = 1; id < 8; id++) {
            if (izq[0][id] < 0 && izq[1][id] < 0 && izq[2][id] < 0) { continue; }
            printf("    %-5s empieza en  %3d  %3d  %3d",
                   nombre_chip(id), izq[0][id], izq[1][id], izq[2][id]);
            if (izq[0][id] == izq[1][id] && izq[1][id] == izq[2][id]) {
                printf("   ok\n");
            } else {
                printf("   *** SE MUEVE ***\n");
                malos++;
            }
        }
        if (malos) { fallos += malos; }
        else { printf("    (los tres estados dejan todo en el mismo sitio)\n"); }
    }

    printf("\nla lista de cadenas por codigo de programa\n");
    {
        uint16_t i, j, n = rds_pi_cuantas();
        int16_t hueco;

        /* El hueco de la marquesina depende de lo ancha que sea la pastilla
         * de banda, y en FM pone "FM", que es de las mas estrechas. Se mide
         * con la pantalla montada tal y como queda en FM. */
        memset(&t, 0, sizeof t);
        t.freq = "106.300,00"; t.mode = "WFM"; t.band = "FM";
        t.clock = "00:03"; t.s_units = 9;
        t.agc = "FST"; t.att = "OFF"; t.nco_on = 1;
        t.knob = UI_KNOB_TUNE; t.knob_value = "100 kHz";
        ui_top_draw(&t);
        hueco = ui_top_rds_ancho();
        printf("  %u cadenas, y el hueco de la cabecera en FM mide %d px\n",
               n, hueco);

        for (i = 0; i < n; i++) {
            int16_t w = ui_top_txtw_dbg(rds_pi_nombre_i(i), 14);
            printf("    %04X  %-14s %3d px%s\n", rds_pi_codigo(i),
                   rds_pi_nombre_i(i), w, (w <= hueco) ? "" : "   <-- NO CABE");
            if (w > hueco) { fallos++; }
            /* Codigos repetidos: dos cadenas con el mismo codigo significa
             * que una de las dos se enseñaria con el nombre de la otra. */
            for (j = i + 1; j < n; j++) {
                if (rds_pi_codigo(i) == rds_pi_codigo(j)) {
                    printf("      *** %04X esta dos veces ***\n", rds_pi_codigo(i));
                    fallos++;
                }
            }
            /* Esta lista es SOLO de Espana - ver rds_pi.h. Un codigo que no
             * empiece por E es de otro pais y aqui sobra. */
            if ((rds_pi_codigo(i) >> 12) != 0xEU) {
                printf("      *** %04X no es un codigo espanol ***\n", rds_pi_codigo(i));
                fallos++;
            }
        }
        /* Y el caso de "no esta en la lista": se enseñan cuatro digitos. */
        {
            int16_t w = ui_top_txtw_dbg("E2CA", 14);
            printf("    y un codigo desconocido, en crudo: %d px\n", w);
            if (w > hueco) { fallos++; }
        }
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

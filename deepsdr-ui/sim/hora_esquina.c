/*
 * EL RELOJ SE REPINTA SOLO EN SU ESQUINA, Y TIENE QUE SALIR IGUAL
 * ================================================================
 *
 * DE DONDE SALE ESTO - 20/10/2026.
 *
 * Medido en la radio con la vuelta del bucle partida en tramos: el reloj
 * se llevaba 109 ms de una vuelta de 168, UNA VEZ POR MINUTO, y el tramo
 * que lo decia -`cab`- señalaba a componer la cabecera, no a empujarla al
 * bus. time_display_draw() llamaba a ui_top_draw(): la cabecera entera
 * (800x64) mas la marquesina mas la franja de estado (800x40). 91.003
 * accesos al bus para cambiar cinco caracteres.
 *
 * Es la cuarta vez que aparece el mismo fallo en este fichero -el S-metro
 * el 30/09, el PPM del SAM el 04/10 y la frecuencia el 05/10- y se arregla
 * igual: el MISMO draw_header con la ventana recortada a donde vive lo que
 * ha cambiado. Medido aqui mismo: 5.512 accesos en vez de 91.003.
 *
 * LO QUE ESTE BANCO COMPRUEBA, que es lo unico que puede salir mal: que
 * pintar solo la esquina deja la pantalla EXACTAMENTE igual que pintar la
 * cabecera entera. Se hacen las dos cosas y se comparan los 384.000
 * pixeles del marco, no la esquina: asi tambien se ve si la ventana se
 * come algo de al lado -el PPM del SAM vive pegado, de 620 a 714-.
 *
 * Y se prueba con los relojes mas anchos y con la bateria cambiando,
 * porque el reloj va alineado a la DERECHA: si algun dia crece, lo que se
 * sale es por la izquierda, que es justo donde esta el borde de la
 * ventana.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

static int fallos;
static uint16_t marco_a[LCD_W * LCD_H];

static void base(ui_top_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->freq       = "7.105,00";
    st->freq_digit = 4;
    st->mode       = "LSB";
    st->band       = "40 m";
    st->en_banda   = 1U;
    st->clock      = "13:40";
    st->batt_pct   = 93U;
    st->s_units    = 7U;
    st->dbm        = -80;
    st->dbm_valid  = 1U;
    st->knob       = UI_KNOB_TUNE;
    st->knob_value = "1 kHz";
    st->agc        = "FST";
    st->bw         = "0-2,3k";
    st->att        = "OFF";
    st->rds_nombre = "Radio Mediterranee Int'l";
    st->rds        = "MRC - Nador - Fre - 05:00-24:00 UTC";
}

/* Pinta la cabecera entera con el estado `antes`, cambia a `despues` y
 * repinta: entero en un caso, solo la esquina (o el S-metro) en el otro.
 * Compara los 384.000 pixeles del marco. */
static void prueba_f(const char *que, ui_top_state_t *antes,
                     ui_top_state_t *despues,
                     void (*parcial)(const ui_top_state_t *))
{
    uint32_t distintos = 0U, i;

    lcd_sim_reset(0x0000);
    ui_top_draw(antes);
    ui_top_draw(despues);                  /* la referencia: entera */
    memcpy(marco_a, lcd_sim_fb(), sizeof marco_a);

    lcd_sim_reset(0x0000);
    ui_top_draw(antes);
    parcial(despues);                      /* lo nuevo: solo su trozo */

    for (i = 0U; i < (uint32_t)(LCD_W * LCD_H); i++) {
        if (lcd_sim_fb()[i] != marco_a[i]) { distintos++; }
    }
    printf("    %-44s %s", que, distintos ? "*** MAL ***" : "ok");
    if (distintos) {
        printf("  %u pixeles distintos", (unsigned)distintos);
        fallos++;
    }
    printf("\n");
}

static void prueba(const char *que, ui_top_state_t *antes,
                   ui_top_state_t *despues)
{
    uint32_t distintos = 0U, i;

    lcd_sim_reset(0x0000);
    ui_top_draw(antes);
    ui_top_draw(despues);                  /* la referencia: entera */
    memcpy(marco_a, lcd_sim_fb(), sizeof marco_a);

    lcd_sim_reset(0x0000);
    ui_top_draw(antes);
    ui_top_draw_hora(despues);             /* lo nuevo: solo la esquina */

    for (i = 0U; i < (uint32_t)(LCD_W * LCD_H); i++) {
        if (lcd_sim_fb()[i] != marco_a[i]) { distintos++; }
    }
    printf("    %-44s %s", que, distintos ? "*** MAL ***" : "ok");
    if (distintos) {
        printf("  %u pixeles distintos", (unsigned)distintos);
        fallos++;
    }
    printf("\n");
}

int main(void)
{
    ui_top_state_t a, b;
    lcd_cost_t c;

    printf("\n== EL RELOJ, SOLO EN SU ESQUINA ==\n\n");

    base(&a); base(&b); b.clock = "13:41";
    prueba("cambia el minuto", &a, &b);

    base(&a); base(&b); b.clock = "00:00";
    prueba("y a medianoche, que es el mas estrecho", &a, &b);

    base(&a); a.clock = "00:00"; base(&b); b.clock = "23:59";
    prueba("del mas estrecho al mas ancho", &a, &b);

    base(&a); base(&b); b.batt_pct = 9U;
    prueba("la bateria, que vive en la misma esquina", &a, &b);

    base(&a); base(&b); b.batt_pct = 100U; b.clock = "09:07";
    prueba("las dos a la vez, y con el 100%", &a, &b);

    /* Con el PPM del SAM puesto, que vive pegado por la izquierda: la
     * ventana no puede comerselo. */
    base(&a); a.sam_ppm = "+19,4 PPM";
    base(&b); b.sam_ppm = "+19,4 PPM"; b.clock = "13:41";
    prueba("con el PPM del SAM al lado, que no se toca", &a, &b);

    /*
     * Y EL S-METRO, que es el mismo cuento con otra ventana: la barra y su
     * etiqueta se mueven con la señal, la pastilla del mando y los chips
     * de la derecha no. Ver ui_top_draw_smeter() y ST_MOVIL_Y1.
     */
    printf("\n== EL S-METRO, SOLO SU BARRA ==\n\n");

    base(&a); base(&b); b.s_units = 4U;
    prueba_f("la barra baja", &a, &b, ui_top_draw_smeter);

    base(&a); base(&b); b.s_units = 9U;
    prueba_f("hasta S9 justo, donde cambia de color", &a, &b, ui_top_draw_smeter);

    base(&a); base(&b); b.s_units = 12U; b.over_db = 21U;
    prueba_f("y por encima de S9, con el +21", &a, &b, ui_top_draw_smeter);

    base(&a); a.s_units = 12U; a.over_db = 60U; base(&b); b.s_units = 0U;
    prueba_f("de S9+60 a cero, que es el que mas borra", &a, &b, ui_top_draw_smeter);

    /* Con el mando en otra cosa: la pastilla cambia de ancho, y la ventana
     * del S-metro acaba justo donde ella empieza. */
    base(&a); a.knob = UI_KNOB_VOLUME; a.knob_value = "-47,5 dB";
    base(&b); b.knob = UI_KNOB_VOLUME; b.knob_value = "-47,5 dB"; b.s_units = 3U;
    prueba_f("con el mando en volumen, que mueve la pastilla", &a, &b,
             ui_top_draw_smeter);

    printf("\n== LO QUE CUESTA ==\n\n");
    base(&a);
    lcd_sim_reset(0x0000);
    ui_top_draw(&a);
    lcd_sim_cost_reset();
    ui_top_draw(&a);
    c = lcd_sim_cost();
    printf("    la cabecera entera   %6llu accesos al bus\n",
           (unsigned long long)c.bus_writes);
    lcd_sim_cost_reset();
    ui_top_draw_hora(&a);
    c = lcd_sim_cost();
    printf("    solo la esquina      %6llu accesos al bus\n",
           (unsigned long long)c.bus_writes);
    if (c.bus_writes > 12000ULL) {
        printf("    *** MAL: la esquina deberia costar menos de 12.000 ***\n");
        fallos++;
    }
    lcd_sim_cost_reset();
    ui_top_draw_smeter(&a);
    c = lcd_sim_cost();
    printf("    solo la barra del S  %6llu accesos al bus\n",
           (unsigned long long)c.bus_writes);
    if (c.bus_writes > 8000ULL) {
        printf("    *** MAL: la barra deberia costar menos de 8.000 ***\n");
        fallos++;
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

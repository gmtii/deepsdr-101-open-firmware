/*
 * LA PANTALLA DE COPIAR LA BASE DE AVIONES, PARA MIRARLA.
 *
 * Pedido por el usuario. Una queja visual
 * no se contesta compilando: se contesta mirando. Esto pinta las cuatro
 * fases en ficheros que se pueden abrir, que es la unica forma de saber
 * si ha quedado bien antes de gastarle un flasheo.
 *
 * Y de paso mide, que es lo que no se ve mirando: que ningun rotulo se
 * salga de la pantalla ni se coma la barra.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_18b.h"
#include "aviones_pantalla.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

static void cabe(const char *txt, const gfx2_font_t *f, const char *que)
{
    int16_t w = gfx2_text_w(txt, f);
    int ok = (w <= GFX2_W - 40);

    printf("    %-52s %4d px   %s\n", que, w, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

int main(void)
{
    lcd_sim_reset(0x0000);
    aviones_pantalla(AVIP_BUSCANDO, 0U, 0, 0UL);
    lcd_sim_write_ppm("out/aviones_1_abriendo.ppm");

    lcd_sim_reset(0x0000);
    aviones_pantalla(AVIP_COPIANDO, 37U, 0, 515720UL);
    lcd_sim_write_ppm("out/aviones_2_copiando.ppm");

    lcd_sim_reset(0x0000);
    aviones_pantalla(AVIP_COMPROBANDO, 41U, 0, 512000UL);
    lcd_sim_write_ppm("out/aviones_3_comprobando.ppm");

    lcd_sim_reset(0x0000);
    aviones_pantalla(AVIP_HECHO, 100U, 0, 515720UL);
    lcd_sim_write_ppm("out/aviones_3_hecho.ppm");

    lcd_sim_reset(0x0000);
    aviones_pantalla(AVIP_FALLO, 0U, "sus clusters no son seguidos", 0UL);
    lcd_sim_write_ppm("out/aviones_4_fallo.ppm");

    printf("\n  PANTALLA DE LA BASE DE AVIONES\n\n");
    cabe("Base de datos de aviones", &font_ui_18b, "el titulo");
    cabe("ICAO24.BIN, del disco a la zona alta de la flash",
         &font_ui_14, "la linea de debajo");
    cabe("Cargada: 515.720 bytes. Borrada del disco.",
         &font_ui_14, "el pie cuando acaba");
    cabe("sus clusters no son seguidos", &font_ui_14, "el motivo mas largo");

    cabe("Comprobando lo copiado...", &font_ui_14, "el pie mientras comprueba");

    /*
     * LOS PUNTOS DE MILLAR, CON NUMEROS DE TODOS LOS LARGOS.
     *
     * Pedido por el usuario. El separador se ponia contando desde la izquierda, que
     * da lo mismo con 3, 6 o 9 cifras y esta mal en el resto - y hasta ese
     * dia los unicos numeros que enseñaba esta pantalla eran de seis-.
     *
     * Asi que aqui van de UNA a NUEVE cifras, que es lo que faltaba. La
     * respuesta esperada se escribe a mano: sacarla con la misma regla que
     * usa el codigo seria comprobar que el codigo hace lo que hace.
     */
    {
        static const struct { uint32_t v; const char *sale; } k[] = {
            { 0UL,          "0" },
            { 7UL,          "7" },
            { 42UL,         "42" },
            { 999UL,        "999" },
            { 1000UL,       "1.000" },
            { 4096UL,       "4.096" },
            { 66560UL,      "66.560" },     /* el de la foto */
            { 512000UL,     "512.000" },
            { 515720UL,     "515.720" },
            { 1048576UL,    "1.048.576" },
            { 4294967295UL, "4.294.967.295" },
        };
        unsigned i;

        printf("\n  PUNTOS DE MILLAR\n\n");
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            char t[16];
            int ok;

            aviones_pantalla_miles_dbg(t, k[i].v);
            ok = (strcmp(t, k[i].sale) == 0);
            printf("    %10u -> %-16s %s", (unsigned)k[i].v, t,
                   ok ? "ok\n" : "MAL");
            if (!ok) { printf("  (deberia ser %s)\n", k[i].sale); fallos++; }
        }
    }

    printf("\n  imagenes: out/aviones_*.ppm\n");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

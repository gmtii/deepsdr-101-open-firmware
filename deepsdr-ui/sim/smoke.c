/*
 * Prueba de humo del simulador + medida del coste real de gfx_char().
 * Compila contra el User/gfx.c del repositorio sin modificarlo.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include <stdio.h>

int main(void)
{
    lcd_cost_t c;

    /* --- 1. Coste de UN glyph con el gfx.c actual --- */
    lcd_sim_reset(GFX_COLOR_BLACK);
    lcd_sim_cost_reset();
    gfx_char(10, 10, 'A', GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
    c = lcd_sim_cost();
    printf("gfx_char('A', scale=1): ventanas=%llu accesos_bus=%llu pixeles=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes,
           (unsigned long long)c.pixels);
    printf("  -> minimo teorico (1 ventana + 6x7 pixeles) = %d accesos\n", 17 + 42);
    printf("  -> desperdicio = %.1fx\n", (double)c.bus_writes / (17.0 + 42.0));

    /* --- 2. Coste de una cadena tipica de la barra de estado --- */
    lcd_sim_cost_reset();
    gfx_text(10, 30, "S9+20 -73dBm AGC MED", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    c = lcd_sim_cost();
    printf("\ngfx_text(20 chars, scale=2): ventanas=%llu accesos_bus=%llu\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes);
    printf("  -> a 21 ciclos/acceso y AHB 200MHz: %.2f ms\n",
           lcd_sim_est_us(21, 200.0) / 1000.0);

    /* --- 3. Coste de un fill_rect (camino rapido, para contraste) --- */
    lcd_sim_cost_reset();
    gfx_fill_rect(0, 60, 800, 64, GFX_COLOR_DARKGRAY);
    c = lcd_sim_cost();
    printf("\ngfx_fill_rect(800x64): ventanas=%llu accesos_bus=%llu (optimo)\n",
           (unsigned long long)c.windows, (unsigned long long)c.bus_writes);

    /* --- 4. Pantalla completa, para calibrar el techo de refresco --- */
    lcd_sim_cost_reset();
    gfx_fill_screen(GFX_COLOR_BLACK);
    c = lcd_sim_cost();
    printf("\nfill_screen 800x480: accesos_bus=%llu\n", (unsigned long long)c.bus_writes);
    printf("  -> timings ACTUALES (21 ciclos/acceso @200MHz): %.1f ms  = %.1f fps\n",
           lcd_sim_est_us(21, 200.0) / 1000.0, 1000.0 / (lcd_sim_est_us(21, 200.0) / 1000.0));
    printf("  -> timings AJUSTADOS (6 ciclos/acceso @200MHz):  %.1f ms  = %.1f fps\n",
           lcd_sim_est_us(6, 200.0) / 1000.0, 1000.0 / (lcd_sim_est_us(6, 200.0) / 1000.0));

    /* --- 5. Muestra visual de las fuentes y la paleta actuales --- */
    lcd_sim_reset(GFX_COLOR_BLACK);
    gfx_text(20,  20, "DEEPSDR 101 - fuente 5x7 actual", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
    gfx_text(20,  50, "scale=2 ABCDEFG abcdefg 0123456789", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    gfx_text(20,  90, "scale=3 14.200 MHz", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 3);
    gfx_text(20, 140, "scale=4 14.200", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 4);
    gfx_text(20, 200, "scale=6 14.200", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 6);
    {
        /* la paleta entera del repo, tal cual */
        const uint16_t pal[] = {
            GFX_COLOR_BLACK, GFX_COLOR_WHITE, GFX_COLOR_RED, GFX_COLOR_GREEN,
            GFX_COLOR_BLUE, GFX_COLOR_YELLOW, GFX_COLOR_CYAN, GFX_COLOR_MAGENTA,
            GFX_COLOR_GRAY, GFX_COLOR_DARKGRAY, GFX_COLOR_ORANGE
        };
        const char *nm[] = {"BLACK","WHITE","RED","GREEN","BLUE","YELLOW",
                            "CYAN","MAGENTA","GRAY","DARKGRAY","ORANGE"};
        int i;
        gfx_text(20, 300, "Paleta actual (11 colores):", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
        for (i = 0; i < 11; i++) {
            gfx_fill_rect((uint16_t)(20 + i * 70), 340, 60, 60, pal[i]);
            gfx_rect((uint16_t)(20 + i * 70), 340, 60, 60, GFX_COLOR_GRAY);
            gfx_text((uint16_t)(20 + i * 70), 408, nm[i], GFX_COLOR_GRAY, GFX_COLOR_BLACK, 1);
        }
    }
    lcd_sim_write_ppm("out/00_smoke.ppm");
    printf("\nescrito out/00_smoke.ppm\n");
    return 0;
}

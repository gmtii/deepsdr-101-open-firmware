/*
 * Prueba de la banda superior reservada (gfx_guard_top_set).
 *
 * Compila el gfx.c REAL del firmware contra el panel simulado y cuenta
 * accesos al bus. No comprueba que el codigo "parezca" correcto: comprueba
 * que con el guard armado NINGUN pixel de la banda llega al panel.
 */
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "lcd_sim.h"

static int fallos = 0;

static void chk(const char *que, unsigned long got, unsigned long want)
{
    int ok = (got == want);
    if (!ok) { fallos++; }
    printf("  %-58s %8lu (esperado %8lu)  %s\n", que, got, want, ok ? "OK" : "FALLO");
}

/* pixeles escritos por el panel simulado desde el ultimo reset. Se usa
 * .pixels y no .bus_writes porque este ultimo incluye los 17 accesos de
 * abrir ventana, que no son contenido. */
static unsigned long pixels(void)
{
    lcd_cost_t c = lcd_sim_cost();
    return (unsigned long)c.pixels;
}

#define GUARD 104

int main(void)
{
    unsigned long n;

    lcd_sim_reset(0x0000);
    printf("Banda reservada: y = 0..%d\n\n", GUARD - 1);

    /* 1. Sin guard, todo pasa. */
    gfx_guard_top_set(0);
    lcd_sim_cost_reset();
    gfx_fill_rect(0, 0, 800, 64, 0x1234);
    n = pixels();
    chk("sin guard: fill_rect(0,0,800,64)", n, 800UL * 64UL);

    /* 2. Con guard, un relleno entero dentro de la banda no toca el bus. */
    gfx_guard_top_set(GUARD);
    lcd_sim_cost_reset();
    gfx_fill_rect(0, 0, 800, 64, 0x1234);
    chk("guard: fill_rect entero en la banda", pixels(), 0UL);

    /* 3. El caso real: el rectangulo cian del boton AGC (BADGE_COL(3), BADGE_Y0). */
    lcd_sim_cost_reset();
    gfx_fill_rect(345, 74, 62, 22, 0x07FF);
    chk("guard: el recuadro cian del boton AGC", pixels(), 0UL);

    /* 4. El panel plano de la cabecera y el de la barra de estado. */
    lcd_sim_cost_reset();
    gfx_fill_rect(0, 0, 800, 64, 0x4208);
    gfx_fill_rect(0, 64, 800, 40, 0x0000);
    chk("guard: los dos paneles planos de arriba", pixels(), 0UL);

    /* 5. Un dibujo que CRUZA la frontera se recorta, no se descarta. */
    lcd_sim_cost_reset();
    gfx_fill_rect(0, 100, 800, 20, 0x1234);   /* 4 filas dentro, 16 fuera */
    chk("guard: fill_rect a caballo (y=100,h=20)", pixels(), 800UL * 16UL);

    /* 6. Todo lo demas pasa por fill_rect: texto, lineas, bordes, marco. */
    lcd_sim_cost_reset();
    gfx_text(10, 8, "AGC MED", 0xFFFF, 0x0000, 2);
    chk("guard: gfx_text en la banda", pixels(), 0UL);

    lcd_sim_cost_reset();
    gfx_rect(0, 64, 800, 40, 0xFFFF);
    chk("guard: gfx_rect en la banda", pixels(), 0UL);

    lcd_sim_cost_reset();
    gfx_hline(0, 30, 800, 0xFFFF);
    gfx_vline(400, 0, 64, 0xFFFF);
    gfx_line(0, 10, 799, 10, 0xFFFF);
    gfx_pixel(5, 5, 0xFFFF);
    chk("guard: hline/vline/line/pixel en la banda", pixels(), 0UL);

    /* 7. gfx_blit salta las filas recortadas, no las dibuja desplazadas. */
    {
        static uint16_t buf[800 * 20];
        unsigned i;
        for (i = 0; i < 800 * 20; i++) { buf[i] = (uint16_t)(i / 800); }
        lcd_sim_cost_reset();
        gfx_blit(0, 94, 800, 20, buf);   /* 10 filas dentro, 10 fuera */
        chk("guard: gfx_blit a caballo (y=94,h=20)", pixels(), 800UL * 10UL);
        /* la primera fila visible tiene que ser la 10 del buffer, no la 0 */
        chk("guard: gfx_blit no desplaza el contenido",
            (unsigned long)lcd_sim_fb()[(size_t)GUARD * LCD_W], 10UL);
    }

    /* 8. fill_screen respeta la banda con el guard armado, y la borra sin el. */
    lcd_sim_cost_reset();
    gfx_fill_screen(0x0000);
    chk("guard: fill_screen respeta la banda", pixels(), 800UL * (480UL - GUARD));

    gfx_guard_top_set(0);
    lcd_sim_cost_reset();
    gfx_fill_screen(0x0000);
    chk("sin guard: fill_screen borra el panel entero", pixels(), 800UL * 480UL);

    /* 9. Lo que esta JUSTO en la frontera (y = GUARD) si se dibuja: el
     *    espectro empieza ahi y no puede perder su primera fila. */
    gfx_guard_top_set(GUARD);
    lcd_sim_cost_reset();
    gfx_hline(0, GUARD, 800, 0xFFFF);
    chk("guard: y = GUARD exacto SI se dibuja", pixels(), 800UL);

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

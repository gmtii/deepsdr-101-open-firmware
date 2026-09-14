#include "splash_screen.h"
#include "gfx.h"

/*
 * Splash screen - 3 líneas de texto centradas + número de versión
 * abajo. Autocontenida: solo usa gfx_*() (ya declaradas en gfx.h) y
 * g_msticks (ya declarado extern en varios .c del proyecto, mismo
 * patrón).
 *
 * DÓNDE LLAMARLA: una vez, al principio de main(), justo después de
 * que la pantalla esté inicializada (después de rm68120_exmc_init() o
 * como se llame en tu secuencia de arranque) y ANTES de
 * radio_screen_draw(). Por ejemplo:
 *
 *   ... init de la pantalla ...
 *   splash_screen_draw();
 *   ... resto del arranque (I2C, códec, MS5351, etc.) ...
 *   radio_screen_draw();
 *
 * IMPORTANTE - LA FUENTE SOLO TIENE MAYÚSCULAS: gfx_font5x7 cubre
 * 0x20-0x5A (espacio, dígitos, MAYÚSCULAS, puntuación básica) - no
 * hay glifos en minúscula, y cualquier carácter fuera de ese rango se
 * pinta como un espacio en blanco (silencioso, sin aviso). Si cambias
 * los textos de abajo, escríbelos en MAYÚSCULAS.
 */
#define SPLASH_HOLD_MS 2500U /* cuánto tiempo se queda en pantalla, en ms */

void splash_screen_draw(void)
{
    extern volatile uint32_t g_msticks;
    uint32_t t0;

    /* Personaliza estas 3 líneas y la versión - todo en MAYÚSCULAS. */
    static const char *line1 = "OSS GD32F450 SDR";
    static const char *line2 = "WFM / NFM / AM / SSB";
    static const char *line3 = "EA8DGL ESTEBAN - UA6YKK ALEXANDR - EA7GIB BLAS";
    static const char *line4 = "CREATIVE COMMONS NC";
    static const char *version = "V0.4 18/8/2026 ";

    const uint8_t scale1 = 4; /* linea 1, grande */
    const uint8_t scale2 = 2; /* lineas 2-3, mas pequenas */
    const uint8_t scale_v = 2; /* version, abajo */

    uint16_t w1 = gfx_text_width(line1, scale1);
    uint16_t w2 = gfx_text_width(line2, scale2);
    uint16_t w3 = gfx_text_width(line3, scale2);
    uint16_t w4 = gfx_text_width(line4, scale2);
    uint16_t wv = gfx_text_width(version, scale_v);

    gfx_fill_screen(GFX_COLOR_BLACK);

    /* Bloque de texto centrado verticalmente a ojo (no hace falta
     * exactitud aqui) - ajusta las 3 "y" si quieres subirlo o
     * bajarlo. */
    gfx_text((uint16_t)((GFX_SCREEN_WIDTH - w1) / 2), 160, line1,
              GFX_COLOR_YELLOW, GFX_COLOR_BLACK, scale1);
    gfx_text((uint16_t)((GFX_SCREEN_WIDTH - w2) / 2), 220, line2,
              GFX_COLOR_WHITE, GFX_COLOR_BLACK, scale2);
    gfx_text((uint16_t)((GFX_SCREEN_WIDTH - w3) / 2), 250, line3,
              GFX_COLOR_GRAY, GFX_COLOR_BLACK, scale2);
    gfx_text((uint16_t)((GFX_SCREEN_WIDTH - w4) / 2), 280, line4,
              GFX_COLOR_GRAY, GFX_COLOR_BLACK, scale2);

    /* Version, pegada abajo del todo. */
    gfx_text((uint16_t)((GFX_SCREEN_WIDTH - wv) / 2),
              (uint16_t)(GFX_SCREEN_HEIGHT - 30), version,
              GFX_COLOR_GRAY, GFX_COLOR_BLACK, scale_v);

    /* Retardo bloqueante simple con g_msticks (ya lo actualiza
     * SysTick_Handler cada 1ms) - si prefieres que NO bloquee el
     * arranque, quita este bucle y llama a splash_screen_draw() sin
     * mas, dejando que la pantalla se sobreescriba sola cuando
     * radio_screen_draw() se ejecute despues. */
    t0 = g_msticks;
    while ((g_msticks - t0) < SPLASH_HOLD_MS) {
        /* espera */
    }
}

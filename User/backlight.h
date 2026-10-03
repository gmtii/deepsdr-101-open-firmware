#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdint.h>

/*
 * LCD backlight brightness via hardware PWM on PA3 - 31/07/2026, per
 * the project owner's request. Before this, the backlight had no
 * firmware control at all; whatever comes up from the bootloader/
 * hardware default is what you got.
 *
 * PIN/TIMER: PA3 = TIMER1_CH3, alternate function AF1. NOT
 * empirically confirmed on THIS board (unlike PC6/TIMER2_CH0's MCLK,
 * which was scope-verified - see gd32_i2s.c's comment above
 * gd32_i2s_mclk_timer_start()) - it follows the same GD32F4xx<->
 * STM32F4 pin-compatible mapping this project already relies on
 * there (GD32 TIMERn = STM32 TIMn+1, same peripheral memory-map
 * offsets, same AF numbers - TIMER1's base offset is literally 0
 * within the APB1 timer block, exactly where STM32 TIM2 sits), where
 * PA3/AF1 is TIM2_CH4 (= GD32 TIMER1_CH3). VERIFY before trusting
 * this blind: watch the backlight actually respond to
 * backlight_set_percent(), or better, a scope on PA3.
 *
 * POLARITY: CONFIRMED ACTIVE-LOW on hardware, 31/07/2026 (this board's
 * backlight driver turns the LEDs ON when the pin is LOW) - configured
 * with TIMER_OC_POLARITY_LOW in backlight_init() (backlight.c) to
 * match, so backlight_set_percent()'s "0=darkest, 100=brightest"
 * contract holds as documented below. Before this was confirmed, the
 * driver was wired active-high by (wrong) default assumption, which
 * inverted the whole scale - if you ever swap to different backlight
 * hardware and brightness comes out backwards again, that single
 * ocpolarity line is the fix, nothing else in this file needs to
 * change.
 *
 * PWM FREQUENCY: 20kHz - comfortably above the ~200Hz flicker-
 * perception threshold for LCD/LED backlights (no visible flicker,
 * not even out of the corner of the eye) and high enough to stay
 * inaudible if any switching noise ever coupled into nearby analog
 * audio circuitry - not asserting this board has that problem, just
 * why 20kHz was picked over a lower, arithmetically "rounder"
 * frequency.
 */
void backlight_init(void);

/* Requested brightness, 0 = darkest, 100 = brightest - but 0 does NOT
 * mean fully off: backlight.c enforces a BACKLIGHT_MIN_PERCENT floor
 * (currently 10%) underneath everything, per the project owner's
 * explicit request that the panel never go fully black. Values above
 * 100 clamp to 100; values below the floor clamp UP to the floor, not
 * down to 0. Only touches the timer's compare register - shadow-
 * latched at the next update event, same hardware behavior
 * gd32_i2s_mclk_timer_start() already relies on for its own PWM - so
 * it's safe to call this as often as you like, e.g. live off an
 * encoder while adjusting brightness. */
void backlight_set_percent(uint8_t percent);

/* Query the last percent ACTUALLY APPLIED (i.e. post floor-clamping -
 * see backlight_set_percent()'s comment) - for UI readouts. */
uint8_t backlight_get_percent(void);

/*
 * Apagar y encender la retroiluminacion para el modo de pantalla dormida.
 *
 * ESTABAN SIN DECLARAR: main.c las llamaba (screen_sleep/screen_wake) y
 * backlight.c las define, pero no aparecian en esta cabecera, asi que el
 * compilador las daba por implicitas - suponiendo "int f()" y sin comprobar
 * ni argumentos ni tipo de retorno. Funcionaba por casualidad, porque ambas
 * son void f(void) y se llaman sin argumentos. El dia que una de las dos
 * tomara un parametro o devolviera algo, habria roto en silencio.
 */
void backlight_sleep(void);
void backlight_wake(void);

#endif /* BACKLIGHT_H */

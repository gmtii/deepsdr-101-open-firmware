#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdint.h>

/*
 * EL BRILLO DE LA PANTALLA, POR PWM EN PA3 - 31/07/2026, pedido por el
 * usuario. Antes de esto el firmware no tocaba la luz de fondo: te
 * quedabas con lo que dejara puesto el cargador o el arranque.
 *
 * PATILLA Y TEMPORIZADOR: PA3 = TIMER1_CH3, funcion alternativa AF1. NO
 * esta comprobado a mano en ESTA placa, al contrario que el MCLK de
 * PC6/TIMER2_CH0, que si se vio con el osciloscopio -ver el comentario de
 * gd32_i2s_mclk_timer_start() en gd32_i2s.c-. Sale de la misma
 * equivalencia GD32F4xx <-> STM32F4 en la que este proyecto ya se apoya
 * alli: TIMERn del GD32 = TIMn+1 del STM32, los mismos desplazamientos en
 * el mapa de periféricos y los mismos numeros de AF -el desplazamiento
 * base de TIMER1 es literalmente 0 dentro del bloque de temporizadores de
 * APB1, justo donde esta el TIM2 del STM32-, y alli PA3/AF1 es TIM2_CH4,
 * o sea TIMER1_CH3. COMPRUEBALO antes de fiarte a ciegas: mira que la luz
 * responda de verdad a backlight_set_percent(), o mejor, pon el
 * osciloscopio en PA3.
 *
 * POLARIDAD: CONFIRMADA ACTIVA A NIVEL BAJO sobre la placa, el
 * 31/07/2026. El excitador de la luz de esta placa enciende los LED
 * cuando la patilla esta BAJA, asi que backlight_init() en backlight.c lo
 * configura con TIMER_OC_POLARITY_LOW y el contrato de
 * backlight_set_percent() -0 lo mas oscuro, 100 lo mas claro- se cumple
 * tal y como se documenta aqui abajo. Antes de comprobarlo, el excitador
 * estaba puesto activo a nivel alto por suposicion, que era la
 * equivocada, y eso daba la vuelta a la escala entera. Si algun dia se
 * cambia el hardware de la luz y el brillo vuelve a salir del reves, esa
 * unica linea de la polaridad es el arreglo; no hay que tocar nada mas de
 * este fichero.
 *
 * FRECUENCIA DEL PWM: 20 kHz. Queda bien por encima del umbral de unos
 * 200 Hz a partir del cual se percibe el parpadeo en luces de LCD y LED
 * -no parpadea, ni mirando de reojo- y lo bastante alta para no oirse si
 * algun dia el ruido de conmutacion se colara en la parte analogica del
 * audio. No se esta diciendo que esta placa tenga ese problema; se esta
 * diciendo por que se eligieron 20 kHz y no una frecuencia mas baja y
 * aritmeticamente mas redonda.
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

#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/*
 * Rotary encoder driver (quadrature + push button).
 *
 * Pins (confirmed by Jorge on real hardware, 30/07/2026):
 *   PD13 - channel A
 *   PD12 - channel B
 *   PC9  - push button, ACTIVE HIGH (reads Vcc when pressed, 0V at
 *          rest - note the opposite polarity to the A/B channels)
 * A/B are inputs with internal pull-ups (the encoder switches them
 * to ground); the button is an input with internal pull-down (the
 * button switches it to Vcc).
 *
 * Decoding model: encoder_tick() must be called at 1kHz (from
 * SysTick_Handler). Each tick samples A/B and runs them through a
 * full-quadrature transition table - every VALID Gray-code transition
 * adds +/-1 quarter-step, every invalid one (bounce, skipped state)
 * adds 0. This makes the decoder inherently debounced without any
 * timing heuristics: contact bounce produces back-and-forth valid
 * transitions that cancel out, and electrical glitches land on
 * invalid transitions that are simply ignored. A detented encoder
 * produces 4 quarter-steps per detent; encoder_take_delta() divides
 * accordingly and carries the remainder, so slow "between detents"
 * wiggling never loses or invents counts.
 *
 * 1kHz sampling bounds the maximum rate at ~250 detents/s before
 * quarter-steps start aliasing - a hand on a knob tops out around
 * 30-50 detents/s, so there is an ample margin.
 *
 * The button is debounced with a 20ms integrator. A press is
 * classified SHORT or LONG on RELEASE (not on the initial press edge)
 * by how long it was held, and reported as one of two discrete event
 * types accordingly - see encoder_take_press()/encoder_take_long_press().
 * Deciding on release rather than at press-down is deliberate: this
 * project uses a short press for different things depending on
 * encoder_target (cycle the tune step, toggle SCALE's LO/HI bound,
 * ...), so firing that side effect immediately on press-down would
 * mean every long-press-to-exit gesture (see main.c's
 * tune_encoder_poll()) also fires whatever the short press meant on
 * the way down, before there was any way to know the hold would turn
 * out to be long. Waiting for release adds at most one debounce
 * window (20ms) of latency to a normal click - imperceptible.
 *
 * Concurrency: encoder_tick() runs in interrupt context, the
 * encoder_take_*() functions in the main loop. The take functions
 * mask interrupts for the few cycles of their read-and-clear - both
 * are safe to call from anywhere below SysTick priority.
 */

/* Call once at startup: configures the three GPIOs and captures the
 * initial A/B state. */
void encoder_init(void);

/* Call at 1kHz from SysTick_Handler. Cheap: three GPIO reads and a
 * table lookup. */
void encoder_tick(void);

/* Returns the number of whole detents turned since the last call.
 * Sign convention: this project's ENCODER_DIRECTION (encoder.c) is
 * currently -1, confirmed on real hardware 31/07/2026 (A leading B is
 * counter-clockwise on this knob, not clockwise as first assumed) -
 * so this function's raw quadrature reading is already flipped before
 * it gets here; positive means the knob was turned the way this board
 * expects "increase" to feel. Sub-detent quarter-steps are carried
 * over, not lost. */
int32_t encoder_take_delta(void);

/*
 * Nivel del boton ya antirrebotado: 1 mientras esta apretado.
 *
 * Para gestos de "apretar y girar a la vez". Las tres funciones
 * encoder_take_*() reportan EVENTOS al soltar, que es lo correcto para un
 * clic pero no sirve para saber si el boton esta apretado AHORA, que es lo
 * que necesita un gesto combinado.
 */
uint8_t encoder_button_down(void);

/*
 * Marca la pulsacion en curso como ya usada: al soltar no se reportara ni
 * corta ni larga.
 *
 * Se llama en cuanto un gesto combinado se da por iniciado. Sin esto, el
 * gesto "apretar y girar" acabaria disparando ademas la accion de la
 * pulsacion corta al levantar el dedo.
 */
void encoder_consume_press(void);

/* Returns 1 exactly once per debounced SHORT button press (held less
 * than the long-press threshold - see encoder.c's BTN_LONG_PRESS_MS). */
uint8_t encoder_take_press(void);

/* Returns 1 exactly once per debounced LONG button press (held at
 * least BTN_LONG_PRESS_MS before release). Mutually exclusive with
 * encoder_take_press() for a given physical press - a single press is
 * reported as EITHER one or the other, never both, never neither. */
uint8_t encoder_take_long_press(void);

#endif /* ENCODER_H */

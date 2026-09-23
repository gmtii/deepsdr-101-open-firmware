#include "encoder.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

#define ENC_A_PORT   GPIOD
#define ENC_A_PIN    GPIO_PIN_13
#define ENC_B_PORT   GPIOD
#define ENC_B_PIN    GPIO_PIN_12
#define ENC_BTN_PORT GPIOC
#define ENC_BTN_PIN  GPIO_PIN_9

/* Confirmed inverted on real hardware, 31/07/2026: tuning went
 * backwards with ENCODER_DIRECTION=1 (i.e. A leading B is actually
 * counter-clockwise on this knob, opposite of what was assumed when
 * this flag was first added - see encoder.h's encoder_take_delta()
 * comment). Flipped here rather than swapping the A/B wires. */
#define ENCODER_DIRECTION -1

/* Quarter-steps per detent of the mechanical encoder. The common
 * EC11-style part gives one full quadrature cycle (4 transitions) per
 * detent; if this encoder turns out to be a 2-per-detent type, set
 * this to 2. */
#define QUARTER_STEPS_PER_DETENT 4

#define BTN_DEBOUNCE_MS 20U

/* Held this long (or more) before release = LONG press instead of
 * SHORT - see encoder.h's comment on why classification happens on
 * release. 600ms comfortably clears an intentional-but-quick click
 * (even a deliberately slow one) while still feeling immediate for a
 * "hold to exit" gesture. */
#define BTN_LONG_PRESS_MS 600U

/*
 * Full-quadrature transition table, indexed by (prev_state << 2) |
 * new_state, where state = (A << 1) | B. Valid Gray transitions map
 * to +/-1, everything else (no change, or an "impossible" two-bit
 * jump from noise) maps to 0.
 */
static const int8_t k_qdec[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static volatile int32_t s_qsteps    = 0; /* accumulated quarter-steps */
static volatile uint8_t s_presses   = 0; /* pending debounced SHORT presses */
static volatile uint8_t s_long_presses = 0; /* pending debounced LONG presses */
static uint8_t          s_ab_prev   = 0; /* last sampled (A<<1)|B     */
static uint8_t          s_btn_integ = 0; /* debounce integrator, ms   */
static uint8_t          s_btn_state = 0; /* debounced level, 0 = idle (button is active HIGH) */
static uint16_t         s_btn_held_ms = 0; /* ms held since the current debounced press started; only meaningful while s_btn_state==1 */
/*
 * 1 = la pulsacion que hay en curso YA se ha usado para otra cosa (girar
 * el mando con el boton apretado), asi que al soltar no se reporta ni
 * pulsacion corta ni larga.
 *
 * Por que hace falta: el gesto "aprieta y gira" del firmware original
 * termina siempre en una suelta, y esa suelta, sin esto, dispararia ademas
 * lo que signifique la pulsacion corta - cambiar el paso de sintonia, que
 * es justo lo que el gesto acaba de estar ajustando. Se marca desde el
 * bucle principal (encoder_consume_press) en cuanto llega el primer detente
 * con el boton apretado, y se limpia sola en el siguiente flanco de
 * pulsacion.
 */
static volatile uint8_t s_btn_consumed = 0;

static uint8_t ab_read(void)
{
    uint8_t a = (gpio_input_bit_get(ENC_A_PORT, ENC_A_PIN) == SET) ? 1U : 0U;
    uint8_t b = (gpio_input_bit_get(ENC_B_PORT, ENC_B_PIN) == SET) ? 1U : 0U;
    return (uint8_t)((a << 1) | b);
}

void encoder_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);

    gpio_mode_set(ENC_A_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, ENC_A_PIN);
    gpio_mode_set(ENC_B_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, ENC_B_PIN);
    /* Button drives Vcc when pressed and reads 0V at rest: internal
     * pull-down keeps the line firmly low while idle. */
    gpio_mode_set(ENC_BTN_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, ENC_BTN_PIN);

    s_ab_prev = ab_read();
    s_btn_state = (gpio_input_bit_get(ENC_BTN_PORT, ENC_BTN_PIN) == SET) ? 1U : 0U;
    s_btn_integ = 0;

    debug_print("encoder: PD13(A)/PD12(B)/PC0(btn) configured\n");
    debug_print_dec("encoder: initial AB state", s_ab_prev);
    debug_print_dec("encoder: initial button level (0=idle)", s_btn_state);
}

void encoder_tick(void)
{
    uint8_t ab;
    uint8_t btn_raw;

    /* --- quadrature --- */
    ab = ab_read();
    if (ab != s_ab_prev) {
        s_qsteps += k_qdec[(uint8_t)((s_ab_prev << 2) | ab)];
        s_ab_prev = ab;
    }

    /* --- button, integrating debounce ---
     * The integrator ramps toward the raw level and the debounced
     * state only flips once it saturates: a press must hold LOW for
     * BTN_DEBOUNCE_MS consecutive ms to register, and likewise for
     * release. Bounce shorter than that just wiggles the counter.
     *
     * Classification (SHORT vs LONG) happens on the RELEASE edge, not
     * the press edge - see encoder.h's comment for why. While held,
     * s_btn_held_ms just counts ms since the debounced press started
     * (saturating rather than wrapping on an implausibly long hold);
     * on the debounced release it's compared against
     * BTN_LONG_PRESS_MS to decide which counter to bump. */
    btn_raw = (gpio_input_bit_get(ENC_BTN_PORT, ENC_BTN_PIN) == SET) ? 1U : 0U;
    if (btn_raw == s_btn_state) {
        s_btn_integ = 0;
        if (s_btn_state == 1U && s_btn_held_ms < 0xFFFFU) {
            s_btn_held_ms++;
        }
    } else {
        s_btn_integ++;
        if (s_btn_integ >= BTN_DEBOUNCE_MS) {
            s_btn_state = btn_raw;
            s_btn_integ = 0;
            if (s_btn_state == 1U) { /* active high: 0->1 = press started */
                s_btn_held_ms = 0;
                s_btn_consumed = 0U; /* pulsacion nueva, aun sin usar */
            } else { /* 1->0 = released - classify now that the hold time is known */
                if (s_btn_consumed) {
                    /* se gasto girando: ni corta ni larga - ver
                     * s_btn_consumed */
                } else if (s_btn_held_ms >= BTN_LONG_PRESS_MS) {
                    s_long_presses++;
                } else {
                    s_presses++;
                }
            }
        }
    }
}

uint8_t encoder_button_down(void)
{
    return s_btn_state;
}

void encoder_consume_press(void)
{
    s_btn_consumed = 1U;
}

int32_t encoder_take_delta(void)
{
    int32_t q;
    int32_t detents;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    q = s_qsteps;
    /* Keep the sub-detent remainder for next time; hand out only
     * whole detents. C99 division truncates toward zero, which is
     * exactly what we want for both signs here. */
    detents  = q / QUARTER_STEPS_PER_DETENT;
    s_qsteps = q % QUARTER_STEPS_PER_DETENT;
    __set_PRIMASK(primask);

    return detents * ENCODER_DIRECTION;
}

uint8_t encoder_take_press(void)
{
    uint8_t p = 0;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_presses > 0U) {
        s_presses--;
        p = 1;
    }
    __set_PRIMASK(primask);

    return p;
}

uint8_t encoder_take_long_press(void)
{
    uint8_t p = 0;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_long_presses > 0U) {
        s_long_presses--;
        p = 1;
    }
    __set_PRIMASK(primask);

    return p;
}

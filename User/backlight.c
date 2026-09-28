#include "backlight.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

/*
 * TIMER1CLK on this project's clock tree: APB1_PSC=4 (!=1), so - same
 * x2 doubling rule gd32_i2s_mclk_timer_start()'s comment already
 * spells out for TIMER2 on this same APB1 bus - TIMER1CLK = 2*PCLK1 =
 * 2*49.92MHz = 99.84MHz.
 *
 * PSC=0, period 4991 (4992 counts): 99.84MHz / 4992 = 20.0kHz exact -
 * see backlight.h's PWM FREQUENCY note. A clean, exact division, not
 * a rounded approximation (same standard this project already holds
 * itself to for TIMER2's MCLK).
 */
#define BACKLIGHT_PWM_PERIOD 4991U /* ARR; total counts = PERIOD+1 = 4992 */

/* Never let the panel go fully black, even if something asks for
 * percent=0 (an encoder run to the floor, a bug elsewhere, etc.) -
 * per the project owner's explicit request, 31/07/2026. Picked as "a
 * dim but still legible glow in a dark room", not calibrated against
 * an actual lux measurement - adjust if it's still too dim/too bright
 * for real use. Enforced centrally in backlight_set_percent() below,
 * so every caller (the encoder, any future UI control, and
 * backlight_init()'s own boot default) gets the floor for free with
 * no risk of forgetting it somewhere. */
#define BACKLIGHT_MIN_PERCENT 10U

/*
 * BRILLO AL ARRANCAR. 50 -> 80 el 25/09/2026.
 *
 * *** Por el dueno del proyecto: "revisa el brillo, y mira si se puede
 * aumentar mas". ***
 *
 * Este numero solo manda cuando el CONFIG.CSV no trae brillo guardado - o
 * sea, la primera vez, o despues de borrarlo. Y el dueno acababa de
 * borrarlo: "he borrado el config.csv y he dejao que lo cree el". Asi que
 * la pantalla se quedo al 50% sin que nadie lo tocara, igual que el NR se
 * quedo en 50 de 4.095. Dos mordiscos del mismo borrado.
 *
 * POR QUE ESTABA EN 50. El 31/07/2026 se bajo porque "al 100% la pantalla
 * se quedaba NEGRA y volvia al 50%". Eso era verdad, pero la causa no era
 * la que quedo escrita aqui. Ese mismo dia el driver estaba cableado como
 * ACTIVO EN ALTO por una suposicion equivocada, y con esa polaridad el
 * 100% deja el pin permanentemente ALTO... que en este panel, que es
 * ACTIVO EN BAJO, es exactamente apagar la luz. El 100% no fallaba: hacia
 * lo que se le pedia, al reves.
 *
 * La polaridad se arreglo el mismo dia. El 50 y la teoria de que el driver
 * necesita flancos se quedaron aqui sin volver a probarse, y han tenido la
 * pantalla a medio gas casi dos meses.
 *
 * SE DEJA EN 80 Y NO EN 100 a proposito: es de fabrica, no la preferencia
 * de nadie. Bien clara sin ser un numero que nunca se ha probado como
 * arranque. El 100 esta a mano con el encoder y se guarda solo.
 */
static uint8_t s_backlight_percent = 80U;

void backlight_init(void)
{
    timer_parameter_struct timer_init_struct;
    timer_oc_parameter_struct oc_init_struct;

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_TIMER1);

    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_3);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_3);
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_3);

    timer_deinit(TIMER1);

    timer_struct_para_init(&timer_init_struct);
    timer_init_struct.prescaler         = 0U;
    timer_init_struct.alignedmode       = TIMER_COUNTER_EDGE;
    timer_init_struct.counterdirection  = TIMER_COUNTER_UP;
    timer_init_struct.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init_struct.period            = BACKLIGHT_PWM_PERIOD;
    timer_init_struct.repetitioncounter = 0U;
    gd32_timer_init(TIMER1, &timer_init_struct);

    timer_channel_output_struct_para_init(&oc_init_struct);
    oc_init_struct.outputstate  = TIMER_CCX_ENABLE;
    /* CONFIRMED ON HARDWARE 31/07/2026 (was a documented guess before):
     * the backlight driver is ACTIVE-LOW (pin LOW = LEDs on) - with
     * TIMER_OC_POLARITY_HIGH, percent=0 (CCR=0, pin sits LOW the whole
     * period) came out at MAXIMUM brightness and percent=100 (CCR=
     * ARR+1, pin sits HIGH the whole period) came out as TOTAL
     * DARKNESS - backwards from backlight_set_percent()'s documented
     * "0=off, 100=full" contract. TIMER_OC_POLARITY_LOW inverts the
     * pin's electrical state relative to the internal OC compare
     * result, which restores the intended mapping (0=darkest,
     * 100=brightest) without touching any of the CCR math below - see
     * backlight.h's POLARITY note. */
    oc_init_struct.ocpolarity   = TIMER_OC_POLARITY_LOW;
    timer_channel_output_config(TIMER1, TIMER_CH_3, &oc_init_struct);

    timer_channel_output_mode_config(TIMER1, TIMER_CH_3, TIMER_OC_MODE_PWM0);
    /* Start at some safe, valid CCR before the timer is even enabled -
     * the real boot-default value gets applied right after
     * timer_enable() below, via backlight_set_percent() itself, so
     * the CCR formula and the BACKLIGHT_MIN_PERCENT floor only live in
     * ONE place. */
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, 0U);

    timer_auto_reload_shadow_enable(TIMER1);
    timer_enable(TIMER1);

    backlight_set_percent(s_backlight_percent); /* apply the real boot default */

    debug_print("backlight: PWM started on PA3 (TIMER1_CH3, AF1), 20kHz - "
                "verify the panel actually responds before relying on this\n");
}

void backlight_set_percent(uint8_t percent)
{
    uint32_t ccr;

    if (percent > 100U) {
        percent = 100U;
    }
    if (percent < BACKLIGHT_MIN_PERCENT) {
        percent = BACKLIGHT_MIN_PERCENT;
    }
    s_backlight_percent = percent;

    /*
     * EL 100% YA NO PARA EL PIN. 25/09/2026.
     *
     * La cuenta da CCR=4992 para el 100%, un paso MAS ALLA del ARR de
     * 4991. En modo PWM0 eso deja la salida permanentemente activa: el pin
     * se queda quieto y no da un solo flanco en todo el periodo.
     *
     * Eso es corriente continua, no PWM. Y aunque la pantalla negra del
     * 31/07/2026 tuvo otra causa -la polaridad al reves, ver el comentario
     * del valor de arranque- hay drivers de retroiluminacion que usan los
     * propios flancos para su conmutacion interna y se apagan si les llega
     * un nivel fijo. Si este es uno de ellos, no se sabe, porque nunca se
     * ha probado el 100% con la polaridad correcta.
     *
     * Asi que se limita el CCR al ARR. El pin sigue conmutando a 20 kHz,
     * con un hueco de UN paso de 4.992: el 99,98% del tiempo encendido.
     * Cuesta dos centesimas de por ciento de luz -invisible, y el ojo no
     * distingue eso ni de lejos- y a cambio el fallo deja de ser posible
     * en vez de ser improbable.
     *
     * Es la diferencia entre "seguramente va bien" y "no puede ir mal",
     * por dos centesimas.
     */
    ccr = ((uint32_t)(BACKLIGHT_PWM_PERIOD + 1U) * percent) / 100U;
    if (ccr > BACKLIGHT_PWM_PERIOD) { ccr = BACKLIGHT_PWM_PERIOD; }
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, ccr);
}

uint8_t backlight_get_percent(void)
{
    return s_backlight_percent;
}

/*
 * See backlight.h's comment. Straight CCR=0 (same value
 * backlight_set_percent()'s formula would give for percent=0, minus
 * the floor clamp) - TIMER_OC_POLARITY_LOW is already configured in
 * backlight_init(), so this reproduces exactly the "pin LOW the whole
 * period" case backlight_init()'s comment confirms is REAL darkness on
 * this hardware, not the floor-enforced dim glow. s_backlight_percent
 * is deliberately left untouched here - it still reflects the
 * brightness the person actually chose, for backlight_wake() to
 * restore and for any UI readout in the meantime.
 */
void backlight_sleep(void)
{
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, 0U);
    debug_print("backlight: sleep (forced off, bypassing the floor)\n");
}

void backlight_wake(void)
{
    backlight_set_percent(s_backlight_percent); /* restores the CCR for the percent that was already stored - a harmless no-op if never put to sleep */
    debug_print("backlight: wake\n");
}

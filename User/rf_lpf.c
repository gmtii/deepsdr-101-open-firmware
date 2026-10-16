#include "rf_lpf.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

#define LPF_PORT GPIOA
#define LPF_PA1  GPIO_PIN_1
#define LPF_PA2  GPIO_PIN_2
#define LPF_PA5  GPIO_PIN_5
#define LPF_ALL  (LPF_PA1 | LPF_PA2 | LPF_PA5)

/* One filter range: upper frequency bound (inclusive) and the level
 * for each control line. */
typedef struct {
    uint32_t max_hz;
    uint8_t  pa1;
    uint8_t  pa2;
    uint8_t  pa5;
} lpf_range_t;

static const lpf_range_t ranges[4] = {
    {  36999999UL, 0U, 1U, 0U },  /* range 1:   0 -  36 MHz -> 010 */
    {  60999999UL, 1U, 1U, 0U },  /* range 2:  37 -  60 MHz -> 110 */
    { 120999999UL, 0U, 0U, 0U },  /* range 3:  61 - 120 MHz -> 000 */
    { 180000000UL, 1U, 0U, 0U },  /* range 4: 121 - 180 MHz -> 100 */
};

static uint8_t s_current = 0U; /* 0 = not yet applied, else 1..4 */

static void apply(uint8_t idx)
{
    const lpf_range_t *r = &ranges[idx];

    gpio_bit_write(LPF_PORT, LPF_PA1, r->pa1 ? SET : RESET);
    gpio_bit_write(LPF_PORT, LPF_PA2, r->pa2 ? SET : RESET);
    gpio_bit_write(LPF_PORT, LPF_PA5, r->pa5 ? SET : RESET);
}

void rf_lpf_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);

    gpio_mode_set(LPF_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LPF_ALL);
    gpio_output_options_set(LPF_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LPF_ALL);

    /* Safe default until the first tune: lowest range. */
    apply(0U);
    s_current = 1U;

    debug_print("rf_lpf: PA1/PA2/PA5 configured, range 1 (0-36MHz) as default\n");
}

uint8_t rf_lpf_select(uint32_t freq_hz)
{
    uint8_t idx;

    for (idx = 0U; idx < 3U; idx++) {
        if (freq_hz <= ranges[idx].max_hz) {
            break;
        }
    }
    /* idx == 3 here means "above range 3", i.e. range 4 - which also
     * absorbs anything above its own 180MHz bound (clamped). */
    if (freq_hz > ranges[3].max_hz) {
        debug_print_dec("rf_lpf: above 180MHz, clamping to range 4, Hz", freq_hz);
    }

    if ((uint8_t)(idx + 1U) != s_current) {
        apply(idx);
        s_current = (uint8_t)(idx + 1U);
        debug_print_dec("rf_lpf: switched to range", s_current);
    }

    return s_current;
}

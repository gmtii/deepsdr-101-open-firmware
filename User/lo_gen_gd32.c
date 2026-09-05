#include "lo_gen_gd32.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

/*
 * TIMER2CLK derivation - same APB1 bus/doubling rule as
 * gd32_i2s_mclk_timer_start()'s own TIMER2CLK comment in gd32_i2s.c
 * (same peripheral, in fact - see lo_gen_gd32.h's PIN MAPPING comment
 * for why this module took TIMER2 back over from that function):
 * SYSCLK=199.68MHz, APB1_PSC=4 -> PCLK1=49.92MHz -> TIMER2CLK=2xPCLK1
 * =99.84MHz, since the APB1 prescaler isn't 1.
 */
#define LO_GEN_TIMER_CLK_HZ 99840000UL

/* See lo_gen_gd32.h's big comment: toggle-mode quadrature makes the
 * OUTPUT period equal to TWO counter periods, so the achievable
 * output frequency is TIMERCLK/(PSC+1)/(ARR+1)/2. PSC is always 0
 * within this module's intended <300kHz scope - even at the lowest
 * real LO frequency this project ever asks for (TUNE_MIN_HZ minus
 * DEMOD_IF_OFFSET_HZ = 30000-24000 = 6kHz, since TUNE_MIN_HZ was
 * lowered from 100kHz on 01/09/2026 to reach DCF77/similar LF
 * stations), the needed (ARR+1) is only ~8320, nowhere near the
 * 16-bit ceiling - so a prescaler search is unnecessary complexity
 * for the range this is actually used in. Kept as a plain
 * compile-time assumption rather than a runtime search: if
 * LO_GEN_CROSSOVER_HZ is ever raised enough to need it,
 * lo_gen_gd32_set_freq()'s own range check below will start failing
 * loudly (return 0) well before silently producing a wrong frequency,
 * which is the right failure mode for a #define change nobody
 * remembered to revisit this comment for. */
#define LO_GEN_ARR_MAX 65535UL

static uint8_t s_running = 0U; /* 0 = pins in their boot-time Hi-Z input state, timer stopped */

void lo_gen_gd32_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_TIMER2);
    /* Pins/timer deliberately left untouched here - see this
     * function's header comment in lo_gen_gd32.h. */
}

uint8_t lo_gen_gd32_set_freq(uint32_t freq_hz)
{
    uint32_t arr;   /* ARR register value: counter period = arr+1 counts */
    uint32_t half;  /* CH2's CCR offset from CH1's - quarter of the OUTPUT period */

    if (freq_hz == 0U) {
        return 0U;
    }

    /* counter_period_counts (= ARR+1) = round(TIMERCLK / freq_hz / 2) -
     * see this file's TIMERCLK comment and lo_gen_gd32.h's technique
     * writeup for the /2 (toggle-mode quadrature's output period is
     * two counter periods). Round-to-nearest via the classic
     * integer-division trick round(A/B) = (A + B/2)/B, with A=TIMERCLK
     * and B=2*freq_hz (so B/2 = freq_hz, not TIMERCLK/2 - verified
     * against a plain floating-point round() for a few sample
     * frequencies before trusting this integer form). */
    arr = (LO_GEN_TIMER_CLK_HZ + freq_hz) / (2U * freq_hz);
    if (arr < 2U) {
        arr = 2U; /* need at least 2 counts to place a CCR offset at (ARR+1)/2 */
    }
    if (arr > (LO_GEN_ARR_MAX + 1U)) {
        debug_print_dec("lo_gen_gd32: freq too low for this module's range, Hz", freq_hz);
        return 0U;
    }
    arr -= 1U; /* ARR register value = counter_period_counts - 1 */
    half = (arr + 1U) / 2U; /* quarter of the OUTPUT period, in counts - CH2's offset from CH1 */

    if (!s_running) {
        timer_parameter_struct timer_init_struct;
        timer_oc_parameter_struct oc_init_struct;

        gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_6 | GPIO_PIN_7);
        gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6 | GPIO_PIN_7);
        gpio_af_set(GPIOA, GPIO_AF_2, GPIO_PIN_6 | GPIO_PIN_7);

        timer_deinit(TIMER2);
        timer_struct_para_init(&timer_init_struct);
        timer_init_struct.prescaler         = 0U;
        timer_init_struct.alignedmode       = TIMER_COUNTER_EDGE;
        timer_init_struct.counterdirection  = TIMER_COUNTER_UP;
        timer_init_struct.clockdivision     = TIMER_CKDIV_DIV1;
        timer_init_struct.period            = arr;
        timer_init_struct.repetitioncounter = 0U;
        gd32_timer_init(TIMER2, &timer_init_struct);

        timer_channel_output_struct_para_init(&oc_init_struct);
        oc_init_struct.outputstate = TIMER_CCX_ENABLE;
        oc_init_struct.ocpolarity  = TIMER_OC_POLARITY_HIGH;

        /*
         * *** 01/09/2026, SWAPPED - real hardware bug fix, same root
         * cause as ms5351.c's low-band fix this same day *** - this
         * used to give CH1(PA6) the CCR=0 slot on the assumption that
         * PA6/CLK0 should be the LEADING channel, matching what
         * ms5351.c's own comments claimed ("CLK0 leads CLK1 by 90
         * degrees"). That claim was backwards: the Si5351/MS5351
         * datasheet defines CLKx_PHOFF as a time DELAY, and the
         * high-band register-offset scheme writes its nonzero value to
         * CLK0 - so CLK0 actually LAGS, CLK1 LEADS, in the real,
         * already-working high-band path (confirmed by the project
         * owner: SSB sidebands came out correct above 5MHz but swapped
         * below it, on BOTH this module and ms5351.c's own low-band
         * trick, which had made the identical wrong assumption). CH2
         * (PA7) now gets CCR=0 (leading), CH1 (PA6) gets CCR=half
         * (lagging) - matching PA7<->CLK1 as the real leading net.
         *
         * *** SWAPPED BACK, same day - real hardware measurement,
         * external generator *** - the project owner directly
         * measured this module's own PA6/PA7 output (below the
         * 300kHz crossover) with an external signal generator and
         * found CLK0/CLK1 polarity inverted relative to what the
         * swap above produced - a DIRECT phase measurement, a more
         * reliable data point than the SSB-sideband-sense inference
         * the swap above was based on. Reverted to CH1(PA6)=leading
         * (CCR=0), CH2(PA7)=lagging (CCR=half) - i.e. back to this
         * function's ORIGINAL convention, before that same-day swap.
         * NOT yet re-confirmed whether this reintroduces the SSB
         * sideband inversion the swap above was chasing in the first
         * place - the two symptoms (external-generator phase sense,
         * and SSB sideband sense) SHOULD agree if this is the only
         * thing in the chain that matters, but haven't both been
         * re-checked together after this specific change - worth
         * re-testing SSB reception below 300kHz once this is flashed,
         * not just trusting the generator measurement alone. If SSB
         * comes out wrong again after this, the actual bug is
         * probably elsewhere (e.g. an I/Q channel-order swap
         * somewhere in the RX chain, independent of CLK0/CLK1 timing)
         * rather than in this specific phase relationship.
         */
        timer_channel_output_config(TIMER2, TIMER_CH_0, &oc_init_struct);
        timer_channel_output_mode_config(TIMER2, TIMER_CH_0, TIMER_OC_MODE_TOGGLE);
        timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_0, 0U);

        timer_channel_output_config(TIMER2, TIMER_CH_1, &oc_init_struct);
        timer_channel_output_mode_config(TIMER2, TIMER_CH_1, TIMER_OC_MODE_TOGGLE);
        timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_1, half);

        timer_enable(TIMER2);
        s_running = 1U;

        debug_print_dec("lo_gen_gd32: started, Hz", freq_hz);
    } else {
        /* Live retune within this module's range: just move both CCRs
         * (and ARR) together - CH2 stays offset from CH1 by exactly
         * half the new period, so quadrature survives the retune with
         * no re-init/glitch beyond the timer's own regular ARR/CCR
         * update timing. */
        timer_autoreload_value_config(TIMER2, arr);
        timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_0, 0U);
        timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_1, half);
    }

    debug_print_dec("lo_gen_gd32: ARR", arr);
    return 1U;
}

void lo_gen_gd32_stop(void)
{
    if (!s_running) {
        return;
    }

    timer_disable(TIMER2);
    timer_deinit(TIMER2);

    /* Exact same Hi-Z input config main.c's boot sequence uses for
     * these two pins - see its own PA6/PA7 comment. */
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO_PIN_6 | GPIO_PIN_7);

    s_running = 0U;
    debug_print("lo_gen_gd32: stopped, PA6/PA7 back to Hi-Z\n");
}

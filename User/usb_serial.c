/*
 * usb_serial.c - USB CDC-ACM virtual COM port for the DeepSDR 101.
 * See usb_serial.h for the API contract and the TIMER2-vs-TIMER5 note.
 *
 * This file plays the role drv_usb_hw.c plays in GigaDevice's vendor
 * CDC_ACM demo (RCU/GPIO/NVIC/delay-unit setup), plus the IRQ handlers
 * the demo puts in gd32f4xx_it.c, plus a small ring-buffer front end
 * so the rest of the firmware doesn't have to know about the class
 * driver's internal packet_receive/packet_sent bookkeeping.
 */

#include "usb_serial.h"

#include "gd32f4xx.h"
#include "usbd_core.h"
#include "cdc_acm_core.h"
#include "drv_usb_hw.h"
#include "drv_usbd_int.h"

/* ------------------------------------------------------------------
 * USB device core instance + CDC RX ring buffer
 * ------------------------------------------------------------------ */

static usb_core_driver s_cdc_usb;

/* 256 bytes gives a few USB full-speed packets (64 bytes each) of
 * slack if the main loop is briefly busy (e.g. mid waterfall row)
 * when a burst of command bytes arrives - plenty for a command/
 * telemetry channel, not sized for bulk throughput. */
#define USB_SERIAL_RX_RING_SIZE   256U
static volatile uint8_t s_rx_ring[USB_SERIAL_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0U; /* next free slot to write */
static volatile uint32_t s_rx_tail = 0U; /* next byte to read */
static volatile uint32_t s_rx_drops = 0U; /* bytes lost to ring overflow - diagnostic only */

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

void usb_serial_init(void)
{
    usb_gpio_config();
    usb_rcu_config();
    usb_timer_init();

    usbd_init(&s_cdc_usb, USB_CORE_ENUM_FS, &cdc_desc, &cdc_class);

    usb_intr_config();
}

uint8_t usb_serial_is_ready(void)
{
    return (USBD_CONFIGURED == s_cdc_usb.dev.cur_status) ? 1U : 0U;
}

void usb_serial_poll(void)
{
    usb_cdc_handler *cdc;

    if (USBD_CONFIGURED != s_cdc_usb.dev.cur_status) {
        return;
    }

    cdc = (usb_cdc_handler *)s_cdc_usb.dev.class_data[CDC_COM_INTERFACE];
    if (NULL == cdc) {
        return;
    }

    if (1U == cdc->packet_receive) {
        uint32_t i;
        uint32_t n = cdc->receive_length;

        /* Copy out of the class driver's own buffer BEFORE re-arming
         * the endpoint below - re-arming is what allows the next OUT
         * packet to overwrite cdc->data, and until we re-arm the
         * endpoint just NAKs, so there's no race here even though
         * this runs in the main loop, not an ISR. */
        for (i = 0U; i < n; i++) {
            uint32_t next_head = (s_rx_head + 1U) % USB_SERIAL_RX_RING_SIZE;

            if (next_head == s_rx_tail) {
                /* ring full - drop the rest of this packet rather
                 * than overwrite unread data */
                s_rx_drops += (n - i);
                break;
            }

            s_rx_ring[s_rx_head] = cdc->data[i];
            s_rx_head = next_head;
        }

        /* Re-arm reception of the next OUT packet. This also clears
         * cdc->packet_receive/packet_sent back to 0 - see
         * cdc_acm_data_receive()'s own body. */
        cdc_acm_data_receive(&s_cdc_usb);
    }
}

uint32_t usb_serial_read(uint8_t *buf, uint32_t maxlen)
{
    uint32_t n = 0U;

    while ((n < maxlen) && (s_rx_tail != s_rx_head)) {
        buf[n] = s_rx_ring[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1U) % USB_SERIAL_RX_RING_SIZE;
        n++;
    }

    return n;
}

uint8_t usb_serial_write(const uint8_t *buf, uint16_t len)
{
    usb_cdc_handler *cdc;

    if (USBD_CONFIGURED != s_cdc_usb.dev.cur_status) {
        return 1U;
    }

    cdc = (usb_cdc_handler *)s_cdc_usb.dev.class_data[CDC_COM_INTERFACE];
    if (NULL == cdc) {
        return 1U;
    }

    /* Single full-speed bulk packet per call, same as the class
     * driver's own max endpoint size - keeps this function simple
     * (no multi-packet queuing/zero-length-packet handling). Callers
     * with more than 64 bytes to send should split it across
     * multiple usb_serial_poll() iterations, checking the return
     * value each time. */
    if (len > USB_CDC_DATA_PACKET_SIZE) {
        return 1U;
    }

    if (0U == cdc->packet_sent) {
        /* previous IN transfer (or the class driver's own init,
         * which starts with packet_sent = 1) hasn't completed yet */
        return 1U;
    }

    cdc->packet_sent = 0U;
    usbd_ep_send(&s_cdc_usb, CDC_DATA_IN_EP, (uint8_t *)buf, len);

    return 0U;
}

/* ------------------------------------------------------------------
 * Hardware layer (drv_usb_hw.h contract) - RCU/GPIO/NVIC/delay unit.
 *
 * Based on GigaDevice's CDC_ACM demo gd32f4xx_hw.c, with the delay
 * unit moved from TIMER2 to TIMER5 - see usb_serial.h's header
 * comment for why.
 * ------------------------------------------------------------------ */

#define TIM_MSEC_DELAY    0x01U
#define TIM_USEC_DELAY    0x02U

static __IO uint32_t s_usb_delay_time = 0U;

static void usb_hw_time_set(uint8_t unit);
static void usb_hw_delay(uint32_t ntime, uint8_t unit);

void usb_rcu_config(void)
{
    /*
     * PLLSAIP = 48MHz for the USB clock.
     *
     * IMPORTANT: PLLSAI has NO input divider of its own - it shares
     * the main PLL's PSC field (RCU_PLL, same register system_clock_
     * config() in system_gd32f4xx.c writes, and the same one gd32_i2s.c
     * already had to account for when computing PLLI2S's N/R - see
     * that file's own TIMERCLK-style comment and system_gd32f4xx.c's
     * "PSC is the same field used by PLLI2S" note). This board runs
     * PSC=8 against its real 12.288MHz crystal (not the 25MHz an
     * unmodified vendor PSC=25 assumes), so:
     *
     *   PLLSAI input = HXTAL / PSC = 12.288MHz / 8 = 1.536MHz
     *
     * The vendor CDC_ACM demo's own PLLSAI numbers (N=288, P=6) assume
     * a 1MHz input (25MHz EVAL crystal / PSC=25) - lifting them
     * unchanged onto this board's PSC=8 would give
     * 1.536MHz*288/6 = 73.728MHz instead of 48MHz, which is exactly
     * the kind of clock-out-of-spec problem that shows up on the host
     * as "Device not responding to setup address" / "error -71"
     * rather than a clean failure - the device electrically connects
     * (D+/D- pull-up is fine) but SETUP transactions fail because the
     * peripheral's bit timing is off.
     *
     * Recalculated for THIS board's 1.536MHz PLLSAI input:
     *   N=125, P=4 -> VCO = 1.536MHz * 125 = 192MHz (well within the
     *   100-432MHz VCO range) -> PLLSAIP = 192MHz / 4 = 48MHz exactly.
     * R is unused by anything USB-related here (only PLLSAIP matters
     * for CK48M) - kept at 2, the only requirement is that it's a
     * valid divider (2-7).
     */
    rcu_pllsai_config(125U, 4U, 2U);
    RCU_CTL |= RCU_CTL_PLLSAIEN;
    while (0U == (RCU_CTL & RCU_CTL_PLLSAISTB)) {
    }

    rcu_pll48m_clock_config(RCU_PLL48MSRC_PLLSAIP);
    rcu_ck48m_clock_config(RCU_CK48MSRC_PLL48M);

    rcu_periph_clock_enable(RCU_USBFS);
}

void usb_gpio_config(void)
{
    rcu_periph_clock_enable(RCU_SYSCFG);
    rcu_periph_clock_enable(RCU_GPIOA);

    /* USBFS_DM(PA11) and USBFS_DP(PA12) - confirmed free in this
     * project (nothing else on the board uses PA11/PA12), and the
     * bootloader already exercises this same USBFS peripheral/pins
     * for its own MSC firmware-update mode, so this pinout is known
     * to work on real hardware. */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_11 | GPIO_PIN_12);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_11 | GPIO_PIN_12);
    gpio_af_set(GPIOA, GPIO_AF_10, GPIO_PIN_11 | GPIO_PIN_12);
}

void usb_intr_config(void)
{
    /* Deliberately NOT calling nvic_priority_group_set() here: this
     * project never sets a priority group of its own (relies on the
     * CMSIS/startup default), and existing nvic_irq_enable() calls
     * elsewhere (DMA0_Channel3 at priority 6, EXTI2 at priority 2)
     * assume that default grouping. Changing the
     * grouping here would silently reinterpret those numbers under a
     * different preempt/sub-priority split. If the project ever wants
     * an explicit group, it should be set once, early in main(),
     * before any nvic_irq_enable() call - not from here.
     *
     * USBFS_LOW_POWER is 0 (see usb_conf.h), so no wakeup-from-STOP
     * EXTI/NVIC setup is needed either.
     *
     * Priority 7: lower than every existing real-time peripheral in
     * this project (DMA0_Channel3/audio at 6, EXTI2/touch at 2) - a
     * command/telemetry USB link should never be able to delay the
     * audio DMA ISR, and enumeration is not latency-sensitive.
     *
     * IMPORTANT - this must stay numerically HIGHER (lower priority)
     * than TIMER5_DAC_IRQn (see usb_timer_init() below), not equal:
     * the USB core's own reset handling (usbd_int_reset(), called
     * from THIS ISR on every USB bus reset - a normal, unavoidable
     * part of enumeration) flushes FIFOs via usb_txfifo_flush(),
     * which blocks on usb_udelay(3) - i.e. it busy-waits from INSIDE
     * this ISR for TIMER5's interrupt to decrement the delay counter.
     * On Cortex-M, an interrupt can only preempt one of STRICTLY
     * lower priority (a higher number here) - if TIMER5 and USBFS
     * shared this same priority (an earlier version of this file had
     * them both at 7), that wait could never be satisfied while
     * USBFS_IRQHandler is running, freezing the whole firmware solid
     * on the very first bus reset the host issues - which is exactly
     * what happened when this bug was in place. */
    nvic_irq_enable(USBFS_IRQn, 7U, 0U);
}

void usb_timer_init(void)
{
    /* Priority 5: deliberately HIGHER than USBFS_IRQn (7) - see the
     * IMPORTANT note in usb_intr_config() above for why this specific
     * ordering isn't optional. It also ends up higher than the audio
     * DMA ISR (6), but that's harmless: this ISR's body is a handful
     * of register accesses (flag check/clear, decrement, maybe
     * timer_disable()), only active for the brief bursts around USB
     * enumeration/reset, not a continuous load. */
    nvic_irq_enable(TIMER5_DAC_IRQn, 5U, 0U);
    rcu_periph_clock_enable(RCU_TIMER5);
}

void usb_udelay(const uint32_t usec)
{
    usb_hw_delay(usec, TIM_USEC_DELAY);
}

void usb_mdelay(const uint32_t msec)
{
    usb_hw_delay(msec, TIM_MSEC_DELAY);
}

/* Called from this file's own TIMER5_DAC_IRQHandler(), see below. */
void usb_timer_irq(void)
{
    if (RESET != timer_interrupt_flag_get(TIMER5, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);

        if (s_usb_delay_time > 0U) {
            s_usb_delay_time--;
        } else {
            timer_disable(TIMER5);
        }
    }
}

/* USBFS_LOW_POWER is 0 in usb_conf.h, so nothing in this build ever
 * suspends the USB core into STOP mode - this stub exists only to
 * satisfy drv_usb_hw.h's contract, in case wakeup-from-STOP support
 * is added later. */
void system_clk_config_stop(void)
{
}

static void usb_hw_delay(uint32_t ntime, uint8_t unit)
{
    s_usb_delay_time = ntime;

    usb_hw_time_set(unit);

    while (0U != s_usb_delay_time) {
    }

    timer_disable(TIMER5);
}

static void usb_hw_time_set(uint8_t unit)
{
    timer_parameter_struct timer_basestructure;
    uint16_t timer_prescaler;

    /* Same APB1-doubling reasoning as lo_gen_gd32.c's/gd32_i2s.c's own
     * TIMERCLK comments: APB1 prescaler isn't 1, so TIMERxCLK = 2 x
     * PCLK1 for every APB1 timer, TIMER5 included. */
    timer_prescaler = (uint16_t)(((rcu_clock_freq_get(CK_APB1) / 1000000U * 2U) / 12U) - 1U);

    timer_disable(TIMER5);
    timer_interrupt_disable(TIMER5, TIMER_INT_UP);

    if (TIM_USEC_DELAY == unit) {
        timer_basestructure.period = 11U;
    } else {
        timer_basestructure.period = 11999U;
    }

    timer_basestructure.prescaler         = timer_prescaler;
    timer_basestructure.alignedmode       = TIMER_COUNTER_EDGE;
    timer_basestructure.counterdirection  = TIMER_COUNTER_UP;
    timer_basestructure.clockdivision     = TIMER_CKDIV_DIV1;
    timer_basestructure.repetitioncounter = 0U;

    gd32_timer_init(TIMER5, &timer_basestructure);

    timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);
    timer_auto_reload_shadow_enable(TIMER5);
    timer_interrupt_enable(TIMER5, TIMER_INT_UP);
    timer_enable(TIMER5);
}

/* ------------------------------------------------------------------
 * IRQ handlers - both weak symbols in the startup file, overridden
 * here. Kept in this file (not a generic gd32f4xx_it.c) to match this
 * project's existing convention of one handler per owning module
 * (see sdr_rx.c's DMA0_Channel3_IRQHandler, touch.c's
 * EXTI2_IRQHandler).
 * ------------------------------------------------------------------ */

void USBFS_IRQHandler(void)
{
    usbd_isr(&s_cdc_usb);
}

void TIMER5_DAC_IRQHandler(void)
{
    usb_timer_irq();
}

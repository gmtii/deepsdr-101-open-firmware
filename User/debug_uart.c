#include "debug_uart.h"

#if DEBUG_UART_ENABLED
/* Whole file compiles to nothing when DEBUG_UART_ENABLED is 0 - see
 * debug_uart.h's comment. Keeps PA9/USART0 untouched (no GPIO AF
 * config, no clock enable) rather than just skipping the prints. */

void debug_uart_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
}

static void uart_putc(char c)
{
    while (usart_flag_get(USART0, USART_FLAG_TBE) == RESET) {
    }
    usart_data_transmit(USART0, (uint8_t)c);
}

void debug_print(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s);
        s++;
    }
}

static void uart_print_hex_nibble(uint8_t nibble)
{
    if (nibble < 10) {
        uart_putc('0' + nibble);
    } else {
        uart_putc('A' + (nibble - 10));
    }
}

void debug_print_hex32(const char *label, uint32_t val)
{
    debug_print(label);
    debug_print(" = 0x");
    for (int i = 7; i >= 0; i--) {
        uart_print_hex_nibble((val >> (i * 4)) & 0xF);
    }
    debug_print("\n");
}

void debug_print_hex16(const char *label, uint16_t val)
{
    debug_print(label);
    debug_print(" = 0x");
    for (int i = 3; i >= 0; i--) {
        uart_print_hex_nibble((val >> (i * 4)) & 0xF);
    }
    debug_print("\n");
}

void debug_print_dec(const char *label, uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[10] = '\0';

    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = '0' + (val % 10);
            val /= 10;
        }
    }

    debug_print(label);
    debug_print(" = ");
    debug_print(&buf[i]);
    debug_print("\n");
}

#endif /* DEBUG_UART_ENABLED */

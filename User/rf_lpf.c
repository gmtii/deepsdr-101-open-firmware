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
    uint32_t max_hz;    /* hasta donde ELIGE este filtro */
    uint32_t corte_hz;  /* y hasta donde DEJA PASAR, que no es lo mismo */
    uint8_t  pa1;
    uint8_t  pa2;
    uint8_t  pa5;
} lpf_range_t;

/*
 * LOS TOPES, Y POR QUE ACABAN EN 999.999 - aclarado el 24/10/2026.
 *
 * La cabecera documenta los tramos como 0-36 / 37-60 / 61-120 / 121-180
 * MHz, y los topes de aqui son 36.999.999 / 60.999.999 / 120.999.999. O sea
 * que 36,0-37,0 usa el filtro "hasta 36", 60,0-61,0 el de "hasta 60" y
 * 120,0-121,0 el de "hasta 120": tres franjas de 1 MHz en las que el filtro
 * trabaja por encima de su numero.
 *
 * ES A PROPOSITO -la alternativa es un hueco de 1 MHz sin filtro asignado,
 * que seria peor- pero el codigo y la cabecera no decian lo mismo y eso es
 * lo que se arregla: queda escrito aqui, que es donde se mira.
 */
static const lpf_range_t ranges[4] = {
    {  36999999UL,  36000000UL, 0U, 1U, 0U },  /* 1:   0 -  36,999 -> 010 */
    {  60999999UL,  60000000UL, 1U, 1U, 0U },  /* 2:      -  60,999 -> 110 */
    { 120999999UL, 120000000UL, 0U, 0U, 0U },  /* 3:      - 120,999 -> 000 */
    { 180000000UL, 180000000UL, 1U, 0U, 0U },  /* 4:      - 180     -> 100 */
};

static uint8_t s_current = 0U; /* 0 = not yet applied, else 1..4 */

uint32_t rf_lpf_tope_hz(void)
{
    /*
     * EL CORTE DEL FILTRO, NO EL BORDE DEL TRAMO - 26/10/2026, revision a
     * fondo de espejos y armonicos.
     *
     * Devolvia max_hz, que son los 36.999.999 del tramo de SELECCION. Pero
     * el comentario de aqui arriba explica que ese ultimo megahercio es a
     * proposito y que ahi "el filtro trabaja por encima de su numero": el
     * filtro corta en 36, no en 36,999.
     *
     * Quien llama a esto -arm_busca(), para no nombrar bandas que el
     * filtro ya rechaza- se creia que entraba un megahercio mas de lo que
     * entra, y habria nombrado como sospechoso un armonico entre 36 y 37
     * MHz estando cortado. Hoy no es alcanzable porque no hay ninguna
     * banda en la tabla entre 29,7 y 50 MHz, asi que esto no cambia ni una
     * fila; se arregla para que no sea una trampa el dia que se añada una.
     */
    if (s_current == 0U || s_current > 4U) { return 0UL; }
    return ranges[s_current - 1U].corte_hz;
}

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

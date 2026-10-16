/*
 * LOS CUATRO CLAVOS QUE EL CARGADOR NECESITA Y LA APLICACION PONE EN
 * main.c: el contador de milisegundos que usa delay_ms() del driver del
 * panel, los dos identificadores que rm68120_init() deja escritos, y un
 * _init() vacio para __libc_init_array.
 *
 * Estan aqui y no en boot_main.c para que se vea de un vistazo TODO lo que
 * el cargador toma prestado del firmware sin haberlo escrito el.
 */
#include "gd32f4xx.h"

volatile uint32_t g_msticks = 0U;
volatile uint16_t g_panel_id_check1 = 0U;
volatile uint16_t g_panel_id_check2 = 0U;

void SysTick_Handler(void) { g_msticks++; }

/* newlib llama a esto desde __libc_init_array; aqui no hay constructores. */
void _init(void) { }

void reloj_de_milisegundos(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
}

/* La del driver del panel es static, asi que aqui va otra. Cuenta ticks
 * reales del SysTick, no NOPs: al cambiar de chip o de reloj sigue
 * midiendo milisegundos. */
void delay_ms(uint32_t ms)
{
    uint32_t t = g_msticks;
    while ((g_msticks - t) < ms) { __NOP(); }
}

/* El manejador del USB que usa toda la libreria de GigaDevice. Vive aqui,
 * en un solo sitio, porque lo comparten boot_main.c y usb_it.c. */
#include "drv_usb_core.h"
usb_core_driver g_usb;

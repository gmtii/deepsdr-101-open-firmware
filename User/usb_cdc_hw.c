/*
 * EL PEGAMENTO DE HARDWARE DEL USB, ESCRITO AQUI Y NO REUTILIZADO.
 *
 * La libreria del fabricante trae usb/gd32f4xx_hw.c con estas mismas
 * funciones ya hechas, y el cargador las usa. En la aplicacion NO SE PUEDEN
 * USAR, y no por gusto: tienen tres cosas que en esta radio hacen daño.
 *
 * 1. TIMER2 SE LO LLEVARIA PUESTO.
 *    usb_timer_init() del fabricante configura TIMER2 para sus retardos.
 *    En esta radio TIMER2 ya tiene dueño -dos, de hecho-: genera el reloj
 *    maestro de 1,536 MHz del codec por TIMER2_CH0/PC6 (ver gd32_i2s.c) y
 *    es el oscilador local por debajo de 300 kHz (ver lo_gen_gd32.c).
 *    Llamarla dejaria la radio muda en el momento de enchufar el USB.
 *    Aqui los retardos salen de g_msticks y de un bucle, que no es de nadie.
 *
 * 2. CAMBIARIA LA AGRUPACION DE PRIORIDADES DE TODA LA RADIO.
 *    usb_intr_config() empieza con nvic_priority_group_set(PRE2_SUB2). Esta
 *    aplicacion usa PRE4_SUB0 (ver irq_prio.h) y reparte prioridades con
 *    ese reparto: 0 el systick, 1 el audio, 2 el tactil. Cambiar la
 *    agrupacion no da ningun error -reinterpreta en silencio los numeros ya
 *    escritos en todos los periféricos-, y lo que sale es un orden de
 *    expulsion distinto del que se diseño. Aqui no se toca.
 *
 * 3. EL RELOJ DE 48 MHz NO PUEDE SALIR DEL PLL PRINCIPAL. Ver abajo.
 */
#include "gd32f4xx.h"
#include "usb_cdc_hw.h"
#include "irq_prio.h"

extern volatile uint32_t g_msticks;

/*
 * LOS 48 MHz DEL USB, SIN BAJAR LA CPU.
 *
 * El USB de velocidad plena necesita 48 MHz con un +-0,25%. Y de este PLL
 * principal no salen:
 *
 *   cristal 12,288 / PSC 8 = 1,536 MHz
 *   1,536 x N=260 = VCO 399,36      / P=2 = 199,68 MHz de sistema
 *                                   / Q=8 =  49,92 MHz   <- +4,0%
 *
 * Un 4% son dieciseis veces la tolerancia: el ordenador ve que se engancha
 * algo y la enumeracion no termina nunca. Es exactamente lo que le pasaba
 * al cargador, y alli se arreglo BAJANDO el sistema a 192 MHz (N=250), que
 * da Q=8 -> 48,000 exactos. A un cargador le sobra CPU; a la aplicacion no,
 * y ademas mover el reloj de sistema arrastra el reloj del oscilador de
 * banda baja (LO_GEN_TIMER_CLK_HZ), el del I2S, el del codec y los tiempos
 * del bus de pantalla. Todo eso por un puerto serie no sale a cuenta.
 *
 * LA SALIDA: el GD32F450 puede alimentar su PLL48M del PLLSAI en vez de
 * del Q del PLL principal (RCU_PLL48MSRC_PLLSAIP). El PLLSAI en esta radio
 * NO LO USA NADIE -el I2S va por PLLI2S, que es otro-, asi que esta libre:
 *
 *   1,536 x N=125 = VCO 192,00 MHz   (dentro de 100-432)
 *   VCO / P=4     =  48,000 MHz EXACTOS
 *
 * Y el sistema se queda en 199,68 sin enterarse. El PSC es compartido entre
 * los tres PLL y ya vale 8, que es justo el que hace falta: si alguien lo
 * cambia, esta cuenta deja de salir, y por eso el _Static_assert de abajo.
 */
#define PLLSAI_N  125U
#define PLLSAI_P  4U

_Static_assert((12288000UL / 8UL) * PLLSAI_N / PLLSAI_P == 48000000UL,
               "el PLLSAI no da 48,000 MHz exactos: el USB no enumerara");
_Static_assert(((12288000UL / 8UL) * PLLSAI_N) >= 100000000UL
               && ((12288000UL / 8UL) * PLLSAI_N) <= 432000000UL,
               "el VCO del PLLSAI se sale de su rango de 100 a 432 MHz");

uint8_t usb_cdc_hw_reloj(void)
{
    uint32_t guard = 500000UL;

    /* PLLSAIP a 4, PLLSAIN a 125. Los campos Q y R no los usa nadie aqui;
     * se dejan en un valor legal (2 y 2) porque el cero no lo es. */
    RCU_PLLSAI = (PLLSAI_N << 6)
               | (((PLLSAI_P / 2U) - 1U) << 16)
               | (2UL << 24)
               | (2UL << 28);

    RCU_CTL |= RCU_CTL_PLLSAIEN;
    while (((RCU_CTL & RCU_CTL_PLLSAISTB) == 0U) && (guard != 0U)) {
        guard--;
    }
    if (guard == 0U) {
        /*
         * Si el PLLSAI no engancha, NO se sigue. Arrancar el USB con un
         * reloj que no es 48 MHz no da un error: enumera a medias y deja al
         * ordenador reintentando, que es mucho peor de diagnosticar que un
         * "no se pudo".
         */
        return 0U;
    }

    rcu_pll48m_clock_config(RCU_PLL48MSRC_PLLSAIP);
    rcu_ck48m_clock_config(RCU_CK48MSRC_PLL48M);
    rcu_periph_clock_enable(RCU_USBFS);
    return 1U;
}

void usb_gpio_config(void)
{
    rcu_periph_clock_enable(RCU_SYSCFG);
    rcu_periph_clock_enable(RCU_GPIOA);

    /* USBFS_DM(PA11) y USBFS_DP(PA12). Las mismas que usa el cargador. */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_11 | GPIO_PIN_12);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX,
                            GPIO_PIN_11 | GPIO_PIN_12);
    gpio_af_set(GPIOA, GPIO_AF_10, GPIO_PIN_11 | GPIO_PIN_12);
}

void usb_rcu_config(void)
{
    /* Lo del reloj ya lo hizo usb_cdc_hw_reloj(); esta existe porque la
     * libreria del fabricante la llama por su nombre. */
}

void usb_intr_config(void)
{
    /*
     * SIN nvic_priority_group_set(). Ver el punto 2 de arriba.
     *
     * Y por DEBAJO del audio: el USB interrumpe a 1 kHz y su manejador
     * copia buffers. Si pudiera expulsar al audio, cada trama de USB seria
     * un hueco en el I2S. Al reves no importa: una trama de USB que llega
     * tarde la reintenta el ordenador; una muestra de audio que llega tarde
     * se oye.
     */
    nvic_irq_enable(USBFS_IRQn, IRQ_PRIO_USB, 0U);
}

void usb_timer_init(void)
{
    /* A proposito vacia: ver el punto 1 de la cabecera. TIMER2 es el reloj
     * del codec y el oscilador de banda baja. */
}

void usb_udelay(const uint32_t usec)
{
    /*
     * Bucle y no temporizador. Sale largo -no esta calibrado- y da igual:
     * la libreria solo lo usa en el arranque del nucleo, donde lo que
     * importa es esperar AL MENOS lo pedido.
     */
    volatile uint32_t i;
    uint32_t vueltas = usec * 40UL;

    for (i = 0UL; i < vueltas; i++) { __NOP(); }
}

void usb_mdelay(const uint32_t msec)
{
    uint32_t t0 = g_msticks;

    while ((g_msticks - t0) < msec) { }
}

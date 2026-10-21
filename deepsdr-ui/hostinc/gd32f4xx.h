/*
 * Sucedaneo del gd32f4xx.h del fabricante, SOLO para el simulador del PC.
 *
 * Existe para que se pueda compilar aqui el User/spectrum.c de verdad. La
 * cadena de inclusiones del firmware acaba llegando al header del micro, y
 * en el PC no hay micro; pero spectrum.c no toca un solo registro -dibuja
 * sobre un buffer-, asi que con los tipos basta.
 *
 * Si algun dia un fichero que se compile aqui SI necesita un registro, el
 * error sera de simbolo que falta, bien visible, y no un valor silenciosamente
 * equivocado. Que es como tiene que fallar un sucedaneo.
 */
#ifndef GD32F4XX_H_SUCEDANEO
#define GD32F4XX_H_SUCEDANEO

#include <stdint.h>
#include <stddef.h>


/*
 * Anadido 24/09/2026 para poder compilar aqui el User/spi_flash.c de
 * verdad. Estos SI son registros -o mas bien pines-, y por eso no se
 * inventa ningun valor: son declaraciones, y quien enlace el banco tiene
 * que dar las definiciones (sim/gd32_pines.c las encamina al W25Q16
 * simulado de sim/w25q_sim.c). Un banco que se olvide de darlas falla al
 * enlazar, bien visible, que es como tiene que fallar un sucedaneo.
 */
typedef enum { RESET = 0, SET = 1 } bit_status;

#define GPIOA  0U
#define GPIOB  1U
#define GPIOC  2U
#define GPIOD  3U
#define GPIOE  4U

#define GPIO_PIN_3  (1U << 3)
#define GPIO_PIN_4  (1U << 4)
#define GPIO_PIN_5  (1U << 5)
#define GPIO_PIN_6  (1U << 6)
/* El 7 lo usa la patilla de medida de demod_am.c (DEMOD_AM_PROFILE_PIN).
 * Aqui no hay patillas, asi que solo hace falta que el numero exista y que
 * gpio_bit_set/reset se declaren; quien las define es el banco que las
 * necesite (sim/demod_stubs.c), igual que gd32_pines.c define las del bus
 * de la flash. */
#define GPIO_PIN_7  (1U << 7)

#define GPIO_MODE_OUTPUT   1U
#define GPIO_MODE_INPUT    0U
#define GPIO_PUPD_NONE     0U
#define GPIO_OTYPE_PP      0U
#define GPIO_OSPEED_2MHZ   0U
#define GPIO_OSPEED_25MHZ  1U
#define GPIO_OSPEED_50MHZ  2U
#define GPIO_OSPEED_200MHZ 3U
#define RCU_GPIOB          1U

void       gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pin);
void       gpio_output_options_set(uint32_t port, uint32_t otype, uint32_t speed, uint32_t pin);
void       gpio_bit_write(uint32_t port, uint32_t pin, bit_status val);
void       gpio_bit_set(uint32_t port, uint32_t pin);
void       gpio_bit_reset(uint32_t port, uint32_t pin);
bit_status gpio_input_bit_get(uint32_t port, uint32_t pin);
void       rcu_periph_clock_enable(uint32_t periph);

/* El "no hacer nada" del Cortex-M. En el PC es literalmente eso, y ademas
 * gratis: los retardos por conteo de instrucciones del firmware no pintan
 * nada aqui, donde el chip simulado contesta al instante. */
#define __NOP()  ((void)0)

/*
 * Anadido 25/09/2026 para poder compilar aqui el User/backlight.c de
 * verdad y comprobar la cuenta del CCR - que es donde vive el techo del
 * 100% y el suelo del 10%, o sea justo lo que se queria probar.
 *
 * Misma regla que arriba: aqui se DECLARA y el banco DEFINE. El banco
 * (sim/brillo.c) se queda con el CCR que backlight.c escribe y lo mira.
 * Nada de reimplementar la formula en el banco: eso probaria la copia.
 */
#define TIMER1                    1U
#define TIMER_CH_3                3U
#define TIMER_COUNTER_EDGE        0U
#define TIMER_COUNTER_UP          0U
#define TIMER_CKDIV_DIV1          0U
#define TIMER_CCX_ENABLE          1U
#define TIMER_OC_POLARITY_LOW     1U
#define TIMER_OC_POLARITY_HIGH    0U
#define TIMER_OC_MODE_PWM0        0U
#define GPIO_MODE_AF              2U
#define GPIO_AF_1                 1U
#define RCU_GPIOA                 0U
#define RCU_TIMER1                10U

typedef struct {
    uint16_t prescaler;
    uint16_t alignedmode;
    uint16_t counterdirection;
    uint32_t period;
    uint16_t clockdivision;
    uint8_t  repetitioncounter;
} timer_parameter_struct;

typedef struct {
    uint16_t outputstate;
    uint16_t outputnstate;
    uint16_t ocpolarity;
    uint16_t ocnpolarity;
    uint16_t ocidlestate;
    uint16_t ocnidlestate;
} timer_oc_parameter_struct;

void timer_deinit(uint32_t t);
void timer_struct_para_init(timer_parameter_struct *p);
void gd32_timer_init(uint32_t t, timer_parameter_struct *p);
void timer_channel_output_struct_para_init(timer_oc_parameter_struct *p);
void timer_channel_output_config(uint32_t t, uint16_t ch, timer_oc_parameter_struct *p);
void timer_channel_output_mode_config(uint32_t t, uint16_t ch, uint16_t mode);
void timer_channel_output_pulse_value_config(uint32_t t, uint16_t ch, uint32_t pulse);
void timer_auto_reload_shadow_enable(uint32_t t);
/* La sombra del comparador - 06/10/2026. Sin ella, cada escritura del
 * comparador entra en caliente y cuela un pulso espurio de 50 us en la
 * retroiluminacion por cada detente del mando. Ver backlight.c. */
#define TIMER_OC_SHADOW_ENABLE    ((uint16_t)0x0008U)
#define TIMER_OC_SHADOW_DISABLE   ((uint16_t)0x0000U)
void timer_channel_output_shadow_config(uint32_t t, uint16_t ch, uint16_t s);
void timer_enable(uint32_t t);
void gpio_af_set(uint32_t port, uint32_t af, uint32_t pin);


/*
 * EL CONTADOR DE CICLOS DEL NUCLEO (DWT->CYCCNT) - 10/10/2026.
 *
 * demod_am.c lo lee al principio y al final de cada bloque para publicar
 * cuanto le ha costado (demod_am_get_last_cycles(), que es de donde sale la
 * fila de carga de la pantalla de Informacion).
 *
 * Aqui NO es el contador del Cortex-M: es un contador de mentira que sube
 * solo cuando alguien lo mueve. A proposito, y no por pereza:
 *
 *   - una cuenta de ciclos del PC no dice NADA sobre el coste en la radio,
 *     que es un M4 a 199,68 MHz con una FPU de una instruccion por
 *     operacion; dar un numero aqui seria inventarse una medida;
 *   - y el banco necesita que sea PREDECIBLE para poder comprobar que
 *     demod_am.c resta bien el principio del final. Con un reloj de verdad
 *     eso no se puede afirmar, solo mirar.
 *
 * Asi que el banco lo pone a mano antes de cada bloque y comprueba la
 * resta. Lo que se mide es la CUENTA, no el tiempo.
 */
typedef struct { uint32_t CYCCNT; uint32_t CTRL; uint32_t LAR; } dwt_falso_t;
extern dwt_falso_t dwt_falso;
#define DWT (&dwt_falso)

/*
 * CTRL y LAR se añaden el 19/10/2026 para poder compilar aqui el
 * User/ms5351.c de verdad: su espera de la maniobra de los 90 grados
 * enciende el contador antes de usarlo. CYCCNT sigue siendo lo que era y
 * los bancos que lo mueven a mano no se enteran de esto.
 *
 * OJO con la espera: aqui CYCCNT no avanza solo, asi que un banco que
 * llame a algo que la use tiene que dejar SystemCoreClock a cero. Con cero
 * la cuenta de ciclos sale cero y la espera termina en la primera vuelta,
 * que es lo que hace falta en un banco que mide REGISTROS: la espera no
 * escribe ninguno, solo deja pasar tiempo de verdad en la radio.
 */
#define CoreDebug_DEMCR_TRCENA_Msk   (1UL << 24)
#define DWT_CTRL_CYCCNTENA_Msk       (1UL << 0)
typedef struct { uint32_t DEMCR; } coredebug_falso_t;
extern coredebug_falso_t coredebug_falso;
#define CoreDebug (&coredebug_falso)
extern uint32_t SystemCoreClock;

/*
 * Anadido 11/10/2026 para poder compilar aqui el User/touch.c de verdad.
 *
 * El arrastre no se podia medir sin el tactil de VERDAD: el filtro de
 * confirmacion que decide si una lectura vale vive en touch.c, y un banco
 * que se lo reimplemente mide su propia copia. Como el XPT2046 se habla a
 * patadas de GPIO -bit-bang, nada de periferico SPI-, basta con que el
 * banco defina gpio_input_bit_get() y amigos para poder dictarle al
 * firmware el dedo que quiera. Ver sim/tactil_arrastre.c.
 *
 * Misma regla de siempre: aqui se DECLARA, el banco DEFINE.
 */
#define GPIO_PIN_2         (1U << 2)
#define GPIO_PUPD_PULLUP   1U
#define RCU_GPIOD          3U
#define RCU_SYSCFG         20U

#define EXTI_2             (1U << 2)
#define EXTI_INTERRUPT     0U
#define EXTI_TRIG_FALLING  0U
#define EXTI_SOURCE_GPIOD  3U
#define EXTI_SOURCE_PIN2   2U
#define EXTI2_IRQn         8

void     syscfg_exti_line_config(uint8_t port, uint8_t pin);
void     exti_init(uint32_t linex, uint8_t mode, uint8_t trig);
uint8_t  exti_interrupt_flag_get(uint32_t linex);
void     exti_interrupt_flag_clear(uint32_t linex);
void     nvic_irq_enable(uint8_t irq, uint8_t pre, uint8_t sub);

#endif

/*
 * EL SALTO AL CARGADOR DE FABRICA - 01/10/2026.
 *
 * *** El dueno: "el nanovna tiene la opcion de con un boton entrar en modo
 * dfu" ... "asi me ahorro soldarle cables". *** Es la misma maniobra que
 * hace el NanoVNA, y aqui tambien se puede.
 *
 * El GD32F450 lleva de fabrica un cargador de 30 kB en 0x1FFF0000, grabado
 * en produccion y NO borrable, que reprograma la flash principal por
 * USART0, USART2 o USBFS -PA9, PA10, PA11 y PA12, o sea el mismo USB-C de
 * la radio- (manual GD32F4xx, seccion 1.4).
 *
 * Lo normal es entrar poniendo BOOT0 a 1 en el reset. Por software se hace
 * con los bits [2:0] del SYSCFG_CFG0, que mapean la memoria de sistema en
 * 0x00000000; desde ahi se salta como a cualquier imagen.
 *
 * PARA QUE SIRVE: para regrabar el CARGADOR sin abrir la radio. Un cargador
 * no puede reescribirse a si mismo; el de fabrica si puede.
 *
 * POR QUE SE QUEDABA PILLADA, LEIDO EN LA PROPIA ROM - 01/10/2026.
 *
 * *** El dueno: "tu boton de dfu no hace que la radio entre en modo dfu,
 * punto, y el modo dfu existe, asi que averigua como coño entrar en modo
 * dfu desde software". *** Pues volcada la ROM, desensamblada, y esto es
 * lo que hace su bucle principal, en 0x1FFF4D3E:
 *
 *     sondea las tres interfaces serie, una tras otra, para siempre
 *     ... y la UNICA otra salida del bucle es:
 *
 *     1fff4d62:  ldr r0, =0x20000162
 *     1fff4d64:  ldrb r0, [r0]
 *     1fff4d66:  cbz r0, seguir_sondeando
 *     1fff4d68:  movs r4, #0          -> y con r4=0 se va por el camino USB
 *
 * Un byte en 0x20000162. Y quien lo pone es la interrupcion del USB, en
 * 0x1FFF32C4:
 *
 *     ubfx r0, r0, #12, #1   ; bit 12 del registro de interrupcion = ENUMF,
 *     cbz  r0, ...           ; "enumeracion terminada"
 *     movs r0, #1
 *     strb r0, [r1]          ; r1 = 0x20000162
 *
 * O sea: la ROM sale a DFU **cuando un host la enumera por USB**. No mira
 * BOOT0 ni nada parecido; se queda esperando a que alguien le hable.
 *
 * Y ahi esta el fallo: cuando saltamos desde la aplicacion, el USBFS **ya
 * esta enumerado** -el gestor de arranque lo dejo montado como disco MSC
 * antes de saltar a la aplicacion-. El PC ya tiene su dispositivo y no
 * vuelve a enumerar, asi que ese bit nunca llega, el byte nunca se pone, y
 * la ROM se queda dando vueltas al bucle de los USART. Exactamente lo que
 * se ve desde fuera: pillada.
 *
 * La cura es forzar que el PC lo vea desaparecer y volver: **resetear el
 * USBFS** por su bit del RCU antes de saltar. Eso suelta la resistencia de
 * DP, el host da el dispositivo por desconectado, y cuando la ROM levanta
 * el suyo hay enumeracion nueva, llega el ENUMF y el byte se pone.
 *
 * *** 01/10/2026, por el dueno: "la radio se queda pillada y no sale el
 * dfu". *** Y la primera version se quedaba pillada por esto: __disable_irq()
 * solo pone la MASCARA. Las interrupciones siguen HABILITADAS en el NVIC y
 * siguen apuntandose como pendientes. El cargador de fabrica, en cuanto
 * levanta la mascara, se come todas las que la radio tenia en marcha -y
 * tenia el DMA del audio corriendo sin parar, canal 3 del DMA0, mas el EXTI2
 * del tactil- contra SU tabla de vectores, que no las espera.
 *
 * Por eso ahora se apagan y se limpian las 240 lineas del NVIC una a una,
 * antes de tocar el reloj. Es lo que faltaba.
 *
 * LOS PASOS QUE NO SON ADORNO, y en este orden:
 *
 *   1. Apagar las interrupciones, el SysTick, y DESHABILITAR Y LIMPIAR todo
 *      el NVIC. Si salta una interrupcion despues de remapear la memoria, el
 *      vector que lee ya no es el nuestro.
 *   2. rcu_deinit(). El cargador de fabrica espera el arbol de relojes como
 *      lo deja un reset, y aqui se llega con el PLL a 199,68 MHz. Sin esto
 *      puede no enumerar por USB.
 *   3. El remapeo ANTES de leer la pila y el vector, con DSB e ISB en medio:
 *      hasta que el remapeo no ha surtido efecto, 0x00000000 sigue siendo
 *      la flash.
 */
#include "dfu_salto.h"
#include "gd32f4xx.h"

#define DFU_BASE  0x1FFF0000UL

void dfu_salta(void)
{
    uint32_t sp, pc, i;

    /*
     * LA RECETA, TAL CUAL, SIN INVENTOS MIOS - 01/10/2026.
     *
     * *** El dueno: "pero y que me dices del codigo fuente que te he pasado
     * antes?" *** Comparandolo con lo que yo tenia, yo me habia pasado de
     * listo en tres sitios y me faltaba el que importa:
     *
     *   FALTABA  tirar de DP a masa por GPIO durante 100 ms. Yo reseteaba el
     *            USBFS -que suelta la resistencia- y seguia a 1 ms. Para que
     *            un host registre una desconexion eso no es nada, y por eso
     *            no sonaba ni el "pin" de desconectar.
     *
     *   SOBRABA  rcu_deinit(). La receta no toca el reloj. Lo meti yo
     *            razonando que la ROM querria el arbol como en un reset, y
     *            puede ser justo lo que la deja sin su reloj de 48 MHz.
     *
     *   SOBRABA  el remapeo del SYSCFG. Innecesario saltando directo a
     *            0x1FFF0000.
     *
     *   SOBRABA  __enable_irq() antes del salto. Si algo pendiente se cuela
     *            antes de que la ROM monte su tabla, se acabo.
     */
    __disable_irq();
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL  = 0UL;

    for (i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    __DSB();
    __ISB();

    /*
     * Y EL RELOJ TAMBIEN AL ESTADO DE RESET. Esto lo quite en la V2.39 por
     * seguir la receta al pie de la letra, y creo que fue un error:
     *
     * Llegamos aqui con el PLL EN MARCHA y siendo la fuente del reloj de
     * sistema, a 199,68 MHz. La ROM necesita 48 MHz para el USB y se los
     * monta ella. Pero el hardware IGNORA las escrituras a la configuracion
     * del PLL mientras el PLL esta encendido: si su codigo da por hecho que
     * arranca con el PLL apagado -que es lo que se encuentra viniendo de
     * BOOT0- escribira su configuracion, no surtira efecto, y se quedara
     * con un reloj de USB que no es 48 MHz. Resultado: no enumera nunca.
     *
     * La receta de referencia es de STM32, donde su ROM si se defiende de
     * eso. Aqui no cuesta nada dejarlo como lo deja un reset.
     */
    rcu_deinit();

    /*
     * Todos los perifericos como los deja un reset de verdad... MENOS EL
     * SYSCFG.
     *
     * *** Y aqui estaba el fallo gordo, el de la V2.38 y la V2.40. *** El
     * bit 14 del APB2RST es SYSCFGRST, y el SYSCFG es justo donde vive el
     * bit del modo de arranque que la ROM va a mirar. Resetearlo lo borra.
     * O sea que yo ponia el modo y acto seguido lo quitaba de en medio.
     */
    RCU_AHB1RST = 0xFFFFFFFFUL; RCU_AHB1RST = 0UL;
    RCU_AHB2RST = 0xFFFFFFFFUL; RCU_AHB2RST = 0UL;
    RCU_APB1RST = 0xFFFFFFFFUL; RCU_APB1RST = 0UL;
    RCU_APB2RST = 0xFFFFFFFFUL & ~RCU_APB2RST_SYSCFGRST;
    RCU_APB2RST = 0UL;
    __DSB();

    /*
     * DP (PA12) A MASA, 100 ms. Esto es lo que ve el PC como "me han
     * desenchufado un dispositivo", y es la condicion para que luego haya
     * una enumeracion NUEVA, que es lo unico que saca a la ROM de su bucle
     * (pone su byte de 0x20000162 al llegar el ENUMF).
     *
     * El retardo se cuenta a pelo porque aqui ya no hay SysTick: a 199,68
     * MHz, con el bucle a 3 ciclos por vuelta, 6.656.000 vueltas son los
     * 100 ms. No hace falta que sean exactos, hace falta que sean LARGOS.
     */
    /*
     * Y AHORA EL MODO DE ARRANQUE, LO ULTIMO, PARA QUE NADIE LO PISE.
     *
     * *** Esto es lo que lo explica todo, y sale de desensamblar la ROM. ***
     * Su main, en 0x1FFF12E0, hace lo primero de todo:
     *
     *   1fff12ea:  lsls r0, r1, #14     ; 0x4000 = bit 14 del APB2EN = SYSCFG
     *   1fff12ec:  bl   0x1fff3814      ; enciende su reloj
     *   1fff12f0:  ldr  r0, =0x40013800 ; SYSCFG_CFG0
     *   1fff12f4:  and.w r6, r0, #1     ; bit 0 = BOOT_MODE[0]
     *   1fff12f8:  cbnz r6, 0x1fff1344  ; si vale 1 -> se queda en la ROM
     *              ; si vale 0 -> SALTA A LA APLICACION y adios DFU
     *
     * La ROM SI comprueba el modo de arranque. Con BOOT0 lo pone el
     * hardware; por software hay que ponerlo aqui. Y lo quite en la V2.39
     * siguiendo una receta de STM32, donde no hace falta.
     */
    rcu_periph_clock_enable(RCU_SYSCFG);
    syscfg_bootmode_config(SYSCFG_BOOTMODE_BOOTLOADER);
    __DSB();

    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_12);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
    gpio_bit_reset(GPIOA, GPIO_PIN_12);
    for (i = 0U; i < 6656000U; i++) { __NOP(); }
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO_PIN_12);

    sp = *(volatile uint32_t *)(DFU_BASE);
    pc = *(volatile uint32_t *)(DFU_BASE + 4UL);
    __set_MSP(sp);
    ((void (*)(void))pc)();

    while (1) { }      /* no se vuelve de ahi */
}

/*
 * EL TRUCO DEL TESTIGO: REINICIAR Y SALTAR AL PRINCIPIO DE TODO.
 *
 * Saltar a la ROM desde una aplicacion en marcha es el momento mas malo
 * posible: el DMA del audio corriendo, el de la pantalla, el I2S, el EXMC.
 * dfu_salta() apaga lo que puede, pero apagar no es lo mismo que no haber
 * encendido.
 *
 * Asi que se hace al reves: la fila de Informacion deja una palabra magica
 * en una variable que NO se borra al reiniciar -seccion .noinit del linker
 * script- y pide un reinicio de verdad. Al arrancar, lo PRIMERO que hace
 * main(), antes de tocar un solo periferico, es mirar esa palabra. Si esta,
 * la borra y salta.
 *
 * Asi la ROM recibe una maquina practicamente recien reseteada.
 *
 * Lo unico que corre entre medias es el gestor de arranque, que monta su
 * disco USB. Por eso dfu_salta() resetea el USBFS de todas formas: sin eso
 * el PC no vuelve a enumerar y la ROM nunca ve su ENUMF.
 */
#define DFU_TESTIGO  0xDF0DF0DFUL

/* Global, no static: lo lee SystemInit() antes de que exista nada mas. */
__attribute__((section(".noinit"))) uint32_t g_dfu_testigo;
#define s_testigo g_dfu_testigo

void dfu_pide_reinicio(void)
{
    s_testigo = DFU_TESTIGO;
    __DSB();
    NVIC_SystemReset();
}

void dfu_mira_al_arrancar(void)
{
    if (s_testigo == DFU_TESTIGO) {
        s_testigo = 0UL;
        __DSB();
        dfu_salta();
    }
    s_testigo = 0UL;   /* basura de un arranque en frio */
}

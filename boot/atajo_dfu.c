/*
 * LA SALIDA DE EMERGENCIA DEL CARGADOR - 01/10/2026.
 *
 * *** El dueno: "si, vamos a anadir ese atajo, te diria encoder pulsado y
 * girandolo todo el rato". *** Eso es lo que hay aqui.
 *
 * PARA QUE SIRVE, QUE ES LO IMPORTANTE.
 *
 * El boton de DFU de la radio vive en la APLICACION (Ajustes -> Equipo ->
 * Informacion, pagina oculta). Y a la aplicacion se llega a traves del
 * cargador. O sea que si un cargador nuevo no arranca -se cuelga montando
 * el panel, o la flash SPI, o el USB- no hay aplicacion, no hay boton, y no
 * hay forma de regrabar nada sin abrir la radio.
 *
 * Esto rompe esa cadena: el gesto se mira ANTES que nada. Antes del panel,
 * antes de la flash SPI, antes del USB, antes incluso de que SystemInit()
 * toque el reloj. Si el resto del cargador esta roto, da igual: a este
 * trozo se llega siempre, porque es la primera instruccion util tras el
 * reset.
 *
 * EL GESTO: mantener pulsado el mando Y GIRARLO sin soltar.
 *
 * Tiene que ser algo que no pueda salir por accidente, porque el mando
 * pulsado a secas YA significa otra cosa -modo actualizacion- y encenderse
 * en DFU sin querer seria desconcertante: la pantalla se queda negra y la
 * radio parece muerta. Pulsar y girar no pasa sin querer.
 *
 * Se piden OCHO transiciones de cuadratura, que son dos topes del mando,
 * con el boton pulsado en TODAS las lecturas. Si se suelta, se abandona. Si
 * tras un cuarto de segundo no se ha movido ni un paso, tambien se
 * abandona: asi el que solo quiere el modo actualizacion no se come la
 * espera entera.
 *
 * POR QUE AQUI Y NO EN main().
 *
 * Doce versiones del boton de DFU de la aplicacion se fueron en aprender
 * esto: la ROM de fabrica MIDE el cristal para sacar sus 48 MHz de USB, y
 * lo hace dando por hecho que arranca desde el estado de reset. En main()
 * ya es tarde, porque SystemInit() ha levantado el HXTAL y subido el PLL a
 * 199,68 MHz. Aqui el reloj es el IRC16M y el PLL esta parado, que es
 * exactamente lo que la ROM espera.
 *
 * Por eso tambien va todo a registro pelado, sin llamar a la libreria de
 * GigaDevice: cuanto menos dependa este trozo de que algo mas funcione,
 * mejor cumple su oficio.
 *
 * LO QUE NO ARREGLA. Si el chip esta tan mal que no llega ni a ejecutar la
 * primera instruccion de la flash, esto tampoco corre. Para ese caso
 * siguen estando BOOT0 y el SWD, que son de hardware. Pero eso ya no es un
 * cargador malo: eso es una flash borrada.
 */
#include "gd32f4xx.h"
#include "atajo_gesto.h"

/* Los pines son los mismos que encoder_init() del firmware (User/encoder.c,
 * lineas 5 a 10). Si cambian alli, cambian aqui.
 *   A   = PD13, con resistencia de subida
 *   B   = PD12, con resistencia de subida
 *   SW  = PC9,  activo ALTO, con resistencia de bajada */
#define RCU_AHB1EN_R   REG32(0x40023830UL)
#define GPIOC_R        0x40020800UL
#define GPIOD_R        0x40020C00UL
#define CTL(p)         REG32((p) + 0x00UL)
#define PUD(p)         REG32((p) + 0x0CUL)
#define ISTAT(p)       REG32((p) + 0x10UL)

/* Un respiro de unos 2 ms. El reloj aqui es el IRC16M a 16 MHz y el bucle
 * sale a unos 3 ciclos por vuelta: 16e6 x 0,002 / 3 = 10.667.
 *
 * El porque de los 2 ms y no 5 esta en atajo_gesto.h, y lo descubrio el
 * banco: a 5 ms un giro brusco se salta dos cuartos de paso entre lecturas,
 * eso es un salto de dos bits, y los saltos de dos bits valen cero porque
 * es lo que nos protege del ruido. Muestreando mas rapido que el mando el
 * problema no existe. */
static void respiro(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 10667U; i++) { __NOP(); }
}

/* El estado de cuadratura, 0..3, tal cual lo lee el firmware: (A << 1) | B. */
static uint32_t lee_ab(void)
{
    uint32_t v = ISTAT(GPIOD_R);
    return (((v >> 13) & 1UL) << 1) | ((v >> 12) & 1UL);
}

static uint32_t boton(void)
{
    return (ISTAT(GPIOC_R) >> 9) & 1UL;
}

/*
 * EL SALTO, QUE ES LA RECETA DE LA V2.44 Y NI UN PASO MENOS.
 *
 * Cada linea de aqui costo una version:
 *
 *   NVIC limpio      (V2.32) __disable_irq() solo pone la MASCARA; las
 *                            lineas siguen habilitadas y apuntandose.
 *   modo de arranque (V2.41) la ROM mira el bit 0 del SYSCFG_CFG0 nada mas
 *                            empezar: si vale 0, se va a la aplicacion.
 *   reloj virgen     (V2.42) la ROM mide el cristal desde el reset.
 *   reset del USBFS  (V2.43) para que el host vea desaparecer lo que
 *                            hubiera enumerado.
 *   VTOR = 0         (V2.44) EL QUE FALTABA. Sin esto la interrupcion de
 *                            USB de la ROM se va a NUESTRA tabla, su
 *                            manejador no corre, el byte de 0x20000162 no
 *                            se pone, y la ROM se queda en su bucle de
 *                            USART. Que es "la radio se queda pillada".
 *
 * Viniendo del reset, el reloj y el VTOR YA estan como queremos y el USBFS
 * no lo ha tocado nadie. Se hace igual de todas formas: cuesta nada y no
 * depende de suponer por donde se ha llegado hasta aqui.
 */
static void salta_a_la_rom(void)
{
    volatile uint32_t i, k;
    uint32_t sp, pc;

    /*
     * EL UNICO AVISO POSIBLE: EL LED - 01/10/2026.
     *
     * *** El dueno: "en modo dfu no deberia de poner algo en pantalla?". ***
     * Deberia, pero no se puede, y el motivo es el mismo que hace que esto
     * funcione: la ROM de fabrica exige llegar con el reloj como lo deja un
     * reset y sin periféricos montados. Encender el panel aqui dentro
     * significaria EXMC, la secuencia de 374 pasos y el PLL a 192 MHz, o
     * sea deshacer las cuatro condiciones que costaron doce versiones.
     *
     * Asi que el aviso va por donde no estorba: PA8, el LED de la radio
     * (led_gpio_init(), main.c 20906). Cuatro parpadeos y se queda
     * encendido. No necesita reloj, ni panel, ni memoria: son dos
     * registros.
     *
     * Y va a PARPADEAR, no a encenderse, a proposito. La polaridad del LED
     * no la tengo confirmada -el de serie lo pone a 0 al arrancar, que
     * supongo que es apagado, pero suponer es lo que me ha costado media
     * tarde hoy-. Un parpadeo se ve igual de bien si estaba encendido que
     * si estaba apagado: lo que se nota es el cambio, no el nivel. Si la
     * polaridad resulta ser la otra, el aviso sigue valiendo.
     */
    REG32(0x40023830UL) |= (1UL << 0);                 /* reloj de GPIOA */
    for (i = 0U; i < 100U; i++) { __NOP(); }
    REG32(0x40020000UL) = (REG32(0x40020000UL) & ~(3UL << 16)) | (1UL << 16);
    for (k = 0U; k < 4U; k++) {
        REG32(0x40020018UL) = (1UL << 8);             /* PA8 a 1 */
        for (i = 0U; i < 400000U; i++) { __NOP(); }
        REG32(0x40020018UL) = (1UL << (8 + 16));      /* PA8 a 0 */
        for (i = 0U; i < 400000U; i++) { __NOP(); }
    }
    REG32(0x40020018UL) = (1UL << 8);                 /* y se queda asi */

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

    /* Reset del USBFS (AHB2RST / AHB2EN, bit 7). El reloj NO se toca. */
    REG32(0x40023834UL) |= (1UL << 7);
    REG32(0x40023814UL) |= (1UL << 7);
    for (i = 0U; i < 2000U; i++) { __NOP(); }
    REG32(0x40023814UL) &= ~(1UL << 7);
    REG32(0x40023834UL) &= ~(1UL << 7);

    /* La ROM en cero: SYSCFG_CFG0 BOOT_MODE = 001. */
    REG32(0x40023844UL) |= (1UL << 14);
    REG32(0x40013800UL) = (REG32(0x40013800UL) & ~7UL) | 1UL;
    __DSB();

    /* Y los vectores tambien en cero. */
    SCB->VTOR = 0UL;
    __DSB();
    __ISB();

    /* DP (PA12) a masa un buen rato, para que el PC vea una desconexion de
     * verdad y vuelva a enumerar. A 16 MHz, 3 ciclos por vuelta: 1.600.000
     * vueltas son unos 300 ms. */
    REG32(0x40023830UL) |= (1UL << 0);
    REG32(0x40020000UL) = (REG32(0x40020000UL) & ~(3UL << 24)) | (1UL << 24);
    REG32(0x40020018UL) = (1UL << (12 + 16));
    for (i = 0U; i < 1600000U; i++) { __NOP(); }
    REG32(0x40020000UL) &= ~(3UL << 24);

    sp = *(volatile uint32_t *)0x1FFF0000UL;
    pc = *(volatile uint32_t *)0x1FFF0004UL;
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();
    while (1) { }
}

void boot_atajo_dfu(void)
{
    atajo_t a;
    atajo_r r;
    volatile uint32_t i;

    /* Relojes de GPIOC y GPIOD, y los tres pines como entrada con su
     * resistencia. Igual que encoder_init(), pero a pelo. */
    RCU_AHB1EN_R |= (1UL << 2) | (1UL << 3);
    for (i = 0U; i < 100U; i++) { __NOP(); }     /* el reloj asentandose */

    CTL(GPIOC_R) &= ~(3UL << (9 * 2));
    PUD(GPIOC_R)  = (PUD(GPIOC_R) & ~(3UL << (9 * 2)))  | (2UL << (9 * 2));
    CTL(GPIOD_R) &= ~((3UL << (12 * 2)) | (3UL << (13 * 2)));
    PUD(GPIOD_R)  = (PUD(GPIOD_R) & ~((3UL << (12 * 2)) | (3UL << (13 * 2))))
                  | ((1UL << (12 * 2)) | (1UL << (13 * 2)));

    /* Que las resistencias tiren de las lineas antes de creerse la primera
     * lectura. Sin esto, el estado inicial seria ruido y contaria como un
     * paso de regalo. */
    respiro();

    r = atajo_empieza(&a, boton(), lee_ab());

    while (r == ATAJO_SIGUE) {
        respiro();
        r = atajo_muestra(&a, boton(), lee_ab());
    }

    if (r == ATAJO_SI) { salta_a_la_rom(); }      /* no vuelve */
}

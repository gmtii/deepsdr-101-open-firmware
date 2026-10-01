/*!
    \file  system_gd32f4xx.c
    \brief CMSIS Cortex-M4 Device Peripheral Access Layer Source File for
           GD32F4xx Device Series
*/

/* Copyright (c) 2012 ARM LIMITED

   All rights reserved.
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
   - Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
   - Neither the name of ARM nor the names of its contributors may be used
     to endorse or promote products derived from this software without
     specific prior written permission.
   *
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
   ---------------------------------------------------------------------------*/

/* This file refers the CMSIS standard, some adjustments are made according to GigaDevice chips */

#include "gd32f4xx.h"

/* system frequency define */
#define __IRC16M          (IRC16M_VALUE)            /* internal 16 MHz RC oscillator frequency */
#define __HXTAL           (HXTAL_VALUE)             /* high speed crystal oscillator frequency */
#define __SYS_OSC_CLK     (__IRC16M)                /* main oscillator frequency */

/* select a system clock by uncommenting the following line */
//#define __SYSTEM_CLOCK_IRC16M                   (uint32_t)(__IRC16M)
//#define __SYSTEM_CLOCK_HXTAL                    (uint32_t)(__HXTAL)
//#define __SYSTEM_CLOCK_120M_PLL_IRC16M          (uint32_t)(120000000)
//#define __SYSTEM_CLOCK_120M_PLL_8M_HXTAL        (uint32_t)(120000000)
//#define __SYSTEM_CLOCK_120M_PLL_25M_HXTAL       (uint32_t)(120000000)
//#define __SYSTEM_CLOCK_168M_PLL_IRC16M          (uint32_t)(168000000)
//#define __SYSTEM_CLOCK_168M_PLL_8M_HXTAL        (uint32_t)(168000000)
//#define __SYSTEM_CLOCK_168M_PLL_25M_HXTAL       (uint32_t)(168000000)
//#define __SYSTEM_CLOCK_200M_PLL_IRC16M          (uint32_t)(200000000)
//#define __SYSTEM_CLOCK_200M_PLL_8M_HXTAL        (uint32_t)(200000000)
#define __SYSTEM_CLOCK_200M_PLL_25M_HXTAL       (uint32_t)(200000000)
//#define __SYSTEM_CLOCK_240M_PLL_IRC16M          (uint32_t)(240000000)
//#define __SYSTEM_CLOCK_240M_PLL_8M_HXTAL        (uint32_t)(240000000)
//#define __SYSTEM_CLOCK_240M_PLL_25M_HXTAL       (uint32_t)(240000000)

#define SEL_IRC16M      0x00U
#define SEL_HXTAL       0x01U
#define SEL_PLLP        0x02U
#define RCU_MODIFY      {volatile uint32_t i; \
                         RCU_CFG0 |= RCU_AHB_CKSYS_DIV2; \
                         for(i=0;i<50000;i++); \
                         RCU_CFG0 |= RCU_AHB_CKSYS_DIV4; \
                         for(i=0;i<50000;i++);}
                        
/* set the system clock frequency and declare the system clock configuration function */
#ifdef __SYSTEM_CLOCK_IRC16M
uint32_t SystemCoreClock = __SYSTEM_CLOCK_IRC16M;
static void system_clock_16m_irc16m(void);
#elif defined (__SYSTEM_CLOCK_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_HXTAL;
static void system_clock_hxtal(void);
#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC16M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_120M_PLL_IRC16M;
static void system_clock_120m_irc16m(void);
#elif defined (__SYSTEM_CLOCK_120M_PLL_8M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_120M_PLL_8M_HXTAL;
static void system_clock_120m_8m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_120M_PLL_25M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_120M_PLL_25M_HXTAL;
static void system_clock_120m_25m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_168M_PLL_IRC16M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_168M_PLL_IRC16M;
static void system_clock_168m_irc16m(void);
#elif defined (__SYSTEM_CLOCK_168M_PLL_8M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_168M_PLL_8M_HXTAL;
static void system_clock_168m_8m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_168M_PLL_25M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_168M_PLL_25M_HXTAL;
static void system_clock_168m_25m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_200M_PLL_IRC16M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_200M_PLL_IRC16M;
static void system_clock_200m_irc16m(void);
#elif defined (__SYSTEM_CLOCK_200M_PLL_8M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_200M_PLL_8M_HXTAL;
static void system_clock_200m_8m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_200M_PLL_25M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_200M_PLL_25M_HXTAL;
static void system_clock_200m_25m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_240M_PLL_IRC16M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_240M_PLL_IRC16M;
static void system_clock_240m_irc16m(void);
#elif defined (__SYSTEM_CLOCK_240M_PLL_8M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_240M_PLL_8M_HXTAL;
static void system_clock_240m_8m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_240M_PLL_25M_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_240M_PLL_25M_HXTAL;
static void system_clock_240m_25m_hxtal(void);

#endif /* __SYSTEM_CLOCK_IRC16M */

/* configure the system clock */
static void system_clock_config(void);

/*!
    \brief      setup the microcontroller system, initialize the system
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SystemInit (void)
{
#ifdef CARGADOR_ARRANQUE
    /*
     * LA SALIDA DE EMERGENCIA DEL CARGADOR, Y VA LA PRIMERA DE TODO.
     *
     * Mando pulsado Y girandolo = saltar a la ROM de fabrica. Tiene que
     * estar AQUI, antes de tocar el reloj, porque la ROM mide el cristal
     * desde el estado de reset (la leccion de la V2.42). Y antes de main(),
     * porque su oficio es funcionar aunque el resto del cargador este roto.
     *
     * Ver boot/atajo_dfu.c, que lo explica entero.
     */
    {
        extern void boot_atajo_dfu(void);
        boot_atajo_dfu();
    }
#endif

    /*
     * Y ANTES QUE TODO ESO: ¿venimos de pedir el modo DFU? - 01/10/2026.
     *
     * *** Cinco versiones fallando porque la comprobacion estaba en main().
     * El aviso que lo explica: "the built-in bootloader DEPENDS ON AN
     * EXTERNAL CRYSTAL (HXTAL) ... the internal bootloader AUTO-DETECTS
     * based on specific expected frequencies". ***
     *
     * La ROM mide el cristal para sacar sus 48 MHz de USB, y eso lo hace
     * desde el estado de reset. En main() ya es tarde: SystemInit() ha
     * arrancado el HXTAL de 12,288 MHz y subido el PLL a 199,68. rcu_deinit()
     * devuelve los REGISTROS a su sitio, pero el cristal lleva rato
     * oscilando y la deteccion no se comporta igual. Con BOOT0 el chip llega
     * virgen de verdad, y por eso por ahi si entra.
     *
     * Asi que el salto va aqui, en la primera instruccion util del firmware:
     * IRC16M, PLL apagado, HXTAL parado. Exactamente lo que la ROM espera.
     *
     * TODO a registro pelado, sin llamar a la libreria: en este punto el
     * arranque todavia no ha copiado .data, asi que cuanto menos dependa de
     * nada, mejor. El testigo vive en .noinit (ver dfu_salto.c y el linker
     * script), que ni se copia ni se borra.
     */
#ifndef CARGADOR_ARRANQUE
    /*
     * ESTE BLOQUE ES SOLO DE LA APLICACION - 01/10/2026.
     *
     * Este fichero lo comparten los DOS programas, la aplicacion y el
     * cargador de arranque (boot/Makefile). El testigo g_dfu_testigo vive en
     * User/dfu_salto.c, que solo entra en la aplicacion; compilando el
     * cargador este bloque dejaba el enlazado en "undefined reference".
     *
     * Y ademas no debe estar ahi: el cargador es el PRIMERO que corre tras
     * el reset, asi que si mirase el testigo saltaria el a la ROM y la
     * aplicacion no llegaria a verlo nunca. El camino bueno -y el que ya
     * esta probado en la radio- es que lo mire la aplicacion.
     */
    {
        extern uint32_t g_dfu_testigo;              /* .noinit, en dfu_salto.c */

        if (g_dfu_testigo == 0xDF0DF0DFUL) {
            volatile uint32_t i;
            uint32_t sp, pc;

            g_dfu_testigo = 0UL;

            /*
             * EL ORDEN, QUE SALE DE TRES SITIOS QUE DICEN LO MISMO.
             *
             * El hilo de 2014 "jumping to the bootloader via software does
             * not permit dfu mode" lo resume en tres condiciones:
             *
             *   1. la ROM mapeada en la direccion CERO, que es "where the
             *      processor loads vectors during boot"
             *   2. corriendo desde el oscilador interno
             *   3. sin interrupciones ni perifericos configurados
             *
             * Y el articulo de ST da el orden exacto: interrupciones fuera,
             * SysTick fuera, relojes a cero, NVIC limpio, interrupciones
             * OTRA VEZ DENTRO, MSP y salto.
             *
             * LO QUE FALTABA, Y ES LA PRIMERA CONDICION BIEN LEIDA: **VTOR**.
             * Arrancando con el puente vale 0 -su valor de reset- y la ROM
             * esta en cero, asi que sus interrupciones van a SU tabla. Al
             * saltar nosotros vale 0x08000000, que lo dejo puesto el gestor
             * de arranque. La ROM levanta su USB, llega el ENUMF, y la
             * interrupcion se va a la tabla DEL GESTOR. Su manejador nunca
             * corre, nunca pone el byte de 0x20000162, y se queda en el
             * bucle de los USART. Que es exactamente el sintoma.
             */
            __disable_irq();
            SysTick->CTRL = 0UL;
            SysTick->LOAD = 0UL;
            SysTick->VAL  = 0UL;

            /* El gestor dejo su interrupcion del USB habilitada: fuera. */
            for (i = 0U; i < 8U; i++) {
                NVIC->ICER[i] = 0xFFFFFFFFUL;
                NVIC->ICPR[i] = 0xFFFFFFFFUL;
            }
            __DSB();
            __ISB();

            /* Deshacer el USB del gestor: reset del USBFS (AHB2RST bit 7).
             * El reloj NO se toca: aqui todavia es el de reset, que es la
             * condicion 2. */
            REG32(0x40023834UL) |= (1UL << 7);
            REG32(0x40023814UL) |= (1UL << 7);
            for (i = 0U; i < 2000U; i++) { __NOP(); }
            REG32(0x40023814UL) &= ~(1UL << 7);
            REG32(0x40023834UL) &= ~(1UL << 7);

            /* Condicion 1: la ROM en cero. SYSCFG_CFG0 BOOT_MODE = 001. */
            REG32(0x40023844UL) |= (1UL << 14);
            REG32(0x40013800UL) = (REG32(0x40013800UL) & ~7UL) | 1UL;
            __DSB();

            /* Y los vectores TAMBIEN en cero, que es lo que faltaba. */
            SCB->VTOR = 0UL;
            __DSB();
            __ISB();

            /* DP (PA12) a masa 250 ms, para que el PC vea desaparecer lo que
             * el gestor hubiera enumerado y vuelva a enumerar de cero. */
            REG32(0x40023830UL) |= (1UL << 0);
            REG32(0x40020000UL) = (REG32(0x40020000UL) & ~(3UL << 24)) | (1UL << 24);
            REG32(0x40020018UL) = (1UL << (12 + 16));
            for (i = 0U; i < 1400000U; i++) { __NOP(); }
            REG32(0x40020000UL) &= ~(3UL << 24);

            sp = *(volatile uint32_t *)0x1FFF0000UL;
            pc = *(volatile uint32_t *)0x1FFF0004UL;
            __set_MSP(sp);
            __enable_irq();          /* el articulo de ST lo pide aqui */
            ((void (*)(void))pc)();
            while (1) { }
        }
        g_dfu_testigo = 0UL;
    }
#endif /* CARGADOR_ARRANQUE */

    /* CRITICO: esto tiene que ser lo PRIMERISIMO, antes de tocar nada
     * del reloj. Vivimos encadenados tras el bootloader (que salta aqui
     * con una llamada normal, no un reset de hardware), asi que VTOR
     * sigue apuntando a la tabla de vectores DEL BOOTLOADER hasta que
     * lo arreglemos. Si algo de la reconfiguracion del reloj de abajo
     * dispara una excepcion/fallo, se gestionaria con el manejador
     * DEL BOOTLOADER, no el nuestro - probablemente colgandose en
     * silencio sin que veamos nada, ni por UART. */
#ifdef CARGADOR_ARRANQUE
    SCB->VTOR = 0x08000000;     /* el cargador: su tabla esta en la base */
#else
    SCB->VTOR = 0x08020000;
#endif

    /* FPU settings */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  /* set CP10 and CP11 Full Access */
#endif
    /* Reset the RCU clock configuration to the default reset state */
    /* Set IRC16MEN bit */
    RCU_CTL |= RCU_CTL_IRC16MEN;

    RCU_MODIFY
    
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    
    /* Reset HXTALEN, CKMEN and PLLEN bits */
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);

    /* Reset HSEBYP bit */
    RCU_CTL &= ~(RCU_CTL_HXTALBPS);
    
    /* Reset CFG0 register */
    RCU_CFG0 = 0x00000000U;

    /* wait until IRC16M is selected as system clock */
    while(0 != (RCU_CFG0 & RCU_SCSS_IRC16M)){
    }

    /* Reset PLLCFGR register */
    RCU_PLL = 0x24003010U;

    /* Disable all interrupts */
    RCU_INT = 0x00000000U;
         
    /* Configure the System clock source, PLL Multiplier and Divider factors, 
        AHB/APBx prescalers and Flash settings */
    system_clock_config();
}
/*!
    \brief      configure the system clock
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_config(void)
{
#ifdef __SYSTEM_CLOCK_IRC16M
    system_clock_16m_irc16m();
#elif defined (__SYSTEM_CLOCK_HXTAL)
    system_clock_hxtal();
#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC16M)
    system_clock_120m_irc16m();
#elif defined (__SYSTEM_CLOCK_120M_PLL_8M_HXTAL)
    system_clock_120m_8m_hxtal();
#elif defined (__SYSTEM_CLOCK_120M_PLL_25M_HXTAL)
    system_clock_120m_25m_hxtal();
#elif defined (__SYSTEM_CLOCK_168M_PLL_IRC16M)
    system_clock_168m_irc16m();
#elif defined (__SYSTEM_CLOCK_168M_PLL_8M_HXTAL)
    system_clock_168m_8m_hxtal();
#elif defined (__SYSTEM_CLOCK_168M_PLL_25M_HXTAL)
    system_clock_168m_25m_hxtal();
#elif defined (__SYSTEM_CLOCK_200M_PLL_IRC16M)
    system_clock_200m_irc16m();
#elif defined (__SYSTEM_CLOCK_200M_PLL_8M_HXTAL)
    system_clock_200m_8m_hxtal();
#elif defined (__SYSTEM_CLOCK_200M_PLL_25M_HXTAL)
    system_clock_200m_25m_hxtal();
#elif defined (__SYSTEM_CLOCK_240M_PLL_IRC16M)
    system_clock_240m_irc16m();
#elif defined (__SYSTEM_CLOCK_240M_PLL_8M_HXTAL)
    system_clock_240m_8m_hxtal();
#elif defined (__SYSTEM_CLOCK_240M_PLL_25M_HXTAL)
    system_clock_240m_25m_hxtal();
#endif /* __SYSTEM_CLOCK_IRC16M */   
}

#ifdef __SYSTEM_CLOCK_IRC16M
/*!
    \brief      configure the system clock to 16M by IRC16M
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_16m_irc16m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC16M */
    RCU_CTL |= RCU_CTL_IRC16MEN;
    
    /* wait until IRC16M is stable or the startup time is longer than IRC16M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC16MSTB);
    }while((0U == stab_flag) && (IRC16M_STARTUP_TIMEOUT != timeout));
    
    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC16MSTB)){
        while(1){
        }
    }
    
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV1;
    
    /* select IRC16M as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_IRC16M;
    
    /* wait until IRC16M is selected as system clock */
    while(0 != (RCU_CFG0 & RCU_SCSS_IRC16M)){
    }
}

#elif defined (__SYSTEM_CLOCK_HXTAL)
/*!
    \brief      configure the system clock to HXTAL
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;
    
    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));
    
    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
    
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV1;
    
    /* select HXTAL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_HXTAL;
    
    /* wait until HXTAL is selected as system clock */
    while(0 == (RCU_CFG0 & RCU_SCSS_HXTAL)){
    }
}

#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC16M)
/*!
    \brief      configure the system clock to 120M by PLL which selects IRC16M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_120m_irc16m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC16M */
    RCU_CTL |= RCU_CTL_IRC16MEN;

    /* wait until IRC16M is stable or the startup time is longer than IRC16M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC16MSTB);
    }while((0U == stab_flag) && (IRC16M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC16MSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC16M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 16, PLL_N = 240, PLL_P = 2, PLL_Q = 5 */ 
    RCU_PLL = (16U | (240U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_IRC16M) | (5U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 120 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_120M_PLL_8M_HXTAL)
/*!
    \brief      configure the system clock to 120M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_120m_8m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 8, PLL_N = 240, PLL_P = 2, PLL_Q = 5 */ 
    RCU_PLL = (8U | (240U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (5U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 120 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_120M_PLL_25M_HXTAL)
/*!
    \brief      configure the system clock to 120M by PLL which selects HXTAL(25M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_120m_25m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 25, PLL_N = 240, PLL_P = 2, PLL_Q = 5 */ 
    RCU_PLL = (25U | (240U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (5U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 120 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_168M_PLL_IRC16M)
/*!
    \brief      configure the system clock to 168M by PLL which selects IRC16M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_168m_irc16m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC16M */
    RCU_CTL |= RCU_CTL_IRC16MEN;

    /* wait until IRC16M is stable or the startup time is longer than IRC16M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC16MSTB);
    }while((0U == stab_flag) && (IRC16M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC16MSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC16M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 16, PLL_N = 336, PLL_P = 2, PLL_Q = 7 */ 
    RCU_PLL = (16U | (336U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_IRC16M) | (7U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 168 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_168M_PLL_8M_HXTAL)
/*!
    \brief      configure the system clock to 168M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_168m_8m_hxtal(void)
{
    uint32_t timeout = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    while((0U == (RCU_CTL & RCU_CTL_HXTALSTB)) && (HXTAL_STARTUP_TIMEOUT != timeout++)){
    }

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;
    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 8, PLL_N = 336, PLL_P = 2, PLL_Q = 7 */ 
    RCU_PLL = (8U | (336 << 6U) | (((2 >> 1U) -1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (7 << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
  
    /* Enable the high-drive to extend the clock frequency to 168 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_168M_PLL_25M_HXTAL)
/*!
    \brief      configure the system clock to 168M by PLL which selects HXTAL(25M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_168m_25m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 25, PLL_N = 336, PLL_P = 2, PLL_Q = 7 */ 
    RCU_PLL = (25U | (336U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (7U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 168 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_200M_PLL_IRC16M)
/*!
    \brief      configure the system clock to 200M by PLL which selects IRC16M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_200m_irc16m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC16M */
    RCU_CTL |= RCU_CTL_IRC16MEN;

    /* wait until IRC16M is stable or the startup time is longer than IRC16M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC16MSTB);
    }while((0U == stab_flag) && (IRC16M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC16MSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC16M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 16, PLL_N = 400, PLL_P = 2, PLL_Q = 9 */ 
    RCU_PLL = (16U | (400U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_IRC16M) | (9U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 200 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_200M_PLL_8M_HXTAL)
/*!
    \brief      configure the system clock to 200M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_200m_8m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 8, PLL_N = 400, PLL_P = 2, PLL_Q = 9 */ 
    RCU_PLL = (8U | (400U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (9U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 200 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_200M_PLL_25M_HXTAL)
/*!
    \brief      configure the system clock to 200M by PLL which selects HXTAL(25M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_200m_25m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /*
     * PSC was originally 25, intended for a 25MHz crystal, but this
     * board uses a real 12.288MHz crystal - comparison frequency =
     * 12.288/25 = 491.5kHz, well outside the healthy 1-2MHz range.
     * With PSC=8: comparison = 12.288/8 = 1.536MHz (in range).
     * PLL_N=260, PLL_P=2 -> VCO=399.36MHz (within 100-432MHz),
     * SYSCLK~=199.68MHz (just under the 200MHz maximum). PLL_Q=8
     * (approximate - USB is not used in this project).
     *
     * IMPORTANT: PSC is the same field used by PLLI2S as its input
     * divider (rcu_plli2s_config reads RCU_PLL_PLLPSC) - see the
     * matching change in gd32_i2s.c (N/R recalculated to keep the
     * same i2sclock) whenever this value changes.
     *
     * Flash wait-states are intentionally left untouched: per the
     * datasheet, this device operates at 200MHz with 0 flash wait
     * states (a different flash architecture from the equivalent
     * STM32F4 parts), and WSCNT=0 is exactly the reset value already
     * in use.
     */
#ifdef CARGADOR_ARRANQUE
    /*
     * EL CARGADOR VA A 192 MHz, NO A 199,68, Y ES POR EL USB - 01/10/2026.
     *
     * *** El dueno: "sigue sin montar el usb". *** Y el motivo estaba
     * escrito aqui mismo, tres lineas mas arriba, desde que se puso este
     * reloj: "PLL_Q=8 (approximate - USB is NOT USED in this project)".
     * Ya si se usa: el cargador monta un disco USB.
     *
     * La cuenta, que no es una teoria:
     *
     *   cristal 12,288 MHz / PSC 8 = 1,536 MHz
     *   1,536 x N=260 = VCO 399,36 MHz
     *   VCO / P=2 = 199,68 MHz de sistema
     *   VCO / Q=8 = 49,92 MHz para el USB        <-- y tienen que ser 48
     *
     * 49,92 son un +4,0%. La norma de USB full-speed le da al dispositivo
     * un +-0,25%. O sea que el reloj de bits va dieciseis veces fuera de
     * tolerancia: el host ve que se engancha algo -de ahi el "pin" de
     * Windows- pero no hay manera de que una trama llegue bien, asi que la
     * enumeracion no termina y no sale ninguna unidad. Exactamente lo que
     * se ve.
     *
     * Con N=250 en vez de 260:
     *
     *   1,536 x 250 = VCO 384,00 MHz   (dentro de 100-432)
     *   / P=2 = 192,00 MHz de sistema
     *   / Q=8 =  48,00 MHz EXACTOS para el USB
     *
     * Son 7,68 MHz menos de CPU. A un cargador le sobran: lo unico que
     * hace es pintar cuatro lineas, leer una flash por SPI y copiar 320 kB.
     * La aplicacion sigue a 199,68 porque ella no usa el USB y si usa cada
     * MHz. Por eso esto va bajo el macro y no toca el otro camino.
     */
    /*
     * Y AL FINAL, EL RELOJ DEL DE SERIE TAL CUAL - 01/10/2026.
     *
     * Puse 192 MHz porque daban un USB de 48,000 exactos y me parecio
     * mejor que lo que yo creia que hacia el original. Pero desensamblada
     * su configuracion (0x08003E74, la estructura que le pasa a su HAL):
     *
     *   PLLM = 8   PLLN = 188   PLLP = 2   PLLQ = 6
     *   12,288/8 = 1,536 -> VCO 288,768 -> sistema 144,384, USB 48,128
     *
     * Su USB va a +0,27% y funciona; el nuestro iba perfecto y tambien.
     * Pero el reloj no solo manda en el USB: manda en CADA ciclo del bus
     * del panel, en los retardos y en los tiempos de establecimiento de
     * los 374 pasos de su secuencia de arranque.
     *
     * Y mientras nuestro reloj fuera otro, todo lo que copiaramos de el
     * habria que TRADUCIRLO, y cada traduccion es una aproximacion mia
     * mas. Ya llevo demasiadas hoy. Con el mismo reloj, sus numeros son
     * nuestros numeros y no hay nada que interpretar.
     *
     * La aplicacion sigue a 199,68: esto va solo bajo el macro.
     */
    RCU_PLL = (8U | (188U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (6U << 24U));
    SystemCoreClock = 144384000U;
#else
    RCU_PLL = (8U | (260U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (8U << 24U));
#endif

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 200 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_240M_PLL_IRC16M)
/*!
    \brief      configure the system clock to 240M by PLL which selects IRC16M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_240m_irc16m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC16M */
    RCU_CTL |= RCU_CTL_IRC16MEN;

    /* wait until IRC16M is stable or the startup time is longer than IRC16M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC16MSTB);
    }while((0U == stab_flag) && (IRC16M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC16MSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC16M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 16, PLL_N = 480, PLL_P = 2, PLL_Q = 10 */ 
    RCU_PLL = (16U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_IRC16M) | (10U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 240 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_240M_PLL_8M_HXTAL)
/*!
    \brief      configure the system clock to 240M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_240m_8m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 8, PLL_N = 480, PLL_P = 2, PLL_Q = 10 */ 
    RCU_PLL = (8U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (10U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 240 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}

#elif defined (__SYSTEM_CLOCK_240M_PLL_25M_HXTAL)
/*!
    \brief      configure the system clock to 240M by PLL which selects HXTAL(25M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_240m_25m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
         
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 25, PLL_N = 480, PLL_P = 2, PLL_Q = 10 */ 
    RCU_PLL = (25U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) |
                   (RCU_PLLSRC_HXTAL) | (10U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* Enable the high-drive to extend the clock frequency to 240 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLLP)){
    }
}
#endif /* __SYSTEM_CLOCK_IRC16M */
/*!
    \brief      update the SystemCoreClock with current core clock retrieved from cpu registers
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SystemCoreClockUpdate(void)
{
    uint32_t sws;
    uint32_t pllpsc, plln, pllsel, pllp, ck_src, idx, clk_exp;
    
    /* exponent of AHB, APB1 and APB2 clock divider */
    const uint8_t ahb_exp[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};

    sws = GET_BITS(RCU_CFG0, 2, 3);
    switch(sws){
    /* IRC16M is selected as CK_SYS */
    case SEL_IRC16M:
        SystemCoreClock = IRC16M_VALUE;
        break;
    /* HXTAL is selected as CK_SYS */
    case SEL_HXTAL:
        SystemCoreClock = HXTAL_VALUE;
        break;
    /* PLLP is selected as CK_SYS */
    case SEL_PLLP:
        /* get the value of PLLPSC[5:0] */
        pllpsc = GET_BITS(RCU_PLL, 0U, 5U);
        plln = GET_BITS(RCU_PLL, 6U, 14U);
        pllp = (GET_BITS(RCU_PLL, 16U, 17U) + 1U) * 2U;
        /* PLL clock source selection, HXTAL or IRC8M/2 */
        pllsel = (RCU_PLL & RCU_PLL_PLLSEL);
        if (RCU_PLLSRC_HXTAL == pllsel) {
            ck_src = HXTAL_VALUE;
        } else {
            ck_src = IRC16M_VALUE;
        }
        SystemCoreClock = ((ck_src / pllpsc) * plln) / pllp;
        break;
    /* IRC16M is selected as CK_SYS */
    default:
        SystemCoreClock = IRC16M_VALUE;
        break;
    }
    /* calculate AHB clock frequency */
    idx = GET_BITS(RCU_CFG0, 4, 7);
    clk_exp = ahb_exp[idx];
    SystemCoreClock = SystemCoreClock >> clk_exp;
}

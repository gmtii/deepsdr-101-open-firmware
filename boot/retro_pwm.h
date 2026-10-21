/*
 * EL PWM DE LA RETROILUMINACION, A REGISTRO - 20/10/2026.
 *
 * TIMER4 canal 3 sobre PA3 en AF2, prescaler 7, periodo 375, pulso 188,
 * PWM0, polaridad alta. Son los mismos numeros de siempre; lo que cambia
 * es quien los escribe.
 *
 * POR QUE NO SE LLAMA A LA BIBLIOTECA DEL FABRICANTE. timer_deinit(),
 * timer_struct_para_init(), timer_channel_output_struct_para_init(),
 * timer_channel_output_config() y las de modo y pulso llevan dentro un
 * switch con los catorce temporizadores o con los cuatro canales, y el
 * enlazador se las trae enteras aunque aqui solo se use un canal de un
 * temporizador: 896 bytes. El cargador vive en 32 KB y se pasaba de 136.
 *
 * LO DE ABAJO NO ESTA DEDUCIDO, ESTA LEIDO de
 * Firmware/Source/gd32f4xx_timer.c. La rama de TIMER0/TIMER7 (CH3NEN,
 * ISO3) no entra porque TIMER4 no es ninguno de los dos, y la polaridad
 * ALTA vale cero, asi que el bit se limpia y no se vuelve a poner.
 *
 * Y NO SE FIA DE QUE YO LO LEYERA BIEN: sim/bootpwm.c compila el fichero
 * del fabricante tal cual en el PC, contra un mapa de registros de
 * mentira, ejecuta las dos secuencias y exige que dejen los mismos
 * registros con los mismos valores. Por eso esto vive en una cabecera y
 * no suelto dentro de boot_main.c: para que el banco mida ESTE codigo y
 * no una copia suya.
 *
 * gd32_timer_init(), timer_auto_reload_shadow_enable() y timer_enable()
 * se quedan como llamadas: usb_hw.c ya las usa para el TIMER2, o sea que
 * entran de todas formas y no cuestan nada.
 */
#ifndef RETRO_PWM_H
#define RETRO_PWM_H

#include "gd32f4xx.h"

static void retro_pwm_timer4(void)
{
    timer_parameter_struct t;

    /* timer_deinit(TIMER4) */
    rcu_periph_reset_enable(RCU_TIMER4RST);
    rcu_periph_reset_disable(RCU_TIMER4RST);

    /* los seis campos se ponen todos: timer_struct_para_init() sobra */
    t.prescaler         = 7U;
    t.alignedmode       = TIMER_COUNTER_EDGE;
    t.counterdirection  = TIMER_COUNTER_UP;
    t.clockdivision     = TIMER_CKDIV_DIV1;
    t.period            = 375U;
    t.repetitioncounter = 0U;
    gd32_timer_init(TIMER4, &t);

    /* timer_channel_output_config(TIMER4, CH_3, salida ON, polaridad ALTA) */
    TIMER_CHCTL2(TIMER4) &= ~(uint32_t)TIMER_CHCTL2_CH3EN;
    TIMER_CHCTL1(TIMER4) &= ~(uint32_t)TIMER_CHCTL1_CH3MS;
    TIMER_CHCTL2(TIMER4) |= (uint32_t)TIMER_CCX_ENABLE << 12U;
    TIMER_CHCTL2(TIMER4) &= ~(uint32_t)TIMER_CHCTL2_CH3P;

    /* timer_channel_output_mode_config(TIMER4, CH_3, TIMER_OC_MODE_PWM0) */
    TIMER_CHCTL1(TIMER4) &= ~(uint32_t)TIMER_CHCTL1_CH3COMCTL;
    TIMER_CHCTL1(TIMER4) |= (uint32_t)TIMER_OC_MODE_PWM0 << 8U;

    /* timer_channel_output_pulse_value_config(TIMER4, CH_3, 188) */
    TIMER_CH3CV(TIMER4) = 188U;

    timer_auto_reload_shadow_enable(TIMER4);
    timer_enable(TIMER4);
}

#endif

#include "battery.h"
#include "gd32f4xx.h"

/*
 * Assumed pack range for battery_get_percent()'s linear mapping (see
 * battery.h) - a single-cell Li-ion/Li-Po as a starting placeholder,
 * NOT confirmed against this board's actual battery. Change these two
 * once known.
 */
#define BATTERY_EMPTY_MV 3000U
#define BATTERY_FULL_MV  4200U

void battery_init(void)
{
    rcu_periph_clock_enable(RCU_ADC0);

    /* PCLK2 (APB2) = 100MHz on this board's clock tree (AHB/2 off a
     * 200MHz core - see system_gd32f4xx.c's system_clock_200m_25m_hxtal(),
     * the function actually selected by this project's __SYSTEM_CLOCK_*
     * define). DIV4 = 25MHz, under the GD32F450 datasheet's 30MHz ADC
     * clock ceiling. Not latency-critical (a battery gauge polled from
     * the main loop), so there's no reason to push closer to the limit. */
    adc_clock_config(ADC_ADCCK_PCLK2_DIV4);

    adc_resolution_config(ADC0, ADC_RESOLUTION_12B);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);

    /* Single conversion, software-triggered, one regular channel - no
     * scan/continuous/DMA, this isn't a streaming source like sdr_rx's
     * I/Q path. */
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL, 1U);
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, EXTERNAL_TRIGGER_DISABLE);

    /* Route channel 18 to VBAT/4 internally (ADC_SYNCCTL_VBATEN - see
     * battery.h's comment on the divider). The VBAT channel needs a
     * long sample time per the datasheet (it's a higher-impedance
     * internal path than a regular external pin) - use the longest
     * available, 480 cycles, since accuracy matters far more than
     * speed for a housekeeping reading like this. */
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);

    adc_enable(ADC0);
    /* Datasheet-mandated stabilization delay after enabling the ADC
     * and before calibration. This only runs once at boot, so a crude
     * cycle-counting busy loop is fine here - same reasoning the
     * project already applies to its other uncalibrated delay loops
     * (see touch.c's delay_us_approx()), just without even bothering
     * to name it: a few thousand NOPs is a healthy multiple of the
     * datasheet's microsecond-scale requirement at this core clock. */
    {
        volatile uint32_t d;
        for (d = 0; d < 2000U; d++) {
            __NOP();
        }
    }
    adc_calibration_enable(ADC0);
}

uint16_t battery_get_millivolts(void)
{
    uint16_t raw;
    uint32_t adc_mv;
    uint32_t vbat_pin_mv;

    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    while (SET != adc_flag_get(ADC0, ADC_FLAG_EOC)) {
        /* Blocking - one conversion at 480 sample cycles + 12
         * successive-approximation cycles is well under a millisecond
         * even at this ADC clock, and battery_get_millivolts() is only
         * ever called from the main loop (see battery.h), never the
         * demod ISR - spinning here is fine. */
    }
    raw = adc_regular_data_read(ADC0);
    adc_flag_clear(ADC0, ADC_FLAG_EOC);

    /* raw (12-bit, 0-4095) -> millivolts AT THE ADC PIN (i.e. VBAT/4,
     * post internal divider), against the MCU's 3.3V rail as
     * reference. NOT VREFINT-calibrated, so this inherits whatever
     * tolerance that rail has - fine for a gauge, not for a lab
     * measurement. */
    adc_mv = ((uint32_t)raw * 3300UL) / 4095UL;

    /* Undo the internal /4 VBAT divider to get the voltage AT THE
     * VBAT PIN, then add back the series diode's forward drop (see
     * battery.h's DIODE DROP COMPENSATION comment) to approximate the
     * actual battery terminal voltage. */
    vbat_pin_mv = adc_mv * 4UL;
    return (uint16_t)(vbat_pin_mv + BATTERY_DIODE_DROP_MV);
}

int16_t battery_get_chip_temp_c(void)
{
    uint16_t raw;
    int32_t  mv, t_x10;

    /*
     * El canal 16 (sensor de temperatura) y el 18 (VBAT) comparten
     * ADC0, asi que hay que pedir el otro interruptor interno,
     * reconfigurar el canal, convertir, y DEJARLO COMO ESTABA. Si no se
     * restaura, la siguiente medida de bateria leeria el sensor de
     * temperatura y daria un voltaje absurdo, que es el tipo de averia
     * que aparece lejos de aqui y cuesta encontrar.
     */
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, DISABLE);
    adc_channel_16_to_18(ADC_TEMP_VREF_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_16, ADC_SAMPLETIME_480);

    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    while (SET != adc_flag_get(ADC0, ADC_FLAG_EOC)) {
        /* bloqueante y corto, mismo razonamiento que la bateria */
    }
    raw = adc_regular_data_read(ADC0);
    adc_flag_clear(ADC0, ADC_FLAG_EOC);

    /* restaurar la configuracion de bateria antes de hacer ninguna cuenta */
    adc_channel_16_to_18(ADC_TEMP_VREF_CHANNEL_SWITCH, DISABLE);
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);

    /* T = (V25 - Vts) / pendiente + 25, con la hoja de datos: V25 =
     * 1450 mV y pendiente 4,1 mV por grado. Se trabaja en decimas para
     * no perder resolucion en enteros. */
    mv    = ((int32_t)raw * 3300) / 4095;
    t_x10 = (((1450 - mv) * 10) / 41) + 250;

    if (t_x10 < -400) { t_x10 = -400; }   /* fuera de rango: algo va mal, */
    if (t_x10 > 1500) { t_x10 = 1500; }   /* mejor un tope que un absurdo */
    return (int16_t)(t_x10 / 10);
}

uint8_t battery_get_percent(void)
{
    uint16_t mv = battery_get_millivolts();
    uint32_t pct;

    if (mv <= BATTERY_EMPTY_MV) {
        return 0U;
    }
    if (mv >= BATTERY_FULL_MV) {
        return 100U;
    }

    pct = ((uint32_t)(mv - BATTERY_EMPTY_MV) * 100UL) / (uint32_t)(BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    return (uint8_t)pct;
}

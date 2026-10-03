#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/*
 * VBAT monitoring via the GD32F450's built-in ADC0 channel 18. Per
 * gd32f4xx_adc.h's ADC_SYNCCTL_VBATEN comment ("channel 18 (1/4
 * voltage of external battery) enable of ADC0") and the GD32F450
 * datasheet's ADC chapter, enabling that switch internally routes
 * VBAT/4 onto channel 18 - the 1/4 divider is built into the ADC
 * peripheral itself, not a board-level resistor network, so
 * battery_get_millivolts() below undoes it (raw_adc_mV * 4) as part
 * of the conversion.
 *
 * DIODE DROP COMPENSATION (per the project owner, 31/07/2026): this
 * board's VBAT pin sits behind a series diode from the actual battery
 * terminals - a common reverse-polarity/back-feed protection on
 * battery inputs - so the pin itself reads LOWER than the real
 * battery voltage by that diode's forward drop. BATTERY_DIODE_DROP_MV
 * (700mV, a typical silicon diode Vf) is added back on top of the *4
 * result to approximate the actual battery terminal voltage.
 *
 * This is a FIXED approximation, not a measured/temperature-
 * compensated Vf - real diodes drop somewhat less at low current and
 * more at high current or low temperature, so treat the result as
 * "close enough for a gauge", not a calibrated instrument. If it
 * reads consistently off against a multimeter on the actual battery
 * terminals, retune BATTERY_DIODE_DROP_MV - the ADC/divider math
 * above is exact per the datasheet, the diode drop is the only fudge
 * factor in this file.
 */
#define BATTERY_DIODE_DROP_MV 400U // Ajustado aquí a ojo....

/* Configure ADC0 for a single-channel, software-triggered, blocking
 * VBAT read (channel 18, longest sample time - see battery.c for why
 * that's fine here). Call once at boot, alongside the other
 * peripheral _init() calls in main.c - no dependency on I2C/I2S/the
 * audio chain, so it can go anywhere in that sequence. */
void battery_init(void);

/* Blocking single conversion (~a few us, see battery.c) - reads VBAT
 * right now, undoes the /4 internal divider, and adds
 * BATTERY_DIODE_DROP_MV back on top. NOT ISR-safe (shares ADC0 with
 * nothing else today, but don't call this from the demod ISR context
 * if that ever changes) - intended for the main loop, polled
 * occasionally by battery_display_draw()'s call site in main.c. */
uint16_t battery_get_millivolts(void);

/*
 * Convenience wrapper: maps battery_get_millivolts() onto 0..100 for
 * the UI gauge, linearly between BATTERY_EMPTY_MV and BATTERY_FULL_MV
 * (see battery.c - both currently a single-cell Li-ion/Li-Po
 * placeholder range, 3.0-4.2V, NOT confirmed against this board's
 * actual pack). Adjust those two constants once the real
 * chemistry/cell count is known; nothing else needs to change.
 */
uint8_t battery_get_percent(void);

/*
 * Temperatura del SENSOR INTERNO DEL MICRO, en grados enteros.
 *
 * Mide el die del GD32F450, no la habitacion: con la radio encendida da
 * bastante mas que la temperatura ambiente, y esa diferencia es normal.
 * Sirve para ver si el aparato se esta calentando de mas, que es para lo
 * que esta, no para saber que tiempo hace.
 *
 * *** SIN CALIBRAR Y SIN COMPROBAR CONTRA UN TERMOMETRO ***. Usa los
 * valores tipicos de la hoja de datos (1,45 V a 25 grados y 4,1 mV por
 * grado) y estos sensores se van facilmente diez grados de un chip a
 * otro sin una calibracion por unidad. O sea: la TENDENCIA es fiable,
 * el numero absoluto no. Por eso en pantalla pone "del chip" y no
 * "temperatura" a secas.
 *
 * Comparte el ADC0 con la medida de bateria y cambia de canal para leer,
 * dejandolo como estaba al terminar. Solo desde el bucle principal,
 * igual que battery_get_millivolts().
 */
int16_t battery_get_chip_temp_c(void);

#endif /* BATTERY_H */

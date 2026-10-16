#ifndef RF_LPF_H
#define RF_LPF_H

#include <stdint.h>

/*
 * RF front-end low-pass filter bank selector.
 *
 * The board has a 4-position input low-pass filter bank switched by
 * three GPIO lines: PA1, PA2, PA5. Truth table (bit order as listed,
 * PA1 PA2 PA5), per the board's documented behaviour:
 *
 *   range 1:   0 - 36 MHz   -> 0 1 0
 *   range 2:  37 - 60 MHz   -> 1 1 0
 *   range 3:  61 - 120 MHz  -> 0 0 0
 *   range 4: 121 - 180 MHz  -> 1 0 0
 *
 * Note that PA5 is 0 in all four positions: PA1/PA2 alone cover the
 * four combinations. PA5 is still driven (low) here in case it
 * selects some additional front-end function (bypass, attenuator,
 * an extra range...) not exercised by these four positions - if that
 * turns out to be the case, extend this module rather than leaving
 * the pin floating.
 *
 * rf_lpf_select() is called automatically from the MS5351 driver on
 * every tune, so the filter always tracks the LO frequency; there is
 * normally no need to call it by hand.
 */

/* Call once at startup, before the first tune: configures PA1, PA2
 * and PA5 as push-pull outputs and applies the range-1 (lowest)
 * filter as a safe default. */
void rf_lpf_init(void);

/* Selects the filter range appropriate for `freq_hz`. Frequencies
 * above 180MHz clamp to range 4 (with a UART warning); the MS5351
 * quadrature tune tops out around 225MHz anyway. Returns the selected
 * range, 1..4. */
uint8_t rf_lpf_select(uint32_t freq_hz);

#endif /* RF_LPF_H */

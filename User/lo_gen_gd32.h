#ifndef LO_GEN_GD32_H
#define LO_GEN_GD32_H

#include <stdint.h>

/*
 * GD32-GENERATED QUADRATURE LO, added 01/09/2026 - per the project
 * owner: the MS5351 (Si5351 clone) doesn't hold reliable I/Q
 * quadrature on real hardware below 5MHz (both its own techniques -
 * see ms5351.h's comment on the >=4.8MHz phase-offset-register scheme
 * and the <4.8MHz fractional low-band trick - are either out of range
 * or explicitly flagged as "NOT bench-confirmed... PLL FEEDBACK
 * MULTIPLIER WARNING" for anything much below ~1MHz). This board's
 * own designer anticipated exactly that limitation: PA6/PA7 route to
 * the SAME physical nets as the MS5351's CLK0/CLK1 outputs (through
 * 100-ohm series resistors - see main.c's boot-time Hi-Z comment),
 * meaning the GD32 itself can drive the QSD's LO directly whenever the
 * MS5351's own outputs are put into Hi-Z.
 *
 * WHY ONLY A NARROW LOW SLICE, NOT THE WHOLE <5MHz RANGE: a plain
 * hardware-timer square wave has a frequency-dependent tuning
 * resolution of roughly f^2/TIMERCLK per LSB of the timer's period
 * register - at TIMERCLK=99.84MHz this is already ~900Hz right at
 * LO_GEN_CROSSOVER_HZ (300kHz) and gets rapidly worse above that
 * (~5kHz at 500kHz, hundreds of kHz by 5MHz) - nowhere near usable for
 * tuning. A true DDS (fine-grained phase accumulator via a fast
 * interrupt) would need an interrupt rate many times the output
 * frequency - completely infeasible up toward 5MHz on this MCU, and a
 * fractional/dithered divider risks audible spurs right in the bands
 * that matter most. So this module is deliberately narrow-scoped: it
 * only replaces the MS5351 for the bottom slice (below
 * LO_GEN_CROSSOVER_HZ) where a plain timer's resolution is still
 * usable (a few hundred Hz) AND where the MS5351's own low-band trick
 * is simultaneously at its LEAST confident (see the warning cited
 * above) - the two constraints happen to point at the same slice.
 * Everything from LO_GEN_CROSSOVER_HZ up to 5MHz (and above) is
 * UNCHANGED, still handled by ms5351_set_lo_freq() exactly as before -
 * this module does not attempt to fix the MS5351's reliability in that
 * middle zone.
 *
 * PIN MAPPING (01/09/2026, corrected - see this file's own history):
 * PA6=TIMER2_CH0, PA7=TIMER2_CH1, both AF2 - confirmed directly
 * against this board's real GD32F450 pin-function datasheet table
 * (the project owner supplied the actual row for PA6/PA7, not the
 * generic STM32F4-family guess this module started from - TIMER3 was
 * the WRONG timer entirely on this specific chip). These two pins'
 * ONLY other timer-output alternates are TIMER0_BRKIN (an input, not
 * usable) / TIMER12_CH0 for PA6, and TIMER0_CH0_ON (a complementary
 * output tied to TIMER0's OWN channel 0, not an independent
 * frequency) / TIMER13_CH0 for PA7 - i.e. TIMER2_CH0/CH1 is the ONLY
 * combination on these exact two pins where both channels share one
 * counter, which this technique depends on. TIMER2 was, until this
 * same date, fully committed to generating MCLK (gd32_i2s_mclk_
 * timer_start(), also TIMER2_CH0, output on PC6 instead) - freed up by
 * disabling that call in main.c as a real-hardware experiment, once
 * aic3204.c's own capture-based finding (the codec's PLL derives
 * CODEC_CLKIN from BCLK, not MCLK) suggested it was never actually
 * needed - see main.c's boot sequence comment for the full story and
 * how to revert if disabling MCLK turns out to matter after all.
 *
 * TECHNIQUE: both TIMER2 channels in TIMER_OC_MODE_TOGGLE with STATIC
 * (non-updating) compare values, offset from each other by exactly
 * half the counter's own period. A channel in toggle mode flips its
 * output once per counter period (whenever the counter reaches that
 * channel's fixed CCR value) - with CCR held constant, consecutive
 * toggles are always exactly (ARR+1) counts apart, so the result is a
 * PERFECT square wave at half the counter's rollover rate, regardless
 * of where in [0,ARR] that channel's CCR sits. Offsetting channel 2's
 * CCR from channel 1's by (ARR+1)/2 - half a COUNTER period, which is
 * a quarter of the resulting (2x longer) OUTPUT period - gives a
 * genuine, perfectly phase-locked 90-degree quadrature relationship
 * between the two outputs, entirely in hardware, with ZERO CPU/
 * interrupt/DMA involvement after setup (no drift is even possible -
 * both channels toggle off the exact same free-running counter).
 *
 * *** PHASE-SENSE - see lo_gen_gd32_set_freq()'s own comment in the .c
 * file for the full back-and-forth *** - which physical pin (PA6 or
 * PA7) needs to be the LEADING channel to match the QSD's expected
 * I/Q sense went through two real-hardware-driven fixes the same day:
 * first swapped away from PA6/CLK0-leads (based on re-decoding the
 * Si5351/MS5351 PHOFF register semantics, confirmed via SSB sideband
 * sense above/below 5MHz), then swapped BACK to PA6/CLK0-leading
 * after a direct external-generator phase measurement of this
 * module's own output (below the 300kHz crossover) showed the first
 * swap had it backwards specifically here. Currently: PA6 (CLK0's
 * net) leads, PA7 (CLK1's net) lags - CH1 gets CCR=0, CH2 gets
 * CCR=half in lo_gen_gd32_set_freq(). NOT yet re-confirmed whether
 * SSB sideband sense below 300kHz still agrees with this after the
 * second swap - see that function's comment for what to check if it
 * doesn't.
 */

/* Anything below this tunes via this module; anything at or above it
 * keeps using ms5351_set_lo_freq() exactly as before - a single,
 * easily-adjusted crossover point. 300kHz chosen as the point where a
 * plain timer's resolution (~900Hz/LSB here) is still broadly usable
 * (fine for LW/lower MW - AM channels are 9/10kHz apart - not
 * intended for SSB work). Revisit after bench-testing both the
 * generator's actual resolution/cleanliness and how far up the
 * MS5351's own low-band trick can be trusted on this board. */
#define LO_GEN_CROSSOVER_HZ 300000UL

/*
 * One-time GPIO/timer clock enable - safe to call once at boot,
 * before the first tune. Does NOT touch PA6/PA7's mode (main.c's own
 * boot sequence already puts them in explicit Hi-Z input - see its
 * comment) and does NOT start the timer - lo_gen_gd32_set_freq() does
 * both, only the first time it's actually needed.
 */
void lo_gen_gd32_init(void);

/*
 * Configures PA6/PA7 as TIMER2 AF2 quadrature outputs (if not already)
 * and (re)programs the timer for freq_hz - see this header's own
 * comment for the technique. Uses the timer's shadow/preload
 * mechanism where practical to avoid a glitch on a live frequency
 * change. Returns 1 on success, 0 if freq_hz is out of this module's
 * representable range (shouldn't happen in practice within its
 * intended <300kHz scope - TUNE_MIN_HZ minus DEMOD_IF_OFFSET_HZ, the
 * lowest real LO frequency this project ever asks for, is nowhere
 * near this module's actual floor).
 */
uint8_t lo_gen_gd32_set_freq(uint32_t freq_hz);

/*
 * Stops the timer and returns PA6/PA7 to the exact same Hi-Z input
 * state main.c's boot sequence originally left them in - call this
 * when retuning back up to/past LO_GEN_CROSSOVER_HZ, before
 * ms5351_set_lo_freq() takes back over. Note: as of 01/09/2026,
 * ms5351_set_lo_freq() DOES now always re-assert the MS5351's own
 * output-enable mask on its next call after a disable (a real gap
 * fixed alongside implementing ms5351_lo_disable() itself - see both
 * their comments in ms5351.c) - so no separate MS5351-side re-enable
 * call is needed there either; this function only needs to undo what
 * THIS module did (stop the timer, release PA6/PA7 back to Hi-Z).
 */
void lo_gen_gd32_stop(void);

#endif

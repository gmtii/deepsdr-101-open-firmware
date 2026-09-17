#ifndef STACK_WATERMARK_H
#define STACK_WATERMARK_H

#include <stdint.h>

/*
 * Stack high-water-mark measurement, added to answer a concrete
 * question before integrating ft8_lib: bp_decode() (the sparse LDPC
 * decoder we'll use - see ft8_lib's ldpc.c) puts ~4.4KB of local
 * arrays (tov[174][3] + toc[83][7]) on the stack per call, transient,
 * not persistent RAM. The linker script only reserves
 * _Min_Stack_Size=0x800 (2KB) as a build-time sanity floor - the
 * REAL stack lives in whatever TCM space is left above the linked
 * image (from _end, right after the .tcmram section, up to _estack
 * at the top of TCM - see GD32F450VE_FLASH.ld), shared by every ISR
 * in the project (audio DMA, USBFS/TIMER5 delay unit, touch EXTI,
 * SysTick) since this is bare-metal with a single stack, no RTOS.
 * This module measures how much of that shared stack normal operation actually
 * uses, so we know the real margin before adding ft8's ~5KB burst on
 * top.
 *
 * Classic "stack painting" technique: fill the entire unused-so-far
 * stack region with a distinctive pattern as early as possible (top
 * of main(), before almost anything has run), then later scan from
 * the bottom (near _end) upward for the first word that's no longer
 * the pattern - that boundary is the historical worst-case low-water
 * mark, i.e. the deepest the stack has ever reached, including
 * whatever ISR nested on top of whatever call chain at the worst
 * moment since boot.
 */

/* Call exactly once, as the very FIRST line of main() - before
 * SystemCoreClockUpdate() or anything else - so the painted region
 * covers as much of the stack as possible (anything already used by
 * the time this runs can't be painted without corrupting a live
 * frame, and is treated as permanently "used" from then on). */
void stack_watermark_paint(void);

/* Scans the painted region for the low-water mark and returns how
 * many bytes were NEVER touched by any call chain or ISR since boot -
 * i.e. the current safety margin. Cheap-ish (a linear scan up to a
 * few KB) - call it occasionally (a menu action, a periodic debug
 * report), not from a hot path. */
uint32_t stack_watermark_get_free_bytes(void);

/* Total size of the region that was actually painted at boot (paint
 * top - _end) - use as the denominator against
 * stack_watermark_get_free_bytes() to express usage as a fraction. */
uint32_t stack_watermark_get_painted_bytes(void);

#endif /* STACK_WATERMARK_H */

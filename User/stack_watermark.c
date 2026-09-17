#include "stack_watermark.h"

#define STACK_PAINT_PATTERN 0xA5A5A5A5U
#define STACK_PAINT_MARGIN_BYTES 64U /* never paint closer than this to the live SP at paint time - safety margin against off-by-one/alignment mistakes corrupting a real stack frame */

/* _end: linker symbol (GD32F450VE_FLASH.ld's ._user_heap_stack
 * section, PROVIDE(_end = .)) marking the first free word right
 * after the .tcmram section (spectrum/rtty_scope/zoom/demod_am/nr_ss
 * buffers) - i.e. the lowest address the stack could ever reach
 * without smashing one of those. We only care about its ADDRESS
 * (&_end), never its value - it has no defined type/value, just a
 * linker-computed location, hence the uint32_t placeholder type. */
extern uint32_t _end;

static uint32_t *s_paint_bottom; /* &_end, cached */
static uint32_t s_painted_words;

void stack_watermark_paint(void)
{
    uint32_t sp;
    uint32_t *paint_top;
    uint32_t *p;

    __asm volatile("mov %0, sp" : "=r"(sp));

    s_paint_bottom = &_end;
    paint_top = (uint32_t *)(sp - STACK_PAINT_MARGIN_BYTES);
    /* Round down to a word boundary - sp is always word-aligned on
     * Cortex-M4, but the margin subtraction keeps it aligned too here
     * since STACK_PAINT_MARGIN_BYTES is a multiple of 4. */

    s_painted_words = 0U;
    for (p = s_paint_bottom; p < paint_top; p++)
    {
        *p = STACK_PAINT_PATTERN;
        s_painted_words++;
    }
}

uint32_t stack_watermark_get_free_bytes(void)
{
    uint32_t *p = s_paint_bottom;
    uint32_t *paint_end = s_paint_bottom + s_painted_words;
    uint32_t untouched_words = 0U;

    /* Scan from the bottom (closest to _end) upward - the pattern
     * survives everywhere the stack never reached down to, and breaks
     * at the first word any call chain/ISR actually wrote to. Since
     * the stack grows DOWN from high addresses, "untouched from the
     * bottom up" is exactly "never used, ever". */
    while (p < paint_end && *p == STACK_PAINT_PATTERN)
    {
        untouched_words++;
        p++;
    }

    return untouched_words * 4U;
}

uint32_t stack_watermark_get_painted_bytes(void)
{
    return s_painted_words * 4U;
}

#ifndef FT8_SHARED_RAM_H
#define FT8_SHARED_RAM_H

#include <stdint.h>
#include "waterfall.h"       /* WATERFALL_WIDTH, WATERFALL_ROWS */
#include "ft8_fft1024.h"     /* FT8_FFT_SIZE, FT8_FFT_BINS_USEFUL */
#include "ft8_waterfall_adapter.h" /* FT8_ADAPTER_* */

/*
 * Real memory union between the main waterfall's own pixel buffer and
 * every large static buffer FT8 mode needs (09/2026) - added when
 * widening the FT8 search window to 1600Hz overflowed RAM by ~41.9KB
 * at link time. Up to that point, "FT8 mode reuses the waterfall's
 * memory footprint" had only ever been a budget ESTIMATE in comments
 * (see ft8_waterfall_adapter.h's own memory-budget history) - the
 * actual code just declared separate static arrays for everything,
 * which the linker allocates ADDITIONALLY on top of the waterfall's
 * own buffer, not instead of it. This union is what finally makes
 * that reuse real, the same way the HFDL decoder's own RAM management
 * already does for ITS waterfall-lending trick (see
 * /areas/deepsdr-hfdl-decoder.md's "RAM managed via a union lending
 * the waterfall buffer to HFDL demod when in HFDL mode").
 *
 * SAFETY: the waterfall's own buffer (waterfall.c's s_buf) is written
 * and read purely by CPU code (memset/memcpy/gfx_blit - confirmed by
 * reading waterfall.c directly, no DMA anywhere in that file), same
 * "CPU-only, safe to overlap" class of buffer as the TCM-relocated
 * ones (fft.c, spectrum.c, etc.) - and it is NEVER written while FT8
 * mode is active, because sdr_spectrum_waterfall_tick() (the only
 * thing that calls waterfall_push_line()) does not run while FT8 mode
 * has taken over that same screen region (see main.c's rtty_showing/
 * ft8_showing render gate). Conversely, none of FT8's own buffers are
 * touched while a normal (non-FT8, non-RTTY) mode is showing the real
 * waterfall - ft8_waterfall_reset()/feed_subblock() are simply never
 * called outside FT8 mode.
 *
 * SIZE: the waterfall's own buffer (WATERFALL_WIDTH*WATERFALL_ROWS*2 =
 * 114624 bytes) is comfortably the larger member - everything FT8
 * needs together (mag[] + the FFT-1024 tables + the cascade history +
 * the sliding analysis window + FFT scratch) totals ~74.2KB, ~40KB
 * less than the waterfall buffer alone - so this union costs exactly
 * what the waterfall buffer already cost by itself: NO net RAM
 * increase for adding all of FT8's needs.
 *
 * ONE instance, defined (not just declared extern) in waterfall.c -
 * see that file's own comment at the point it replaced its old
 * private `static uint16_t s_buf[...]` declaration with a reference
 * into this union.
 */
typedef union
{
    uint16_t waterfall_buf[WATERFALL_ROWS][WATERFALL_WIDTH];

    /* HFDL (16/09/2026, primera pieza de la incorporacion del modulo
     * HFDL - ver /areas/deepsdr-hfdl-decoder.md): tercer miembro
     * mutuamente excluyente con los dos de arriba, exactamente el
     * mismo espacio fisico. Usado por hfdl_payload_decode.c (traido
     * tal cual de la rama HFDL) a traves de
     * waterfall_ram_borrow_for_hfdl()/hfdl_scope_get_hfdl_ram() - ver
     * el contrato completo en waterfall.h. Nunca activo a la vez que
     * `ft8` (modos mutuamente excluyentes en main.c) ni que
     * `waterfall_buf` (por el mismo motivo que ya vale para `ft8`). */
    uint8_t hfdl_scratch[WATERFALL_RAM_BORROW_CAPACITY];

    struct
    {
        uint8_t mag[FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS];
        /*
         * Snapshot copy of the field above (09/2026) - REAL FIX for a
         * structural timing problem, per the project owner's own
         * ground-truth check against an NTP server: capture (14.88s,
         * measured exact) + decode processing (ftx_find_candidates +
         * ftx_decode_candidate, ~277ms+ even with zero real signals -
         * see ft8_decoder_get_last_num_candidates()'s own comment)
         * together take MORE real wall-clock time than the 15.0s a
         * real FT8 slot actually lasts - a few hundred ms too much,
         * EVERY single cycle, with no scheduling trick able to fix
         * it: the total work genuinely doesn't fit the time budget.
         * Falling further behind every cycle, without bound, is the
         * unavoidable result of that - not a bug in when captures get
         * armed.
         *
         * The fix: memcpy the just-completed mag[] into THIS buffer
         * the instant the slot is full, then reset+rearm the real
         * capture IMMEDIATELY (before decoding anything) so new audio
         * starts counting toward the NEXT slot with no delay at all -
         * only THEN does the (still ~277ms+) decode work happen,
         * reading from this now-frozen copy instead of the live mag[]
         * that the new capture will already be overwriting. Costs
         * another copy of mag[]'s own size (47616 bytes at
         * NUM_BINS=256) - the union's overall size grows past the
         * waterfall buffer's own 114624 bytes for the first time (by
         * about 7.2KB) rather than fitting inside it "for free", but
         * that's a small, one-time RAM cost for fixing a structural
         * timing problem no amount of scheduling logic could have
         * solved.
         */
        uint8_t mag_snapshot[FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS];
        float fft_re[FT8_FFT_SIZE];
        float fft_im[FT8_FFT_SIZE];
        float fft_twiddle_cos[FT8_FFT_SIZE / 2U];
        float fft_twiddle_sin[FT8_FFT_SIZE / 2U];
        float fft_hann[FT8_FFT_SIZE];
        uint16_t fft_bitrev[FT8_FFT_SIZE];
        uint8_t cascade[16][FT8_ADAPTER_NUM_BINS]; /* 16 = FT8_WF_HISTORY_ROWS, duplicated here since that constant is private to ft8_waterfall_adapter.c - keep in sync if it ever changes */
        int16_t window[FT8_ADAPTER_NFFT];
        float dbout[FT8_FFT_BINS_USEFUL];
    } ft8;
} ft8_shared_ram_t;

extern ft8_shared_ram_t g_ft8_shared_ram;

#endif /* FT8_SHARED_RAM_H */

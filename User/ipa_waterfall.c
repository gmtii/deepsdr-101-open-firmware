#include "ipa_waterfall.h"
#include "gd32f4xx.h"
#include "gd32f4xx_ipa.h"
#include "debug_uart.h"
#include <string.h> /* memset() - ipa_waterfall_lut_size_selftest() */

/*
 * IPA_FPCTL's FCNP field ("foreground LUT number of pixel") is only 8 bits
 * wide (IPA_FPCTL_FCNP = BITS(8,15)), so it cannot literally hold the value
 * 256. Every other GigaDevice/ST peripheral with the same layout (this
 * block is functionally the DMA2D/Chrom-ART equivalent) encodes a full-size
 * table as size-1, i.e. 255 means "256 entries, indices 0..255". The
 * gd32f4xx_ipa.c reference driver takes fg_lut_num at face value and writes
 * it straight into FCNP without documenting which convention applies here.
 *
 * TODO(jorge): confirm this against the GD32F4xx user manual's IPA_FPCTL
 * register description (section 11.6.8) before trusting it on real
 * hardware - if GigaDevice did NOT follow the size-1 convention, the LUT
 * load would be silently truncated to 255 entries and the last palette
 * color (pure white/max intensity on every palette in spectrum.c) would
 * never render. Easy to check on the bench: load a test palette with a
 * distinct color at index 255 and see if it shows up.
 */
#define IPA_LUT_ENTRIES_256   255U

/*
 * *** DEFENSIVE WAIT (bounded, reports failure) ***
 * The two IPA operations below (LUT load, single-row transfer) each finish
 * by raising a status flag. A first hardware bring-up hit a real bug where
 * ipa_waterfall_load_palette() was reached (via settings_load() loading a
 * saved palette from CONFIG.CSV) BEFORE ipa_waterfall_init() had enabled
 * the IPA's clock - with a plain unbounded `while (RESET == ipa_flag_get(...))`,
 * that hung the boot completely and silently (no HardFault, just a
 * permanent lock). Fixed by moving ipa_waterfall_init() earlier in main.c's
 * boot sequence (see that call site's comment) - but the wait itself stays
 * bounded and WCF-aware rather than reverting to a plain busy-wait, since a
 * silent infinite lock on any future misconfiguration is a bad failure mode
 * regardless of what caused this particular one.
 */
#define IPA_WAIT_TIMEOUT_ITERS   1000000UL

static uint8_t ipa_wait_flag(uint32_t flag, const char *what)
{
    uint32_t n = 0;
    while (RESET == ipa_flag_get(flag)) {
        if (SET == ipa_flag_get(IPA_FLAG_WCF)) {
            debug_print("ipa_waterfall: IPA_FLAG_WCF (wrong configuration) set while waiting for ");
            debug_print(what);
            debug_print("\n");
            ipa_flag_clear(IPA_FLAG_WCF);
            return 0U;
        }
        n++;
        if (n > IPA_WAIT_TIMEOUT_ITERS) {
            debug_print("ipa_waterfall: TIMEOUT waiting for ");
            debug_print(what);
            debug_print("\n");
            debug_print_hex32("  IPA_CTL", IPA_CTL);
            debug_print_hex32("  IPA_INTF", IPA_INTF);
            return 0U;
        }
    }
    return 1U;
}

void ipa_waterfall_init(void)
{
    rcu_periph_clock_enable(RCU_IPA);
    /* Dummy read-back: on this family, a peripheral's AHB clock enable bit
     * needs to be read back once before the peripheral's own registers are
     * guaranteed accessible (bus-domain synchronization delay) - several
     * GD32/STM32 peripherals have this as a documented erratum. Kept as a
     * precaution even though the real bug turned out to be call ordering
     * (see this function's call site in main.c), not this. */
    (void)RCU_AHB1EN;
    ipa_deinit(); /* toggles RCU_IPARST - known-reset state before first use */
}

void ipa_waterfall_load_palette(const uint32_t *argb8888_256)
{
    /* One-shot LUT load, triggered by FLLEN (see IPA_FPCTL_FLLEN) - this
     * is NOT a persistent "LUT enable" bit, it fires a single burst read
     * from FLMADDR and then the hardware clears it back on its own once
     * LLF (LUT loading finish) sets. Busy-waiting here is fine: this only
     * runs on a palette change (a handful of times per session), never
     * per frame. */
    ipa_foreground_lut_init(IPA_LUT_ENTRIES_256, IPA_LUT_PF_ARGB8888,
                             (uint32_t)argb8888_256);
    ipa_flag_clear(IPA_FLAG_LLF);
    ipa_foreground_lut_loading_enable();
    (void)ipa_wait_flag(IPA_FLAG_LLF, "LLF (LUT load finish)");
    ipa_flag_clear(IPA_FLAG_LLF);
}

void ipa_waterfall_colormap_line(const uint8_t *idx_line, uint16_t *out_line, uint16_t width)
{
    ipa_foreground_parameter_struct fg;
    ipa_destination_parameter_struct dst;

    /* Foreground: the caller's row of raw 8-bit palette indices. Each byte
     * is looked up in the LUT loaded by ipa_waterfall_load_palette() - it
     * is NOT itself a color, hence FOREGROUND_PPF_L8 rather than one of the
     * direct RGB/ARGB foreground formats. */
    ipa_foreground_struct_para_init(&fg);
    fg.foreground_memaddr = (uint32_t)idx_line;
    fg.foreground_lineoff = 0U; /* single row - no gap between (nonexistent) further lines */
    fg.foreground_pf = FOREGROUND_PPF_L8;
    ipa_foreground_init(&fg);

    /* Destination: RGB565, matching what waterfall_push_line() already
     * expects downstream - this call is a drop-in replacement for the old
     * CPU loop, nothing past it needs to change. */
    ipa_destination_struct_para_init(&dst);
    dst.destination_memaddr = (uint32_t)out_line;
    dst.destination_lineoff = 0U;
    dst.destination_pf = IPA_DPF_RGB565;
    dst.image_width = width;
    dst.image_height = 1U; /* one waterfall row per call */
    ipa_destination_init(&dst);

    /* PFCM=01 (IPA_FGTODE_PF_CONVERT, see Table 11-1): foreground -> destination
     * with pixel format conversion, no background/blending involved. */
    ipa_pixel_format_convert_mode_set(IPA_FGTODE_PF_CONVERT);

    ipa_flag_clear(IPA_FLAG_FTF);
    ipa_transfer_enable();
    (void)ipa_wait_flag(IPA_FLAG_FTF, "FTF (full transfer finish)");
    ipa_flag_clear(IPA_FLAG_FTF);
}

/*
 * *** ONE-SHOT BENCH TEST for the FCNP off-by-one question above ***
 * Loads a 256-entry ARGB8888 test LUT where every entry is 0 except index
 * 255, which gets a value (0xAABBCCDDU) that could never occur by
 * accident from an uninitialized/partially-loaded LUT. Then runs the IPA
 * on a single L8 "image" (one byte, value 255 - the last possible index)
 * through that LUT, converting to ARGB8888 (not RGB565 - this needs to be
 * lossless to prove the EXACT value came through, not just "some
 * bright-ish color that could be a coincidence").
 *
 * If IPA_LUT_ENTRIES_256=255 really means "256 entries" (the size-1
 * convention this file assumes), the destination pixel comes back exactly
 * 0xAABBCCDDU: PASS. If GigaDevice did NOT use that convention and only
 * 255 entries actually get loaded, index 255 is left at whatever
 * uninitialized value was already in the IPA's internal LUT RAM (almost
 * certainly not 0xAABBCCDDU): FAIL - in which case ipa_waterfall_load_palette()
 * needs a real fix, not just this diagnostic.
 *
 * Call this once, early - right after ipa_waterfall_init() and before
 * spectrum_init(), so it runs on its own test LUT before the real palette
 * is ever loaded. Safe to remove once confirmed: it doesn't leave any
 * state behind that matters, spectrum_init() loads the real palette fresh
 * afterward regardless of what this test did.
 */
uint8_t ipa_waterfall_lut_size_selftest(void)
{
    static uint32_t test_lut[256]; /* static: 1KB, too big to put on the stack */
    static uint8_t idx_pixel;      /* the single-byte L8 "image": one pixel, value 255 */
    static uint32_t result_pixel;
    const uint32_t expected = 0xAABBCCDDU;
    ipa_foreground_parameter_struct fg;
    ipa_destination_parameter_struct dst;

    memset(test_lut, 0, sizeof(test_lut));
    test_lut[255] = expected;
    idx_pixel = 255U;
    result_pixel = 0U;

    ipa_waterfall_load_palette(test_lut);

    ipa_foreground_struct_para_init(&fg);
    fg.foreground_memaddr = (uint32_t)&idx_pixel;
    fg.foreground_lineoff = 0U;
    fg.foreground_pf = FOREGROUND_PPF_L8;
    ipa_foreground_init(&fg);

    ipa_destination_struct_para_init(&dst);
    dst.destination_memaddr = (uint32_t)&result_pixel;
    dst.destination_lineoff = 0U;
    dst.destination_pf = IPA_DPF_ARGB8888; /* lossless read-back - RGB565 would round-trip a wrong value into looking "close enough" */
    dst.image_width = 1U;
    dst.image_height = 1U;
    ipa_destination_init(&dst);

    ipa_pixel_format_convert_mode_set(IPA_FGTODE_PF_CONVERT);
    ipa_flag_clear(IPA_FLAG_FTF);
    ipa_transfer_enable();
    (void)ipa_wait_flag(IPA_FLAG_FTF, "FTF (LUT size self-test)");
    ipa_flag_clear(IPA_FLAG_FTF);

    debug_print("ipa_waterfall_lut_size_selftest: index 255 through the LUT read back as:\n");
    debug_print_hex32("  result  ", result_pixel);
    debug_print_hex32("  expected", expected);
    if (result_pixel == expected) {
        debug_print("  PASS - fg_lut_num=255 loads all 256 entries, the size-1 convention holds.\n");
        return 1U;
    } else {
        debug_print("  FAIL - index 255 did NOT come through as loaded, so the LUT is\n");
        debug_print("         effectively only 255 entries wide on this hardware. Fix: stop\n");
        debug_print("         using index 255 at all - requantize spectrum_colormap_index()\n");
        debug_print("         to t*254 instead of t*255 (256 -> 255 usable palette levels).\n");
        return 0U;
    }
}

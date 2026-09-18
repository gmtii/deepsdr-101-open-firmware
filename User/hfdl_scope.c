#include "hfdl_scope.h"
#include "waterfall.h" /* waterfall_ram_borrow_for_hfdl/return_from_hfdl - see hfdl_scope_set_enabled() */
#include "gd32f4xx.h" /* __get_PRIMASK()/__disable_irq()/__set_PRIMASK() - see hfdl_scope_set_enabled()'s critical section */
#include "debug_uart.h" /* debug_uart_set_quiet() - auto quiet mode on HFDL entry/exit, see hfdl_scope_set_enabled() */

/*
 * See hfdl_scope.h for the full "what this is/isn't" story. FFT
 * engine below is a straight structural copy of rtty_scope.c's
 * (radix-2 DIT, same local sin/cos approximations, same accumulate-
 * in-the-ISR/transform-in-the-main-loop split) - deliberately
 * duplicated rather than shared, same "different consumer, different
 * size, decoupled on purpose" reasoning rtty_scope.h's own comment
 * gives for not sharing with fft.c either.
 */

static float s_re[HFDL_SCOPE_FFT_SIZE];
static float s_im[HFDL_SCOPE_FFT_SIZE];
static float s_twiddle_cos[HFDL_SCOPE_FFT_SIZE / 2U];
static float s_twiddle_sin[HFDL_SCOPE_FFT_SIZE / 2U];
static float s_hann[HFDL_SCOPE_FFT_SIZE];
static uint16_t s_bitrev[HFDL_SCOPE_FFT_SIZE];

/* CHANGED (16/08/2026): TWO accumulation rings now, I and Q, instead
 * of one mono audio ring - see hfdl_scope.h's top comment. Filled by
 * hfdl_scope_feed() (ISR context). */
static float s_accum_i[HFDL_SCOPE_FFT_SIZE];
static float s_accum_q[HFDL_SCOPE_FFT_SIZE];
static uint32_t s_accum_count;             /* samples currently in s_accum_i/s_accum_q, ISR-owned */
static volatile uint8_t s_pending;         /* 1 = s_accum_i/s_accum_q has a full window ready for hfdl_scope_poll() */

static float s_mag[HFDL_SCOPE_BINS];       /* main-loop-owned output, written by hfdl_scope_poll() */
static uint8_t s_frame_ready;

static uint8_t s_enabled;

/* --- burst detector state, see hfdl_scope.h's comment --- */
#define HFDL_BURST_ON_RATIO   3.16f  /* ~+10dB (10^(10/20)) above the floor to DECLARE a burst */
#define HFDL_BURST_OFF_RATIO  2.0f   /* ~+6dB - lower than the ON ratio (hysteresis), so a burst hovering right at
                                       * the edge doesn't chatter on/off every window */
#define HFDL_FLOOR_RISE_ALPHA 0.01f  /* floor creeps UP toward a quieter-than-expected... i.e. a LOUDER-than-floor
                                       * reading slowly, so a burst doesn't drag the floor up and mask itself */
#define HFDL_FLOOR_FALL_ALPHA 0.10f  /* floor settles DOWN toward a quieter reading faster - the band genuinely
                                       * getting quieter should be reflected reasonably promptly */
static float   s_floor_power = 1.0e-9f; /* tracked in the SAME linear, per-window-peak-normalized units as s_mag[] sums to - see hfdl_scope_poll() */
static uint8_t s_burst_active;
static uint8_t s_floor_primed; /* 0 right after reset/enable - see hfdl_scope_poll()'s priming block */
static float   s_last_in_band_sum = 0.0f; /* saved every window - see hfdl_scope_get_snr_ratio() */
static float   s_last_dc_band_sum = 0.0f; /* saved every window - see hfdl_scope_get_dc_band_sum() */
static float   s_last_peak_bin_hz = 0.0f; /* saved every window - see hfdl_scope_get_peak_bin_hz() */

/* Same Bhaskara-I approximation as rtty_scope.c/fft.c/gd32_i2s.c,
 * reimplemented locally on purpose - see this file's top comment. */
static float sinf_approx_local(float x)
{
    float pi = 3.14159265358979f;
    float x2;
    int negate = 0;
    while (x > pi) { x -= 2.0f * pi; }
    while (x < -pi) { x += 2.0f * pi; }
    if (x < 0.0f) { x = -x; negate = 1; }
    x2 = (16.0f * x * (pi - x)) / (5.0f * pi * pi - 4.0f * x * (pi - x));
    return negate ? -x2 : x2;
}

static float cosf_approx_local(float x)
{
    return sinf_approx_local(x + (3.14159265358979f / 2.0f));
}

void hfdl_scope_init(void)
{
    uint32_t n;
    uint32_t bits = 0U;
    uint32_t size = HFDL_SCOPE_FFT_SIZE;

    while (size > 1U) { size >>= 1U; bits++; }

    for (n = 0; n < HFDL_SCOPE_FFT_SIZE / 2U; n++) {
        float angle = -2.0f * 3.14159265358979f * (float)n / (float)HFDL_SCOPE_FFT_SIZE;
        s_twiddle_cos[n] = cosf_approx_local(angle);
        s_twiddle_sin[n] = sinf_approx_local(angle);
    }

    for (n = 0; n < HFDL_SCOPE_FFT_SIZE; n++) {
        s_hann[n] = 0.5f - 0.5f * cosf_approx_local(
            2.0f * 3.14159265358979f * (float)n / (float)(HFDL_SCOPE_FFT_SIZE - 1U));
    }

    for (n = 0; n < HFDL_SCOPE_FFT_SIZE; n++) {
        uint32_t v = n;
        uint32_t r = 0U;
        uint32_t b;
        for (b = 0; b < bits; b++) {
            r = (r << 1U) | (v & 1U);
            v >>= 1U;
        }
        s_bitrev[n] = (uint16_t)r;
    }

    s_accum_count = 0U;
    s_pending = 0U;
    s_frame_ready = 0U;
    s_enabled = 0U;
    s_floor_power = 1.0e-9f;
    s_burst_active = 0U;
    s_floor_primed = 0U;
}

void hfdl_scope_feed(const float *i_in, const float *q_in, uint32_t n)
{
    uint32_t k;

    if (!s_enabled) {
        return; /* mirrors rtty_process()'s own "skipped entirely while off" gate, even though the
                  * call site (demod_am.c) also checks hfdl_scope_get_enabled() before calling at
                  * all - cheap belt-and-braces, same reasoning as rtty_scope_feed() not needing
                  * this (rtty_scope_feed() has no such guard; kept here since s_accum_count must
                  * never advance on stale I/Q from BEFORE this was enabled). */
    }
    if (s_pending) {
        return; /* previous window not yet consumed by hfdl_scope_poll() - drop this one, same
                  * reasoning as rtty_scope_feed(). */
    }
    for (k = 0; k < n && s_accum_count < HFDL_SCOPE_FFT_SIZE; k++) {
        s_accum_i[s_accum_count] = i_in[k];
        s_accum_q[s_accum_count] = q_in[k];
        s_accum_count++;
    }
    if (s_accum_count >= HFDL_SCOPE_FFT_SIZE) {
        s_pending = 1U;
        s_accum_count = 0U;
    }
}

/* Same iterative radix-2 DIT butterfly pass as rtty_scope.c/fft.c. */
static void scope_fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = HFDL_SCOPE_FFT_SIZE / 2U; half_size < HFDL_SCOPE_FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < HFDL_SCOPE_FFT_SIZE; i += (half_size << 1U)) {
            for (j = 0; j < half_size; j++) {
                uint32_t tw_idx = j * step;
                float tre = s_twiddle_cos[tw_idx];
                float tim = s_twiddle_sin[tw_idx];
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                s_re[b] = s_re[a] - br;
                s_im[b] = s_im[a] - bi;
                s_re[a] = s_re[a] + br;
                s_im[a] = s_im[a] + bi;
            }
        }
    }
}

void hfdl_scope_poll(void)
{
    uint32_t n;
    float peak;
    float hz_per_bin;
    uint32_t bin_lo, bin_hi;
    float in_band_sum;
    float dc_band_sum;
    uint32_t peak_bin_index;
    float peak_bin_power;

    if (!s_pending) {
        return;
    }

    /* CHANGED (16/08/2026): genuine complex FFT now - s_accum_i feeds
     * the real rail, s_accum_q feeds the imaginary rail, instead of
     * the old real-input trick (im=0) that was only ever valid for
     * the mono post-combine audio this used to run on. Windowing
     * applies identically to both rails, same as any complex-input
     * FFT. See hfdl_scope.h's top comment for why bins 0..HFDL_SCOPE_BINS-1
     * (the positive-frequency half, 0..Nyquist) are still exactly
     * where HFDL_BAND_LO_HZ/HFDL_BAND_HI_HZ expect the subcarrier -
     * that part didn't need to change. */
    for (n = 0; n < HFDL_SCOPE_FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = s_accum_i[src] * s_hann[src];
        s_im[n] = s_accum_q[src] * s_hann[src];
    }

    scope_fft_run();

    /* DIAGNOSTIC (16/08/2026, blind-spot round 2 - see
     * hfdl_scope_get_peak_bin_hz()'s comment): track which bin the
     * peak actually came from, not just its power - "where is the
     * energy" is the whole point of this round's investigation. */
    peak = 1.0e-12f; /* avoid a divide-by-zero if the block was silence */
    peak_bin_index = 0U;
    for (n = 0; n < HFDL_SCOPE_BINS; n++) {
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        s_mag[n] = power;
        if (power > peak) {
            peak = power;
            peak_bin_index = n;
        }
    }
    peak_bin_power = peak; /* pre-normalization power, unused for now but kept for a future calibrated readout */
    (void)peak_bin_power;
    /* Normalize to THIS window's own peak - same "always full display
     * range, tuning aid not a calibrated readout" reasoning as
     * rtty_scope_poll(). */
    for (n = 0; n < HFDL_SCOPE_BINS; n++) {
        s_mag[n] /= peak;
    }

    /*
     * Burst detector - see hfdl_scope.h's comment on exactly what
     * this does/doesn't mean. Sums the (already peak-normalized)
     * magnitude across the expected HFDL_BAND_LO_HZ..HFDL_BAND_HI_HZ
     * bins as a cheap broadband "how much energy is in the window
     * that matters" proxy - not a calibrated power measurement (the
     * peak-normalization above already threw away absolute level),
     * but consistent frame to frame, which is all the floor/threshold
     * comparison below actually needs.
     */
    hz_per_bin = 12000.0f / (float)HFDL_SCOPE_FFT_SIZE;
    bin_lo = (uint32_t)(HFDL_BAND_LO_HZ / hz_per_bin);
    bin_hi = (uint32_t)(HFDL_BAND_HI_HZ / hz_per_bin);
    if (bin_hi >= HFDL_SCOPE_BINS) { bin_hi = HFDL_SCOPE_BINS - 1U; }
    in_band_sum = 0.0f;
    for (n = bin_lo; n <= bin_hi; n++) {
        in_band_sum += s_mag[n];
    }

    /* DIAGNOSTIC (16/08/2026) - energy in the 0Hz..HFDL_BAND_LO_HZ
     * "guard" region, BELOW where in_band_sum even starts counting -
     * see hfdl_scope_get_dc_band_sum()'s comment. bin_lo is exclusive
     * here on purpose (bin_lo itself is already counted in
     * in_band_sum above, no double-counting). */
    dc_band_sum = 0.0f;
    for (n = 0; n < bin_lo; n++) {
        dc_band_sum += s_mag[n];
    }
    s_last_dc_band_sum = dc_band_sum;
    s_last_peak_bin_hz = (float)peak_bin_index * hz_per_bin;

    /* FIX (16/08/2026): prime the floor with the FIRST real measurement
     * instead of chasing it up from the near-zero seed via the slow EMA
     * (HFDL_FLOOR_RISE_ALPHA=0.01 needs ~100 windows to converge) - and
     * don't make a burst decision on this priming window, since there's
     * no meaningful floor to compare against yet. Without this, entering
     * HFDL mode (or re-enabling the scope) with the signal already
     * present - the normal case, not an edge case - guaranteed a false
     * burst on window 1, which silently consumed the one-shot raw
     * capture arm before any real burst ever occurred. */
    if (!s_floor_primed) {
        s_floor_power = in_band_sum;
        if (s_floor_power < 1.0e-9f) { s_floor_power = 1.0e-9f; }
        s_floor_primed = 1U;
        s_pending = 0U;
        s_frame_ready = 1U;
        return;
    }

    /* BUG FOUND (16/08/2026, second round of the exact-tuning
     * investigation - see hfdl_scope_get_floor_power()'s comment for
     * the full evidence trail): this used to update UNCONDITIONALLY,
     * rise or fall, regardless of s_burst_active. HFDL_FLOOR_RISE_ALPHA
     * = 0.01 sounds slow, but its actual time constant is only
     * ~100 windows =~ 2.1s (256-sample windows @ 12kHz) - so any burst
     * that stays elevated for more than a couple of seconds (an
     * entirely normal HFDL frame duration, not an edge case) drags the
     * floor up to meet it DURING the burst itself, quietly erasing the
     * very contrast the ON/OFF ratio below depends on. Confirmed on
     * real hardware: 2 minutes parked dead-on 17928.000 with strong,
     * clearly audible HFDL traffic the whole time NEVER crossed the
     * 3.16 ON threshold (peak ratio ~2.4), while floor_power and
     * in_band_sum sat within the same order of magnitude the entire
     * capture, AND hfdl_scope_get_peak_bin_hz() kept landing right on
     * the expected ~1440-1500Hz subcarrier - i.e. the FFT saw the
     * signal fine, the ratio just never got the chance to reflect it
     * because the floor kept chasing it up in real time. A 2kHz
     * detune (weaker/filtered image of the same signal through the
     * channel filter's skirt) looks more like a clean burst against
     * real silence to this same detector, and DOES cross threshold -
     * consistent with this being the mechanism, not a coincidence of
     * traffic timing.
     *
     * FIX: freeze the floor (both directions) while a burst is
     * already active. It only resumes tracking once s_burst_active
     * drops back to 0, so it keeps chasing genuine background noise
     * during quiet stretches (still needed for e.g. band-condition
     * drift) but can't erode the very burst it's supposed to be
     * measuring against while that burst is happening. */
    if (!s_burst_active) {
        if (in_band_sum > s_floor_power) {
            s_floor_power += HFDL_FLOOR_RISE_ALPHA * (in_band_sum - s_floor_power);
        } else {
            s_floor_power += HFDL_FLOOR_FALL_ALPHA * (in_band_sum - s_floor_power);
        }
        if (s_floor_power < 1.0e-9f) { s_floor_power = 1.0e-9f; } /* keep the ON/OFF ratio comparisons below well-defined even after a long silent stretch */
    }
    s_last_in_band_sum = in_band_sum;

    if (!s_burst_active && in_band_sum > (HFDL_BURST_ON_RATIO * s_floor_power)) {
        s_burst_active = 1U;
    } else if (s_burst_active && in_band_sum < (HFDL_BURST_OFF_RATIO * s_floor_power)) {
        s_burst_active = 0U;
    }

    s_pending = 0U; /* hfdl_scope_feed() can accept the next window again */
    s_frame_ready = 1U;
}

uint8_t hfdl_scope_frame_ready(void)
{
    return s_frame_ready;
}

const float *hfdl_scope_get_frame(void)
{
    s_frame_ready = 0U;
    return s_mag;
}

float hfdl_scope_hz_per_bin(void)
{
    return 12000.0f / (float)HFDL_SCOPE_FFT_SIZE;
}

uint8_t hfdl_scope_burst_active(void)
{
    return s_burst_active;
}

void hfdl_scope_set_enabled(uint8_t on)
{
    uint32_t primask;
    uint8_t was_enabled = s_enabled;

    /* CRITICAL SECTION FIX (16/09/2026, same class of bug as FT8's
     * ft8_decimator_reset() race - see that fix's comment and
     * /areas/ft8-deepsdr.md): hfdl_scope_feed() runs in the audio DMA
     * ISR (called from demod_am_process_raw()) and reads s_enabled,
     * s_pending and s_accum_count, then writes s_accum_count/
     * s_accum_i/s_accum_q. This function runs from the main loop
     * (main.c, on mode change) and was resetting those same three
     * fields with no protection - if the ISR fired mid-reset, it
     * could see a half-updated combination (e.g. s_enabled already 1
     * but s_accum_count/s_pending not yet cleared, or vice versa),
     * silently mixing stale accumulator content into the first
     * window right after a mode switch. Only the fields the ISR
     * actually touches need to be inside the critical section - it
     * never reads s_frame_ready/s_floor_power/s_burst_active/
     * s_floor_primed (those are poll()-only, same main-loop context
     * as this function, so no race there) - kept deliberately narrow
     * (three assignments) so the ISR is never blocked for longer than
     * that, unlike wrapping the RAM borrow/return below too (that
     * memset()s the borrowed region and must stay outside). Same
     * PRIMASK save/restore idiom as encoder.c/sdr_rx.c (nested-safe,
     * rather than an unconditional __enable_irq() that could
     * re-enable interrupts a caller had deliberately turned off). */
    primask = __get_PRIMASK();
    __disable_irq();
    s_enabled = on ? 1U : 0U;
    s_accum_count = 0U;
    s_pending = 0U;
    __set_PRIMASK(primask);

    /* NO-TRANSITION GUARD (16/09/2026, real-hardware regression found
     * by the project owner): every OTHER mode selection also calls
     * this with on=0 (see menu_mode_preset_callback()'s unconditional
     * every-row call, same shape as RTTY's own on/off switch). Without
     * this guard, picking FT8 (is_hfdl=false) called
     * waterfall_ram_return_from_hfdl() below UNCONDITIONALLY, which
     * memset()s the entire g_ft8_shared_ram union - wiping out the
     * ft8_fft1024_init() twiddle/Hann/bit-reversal tables that
     * menu_mode_preset_callback() had JUST populated a few lines
     * earlier in that SAME callback, even though HFDL had never
     * actually borrowed the RAM in the first place. Confirmed on real
     * hardware: FT8's spectrum/cascade never painted anything until
     * this guard was added. Only an ACTUAL transition (off->on or
     * on->off) should touch the loan at all. */
    if (s_enabled == was_enabled) {
        return;
    }

    s_frame_ready = 0U;
    s_floor_power = 1.0e-9f;
    s_burst_active = 0U;
    s_floor_primed = 0U; /* FIX (16/08/2026): without this, the very first window
                           * after enabling HFDL mode compared real in_band_sum
                           * against the near-zero seed floor above, ALWAYS
                           * reading as a burst - a guaranteed false trigger that
                           * consumed the one-shot raw capture arm before any
                           * real burst ever happened. See the priming block in
                           * hfdl_scope_poll() below. */

    /* Borrow/return the waterfall's RAM for the payload-decode
     * pipeline's own buffers (deinterleaver table, then Viterbi
     * metrics/decisions - see hfdl_payload_decode.c, which is the
     * actual consumer; this function only owns the loan's lifetime,
     * matching waterfall.h's documented contract exactly). The
     * pointer itself is NOT cached anywhere here (see
     * hfdl_scope_get_hfdl_ram()'s comment on why: this project has 0
     * bytes of RAM headroom, and even a single 4-byte cached pointer
     * overflowed it during this round) - waterfall_ram_borrow_for_hfdl()
     * always returns the same address and is safe to call repeatedly
     * (see waterfall.c), so the getter just re-derives it from
     * s_enabled on demand instead. This call still matters here even
     * though its return value is discarded: it's what actually sets
     * waterfall.c's own "borrowed" flag, which is what makes
     * waterfall_push_line()/waterfall_blit() correctly no-op while
     * HFDL mode is on. */
    if (s_enabled) {
        (void)waterfall_ram_borrow_for_hfdl(WATERFALL_RAM_BORROW_CAPACITY);
    } else {
        waterfall_ram_return_from_hfdl();
    }

    /* Modo silencioso automatico (16/09/2026) - a peticion del
     * propietario del proyecto: con las ~238 lineas normales de log
     * del proyecto (waterfall ticks, smeter, block budget...) resulta
     * muy dificil leer las lineas de HFDL que importan. Se activa solo
     * al ENTRAR de verdad en modo HFDL (esta funcion ya solo llega
     * aqui en una transicion real, ver el guard de mas arriba) y se
     * desactiva al salir, para no dejar el resto del proyecto mudo por
     * accidente si HFDL nunca se ha usado en esta sesion. */
    debug_uart_set_quiet(s_enabled ? 1u : 0u);
}

uint8_t hfdl_scope_get_enabled(void)
{
    return s_enabled;
}

/* See s_enabled above. Returns NULL if HFDL mode isn't currently on
 * (nothing borrowed) - callers (hfdl_payload_decode.c) must check for
 * that rather than assume the pointer is always valid, same
 * defensive-by-construction spirit as this project's other
 * caller-owned-buffer APIs (hfdl_deinterleaver.h etc). Re-derives the
 * pointer via waterfall_ram_borrow_for_hfdl() on every call rather
 * than caching it (see hfdl_scope_set_enabled()'s own comment on
 * why) - that function always returns the same address and is safe
 * to call repeatedly, so this costs a few instructions per call, not
 * a new static byte. Capacity is always WATERFALL_RAM_BORROW_CAPACITY
 * when non-NULL - exposed via out-param rather than a second getter
 * to keep this one call sufficient for callers that need both. */
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out)
{
    if (!s_enabled) {
        if (capacity_out != (void *)0) {
            *capacity_out = 0u;
        }
        return (void *)0;
    }
    if (capacity_out != (void *)0) {
        *capacity_out = (uint32_t)WATERFALL_RAM_BORROW_CAPACITY;
    }
    return waterfall_ram_borrow_for_hfdl(WATERFALL_RAM_BORROW_CAPACITY);
}

void hfdl_scope_reprime_floor(void)
{
    s_accum_count = 0U;
    s_pending = 0U;
    s_frame_ready = 0U;
    s_floor_power = 1.0e-9f;
    s_burst_active = 0U;
    s_floor_primed = 0U;
}

float hfdl_scope_get_snr_ratio(void)
{
    /* Same ratio the burst detector itself compares against
     * HFDL_BURST_ON_RATIO/HFDL_BURST_OFF_RATIO - NOT a calibrated dB
     * SNR figure, just in-band energy over the adaptive noise floor.
     * Useful as a per-burst relative signal-strength indicator to
     * correlate against how far a burst got (A1/A2 score) - a burst
     * that never gets much past HFDL_BURST_ON_RATIO is a weak one. */
    return s_last_in_band_sum / s_floor_power;
}

float hfdl_scope_get_floor_power(void)
{
    return s_floor_power;
}

float hfdl_scope_get_last_in_band_sum(void)
{
    return s_last_in_band_sum;
}

float hfdl_scope_get_dc_band_sum(void)
{
    return s_last_dc_band_sum;
}

float hfdl_scope_get_peak_bin_hz(void)
{
    return s_last_peak_bin_hz;
}

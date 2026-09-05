/*
 * Noise reduction by Spectral Subtraction - ported 03/08/2026 from
 * JF3HZB / T. Uebo's ESP32 nr_ss.c/h (uploaded by the project owner:
 * "Created on June 10, 2025") to this project's bare-metal GD32F450
 * pipeline. See nr_ss.h for the module's contract; this comment is
 * about what changed getting here from the original.
 *
 * SCOPE: AM/USB/LSB only, mainly SSB - NOT WFM/NFM, per the project
 * owner. demod_am.c gates the call accordingly; this file has no idea
 * what a "demod mode" even is.
 *
 * WHAT CHANGED FROM THE ORIGINAL:
 *
 *   - dsps_fft2r_fc32()/dsps_bit_rev2r_fc32() (ESP-DSP, Xtensa) ->
 *     nr_fft_transform() below: a small self-contained radix-2
 *     complex FFT, same style (local Bhaskara-I sin/cos
 *     approximation, no libm dependency in the per-block hot path) as
 *     this project's own fft.c - see its "reimplemented locally to
 *     keep the two modules decoupled" note; same reasoning applied
 *     here rather than sharing fft.c's engine (which is hardwired to
 *     FFT_SIZE=512 real-input for the panadapter - generalizing it
 *     risked that well-exercised code for a completely different
 *     use, when a fresh ~130-line engine at this module's own fixed
 *     size is simpler and safer). CMSIS-DSP's arm_cfft_f32() would
 *     have been the other option, but it needs the CommonTables
 *     twiddle infrastructure this project has deliberately never
 *     pulled in (see the Makefile's comment by the CMSIS-DSP source
 *     list) - not worth the flash for a 128-point transform this
 *     cheap to hand-roll.
 *
 *   - Nstfft: the original computed this from the ESP32 project's own
 *     SAMPLE_BUFFER_SIZE/DR (not ours - sdr.h was never provided).
 *     Here it's NR_SS_BLOCK_SAMPLES (32, matching demod_am.c's
 *     EXISTING DEC_BLOCK_SAMPLES - the SSB phasing chain's decimated
 *     rate, reused rather than duplicated) * Nframe (4) = 128. A
 *     naive port running at this project's full 192kHz audio rate
 *     instead (matching the "no decimation" note elsewhere in this
 *     codebase) would have needed Nstfft=2048 - 16x the FFT cost, and
 *     inside a hard-realtime ISR with a documented ~2.667ms/block
 *     budget (see demod_am.c's ISR TIMING INSTRUMENTATION comment,
 *     added after a past deadline-overrun hang). Running at the
 *     already-decimated 12kHz rate instead was the project owner's
 *     call, and is why this fits.
 *
 *   - extern V_Rsv (an ESP32-side potentiometer reading, 0-4095) ->
 *     nr_ss_set_strength(), called from a new NR tile in the settings
 *     menu (RADIO page, ENCODER_TARGET_NR - see main.c).
 *
 *   - extern nr_ss_debug -> dropped. This project's debug_print() is
 *     used directly wherever useful instead of a one-shot latched
 *     flag.
 *
 *   - fabs()/pow() (double) -> fabsf()/powf() (float). This project
 *     is float32_t throughout; the originals were double-precision
 *     calls sitting in an otherwise-float pipeline - presumably
 *     harmless on the ESP32's FPU, but not a habit worth carrying
 *     over (and this project links -lm already, as of the WFM
 *     atan2f() addition, so powf() is free to use here without
 *     needing its own local approximation - it only runs once per
 *     nr_ss_set_strength() call, not per-sample, so it wouldn't have
 *     mattered much either way).
 *
 *   - NR_SS_init()/NR_SS() (CamelCase) -> nr_ss_init()/nr_ss_process()
 *     - this project's naming convention (see any other module here).
 *
 *   - The overlap-add reconstruction's final scale factor: the
 *     original's Nframe==4 branch has the "correct" unity-gain
 *     normalization (ATT_Ratio * (1.0f/Nframe)) commented OUT, with a
 *     bare "*= 500" active instead - almost certainly a leftover
 *     empirical scale factor tuned for THAT project's own signal
 *     range at that point in ITS pipeline, not a deliberate part of
 *     the algorithm (the commented-out line is the mathematically
 *     "proper" one - it's what ATT_Ratio's Hann/4x-overlap derivation
 *     is FOR). This port uses that proper normalization
 *     (NR_SS_ATT_RATIO below) instead of guessing at a scale factor
 *     that has no reason to match our signal range. Expect this may
 *     still need an empirical makeup-gain tweak once tried on real
 *     hardware, same as any NR stage's "how strong should this sound"
 *     - the strength control is exactly the knob for that, but if the
 *     OVERALL level (not just the reduction amount) needs a nudge,
 *     this is the constant to revisit.
 *
 * WHAT DIDN'T CHANGE: the actual spectral-subtraction algorithm
 * (Hann window, FFT -> magnitude-domain gain via a fixed threshold ->
 * IFFT, 4-frame 75%-overlap-add) is the same as the original, same
 * constants (the amp/gain formula's 0.96043457f/0.39782422f weights,
 * the Noise_min/max threshold range).
 */

#include <string.h>
#include <math.h>

#include "nr_ss.h"

#define NR_SS_NFRAME 4U
#define NR_SS_NSTFFT (NR_SS_BLOCK_SAMPLES * NR_SS_NFRAME) /* 32*4 = 128 */

/* Threshold setting range - same as the original's Noise_min/Noise_max. */
#define NR_SS_NOISE_MIN 0.0f
#define NR_SS_NOISE_MAX 11.0f

/* sqrtf(2.667f) (Hann window, 4x/75% overlap power-compensation
 * factor), precomputed - see this file's header comment on why this
 * port uses it (unlike the original's active code path, which didn't). */
#define NR_SS_ATT_RATIO 1.63309522f

/*
 * *** 01/09/2026: moved to TCM RAM *** - same reasoning as fft.c's own
 * TCM comment (freed main-RAM headroom for the widened waterfall;
 * none of these are DMA targets - this whole module runs as plain
 * CPU-computed DSP inside the AM/SSB audio chain).
 */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

static float s_dat_stfft[2U * NR_SS_NSTFFT] TCMRAM_BSS; /* interleaved re/im, in place through FFT->gain->IFFT */
static float s_sigf[NR_SS_NFRAME][NR_SS_NSTFFT] TCMRAM_BSS; /* last NR_SS_NFRAME processed (real) frames, for overlap-add */
static float s_sigt[NR_SS_NSTFFT] TCMRAM_BSS; /* input ring buffer */
static float s_win[NR_SS_NSTFFT] TCMRAM_BSS; /* Hann window */
static float s_twiddle_cos[NR_SS_NSTFFT / 2U] TCMRAM_BSS;
static float s_twiddle_sin[NR_SS_NSTFFT / 2U] TCMRAM_BSS;
static uint16_t s_bitrev[NR_SS_NSTFFT] TCMRAM_BSS;
static uint32_t s_pt_w;   /* ring buffer write pointer into s_sigt */
static uint32_t s_pt_frm; /* which s_sigf[] row gets written next (mod NR_SS_NFRAME) */
static float s_inv_th = 1.0f; /* 1/threshold - see nr_ss_set_strength() */
static uint8_t s_enabled; /* master on/off - see nr_ss_set_enabled()'s comment in nr_ss.h */

/*
 * Local sin/cos, same Bhaskara-I approximation fft.c/gd32_i2s.c
 * already use in this project (see fft.c's "reimplemented locally to
 * keep the two modules decoupled" note - same reasoning applied here
 * rather than sharing code across modules). Only used at init (window
 * + twiddle table generation), never in the per-block hot path.
 */
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

/*
 * Self-contained radix-2, decimation-in-time, in-place complex FFT
 * (NR_SS_NSTFFT=128 points - fixed size, no general-N support needed
 * here). `re_im` is NR_SS_NSTFFT complex samples, interleaved
 * re0,im0,re1,im1,... `inverse` runs the SAME forward transform on
 * the conjugated input then conjugates the result back - the classic
 * "IFFT via FFT" trick (avoids needing a second twiddle table for the
 * reverse direction), same approach the original ESP-DSP-based code
 * used (negate imaginary before and after dsps_fft2r_fc32()).
 */
static void nr_fft_transform(float *re_im, int inverse)
{
    uint32_t n;
    uint32_t stage_size;

    if (inverse) {
        for (n = 0; n < NR_SS_NSTFFT; n++) { re_im[2U * n + 1U] = -re_im[2U * n + 1U]; }
    }

    /* Bit-reversal permutation, in place. */
    for (n = 0; n < NR_SS_NSTFFT; n++) {
        uint32_t r = s_bitrev[n];
        if (r > n) {
            float tr = re_im[2U * n];
            float ti = re_im[2U * n + 1U];
            re_im[2U * n]      = re_im[2U * r];
            re_im[2U * n + 1U] = re_im[2U * r + 1U];
            re_im[2U * r]      = tr;
            re_im[2U * r + 1U] = ti;
        }
    }

    /* Iterative Cooley-Tukey butterflies, stage_size doubling each
     * pass (2, 4, 8, ..., NR_SS_NSTFFT). */
    for (stage_size = 2U; stage_size <= NR_SS_NSTFFT; stage_size <<= 1U) {
        uint32_t half = stage_size >> 1U;
        uint32_t tw_step = NR_SS_NSTFFT / stage_size;
        uint32_t group;

        for (group = 0; group < NR_SS_NSTFFT; group += stage_size) {
            uint32_t k;
            for (k = 0; k < half; k++) {
                uint32_t ti = k * tw_step;
                float wr = s_twiddle_cos[ti];
                float wi = s_twiddle_sin[ti];
                uint32_t a = group + k;
                uint32_t b = a + half;
                float br = re_im[2U * b];
                float bi = re_im[2U * b + 1U];
                float tr = wr * br - wi * bi;
                float tv = wr * bi + wi * br;
                float ar = re_im[2U * a];
                float ai = re_im[2U * a + 1U];

                re_im[2U * a]       = ar + tr;
                re_im[2U * a + 1U] = ai + tv;
                re_im[2U * b]       = ar - tr;
                re_im[2U * b + 1U] = ai - tv;
            }
        }
    }

    if (inverse) {
        for (n = 0; n < NR_SS_NSTFFT; n++) { re_im[2U * n + 1U] = -re_im[2U * n + 1U]; }
    }
}

void nr_ss_init(void)
{
    uint32_t n;
    uint32_t bits = 0U;
    uint32_t size = NR_SS_NSTFFT;

    /* Exact log2(NR_SS_NSTFFT) by counting shifts (guaranteed a power
     * of two - NR_SS_BLOCK_SAMPLES=32 * NR_SS_NFRAME=4). */
    while (size > 1U) { size >>= 1U; bits++; }

    for (n = 0; n < NR_SS_NSTFFT / 2U; n++) {
        float angle = -2.0f * 3.14159265358979f * (float)n / (float)NR_SS_NSTFFT;
        s_twiddle_cos[n] = cosf_approx_local(angle);
        s_twiddle_sin[n] = sinf_approx_local(angle);
    }

    for (n = 0; n < NR_SS_NSTFFT; n++) {
        uint32_t v = n;
        uint32_t r = 0U;
        uint32_t b;
        for (b = 0; b < bits; b++) {
            r = (r << 1U) | (v & 1U);
            v >>= 1U;
        }
        s_bitrev[n] = (uint16_t)r;
    }

    /* Hann window, same t=i/N (not i/(N-1)) as the original. */
    for (n = 0; n < NR_SS_NSTFFT; n++) {
        float t = (float)n / (float)NR_SS_NSTFFT;
        s_win[n] = 0.5f - 0.5f * cosf_approx_local(2.0f * 3.14159265358979f * t);
    }

    memset(s_sigt, 0, sizeof(s_sigt));
    memset(s_sigf, 0, sizeof(s_sigf));
    s_pt_w = 0U;
    s_pt_frm = 0U;
    s_enabled = 0U; /* off by default - matches nr_ss_set_strength(0)'s
                      * own default below, and the project owner's ask
                      * that OFF be a genuine, distinct switch. */
    nr_ss_set_strength(0U); /* see its own comment */
}

void nr_ss_set_enabled(uint8_t on)
{
    s_enabled = on ? 1U : 0U;
}

uint8_t nr_ss_get_enabled(void)
{
    return s_enabled;
}

void nr_ss_set_strength(uint16_t v)
{
    float th_nr_val;
    float th_nr;

    if (v > 4095U) { v = 4095U; }
    /*
     * Same mapping the original ESP32 code used for its V_Rsv pot: a
     * 0-4095 value linearly spread across NR_SS_NOISE_MIN..MAX, then
     * used as a base-10 exponent (a dB-like threshold, hence the
     * pow/log10 framing even though nothing here is calibrated to a
     * real dB reference - same "uncalibrated but useful" spirit as
     * this project's own spectrum dB scale).
     *
     * v=0 -> th_nr_val=NOISE_MIN=0 -> th_nr=10^0=1 -> inv_th=1, which
     * makes every bin's g_reduce=amp*1 clamp to 1.0 (no reduction)
     * unless amp<1 - i.e. an effectively-transparent bypass at the
     * strength control's minimum, the same natural "off" behavior the
     * original design had, not a special-cased bypass path.
     */
    th_nr_val = (NR_SS_NOISE_MAX - NR_SS_NOISE_MIN) * (float)v / 4096.0f + NR_SS_NOISE_MIN;
    th_nr = powf(10.0f, th_nr_val);
    s_inv_th = 1.0f / th_nr;
}

void nr_ss_process(float *audio, uint32_t n)
{
    uint32_t i;
    uint32_t frm[NR_SS_NFRAME];

    /* 1. Feed the ring buffer with this block's raw input. */
    for (i = 0; i < n; i++) {
        s_sigt[s_pt_w] = audio[i];
        s_pt_w++;
        if (s_pt_w == NR_SS_NSTFFT) { s_pt_w = 0U; }
    }

    /* 2. Window the last NR_SS_NSTFFT samples (oldest-to-newest,
     * starting right after where step 1 left the write pointer - same
     * read-then-advance walk the original used) into the complex FFT
     * buffer, imaginary part zeroed (real input). */
    for (i = 0; i < NR_SS_NSTFFT; i++) {
        s_dat_stfft[2U * i]      = s_sigt[s_pt_w] * s_win[i];
        s_dat_stfft[2U * i + 1U] = 0.0f;
        s_pt_w++;
        if (s_pt_w == NR_SS_NSTFFT) { s_pt_w = 0U; }
    }

    nr_fft_transform(s_dat_stfft, 0);

    /* 3. Spectral subtraction. amp uses the same alpha-max-beta-min
     * style weighted sum of |re|/|im| as the original (and this
     * project's own AM envelope-adjacent code elsewhere) - cheaper
     * than a true sqrt(re^2+im^2) magnitude and accurate enough for a
     * noise-floor gain decision. g clamped to <=1.0: this can only
     * ATTENUATE a bin, never boost it. */
    for (i = 0; i < NR_SS_NSTFFT; i++) {
        float xr = fabsf(s_dat_stfft[2U * i]);
        float xi = fabsf(s_dat_stfft[2U * i + 1U]);
        float amp;
        float g;

        amp = (xr > xi) ? (0.96043457f * xr + 0.39782422f * xi)
                         : (0.96043457f * xi + 0.39782422f * xr);
        amp *= (1.0f / (float)NR_SS_NSTFFT);

        g = amp * s_inv_th;
        if (g > 1.0f) { g = 1.0f; }
        s_dat_stfft[2U * i]      *= g;
        s_dat_stfft[2U * i + 1U] *= g;
    }

    nr_fft_transform(s_dat_stfft, 1);

    for (i = 0; i < NR_SS_NSTFFT; i++) {
        s_sigf[s_pt_frm][i] = s_dat_stfft[2U * i] * (1.0f / (float)NR_SS_NSTFFT);
    }
    s_pt_frm++;
    if (s_pt_frm == NR_SS_NFRAME) { s_pt_frm = 0U; }

    /* 4. Overlap-add: reconstruct this block's `n` output samples
     * from the last NR_SS_NFRAME processed frames, same indexing the
     * original's #if Nframe==4 branch hardcoded, generalized off
     * NR_SS_NFRAME instead. */
    for (i = 0; i < NR_SS_NFRAME; i++) {
        uint32_t k = s_pt_frm + i;
        if (k >= NR_SS_NFRAME) { k -= NR_SS_NFRAME; }
        frm[i] = k;
    }
    for (i = 0; i < n; i++) {
        float sum = 0.0f;
        uint32_t f;

        for (f = 0; f < NR_SS_NFRAME; f++) {
            sum += s_sigf[frm[f]][i + (NR_SS_NFRAME - 1U - f) * (NR_SS_NSTFFT / NR_SS_NFRAME)];
        }
        audio[i] = sum * NR_SS_ATT_RATIO * (1.0f / (float)NR_SS_NFRAME);
    }
}

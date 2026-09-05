#ifndef DEMOD_AM_H
#define DEMOD_AM_H

#include <stdint.h>

/*
 * AM envelope demodulator, from baseband I/Q to the speaker.
 *
 * SIGNAL PATH: the QSD mixes the antenna signal with the quadrature
 * LO, so a station tuned on the VFO arrives here as a complex
 * baseband signal centered at (or near) DC. For AM, the transmitted
 * information IS the envelope, and the envelope of the complex
 * baseband is simply |I + jQ| - which has the classic property of
 * being INSENSITIVE to small tuning error: a carrier offset only
 * rotates the phasor, |z| doesn't change. You don't need to zero-beat
 * the station; anywhere within the audio filter's reach sounds the
 * same.
 *
 * Per-block chain (at the full 192kHz rate, no decimation - the DAC
 * runs at 192kHz on the same I2S bus anyway). As of 30/07/2026, the
 * two heaviest stages run through CMSIS-DSP (arm_biquad_cascade_df1_f32,
 * arm_cmplx_mag_f32) instead of a hand-rolled per-sample scalar loop -
 * see CMSIS/DSP/Include/cmsis_compiler.h for why that needed a small
 * shim on top of this project's pre-5.0 CMSIS-Core. Everything else
 * (DC blocker, AGC) is cheap enough per-sample that a CMSIS block
 * call wouldn't pay for itself, so it stays scalar:
 *
 *   0a. DOWN-MIX: digitally shift the raw IQ by DEMOD_IF_OFFSET_HZ
 *      (see the LOW-IF TUNING note below for why), only while
 *      s_if_offset_active is set. Skip this entirely and everything
 *      downstream is unchanged from before.
 *   0b. CHANNEL FILTER: a 4th-order Butterworth low-pass (2 cascaded
 *      CMSIS biquad DF1 stages), applied IDENTICALLY to I and Q via
 *      arm_biquad_cascade_df1_f32() = a complex low-pass centered on
 *      the VFO (-3dB at ~4kHz, ~-57dB by +/-20kHz). This is what
 *      makes the receiver demodulate AT THE CENTER LINE of the
 *      panadapter instead of hearing the strongest station anywhere
 *      in the +/-96kHz window. Must come BEFORE the envelope
 *      detector: |z| is nonlinear and would intermodulate everything
 *      it sees. Coefficients in CHF_COEFFS were designed offline via
 *      bilinear transform with prewarping (see the design script
 *      referenced in the project notes) - to retune, regenerate the
 *      whole 10-value array, don't hand-edit individual numbers.
 *   1. |z| via arm_cmplx_mag_f32() on the filtered, interleaved I/Q -
 *      true magnitude using the Cortex-M4F's hardware VSQRT.F32,
 *      replacing the earlier alpha-max-beta-min approximation (no
 *      accuracy trade-off needed once sqrt is a single FPU
 *      instruction rather than a libm call).
 *   2. DC blocker (one-pole HPF ~15Hz): removes the carrier's DC
 *      term so the AGC and the DAC see only audio. Left as a scalar
 *      per-sample loop - a single MAC, not worth a CMSIS block call.
 *   3. Audio low-pass: another 4th-order Butterworth (2 more CMSIS
 *      biquad DF1 stages, separate instance/state from the channel
 *      filter), -3dB at ~6kHz - steeper (~24dB/oct) than the two
 *      one-pole sections it replaces (~12dB/oct), same corner.
 *   4. AGC: instant-attack / slow-release peak follower normalizing
 *      to a comfortable output level, with bounded gain so pure
 *      noise doesn't get amplified to full scale. Scalar (branchy
 *      control loop, not a filter CMSIS has a block primitive for).
 *   5. Same sample to L and R, int16, into the I2S TX stream buffer.
 *
 * RAM BUDGET (added by the CMSIS-DSP migration, all static/.bss, ISR-
 * safe): i_buf + q_buf (512*4*2 = 4096B) + iq_cplx (512*2*4 = 4096B)
 * + env (512*4 = 2048B) + biquad state (3 instances * 2 stages * 4
 * floats * 4B = 96B) = ~10.2KB. The decimated SSB chain (30/07/2026)
 * adds: 2x decimator state ((161+512-1)*4*2 = 5376B) + Hilbert state
 * ((63+32-1)*4 = 376B) + interpolator state ((10+32-1)*4 = 164B) +
 * the 12kHz working buffers (4x32 floats + delay hist/work 31+63
 * floats = ~890B) = ~6.8KB more, ~17KB total. WFM (31/07/2026) adds:
 * its own audio-LPF biquad state (2 stages * 4 floats * 4B = 32B) +
 * discriminator/de-emphasis scalars (3 floats = 12B) - negligible,
 * ~17KB total is still accurate to a rounding error. NFM (also
 * 31/07/2026, right after WFM) adds its own channel-filter biquad
 * state, same size as WFM's audio-LPF state (2 instances * 2 stages *
 * 4 floats * 4B = 64B) and no new scalars (shares WFM's discriminator
 * state) - again negligible. Combined with the existing s_audio_out
 * buffer and everything else in fft.c/waterfall.c, still comfortably
 * within the GD32F450VET6's SRAM.
 *
 * LOW-IF TUNING - history (30/07/2026), why this exists, and why it
 * was tried twice: this is a zero-IF/direct-conversion QSD, so tuning
 * the LO exactly on the station puts BOTH the wanted carrier AND the
 * QSD's own LO-leakage self-mixing artifact at exactly 0Hz in the
 * complex baseband. For AM you can't just notch DC to remove the
 * leakage - the carrier IS the thing envelope detection needs, so
 * removing it removes the signal too. That's the blob of distortion
 * that always sits on the center line; it's the zero-IF architecture
 * itself, not this filter chain.
 *
 *   1st attempt (Fs/8 = 24kHz, cos/sin NCO table): implemented, then
 *   caused a broadband noise floor / jumping spectrum on hardware and
 *   was fully reverted. The person confirmed the I2S signals and the
 *   MS5351's LO were both fine on their own, which ruled out "the
 *   retuning broke the LO" as the explanation.
 *
 *   Root cause (found afterwards, unrelated to tuning): the SAME day,
 *   the project-wide build moved from -O0 to -O2 for the framerate
 *   fix. i2c_bitbang.c's delay_i2c() - an admittedly uncalibrated
 *   NOP-counting loop - got compiled tighter at -O2, shrinking its
 *   real wall-clock delay enough to slice into the I2C bit-bang edge
 *   timing on the bus shared by the AIC3204 codec AND the MS5351 - a
 *   garbled I2C write during codec/LO setup, not the tuning offset,
 *   was the actual cause. Fixed by pinning delay_i2c() (and touch.c's
 *   equally-uncalibrated delay_us_approx()) to -O0 via
 *   __attribute__((optimize("O0"))), independent of the project's
 *   CFLAGS from then on.
 *
 *   2nd attempt (this one, Fs/4 = 48kHz, sign-flip rotation): once
 *   the real bug above was found and fixed, low-IF tuning was tried
 *   again with a different, simpler algorithm (provided by the
 *   project owner as known-working elsewhere): at exactly Fs/4 the
 *   rotation e^(+j*2*pi*n/4) cycles through 1, j, -1, -j, so it needs
 *   NO multiplications at all - just sign flips and an I/Q swap. See
 *   the block comment above s_if_offset_active in demod_am.c for the
 *   exact per-sample mapping.
 *
 * Mechanics (same for either offset value): the LO is tuned
 * DEMOD_IF_OFFSET_HZ below the selected station (see main.c's
 * tune_encoder_poll()), so the wanted signal arrives away from 0Hz in
 * the raw IQ while the LO-leakage artifact (which self-mixes to 0Hz
 * regardless of tuning) stays put. demod_am_process_raw() then
 * digitally mixes the block back so the wanted signal lands on DC -
 * right where CHF_COEFFS already expects it, no filter redesign
 * needed - moving the leakage artifact safely outside the channel
 * filter's ~+/-4kHz passband. s_if_offset_active (see demod_am.c)
 * keeps the digital down-mix in sync with whatever's actually
 * programmed on the LO: it starts at 0 (the boot tune is still the
 * untouched byte-exact captured replay, no offset - see main.c) and
 * only tune_encoder_poll() turns it on, once the LO is genuinely
 * offset-tuned. Getting this out of sync shifts the wanted signal OUT
 * of the channel filter's passband instead of into it.
 *
 * SIDE EFFECT ON THE PANADAPTER: the waterfall/spectrum FFT still
 * reads the raw (non-shifted) IQ from sdr_rx_poll_block_iq(), and its
 * center line is defined as "wherever the LO actually is" - so with
 * this offset tuning, a station you land on will show up
 * DEMOD_IF_OFFSET_HZ off from dead-center rather than exactly on the
 * center line. This is expected and is how real low-IF SDR receivers
 * using this trick normally look; it's cosmetic only (the demod audio
 * is correct either way). If you want the panadapter's center line to
 * track the *selected* station instead of the raw LO, that's a
 * separate change in spectrum.c/main.c (shift the displayed axis, or
 * apply the same down-mix before the FFT) - out of scope here.
 *
 * SSB (USB/LSB) - added 30/07/2026, phasing method, DECIMATED
 * architecture (same day, second iteration): shares steps 0a-0c with
 * AM (down-mix, channel filter) unchanged, then branches: decimate
 * I/Q by 16 to 12kHz (arm_fir_decimate_f32), Hilbert-shift Q and
 * delay-match I at that rate, combine into single-sideband audio, and
 * interpolate back up to 192kHz (arm_fir_interpolate_f32) straight
 * into the shared s_env[] buffer - everything downstream (DC blocker,
 * audio LPF, AGC, output) is identical to AM. See the PIPELINE
 * comment above DECIM_COEFFS in demod_am.c for the design numbers.
 *
 * WHY DECIMATED - the first iteration ran a Hilbert FIR at the full
 * 192kHz rate and real DWT cycle counts showed the problem: the ISR
 * fit its own block budget (~79% at 63 taps) but at NVIC priority 6
 * that duty cycle starved the main loop, stretching frame times
 * 3.6-4.8x - spectrum/touch/encoder all crawled, reported as "the
 * system hangs when switching mode". At 12kHz the Hilbert covers
 * 300Hz-6kHz FAR better than the full-rate version ever could
 * (|H|=0.913 @ 300Hz vs 0.164) while the whole SSB chain costs a
 * fraction of the old one - the decimator only evaluates at the
 * output rate, so even its 161 anti-alias taps are cheap. Verify on
 * hardware with demod_am_get_last_cycles() - expected to land close
 * to the AM path's ~136k cycles, far from the old 422k.
 *
 * EXECUTION CONTEXT - IMPORTANT: demod_am_process_raw() is designed
 * to run from the RX DMA interrupt (registered via
 * sdr_rx_set_block_hook()), NOT from the main loop. The main loop
 * blocks for longer than one 2.67ms block while drawing a spectrum
 * frame, so audio fed from there would gap ~30 times a second. In
 * the ISR it's immune to whatever the display is doing. The FPU is
 * safe to use here: Cortex-M4F lazy stacking handles float in
 * interrupt context.
 */

/* Fixed output gain trim applied after the AGC (1.0 = none). */
#define DEMOD_AM_GAIN 1.0f

/* Demodulation mode. AM is the default (matches all behavior before
 * 30/07/2026). USB/LSB share the same down-mix + channel filter
 * front-end as AM - see the SSB note above for how they diverge
 * afterwards, and its known low-frequency limitation. NFM ALSO shares
 * that down-mix front-end (unlike WFM) but with its OWN, wider
 * channel filter - see the NFM note below. WFM shares neither - see
 * its own note below that. */
typedef enum {
    DEMOD_MODE_AM = 0,
    DEMOD_MODE_USB,
    DEMOD_MODE_LSB,
    DEMOD_MODE_NFM,
    DEMOD_MODE_WFM,
    DEMOD_MODE_SAM /* synchronous AM (both sidebands) - 21/08/2026, see sam.h */
} demod_mode_t;

/*
 * NFM (narrowband FM - ham/PMR-style voice channels) - added
 * 31/07/2026, right after WFM, and deliberately built to share as
 * much of WFM's machinery as makes sense while keeping the parts that
 * genuinely differ separate:
 *
 * SHARES WITH WFM: the discriminator itself (delay-and-conjugate-
 * multiply -> atan2f(), see WFM's note above for the full derivation
 * - factored into a single fm_discriminate() helper in demod_am.c
 * that both modes call, rather than duplicating the loop) and its
 * shared prev-sample state (s_fm_i_prev/s_fm_q_prev) - safe to share
 * since WFM and NFM are never active at once (same "one stale sample
 * on mode entry" tradeoff already accepted for that state).
 *
 * DIFFERS FROM WFM: NFM keeps the down-mix + channel filter front-end
 * (like AM/USB/LSB), it just uses its OWN channel filter
 * (NFM_CHF_COEFFS in demod_am.c) instead of CHF_COEFFS - a narrowband
 * FM channel needs the down-mix's DC-leakage workaround just as much
 * as AM/SSB do (small, single-station bandwidth, no reason to burn
 * the full +/-96kHz WFM needs), but CHF_COEFFS' ~4kHz corner is sized
 * for AM/SSB's plain audio bandwidth and would clip a narrowband FM
 * signal's frequency-modulated SIDEBANDS before they ever reach the
 * discriminator - not just filter the resulting audio, but actively
 * distort the FM information itself. NFM_CHF_COEFFS is instead sized
 * to Carson's-rule bandwidth for a 12.5kHz-spaced narrowband channel
 * (+/-2.5kHz max deviation, ~3kHz voice) - see its own comment in
 * demod_am.c for the exact numbers. main.c's apply_lo_tune() and the
 * MODE cycle need NO special case for NFM here (unlike WFM) - it's
 * grouped with AM/USB/LSB in the "keep the low-IF offset" branch by
 * simply NOT being WFM, since that's the only mode apply_lo_tune()
 * singles out.
 *
 * NO DE-EMPHASIS: unlike WFM, NFM does NOT apply the 50us de-emphasis
 * (or any de-emphasis) - narrowband ham/PMR FM voice radios don't
 * follow the same standardized broadcast pre-emphasis convention WFM
 * relies on, so guessing a time constant here would be as likely to
 * make things WORSE (audibly wrong tonal balance) as better. If NFM
 * audio comes out sounding thin/dull on real signals, that's the
 * first thing to revisit - not a sign anything else is broken.
 *
 * AUDIO LPF: reuses the EXISTING ALPF_COEFFS/s_alpf_inst (~6kHz, same
 * as AM/SSB) rather than WFM's wider WFM_ALPF_COEFFS - narrowband FM
 * voice audio is the same ballpark bandwidth as AM/SSB voice, no
 * reason for a separate filter instance here.
 *
 * ASSUMPTION WORTH FLAGGING: NFM_CHF_COEFFS is sized for 12.5kHz
 * channel spacing (the common case for ham/PMR narrowband FM in
 * Europe). If this ever needs to receive 25kHz-spaced channels
 * (+/-5kHz deviation) instead, NFM_CHF_COEFFS' corner is too narrow
 * and would need widening - not a code structure change, just new
 * filter coefficients.
 */

/*
 * WFM (broadcast wide FM, mono) - added 31/07/2026.
 *
 * WHY NO DOWN-MIX AND NO CHANNEL FILTER (per the project owner's
 * request): AM/USB/LSB are narrowband, so it's worth spending the
 * complex baseband's full +/-24kHz (was +/-96kHz @ 192kHz before
 * 04/08/2026 - see sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment) just to
 * reach one station, and the CHF_COEFFS channel filter (-3dB ~4kHz)
 * is what makes that possible. A broadcast FM channel is ~200kHz wide
 * with up to +/-75kHz peak deviation - CHF_COEFFS would butcher it
 * beyond recognition, and the DEMOD_IF_OFFSET_HZ (12kHz, was 48kHz)
 * low-IF down-mix would shove a good chunk of that 200kHz channel
 * outside the Nyquist window instead of centering it. So WFM skips
 * BOTH steps entirely (see the mode check around 0b/0c in
 * demod_am_process_raw()) and runs the discriminator directly on the
 * raw, full-bandwidth deinterleaved I/Q - this only works because the
 * station is tuned dead-center (LO exactly on frequency, no offset).
 *
 * *** 04/08/2026: this whole approach is now fundamentally
 * insufficient for WFM, not just imperfect *** - a +/-24kHz Nyquist
 * window (down from +/-96kHz) genuinely cannot contain a ~200kHz-wide
 * broadcast FM channel AT ALL, centered or not; this isn't a filter
 * or offset tuning problem anymore, it's a hard physical bandwidth
 * limit. WFM stays wired up and selectable (per the project owner:
 * accept the degradation for now rather than block today's 48kHz work
 * on it, or hide the mode), but expect it to sound badly aliased/
 * distorted, not just "a bit narrow", until a future dual-rate design
 * gives WFM its own wider-bandwidth capture again (see this project's
 * 48kHz migration notes for that deferred plan).
 *
 * main.c's tune_encoder_poll() and
 * the MODE button handler both need to know this: whenever the mode
 * is WFM, they must program the LO at the plain selected frequency
 * (not freq - DEMOD_IF_OFFSET_HZ) and call
 * demod_am_set_if_offset_active(0) - getting that out of sync would
 * center the discriminator on empty spectrum 12kHz off the actual
 * station. This board's tuning range (4.8-180MHz, see main.c's
 * TUNE_MIN_HZ/MAX_HZ) already covers the 88-108MHz broadcast band -
 * in fact the byte-exact captured boot tune (MS5351_CAPTURED_LO_HZ,
 * see ms5351.h) sits at 90.8MHz, right in it.
 *
 * BANDWIDTH BUDGET: +/-96kHz of complex Nyquist bandwidth against a
 * +/-75kHz-deviation, ~200kHz-wide broadcast signal is tight but
 * workable for MONO reception - there's no room left to also decode
 * the 19kHz stereo pilot + 38kHz L-R subcarrier (stereo would need
 * clean baseband out past 53kHz, which doesn't fit inside +/-96kHz
 * with any anti-alias margin left). This implementation is mono-only;
 * the WFM audio LPF below is deliberately steep enough to knock the
 * pilot down hard (~-9dB @ 19kHz, ~-16dB @ 23kHz, ~-36dB @ 38kHz -
 * verified numerically) rather than let it beat audibly against the
 * audio band.
 *
 * DISCRIMINATOR: delay-and-conjugate-multiply (a.k.a. differentiate-
 * and-cross-multiply), the standard SDR FM discriminator - avoids an
 * actual derivative/differencing of the noisy instantaneous phase.
 * For consecutive complex baseband samples z[n] = I[n] + jQ[n]:
 *
 *   z[n] * conj(z[n-1]) = (I[n]*I[n-1] + Q[n]*Q[n-1])
 *                       + j*(Q[n]*I[n-1] - I[n]*Q[n-1])
 *
 * ...and the ANGLE of that product is exactly the phase step between
 * consecutive samples, i.e. the instantaneous frequency - which is
 * what FM demodulation wants. Computed per-sample via atan2f() from
 * newlib (NOT CMSIS's arm_atan2_f32(): this project's CMSIS-DSP tree
 * only carries FilteringFunctions/ComplexMathFunctions/etc - see the
 * Makefile's C_SOURCES comment - arm_atan2_f32.c's home,
 * Source/FastMathFunctions/, isn't part of it, so pulling that one
 * function in would mean sourcing extra files from upstream CMSIS-DSP
 * rather than just adding one line to the Makefile). At the 200MHz
 * core clock / 192kHz sample rate this project runs at, that's
 * ~1041 cycles of budget per sample with NOTHING else running in this
 * mode (no CHF, no down-mix) - libm's atan2f should have plenty of
 * room, but this is reasoned from the datasheet numbers, not measured
 * on hardware. VERIFY with demod_am_get_last_cycles() after flashing,
 * same as the SSB path was verified (see its note above) - if it ever
 * comes back close to the ~534k-cycle full block budget, that's the
 * signal to reconsider (e.g. sourcing arm_atan2_f32.c for real, or a
 * cheaper polynomial atan2 approximation).
 *
 * No ambiguity/wraparound risk: at +/-75kHz deviation the phase step
 * per sample tops out at ~2.454 rad (2*pi*75000/192000), comfortably
 * inside atan2's +/-pi range.
 *
 * GAIN: WFM_DISC_GAIN (in demod_am.c) scales the raw radians-per-
 * sample output so a full +/-75kHz deviation lands in the same
 * ballpark as AM/SSB's pre-AGC signal level - not that it matters
 * much, since the AGC peak-normalizes downstream regardless (see step
 * 4 in demod_am.c), just keeps the AGC's gain excursion sane rather
 * than starting from a near-zero or wildly-clipped raw signal.
 *
 * DE-EMPHASIS + AUDIO LPF: broadcast FM pre-emphasizes highs at the
 * transmitter (a fixed time-constant, 50us in Europe/CCIR - this
 * receiver assumes that, since it's built and tuned in Spain; US/NTSC
 * broadcasts use 75us instead, see WFM_DEEMPH_ALPHA's comment in
 * demod_am.c if that ever needs to be selectable), so the receiver
 * must de-emphasize (a matching single-pole LOWpass, -3dB ~3.18kHz)
 * or the audio comes out badly treble-heavy. Applied AFTER the shared
 * DC blocker (step 2, unchanged - here it mainly soaks up any DC term
 * from a small tuning/LO error, not a carrier) and BEFORE a WFM-
 * specific audio LPF (WFM_ALPF_COEFFS, ~15kHz -3dB, replacing
 * ALPF_COEFFS' ~6kHz corner only for this mode - AM/SSB's narrower
 * voice-bandwidth LPF would strip most of the fidelity broadcast FM
 * is for). Everything after that (AGC, L/R output) is the same shared
 * code AM/SSB already use.
 */

/* Selects the demodulation mode. Safe to call at any time, including
 * from the main loop while the ISR is running (see demod_am.c: the
 * mode variable is a single aligned uint8_t, and worst case a stale
 * read for one block just demodulates that one block in the previous
 * mode - not worth a critical section for). */
void demod_am_set_mode(demod_mode_t mode);

/* Query the current mode - used by main.c for the on-screen mode
 * label. */
demod_mode_t demod_am_get_mode(void);

/* Live SAM carrier-frequency-offset reading, Hz (21/08/2026) - see
 * sam.h's own top comment and demod_am.c's s_sam declaration. Only
 * meaningful while DEMOD_MODE_SAM is active and has had time to
 * settle (several hundred ms to a few seconds after retuning) -
 * reads 0 (or a stale value) otherwise. For MS5351 PPM calibration:
 * tune to a station with a precisely known carrier frequency, wait
 * for this to settle, then ppm = 1e6 * this_value / tuned_freq_hz. */
float demod_am_get_sam_carrier_hz(void);

/*
 * AM/SSB AUDIO FILTER WIDTH - added 02/08/2026, extended to a 3-way
 * selector 02/08/2026 (per the project owner: the main-screen BW
 * badge itself became the control, so a plain WIDE/NARROW toggle grew
 * a third step). Picks between three dedicated audio LPFs - AUDIO_BW_4K0
 * (~4.0kHz, ALPF_4K0_COEFFS), AUDIO_BW_2K3 (~2.3kHz, ALPF_2K3_COEFFS),
 * AUDIO_BW_1K8 (~1.8kHz, ALPF_1K8_COEFFS) - same idea as the WIDE/
 * NARROW/NARROWER filter switch on a typical HF transceiver for
 * crowded-band SSB/AM work. See each *_COEFFS array's comment in
 * demod_am.c for its design/verification.
 *
 * NONE of these three is the same filter NFM uses (ALPF_COEFFS,
 * s_alpf_inst, still ~6kHz, completely untouched by this selector) -
 * deliberately kept separate so tightening AM/SSB's audio bandwidth
 * can never accidentally narrow NFM's too. Setting this while in NFM
 * is harmless (no audible effect until you switch to AM/USB/LSB - the
 * state itself is unconditional, only its APPLICATION in
 * demod_am_process_raw() is mode-gated).
 *
 * *** 01/09/2026: WFM is DIFFERENT from NFM here - this selector now
 * DOES have an audible, immediate effect in WFM too *** - see
 * demod_wfm_process_raw()'s own WFM_ALPF_WIDE/NORM/NARROW_COEFFS
 * selection in demod_am.c. Same three enum values, same underlying
 * s_audio_bw storage and BW tile, but a completely different set of
 * three filters (15kHz/8kHz/4kHz, not 4.0kHz/2.3kHz/1.8kHz) - the
 * value only means one specific filter once combined with the
 * CURRENT mode, never on its own. main.c's badges_draw()/
 * menu_tile_bw_refresh() swap the displayed label text between the
 * two meanings depending on demod_am_get_mode(), so the screen never
 * shows "BW 4K0" while WFM is actually narrowed to 4kHz or vice versa.
 *
 * Same "single aligned uint8_t-sized enum, no critical section"
 * reasoning as demod_am_set_mode()'s comment above - safe to call from
 * the main loop while the ISR runs.
 */
typedef enum {
    AUDIO_BW_4K0 = 0,
    AUDIO_BW_2K3,
    AUDIO_BW_1K8
} audio_bw_t;

void demod_am_set_audio_bw(audio_bw_t bw);
audio_bw_t demod_am_get_audio_bw(void);

/*
 * WFM pre-discriminator channel filter width - added 01/09/2026, per
 * the project owner: a control over the RAW COMPLEX BASEBAND
 * bandwidth reaching the FM discriminator, completely separate from
 * audio_bw_t/WFM_ALPF_* above (which shape the DEMODULATED audio,
 * after the discriminator has already run). See WFM_IFBW_NARROW_
 * COEFFS' comment in demod_am.c for the full filter design/
 * verification and why 80kHz specifically. WIDE (default) is a
 * genuine no-op - the discriminator sees completely unfiltered I/Q,
 * exactly as it always has - so nobody who never touches this control
 * gets a different signal path than before this was added. NARROW
 * trades a small amount of margin right at full +/-75kHz deviation
 * (~0.4dB, ~22.5us group delay - both verified negligible) for real
 * rejection of anything sitting further out in the +/-96kHz capture -
 * useful on a crowded band, or a weak station near a strong neighbor.
 *
 * Same "plain uint8_t-sized enum, no critical section" reasoning as
 * demod_am_set_mode()'s comment above - safe to call from the main
 * loop while the ISR runs.
 */
typedef enum {
    WFM_IFBW_WIDE = 0,
    WFM_IFBW_NARROW
} wfm_ifbw_t;

void demod_am_set_wfm_ifbw(wfm_ifbw_t bw);
wfm_ifbw_t demod_am_get_wfm_ifbw(void);

/*
 * --- AGC profile (release speed / manual bypass) --------------------
 *
 * Added 31/07/2026, per the project owner: the classic MAN/SLW/MED/FST
 * selector found on most communications receivers, applied to the
 * SAME shared AGC peak-detector every mode already uses (step 4 of
 * demod_am_process_raw() - see AGC_TARGET's comment in demod_am.c).
 * Attack stays instant in every profile (a real signal should always
 * grab the gain down immediately, no reason to make that adjustable)
 * - only the RELEASE time constant changes:
 *
 *   AGC_PROFILE_SLOW   - ~700ms release. Rides through fading/flutter
 *                         and inter-word gaps in speech without the
 *                         gain visibly "pumping" - the classic AM/SSB
 *                         broadcast-listening choice.
 *   AGC_PROFILE_MEDIUM - ~175ms release (DEFAULT - this is the exact
 *                         same release rate the AGC has always used,
 *                         see AGC_RELEASE_MEDIUM's comment in
 *                         demod_am.c - so leaving the profile at its
 *                         default changes nothing about existing
 *                         behavior).
 *   AGC_PROFILE_FAST    - ~60ms release. Recovers quickly between
 *                         syllables/CW elements at the cost of some
 *                         audible pumping on a fluttery signal - handy
 *                         for weak, choppy contacts.
 *   AGC_PROFILE_MANUAL  - bypasses the peak-tracking loop ENTIRELY:
 *                         fixed unity gain, no automatic leveling at
 *                         all. Loudness becomes the job of the
 *                         existing VOLUME control (main.c's
 *                         ENCODER_TARGET_VOLUME) instead of this loop
 *                         - same tradeoff as flipping a real radio's
 *                         AGC to MANUAL and riding the volume knob by
 *                         hand: a strong signal can clip if VOLUME is
 *                         set too hot, and a weak one can vanish into
 *                         the noise floor if it's set too low. This is
 *                         NOT an adjustable manual GAIN LEVEL of its
 *                         own (that would need its own encoder target
 *                         and state) - just an AGC on/off switch,
 *                         which is what was actually asked for; a real
 *                         manual RF/IF gain control would be a natural
 *                         follow-up if ever needed.
 */
typedef enum {
    AGC_PROFILE_MANUAL = 0,
    AGC_PROFILE_SLOW,
    AGC_PROFILE_MEDIUM,
    AGC_PROFILE_FAST
} agc_profile_t;

/* Safe to call at any time (same reasoning as demod_am_set_mode() -
 * see its comment), including from the main loop while the ISR runs. */
void demod_am_set_agc_profile(agc_profile_t profile);

/* Query the current profile - used by main.c for the badge. */
agc_profile_t demod_am_get_agc_profile(void);

/*
 * --- Squelch (AM + NFM) --------------------------------------------------
 *
 * Added 31/07/2026 for NFM, EXTENDED to AM the same day per the
 * project owner: a classic RF-level squelch. UNLIKE a noise-band
 * squelch (which filters the discriminator's AUDIO output above the
 * voice band and measures hiss energy there - the other common
 * technique), this measures the average magnitude of the down-mixed,
 * channel-filtered COMPLEX BASEBAND itself (CHF_COEFFS for AM,
 * NFM_CHF_COEFFS for NFM - each mode's own channel filter, see their
 * respective notes above), i.e. how strong the actual in-channel RF
 * signal is, BEFORE the audio-extraction stage - see demod_am.c's
 * shared squelch block, which runs right after step 1's AM/WFM/NFM/
 * SSB branch (not inside any one of them - it's the same code for
 * both AM and NFM). This was picked over a noise-band squelch because
 * it doesn't depend on what the signal is doing: for NFM specifically,
 * a loud sustained voice peak can't fool it into reading "no carrier"
 * for an instant, since FM's constant-envelope carrier stays at
 * roughly the same amplitude whether or not it's being modulated
 * hard. For AM the same metric is, if anything, an even more natural
 * fit - carrier presence there IS amplitude, by definition, so this
 * is close to how a classic AM squelch works.
 *
 * WFM and SSB don't get this: WFM's front-end skips the channel
 * filter entirely (no down-mix, no CHF - see its own note), so
 * there's no equivalent in-channel RF level to measure the same way.
 * SSB shares AM's exact front-end (down-mix + CHF_COEFFS) and could
 * reuse this identically, but hasn't been asked for and isn't wired
 * up yet - straightforward to add the same way if it's ever wanted.
 *
 * UNCALIBRATED, same caveat as the spectrum's dB scale (see
 * SDR_DB_MIN/MAX's original comment, main.c): the threshold is in dB
 * (20*log10 of the linear magnitude, a REAL log10f() this time, not
 * fft.c's bit-manipulation approximation), but there's no reference
 * level tying it to anything absolute - it has to be dialed in by
 * watching demod_am_get_squelch_level_db() (or just listening)
 * against real signals and real band noise on actual hardware, the
 * same way AGC_PEAK_MIN and the WFM/NFM discriminator gains were
 * tuned by reasoning + a target ballpark rather than a lab reference.
 * AM and NFM's channel filters have different passband gains/noise
 * bandwidths (CHF_COEFFS ~4kHz vs NFM_CHF_COEFFS ~6.25kHz), so the
 * SAME threshold value may not read the same "distance above the
 * noise floor" in both modes - if that turns out to matter in
 * practice, splitting into two independent thresholds (one per mode)
 * is a small change; they're deliberately shared for now, simplicity
 * first.
 *
 * DEFAULT: -100dB, i.e. effectively OFF (nothing realistic reads that
 * low) - so AM/NFM behave EXACTLY as they did before squelch existed
 * until the threshold is deliberately raised. Raising it toward the
 * noise floor (as read on real hardware) is what actually engages
 * muting.
 *
 * HYSTERESIS: opening and closing use different trip points (+/-1.5dB
 * around the threshold, see SQUELCH_HYSTERESIS_DB in demod_am.c)
 * so a signal sitting right at the threshold doesn't chatter open/
 * closed every block.
 */

/* Sets the squelch threshold in dB - see the block comment above for
 * what "dB" means here. Safe to call at any time (same reasoning as
 * demod_am_set_mode()). Has no audible effect outside AM/NFM mode,
 * but is remembered regardless (no need to be in one of those modes
 * to adjust it ahead of switching into it). */
void demod_am_set_squelch_db(float threshold_db);

/* Query the current threshold - used by main.c for the readout. */
float demod_am_get_squelch_db(void);

/* Query the LAST BLOCK's smoothed RF level, in the same dB units as
 * the threshold - lets main.c (or a future debug view) show where the
 * signal/noise floor actually sits, which is what you need to see to
 * set the threshold sensibly in the first place. */
float demod_am_get_squelch_level_db(void);

/* Query whether the squelch is currently PASSING audio (1) or MUTING
 * it (0). Only meaningful in AM/NFM mode - see demod_am_set_mode(). */
uint8_t demod_am_get_squelch_open(void);

/* Cycles spent in the LAST call to demod_am_process_raw() (the whole
 * ISR body, including the I2S stream write), measured via
 * DWT->CYCCNT. Added 30/07/2026 to diagnose a system hang reported
 * when switching into USB/LSB - see demod_am.c's comment above
 * HILBERT_COEFFS. Compare against SystemCoreClock * 512 / 192000 (one
 * block's real-time budget in cycles) - if this is consistently close
 * to or over that, the ISR doesn't fit in real time. Safe to poll
 * from the main loop (plain volatile uint32_t, single aligned word). */
uint32_t demod_am_get_last_cycles(void);

/*
 * Per-stage cycle breakdown of the LAST call to demod_am_process_raw()
 * - added 31/07/2026 to pin down exactly where an ISR block's cycles
 * go, after the whole-ISR total (demod_am_get_last_cycles() above)
 * came back higher than expected for USB/LSB once WFM was added
 * alongside it. Same DWT->CYCCNT read-and-subtract pattern, same
 * "cheap enough not to matter, no UART here" reasoning - see
 * demod_am_get_last_cycles()'s comment. Stages:
 *   frontend: step 0a-0c (deinterleave, low-IF down-mix, channel
 *             filter) - skipped entirely for WFM, see demod_am.h.
 *   extract:  step 1 (mode-specific: AM magnitude / WFM discriminator
 *             / SSB decimated Hilbert chain) - the one most likely to
 *             explain a USB/LSB-specific change.
 *   audio:    steps 2-3 (DC blocker, audio LPF or WFM de-emphasis+LPF)
 *   nr:       Spectral Subtraction NR cost - added 03/08/2026, revised
 *             04/08/2026 when SSB's path changed (see demod_am.c's NR
 *             INTEGRATION comment): for AM it's step 3b's dedicated
 *             decimate/nr_ss_process/interpolate; for USB/LSB it's
 *             step 2d's cost instead, running NR inline on the SSB
 *             chain's own already-12kHz buffer (cheaper - no separate
 *             decimate/interpolate pair). Always 0 for WFM/NFM (the
 *             stage is skipped entirely for those modes, not just
 *             cheap) or whenever NR is switched off. THE number to
 *             watch when checking whether this fits the ISR's
 *             real-time budget - see nr_ss.c's header comment.
 *   agc_out:  steps 4-5 (AGC, clamp, I2S write)
 * Sum of the five should land close to demod_am_get_last_cycles()'s
 * total (a few cycles off is normal - the reads themselves aren't
 * free). Safe to poll from the main loop, same as the total. */
typedef struct {
    uint32_t frontend;
    uint32_t extract;
    uint32_t audio;
    uint32_t nr;
    uint32_t agc_out;
} demod_am_cycles_breakdown_t;

demod_am_cycles_breakdown_t demod_am_get_last_cycles_breakdown(void);

/* Pre-AGC envelope peak (int16 full-scale units, instant-attack /
 * slow-release ballistics) - the UI's S-meter source. Convert to
 * dB/S-units in the main loop, not here. Safe to poll anytime. */
float demod_am_get_signal_peak(void);

/*
 * RF front-end clipping flag - added 07/08/2026, per the project
 * owner, for the RF-level (analog PGA) auto-AGC in main.c
 * (rf_agc_poll()/s_rf_agc_enabled). Set from INSIDE the RX DMA
 * interrupt (both demod_am_process_raw() and demod_wfm_process_raw()
 * scan every raw_interleaved block for samples riding the ADC's
 * railed value - see rf_clip_scan() in demod_am.c), because that's
 * the only place raw, pre-DSP samples ever exist: by the time
 * anything downstream (envelope, AGC, s_env[]) sees a value, a
 * genuinely clipped sample already lost information no software can
 * recover - digital AGC gain math being mathematically correct
 * doesn't help if the INPUT was already flat-topped. This flag is
 * the escape hatch: the ISR can detect it cheaply (a plain scan, same
 * cost class as the existing min/max tracking), but changing the
 * actual PGA gain means a bit-banged I2C transaction
 * (aic3204_set_pga_gain_db()) that must NEVER run inside an ISR (see
 * demod_am_process_raw()'s own "runs in the RX DMA interrupt"
 * warning) - so this just sets a flag and gets out; main.c's
 * rf_agc_poll(), called once per main-loop iteration same as
 * tune_encoder_poll(), is what actually acts on it.
 *
 * Test-and-clear semantics (reads AND clears the flag in one call) -
 * the ISR only ever sets it (never clears), so there's no meaningful
 * race beyond the same "plain volatile shared with an ISR" class
 * every other cross-context field in this file already accepts (e.g.
 * s_last_cycles_*) - a flag set between the read and the clear just
 * means it's caught on the NEXT poll instead, harmless for something
 * this coarse-grained (backoff steps happen on a many-milliseconds
 * cooldown, not sample-accurate).
 */
uint8_t demod_am_get_and_clear_rf_clip_flag(void);

/* LO offset for low-IF tuning: actual LO = selected_freq -
 * DEMOD_IF_OFFSET_HZ (see the LOW-IF TUNING note above). Currently
 * Fs/4 (96000/4 = 24000 - was 48000/4 = 12000 before AM/SSB/NFM moved
 * from 48kHz to 96kHz, and 192000/4 = 48000 before that - see
 * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment) to match demod_am.c's
 * zero-multiply sign-flip rotation - if this ever changes, the
 * rotation algorithm in demod_am.c has to change with it (it is NOT a
 * generic NCO, it only works at exactly Fs/4).
 *
 * *** 01/09/2026: TWO values now, not one - see demod_am_set_active_
 * rate()'s comment below *** - AM/USB/LSB/NFM can run at either 96kHz
 * or 48kHz (main.c's RATE tile), and since this offset MUST always
 * equal the ACTUAL running rate's Fs/4 (the down-mix rotation has no
 * other mode), main.c needs to pick the right one of these two
 * constants based on s_nonwfm_use_48k, not just use
 * DEMOD_IF_OFFSET_HZ unconditionally the way older code did - see
 * main.c's demod_if_offset_hz() helper, which is the ONLY place that
 * should still read either of these two macros directly.
 */
#define DEMOD_IF_OFFSET_HZ 24000UL /* Fs/4 @ 96kHz - the default/96kHz-rate value */
#define DEMOD_IF_OFFSET_HZ_48K 12000UL /* Fs/4 @ 48kHz */

/* Speaker enable pin: PB7, driven high to unmute the speaker amp. */

/* Configure PB7 (speaker enable, set high), initialize the CMSIS-DSP
 * biquad instances (channel filter x2, audio LPF), and reset all
 * filter/AGC state. Call once before registering the block hook. */
void demod_am_init(void);

/* Enable/disable the low-IF down-mix (see the LOW-IF TUNING note
 * above). MUST stay in sync with whatever's actually programmed on
 * the LO: active=1 only once the LO is genuinely tuned
 * DEMOD_IF_OFFSET_HZ below the selected station, active=0 whenever
 * it's tuned exactly on it (e.g. the captured-bytes boot tune, which
 * carries no offset). Starts at 0 (matches demod_am_init() being
 * called before the first tune). Mismatching this with the real LO
 * shifts the wanted signal OUT of the channel filter's passband
 * instead of into it. */
void demod_am_set_if_offset_active(uint8_t active);

/* Query the current state set by demod_am_set_if_offset_active() -
 * used by main.c to know how far off-center the actually-demodulated
 * signal sits on the panadapter (see spectrum_draw()'s
 * center_mark_offset_px parameter). */
uint8_t demod_am_get_if_offset_active(void);

/*
 * demod_am_set_active_rate() - added 01/09/2026, per the project
 * owner: AM/USB/LSB/NFM can now run at either 96kHz (default) or
 * 48kHz (main.c's RATE tile - see s_nonwfm_use_48k's declaration
 * comment there for the full "better SNR + CPU headroom for HFDL"
 * motivation). MUST be called (from main.c's apply_demod_mode(),
 * whenever a non-WFM rate is actually applied to the codec) BEFORE
 * the block hook starts feeding real samples at the new rate -
 * mismatching this with what the codec is ACTUALLY running at means
 * every filter in the chain runs its coefficients against samples
 * arriving at the wrong real-world rate, silently shifting every
 * cutoff frequency and time constant by whatever ratio the two rates
 * differ by. This is NOT the same flag as demod_am_set_if_offset_
 * active() above - that one is about WHETHER the down-mix offset is
 * active at all (always yes, except the captured boot tune); this one
 * is about WHICH of two complete coefficient sets (filters, AGC/DC-
 * blocker time constants, SSB decimator) the DSP chain should be
 * using, independent of whether the down-mix itself is active.
 *
 * WFM is COMPLETELY UNAFFECTED either way - it has always run its own
 * separate 192kHz path (demod_wfm_process_raw(), its own WFM_ALPF_
 * and WFM_IFBW_ filters, its own AGC/DC-blocker state) regardless of
 * what this flag says; this only selects between the AM/USB/LSB/NFM
 * coefficient sets used by demod_am_process_raw().
 */
void demod_am_set_active_rate(uint8_t is_48k);
uint8_t demod_am_get_active_rate_is_48k(void);

/* Process one raw RX half: SDR_RX_BLOCK_SAMPLES interleaved L/R (I/Q)
 * frames, demodulate, and push the resulting audio block into the
 * I2S TX stream (gd32_i2s_stream_write_half()). Intended as the
 * sdr_rx block hook - runs in DMA interrupt context. */
void demod_am_process_raw(const int16_t *raw_interleaved);

/*
 * WFM's OWN, separate sdr_rx block hook (05/08/2026 - see its own
 * header comment in demod_am.c) - SDR_RX_BLOCK_SAMPLES_WFM (512)
 * interleaved L/R (I/Q) frames, at WFM's native 192kHz rate, entirely
 * independent of demod_am_process_raw()'s 128-sample/48kHz state.
 * main.c registers THIS (via sdr_rx_set_block_hook()) instead of
 * demod_am_process_raw() while switching INTO WFM, and switches back
 * on the way out - see the mode-switch sequence there. Also runs in
 * DMA interrupt context, same as demod_am_process_raw().
 */
void demod_wfm_process_raw(const int16_t *raw_interleaved);

/* Diagnostic-only: resets the call counter that gates
 * demod_wfm_process_raw()'s per-block debug logging - call this
 * right when switching INTO WFM (see main.c's apply_demod_mode()) so
 * each WFM entry gets its own fresh set of logged calls, the same way
 * gd32_i2s.c's write_half() diagnostic already does for its own
 * counter. As of 05/08/2026 this ALSO (re-)arms the settle-mute
 * window (WFM_SETTLE_MUTE_BLOCKS in demod_am.c) - both need to reset
 * together on every WFM entry, so they share this one call rather
 * than main.c needing to know about two separate counters. */
void demod_wfm_reset_diag(void);

/* Diagnostic-only: resets the call counter that gates
 * demod_am_process_raw()'s per-block debug logging - call this right
 * when switching OUT of WFM (into AM/USB/LSB/NFM), mirroring
 * demod_wfm_reset_diag()'s own call on the way in. See
 * am_should_log_diag()'s comment in demod_am.c for why this was
 * added. */
void demod_am_reset_diag(void);

/* WFM's own ISR cycle counter, mirrors demod_am_get_last_cycles() -
 * was missing from this header (implicit-declaration warning fixed
 * 05/08/2026 alongside the R27/R30 clock-start reordering). */
uint32_t demod_wfm_get_last_cycles(void);

#endif /* DEMOD_AM_H */

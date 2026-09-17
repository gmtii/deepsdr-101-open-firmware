#ifndef HFDL_SCOPE_H
#define HFDL_SCOPE_H

#include <stdint.h>

/*
 * HFDL (High Frequency Data Link, ARINC 635-3 - "HF ACARS") burst
 * detector/tuning scope - added 10/08/2026, per the project owner,
 * PHASE 1 of an agreed staged plan. This is deliberately visual-only:
 * it does NOT demodulate or decode anything. HFDL is a real
 * differential-PSK modem (2/4/8-PSK at 1800 symbols/s, 300-1800 bps
 * depending on the negotiated rate, TDMA slots, convolutional coding
 * + interleaving, ARINC 635 frame structure on top) - genuinely
 * comparable in scope to writing a whole new modem, not an extension
 * of rtty.c's Goertzel/bit-sync approach. That's Phase 2+, deferred
 * until this phase is validated against a real signal and (per the
 * project owner) dumphfdl's source has been consulted for the exact
 * bit-level parameters (interleaver depth, convolutional polynomial,
 * sync/preamble sequence) that aren't nailed down from published
 * summaries alone.
 *
 * What THIS phase actually does: a COMPLEX baseband-IQ FFT (same
 * "dedicated small FFT, decoupled from the real-time RF path" shape
 * as rtty_scope.c - see that file's own header comment for the full
 * "why a separate FFT engine" reasoning, which applies here
 * identically) over the SAME already-12kHz decimated I/Q pair
 * demod_am.c hands the HFDL demod chain itself (s_i_dec/s_q_dec -
 * genuine hardware I/Q, BOTH sidebands present, taken right after
 * decimation and BEFORE the Hilbert-based USB/LSB sideband combine),
 * plus a simple broadband energy/burst detector over HFDL's expected
 * occupied band.
 *
 * CHANGED (16/08/2026): this used to run on the mono, ALREADY-
 * SIDEBAND-COMBINED s_ssb_dec audio (post-Hilbert, same buffer rtty.c
 * reads). That tap point had a confirmed blind spot: tuned dead-on to
 * a channel's nominal frequency (e.g. 11348.000 exact), the burst
 * detector would not fire at all, only recovering with a manual
 * +-2kHz detune. Root cause was never fully nailed down (something in
 * how the Hilbert combine's image cancellation interacts with where
 * the subcarrier lands for that specific tuning), but moving the tap
 * point upstream of the combine - straight off the real I/Q, the same
 * signal the actual demod chain (hfdl_demod_chain_process()) consumes
 * - sidesteps the problem structurally instead of explaining it: there
 * is no combine stage left to have a blind spot in. The dead-tuning
 * workaround should no longer be necessary; if it still is, that's a
 * NEW bug, not the old one.
 *
 * Confirmed HFDL physical-layer facts this module relies on (see the
 * project's own research before this was written - NOT independently
 * re-verified against a real capture yet):
 *   - Transmitted in USB, PSK subcarrier centered at 1440Hz within
 *     the SSB passband - equivalently, at the SAME +1440Hz offset in
 *     the complex baseband I/Q's POSITIVE-frequency half (USB tuning
 *     convention: LO sits 1440Hz below the nominal/subcarrier
 *     frequency, so the subcarrier lands at +1440Hz either way -
 *     that's why HFDL_BAND_LO_HZ/HFDL_BAND_HI_HZ below did NOT need
 *     to change when the tap point moved from post-combine audio to
 *     pre-combine complex I/Q, only the FFT itself did (real ->
 *     complex, see hfdl_scope_feed()/hfdl_scope_poll())).
 *   - 1800 baud symbol rate, occupied bandwidth on the order of
 *     ~2.4-2.9kHz (roughly the whole practical width of a 3kHz HF
 *     channel) - HFDL_BAND_LO_HZ/HFDL_BAND_HI_HZ below are this
 *     project's best estimate of that occupied window, not a
 *     spec-verified exact figure.
 *   - TDMA slots, not continuous like RTTY - a "burst" is the
 *     expected shape, not a steady tone pair.
 *
 * The burst detector is HONEST about its limits: it flags "broadband
 * energy in the expected HFDL band just jumped", nothing more. It
 * will also fire for a strong voice syllable, a static crash, or any
 * other wideband signal landing in the same window - there's no
 * correlation against HFDL's actual preamble/sync pattern yet (that's
 * squarely Phase 2 territory, needs real symbol-timing recovery).
 * Still a genuinely useful "something's happening here, worth a
 * listen" indicator on its own, and the natural first building block
 * either way.
 */

/* Half of rtty_scope.c's FFT size - deliberately SMALLER, not just
 * reused as-is: this module doesn't need RTTY's fine 23.4Hz/bin
 * resolution (there's no pair of tones a few hundred Hz apart to
 * separate here, just one broad ~2.5kHz-wide burst shape - 256 points
 * still gives 46.9Hz/bin, 128 bins comfortably across it) AND the
 * combined RAM cost of running BOTH FFT engines side by side
 * (rtty_scope.c's 512-point tables are always resident too, RTTY and
 * HFDL being mutually exclusive at RUNTIME doesn't free either one's
 * static RAM at COMPILE time) blew the 192KB SRAM budget by 396 bytes
 * at 512 - confirmed by the linker, not a guess. */
#define HFDL_SCOPE_FFT_SIZE 256U
#define HFDL_SCOPE_BINS (HFDL_SCOPE_FFT_SIZE / 2U) /* real input: bins 0..Nyquist (6kHz) */

/* Expected occupied band, Hz - see this file's top comment on where
 * these numbers come from (an estimate from published HFDL summaries,
 * not yet checked against a real capture). Used both for the burst
 * detector's in-band energy sum and for the two dim boundary markers
 * hfdl_scope_draw() (main.c) paints on the trace, purely informational
 * (unlike RTTY's mark/space lines, these aren't live-adjustable). */
#define HFDL_BAND_LO_HZ 300.0f
#define HFDL_BAND_HI_HZ 2900.0f

/* Precomputes twiddle/Hann/bit-reversal tables and resets state. Call
 * once at startup, same as rtty_scope_init(). */
void hfdl_scope_init(void);

/*
 * Feeds HFDL_SCOPE block-sized (DEC_BLOCK_SAMPLES, see demod_am.c)
 * chunks of 12kHz COMPLEX baseband I/Q - CHANGED (16/08/2026) from a
 * single mono audio buffer to a genuine (i, q) pair: this MUST be
 * s_i_dec/s_q_dec (or an equivalent pre-Hilbert-combine tap), the
 * SAME real hardware I/Q hfdl_demod_chain_process() itself consumes,
 * NOT the post-combine s_ssb_dec audio - see this file's top comment
 * for why (the whole point of this change was to stop tapping
 * downstream of the combine stage). Call site/cadence otherwise
 * unchanged from before: same place in demod_am.c's ISR-side
 * RTTY/HFDL INTEGRATION block, called right after decimation. Only
 * accumulates into a ring; the actual FFT runs from hfdl_scope_poll()
 * in the main loop, never here.
 */
void hfdl_scope_feed(const float *i, const float *q, uint32_t n);

/*
 * Call from the main loop, same cadence as rtty_scope_poll(). Runs
 * the windowed FFT + magnitude computation + burst-detector update IF
 * a full window has accumulated since the last call - cheap no-op
 * otherwise. Sets the frame-ready flag hfdl_scope_frame_ready() reads.
 */
void hfdl_scope_poll(void);

/* 1 if a new frame is ready to draw (cleared by hfdl_scope_get_frame()). */
uint8_t hfdl_scope_frame_ready(void);

/*
 * Returns a pointer to HFDL_SCOPE_BINS magnitude values (linear, NOT
 * dB - auto-normalized to this frame's own peak, same "tuning aid, not
 * a calibrated strength readout" convention as rtty_scope_get_frame())
 * and clears the ready flag. Bin 0 = DC, bin HFDL_SCOPE_BINS-1 =
 * Nyquist (6kHz).
 */
const float *hfdl_scope_get_frame(void);

/* Hz per bin (12000.0f / HFDL_SCOPE_FFT_SIZE) - for the UI's Hz -> x
 * pixel mapping (the HFDL_BAND_LO_HZ/HI_HZ boundary markers). */
float hfdl_scope_hz_per_bin(void);

/*
 * 1 while the in-band (HFDL_BAND_LO_HZ..HFDL_BAND_HI_HZ) broadband
 * energy is elevated above the tracked floor by more than
 * HFDL_BURST_THRESHOLD_DB (see hfdl_scope.c) - see this file's top
 * comment for exactly what this does and doesn't mean. Updated once
 * per completed FFT window inside hfdl_scope_poll(), independent of
 * hfdl_scope_frame_ready()/hfdl_scope_get_frame() (the burst flag is
 * meant to be checked every poll, e.g. for a badge, without also
 * consuming/needing the trace data).
 */
uint8_t hfdl_scope_burst_active(void);

/* Master on/off switch - same "toggle separate from any tuning value"
 * shape as rtty_set_enabled(). Set from main.c's
 * menu_mode_preset_callback() when the HFDL mode entry is selected,
 * and back to 0 when any other mode is picked. */
void hfdl_scope_set_enabled(uint8_t on);
uint8_t hfdl_scope_get_enabled(void);

/* See hfdl_scope.c's s_hfdl_ram comment - the RAM borrowed from the
 * waterfall buffer for the payload-decode pipeline (deinterleaver
 * table, then Viterbi metrics/decisions - see hfdl_payload_decode.c),
 * for the lifetime of HFDL mode being on. Returns NULL (and writes 0
 * to *capacity_out, if non-NULL) when HFDL mode is off. */
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out);

/* FIX (16/08/2026): re-primes JUST the floor/burst-decision state
 * (same as hfdl_scope_set_enabled() does internally) WITHOUT touching
 * s_enabled itself - for retuning while already in HFDL mode. A
 * frequency change re-programs the MS5351 LO, which can glitch the
 * front end for a window or two while the PLL settles - without this,
 * that glitch gets compared against a floor that was primed for the
 * PREVIOUS frequency's noise level, and can misfire a false burst
 * (and, if raw capture is armed, waste the one-shot arm on it) the
 * same way a stale/near-zero floor did at mode-entry before that was
 * fixed. Call this from apply_lo_tune() (or anywhere else the tuned
 * frequency changes while HFDL mode may be active) right after the
 * retune - safe to call even if HFDL mode is currently off (just
 * primes state nothing will read until/unless it's turned on). */
void hfdl_scope_reprime_floor(void);

/* Ratio of in-band energy over the adaptive noise floor, as of the last
 * poll - see hfdl_scope_get_snr_ratio()'s definition comment. */
float hfdl_scope_get_snr_ratio(void);

/*
 * DIAGNOSTIC GETTERS (16/08/2026, blind-spot-at-exact-tuning
 * investigation, round 2) - the move to complex baseband I/Q
 * (hfdl_scope_feed()'s (i,q) signature, see this file's top comment)
 * did NOT fix the blind spot: still no burst detected dead-on at
 * e.g. 11348.000 or 17928.000, only recovers with a manual ~2kHz
 * detune. That rules OUT the Hilbert-combine hypothesis (there's no
 * combine stage left in the burst detector's own signal path) and
 * points at something tuning-frequency-specific instead - most
 * likely a hardware-side artifact (LO/PLL spur, ADC/codec DC offset)
 * that happens to land inside or right next to the expected
 * HFDL_BAND_LO_HZ..HFDL_BAND_HI_HZ window specifically when tuned
 * dead-on, not a software processing bug. These four getters exist to
 * tell the two cases apart from a UART log instead of guessing again:
 *   - if s_floor_power (see hfdl_scope_get_floor_power()) is
 *     ITSELF elevated at exact tuning vs ~2kHz off, the noise FLOOR
 *     is the problem (a spur raising the whole band, not just eating
 *     the signal).
 *   - if hfdl_scope_get_dc_band_sum() (energy in the 0..HFDL_BAND_LO_HZ
 *     "guard" region, i.e. BELOW where the in-band sum even starts
 *     counting) is disproportionately large at exact tuning, that's
 *     a DC-adjacent spur (LO leakage/self-mixing, ADC DC offset)
 *     sitting just outside the counted band but plausibly wide enough
 *     to depress the AGC/PGA or otherwise mask the real signal.
 *   - hfdl_scope_get_peak_bin_hz() says literally WHERE the strongest
 *     energy in the window sits (across the full 0..6kHz half-
 *     spectrum, not just the HFDL window) - if it sits near 0Hz at
 *     exact tuning and jumps to a sane in-band frequency once
 *     detuned, that's a strong, direct confirmation of a DC-adjacent
 *     spur rather than the real subcarrier just being weak.
 * All four update every hfdl_scope_poll() call, same cadence/validity
 * as hfdl_scope_get_snr_ratio().
 */

/* Raw (not ratio'd) adaptive floor level - same linear,
 * per-window-peak-normalized units as hfdl_scope_get_frame()'s bins
 * sum to. Compare this value at exact tuning vs a few kHz off. */
float hfdl_scope_get_floor_power(void);

/* Raw (not ratio'd) in-band (HFDL_BAND_LO_HZ..HFDL_BAND_HI_HZ) energy
 * sum, i.e. the numerator hfdl_scope_get_snr_ratio() divides by the
 * floor - exposed directly so floor and signal can be compared
 * side-by-side without doing the division back out. */
float hfdl_scope_get_last_in_band_sum(void);

/* Energy sum over 0Hz..HFDL_BAND_LO_HZ (the "guard" region BELOW
 * where the in-band sum starts counting) - same normalized units as
 * hfdl_scope_get_last_in_band_sum(), directly comparable to it. A
 * DC-adjacent spur shows up here, not in the in-band sum itself. */
float hfdl_scope_get_dc_band_sum(void);

/* Frequency (Hz) of the single strongest bin in the FULL 0..6kHz
 * half-spectrum (not limited to the HFDL_BAND_LO_HZ..HFDL_BAND_HI_HZ
 * window) as of the last poll - literally "where is the energy",
 * independent of the burst detector's own in-band/floor logic. */
float hfdl_scope_get_peak_bin_hz(void);

#endif /* HFDL_SCOPE_H */

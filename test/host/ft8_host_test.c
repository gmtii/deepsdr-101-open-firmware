/*
 * Host-side smoke test for the FT8 decode chain: compiles the REAL
 * project files (fft.c, ft8_decimator.c, ft8_waterfall_adapter.c) and
 * the real ft8_lib sources unmodified (decode.c's one ARM-specific
 * line already falls back to plain sqrtf() here - see its own
 * comment) against a WAV file on disk, exactly the same "compile the
 * real C files against x86 stubs" approach already used for the HFDL
 * work.
 *
 * Deliberately narrow in scope: this only proves the DSP chain
 * (decimator -> waterfall adapter -> ft8_lib sync search + LDPC
 * decode) actually produces correct decodes end to end, on a known
 * real off-air recording - it says nothing about the GD32 hardware
 * itself (ISR timing, ADC noise, RTC slot alignment), which still
 * need their own separate validation once this passes.
 *
 * Usage: ft8_host_test <path/to/15s_mono_12kHz.wav>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ft8_fft1024.h"
#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8/decode.h"
#include "ft8/message.h"

#define MAX_CANDIDATES 140
#define MIN_SCORE      10
#define LDPC_ITERATIONS 25

/* Trivial hash interface - always miss/no-op. Good enough for this
 * smoke test: compound-callsign lookups may come back less complete
 * than WSJT-X's own multi-pass hashtable would give you, but standard
 * messages (the large majority in any real capture) decode fully
 * without needing this at all. */
static bool hash_lookup_stub(ftx_callsign_hash_type_t hash_type, uint32_t hash, char *callsign)
{
    (void)hash_type;
    (void)hash;
    callsign[0] = '\0';
    return false;
}
static void hash_save_stub(const char *callsign, uint32_t n22)
{
    (void)callsign;
    (void)n22;
}

/* Minimal WAV reader - just enough for the ft8_lib test corpus's own
 * format (mono, 16-bit PCM, 12000 Hz - verified against test_01.wav
 * before writing this). Not a general-purpose WAV parser. */
static int16_t *load_wav_mono16(const char *path, uint32_t *out_num_samples, uint32_t *out_rate)
{
    FILE *f = fopen(path, "rb");
    uint8_t riff_hdr[12];
    uint16_t num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    int16_t *samples = NULL;

    if (!f) {
        fprintf(stderr, "Cannot open %s\n", path);
        return NULL;
    }
    fread(riff_hdr, 1, 12, f); /* "RIFF"....."WAVE" */

    while (!feof(f)) {
        uint8_t chunk_id[4];
        uint32_t chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            fread(fmt, 1, chunk_size, f);
            num_channels    = fmt[2] | (fmt[3] << 8);
            sample_rate     = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            bits_per_sample = fmt[14] | (fmt[15] << 8);
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            uint32_t n = chunk_size / 2U; /* 16-bit samples */
            samples = (int16_t *)malloc(chunk_size);
            fread(samples, 1, chunk_size, f);
            *out_num_samples = n;
            *out_rate = sample_rate;
            break;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }
    fclose(f);

    if (num_channels != 1 || bits_per_sample != 16) {
        fprintf(stderr, "Expected mono/16-bit, got %u channel(s)/%u-bit - this harness doesn't handle that format\n",
                num_channels, bits_per_sample);
        free(samples);
        return NULL;
    }
    return samples;
}

int main(int argc, char **argv)
{
    uint32_t num_samples = 0, rate = 0;
    int16_t *samples;
    uint32_t i;
    ftx_candidate_t candidate_list[MAX_CANDIDATES];
    int num_candidates;
    const ftx_waterfall_t *wf;
    int num_decoded = 0;
    ftx_callsign_hash_interface_t hash_if = { hash_lookup_stub, hash_save_stub };

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <wav_path>\n", argv[0]);
        return 1;
    }

    samples = load_wav_mono16(argv[1], &num_samples, &rate);
    if (!samples) {
        return 1;
    }
    printf("Loaded %s: %u samples @ %u Hz (%.2fs)\n", argv[1], num_samples, rate, (double)num_samples / rate);

    if (rate != (uint32_t)FT8_DECIM_INPUT_RATE_HZ) {
        fprintf(stderr, "WARNING: WAV rate %u != FT8_DECIM_INPUT_RATE_HZ (%d) - results won't be meaningful\n",
                rate, FT8_DECIM_INPUT_RATE_HZ);
    }

    ft8_fft1024_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();

    /* Feed every sample through the REAL decimator + adapter, exactly
     * as the GD32 firmware's audio pipeline eventually will, one
     * sample at a time - no shortcuts. */
    for (i = 0; i < num_samples; i++) {
        ft8_decimator_feed_sample(samples[i]);
        ft8_decimator_poll(); /* now required - the FFT/encode work no longer runs synchronously inside feed_sample() (moved out of the ISR context it mirrors - see ft8_decimator.h) */
    }

    printf("Waterfall full: %s\n", ft8_waterfall_is_full() ? "yes" : "no (not enough samples for a full 15s slot)");

    wf = ft8_waterfall_get();
    {
        float dmin, dmax;
        ft8_waterfall_get_db_range(&dmin, &dmax);
        printf("DIAG raw db range: min=%.2f max=%.2f\n", dmin, dmax);
    }
    {
        int mn = 255, mx = 0;
        long sum = 0;
        long count = (long)wf->num_blocks * wf->block_stride;
        for (long k = 0; k < count; k++) {
            int v = wf->mag[k];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
        }
        printf("DIAG mag[] stats: min=%d max=%d avg=%.1f count=%ld\n", mn, mx, (double)sum/count, count);
    }

    num_candidates = ftx_find_candidates(wf, MAX_CANDIDATES, candidate_list, MIN_SCORE);
    printf("Sync candidates found: %d\n", num_candidates);

    for (i = 0; i < (uint32_t)num_candidates; i++) {
        const ftx_candidate_t *cand = &candidate_list[i];
        float freq_hz = (FT8_ADAPTER_MIN_RAW_BIN + cand->freq_offset + (float)cand->freq_sub / wf->freq_osr) / FT8_SYMBOL_PERIOD;
        float time_sec = (cand->time_offset + (float)cand->time_sub / wf->time_osr) * ((float)FT8_ADAPTER_BLOCK_SIZE / FT8_ADAPTER_SAMPLE_RATE_HZ);
        ftx_message_t message;
        ftx_decode_status_t status;

        if (!ftx_decode_candidate(wf, cand, LDPC_ITERATIONS, &message, &status)) {
            printf("  [candidate %6.1f Hz %+5.1fs score=%3d] FAILED - ldpc_errors=%d crc_calc=%04x crc_extr=%04x\n",
                   freq_hz, time_sec, cand->score, status.ldpc_errors, status.crc_calculated, status.crc_extracted);
            continue;
        }

        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        ftx_message_rc_t rc = ftx_message_decode(&message, &hash_if, text, &offsets);
        if (rc != FTX_MESSAGE_RC_OK) {
            continue;
        }

        printf("  %6.1f Hz  %+5.1fs  score=%3d  ~  %s\n", freq_hz, time_sec, cand->score, text);
        num_decoded++;
    }

    printf("Total decoded: %d (search window: 0-%.0f Hz only, by design - see ft8_waterfall_adapter.h)\n",
           num_decoded, FT8_ADAPTER_NUM_BINS * 6.25f);

    free(samples);
    return 0;
}

/* --- diagnostic appended for debugging, not part of the real harness --- */

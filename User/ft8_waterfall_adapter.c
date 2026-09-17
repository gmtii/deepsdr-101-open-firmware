#include "ft8_waterfall_adapter.h"
#include "ft8_fft1024.h"
#include "ft8_shared_ram.h"

#if FT8_ADAPTER_NFFT != FT8_FFT_SIZE
#error "FT8_ADAPTER_NFFT must equal ft8_fft1024.h's FT8_FFT_SIZE - see ft8_waterfall_adapter.h's derivation comment"
#endif

#if (FT8_ADAPTER_MIN_RAW_BIN + (FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_RAW_BIN_STEP)) > FT8_FFT_BINS_USEFUL
#error "FT8_ADAPTER_NUM_BINS/MIN_RAW_BIN window exceeds ft8_fft1024_compute_db()'s available output bins"
#endif

/* Real memory union with the main waterfall's own pixel buffer - see
 * ft8_shared_ram.h for the full "why". Storage is defined once, in
 * waterfall.c - these are typed views into it via macros, so every
 * line below that already said s_window/s_dbout/s_mag/s_wf_history
 * keeps working unchanged. */
#define s_window    (g_ft8_shared_ram.ft8.window)    /* sliding analysis window, oldest sample first - see ft8_waterfall_feed_subblock() */
#define s_dbout     (g_ft8_shared_ram.ft8.dbout)     /* scratch, reused every subblock */
#define s_mag       (g_ft8_shared_ram.ft8.mag)       /* ~46.5KB with NUM_BINS=256 - see header comment */
#define s_wf_history (g_ft8_shared_ram.ft8.cascade)

#define FT8_ADAPTER_MAG_SIZE (FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS)

static ftx_waterfall_t s_wf;
static int s_time_sub; /* 0..FT8_ADAPTER_TIME_OSR-1: which subblock within the CURRENT symbol block this feed will land in */

/* Calibration diagnostics - see FT8_ADAPTER_DB_OFFSET's comment in
 * the header. Tracks the min/max raw (pre-offset, pre-quantization)
 * dB value seen across the whole current slot, so FT8_ADAPTER_DB_OFFSET
 * can be tuned from a real capture instead of guessed. Reset alongside
 * everything else in ft8_waterfall_reset(). */
static float s_db_min;
static float s_db_max;

static float s_live_spectrum[FT8_ADAPTER_NUM_BINS]; /* freq_sub=0 slice of the most recent subblock - see ft8_waterfall_get_live_spectrum()'s comment - small (1KB), left as an ordinary static, not worth unioning */
static uint32_t s_update_counter;

/* Small scrolling history for a narrowband cascade display (09/2026) -
 * a live bar spectrum alone can't show "was there a weak/intermittent
 * signal a few seconds ago", which is exactly the kind of thing worth
 * seeing while decodes aren't working yet. Stores the SAME quantized
 * byte that goes into mag[] (freq_sub=0 slice) - reuses the encoding
 * already computed below, no extra work. Newest row at index 0,
 * shifts down (toward higher indices = further in the past) each
 * subblock - see ft8_waterfall_get_history()'s comment for the
 * caller-side convention this implies. Kept short (16 rows, not a
 * deeper scrollback) deliberately - see main.c's ft8_cascade_draw()
 * comment for why: redrawing this pixel-by-pixel every subblock scales
 * with rows*bins, and this project already hit one real UI-slowdown
 * bug this session from underestimating per-sample/per-cell cost at
 * scale. Lives inside g_ft8_shared_ram now (see above) - 16 must match
 * ft8_shared_ram.h's own hardcoded cascade[16][...] dimension. */
#define FT8_WF_HISTORY_ROWS 16

/* Cascade scroll SPEED (09/2026, per the project owner) - a new row
 * only gets pushed every FT8_WF_ROW_PERIOD subblocks (~320ms at the
 * default of 4), not every single one (~80ms). This throttles the
 * SOURCE of motion, not just the redraw - each redraw therefore only
 * ever needs to show a ONE-row shift, so the scroll looks smooth and
 * slower, not like it's skipping FT8_WF_ROW_PERIOD rows at a time
 * (which is what throttling only the on-screen redraw produced: the
 * data kept shifting every subblock underneath, so whenever a redraw
 * finally happened it had 4 rows' worth of motion to show at once). */
#define FT8_WF_ROW_PERIOD 4U
static uint32_t s_wf_subblock_count;
static uint32_t s_wf_row_counter; /* increments only when a row is actually pushed - see ft8_waterfall_get_row_counter() */

void ft8_waterfall_reset(void)
{
    int i;

    for (i = 0; i < FT8_ADAPTER_NFFT; i++)
    {
        s_window[i] = 0; /* silence - the first ~NFFT/SUBBLOCK_SIZE feeds of a fresh slot will see some of this until the window fills with real audio, same transient any STFT has */
    }
    s_time_sub = 0;
    s_db_min = 1e9f;
    s_db_max = -1e9f;

    {
        int r, c;
        for (r = 0; r < FT8_WF_HISTORY_ROWS; r++)
        {
            for (c = 0; c < FT8_ADAPTER_NUM_BINS; c++)
            {
                s_wf_history[r][c] = 0U;
            }
        }
    }

    s_wf.max_blocks = FT8_ADAPTER_MAX_BLOCKS;
    s_wf.num_blocks = 0;
    s_wf.num_bins = FT8_ADAPTER_NUM_BINS;
    s_wf.time_osr = FT8_ADAPTER_TIME_OSR;
    s_wf.freq_osr = FT8_ADAPTER_FREQ_OSR;
    s_wf.mag = s_mag;
    s_wf.block_stride = FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS;
    s_wf.protocol = FTX_PROTOCOL_FT8;
}

bool ft8_waterfall_is_full(void)
{
    return s_wf.num_blocks >= FT8_ADAPTER_MAX_BLOCKS;
}

const ftx_waterfall_t *ft8_waterfall_get(void)
{
    return &s_wf;
}

/*
 * Snapshot the current mag[] into g_ft8_shared_ram.ft8.mag_snapshot
 * (09/2026) - see mag_snapshot's own declaration comment in
 * ft8_shared_ram.h for the full "why". Call this the INSTANT
 * ft8_waterfall_is_full() is true, BEFORE ft8_waterfall_reset() -
 * once reset runs, the live mag[] is gone. The small ftx_waterfall_t
 * metadata (num_blocks/num_bins/time_osr/freq_osr) doesn't need
 * snapshotting alongside it - those are fixed constants for this
 * project (FT8_ADAPTER_MAX_BLOCKS/NUM_BINS/TIME_OSR/FREQ_OSR), never
 * anything the caller needs to have captured at a particular instant.
 */
void ft8_waterfall_snapshot_mag(void)
{
    uint32_t i;
    for (i = 0; i < FT8_ADAPTER_MAG_SIZE; i++)
    {
        g_ft8_shared_ram.ft8.mag_snapshot[i] = s_mag[i];
    }
}

/*
 * Read-only pointer to the snapshot copy made by
 * ft8_waterfall_snapshot_mag() - for ft8_decoder_process_slot() to
 * build its own scratch ftx_waterfall_t against, instead of the live
 * (already-reset-and-refilling) one ft8_waterfall_get() returns. See
 * both functions' own comments for the full "why".
 */
const uint8_t *ft8_waterfall_get_mag_snapshot(void)
{
    return g_ft8_shared_ram.ft8.mag_snapshot;
}

void ft8_waterfall_get_db_range(float *out_min, float *out_max)
{
    *out_min = s_db_min;
    *out_max = s_db_max;
}

void ft8_waterfall_get_live_spectrum(const float **out_db, int *out_n_bins)
{
    *out_db = s_live_spectrum;
    *out_n_bins = FT8_ADAPTER_NUM_BINS;
}

uint32_t ft8_waterfall_get_update_counter(void)
{
    return s_update_counter;
}

uint32_t ft8_waterfall_get_row_counter(void)
{
    return s_wf_row_counter;
}

void ft8_waterfall_get_history(const uint8_t **out_buf, int *out_rows, int *out_cols)
{
    *out_buf = &s_wf_history[0][0];
    *out_rows = FT8_WF_HISTORY_ROWS;
    *out_cols = FT8_ADAPTER_NUM_BINS;
}

void ft8_waterfall_feed_subblock(const int16_t *audio_samples)
{
    int i;
    int freq_sub;
    int bin;
    int offset = 0;
    /* Decoupled on purpose (09/2026, per the project owner): the
     * cascade/live-spectrum below this point must keep running
     * continuously regardless of FT8 decode/detection activity -
     * only the mag[] encoding actually FED TO ft8_lib needs to stop
     * once a slot's 93 blocks are collected (to avoid writing past
     * s_mag[]) and wait for main.c's RTC-aligned reset. Before this
     * change, the whole function returned early once full, which also
     * froze the cascade for the ~14.98 minutes of every 15-minute
     * cycle spent waiting for the next :00/:15/:30/:45 boundary - the
     * cascade is meant to show "is anything here at all", continuously,
     * with no relation to that alignment. */
    bool decode_slot_full = ft8_waterfall_is_full();
    bool push_wf_row;

    s_wf_subblock_count++;
    push_wf_row = ((s_wf_subblock_count % FT8_WF_ROW_PERIOD) == 0U);

    /* Cascade history shift - only when push_wf_row is true (every
     * FT8_WF_ROW_PERIOD-th subblock - see that macro's comment for
     * why), row 0 = newest. Done here (not per-bin below) so it
     * happens exactly once regardless of FT8_ADAPTER_NUM_BINS. */
    if (push_wf_row)
    {
        int r;
        for (r = FT8_WF_HISTORY_ROWS - 1; r > 0; r--)
        {
            int c;
            for (c = 0; c < FT8_ADAPTER_NUM_BINS; c++)
            {
                s_wf_history[r][c] = s_wf_history[r - 1][c];
            }
        }
    }

    /* Slide the analysis window: drop the oldest SUBBLOCK_SIZE
     * samples, append the new ones at the end - same overlap
     * technique as ft8_lib's own monitor_process() ("shift
     * last_frame, add new subblock"), just against our own int16
     * buffer instead of a float one (fft_compute_db() applies the
     * Hann window internally, same as it does for the main spectrum -
     * no pre-windowing needed here). ALWAYS runs, same reasoning. */
    for (i = 0; i < FT8_ADAPTER_NFFT - FT8_ADAPTER_SUBBLOCK_SIZE; i++)
    {
        s_window[i] = s_window[i + FT8_ADAPTER_SUBBLOCK_SIZE];
    }
    for (i = 0; i < FT8_ADAPTER_SUBBLOCK_SIZE; i++)
    {
        s_window[FT8_ADAPTER_NFFT - FT8_ADAPTER_SUBBLOCK_SIZE + i] = audio_samples[i];
    }

    ft8_fft1024_compute_db(s_window, s_dbout);

    if (!decode_slot_full)
    {
        offset = (s_wf.num_blocks * s_wf.block_stride) + (s_time_sub * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS);
    }

    /* src_bin = bin*RAW_BIN_STEP + freq_sub*(RAW_BIN_STEP/FREQ_OSR) -
     * see FT8_ADAPTER_RAW_BIN_STEP's comment in the header for why
     * this is no longer simply "bin*freq_osr + freq_sub" (that only
     * matched common/monitor.c's own mapping back when FREQ_OSR
     * happened to equal the raw-bin-step by coincidence, before
     * FREQ_OSR was reduced to 1 for the wider-window memory budget -
     * see the header's MEMORY BUDGET CONTEXT comment). The "2*db+240,
     * clamp 0-255" encoding itself (WF_ELEM_MAG in decode.h decodes it
     * back as x*0.5-120) is unchanged. The cascade/live-spectrum writes
     * (freq_sub==0 branches) ALWAYS run; only the s_mag[] write (what
     * ft8_lib actually decodes) is gated on !decode_slot_full. */
    for (freq_sub = 0; freq_sub < FT8_ADAPTER_FREQ_OSR; freq_sub++)
    {
        for (bin = 0; bin < FT8_ADAPTER_NUM_BINS; bin++)
        {
            int src_bin = FT8_ADAPTER_MIN_RAW_BIN + (bin * FT8_ADAPTER_RAW_BIN_STEP) + (freq_sub * (FT8_ADAPTER_RAW_BIN_STEP / FT8_ADAPTER_FREQ_OSR));
            float db = s_dbout[src_bin] + FT8_ADAPTER_DB_OFFSET;
            int scaled = (int)(2.0f * db + 240.0f);

            if (db < s_db_min) { s_db_min = db; }
            if (db > s_db_max) { s_db_max = db; }

            if (scaled < 0) { scaled = 0; }
            if (scaled > 255) { scaled = 255; }

            if (freq_sub == 0) {
                s_live_spectrum[bin] = db; /* see ft8_waterfall_get_live_spectrum()'s comment - just one freq_sub slice, plenty for a simple bar display - updates every subblock, NOT throttled by FT8_WF_ROW_PERIOD (only the cascade's row-push is) */
                if (push_wf_row) { s_wf_history[0][bin] = (uint8_t)scaled; } /* row 0 already shifted into row 1 above, only when push_wf_row - see there */
            }

            if (!decode_slot_full)
            {
                s_mag[offset] = (WF_ELEM_T)scaled;
                offset++;
            }
        }
    }
    s_update_counter++; /* ALWAYS - live-spectrum readers poll this, independent of the cascade's own slower row_counter below */
    if (push_wf_row) { s_wf_row_counter++; } /* see ft8_waterfall_get_row_counter() - what main.c's cascade redraw actually polls now */

    if (!decode_slot_full)
    {
        s_time_sub++;
        if (s_time_sub >= FT8_ADAPTER_TIME_OSR)
        {
            s_time_sub = 0;
            s_wf.num_blocks++;
        }
    }
}

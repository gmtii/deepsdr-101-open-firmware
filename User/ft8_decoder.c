#include "ft8_decoder.h"
#include "ft8_waterfall_adapter.h"
#include "ft8/decode.h"
#include "ft8/message.h"
#include "ft8/constants.h"
#include "rtc_hw.h" /* timestamp each decoded line - see the project owner's request (09/2026) to help tune real-world performance over time */

/* Same "always miss/no-op" hash interface as test/host/ft8_host_test.c
 * - see ft8_decoder_process_slot()'s header comment on why. */
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

static ft8_decoded_msg_t s_queue[FT8_DECODER_MSG_QUEUE_SIZE];
static uint8_t s_queue_head; /* next slot to write */
static uint8_t s_queue_tail; /* next slot to read */
static uint8_t s_queue_count;
static uint32_t s_total_decoded_count; /* running total since boot - see ft8_decoder_get_total_count() */
/* Candidate count from the LAST ftx_find_candidates() call (09/2026) -
 * per the project owner's own investigation into growing proc_ms/
 * apparent slot drift: more candidates (busier band) means more LDPC
 * decode attempts, which is real, variable CPU cost - this exposes
 * that number directly so it can be correlated on-screen against
 * proc_ms/the arm-cycle drift, instead of guessing whether a busy
 * band is the cause. */
static int s_last_num_candidates;

void ft8_decoder_init(void)
{
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_queue_count = 0U;
    s_total_decoded_count = 0U;
}

static void queue_push(const char *line)
{
    uint8_t i;

    s_total_decoded_count++; /* count every real decode, even if the display queue below is full and this one gets dropped - see ft8_decoder_get_total_count() */

    if (s_queue_count >= FT8_DECODER_MSG_QUEUE_SIZE)
    {
        return; /* queue full - drop rather than overwrite undrained messages; the UI is expected to drain promptly */
    }

    for (i = 0U; i < FT8_DECODER_LINE_LEN - 1U && line[i] != '\0'; i++)
    {
        s_queue[s_queue_head].line[i] = line[i];
    }
    s_queue[s_queue_head].line[i] = '\0';

    s_queue_head = (uint8_t)((s_queue_head + 1U) % FT8_DECODER_MSG_QUEUE_SIZE);
    s_queue_count++;
}

bool ft8_decoder_get_message(ft8_decoded_msg_t *out)
{
    if (s_queue_count == 0U)
    {
        return false;
    }

    *out = s_queue[s_queue_tail];
    s_queue_tail = (uint8_t)((s_queue_tail + 1U) % FT8_DECODER_MSG_QUEUE_SIZE);
    s_queue_count--;
    return true;
}

uint32_t ft8_decoder_get_total_count(void)
{
    return s_total_decoded_count;
}

int ft8_decoder_get_last_num_candidates(void)
{
    return s_last_num_candidates;
}

static int append_str(char *dst, int pos, int max, const char *src)
{
    while (*src != '\0' && pos < max - 1)
    {
        dst[pos] = *src;
        pos++;
        src++;
    }
    dst[pos] = '\0';
    return pos;
}

/* Zero-padded 2-digit decimal (00-99) - for the HH:MM timestamp
 * prefix (09/2026, per the project owner's request to help tune
 * real-world performance over time: seeing WHEN a decode happened,
 * not just how many, matters for correlating against band
 * conditions/time of day). append_int() above doesn't zero-pad
 * (would print "9" not "09"), hence this separate small helper. */
static int append_u2(char *dst, int pos, int max, uint8_t value)
{
    if (pos < max - 1) { dst[pos++] = (char)('0' + (value / 10U) % 10U); }
    if (pos < max - 1) { dst[pos++] = (char)('0' + value % 10U); }
    dst[pos] = '\0';
    return pos;
}

/* Zero-padded 4-digit decimal (0000-9999) - same "keep the column
 * aligned" reasoning as append_u2() above, for the frequency-offset
 * field (09/2026, per the project owner: this window's 0-1600Hz audio
 * passband means freq_hz_i naturally varies between 3 and 4 digits
 * decode to decode, which without padding shifts every field after it
 * out of alignment - e.g. "169Hz" one line, "1169Hz" the next). Only
 * ever fed a non-negative value in practice (a frequency location
 * within the passband can't be negative) - no sign handling, unlike
 * append_int(). Values above 9999 (shouldn't happen at a 1600Hz
 * passband width) are clamped rather than silently truncating a
 * digit off the front, which would misalign in the exact same way
 * this exists to prevent. */
static int append_u4(char *dst, int pos, int max, int value)
{
    unsigned int v = (value < 0) ? 0U : (unsigned int)value;
    if (v > 9999U) { v = 9999U; }
    if (pos < max - 1) { dst[pos++] = (char)('0' + (v / 1000U) % 10U); }
    if (pos < max - 1) { dst[pos++] = (char)('0' + (v / 100U) % 10U); }
    if (pos < max - 1) { dst[pos++] = (char)('0' + (v / 10U) % 10U); }
    if (pos < max - 1) { dst[pos++] = (char)('0' + v % 10U); }
    dst[pos] = '\0';
    return pos;
}

/* Signed, zero-padded, ALWAYS-explicit-sign 2-digit decimal
 * ("+00".."+99" / "-00".."-99") - same "keep the column aligned"
 * reasoning as append_u2()/append_u4() above, for the SNR field
 * (09/2026 #3, per the project owner: "-5dB" one line, "-12dB" the
 * next shifts everything after it out of alignment exactly like the
 * frequency field did). Unlike append_u4(), this value genuinely can
 * be (and usually is) negative - FT8's whole point is decoding well
 * below the noise floor, so cand->score-derived SNR estimates
 * typically run negative (roughly -24 to +10dB in practice) - so the
 * sign is always printed explicitly (even for 0 or positive values)
 * rather than only prepending "-" for negatives the way append_int()
 * does: printing the sign only sometimes would itself reintroduce a
 * 1-character alignment shift between negative and non-negative
 * lines, which is exactly what this exists to avoid. Magnitude
 * clamped to 99 (SNR estimates this project produces shouldn't
 * realistically reach that) for the same "clamp, don't silently
 * misalign" reasoning as append_u4(). */
static int append_s2(char *dst, int pos, int max, int value)
{
    int neg = (value < 0);
    unsigned int v = neg ? (unsigned int)(-value) : (unsigned int)value;
    if (v > 99U) { v = 99U; }
    if (pos < max - 1) { dst[pos++] = neg ? '-' : '+'; }
    if (pos < max - 1) { dst[pos++] = (char)('0' + (v / 10U) % 10U); }
    if (pos < max - 1) { dst[pos++] = (char)('0' + v % 10U); }
    dst[pos] = '\0';
    return pos;
}

void ft8_decoder_process_slot(void)
{
    ftx_waterfall_t wf_snapshot;
    const ftx_waterfall_t *wf = &wf_snapshot;
    ftx_candidate_t candidate_list[FT8_DECODER_MAX_CANDIDATES];
    int num_candidates;
    ftx_callsign_hash_interface_t hash_if = { hash_lookup_stub, hash_save_stub };
    uint32_t seen_hash[FT8_DECODER_MAX_CANDIDATES];
    int num_seen = 0;
    int i;
    rtc_hw_datetime_t slot_time; /* captured once per slot (not per-candidate) - all decodes from the same slot share the same real-world reception time, down to the minute shown */

    rtc_hw_get(&slot_time);

    /*
     * Build the local waterfall struct against the SNAPSHOT copy
     * (09/2026) - see ft8_waterfall_snapshot_mag()'s own comment for
     * the full "why": by the time this function runs, the REAL
     * capture for the NEXT slot has already been reset+rearmed (see
     * main.c's call site - it now snapshots+rearms BEFORE calling
     * this), so ft8_waterfall_get()'s live structure is no longer the
     * completed slot's data at all. max_blocks/num_bins/time_osr/
     * freq_osr/block_stride/protocol are fixed constants for this
     * project (never anything that changes slot to slot), so there's
     * nothing to snapshot for those - only mag[] itself needed a copy.
     */
    wf_snapshot.max_blocks = FT8_ADAPTER_MAX_BLOCKS;
    wf_snapshot.num_blocks = FT8_ADAPTER_MAX_BLOCKS; /* only ever called once a slot is FULL - see this function's own call site */
    wf_snapshot.num_bins = FT8_ADAPTER_NUM_BINS;
    wf_snapshot.time_osr = FT8_ADAPTER_TIME_OSR;
    wf_snapshot.freq_osr = FT8_ADAPTER_FREQ_OSR;
    wf_snapshot.mag = (WF_ELEM_T *)ft8_waterfall_get_mag_snapshot(); /* cast away const - ftx_find_candidates()/ftx_decode_candidate() only ever read through this pointer, never write */
    wf_snapshot.block_stride = FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS;
    wf_snapshot.protocol = FTX_PROTOCOL_FT8;

    num_candidates = ftx_find_candidates(wf, FT8_DECODER_MAX_CANDIDATES, candidate_list, FT8_DECODER_MIN_SCORE);
    s_last_num_candidates = num_candidates;

    for (i = 0; i < num_candidates; i++)
    {
        const ftx_candidate_t *cand = &candidate_list[i];
        ftx_message_t message;
        ftx_decode_status_t status;
        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        char line[FT8_DECODER_LINE_LEN];
        int pos;
        int freq_hz_i;
        int snr_i;
        int dup;
        int j;

        if (!ftx_decode_candidate(wf, cand, FT8_DECODER_LDPC_ITERATIONS, &message, &status))
        {
            continue; /* LDPC didn't converge or CRC mismatch - see status.ldpc_errors/crc_* for diagnostics if this needs investigating further */
        }

        if (ftx_message_decode(&message, &hash_if, text, &offsets) != FTX_MESSAGE_RC_OK)
        {
            continue;
        }

        /* Simple within-this-slot duplicate suppression by message
         * hash - NOT ft8_lib's own hash table (see this file's header
         * comment on why that's skipped for now). A real transmission
         * decoded from more than one nearby candidate (common - sync
         * search often finds several close time/freq offsets for the
         * same signal, exactly as seen clustered in this session's own
         * WAV test) would otherwise show up as repeated lines. */
        dup = 0;
        for (j = 0; j < num_seen; j++)
        {
            if (seen_hash[j] == message.hash) { dup = 1; break; }
        }
        if (dup) { continue; }
        if (num_seen < FT8_DECODER_MAX_CANDIDATES) { seen_hash[num_seen++] = (uint32_t)message.hash; }

        /* Format: "08:14 0169Hz -05dB CQ IU8DMZ JN70" - HH:MM
         * timestamp (UTC, from the RTC - see rtc_hw.h) first, per the
         * project owner's request to correlate decodes against time
         * of day/band conditions while tuning; freq rounded to the
         * nearest Hz and zero-padded to 4 digits (append_u4() above,
         * 09/2026 #2 - keeps this column aligned across the 3-vs-4-
         * digit range this passband actually produces); SNR from
         * cand->score * 0.5 (ft8_lib's own demo uses this exact
         * placeholder formula, marked there as a TODO for "compute
         * better approximation" - carried over as-is, not improved on
         * here), explicit-sign zero-padded to 2 digits (append_s2()
         * above, 09/2026 #3 - same alignment reasoning, and SNR is
         * usually negative in real use: FT8 is designed to decode
         * well below the noise floor). */
        freq_hz_i = (int)((FT8_ADAPTER_MIN_RAW_BIN + cand->freq_offset + (float)cand->freq_sub / wf->freq_osr) / FT8_SYMBOL_PERIOD + 0.5f);
        snr_i = (int)(cand->score * 0.5f);

        pos = 0;
        pos = append_u2(line, pos, FT8_DECODER_LINE_LEN, slot_time.hour);
        pos = append_str(line, pos, FT8_DECODER_LINE_LEN, ":");
        pos = append_u2(line, pos, FT8_DECODER_LINE_LEN, slot_time.minute);
        pos = append_str(line, pos, FT8_DECODER_LINE_LEN, " ");
        pos = append_u4(line, pos, FT8_DECODER_LINE_LEN, freq_hz_i);
        pos = append_str(line, pos, FT8_DECODER_LINE_LEN, "Hz ");
        pos = append_s2(line, pos, FT8_DECODER_LINE_LEN, snr_i);
        pos = append_str(line, pos, FT8_DECODER_LINE_LEN, "dB ~ ");
        pos = append_str(line, pos, FT8_DECODER_LINE_LEN, text);
        (void)pos;

        queue_push(line);
    }
}

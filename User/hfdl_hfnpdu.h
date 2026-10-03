#ifndef HFDL_HFNPDU_H
#define HFDL_HFNPDU_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Minimal decoder for the HFDL HFNPDU "Performance data" message
 * (type 0xD1) - the position-report format carried inside a
 * HFDL_LPDU_KIND_USER_DATA LPDU (see hfdl_pdu.h). Ported 16/09/2026
 * from the real, open-source reference implementation (dumphfdl's
 * hfnpdu.c performance_data_parse()/parse_coordinate(), itself using
 * libacars) - field offsets and the coordinate formula are copied
 * exactly from that source, not reverse-engineered from scratch.
 * Verified byte-for-byte against a real decoded HFDL message from
 * this project's own hardware (src_id=250/dst_gs_id=4, 11387kHz New
 * York/Riverhead capture): bytes 10-11 of the LPDU user data were
 * 0xFF 0xD1 (the envelope marker + this type), and the latitude
 * field computed to 42.117 deg, matching dumphfdl's own 42.1120493
 * for the identical message (small residual difference is this
 * comment's hand-calculation rounding, not a formula difference).
 *
 * Deliberately NOT a full HFNPDU decoder: only PERFORMANCE_DATA
 * (0xD1) is handled here. SYSTEM_TABLE (0xD0), SYSTEM_TABLE_REQUEST
 * (0xD2), FREQUENCY_DATA (0xD5), DELAYED_ECHO (0xDE) and
 * ENVELOPED_DATA (0xFF, which is actually ACARS - needs a real ACARS
 * decoder, out of scope here) are all left unhandled - see
 * hfdl_hfnpdu_parse_performance_data()'s own comment on how to tell
 * them apart if extending this later.
 *
 * Only the fields useful for a position report are extracted
 * (flight ID, position, UTC time, flight leg, ground station ID) -
 * the MPDU/SPDU traffic-statistics fields further into the same PDU
 * (mpdus_rx, spdus_rx, etc. in dumphfdl's struct) are skipped, not
 * because they're hard, just not useful for "where is the aircraft".
 */

struct hfdl_hfnpdu_location {
    double lat; /* degrees, positive = North */
    double lon; /* degrees, positive = East */
};

struct hfdl_hfnpdu_time {
    uint8_t hour, min, sec; /* UTC */
};

typedef struct {
    char flight_id[7];               /* 6 chars + NUL - often all-zero/blank in practice, same as real captures */
    struct hfdl_hfnpdu_location location;
    struct hfdl_hfnpdu_time utc_time;
    uint8_t version;
    uint8_t flight_leg;
    uint8_t gs_id;                    /* ground station the aircraft is currently working */
} hfdl_hfnpdu_perf_data_t;

/*
 * `buf`/`len` are the LPDU's user-data bytes exactly as reported by
 * hfdl_mpdu_extract_lpdus() (hfdl_lpdu_result_t doesn't hand back a
 * pointer/length pair directly - the caller already has `decoded` +
 * the LPDU's byte range from the same buffer it fed to
 * hfdl_mpdu_extract_lpdus(); see demod_am.c's call site for exactly
 * where this offset comes from).
 *
 * Returns false (leaving *out unchanged) if:
 *   - len < 47 (the fixed HFNPDU performance-data length dumphfdl's
 *     own PERFORMANCE_DATA_HFNPDU_LEN uses), or
 *   - buf[0] != 0xFF (not a HFNPDU envelope at all - could be a plain
 *     ACARS LPDU that doesn't use the HFNPDU wrapper), or
 *   - buf[1] != 0xD1 (a HFNPDU, but a different type - 0xD0 system
 *     table, 0xD2 system table request, 0xD5 frequency data, 0xDE
 *     delayed echo, 0xFF enveloped ACARS - none of those decoded
 *     here, see this file's top comment).
 * Callers should check the return value before trusting *out - unlike
 * hfdl_mpdu_parse_header()'s own hdr_out->crc_ok pattern, there's no
 * separate per-field validity flag here, since the whole point of
 * this function is "is this even a performance-data message".
 */
bool hfdl_hfnpdu_parse_performance_data(const uint8_t *buf, uint32_t len, hfdl_hfnpdu_perf_data_t *out);

#endif /* HFDL_HFNPDU_H */

#include "hfdl_hfnpdu.h"

#define PERFORMANCE_DATA_HFNPDU_LEN 47u /* from HFNPDU envelope byte through the last freq-change-code byte - see dumphfdl's own PERFORMANCE_DATA_HFNPDU_LEN */
#define HFNPDU_ENVELOPE_MARKER 0xFFu
#define HFNPDU_TYPE_PERFORMANCE_DATA 0xD1u

static uint16_t extract_uint16(const uint8_t *buf)
{
    /* little-endian 16-bit, same as dumphfdl's extract_uint16_t() macro */
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/*
 * Ported verbatim (in effect) from dumphfdl's parse_coordinate() in
 * util.c, which uses a GCC bitfield trick (`struct { int32_t coord:20; }`)
 * to sign-extend a 20-bit value - written out explicitly here instead,
 * to not depend on that extension being available/working the same
 * way on this project's ARM toolchain. `raw` must already have only
 * its low 20 bits meaningful (the two call sites below already mask
 * it that way while assembling it, same as dumphfdl's own callers do).
 */
static double parse_coordinate(uint32_t raw)
{
    int32_t signed_val;
    if (raw & 0x80000u) { /* bit 19 set - negative in 20-bit two's complement */
        signed_val = (int32_t)raw - 0x100000; /* same as (int32_t)(raw | 0xFFF00000u) but without relying on the shift/cast being well-defined for the top bits */
    } else {
        signed_val = (int32_t)raw;
    }
    return (double)signed_val * 180.0 / (double)0x7FFFF; /* 0x7FFFF = 2^19-1, the max positive 20-bit value - same divisor dumphfdl uses */
}

static struct hfdl_hfnpdu_time parse_utc_time(uint32_t t)
{
    struct hfdl_hfnpdu_time result;
    result.hour = (uint8_t)(t / 3600u);
    result.min = (uint8_t)((t % 3600u) / 60u);
    result.sec = (uint8_t)(t % 60u);
    return result;
}

bool hfdl_hfnpdu_parse_performance_data(const uint8_t *buf, uint32_t len, hfdl_hfnpdu_perf_data_t *out)
{
    uint32_t coord;

    if (buf == (void *)0 || out == (void *)0) {
        return false;
    }
    if (len < PERFORMANCE_DATA_HFNPDU_LEN) {
        return false;
    }
    if (buf[0] != HFNPDU_ENVELOPE_MARKER || buf[1] != HFNPDU_TYPE_PERFORMANCE_DATA) {
        return false;
    }

    out->flight_id[0] = (char)buf[2];
    out->flight_id[1] = (char)buf[3];
    out->flight_id[2] = (char)buf[4];
    out->flight_id[3] = (char)buf[5];
    out->flight_id[4] = (char)buf[6];
    out->flight_id[5] = (char)buf[7];
    out->flight_id[6] = '\0';

    /* Coordinate packing is NOT byte-aligned - each 20-bit value spans
     * parts of 3 bytes, with lon's low nibble sharing byte[10] with
     * lat's top nibble. Copied exactly from dumphfdl's own bit
     * shifts/masks - see this file's header comment for why this is
     * trusted rather than re-derived. */
    coord = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | (((uint32_t)buf[10] & 0xFu) << 16);
    out->location.lat = parse_coordinate(coord);
    coord = (((uint32_t)buf[10] & 0xF0u) >> 4) | ((uint32_t)buf[11] << 4) | ((uint32_t)buf[12] << 12);
    out->location.lon = parse_coordinate(coord);

    out->utc_time = parse_utc_time(2u * (uint32_t)extract_uint16(buf + 13));
    out->version = buf[15];
    out->flight_leg = buf[16];
    out->gs_id = (uint8_t)(buf[17] & 0x7Fu);

    return true;
}

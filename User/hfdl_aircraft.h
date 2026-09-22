#ifndef HFDL_AIRCRAFT_H
#define HFDL_AIRCRAFT_H

/*
 * HFDL aircraft table (created 20/09/2026, only built when HFDL_AIRCRAFT_TABLE=1).
 *
 * One record per aircraft, newest activity first, filled from the messages the decoder has
 * analysed: ICAO address, model, last flight ID, last position, time and kind of the last
 * message, and how many messages were heard. The table logic and the text of each screen
 * row live here, with no hardware dependency, so they are tested on the PC
 * (test/host/hfdl_aircraft_test.c). Drawing is in main.c.
 *
 * An aircraft is identified by its ICAO address when a logon message told us that. Until then
 * (a downlink message whose aircraft ID we could not map to an ICAO, e.g. the logon happened
 * before we started listening) it is identified by (ground station ID, aircraft ID) and shown
 * as "ID<n>" in the ICAO column; if its ICAO turns up later, the two records are merged.
 */

#include <stdint.h>

#ifndef HFDL_AIRCRAFT_TABLE
#define HFDL_AIRCRAFT_TABLE 0
#endif

#if HFDL_AIRCRAFT_TABLE

/* ---- one analysed message, as handed over by the decoder (demod_am.c) ---- */
#define HFDL_AC_EV_ICAO   0x01U /* icao is valid */
#define HFDL_AC_EV_POS    0x02U /* lat_e4/lon_e4 are valid */
#define HFDL_AC_EV_FLIGHT 0x04U /* flight is valid */
#define HFDL_AC_EV_UPLINK 0x08U /* sent by the ground station (screen column TX = GS) */
#define HFDL_AC_EV_ASSIGN 0x10U /* logon confirm: ac_id is the ID the ground station has just assigned to icao */
#define HFDL_AC_EV_PERF   0x20U /* the message carried a performance data report (position/flight) */
#define HFDL_AC_EV_IGNORE 0x80U /* not a usable message (invalid or unknown LPDU): leave the table alone */

#define HFDL_AC_ID_NONE 0xFFU   /* aircraft ID 255 = not assigned yet / not known */

typedef struct {
    uint32_t icao;              /* 24-bit ICAO address, valid if HFDL_AC_EV_ICAO */
    int32_t  lat_e4;            /* degrees x 10000, valid if HFDL_AC_EV_POS */
    int32_t  lon_e4;
    uint8_t  ac_id;             /* aircraft ID the message was sent by / to, HFDL_AC_ID_NONE if unknown */
    uint8_t  gs_id;             /* ground station ID */
    uint8_t  lpdu_type;         /* raw LPDU type octet of the first LPDU (see hfdl_ac_event_name()) */
    uint8_t  flags;             /* HFDL_AC_EV_* */
    char     flight[7];         /* NUL-terminated flight ID, valid if HFDL_AC_EV_FLIGHT */
    uint8_t  hh, mm, ss;        /* time of the reception, filled in by the caller */
} hfdl_ac_event_t;

/* ---- one table record: 44 bytes ---- */
#define HFDL_AC_R_ICAO   0x01U
#define HFDL_AC_R_POS    0x02U
#define HFDL_AC_R_FLIGHT 0x04U
#define HFDL_AC_R_UPLINK 0x08U /* the last message was sent by the ground station */

typedef struct __attribute__((may_alias)) {
    uint32_t icao;
    int32_t  lat_e4;
    int32_t  lon_e4;
    uint16_t count;             /* messages heard, saturates at 999 */
    uint8_t  ac_id;
    uint8_t  gs_id;
    uint8_t  flags;             /* HFDL_AC_R_* */
    uint8_t  event;             /* raw LPDU type octet of the last message, or HFDL_AC_EVT_PERF */
    uint8_t  hh, mm, ss;        /* time of the last message */
    char     flight[7];
    char     model[16];         /* aircraft model text, filled by the caller (see icao24_db.h); empty if unknown */
} hfdl_ac_rec_t;

#define HFDL_AC_EVT_PERF 0xFEU

typedef struct {
    hfdl_ac_rec_t *rec;         /* rec[0] is the most recently active aircraft */
    uint8_t        cap;
    uint8_t        count;
} hfdl_ac_table_t;

/* Uses `storage` (any address, any size >= 44 bytes) as the record array: it is aligned up to 4
 * bytes and zeroed. Capacity = whatever fits, at most 255 records. */
void hfdl_ac_init(hfdl_ac_table_t *t, void *storage, uint32_t storage_bytes);

/* Applies one message to the table and returns the record it ended up in (always rec[0]).
 * *needs_model is set to 1 when that record has a known ICAO but no model text yet, so the caller
 * can look it up and fill r->model. Returns 0 (and touches nothing) for an IGNORE event. */
hfdl_ac_rec_t *hfdl_ac_update(hfdl_ac_table_t *t, const hfdl_ac_event_t *ev, uint8_t *needs_model);

/* Text of one screen row / of the column titles: exactly HFDL_AC_ROW_LEN characters + NUL,
 * `out` must hold HFDL_AC_ROW_LEN + 1 bytes. Columns (0-based):
 *   0 ICAO(6) | 7 FLIGHT(7) | 15 TYPE(5) | 21 LAT(6) | 28 LON(7) | 36 TIME(8) | 45 TX(2) | 48 EVENT(10) | 59 N(3) */
#define HFDL_AC_ROW_LEN 62U
void hfdl_ac_format_row(const hfdl_ac_rec_t *r, char *out);
void hfdl_ac_format_titles(char *out);

/* Screen name of an LPDU type octet ("LOGON REQ", "USER DATA", ...), at most 10 characters. */
const char *hfdl_ac_event_name(uint8_t event);

#endif /* HFDL_AIRCRAFT_TABLE */

#endif /* HFDL_AIRCRAFT_H */

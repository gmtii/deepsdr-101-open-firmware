#include "hfdl_aircraft.h"

#if HFDL_AIRCRAFT_TABLE

#include <string.h>

void hfdl_ac_init(hfdl_ac_table_t *t, void *storage, uint32_t storage_bytes)
{
    uintptr_t a = ((uintptr_t)storage + 3U) & ~(uintptr_t)3U;
    uint32_t lost = (uint32_t)(a - (uintptr_t)storage);
    uint32_t cap = (storage_bytes > lost) ? ((storage_bytes - lost) / (uint32_t)sizeof(hfdl_ac_rec_t)) : 0U;

    if (cap > 255U) { cap = 255U; }
    t->rec = (hfdl_ac_rec_t *)a;
    t->cap = (uint8_t)cap;
    t->count = 0U;
    memset(t->rec, 0, cap * sizeof(hfdl_ac_rec_t));
}

/* Moves rec[idx] to rec[0], shifting the ones in between down by one place. */
static void ac_move_to_front(hfdl_ac_table_t *t, uint32_t idx)
{
    if (idx > 0U) {
        hfdl_ac_rec_t tmp = t->rec[idx];
        memmove(&t->rec[1], &t->rec[0], idx * sizeof(hfdl_ac_rec_t));
        t->rec[0] = tmp;
    }
}

hfdl_ac_rec_t *hfdl_ac_update(hfdl_ac_table_t *t, const hfdl_ac_event_t *ev, uint8_t *needs_model)
{
    uint32_t i;
    uint32_t idx = 0U;
    uint8_t found = 0U;
    hfdl_ac_rec_t *r;

    if ((t->cap == 0U) || ((ev->flags & HFDL_AC_EV_IGNORE) != 0U)) {
        if (needs_model != (uint8_t *)0) { *needs_model = 0U; }
        return (hfdl_ac_rec_t *)0;
    }

    /* 1. Which record is this message about? */
    if ((ev->flags & HFDL_AC_EV_ICAO) != 0U) {
        for (i = 0U; (i < t->count) && (found == 0U); i++) {
            if (((t->rec[i].flags & HFDL_AC_R_ICAO) != 0U) && (t->rec[i].icao == ev->icao)) { idx = i; found = 1U; }
        }
        if ((found == 0U) && (ev->ac_id != HFDL_AC_ID_NONE)) {
            /* the ICAO of an aircraft we so far only knew by its ID: merge */
            for (i = 0U; (i < t->count) && (found == 0U); i++) {
                if (((t->rec[i].flags & HFDL_AC_R_ICAO) == 0U) && (t->rec[i].ac_id == ev->ac_id) && (t->rec[i].gs_id == ev->gs_id)) {
                    idx = i; found = 1U;
                }
            }
        }
    } else if (ev->ac_id != HFDL_AC_ID_NONE) {
        for (i = 0U; (i < t->count) && (found == 0U); i++) {
            if ((t->rec[i].ac_id == ev->ac_id) && (t->rec[i].gs_id == ev->gs_id)) { idx = i; found = 1U; }
        }
    }

    /* 2. A logon confirm hands the ID to a new owner: nobody else may keep it. */
    if (((ev->flags & HFDL_AC_EV_ASSIGN) != 0U) && (ev->ac_id != HFDL_AC_ID_NONE)) {
        for (i = 0U; i < t->count; i++) {
            if (((found == 0U) || (i != idx)) && (t->rec[i].gs_id == ev->gs_id) && (t->rec[i].ac_id == ev->ac_id)) {
                t->rec[i].ac_id = HFDL_AC_ID_NONE;
            }
        }
    }

    /* 3. New aircraft: open a slot at the front, dropping the oldest one if the table is full. */
    if (found == 0U) {
        uint32_t keep = (t->count < t->cap) ? t->count : (uint32_t)(t->cap - 1U);
        memmove(&t->rec[1], &t->rec[0], keep * sizeof(hfdl_ac_rec_t));
        memset(&t->rec[0], 0, sizeof(hfdl_ac_rec_t));
        t->rec[0].ac_id = HFDL_AC_ID_NONE;
        if (t->count < t->cap) { t->count++; }
        idx = 0U;
    }
    r = &t->rec[idx];

    /* 4. Apply the message. */
    if ((ev->flags & HFDL_AC_EV_ICAO) != 0U) {
        r->icao = ev->icao;
        r->flags |= HFDL_AC_R_ICAO;
    }
    if (ev->ac_id != HFDL_AC_ID_NONE) {
        r->ac_id = ev->ac_id;
    }
    r->gs_id = ev->gs_id;
    if ((ev->flags & HFDL_AC_EV_FLIGHT) != 0U) {
        memcpy(r->flight, ev->flight, sizeof(r->flight));
        r->flight[sizeof(r->flight) - 1U] = '\0';
        r->flags |= HFDL_AC_R_FLIGHT;
    }
    if ((ev->flags & HFDL_AC_EV_POS) != 0U) {
        r->lat_e4 = ev->lat_e4;
        r->lon_e4 = ev->lon_e4;
        r->flags |= HFDL_AC_R_POS;
    }
    if ((ev->flags & HFDL_AC_EV_UPLINK) != 0U) { r->flags |= HFDL_AC_R_UPLINK; } else { r->flags &= (uint8_t)~HFDL_AC_R_UPLINK; }
    r->event = ((ev->flags & HFDL_AC_EV_PERF) != 0U) ? (uint8_t)HFDL_AC_EVT_PERF : ev->lpdu_type;
    r->hh = ev->hh; r->mm = ev->mm; r->ss = ev->ss;
    if (r->count < 999U) { r->count++; }

    /* 5. Most recent first. */
    ac_move_to_front(t, idx);
    r = &t->rec[0];
    if (needs_model != (uint8_t *)0) {
        *needs_model = (((r->flags & HFDL_AC_R_ICAO) != 0U) && (r->model[0] == '\0')) ? 1U : 0U;
    }
    return r;
}

const char *hfdl_ac_event_name(uint8_t event)
{
    switch (event) {
    case 0x0D: case 0x1D: return "USER DATA";
    case 0x2F: return "DENIED";
    case 0x3F: return "LOGOFF";
    case 0x4F: return "RESUME";
    case 0x5F: return "RESUME CFM";
    case 0x8F: return "LOGON REQ";
    case 0x9F: return "CONFIRM";
    case 0xBF: return "LOGON DLS";
    case HFDL_AC_EVT_PERF: return "POSITION";
    default:   return "OTHER";
    }
}

/* ---- text helpers: fixed columns, no printf ---- */
static void put_str(char *out, uint32_t col, uint32_t width, const char *s)
{
    uint32_t i = 0U;
    while ((i < width) && (s[i] != '\0')) { out[col + i] = s[i]; i++; }
    /* the row was pre-filled with spaces */
}

static void put_num_right(char *out, uint32_t col, uint32_t width, uint32_t v)
{
    uint32_t p = col + width;
    do {
        out[--p] = (char)('0' + (v % 10U));
        v /= 10U;
    } while ((v > 0U) && (p > col));
}

/* Coordinate as "62.58N" / "165.73E" to two decimals, right aligned in `width`. */
static void put_coord(char *out, uint32_t col, uint32_t width, int32_t e4, char pos, char neg)
{
    uint32_t a = (uint32_t)((e4 < 0) ? -e4 : e4);
    uint32_t h = (a + 50U) / 100U;               /* hundredths of a degree */
    char tmp[9];
    uint32_t n = 0U;
    uint32_t i;

    tmp[n++] = (e4 < 0) ? neg : pos;
    tmp[n++] = (char)('0' + (h % 10U)); h /= 10U;
    tmp[n++] = (char)('0' + (h % 10U)); h /= 10U;
    tmp[n++] = '.';
    do { tmp[n++] = (char)('0' + (h % 10U)); h /= 10U; } while ((h > 0U) && (n < sizeof(tmp)));
    for (i = 0U; i < n; i++) { out[col + width - 1U - i] = tmp[i]; } /* tmp holds it backwards */
}

static const char k_hex[] = "0123456789ABCDEF";

void hfdl_ac_format_row(const hfdl_ac_rec_t *r, char *out)
{
    uint32_t i;

    memset(out, ' ', HFDL_AC_ROW_LEN);
    out[HFDL_AC_ROW_LEN] = '\0';

    if ((r->flags & HFDL_AC_R_ICAO) != 0U) {
        for (i = 0U; i < 6U; i++) { out[i] = k_hex[(r->icao >> (20U - 4U * i)) & 0xFU]; }
    } else {
        /* "ID 59", "ID117": the aircraft ID, ICAO not known. Not '#': the 5x7 display font has no glyph for it
         * (nor for '*'), it printed as a blank and the row looked like a bare number in the ICAO column. */
        out[0] = 'I';
        out[1] = 'D';
        put_num_right(out, 2U, 3U, r->ac_id);
    }
    put_str(out, 7U, 7U, ((r->flags & HFDL_AC_R_FLIGHT) != 0U) ? r->flight : "-");
    {
        /* type: the first word of the model text (a type code such as B788), at most 5 characters */
        char t[6];
        uint32_t n = 0U;
        while ((n < 5U) && (r->model[n] != '\0') && (r->model[n] != ' ')) { t[n] = r->model[n]; n++; }
        t[n] = '\0';
        put_str(out, 15U, 5U, (n > 0U) ? t : "-");
    }
    if ((r->flags & HFDL_AC_R_POS) != 0U) {
        put_coord(out, 21U, 6U, r->lat_e4, 'N', 'S');
        put_coord(out, 28U, 7U, r->lon_e4, 'E', 'W');
    } else {
        out[26] = '-';
        out[34] = '-';
    }
    out[36] = (char)('0' + ((r->hh / 10U) % 10U)); out[37] = (char)('0' + (r->hh % 10U)); out[38] = ':';
    out[39] = (char)('0' + ((r->mm / 10U) % 10U)); out[40] = (char)('0' + (r->mm % 10U)); out[41] = ':';
    out[42] = (char)('0' + ((r->ss / 10U) % 10U)); out[43] = (char)('0' + (r->ss % 10U));
    out[45] = ((r->flags & HFDL_AC_R_UPLINK) != 0U) ? 'G' : 'A';
    out[46] = ((r->flags & HFDL_AC_R_UPLINK) != 0U) ? 'S' : 'C';
    put_str(out, 48U, 10U, hfdl_ac_event_name(r->event));
    put_num_right(out, 59U, 3U, r->count);
}

void hfdl_ac_format_titles(char *out)
{
    memset(out, ' ', HFDL_AC_ROW_LEN);
    out[HFDL_AC_ROW_LEN] = '\0';
    put_str(out, 0U, 4U, "ICAO");
    put_str(out, 7U, 6U, "FLIGHT");
    put_str(out, 15U, 4U, "TYPE");
    put_str(out, 24U, 3U, "LAT");
    put_str(out, 32U, 3U, "LON");
    put_str(out, 36U, 4U, "TIME");
    put_str(out, 45U, 2U, "TX");
    put_str(out, 48U, 5U, "EVENT");
    put_str(out, 61U, 1U, "N");
}

#else
typedef int hfdl_aircraft_translation_unit_is_empty_when_HFDL_AIRCRAFT_TABLE_is_0_t; /* ISO C: no empty translation unit */
#endif

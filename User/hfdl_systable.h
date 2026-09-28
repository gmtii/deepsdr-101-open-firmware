#ifndef HFDL_SYSTABLE_H
#define HFDL_SYSTABLE_H

/*
 * HFDL ground-station table (created 20/09/2026, only built when HFDL_AIRCRAFT_TABLE=1):
 * which station(s) serve a given assigned frequency, and the station names. The data is
 * generated from dumphfdl's system table by scripts/make_hfdl_systable.py.
 */

#include <stdint.h>

#ifndef HFDL_AIRCRAFT_TABLE
#define HFDL_AIRCRAFT_TABLE 0
#endif

#if HFDL_AIRCRAFT_TABLE

/* Ground stations whose channel list contains exactly `khz`. Returns HOW MANY distinct stations
 * that is (0 if the frequency is not an HFDL channel in the table; 17919 kHz is shared by 5),
 * and writes the IDs of the first 4 of them to ids[] (ascending station order of the table's
 * frequency rows, no duplicates). */
uint32_t hfdl_gs_for_khz(uint32_t khz, uint8_t ids[4]);

/* Short upper-case name ("AUCKLAND") of ground station `id`, or 0 if the ID is not in the table. */
const char *hfdl_gs_name(uint8_t id);

#endif

#endif /* HFDL_SYSTABLE_H */

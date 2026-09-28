#include "hfdl_systable.h"

#if HFDL_AIRCRAFT_TABLE

#include "hfdl_systable_data.inc" /* generated: k_hfdl_gs_names[], k_hfdl_freq_khz[], k_hfdl_freq_gs[] */

uint32_t hfdl_gs_for_khz(uint32_t khz, uint8_t ids[4])
{
    uint32_t i;
    uint32_t total = 0U;
    uint32_t listed = 0U;
    uint32_t seen = 0U; /* bit n set = station id n already counted (ids are below 32) */

    for (i = 0U; i < HFDL_FREQ_COUNT; i++) {
        uint8_t id = k_hfdl_freq_gs[i];

        if ((k_hfdl_freq_khz[i] != khz) || (id >= 32U) || ((seen & (1UL << id)) != 0U)) {
            continue;
        }
        seen |= (1UL << id);
        total++;
        if (listed < 4U) {
            ids[listed++] = id;
        }
    }
    return total;
}

const char *hfdl_gs_name(uint8_t id)
{
    uint32_t i;

    for (i = 0U; i < HFDL_GS_COUNT; i++) {
        if (k_hfdl_gs_names[i].id == id) {
            return k_hfdl_gs_names[i].name;
        }
    }
    return (const char *)0;
}

#else
typedef int hfdl_systable_translation_unit_is_empty_when_HFDL_AIRCRAFT_TABLE_is_0_t; /* ISO C: no empty translation unit */
#endif

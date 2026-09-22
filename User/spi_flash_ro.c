#include "spi_flash_ro.h"

#if HFDL_ICAO24_DB

#include "spi_flash.h" /* spi_flash_read() */

/* FAT12 geometry of the external flash volume. MUST match spi_flash.c
 * (ROOT_DIR_LBA, FAT1_LBA, DATA_START_SECTOR, DATA_CLUSTER_COUNT); these values
 * were confirmed from the real chip on 17/08/2026, see spi_flash.h. */
#define RO_SECTOR_BYTES       512U
#define RO_FAT1_LBA           4U
#define RO_ROOT_LBA           16U
#define RO_DATA_START_SECTOR  48U
#define RO_DATA_CLUSTER_COUNT 2000U /* clusters 2..2001 */

/* One FAT12 entry, fetched with a 2-byte flash read instead of loading the
 * whole 3 KB FAT onto the stack. */
static uint16_t ro_fat_entry(uint32_t cluster)
{
    uint8_t b[2];
    uint32_t off = cluster + (cluster / 2U);
    uint16_t val;

    spi_flash_read((RO_FAT1_LBA * RO_SECTOR_BYTES) + off, b, 2U);
    val = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return (cluster & 1U) ? (uint16_t)(val >> 4) : (uint16_t)(val & 0x0FFFU);
}

static int ro_build_extents(uint32_t first, uint32_t size, spi_flash_ro_file_t *f)
{
    uint32_t total = (size + RO_SECTOR_BYTES - 1U) / RO_SECTOR_BYTES; /* clusters the file spans */
    uint32_t cur = first;
    uint32_t run_first = first;
    uint32_t run_len = 1U;
    uint32_t i;

    if ((size == 0U) || (first < 2U) || (first >= (RO_DATA_CLUSTER_COUNT + 2U))) {
        return -1;
    }

    for (i = 1U; i < total; i++) {
        uint32_t next = ro_fat_entry(cur);

        if ((next < 2U) || (next >= (RO_DATA_CLUSTER_COUNT + 2U))) {
            return -1; /* free, reserved, bad or end-of-chain before the file's last cluster */
        }
        if (next == (cur + 1U)) {
            run_len++;
        } else {
            if (f->extent_count >= SPI_FLASH_RO_MAX_EXTENTS) {
                return -1;
            }
            f->extents[f->extent_count].first_cluster = (uint16_t)run_first;
            f->extents[f->extent_count].cluster_count = (uint16_t)run_len;
            f->extent_count++;
            run_first = next;
            run_len = 1U;
        }
        cur = next;
    }

    if (f->extent_count >= SPI_FLASH_RO_MAX_EXTENTS) {
        return -1;
    }
    f->extents[f->extent_count].first_cluster = (uint16_t)run_first;
    f->extents[f->extent_count].cluster_count = (uint16_t)run_len;
    f->extent_count++;

    if (ro_fat_entry(cur) < 0xFF8U) {
        return -1; /* the chain must end right after the last cluster */
    }
    f->size = size;
    return 1;
}

int spi_flash_ro_open(const char name8[8], const char ext3[3], spi_flash_ro_file_t *f)
{
    uint32_t i;

    f->size = 0U;
    f->extent_count = 0U;

    for (i = 0U; i < (RO_SECTOR_BYTES / 32U); i++) {
        uint8_t e[32]; /* one directory entry at a time, not the whole 512-byte sector */
        uint32_t j;
        uint8_t match = 1U;

        spi_flash_read((RO_ROOT_LBA * RO_SECTOR_BYTES) + (i * 32U), e, sizeof(e));
        if (e[0] == 0x00U) {
            break; /* end of directory */
        }
        if ((e[0] == 0xE5U) || (e[11] == 0x0FU) || ((e[11] & 0x18U) != 0U)) {
            continue; /* deleted / long-filename fragment / volume label / directory */
        }
        for (j = 0U; j < 8U; j++) {
            if (e[j] != (uint8_t)name8[j]) { match = 0U; break; }
        }
        for (j = 0U; match && (j < 3U); j++) {
            if (e[8U + j] != (uint8_t)ext3[j]) { match = 0U; break; }
        }
        if (!match) {
            continue;
        }
        return ro_build_extents((uint32_t)(e[26] | ((uint16_t)e[27] << 8)),
                                (uint32_t)e[28] | ((uint32_t)e[29] << 8) | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24),
                                f);
    }
    return 0;
}

uint32_t spi_flash_ro_start_addr(const spi_flash_ro_file_t *f)
{
    return (RO_DATA_START_SECTOR + (uint32_t)f->extents[0].first_cluster - 2U) * RO_SECTOR_BYTES;
}

uint32_t spi_flash_ro_read(const spi_flash_ro_file_t *f, uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint32_t done = 0U;

    if (offset >= f->size) {
        return 0U;
    }
    if (len > (f->size - offset)) {
        len = f->size - offset;
    }

    while (done < len) {
        uint32_t pos = offset + done;
        uint32_t cl_idx = pos / RO_SECTOR_BYTES;   /* cluster index within the file */
        uint32_t in_cl = pos % RO_SECTOR_BYTES;
        uint32_t base = 0U;
        uint32_t e;

        for (e = 0U; e < f->extent_count; e++) {
            if (cl_idx < (base + f->extents[e].cluster_count)) {
                break;
            }
            base += f->extents[e].cluster_count;
        }
        if (e == f->extent_count) {
            break; /* cannot happen for a file that opened successfully */
        }
        {
            uint32_t idx_in_ext = cl_idx - base;
            uint32_t left_in_ext = ((f->extents[e].cluster_count - idx_in_ext) * RO_SECTOR_BYTES) - in_cl;
            uint32_t chunk = ((len - done) < left_in_ext) ? (len - done) : left_in_ext;
            uint32_t sector = RO_DATA_START_SECTOR + (uint32_t)f->extents[e].first_cluster + idx_in_ext - 2U;

            spi_flash_read((sector * RO_SECTOR_BYTES) + in_cl, buf + done, chunk);
            done += chunk;
        }
    }
    return done;
}

#else
typedef int spi_flash_ro_translation_unit_is_empty_when_HFDL_ICAO24_DB_is_0_t; /* ISO C: no empty translation unit */
#endif

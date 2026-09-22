#ifndef SPI_FLASH_RO_H
#define SPI_FLASH_RO_H

/*
 * Read-only random access to a file on the external SPI flash's FAT12 volume
 * (created 20/09/2026, only built when HFDL_ICAO24_DB=1).
 *
 * spi_flash_read_file_by_name() in spi_flash.c can only read a file from its
 * first byte and needs the whole FAT (3 KB) plus a 512-byte sector on the stack.
 * The aircraft database (ICAO24.BIN, ~350 KB) has to be searched in place, so
 * this module walks the file's cluster chain ONCE into a short list of
 * contiguous runs ("extents", 12 at most, so a file copied onto a mostly-empty
 * volume, which is what happens in practice, is a single one) and then turns any
 * file offset into a flash address arithmetically. It never writes anything and
 * uses no big buffers.
 *
 * Volume geometry (512-byte sectors, 1 sector per cluster, FAT1 at sector 4,
 * root directory at sector 16, data area from sector 48, clusters 2..2001) is
 * the one confirmed on real hardware and documented in spi_flash.h; the root
 * directory is searched in its first sector only, like spi_flash.c does.
 */

#include <stdint.h>

#ifndef HFDL_ICAO24_DB
#define HFDL_ICAO24_DB 0
#endif

#if HFDL_ICAO24_DB

#define SPI_FLASH_RO_MAX_EXTENTS 12U

typedef struct {
    uint16_t first_cluster;
    uint16_t cluster_count;
} spi_flash_ro_extent_t;

typedef struct {
    uint32_t size;                                         /* file size in bytes */
    uint8_t  extent_count;
    spi_flash_ro_extent_t extents[SPI_FLASH_RO_MAX_EXTENTS];
} spi_flash_ro_file_t;

/* name8 / ext3: raw FAT 8.3 fields, space padded, NOT null-terminated (same
 * convention as spi_flash_read_file_by_name()).
 * Returns 1 = ready to read, 0 = no such file in the root directory's first
 * sector, -1 = the file exists but cannot be used (empty, broken cluster chain,
 * or more than SPI_FLASH_RO_MAX_EXTENTS fragments - copy it onto the volume again). */
int spi_flash_ro_open(const char name8[8], const char ext3[3], spi_flash_ro_file_t *f);

/* Reads up to len bytes at file offset `offset` (clamped to the file size).
 * Returns the number of bytes read. */
uint32_t spi_flash_ro_read(const spi_flash_ro_file_t *f, uint32_t offset, uint8_t *buf, uint32_t len);

/* Flash address of the file's first byte (diagnostics only). */
uint32_t spi_flash_ro_start_addr(const spi_flash_ro_file_t *f);

#endif /* HFDL_ICAO24_DB */

#endif /* SPI_FLASH_RO_H */

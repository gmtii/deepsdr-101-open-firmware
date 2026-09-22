#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>

#ifndef HFDL_ICAO24_DB
#define HFDL_ICAO24_DB 0
#endif

/*
 * Bit-banged transport driver for the external SPI NOR flash chip
 * that SHARES this board's touch-panel bus.
 *
 * PINS (per the project owner, 17/08/2026): CS PB6, SCLK PB3, MISO
 * PB4, MOSI PB5. SCLK/MISO/MOSI are the SAME physical pins touch.c
 * already bit-bangs for the XPT2046 (see touch.h's pin comment) - only
 * the chip-select differs (PB6 here vs PD6 for the touch controller).
 * This is a normal, safe way to put two independent SPI devices on
 * one bus: each has its own CS, and a deselected device (CS held
 * HIGH) ignores whatever SCLK/MOSI does and tri-states MOSI - which
 * both this driver and touch.c are careful to guarantee (CS idles
 * high from init, only goes low for the duration of one transaction).
 * Neither driver needs to know the other exists at the bus-electrical
 * level; they just both have to keep holding up their end of that
 * contract. spi_flash_init() reconfigures PB3/4/5 itself (harmless,
 * idempotent GPIO writes, same mode/speed touch_init() already uses)
 * so this driver doesn't secretly depend on touch_init() having run
 * first - but call order in main() should still put spi_flash_init()
 * AFTER touch_init() so the "who's on this bus" story reads top to
 * bottom for the next person.
 *
 * *** THE BOOTLOADER ALREADY USES THIS CHIP TO STORE update4.bin, IN
 * WHAT'S BELIEVED TO BE A FAT FILESYSTEM - NEITHER HAS BEEN CONFIRMED
 * ON REAL HARDWARE YET ***. Until that confirmation happens, this
 * driver is DELIBERATELY READ-ONLY: JEDEC ID (identifies the chip,
 * standard command across essentially every SPI NOR flash - Winbond/
 * GigaDevice/Macronix/ISSI/...) and a plain byte-range READ. Nothing
 * in this file erases or programs a single byte - see
 * spi_flash_probe_dump()'s comment for the confirmation step that
 * needs to run and be checked against real hardware BEFORE any
 * write/erase support gets added on top of this.
 *
 * Once confirmed, the recommended next step is NOT to hand-roll FAT
 * directory/FAT-table parsing directly against spi_flash_read() - a
 * hand-rolled FAT writer is exactly the kind of thing that silently
 * corrupts an existing volume one untested edge case at a time (a
 * split cluster chain, a long-filename entry, a dirty FAT32 FSInfo
 * sector...). Integrate a real, tested FAT library instead - FatFs is
 * the standard choice for a bare-metal project this size (MIT-
 * licensed, no heap requirement, small footprint, exactly the kind of
 * well-worn dependency this project already leans on for CMSIS-DSP) -
 * via its diskio.c shim calling into this file's
 * spi_flash_read()/a future spi_flash_write_sector(). That keeps this
 * file scoped to just being the SPI transport, and gets the actual
 * FAT correctness from code that's been exercised against real FAT
 * volumes far more than anything written from scratch here could be
 * in a reasonable amount of time.
 */

typedef struct {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity_code; /* JEDEC convention is usually log2(bytes) - e.g. 0x18 = 2^24 = 16MB - but this is a vendor convention, not a hard guarantee; cross-check against the chip's actual datasheet once manufacturer_id/memory_type identify it */
} spi_flash_jedec_id_t;

/* Call once at boot, after touch_init() - see this file's header
 * comment for why the order doesn't strictly matter but reads better
 * that way. Leaves this chip's CS (PB6) high (deselected); does not
 * touch PD6 or know anything about the touch controller. */
void spi_flash_init(void);

/* Reads the 3-byte JEDEC ID (command 0x9F). Read-only, no erase/write
 * cycle risk - safe to call on hardware whose exact chip/layout isn't
 * confirmed yet. Use this FIRST. */
void spi_flash_read_jedec_id(spi_flash_jedec_id_t *out);

/* Reads `len` bytes starting at flash byte address `addr` into `buf`,
 * via the standard READ command (0x03, 24-bit address, no dummy
 * cycles - the slowest but most universally-supported read variant,
 * which is fine here: this project's use case is an occasional
 * settings-file read, not a throughput-sensitive path like the SDR's
 * own I/Q DMA). Purely read-only. */
void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * --- WRITE support ---------------------------------------------------
 *
 * Standard SPI NOR mechanics (true of essentially every 25xx-series
 * chip, this Winbond W25Q16 included): PROGRAM can only clear bits
 * (1->0), never set them - the ONLY way to turn a 0 bit back into a 1
 * is to ERASE, and erase only works on a whole aligned block at a
 * time (4KB here, via the standard 0x20 command). That means you can
 * NEVER safely modify just "the bytes you care about" in place if
 * ANY of them are already non-0xFF (e.g. this volume's existing
 * directory entries and file data, all written long ago) - the whole
 * containing 4KB block has to be read into RAM, modified there, then
 * the WHOLE block erased and reprogrammed. spi_flash_write_block_4k()
 * assumes the caller already has the desired FULL 4KB image ready;
 * spi_flash_block_read_modify_write() does the read+splice step too,
 * and is the one actually safe to call against a block that might
 * already hold real data (e.g. the root directory's block, which also
 * holds CHANNEL.CSV's and SYSTEM~1's entries) - it always preserves
 * whatever else is in that block. spi_flash_sector_erase_4k()/
 * spi_flash_page_program() are exposed too as the raw primitives, for
 * whatever ends up building the actual FAT writer on top.
 */

/* Erases the 4KB-aligned block containing `addr` (rounds `addr` down
 * to the nearest 4KB boundary itself - callers do not need to
 * pre-align it). Blocks (busy-polls the status register) until done -
 * a 4KB sector erase is typically tens of milliseconds on real W25Q
 * hardware, fine for an occasional settings save, not something to
 * call from a time-critical path. */
void spi_flash_sector_erase_4k(uint32_t addr);

/* Programs up to 256 bytes at `addr` - `addr` and `addr+len` must not
 * cross a 256-byte page boundary except at len==256 starting exactly
 * on one (standard SPI NOR page-program restriction - the chip
 * silently wraps within the page instead of continuing into the next
 * one if you get this wrong, corrupting the write). Blocks until
 * done, same as the erase above. Does NOT erase first - the target
 * bytes must already be 0xFF (fresh from an erase) for this to do
 * anything meaningful; see spi_flash_write_block_4k() for the erase+
 * program pairing most callers actually want. */
void spi_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len);

/* Erases the 4KB-aligned block containing `block_addr`, then programs
 * the full 4096 bytes of `data4k` into it (16 back-to-back 256-byte
 * page-program calls). `data4k` must already be the COMPLETE desired
 * image of that block - this function does not read or preserve
 * anything currently there. See spi_flash_block_read_modify_write()
 * for the version that does. */
void spi_flash_write_block_4k(uint32_t block_addr, const uint8_t *data4k);

/* Safely changes `new_len` bytes at byte offset `modify_offset_in_block`
 * (relative to the START of the 4KB block, NOT to `any_addr_in_block`)
 * within the 4KB-aligned block containing `any_addr_in_block`: reads
 * the current full 4KB block first, splices in the new bytes in RAM,
 * then erases and reprograms the whole block - so anything else
 * already in that block (other files' data, other directory entries,
 * ...) survives untouched. This is the primitive to reach for whenever
 * a write might land in a block that could already hold real data -
 * which on this volume is essentially anywhere except a block a prior
 * spi_flash_probe_fat_scan() has confirmed is entirely free. */
void spi_flash_block_read_modify_write(uint32_t any_addr_in_block,
                                        uint32_t modify_offset_in_block,
                                        const uint8_t *new_bytes,
                                        uint32_t new_len);

/* Diagnostic bring-up probe - dumps the JEDEC ID and a best-effort
 * interpretation of the first 512 bytes (LBA 0, where a FAT boot
 * sector or MBR would live if this chip really does hold a FAT
 * volume) to the debug UART: the 0x55AA boot signature, OEM name
 * field, a FAT12/16/32 type string if one is found, and the raw bytes
 * of the BIOS Parameter Block for manual cross-checking. See this
 * file's header comment - this needs to be run and its output
 * reviewed against real hardware BEFORE any write/erase support is
 * added. Requires DEBUG_UART_ENABLED=1 to actually produce output
 * (see debug_uart.h) - safe to call either way, just a no-op when
 * that's 0. */
void spi_flash_probe_dump(void);

/* Diagnostic bring-up probe #2 - dumps the first sector of the root
 * directory (see spi_flash_probe_root_dir()'s comment in
 * spi_flash.c for the confirmed LBA and why). Run AFTER
 * spi_flash_probe_dump() has confirmed a FAT12/16 volume - meaningless
 * (and technically undefined, though still just a read at whatever
 * LBA it's hardcoded to look at) otherwise. Same DEBUG_UART_ENABLED
 * requirement, same read-only guarantee. */
void spi_flash_probe_root_dir(void);

/*
 * FAT12 free-space scan - confirmed 17/08/2026 from
 * spi_flash_probe_root_dir()'s output: this volume's root directory
 * currently holds SYSTEM~1 (a Windows-created hidden folder, harmless)
 * and CHANNEL.CSV (727 bytes), confirmed via
 * spi_flash_probe_dump()'s bootloader USB-MSC discussion (17/08/2026)
 * to be the SAME filesystem the bootloader exposes for update4.bin,
 * not a separate area - so this IS the right place for a settings
 * file too, alongside CHANNEL.CSV.
 *
 * Reads FAT copy #1 in full (sectors 4-9, 3072 bytes = 2048 packed
 * 12-bit entries per the standard FAT12 packing: two entries share
 * three bytes) and walks the confirmed data-cluster range (clusters
 * 2..2001 - derived from this volume's own BPB fields: data starts at
 * sector reserved(4)+fats(2)*sectors_per_fat(6)+root_dir(32)=48,
 * 2000 data sectors remain out of 2048 total, 1 sector/cluster) to
 * report how many clusters are free (FAT entry == 0x000), AND to find
 * the first 4KB-aligned block (8 consecutive clusters, since 1
 * sector/cluster and 512 bytes/sector) that is ENTIRELY free - the
 * only kind of block spi_flash_write_block_4k() can safely target
 * without needing the read-modify-write path (there is nothing in it
 * to preserve). Data sector 48 is itself 4KB-block-aligned
 * (48/8=6 - no fractional block), so cluster 2 starts exactly on a
 * block boundary and every 8 clusters after that forms one more block
 * - clean, no split-block edge case to handle.
 *
 * Still entirely read-only.
 */
typedef struct {
    uint32_t total_data_clusters;
    uint32_t free_data_clusters;
    uint8_t  found_free_block;        /* 1 if an entirely-free 4KB block was found */
    uint32_t free_block_first_cluster;/* only valid if found_free_block */
    uint32_t free_block_byte_addr;    /* 4KB-aligned flash byte address of that block - only valid if found_free_block */
} spi_flash_fat_scan_t;

void spi_flash_probe_fat_scan(spi_flash_fat_scan_t *out);

/* Write-path bring-up self-test - see spi_flash.c's comment. Only
 * safe to call with a `scan` whose found_free_block is 1 (an entirely
 * free block, just confirmed by spi_flash_probe_fat_scan() in this
 * SAME run - not persisted/trusted across runs, since nothing stops
 * some other write from having happened in between). Writes a known
 * pattern into that block via spi_flash_block_read_modify_write(),
 * reads it back, and reports PASS/FAIL - this is the first thing that
 * actually exercises the erase+program path on real hardware, before
 * anything that matters (the settings file itself) depends on it. */
void spi_flash_probe_write_selftest(const spi_flash_fat_scan_t *scan);

/*
 * --- CONFIG.CSV: minimal FAT12 file create + read ---------------------
 *
 * Built on top of everything above, now that
 * spi_flash_probe_write_selftest() has confirmed the erase+program
 * path works on real hardware (17/08/2026). Deliberately scoped down
 * from "a general FAT writer" to exactly what this project needs
 * right now:
 *   - spi_flash_write_new_file(): creates a NEW file (does not
 *     search for/reuse an existing entry of the same name - calling
 *     it twice with the same name creates TWO directory entries,
 *     both pointing at valid data, which is confusing but not
 *     corrupt; updating-in-place needs the old entry's clusters
 *     found and freed first, not implemented yet) in the root
 *     directory's FIRST sector only (16 entries - this volume
 *     currently uses 2 of them, plenty of room), using a
 *     spi_flash_probe_fat_scan() result FROM THE SAME RUN as both the
 *     cluster allocator (the whole confirmed-free block becomes this
 *     file's data, so len must fit within CLUSTERS_PER_BLOCK*512 =
 *     4096 bytes - plenty for a settings file, not for anything
 *     bigger) and updates BOTH FAT copies to keep them consistent.
 *   - spi_flash_read_file_by_name(): the inverse - finds a file by
 *     its 8.3 name in the root directory's first sector, follows its
 *     FAT chain, and reads its data back.
 *
 * 8.3 names only (name8/ext3), no long-filename entries - keeps the
 * directory-entry logic simple and matches CHANNEL.CSV's own naming.
 */

/* name8 must be exactly 8 bytes, ext3 exactly 3 bytes (space-padded,
 * e.g. "CONFIG  " / "CSV") - NOT null-terminated C strings, raw FAT
 * 8.3 fields. Returns 1 on success, 0 if len is too big for one
 * confirmed-free block, the root directory's first sector has no free
 * slot left, or `scan` has no confirmed-free block. */
int spi_flash_write_new_file(const spi_flash_fat_scan_t *scan,
                              const char name8[8], const char ext3[3],
                              const uint8_t *data, uint32_t len);

/* Reads up to `out_buf_size` bytes of the named file into `out_buf`.
 * Returns the number of bytes actually read (0 if the name wasn't
 * found in the root directory's first sector, or the file itself is
 * 0 bytes). */
uint32_t spi_flash_read_file_by_name(const char name8[8], const char ext3[3],
                                      uint8_t *out_buf, uint32_t out_buf_size);

/* Creates a NEW file (same as spi_flash_write_new_file()) OR, if a
 * file with this exact 8.3 name already exists in the root
 * directory's first sector, safely REPLACES it in place: frees its
 * old FAT chain (both copies) first, then writes the new data into a
 * freshly re-scanned free block and updates the EXISTING directory
 * entry - no duplicate entry gets added. This is what settings.c
 * actually calls to save CONFIG.CSV; spi_flash_write_new_file() alone
 * would add a fresh duplicate entry every single save.
 *
 * Same size limit as spi_flash_write_new_file() (len must fit in one
 * confirmed-free block, 4096 bytes). The old-chain-freeing step
 * assumes the EXISTING file was itself created by this same driver
 * (a single block, <=8 clusters) - if it finds an existing entry
 * whose recorded size implies more than that, it refuses rather than
 * risk mishandling a chain it doesn't fully understand (e.g. one
 * written by a different tool). Returns 1 on success. */
int spi_flash_write_or_update_file(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len);

/*
 * --- Async (non-blocking) save, fast path only -----------------------
 *
 * Added 18/08/2026: spi_flash_write_or_update_file()'s fast path
 * already cut a save from 6 block erase/program cycles down to 2 (see
 * its comment), but each of those 2 is still a BLOCKING call - the
 * whole main loop (spectrum/waterfall included, though not audio,
 * which is DMA/ISR-driven and unaffected) stalls for however long the
 * flash chip's own internal erase/program circuitry takes, typically
 * tens to a couple hundred ms per 4KB block on a W25Q part - governed
 * by the chip itself, not by how fast bytes are shoved at it, so no
 * amount of transfer-side optimization (DMA included) touches this
 * part of the cost.
 *
 * This spreads the SAME two block operations over many small steps
 * instead, each doing at most one cheap status-register check or one
 * ~4ms page-program transfer - see spi_flash_async_write_block_poll()'s
 * comment for the low-level primitive this is built on. Call
 * spi_flash_async_save_start() once, then spi_flash_async_save_poll()
 * once per main loop iteration until it reports done or error.
 *
 * Deliberately scoped to the SAME fast-path preconditions as
 * spi_flash_write_or_update_file() (existing entry, unchanged cluster
 * count) - spi_flash_async_save_start() returns 0 immediately without
 * starting anything if those aren't met (first-ever save of a given
 * file, or a save whose size crosses a cluster-count boundary), and
 * the caller is expected to fall back to the ordinary blocking
 * spi_flash_write_or_update_file() in that case. Only one async save
 * can be in flight at a time (module-global state, matching this
 * project's generally single-threaded, single-outstanding-operation
 * style elsewhere - see e.g. touch_calib.c).
 */

typedef enum {
    SPI_FLASH_ASYNC_IDLE = 0,  /* nothing in progress */
    SPI_FLASH_ASYNC_BUSY,      /* still working - poll again next iteration */
    SPI_FLASH_ASYNC_DONE,      /* finished successfully */
    SPI_FLASH_ASYNC_ERROR,     /* something failed - see the debug UART */
} spi_flash_async_status_t;

/* `data` must stay valid and UNCHANGED until spi_flash_async_save_poll()
 * reports DONE or ERROR - it is not copied at start time, only
 * referenced. Returns 1 if the async save actually started (poll from
 * here on), 0 if the fast-path preconditions weren't met (nothing
 * started - fall back to spi_flash_write_or_update_file()). */
uint8_t spi_flash_async_save_start(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len);

/* Call once per main loop iteration while a save is in progress. */
spi_flash_async_status_t spi_flash_async_save_poll(void);

#if HFDL_ICAO24_DB
/* 1 while an async save is between start and DONE/ERROR. The flash chip ignores
 * read commands while it is programming or erasing, so anything that reads the
 * chip from the main loop (icao24_db.c) must not do so while this is 1. */
uint8_t spi_flash_async_save_in_progress(void);
#endif

/* Diagnostic bring-up probe #3 - walks CHANNEL.CSV's FAT12 cluster
 * chain and prints its content. See spi_flash_probe_channel_csv()'s
 * comment in spi_flash.c for why this specific file (found by
 * spi_flash_probe_root_dir() on real hardware, 17/08/2026) is worth
 * reading. Same DEBUG_UART_ENABLED requirement, same read-only
 * guarantee. */
void spi_flash_probe_channel_csv(void);

#endif /* SPI_FLASH_H */

#include "spi_flash.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

/* --- pin definitions (per the project owner, 17/08/2026) -----------
 * SCLK/MISO/MOSI are shared with touch.c's XPT2046 bus - see this
 * file's header comment for the full "two devices, one bus, separate
 * CS" reasoning. Only F_CS is unique to this chip. */
#define F_SCLK_PORT  GPIOB
#define F_SCLK_PIN   GPIO_PIN_3
#define F_MISO_PORT  GPIOB
#define F_MISO_PIN   GPIO_PIN_4
#define F_MOSI_PORT  GPIOB
#define F_MOSI_PIN   GPIO_PIN_5
#define F_CS_PORT    GPIOB
#define F_CS_PIN     GPIO_PIN_6

/* Standard SPI NOR opcodes - near-universal across Winbond/
 * GigaDevice/Macronix/ISSI/... (which is exactly why probing with
 * these is safe before the exact chip is even identified). */
#define CMD_JEDEC_ID      0x9FU
#define CMD_READ          0x03U
#define CMD_WRITE_ENABLE  0x06U
#define CMD_READ_STATUS1  0x05U
#define CMD_PAGE_PROGRAM  0x02U
#define CMD_SECTOR_ERASE_4K 0x20U

#define FLASH_PAGE_SIZE    256U
#define FLASH_SECTOR_SIZE  4096U /* erase granularity - see spi_flash.h's WRITE support comment */

/*
 * FORCED -O0, same uncalibrated-NOP-loop reasoning as i2c_bitbang.c's
 * delay_i2c() and touch.c's own delay_us_approx() - see either of
 * those comments for the full story. This is a separate copy rather
 * than sharing touch.c's (which is static to that file) - trivial
 * function, not worth exposing a cross-file dependency for.
 */
__attribute__((optimize("O0")))
static void delay_us_approx(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 20U; i++) {
        __NOP();
    }
}

static inline void f_clk(uint8_t level)
{
    gpio_bit_write(F_SCLK_PORT, F_SCLK_PIN, level ? SET : RESET);
}

static inline void f_mosi(uint8_t level)
{
    gpio_bit_write(F_MOSI_PORT, F_MOSI_PIN, level ? SET : RESET);
}

static inline uint8_t f_miso(void)
{
    return (gpio_input_bit_get(F_MISO_PORT, F_MISO_PIN) == SET) ? 1U : 0U;
}

static inline void f_cs(uint8_t level)
{
    gpio_bit_write(F_CS_PORT, F_CS_PIN, level ? SET : RESET);
}

/* Full-duplex byte transfer, SPI mode 0, MSB first - same bit-bang
 * idiom (MOSI set up while CLK is low, sampled by the slave on the
 * rising edge, MISO sampled by us right around that same rising edge)
 * as touch.c's xpt2046_transfer(), which is already hardware-
 * confirmed to work correctly on this exact GPIOB bus - reused here
 * rather than re-deriving the timing from scratch. Standard SPI NOR
 * flash chips are far more forgiving of exact sample timing than the
 * XPT2046 is, and at this bit-banged rate (~1us/edge, microseconds
 * per bit vs. these chips' typical tens-of-MHz SCLK ratings) there is
 * enormous timing margin either way. */
static uint8_t spi_xfer_byte(uint8_t out)
{
    uint8_t i;
    uint8_t in = 0U;

    for (i = 0; i < 8U; i++) {
        f_mosi((out & 0x80U) ? 1U : 0U);
        out = (uint8_t)(out << 1);
        delay_us_approx(1);
        f_clk(1);
        in = (uint8_t)((in << 1) | f_miso());
        delay_us_approx(1);
        f_clk(0);
    }
    return in;
}

void spi_flash_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);

    /* SCLK/MOSI/MISO: same mode/speed touch_init() already configures
     * these pins with - see this file's header comment for why
     * reconfiguring them again here is harmless. */
    gpio_mode_set(F_SCLK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_SCLK_PIN);
    gpio_output_options_set(F_SCLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_SCLK_PIN);
    gpio_mode_set(F_MOSI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_MOSI_PIN);
    gpio_output_options_set(F_MOSI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_MOSI_PIN);
    gpio_mode_set(F_MISO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, F_MISO_PIN);

    /* This chip's OWN chip-select (PB6) - deselect FIRST, before
     * anything else on this shared bus, same defensive ordering
     * touch_init() uses for its own CS. */
    gpio_mode_set(F_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_CS_PIN);
    gpio_output_options_set(F_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_CS_PIN);
    f_cs(1);
    f_clk(0);
    f_mosi(0);

    debug_print("spi_flash_init: done (bus shared with touch - see header comment)\n");
}

void spi_flash_read_jedec_id(spi_flash_jedec_id_t *out)
{
    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_JEDEC_ID);
    out->manufacturer_id = spi_xfer_byte(0x00U);
    out->memory_type     = spi_xfer_byte(0x00U);
    out->capacity_code   = spi_xfer_byte(0x00U);
    f_cs(1);
}

void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_READ);
    (void)spi_xfer_byte((uint8_t)(addr >> 16));
    (void)spi_xfer_byte((uint8_t)(addr >> 8));
    (void)spi_xfer_byte((uint8_t)(addr));
    for (i = 0; i < len; i++) {
        buf[i] = spi_xfer_byte(0x00U);
    }
    f_cs(1);
}

/* --- write support --------------------------------------------------
 * See spi_flash.h's WRITE support comment for the erase-before-write
 * mechanics this is all built around. */

static void spi_write_enable(void)
{
    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_WRITE_ENABLE);
    f_cs(1);
}

static uint8_t spi_read_status1(void)
{
    uint8_t s;

    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_READ_STATUS1);
    s = spi_xfer_byte(0x00U);
    f_cs(1);
    return s;
}

/* Status register bit 0 (BUSY/WIP) stays set for the whole duration of
 * an erase or program cycle - typically tens of ms for a 4KB erase,
 * sub-ms for a 256-byte program on real W25Q hardware, but there's no
 * universal guarantee, so this polls rather than using a fixed delay.
 * Guarded against a genuinely stuck chip (wiring fault, wrong opcode
 * for this specific part) rather than hanging forever. */
static void spi_wait_busy(void)
{
    uint32_t guard = 0U;

    while ((spi_read_status1() & 0x01U) != 0U) {
        delay_us_approx(100);
        guard++;
        if (guard > 100000U) { /* ~10s worst case - bail rather than hang the whole radio forever */
            debug_print("spi_flash: spi_wait_busy() gave up after ~10s - chip stuck or not responding?\n");
            break;
        }
    }
}

void spi_flash_sector_erase_4k(uint32_t addr)
{
    spi_write_enable();
    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_SECTOR_ERASE_4K);
    (void)spi_xfer_byte((uint8_t)(addr >> 16));
    (void)spi_xfer_byte((uint8_t)(addr >> 8));
    (void)spi_xfer_byte((uint8_t)(addr));
    f_cs(1);
    spi_wait_busy();
}

void spi_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    spi_write_enable();
    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_PAGE_PROGRAM);
    (void)spi_xfer_byte((uint8_t)(addr >> 16));
    (void)spi_xfer_byte((uint8_t)(addr >> 8));
    (void)spi_xfer_byte((uint8_t)(addr));
    for (i = 0; i < len; i++) {
        (void)spi_xfer_byte(data[i]);
    }
    f_cs(1);
    spi_wait_busy();
}

/* Send-only (no write-enable-then-wait) half of a page program - used
 * by the async block writer below, which needs to issue the command
 * and then return to the caller immediately instead of blocking on
 * spi_wait_busy(). Always exactly FLASH_PAGE_SIZE bytes (unlike
 * spi_flash_page_program()'s variable len) since that is the only
 * size the async path ever uses. */
static void spi_page_program_send(uint32_t addr, const uint8_t *data)
{
    uint32_t i;

    spi_write_enable();
    f_cs(0);
    delay_us_approx(1);
    (void)spi_xfer_byte(CMD_PAGE_PROGRAM);
    (void)spi_xfer_byte((uint8_t)(addr >> 16));
    (void)spi_xfer_byte((uint8_t)(addr >> 8));
    (void)spi_xfer_byte((uint8_t)(addr));
    for (i = 0; i < FLASH_PAGE_SIZE; i++) {
        (void)spi_xfer_byte(data[i]);
    }
    f_cs(1);
}

void spi_flash_write_block_4k(uint32_t block_addr, const uint8_t *data4k)
{
    uint32_t off;
    uint32_t aligned = block_addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);

    spi_flash_sector_erase_4k(aligned);
    for (off = 0U; off < FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
        spi_flash_page_program(aligned + off, data4k + off, FLASH_PAGE_SIZE);
    }
}

void spi_flash_block_read_modify_write(uint32_t any_addr_in_block,
                                        uint32_t modify_offset_in_block,
                                        const uint8_t *new_bytes,
                                        uint32_t new_len)
{
    /* Local (stack), not static: this project's stack lives in the
     * separate 64KB TCM RAM region (see GD32F450VE_FLASH.ld's
     * _estack comment), not the tighter 192KB main SRAM pool that
     * .bss already fills to within ~10KB - a 4KB scratch buffer
     * belongs on the stack here, not as a permanent static
     * allocation eating into that margin for a buffer only actually
     * needed for the brief duration of a save. */
    uint8_t block_buf[FLASH_SECTOR_SIZE];
    uint32_t block_addr = any_addr_in_block & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    uint32_t i;

    spi_flash_read(block_addr, block_buf, FLASH_SECTOR_SIZE);
    for (i = 0U; i < new_len; i++) {
        block_buf[modify_offset_in_block + i] = new_bytes[i];
    }
    spi_flash_write_block_4k(block_addr, block_buf);
}

/* Copies up to (out_size-1) bytes from a raw (non-null-terminated)
 * buffer region into a null-terminated string, replacing anything
 * outside printable ASCII with '.' - for dumping boot-sector text
 * fields (OEM name, FAT type string) that may not actually contain
 * text at all if this isn't really a FAT volume, without risking
 * debug_print() running off the end of raw flash contents looking for
 * a '\0' that isn't there. */
static void ascii_field_to_str(const uint8_t *src, uint8_t len, char *out, uint8_t out_size)
{
    uint8_t i;
    uint8_t n = (uint8_t)((len < (uint8_t)(out_size - 1U)) ? len : (uint8_t)(out_size - 1U));

    for (i = 0; i < n; i++) {
        uint8_t c = src[i];
        out[i] = ((c >= 0x20U) && (c < 0x7FU)) ? (char)c : '.';
    }
    out[n] = '\0';
}

void spi_flash_probe_dump(void)
{
    spi_flash_jedec_id_t id;
    uint8_t sector0[512];
    char field[16];
    uint16_t bytes_per_sector;
    (void)bytes_per_sector;
    uint8_t sectors_per_cluster;
    (void)sectors_per_cluster;
    uint16_t reserved_sectors;
    (void)reserved_sectors;
    uint32_t i;

    debug_print("\n--- spi_flash_probe_dump: DIAGNOSTIC ONLY, read-only, no writes ---\n");

    spi_flash_read_jedec_id(&id);
    debug_print_hex16("  JEDEC manufacturer_id", id.manufacturer_id);
    debug_print_hex16("  JEDEC memory_type", id.memory_type);
    debug_print_hex16("  JEDEC capacity_code", id.capacity_code);
    debug_print("  (0xFF/0x00 on all three usually means nothing answered - check wiring/CS before trusting anything below)\n");

    spi_flash_read(0, sector0, sizeof(sector0));

    debug_print("  --- LBA 0, first 512 bytes (possible boot sector / MBR) ---\n");

    ascii_field_to_str(&sector0[3], 8, field, sizeof(field));
    debug_print("  OEM name field (offset 3, 8 bytes): ");
    debug_print(field);
    debug_print("\n");

    bytes_per_sector    = (uint16_t)(sector0[11] | ((uint16_t)sector0[12] << 8));
    sectors_per_cluster = sector0[13];
    reserved_sectors    = (uint16_t)(sector0[14] | ((uint16_t)sector0[15] << 8));
    debug_print_dec("  bytes_per_sector (offset 11)", bytes_per_sector);
    debug_print_dec("  sectors_per_cluster (offset 13)", sectors_per_cluster);
    debug_print_dec("  reserved_sectors (offset 14)", reserved_sectors);

    if ((sector0[510] == 0x55U) && (sector0[511] == 0xAAU)) {
        debug_print("  boot signature 0x55AA at offset 510/511: PRESENT\n");
    } else {
        debug_print_hex16("  boot signature offset 510 (expected 0x55)", sector0[510]);
        debug_print_hex16("  boot signature offset 511 (expected 0xAA)", sector0[511]);
        debug_print("  -> NOT a standard boot sector, or this isn't LBA 0 of the volume\n");
    }

    ascii_field_to_str(&sector0[54], 8, field, sizeof(field));
    debug_print("  FAT12/16 type string (offset 54, 8 bytes): ");
    debug_print(field);
    debug_print("\n");
    ascii_field_to_str(&sector0[82], 8, field, sizeof(field));
    debug_print("  FAT32 type string (offset 82, 8 bytes): ");
    debug_print(field);
    debug_print("\n");

    debug_print("  --- raw hex, first 96 bytes (BPB lives in here on both FAT16 and FAT32) ---\n");
    for (i = 0; i < 96U; i++) {
        char label[16];
        (void)label;
        uint8_t n = 0U;
        uint32_t v = i;
        char tmp[6];
        uint8_t tn = 0U;

        /* Manual formatting, no sprintf - same policy as main.c's own
         * itoa (see its calib_height_ruler_draw() comment) - builds
         * "byte NN" so each hex value in the dump below is labeled
         * with its offset. */
        label[n++] = 'b'; label[n++] = 'y'; label[n++] = 't'; label[n++] = 'e'; label[n++] = ' ';
        if (v == 0U) {
            tmp[tn++] = '0';
        } else {
            while (v > 0U) { tmp[tn++] = (char)('0' + (v % 10U)); v /= 10U; }
        }
        while (tn > 0U) { label[n++] = tmp[--tn]; }
        label[n] = '\0';

        debug_print_hex16(label, sector0[i]);
    }

    debug_print("--- spi_flash_probe_dump: done ---\n\n");
}

/*
 * Root directory dump - confirmed 17/08/2026 from
 * spi_flash_probe_dump()'s output on real hardware: this chip
 * (Winbond W25Q16, JEDEC EF/40/15 - 2MB total) holds a completely
 * standard FAT12 volume at LBA 0, formatted with an ordinary tool
 * (textbook BPB, "MSDOS5.0" OEM string, "NO NAME" label) - BUT that
 * FAT12 volume's own BPB_TotSec16 field says it's only 2048 sectors =
 * 1MiB, HALF the chip's actual 2MB capacity. What (if anything) lives
 * in the other 1MiB (byte offset 0x100000-0x1FFFFF) is unknown -
 * possibly where update4.bin's raw image itself lives, entirely
 * OUTSIDE this filesystem (which would explain why a 1MiB volume was
 * enough for whatever THIS is actually for).
 *
 * This function reads that confirmed volume's FIRST root-directory
 * sector to check whether update4.bin - or anything else
 * recognizable - lives in there as a normal FAT file:
 *   root dir LBA = reserved_sectors(4) + num_fats(2)*sectors_per_fat(6)
 *               = 16
 *   root dir size = root_entries(512) * 32 bytes / 512 bytes/sector
 *                 = 32 sectors (this function only reads the first -
 *                   16 entries is plenty to see what's actually in
 *                   here; extend if a real directory turns out to
 *                   have more entries than that).
 * All numbers above are taken directly from the BPB fields
 * spi_flash_probe_dump() already printed, not re-derived here - if
 * they ever come out different on a different unit, this function's
 * LBA needs updating to match, same as any other hand-measured
 * hardware constant in this project (see e.g. touch_init()'s
 * calibration comment for the general pattern).
 *
 * Still entirely read-only - see this file's header comment.
 */
#define ROOT_DIR_LBA           16U
#define ROOT_DIR_SECTOR_BYTES  512U

void spi_flash_probe_root_dir(void)
{
    uint8_t buf[ROOT_DIR_SECTOR_BYTES];
    uint32_t i;

    debug_print("\n--- spi_flash_probe_root_dir: first root-directory sector (LBA 16) ---\n");
    spi_flash_read(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, buf, sizeof(buf));

    for (i = 0; i < (sizeof(buf) / 32U); i++) {
        const uint8_t *entry = &buf[i * 32U];
        char name[13];
        (void)name;
        uint8_t n = 0U, j;
        uint16_t first_cluster;
        (void)first_cluster;
        uint32_t file_size;
        (void)file_size;

        if (entry[0] == 0x00U) {
            debug_print("  (end of directory)\n");
            break;
        }
        if (entry[0] == 0xE5U) {
            continue; /* deleted entry */
        }
        if (entry[11] == 0x0FU) {
            continue; /* long-filename fragment, not a real 8.3 entry - the short-name entry it belongs to follows separately */
        }
        if ((entry[11] & 0x08U) != 0U) {
            /* Volume label entry, not a file. */
            for (j = 0U; j < 11U; j++) { if (entry[j] != ' ') { name[n++] = (char)entry[j]; } }
            name[n] = '\0';
            debug_print("  [VOLUME LABEL] ");
            debug_print(name);
            debug_print("\n");
            continue;
        }

        for (j = 0U; j < 8U; j++) { if (entry[j] != ' ') { name[n++] = (char)entry[j]; } }
        if (entry[8] != ' ') {
            name[n++] = '.';
            for (j = 8U; j < 11U; j++) { if (entry[j] != ' ') { name[n++] = (char)entry[j]; } }
        }
        name[n] = '\0';

        first_cluster = (uint16_t)(entry[26] | ((uint16_t)entry[27] << 8));
        file_size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8)
                  | ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);

        debug_print(((entry[11] & 0x10U) != 0U) ? "  [DIR]  " : "  [FILE] ");
        debug_print(name);
        debug_print("\n");
        debug_print_dec("    first_cluster", first_cluster);
        debug_print_dec("    file_size (bytes)", file_size);
    }

    debug_print("--- spi_flash_probe_root_dir: done ---\n\n");
}

/* --- FAT12 free-space scan ------------------------------------------
 * See spi_flash.h's comment for the full derivation of these
 * confirmed-from-hardware constants. */
#define FAT1_LBA            4U
#define FAT_SECTORS         6U
#define FAT_BYTES           (FAT_SECTORS * ROOT_DIR_SECTOR_BYTES) /* 3072 */
#define DATA_START_SECTOR   48U
#define DATA_CLUSTER_COUNT  2000U /* clusters 2..2001 */
#define CLUSTERS_PER_BLOCK  (FLASH_SECTOR_SIZE / ROOT_DIR_SECTOR_BYTES) /* 8 - 1 sector/cluster here, so this is also sectors-per-block */

/* FAT12 entries are packed 2-per-3-bytes - the standard unpack
 * algorithm, same as every other FAT12 implementation. */
static uint16_t fat12_entry(const uint8_t *fat, uint32_t cluster)
{
    uint32_t off = cluster + (cluster / 2U);
    uint16_t val = (uint16_t)(fat[off] | ((uint16_t)fat[off + 1U] << 8));

    return (cluster & 1U) ? (uint16_t)(val >> 4) : (uint16_t)(val & 0x0FFFU);
}

void spi_flash_probe_fat_scan(spi_flash_fat_scan_t *out)
{
    uint8_t fat[FAT_BYTES]; /* stack, not static - see spi_flash_block_read_modify_write()'s comment on why that's fine here */
    uint32_t cluster;
    uint32_t block_idx;
    uint8_t fat_is_blank;

    out->total_data_clusters = DATA_CLUSTER_COUNT;
    out->free_data_clusters = 0U;
    out->found_free_block = 0U;
    out->free_block_first_cluster = 0U;
    out->free_block_byte_addr = 0U;

    spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES, fat, sizeof(fat));

    /*
     * *** 01/09/2026, added alongside dir_find_end_marker_offset()'s
     * fix - same "genuinely blank chip" bug, different spot *** - a
     * cluster is normally "free" when its FAT12 entry reads 0x000, but
     * on NOR flash that has NEVER been formatted at all, every entry
     * reads back as 0xFFF (all bits from the erased 0xFF bytes) - the
     * SAME bit pattern FAT12 also uses for a real file's legitimate
     * end-of-chain marker on its last cluster. Unlike the directory
     * fix above, 0xFFF can't just be treated as "also free"
     * everywhere - that would risk mistaking a real file's actual last
     * cluster for free space once at least one real file exists.
     * Instead, detect the unambiguous special case directly: if the
     * ENTIRE FAT1 copy just read back is 0xFF (i.e. nothing has EVER
     * been written to this volume's FAT at all, not even one file),
     * every cluster genuinely is free, full stop - fat_is_blank makes
     * that case bypass the normal per-entry check below instead of
     * redefining what 0xFFF means in general.
     */
    fat_is_blank = 1U;
    for (cluster = 0U; cluster < sizeof(fat); cluster++) {
        if (fat[cluster] != 0xFFU) {
            fat_is_blank = 0U;
            break;
        }
    }

    for (cluster = 2U; cluster < (2U + DATA_CLUSTER_COUNT); cluster++) {
        if (fat_is_blank || (fat12_entry(fat, cluster) == 0x000U)) {
            out->free_data_clusters++;
        }
    }

    debug_print("\n--- spi_flash_probe_fat_scan: FAT12 free-space scan ---\n");
    debug_print_dec("  total_data_clusters", out->total_data_clusters);
    debug_print_dec("  free_data_clusters", out->free_data_clusters);
    if (fat_is_blank) {
        debug_print("  FAT1 copy read back entirely 0xFF - treating volume as never-formatted, "
                     "every cluster counted free\n");
    }

    /* Data sector 48 is itself 4KB-block-aligned (48/8=6), so cluster
     * 2 starts exactly on a block boundary and every CLUSTERS_PER_BLOCK
     * clusters after that is one more whole block - see spi_flash.h's
     * comment. */
    for (block_idx = 0U; block_idx < (DATA_CLUSTER_COUNT / CLUSTERS_PER_BLOCK); block_idx++) {
        uint32_t first_cluster = 2U + (block_idx * CLUSTERS_PER_BLOCK);
        uint8_t all_free = 1U;
        uint32_t c;

        for (c = first_cluster; c < (first_cluster + CLUSTERS_PER_BLOCK); c++) {
            if (!fat_is_blank && (fat12_entry(fat, c) != 0x000U)) {
                all_free = 0U;
                break;
            }
        }
        if (all_free) {
            uint32_t first_data_sector = DATA_START_SECTOR + (first_cluster - 2U);
            out->found_free_block = 1U;
            out->free_block_first_cluster = first_cluster;
            out->free_block_byte_addr = first_data_sector * ROOT_DIR_SECTOR_BYTES;
            break;
        }
    }

    if (out->found_free_block) {
        debug_print_dec("  first entirely-free 4KB block: starts at cluster", out->free_block_first_cluster);
        debug_print_hex32("  first entirely-free 4KB block: byte address", out->free_block_byte_addr);
    } else {
        debug_print("  no entirely-free 4KB block found in the scanned range\n");
    }
    debug_print("--- spi_flash_probe_fat_scan: done ---\n\n");
}

/* --- write-path bring-up self-test ----------------------------------
 * See spi_flash.h's comment - only call with a `scan` whose
 * found_free_block is 1, from the SAME run that produced it. */
void spi_flash_probe_write_selftest(const spi_flash_fat_scan_t *scan)
{
    static const uint8_t k_pattern[] = "DEEPSDR spi_flash write self-test - if you can read this back, erase+program works.";
    uint8_t readback[sizeof(k_pattern)];
    uint8_t ok;
    uint32_t i;

    if (!scan->found_free_block) {
        debug_print("spi_flash_probe_write_selftest: skipped - no confirmed-free block from this run\n");
        return;
    }

    debug_print("\n--- spi_flash_probe_write_selftest: WRITE test (confirmed-free block only) ---\n");
    debug_print_hex32("  target block byte address", scan->free_block_byte_addr);

    spi_flash_block_read_modify_write(scan->free_block_byte_addr, 0U, k_pattern, sizeof(k_pattern));
    spi_flash_read(scan->free_block_byte_addr, readback, sizeof(readback));

    ok = 1U;
    for (i = 0U; i < sizeof(k_pattern); i++) {
        if (readback[i] != k_pattern[i]) {
            ok = 0U;
            break;
        }
    }

    if (ok) {
        debug_print("  PASS - readback matches exactly. erase+program path confirmed working.\n");
    } else {
        debug_print("  *** FAIL - readback does NOT match what was written *** - do not trust the write path yet\n");
        debug_print_dec("  first mismatching byte offset", i);
    }
    debug_print("--- spi_flash_probe_write_selftest: done ---\n\n");
}

/* --- CONFIG.CSV: minimal FAT12 file create + read - see spi_flash.h's comment --- */

/* Sets one FAT12 entry to `value` within `existing` (a copy of the
 * packed bytes already covering that entry), respecting the shared
 * nibble with whichever OTHER entry packs into the same byte -
 * standard FAT12 packing, see fat12_entry()'s comment. `byte_off` is
 * the entry's offset (cluster + cluster/2) relative to the START of
 * `existing`, not to the FAT table. */
static void fat12_pack_entry(uint8_t *existing, uint32_t byte_off, uint32_t cluster, uint16_t value)
{
    uint16_t packed = (uint16_t)(existing[byte_off] | ((uint16_t)existing[byte_off + 1U] << 8));

    if (cluster & 1U) {
        packed = (uint16_t)((packed & 0x000FU) | ((value & 0x0FFFU) << 4));
    } else {
        packed = (uint16_t)((packed & 0xF000U) | (value & 0x0FFFU));
    }
    existing[byte_off] = (uint8_t)(packed & 0xFFU);
    existing[byte_off + 1U] = (uint8_t)((packed >> 8) & 0xFFU);
}

/* Chains `num_clusters` starting at `first_cluster` into an EOC-
 * terminated FAT12 chain, in the FAT copy starting at `fat_start_lba`
 * (call once for FAT1's LBA, once for FAT2's, to keep both copies
 * consistent - see this function's caller). All entries for one
 * allocation are guaranteed to land in a SINGLE 4KB FAT block here
 * (num_clusters <= CLUSTERS_PER_BLOCK, and first_cluster always comes
 * from a spi_flash_probe_fat_scan() block boundary), so one
 * read-modify-write is enough regardless of chain length. */
static void fat_write_chain(uint32_t fat_start_lba, uint32_t first_cluster, uint32_t num_clusters)
{
    uint8_t buf[32]; /* generous upper bound for CLUSTERS_PER_BLOCK=8 worth of packed 12-bit entries */
    uint32_t first_entry_off = first_cluster + (first_cluster / 2U);
    uint32_t last_cluster = first_cluster + num_clusters - 1U;
    uint32_t last_entry_off = last_cluster + (last_cluster / 2U) + 1U; /* +1: include the 2nd byte of the last entry's packed pair */
    uint32_t span_bytes = last_entry_off - first_entry_off + 1U;
    uint32_t abs_addr = (fat_start_lba * ROOT_DIR_SECTOR_BYTES) + first_entry_off;
    uint32_t block_addr = abs_addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    uint32_t off_in_block = abs_addr - block_addr;
    uint32_t i;

    spi_flash_read(abs_addr, buf, span_bytes);
    for (i = 0U; i < num_clusters; i++) {
        uint32_t cluster = first_cluster + i;
        uint16_t value = ((i + 1U) < num_clusters) ? (uint16_t)(cluster + 1U) : 0x0FFFU; /* EOC on the last cluster of the chain */
        fat12_pack_entry(buf, (cluster + cluster / 2U) - first_entry_off, cluster, value);
    }
    spi_flash_block_read_modify_write(abs_addr, off_in_block, buf, span_bytes);
}

/* Finds the first end-of-directory marker in the root directory's
 * FIRST sector - see spi_flash.h's comment on why only the first
 * sector is handled. Returns 1 and sets *out_offset (byte offset
 * within that sector) and *out_needs_terminator if found.
 *
 * *** 01/09/2026, fixed for the "CONFIG.CSV never gets created on a
 * genuinely blank chip" bug *** - a real end-of-directory marker
 * (name[0]==0x00) is what a proper FAT12 format (mkfs.fat) leaves
 * behind, since a formatter explicitly ZEROES the unused tail of the
 * root directory area. But a chip that has NEVER been formatted at
 * all - no CHANNEL.CSV, no CONFIG.CSV, nothing ever written here -
 * reads back as the NOR flash's own erased state, 0xFF, everywhere,
 * not 0x00. The project owner confirmed this exact split: CONFIG.CSV
 * gets created fine once at least one other file already exists on
 * the volume (which only happens because THAT file's own creation,
 * at some point in this volume's history, established a real 0x00
 * terminator after it), but never on a truly virgin chip. In this
 * project's own write path (write_file_data_and_entry()), the byte at
 * a directory slot's offset 0 is NEVER deliberately left at 0xFF for
 * any real entry - every entry this driver ever writes gets a real
 * 8.3 name or an explicit 0x00 terminator - so seeing 0xFF here can
 * only mean "genuinely never written," making it just as valid an
 * insertion point as a real 0x00 terminator, with no ambiguity to
 * worry about (unlike the FAT free-cluster scan below, where 0xFFF
 * is also the legitimate EOC value for a real file's last cluster).
 *
 * *** 01/09/2026, second fix, same day - real hardware log from the
 * project owner *** - the 0x00/0xFF fix above still isn't enough on a
 * volume that has real history: a directory entry gets marked 0xE5
 * ("deleted") when a file is removed, but nothing about deletion ever
 * restores a 0x00 terminator afterward (that's not how FAT deletion
 * works - see e.g. spi_flash_probe_root_dir()'s own comment on
 * skipping 0xE5 silently). A volume previously touched by a PC/Mac
 * (this project's own hardware log shows leftover UNTITL~1/TRASH-~1
 * entries - classic macOS volume litter) can easily end up with every
 * one of the first sector's 16 slots consumed at some point in its
 * history, with several later deleted (0xE5) but the sector never
 * getting a fresh 0x00 anywhere - "root directory full" was the
 * genuinely correct answer for the STRICT 0x00/0xFF definition, but
 * wrong in spirit, since 0xE5 slots ARE free for reuse, just without
 * the strong "everything after this is also unused" guarantee 0x00
 * carries. So: still prefer a real 0x00/0xFF (a bigger, safer
 * contiguous free region, when one exists) via a first pass, but fall
 * back to the FIRST 0xE5 slot seen if no 0x00/0xFF exists anywhere in
 * the sector - reusing a hole needs no fresh terminator afterward
 * (whatever already follows it, terminator or more real entries, is
 * unaffected and still correct), hence *out_needs_terminator. */
static int dir_find_end_marker_offset(uint32_t *out_offset, uint8_t *out_needs_terminator)
{
    uint8_t sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i;
    uint32_t deleted_offset = 0U;
    uint8_t have_deleted = 0U;

    spi_flash_read(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, sector, sizeof(sector));
    for (i = 0U; i < (sizeof(sector) / 32U); i++) {
        if ((sector[i * 32U] == 0x00U) || (sector[i * 32U] == 0xFFU)) {
            *out_offset = i * 32U;
            *out_needs_terminator = 1U;
            return 1;
        }
        if ((sector[i * 32U] == 0xE5U) && !have_deleted) {
            deleted_offset = i * 32U; /* remember the FIRST one - matches this
                                        * driver's existing "earliest usable
                                        * slot" preference elsewhere */
            have_deleted = 1U;
        }
    }
    if (have_deleted) {
        *out_offset = deleted_offset;
        *out_needs_terminator = 0U;
        return 1;
    }
    return 0;
}

/* Finds an existing entry by 8.3 name in the root directory's first
 * sector. Returns 1 and fills *out_dir_off (byte offset within that
 * sector), *out_first_cluster and *out_size (straight from the entry,
 * for the caller to free the old chain with) if found. */
static int dir_find_entry(const char name8[8], const char ext3[3],
                           uint32_t *out_dir_off, uint16_t *out_first_cluster, uint32_t *out_size)
{
    uint8_t sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i;

    spi_flash_read(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, sector, sizeof(sector));
    for (i = 0U; i < (sizeof(sector) / 32U); i++) {
        const uint8_t *e = &sector[i * 32U];
        uint8_t match;
        uint32_t j;

        if (e[0] == 0x00U) {
            break;
        }
        if ((e[0] == 0xE5U) || (e[11] == 0x0FU) || ((e[11] & 0x18U) != 0U)) {
            continue;
        }
        match = 1U;
        for (j = 0U; j < 8U; j++) {
            if (e[j] != (uint8_t)name8[j]) { match = 0U; break; }
        }
        for (j = 0U; match && (j < 3U); j++) {
            if (e[8U + j] != (uint8_t)ext3[j]) { match = 0U; break; }
        }
        if (!match) {
            continue;
        }
        *out_dir_off = i * 32U;
        *out_first_cluster = (uint16_t)(e[26] | ((uint16_t)e[27] << 8));
        *out_size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        return 1;
    }
    return 0;
}

/* Un-chains `num_clusters` starting at `first_cluster` back to free
 * (0x000) in the FAT copy starting at `fat_start_lba` - the inverse of
 * fat_write_chain(), same single-4KB-block assumption (see that
 * function's comment). Call once per FAT copy, same as
 * fat_write_chain(). */
static void fat_free_chain(uint32_t fat_start_lba, uint32_t first_cluster, uint32_t num_clusters)
{
    uint8_t buf[32];
    uint32_t first_entry_off = first_cluster + (first_cluster / 2U);
    uint32_t last_cluster = first_cluster + num_clusters - 1U;
    uint32_t last_entry_off = last_cluster + (last_cluster / 2U) + 1U;
    uint32_t span_bytes = last_entry_off - first_entry_off + 1U;
    uint32_t abs_addr = (fat_start_lba * ROOT_DIR_SECTOR_BYTES) + first_entry_off;
    uint32_t block_addr = abs_addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    uint32_t off_in_block = abs_addr - block_addr;
    uint32_t i;

    spi_flash_read(abs_addr, buf, span_bytes);
    for (i = 0U; i < num_clusters; i++) {
        uint32_t cluster = first_cluster + i;
        fat12_pack_entry(buf, (cluster + cluster / 2U) - first_entry_off, cluster, 0x0000U);
    }
    spi_flash_block_read_modify_write(abs_addr, off_in_block, buf, span_bytes);
}

/* Shared tail for both spi_flash_write_new_file() and
 * spi_flash_write_or_update_file(): chains both FAT copies, writes the
 * file data (the whole confirmed-free block, this file owns all of
 * it), and writes the 32-byte directory entry at `dir_off` - plus a
 * fresh 32-byte terminator right after it if `write_terminator` is
 * set (a BRAND NEW entry needs one; an entry being overwritten in
 * place does not, since whatever terminator already followed it is
 * still correct). */
static int write_file_data_and_entry(uint32_t dir_off, uint8_t write_terminator, uint8_t write_fat_chain,
                                      const spi_flash_fat_scan_t *scan,
                                      const char name8[8], const char ext3[3],
                                      const uint8_t *data, uint32_t len)
{
    uint32_t num_clusters = (len + (ROOT_DIR_SECTOR_BYTES - 1U)) / ROOT_DIR_SECTOR_BYTES;
    uint32_t first_cluster = scan->free_block_first_cluster;
    uint8_t entry_buf[64];
    uint32_t entry_write_len = write_terminator ? 64U : 32U;
    uint32_t i;

    if (num_clusters == 0U) {
        num_clusters = 1U;
    }

    if (write_fat_chain) {
        fat_write_chain(FAT1_LBA, first_cluster, num_clusters);
        fat_write_chain(FAT1_LBA + FAT_SECTORS, first_cluster, num_clusters);
    }

    {
        uint8_t block_img[FLASH_SECTOR_SIZE];

        for (i = 0U; i < FLASH_SECTOR_SIZE; i++) {
            block_img[i] = 0xFFU;
        }
        for (i = 0U; (i < len) && (i < FLASH_SECTOR_SIZE); i++) {
            block_img[i] = data[i];
        }
        spi_flash_write_block_4k(scan->free_block_byte_addr, block_img);
    }

    for (i = 0U; i < entry_write_len; i++) {
        entry_buf[i] = 0x00U;
    }
    for (i = 0U; i < 8U; i++) {
        entry_buf[i] = (uint8_t)name8[i];
    }
    for (i = 0U; i < 3U; i++) {
        entry_buf[8U + i] = (uint8_t)ext3[i];
    }
    entry_buf[11] = 0x20U; /* ATTR_ARCHIVE */
    entry_buf[16] = 0x21U; entry_buf[17] = 0x5CU; /* creation date, DOS format, fixed placeholder (2026-01-01) */
    entry_buf[18] = 0x21U; entry_buf[19] = 0x5CU; /* last access date, same placeholder */
    entry_buf[24] = 0x21U; entry_buf[25] = 0x5CU; /* write date, same placeholder */
    entry_buf[26] = (uint8_t)(first_cluster & 0xFFU);
    entry_buf[27] = (uint8_t)((first_cluster >> 8) & 0xFFU);
    entry_buf[28] = (uint8_t)(len & 0xFFU);
    entry_buf[29] = (uint8_t)((len >> 8) & 0xFFU);
    entry_buf[30] = (uint8_t)((len >> 16) & 0xFFU);
    entry_buf[31] = (uint8_t)((len >> 24) & 0xFFU);
    /* entry_buf[32..63] (if included) stays all-zero: the fresh end-of-directory terminator. */

    spi_flash_block_read_modify_write(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, dir_off, entry_buf, entry_write_len);
    return 1;
}

int spi_flash_write_new_file(const spi_flash_fat_scan_t *scan,
                              const char name8[8], const char ext3[3],
                              const uint8_t *data, uint32_t len)
{
    uint32_t dir_off;
    uint8_t needs_terminator;

    if (!scan->found_free_block) {
        debug_print("spi_flash_write_new_file: no confirmed-free block in `scan` - aborting\n");
        return 0;
    }
    if (len > (CLUSTERS_PER_BLOCK * ROOT_DIR_SECTOR_BYTES)) {
        debug_print("spi_flash_write_new_file: file too big for one confirmed-free block (4096 bytes max right now) - aborting\n");
        return 0;
    }
    if (!dir_find_end_marker_offset(&dir_off, &needs_terminator)) {
        debug_print("spi_flash_write_new_file: root directory's first sector has no end-of-directory marker AND no reusable deleted (0xE5) slot either - genuinely full - aborting\n");
        return 0;
    }
    /* A reused 0xE5 slot only needs room for the entry itself (32
     * bytes) - the fresh terminator (needs_terminator) is what
     * pushes this to 64. */
    if ((dir_off + (needs_terminator ? 64U : 32U)) > ROOT_DIR_SECTOR_BYTES) {
        debug_print("spi_flash_write_new_file: not enough room in the root directory's first sector for the new entry - aborting\n");
        return 0;
    }

    if (!write_file_data_and_entry(dir_off, needs_terminator, 1U, scan, name8, ext3, data, len)) {
        return 0;
    }
    debug_print("spi_flash_write_new_file: done\n");
    return 1;
}

int spi_flash_write_or_update_file(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len)
{
    uint32_t dir_off;
    uint16_t old_cluster;
    uint32_t old_size;
    uint32_t new_num_clusters;
    spi_flash_fat_scan_t scan;

    if (len > (CLUSTERS_PER_BLOCK * ROOT_DIR_SECTOR_BYTES)) {
        debug_print("spi_flash_write_or_update_file: data too big for one block (4096 bytes max right now) - aborting\n");
        return 0;
    }
    new_num_clusters = (len + (ROOT_DIR_SECTOR_BYTES - 1U)) / ROOT_DIR_SECTOR_BYTES;
    if (new_num_clusters == 0U) {
        new_num_clusters = 1U;
    }

    if (dir_find_entry(name8, ext3, &dir_off, &old_cluster, &old_size)) {
        uint32_t old_num_clusters = (old_size + (ROOT_DIR_SECTOR_BYTES - 1U)) / ROOT_DIR_SECTOR_BYTES;

        if (old_num_clusters == 0U) {
            old_num_clusters = 1U;
        }
        if (old_num_clusters > CLUSTERS_PER_BLOCK) {
            debug_print("spi_flash_write_or_update_file: existing entry's chain is bigger than this driver handles - aborting rather than risk it\n");
            return 0;
        }

        if (old_num_clusters == new_num_clusters) {
            /*
             * FAST PATH - same cluster count as before, so the
             * existing FAT chain (same length, same EOC position) is
             * already byte-for-byte correct and does not need to be
             * touched at all: skip freeing it, skip re-scanning for a
             * free block, skip rewriting it. Just overwrite the SAME
             * data block in place and update the directory entry's
             * size field.
             *
             * This matters a lot in practice, not just in theory: this
             * driver's one real caller (settings.c's CONFIG.CSV) is a
             * small fixed-shape CSV that always fits in a single
             * 512-byte cluster, so old_num_clusters==new_num_clusters
             * (both 1) on essentially every save - meaning THIS path,
             * not the slow one below, is what actually runs almost
             * every time. Added 17/08/2026 after the project owner
             * noticed each save visibly froze the spectrum/waterfall
             * for a moment (both share the main loop with this call;
             * audio didn't glitch, being DMA/ISR-driven and
             * independent of it - see s_settings_ready_for_autosave's
             * comment in main.c for the debounce this is paired
             * with): the slow path below does up to SIX separate 4KB
             * erase+reprogram cycles per save (free FAT1+FAT2, rescan,
             * write FAT1+FAT2, data, entry); this fast path does just
             * TWO (data, entry) - a big enough cut in blocking time to
             * take it from a clearly-noticeable stall down to a brief
             * one, without needing to restructure the low-level
             * primitives themselves to be interruptible.
             */
            spi_flash_fat_scan_t synth;
            uint32_t first_data_sector = DATA_START_SECTOR + ((uint32_t)old_cluster - 2U);

            synth.total_data_clusters = 0U; /* unused below */
            synth.free_data_clusters = 0U;  /* unused below */
            synth.found_free_block = 1U;
            synth.free_block_first_cluster = old_cluster;
            synth.free_block_byte_addr = first_data_sector * ROOT_DIR_SECTOR_BYTES;

            if (!write_file_data_and_entry(dir_off, 0U, 0U /* skip FAT rewrite entirely */, &synth, name8, ext3, data, len)) {
                return 0;
            }
            debug_print("spi_flash_write_or_update_file: fast path (unchanged cluster count) - updated data+entry only\n");
            return 1;
        }

        /* SLOW PATH - cluster count actually changed (grew or shrank),
         * so the old chain genuinely needs freeing and a fresh one
         * needs allocating. */
        fat_free_chain(FAT1_LBA, old_cluster, old_num_clusters);
        fat_free_chain(FAT1_LBA + FAT_SECTORS, old_cluster, old_num_clusters);

        spi_flash_probe_fat_scan(&scan);
        if (!scan.found_free_block) {
            debug_print("spi_flash_write_or_update_file: no free block after freeing the old chain - aborting\n");
            return 0;
        }
        if (!write_file_data_and_entry(dir_off, 0U, 1U, &scan, name8, ext3, data, len)) {
            return 0;
        }
        debug_print("spi_flash_write_or_update_file: updated existing entry in place (slow path, cluster count changed)\n");
        return 1;
    }

    spi_flash_probe_fat_scan(&scan);
    if (!scan.found_free_block) {
        debug_print("spi_flash_write_or_update_file: no free block - aborting\n");
        return 0;
    }
    {
    uint8_t needs_terminator;

    if (!dir_find_end_marker_offset(&dir_off, &needs_terminator)) {
        debug_print("spi_flash_write_or_update_file: root directory full (no end-of-directory marker AND no reusable deleted (0xE5) slot) - aborting\n");
        return 0;
    }
    if ((dir_off + (needs_terminator ? 64U : 32U)) > ROOT_DIR_SECTOR_BYTES) {
        debug_print("spi_flash_write_or_update_file: not enough room for the new entry - aborting\n");
        return 0;
    }
    if (!write_file_data_and_entry(dir_off, needs_terminator, 1U, &scan, name8, ext3, data, len)) {
        return 0;
    }
    }
    debug_print("spi_flash_write_or_update_file: created new entry\n");
    return 1;
}

uint32_t spi_flash_read_file_by_name(const char name8[8], const char ext3[3],
                                      uint8_t *out_buf, uint32_t out_buf_size)
{
    uint8_t sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i;

    spi_flash_read(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, sector, sizeof(sector));

    for (i = 0U; i < (sizeof(sector) / 32U); i++) {
        const uint8_t *e = &sector[i * 32U];
        uint8_t match;
        uint32_t j;

        if (e[0] == 0x00U) {
            break; /* end of directory */
        }
        if ((e[0] == 0xE5U) || (e[11] == 0x0FU) || ((e[11] & 0x18U) != 0U)) {
            continue; /* deleted / long-filename fragment / volume-label / directory */
        }

        match = 1U;
        for (j = 0U; j < 8U; j++) {
            if (e[j] != (uint8_t)name8[j]) { match = 0U; break; }
        }
        for (j = 0U; match && (j < 3U); j++) {
            if (e[8U + j] != (uint8_t)ext3[j]) { match = 0U; break; }
        }
        if (!match) {
            continue;
        }

        {
            uint16_t cluster = (uint16_t)(e[26] | ((uint16_t)e[27] << 8));
            uint32_t size = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                           | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
            uint32_t remaining = (size < out_buf_size) ? size : out_buf_size;
            uint32_t written = 0U;
            uint8_t fat[FAT_BYTES];

            spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES, fat, sizeof(fat));
            while ((cluster >= 2U) && (cluster < 0xFF8U) && (remaining > 0U)) {
                uint32_t data_sector = DATA_START_SECTOR + (cluster - 2U);
                uint32_t chunk = (remaining < ROOT_DIR_SECTOR_BYTES) ? remaining : ROOT_DIR_SECTOR_BYTES;

                spi_flash_read(data_sector * ROOT_DIR_SECTOR_BYTES, out_buf + written, chunk);
                written += chunk;
                remaining -= chunk;
                cluster = fat12_entry(fat, cluster);
            }
            return written;
        }
    }
    return 0U; /* not found */
}

/* --- async (non-blocking) block write - see spi_flash.h's comment --- */

typedef enum {
    ABLK_ERASE_SEND = 0,
    ABLK_ERASE_WAIT,
    ABLK_PROGRAM_SEND,
    ABLK_PROGRAM_WAIT,
    ABLK_DONE,
} ablk_phase_t;

typedef struct {
    uint8_t  active;
    uint8_t  phase; /* ablk_phase_t */
    uint32_t block_addr;
    const uint8_t *data4k;
    uint32_t page_index;
} spi_flash_async_block_t;

static void spi_flash_async_write_block_start(spi_flash_async_block_t *op, uint32_t block_addr, const uint8_t *data4k)
{
    op->active = 1U;
    op->block_addr = block_addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    op->data4k = data4k;
    op->page_index = 0U;
    op->phase = (uint8_t)ABLK_ERASE_SEND;
}

/* Advances `op` by one small step - a single status-register check
 * (cheap), or one ~4ms page-program transfer, or issuing the erase
 * command itself (also cheap - just 4 command/address bytes, the
 * actual erase happens in the chip's own background circuitry AFTER
 * this returns, which is exactly what ABLK_ERASE_WAIT then polls for
 * instead of blocking on). NEVER blocks for the tens-to-hundreds-of-ms
 * erase/program time itself - see spi_flash.h's comment. Returns 1
 * once the whole block is done. */
static uint8_t spi_flash_async_write_block_poll(spi_flash_async_block_t *op)
{
    if (!op->active) {
        return 1U;
    }

    switch ((ablk_phase_t)op->phase) {
    case ABLK_ERASE_SEND:
        spi_write_enable();
        f_cs(0);
        delay_us_approx(1);
        (void)spi_xfer_byte(CMD_SECTOR_ERASE_4K);
        (void)spi_xfer_byte((uint8_t)(op->block_addr >> 16));
        (void)spi_xfer_byte((uint8_t)(op->block_addr >> 8));
        (void)spi_xfer_byte((uint8_t)(op->block_addr));
        f_cs(1);
        op->phase = (uint8_t)ABLK_ERASE_WAIT;
        return 0U;

    case ABLK_ERASE_WAIT:
        if ((spi_read_status1() & 0x01U) != 0U) {
            return 0U; /* still busy - try again next poll, no blocking wait */
        }
        op->phase = (uint8_t)ABLK_PROGRAM_SEND;
        return 0U;

    case ABLK_PROGRAM_SEND:
        spi_page_program_send(op->block_addr + (op->page_index * FLASH_PAGE_SIZE),
                               op->data4k + (op->page_index * FLASH_PAGE_SIZE));
        op->phase = (uint8_t)ABLK_PROGRAM_WAIT;
        return 0U;

    case ABLK_PROGRAM_WAIT:
        if ((spi_read_status1() & 0x01U) != 0U) {
            return 0U;
        }
        op->page_index++;
        if (op->page_index >= (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)) {
            op->phase = (uint8_t)ABLK_DONE;
        } else {
            op->phase = (uint8_t)ABLK_PROGRAM_SEND;
        }
        return 0U;

    case ABLK_DONE:
    default:
        op->active = 0U;
        return 1U;
    }
}

/* --- async (non-blocking) file save, fast path only - see spi_flash.h's comment --- */

typedef enum {
    ASAVE_IDLE = 0,
    ASAVE_DATA_BLOCK,
    ASAVE_ENTRY_BLOCK,
} asave_phase_t;

/* Module-global, single-outstanding-operation state - see
 * spi_flash.h's comment on why only one async save can be in flight
 * at a time. s_async_scratch is the only extra static RAM this adds
 * (4KB, permanent - the block image has to live somewhere across many
 * poll() calls, and this project avoids heap allocation entirely -
 * see e.g. spi_flash_block_read_modify_write()'s comment on why its
 * OWN 4KB scratch buffer is stack-local instead: that one is a single
 * blocking call with no need to persist across ticks, this one is the
 * opposite case). Reused for BOTH phases (data block, then entry
 * block) one at a time, never both simultaneously. */
static uint8_t s_async_scratch[FLASH_SECTOR_SIZE];
static spi_flash_async_block_t s_async_block_op;
static uint8_t s_async_phase = (uint8_t)ASAVE_IDLE;
static uint32_t s_async_dir_off;
static uint16_t s_async_cluster;
static char s_async_name8[8];
static char s_async_ext3[3];
static uint32_t s_async_data_len;

uint8_t spi_flash_async_save_start(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len)
{
    uint32_t dir_off;
    uint16_t old_cluster;
    uint32_t old_size;
    uint32_t new_num_clusters = (len + (ROOT_DIR_SECTOR_BYTES - 1U)) / ROOT_DIR_SECTOR_BYTES;
    uint32_t old_num_clusters;
    uint32_t i;
    uint32_t first_data_sector;

    if (new_num_clusters == 0U) {
        new_num_clusters = 1U;
    }
    if (len > (CLUSTERS_PER_BLOCK * ROOT_DIR_SECTOR_BYTES)) {
        return 0U; /* too big for the fast path (and for the slow one too, for that matter) */
    }
    if (!dir_find_entry(name8, ext3, &dir_off, &old_cluster, &old_size)) {
        return 0U; /* no existing entry - not the fast path, caller should create one the normal (blocking) way first */
    }
    old_num_clusters = (old_size + (ROOT_DIR_SECTOR_BYTES - 1U)) / ROOT_DIR_SECTOR_BYTES;
    if (old_num_clusters == 0U) {
        old_num_clusters = 1U;
    }
    if (old_num_clusters != new_num_clusters) {
        return 0U; /* cluster count would change - not the fast path, see spi_flash.h's comment */
    }

    first_data_sector = DATA_START_SECTOR + ((uint32_t)old_cluster - 2U);
    for (i = 0U; i < FLASH_SECTOR_SIZE; i++) {
        s_async_scratch[i] = 0xFFU;
    }
    for (i = 0U; (i < len) && (i < FLASH_SECTOR_SIZE); i++) {
        s_async_scratch[i] = data[i];
    }
    spi_flash_async_write_block_start(&s_async_block_op, first_data_sector * ROOT_DIR_SECTOR_BYTES, s_async_scratch);

    s_async_dir_off = dir_off;
    s_async_cluster = old_cluster;
    for (i = 0U; i < 8U; i++) { s_async_name8[i] = name8[i]; }
    for (i = 0U; i < 3U; i++) { s_async_ext3[i] = ext3[i]; }
    s_async_data_len = len;
    s_async_phase = (uint8_t)ASAVE_DATA_BLOCK;
    return 1U;
}

spi_flash_async_status_t spi_flash_async_save_poll(void)
{
    if (s_async_phase == (uint8_t)ASAVE_IDLE) {
        return SPI_FLASH_ASYNC_IDLE;
    }

    if (s_async_phase == (uint8_t)ASAVE_DATA_BLOCK) {
        if (!spi_flash_async_write_block_poll(&s_async_block_op)) {
            return SPI_FLASH_ASYNC_BUSY;
        }
        /* Data block done - read the root directory's block (single
         * blocking READ, fast - reads aren't the bottleneck, only
         * erase/program are, see spi_flash.h's comment), splice in the
         * updated entry (name/ext/cluster unchanged, only the size and
         * placeholder dates get rewritten - same fields
         * write_file_data_and_entry() sets), and kick off the entry
         * block's async write. */
        {
            uint32_t dir_block_addr = (ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES) & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
            uint32_t off_in_block = (ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES) - dir_block_addr + s_async_dir_off;
            uint8_t *e = &s_async_scratch[off_in_block];
            uint32_t i;

            spi_flash_read(dir_block_addr, s_async_scratch, FLASH_SECTOR_SIZE);
            for (i = 0U; i < 8U; i++) { e[i] = (uint8_t)s_async_name8[i]; }
            for (i = 0U; i < 3U; i++) { e[8U + i] = (uint8_t)s_async_ext3[i]; }
            e[11] = 0x20U;
            e[16] = 0x21U; e[17] = 0x5CU;
            e[18] = 0x21U; e[19] = 0x5CU;
            e[24] = 0x21U; e[25] = 0x5CU;
            e[26] = (uint8_t)(s_async_cluster & 0xFFU);
            e[27] = (uint8_t)((s_async_cluster >> 8) & 0xFFU);
            e[28] = (uint8_t)(s_async_data_len & 0xFFU);
            e[29] = (uint8_t)((s_async_data_len >> 8) & 0xFFU);
            e[30] = (uint8_t)((s_async_data_len >> 16) & 0xFFU);
            e[31] = (uint8_t)((s_async_data_len >> 24) & 0xFFU);

            spi_flash_async_write_block_start(&s_async_block_op, dir_block_addr, s_async_scratch);
            s_async_phase = (uint8_t)ASAVE_ENTRY_BLOCK;
        }
        return SPI_FLASH_ASYNC_BUSY;
    }

    if (s_async_phase == (uint8_t)ASAVE_ENTRY_BLOCK) {
        if (!spi_flash_async_write_block_poll(&s_async_block_op)) {
            return SPI_FLASH_ASYNC_BUSY;
        }
        s_async_phase = (uint8_t)ASAVE_IDLE;
        debug_print("spi_flash_async_save: done\n");
        return SPI_FLASH_ASYNC_DONE;
    }

    s_async_phase = (uint8_t)ASAVE_IDLE;
    return SPI_FLASH_ASYNC_ERROR;
}

#include "icao24_db.h"

#if HFDL_ICAO24_DB

#include "spi_flash.h"
#include "spi_flash_ro.h"
#include "debug_uart.h"

#define I24_HDR_LEN   16U
#define I24_PAD       4096U /* padded layout: the structure starts this far into the file (see icao24_db.h) */
#define I24_REC_LEN   5U
#define I24_NAME_MAX  32U

static spi_flash_ro_file_t s_file;
static uint32_t s_base;      /* file offset of the header: I24_PAD or 0 */
static uint32_t s_n_aircraft;
static uint32_t s_types_off;
static uint16_t s_n_types;
static uint8_t  s_name_len;
static uint8_t  s_state; /* 0 = not tried yet, 1 = ready, 2 = unavailable (until reboot) */
static const char *s_fail_reason; /* why s_state is 2, so icao24_db_report() can repeat it later */
static uint8_t  s_hdr[I24_HDR_LEN];  /* first 16 bytes of the file exactly as read, kept for the diagnostic below */
static uint8_t  s_hdr2[I24_HDR_LEN]; /* the 16 bytes at offset I24_PAD (where a padded file has its header) */
static uint8_t  s_hdr_valid;

/* debug_print_hex32() sends its digits straight to the USART (PA9) and NOT to the USB serial
 * mirror, so over USB it shows "0x" and nothing else. These helpers format into a buffer and go
 * through debug_print(), which does reach USB. */
static const char k_hex[] = "0123456789ABCDEF";

static void i24_print_hexbytes(const char *label, const uint8_t *b, uint32_t n)
{
    char line[3U * 16U + 2U];
    uint32_t i;
    uint32_t p = 0U;

    for (i = 0U; (i < n) && (i < 16U); i++) {
        line[p++] = k_hex[b[i] >> 4];
        line[p++] = k_hex[b[i] & 0xFU];
        line[p++] = ' ';
    }
    line[p++] = '\n';
    line[p] = '\0';
    debug_print(label);
    debug_print(line);
}

/* signed decimal, e.g. "-5" */
static void i24_print_signed(const char *label, int32_t v)
{
    char s[14];
    uint32_t u = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    uint32_t p = 12U;
    s[13] = '\0';
    s[12] = '\n';
    do {
        s[--p] = (char)('0' + (u % 10U));
        u /= 10U;
    } while ((u > 0U) && (p > 1U));
    if (v < 0) {
        s[--p] = '-';
    }
    debug_print(label);
    debug_print(&s[p]);
}

/* Looks for the ICAO24.BIN magic ('I','2','4','B') in the 4 KB of flash around where the file
 * should begin, 1 KB before to 3 KB after, and prints its offset relative to that start. Answers
 * "is the file there but shifted?" - 0 would mean it is exactly where it should be, so the fault
 * lies elsewhere. Failure path only. */
static void i24_scan_for_magic(void)
{
    uint32_t base = spi_flash_ro_start_addr(&s_file);
    int32_t off;
    uint8_t w[68];

    for (off = -1024; off < 5120; off += 64) {
        uint32_t i;
        if ((int32_t)base + off < 0) {
            continue;
        }
        spi_flash_read((uint32_t)((int32_t)base + off), w, sizeof(w));
        for (i = 0U; i < 64U; i++) {
            if ((w[i] == (uint8_t)'I') && (w[i + 1U] == (uint8_t)'2') && (w[i + 2U] == (uint8_t)'4') && (w[i + 3U] == (uint8_t)'B')) {
                i24_print_signed("icao24_db:   magic I24B found at byte offset (0 = where the file should start): ", off + (int32_t)i);
                return;
            }
        }
    }
    debug_print("icao24_db:   magic I24B not found within -1024..+5120 bytes of where the file should start\n");
}

/* "icao24_db:   LBA <n>: xx xx ..." - the first 16 bytes of one 512-byte sector as the firmware reads
 * them straight from the chip. */
static void i24_print_sector(uint32_t lba)
{
    char label[40];
    uint8_t b[16];
    uint32_t p = 0U;
    uint32_t i;
    uint32_t div = 10000U;
    static const char pre[] = "icao24_db:   LBA ";

    for (i = 0U; pre[i] != '\0'; i++) { label[p++] = pre[i]; }
    for (; div > 0U; div /= 10U) {
        uint32_t d = (lba / div) % 10U;
        if ((d != 0U) || (p > (sizeof(pre) - 1U)) || (div == 1U)) { label[p++] = (char)('0' + d); }
    }
    label[p++] = ':';
    label[p++] = ' ';
    label[p] = '\0';
    spi_flash_read(lba * 512U, b, sizeof(b));
    i24_print_hexbytes(label, b, sizeof(b));
}

/* Failure-path probe of the whole volume as the FIRMWARE sees the chip, to compare with what the PC
 * sees through the bootloader's USB volume (dd + xxd). 1) The boot sector's own parameters - the ones
 * the PC's FAT driver uses to place a file. 2) The first bytes of the sectors around where the file
 * should start. 3) Every sector start of the 2 MiB chip searched for the ICAO24.BIN magic, so a file
 * that was written but not where it is expected still turns up. Reads only. */
static void i24_diag_volume(void)
{
    uint8_t b[36];
    uint32_t s;
    uint32_t hits = 0U;
    uint32_t expect_lba = spi_flash_ro_start_addr(&s_file) / 512U;
    static const uint16_t k_lba[] = { 0U, 4U, 16U, 48U, 49U, 50U, 51U, 52U };

    spi_flash_read(0U, b, sizeof(b));
    debug_print("icao24_db:   boot sector (sector 0) as the firmware reads it:\n");
    debug_print_dec("icao24_db:     bytes per sector   ", (uint32_t)(b[11] | ((uint32_t)b[12] << 8)));
    debug_print_dec("icao24_db:     sectors per cluster", b[13]);
    debug_print_dec("icao24_db:     reserved sectors   ", (uint32_t)(b[14] | ((uint32_t)b[15] << 8)));
    debug_print_dec("icao24_db:     FAT copies         ", b[16]);
    debug_print_dec("icao24_db:     root dir entries   ", (uint32_t)(b[17] | ((uint32_t)b[18] << 8)));
    debug_print_dec("icao24_db:     total sectors      ", (uint32_t)(b[19] | ((uint32_t)b[20] << 8)));
    debug_print_dec("icao24_db:     sectors per FAT    ", (uint32_t)(b[22] | ((uint32_t)b[23] << 8)));
    debug_print_dec("icao24_db:   sector where the file should start (LBA)", expect_lba);
    for (s = 0U; s < (sizeof(k_lba) / sizeof(k_lba[0])); s++) {
        i24_print_sector(k_lba[s]);
    }
    for (s = 0U; (s < 4096U) && (hits < 3U); s++) {
        uint8_t m[4];
        spi_flash_read(s * 512U, m, sizeof(m));
        if ((m[0] == (uint8_t)'I') && (m[1] == (uint8_t)'2') && (m[2] == (uint8_t)'4') && (m[3] == (uint8_t)'B')) {
            debug_print_dec("icao24_db:   magic I24B at the start of LBA", s);
            hits++;
        }
    }
    if (hits == 0U) {
        debug_print("icao24_db:   magic I24B at no sector start of the whole 2 MiB chip\n");
    }
}

static bool i24_fail(const char *why)
{
    s_fail_reason = why;
    debug_print(why);
    return false;
}

static bool i24_is_header(const uint8_t *h)
{
    return (h[0] == (uint8_t)'I') && (h[1] == (uint8_t)'2') && (h[2] == (uint8_t)'4') && (h[3] == (uint8_t)'B') && (h[4] == 1U);
}

static bool i24_open(void)
{
    uint8_t h[I24_HDR_LEN];
    int r = spi_flash_ro_open("ICAO24  ", "BIN", &s_file);

    if (r == 0) {
        return i24_fail("icao24_db: ICAO24.BIN not found in the flash volume's root directory\n");
    }
    if (r < 0) {
        return i24_fail("icao24_db: ICAO24.BIN is unusable (empty, damaged, or too fragmented - copy it again)\n");
    }
    if (spi_flash_ro_read(&s_file, 0U, h, I24_HDR_LEN) != I24_HDR_LEN) {
        return i24_fail("icao24_db: could not read the ICAO24.BIN header\n");
    }
    {
        uint32_t k;
        for (k = 0U; k < I24_HDR_LEN; k++) {
            s_hdr[k] = h[k];
        }
        s_hdr_valid = 1U;
    }
    /* Padded layout first (header at offset I24_PAD), then the original one (offset 0). */
    s_base = I24_PAD;
    if ((spi_flash_ro_read(&s_file, I24_PAD, s_hdr2, I24_HDR_LEN) != I24_HDR_LEN) || !i24_is_header(s_hdr2)) {
        s_base = 0U;
        if (!i24_is_header(h)) {
            return i24_fail("icao24_db: ICAO24.BIN has the wrong magic/version (a padded file has it at byte 4096, an old one at 0)\n");
        }
    } else {
        uint32_t k;
        for (k = 0U; k < I24_HDR_LEN; k++) { h[k] = s_hdr2[k]; }
    }
    s_name_len   = h[5];
    s_n_types    = (uint16_t)(h[6] | ((uint16_t)h[7] << 8));
    s_n_aircraft = (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);
    s_types_off  = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);

    if ((s_name_len == 0U) || (s_name_len > I24_NAME_MAX) || (s_n_aircraft == 0U)
        || (s_n_aircraft > 0x1000000U)
        || (s_types_off != (I24_HDR_LEN + (I24_REC_LEN * s_n_aircraft)))
        || ((s_base + s_types_off + ((uint32_t)s_n_types * s_name_len)) > s_file.size)) {
        return i24_fail("icao24_db: ICAO24.BIN header does not match the file size\n");
    }
    return true;
}

void icao24_db_report(void)
{
    if (spi_flash_async_save_in_progress() != 0U) {
        return; /* never read the chip while it programs/erases */
    }
    if (s_state == 0U) {
        s_state = i24_open() ? 1U : 2U;
    }
    if (s_state == 1U) {
        debug_print("icao24_db: ICAO24.BIN ready\n");
        debug_print_dec("icao24_db:   aircraft", s_n_aircraft);
        debug_print_dec("icao24_db:   type names", s_n_types);
        debug_print_dec("icao24_db:   file size (bytes)", s_file.size);
        debug_print_dec("icao24_db:   fragments (max 12)", s_file.extent_count);
        debug_print_dec("icao24_db:   header at byte", s_base);
    } else {
        debug_print("icao24_db: NOT available until the next reboot. Reason:\n");
        debug_print((s_fail_reason != (const char *)0) ? s_fail_reason : "icao24_db: unknown\n");
        if (s_hdr_valid != 0U) {
            /* Where the file was found and what its first bytes really are (a valid ICAO24.BIN
             * starts 49 32 34 42 01, i.e. 'I','2','4','B',1). Typical answers: 30 30 30 30 ... =
             * ASCII digits (the PortaPack icao24.db, not the converted file); FF FF FF FF = flash
             * bytes never written (the USB copy did not reach the chip); 00 00 ... = zeros. If the
             * magic turns up at a non-zero offset, the data is shifted relative to the file table. */
            debug_print_dec("icao24_db:   file found: size (bytes)", s_file.size);
            debug_print_dec("icao24_db:   file found: first cluster", (uint32_t)s_file.extents[0].first_cluster);
            debug_print_dec("icao24_db:   file found: fragments", s_file.extent_count);
            i24_print_hexbytes("icao24_db:   first 16 bytes (offset 0):    ", s_hdr, I24_HDR_LEN);
            i24_print_hexbytes("icao24_db:   16 bytes at offset 4096:      ", s_hdr2, I24_HDR_LEN);
            i24_scan_for_magic();
            i24_diag_volume();
        }
    }
}

bool icao24_db_lookup(uint32_t icao24, char *out, uint32_t out_len)
{
    uint32_t lo;
    uint32_t hi;

    if ((out == (char *)0) || (out_len == 0U)) {
        return false;
    }
    out[0] = '\0';

    if (s_state == 2U) {
        return false;
    }
    if (spi_flash_async_save_in_progress() != 0U) {
        return false; /* the chip ignores reads while it programs/erases: try again later */
    }
    if (s_state == 0U) {
        s_state = i24_open() ? 1U : 2U;
        if (s_state != 1U) {
            return false;
        }
    }

    icao24 &= 0xFFFFFFU;
    lo = 0U;
    hi = s_n_aircraft; /* search [lo, hi) */
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) / 2U);
        uint8_t r[I24_REC_LEN];
        uint32_t key;

        if (spi_flash_ro_read(&s_file, s_base + I24_HDR_LEN + (mid * I24_REC_LEN), r, I24_REC_LEN) != I24_REC_LEN) {
            return false;
        }
        key = ((uint32_t)r[0] << 16) | ((uint32_t)r[1] << 8) | (uint32_t)r[2];
        if (key == icao24) {
            uint16_t idx = (uint16_t)(r[3] | ((uint16_t)r[4] << 8));
            uint8_t name[I24_NAME_MAX];
            uint32_t n;
            uint32_t k;

            if (idx >= s_n_types) {
                return false;
            }
            if (spi_flash_ro_read(&s_file, s_base + s_types_off + ((uint32_t)idx * s_name_len), name, s_name_len) != s_name_len) {
                return false;
            }
            n = 0U;
            for (k = 0U; (k < s_name_len) && (name[k] != 0U) && (n < (out_len - 1U)); k++) {
                out[n++] = ((name[k] >= 0x20U) && (name[k] <= 0x7EU)) ? (char)name[k] : '?';
            }
            while ((n > 0U) && (out[n - 1U] == ' ')) {
                n--;
            }
            out[n] = '\0';
            return n > 0U;
        }
        if (key < icao24) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return false;
}

#else
typedef int icao24_db_translation_unit_is_empty_when_HFDL_ICAO24_DB_is_0_t; /* ISO C: no empty translation unit */
#endif

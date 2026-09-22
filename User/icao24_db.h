#ifndef ICAO24_DB_H
#define ICAO24_DB_H

/*
 * Aircraft model lookup by 24-bit ICAO address (20/09/2026, only built when
 * HFDL_ICAO24_DB=1; the HFDL screens use it to append the model to the ICAO row).
 *
 * The data comes from ICAO24.BIN in the root of the external flash volume,
 * generated on the PC by scripts/make_icao24_compact.py from the OpenSky aircraft
 * database (or from the icao24.db the PortaPack tool produces). The full OpenSky
 * database is ~490,000 aircraft (about 72 MB in icao24.db's format), far more than
 * this board's 1 MiB volume, so the converter keeps the aircraft that fly HFDL
 * (jets: airliners, business jets, freighters) and drops the registration.
 *
 * ICAO24.BIN layout (all integers little-endian unless noted). The structure below starts at
 * byte offset 4096 of the file ("padded" layout, what scripts/make_icao24_compact.py writes by
 * default) or at offset 0 (older files); the first 4096 bytes of a padded file are 0xFF filler.
 * WHY: the flash is erased in 4 KB blocks and the settings save (CONFIG.CSV, first data
 * cluster) erases and reprograms its whole block, which - since the PC puts the next file in the
 * next free cluster - also contains the first sectors of ICAO24.BIN. Nothing in the first
 * 4096 bytes of the file can be trusted to survive; the real header therefore sits after them.
 *   offset 0   header, 16 bytes:
 *                'I','2','4','B'   magic
 *                u8   version (1)
 *                u8   name_len     bytes per type-name record (24)
 *                u16  n_types
 *                u32  n_aircraft
 *                u32  types_off    file offset of the type-name table (= 16 + 5 * n_aircraft)
 *   offset 16  n_aircraft records of 5 bytes, sorted by address:
 *                3 bytes  ICAO24, BIG-endian (so the file sorts as the number does)
 *                u16      index into the type-name table
 *   types_off  n_types records of name_len bytes: ASCII, NUL padded, e.g. "B788 Boeing 787-8"
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef HFDL_ICAO24_DB
#define HFDL_ICAO24_DB 0
#endif

#if HFDL_ICAO24_DB

/* Writes the aircraft model for `icao24` into out (NUL terminated, at most out_len-1
 * characters). Returns true if found. Returns false - leaving out empty - when the
 * address is not in the database, ICAO24.BIN is missing or unusable (the first attempt
 * decides and is remembered until the next boot; the reason goes to the debug UART),
 * or the flash is busy with a settings save right now (then simply ask again later).
 * Main-loop use only: it reads the SPI flash, which takes a millisecond or so. */
bool icao24_db_lookup(uint32_t icao24, char *out, uint32_t out_len);

/* Opens ICAO24.BIN if that has not been tried yet and prints its status (or why it cannot be
 * used) on the debug UART. Lookups otherwise open the file lazily on the first reception, which
 * can be long after boot, and whatever is printed before the USB serial port has enumerated
 * never reaches the PC - so main.c calls this once right after the AIC3204 messages (the first
 * lines a USB terminal actually sees) and again every time HFDL is selected. */
void icao24_db_report(void);

#endif

#endif /* ICAO24_DB_H */

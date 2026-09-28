#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>

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

/*
 * LA GEOMETRIA DEL VOLUMEN, PARA QUIEN LA NECESITE. 25/09/2026.
 *
 * La pide spi_flash_ro.c, el lector de acceso aleatorio que se trajo del
 * proyecto padre. SU version llevaba estos cuatro numeros escritos a mano
 * (4, 16, 48, 2000) porque en su radio son esos; este firmware los LEE del
 * sector de arranque desde que geo_lee() existe, asi que copiarlos habria
 * sido meter una segunda verdad sobre el mismo volumen.
 *
 * Y no es una diferencia academica: el dueno de esta radio ha reformateado
 * la tarjeta mas de una vez. Un volumen con otro reparto y unos numeros
 * fijos leen sectores equivocados, y lo que sale de ahi no es un error, son
 * DATOS PLAUSIBLES Y FALSOS - el modelo de otro avion.
 *
 * Devuelve 0 si el volumen todavia no se ha podido leer; en ese caso los
 * cuatro valores quedan sin tocar.
 */
uint8_t spi_flash_geometria(uint32_t *fat1_lba, uint32_t *raiz_lba,
                            uint32_t *datos_lba, uint32_t *clusters);

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

/*
 * --- QUE HAY POR ENCIMA DEL SISTEMA DE FICHEROS - 24/09/2026 ----------
 *
 * SOLO LEE. No borra ni programa un solo byte, a proposito.
 *
 * El 17/08/2026 este chip contesto W25Q16 (JEDEC EF/40/15, 2 MB) y el
 * FAT12 que lleva declaraba 2048 sectores, o sea 1 MB: la MITAD. Pero
 * NINGUNA de esas dos cifras se da aqui por buena. La placa puede llevar
 * otro chip -alguien pudo cambiarlo por uno mayor- y un clon contesta al
 * identificador lo que le parece. Asi que las dos se miden, cada una por
 * su lado, y se ven juntas: si no cuadran, se nota.
 *
 * Lo que haya por encima del sistema de ficheros no lo sabe nadie, y el
 * comentario de spi_flash_probe_root_dir() apunta a lo peor posible -
 * "posiblemente donde vive la imagen de update4.bin".
 *
 * Eso no es un detalle: update4.bin es el unico camino que hay para
 * meter firmware en esta radio. Escribir ahi a ciegas y acertar en el
 * sitio equivocado deja el aparato sin forma de recibir la siguiente
 * version, y no hay marcha atras desde dentro.
 *
 * Asi que antes de guardar imagenes en ningun sitio, esto mira. Lo que
 * devuelve contesta las tres preguntas que hacen falta:
 *
 *   mapa[]  la zona partida en ocho: 1 si en ese trozo hay ALGO distinto
 *           de 0xFF (borrado). Va por muestreo -no se leen 1 MB por el
 *           bus a mano, que serian segundos- pero lee el principio de
 *           cada bloque de 4 kB, que es justo donde caeria el principio
 *           de cualquier cosa guardada con un minimo de orden.
 *   cab[]   los ocho primeros bytes de la zona. Si ahi estuviera una
 *           imagen de firmware, los cuatro primeros son el puntero de
 *           pila inicial y los cuatro siguientes el vector de arranque,
 *           que se reconocen a simple vista - y ademas se pueden
 *           comparar byte a byte con el firmware.bin que tenemos.
 *   firma   donde aparece la firma de update4.bin (8f 25 c8 65 59 9c
 *           55 31), que es el final de cada imagen. Si esta, ahi hay
 *           una copia y esa zona es intocable.
 */
typedef struct {
    uint8_t  jedec[3];      /* fabricante, tipo, codigo de capacidad */
    uint32_t bytes_dice;    /* capacidad SEGUN el codigo JEDEC */
    uint32_t bytes_mide;    /* capacidad MEDIDA por alias; 0 = no se pudo */
    uint32_t fs_bytes;      /* lo que ocupa el sistema de ficheros, de su BPB */
    uint32_t base, tope;    /* la zona mirada (solo valen si se midio todo) */
    uint8_t  mapa[8];       /* 1 = ese octavo de la zona tiene datos */
    uint8_t  cab[8];        /* los 8 primeros bytes de `base` */
    uint8_t  firma_hay;
    uint32_t firma_addr;    /* solo vale si firma_hay */
    uint32_t bloques_con_datos;
    uint32_t bloques_mirados;
} spi_flash_alta_t;

/* Tarda del orden de una decima de segundo por MB (lee unos 40 bytes de
 * cada bloque de 4 kB por el bus a mano). Llamarla una vez y quedarse con
 * el resultado, no por fotograma. Completamente de solo lectura. */
void spi_flash_mira_alta(spi_flash_alta_t *out);

/* `data` must stay valid and UNCHANGED until spi_flash_async_save_poll()
 * reports DONE or ERROR - it is not copied at start time, only
 * referenced. Returns 1 if the async save actually started (poll from
 * here on), 0 if the fast-path preconditions weren't met (nothing
 * started - fall back to spi_flash_write_or_update_file()). */
uint8_t spi_flash_async_save_start(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len);

/* Call once per main loop iteration while a save is in progress. */
spi_flash_async_status_t spi_flash_async_save_poll(void);

/* Diagnostic bring-up probe #3 - walks CHANNEL.CSV's FAT12 cluster
 * chain and prints its content. See spi_flash_probe_channel_csv()'s
 * comment in spi_flash.c for why this specific file (found by
 * spi_flash_probe_root_dir() on real hardware, 17/08/2026) is worth
 * reading. Same DEBUG_UART_ENABLED requirement, same read-only
 * guarantee. */
void spi_flash_probe_channel_csv(void);


/*
 * --- ANADIR UN TROZO A UN FICHERO DEL PENDRIVE - 24/09/2026 -----------
 *
 * Anade 4096 bytes al final de un fichero del volumen FAT, creandolo si
 * no existe. Pensado para probar el camino de escritura a pulsaciones
 * antes de confiarle una imagen: ver el comentario gordo en spi_flash.c.
 *
 * Bloquea del orden de medio segundo. No llamar por fotograma.
 */
typedef enum {
    SPI_ANADE_OK = 0,
    SPI_ANADE_SIN_SITIO,   /* no queda un bloque de 4 kB entero libre */
    SPI_ANADE_DIR_LLENO,   /* el directorio raiz no admite una entrada mas */
    SPI_ANADE_CADENA,      /* la cadena del fichero y su tamano no cuadran */
    SPI_ANADE_TOPE,        /* ya tiene el maximo de trozos */
    SPI_ANADE_RARO,        /* existe pero no lo escribimos nosotros */
    SPI_ANADE_GEOMETRIA,   /* el volumen no tiene una distribucion que sepamos manejar */
} spi_anade_r_t;

/* `trozo` tiene que apuntar a 4096 bytes. Devuelve 1 si se anadio. */
uint8_t spi_flash_anade_trozo(const char name8[8], const char ext3[3],
                              const uint8_t *trozo, spi_anade_r_t *porque);

const char *spi_flash_anade_porque_txt(spi_anade_r_t r);

/*
 * Si la distribucion del volumen es una que este driver sabe manejar, y si
 * no, cual de las comprobaciones falla. Se lee del sector de arranque en
 * cada llamada, no se cachea: formatear el pendrive desde el PC no pasa por
 * aqui, asi que un valor guardado describiria un volumen que ya no existe.
 *
 * TODO camino que escribe empieza preguntando esto. Ver geo_lee().
 */
uint8_t     spi_flash_puede_escribir(void);
const char *spi_flash_geo_txt(void);

/* Cuanto mide un fichero del volumen (0 si no existe). Mira la entrada de
 * directorio, no la cadena: un fichero escrito a trozos no es contiguo. */
uint8_t spi_flash_fichero_tam(const char name8[8], const char ext3[3], uint32_t *tam);

/* Clusters de la cadena de un fichero y cuantas veces salta HACIA ATRAS
 * (o sea, cuanto esta ocupando huecos que dejaron otros). Recorre la FAT
 * entera: ~50 ms. Llamar al grabar y guardarse el resultado, nunca al
 * pintar. Devuelve 0 si no esta o si la cadena no tiene sentido. */
uint8_t spi_flash_fichero_cadena(const char name8[8], const char ext3[3],
                                 uint32_t *clusters, uint32_t *saltos);

/* El numero mas alto entre los <pre><NNN>.<ext> que haya en el directorio
 * raiz (0 si ninguno). Una sola lectura, no mil busquedas. */
uint32_t spi_flash_dir_max_indice(const char pre[4], const char ext3[3]);

/* Donde empieza en el chip el primer cluster de un fichero. */
uint8_t spi_flash_fichero_cabeza(const char name8[8], const char ext3[3],
                                 uint32_t *addr);

/*
 * Reserva un fichero ENTERO de una vez: busca un hueco seguido de
 * `bytes` (multiplo de 4096), escribe su cadena en las dos copias de la
 * FAT y crea la entrada de directorio ya con el tamaño definitivo.
 * Devuelve en *addr donde empiezan sus datos en el chip, para poder
 * escribir cada bloque con spi_flash_write_block_4k() sin volver a tocar
 * la FAT.
 *
 * Es lo que hace viable guardar fotos: un añadido a trozos cuesta 1205 ms
 * por bloque y asi son unos 130. Ver el comentario gordo en spi_flash.c.
 *
 * Puede fallar por falta de hueco SEGUIDO aunque sobre sitio.
 */
uint8_t spi_flash_reserva(const char name8[8], const char ext3[3],
                          uint32_t bytes, uint32_t *addr, spi_anade_r_t *porque);

/* Recorta un fichero reservado a lo que de verdad se uso, devolviendo los
 * bloques de sobra al volumen. Se redondea hacia arriba a bloque de 4 kB. */
uint8_t spi_flash_recorta(const char name8[8], const char ext3[3], uint32_t bytes);

/* Reescribe los primeros `len` bytes de un fichero (<= 4096). */
uint8_t spi_flash_fichero_cabecera_pon(const char name8[8], const char ext3[3],
                                       const uint8_t *datos, uint32_t len);

#endif /* SPI_FLASH_H */

/*
 * --- VOLCAR LA ZONA ALTA A UN FICHERO QUE YA EXISTE - 24/09/2026 -------
 *
 * PARA QUE. Por encima del sistema de ficheros hay medio megabyte de algo
 * que nadie sabe que es (ver spi_flash_mira_alta()). Para averiguarlo hay
 * que sacarlo del chip, y el unico camino de salida que tiene esta radio
 * es el modo USB, que enseña el sistema de ficheros de abajo.
 *
 * POR QUE NO SE CREA EL FICHERO AQUI. Porque un fichero de medio megabyte
 * son mil clusters encadenados, y encadenar clusters a mano es justo lo
 * que avisa de no hacer la cabecera de este fichero. No hace falta: el
 * fichero lo crea WINDOWS, copiando a la unidad uno lleno de ceros del
 * tamaño que sea. Windows lleva cuarenta años construyendo esa cadena.
 *
 * LO QUE HACE ESTO ES SOLO RELLENARLO. Misma longitud, misma cadena,
 * misma entrada de directorio: no se toca ni la FAT ni el directorio, y
 * lo unico que se programa son bloques que YA son de ese fichero. La
 * clase de fallo que da miedo -romper el volumen del que arranca la
 * actualizacion- desaparece entera, porque no se escribe una sola
 * estructura del sistema de ficheros.
 *
 * VA POR PASOS. Medio megabyte por un bus a mano, con un borrado y una
 * programacion por cada 4 kB, es cosa de medio minuto largo. Hacerlo de
 * una sentada dejaria la radio muda y la pantalla congelada todo ese
 * rato; cada paso mueve un bloque y vuelve, asi que el bucle principal
 * sigue vivo y se puede pintar cuanto lleva.
 */

/* Prepara el volcado de `len` bytes desde `origen` (direccion del chip)
 * al fichero name8/ext3. Devuelve 0 -y no escribe nada- si el fichero no
 * existe, si es mas pequeño que `len`, si sus clusters no son seguidos
 * (recopialo y Windows lo dejara seguido en un volumen casi vacio), o si
 * el origen se solapa con el destino. */
uint8_t spi_flash_volcado_abre(const char name8[8], const char ext3[3],
                               uint32_t origen, uint32_t len);

/* Un bloque de 4 kB por llamada. 1 = queda trabajo, 0 = terminado,
 * -1 = no habia volcado abierto. */
int8_t spi_flash_volcado_paso(void);

uint32_t spi_flash_volcado_hechos(void);   /* bytes ya escritos */
uint32_t spi_flash_volcado_total(void);    /* 0 = no hay volcado en curso */

/* Por que no se pudo abrir el volcado. Ver el comentario de s_vol_porque
 * en spi_flash.c: un "no" que no dice de que es un "no" manda a mirar al
 * sitio equivocado. */
#define SPI_VOL_NADA      0U
#define SPI_VOL_OK        1U
#define SPI_VOL_ZONA      2U   /* no hay nada por encima del sistema de ficheros */
#define SPI_VOL_NO_ESTA   3U   /* el fichero no aparece en el directorio raiz */
#define SPI_VOL_VACIO     4U   /* esta, pero mide 0 bytes */
#define SPI_VOL_TROCEADO  5U   /* sus clusters no son seguidos */
#define SPI_VOL_SOLAPA    6U   /* lo que se lee y lo que se escribe se pisan */
#define SPI_VOL_DESTINO   7U   /* el destino cae fuera de lo permitido */
/*
 * 28/09/2026. "Troceado" y "a medias" eran el MISMO codigo, y desde que
 * el camino de restaurar admite ficheros troceados ya no lo son: uno es
 * ahora normal y el otro sigue siendo un fichero roto. Juntarlos haria
 * que la pantalla dijera "esta troceado" -que es verdad y da igual-
 * cuando lo que pasa es que la cadena se acaba antes que el fichero.
 */
#define SPI_VOL_CORTO     8U   /* la cadena se acaba antes que el tamaño */

uint8_t     spi_flash_volcado_porque(void);
const char *spi_flash_volcado_porque_txt(void);

/*
 * Igual, pero leyendo de MEMORIA. Para sacar la flash interna del micro
 * (0x08000000): esta mapeada, asi que basta un puntero - y funciona
 * aunque la proteccion de lectura cierre el paso por SWD, porque esa
 * proteccion frena al depurador, no al codigo que ya corre dentro.
 */
uint8_t spi_flash_volcado_abre_mem(const char name8[8], const char ext3[3],
                                   const uint8_t *origen, uint32_t len);

/*
 * --- EL CAMINO DE VUELTA: de un fichero del volumen al chip -----------
 *
 * El volcado saca datos del chip a un fichero. Esto los mete: lee un
 * fichero QUE YA EXISTE en el volumen FAT y lo escribe en una direccion
 * del chip.
 *
 * PARA QUE. Para deshacer. El dia que se use la zona de la fuente china
 * para guardar imagenes, la unica forma de volver atras seria un
 * programador y un destornillador. Con esto, es copiar el fichero de
 * respaldo al disco USB y tocar un boton.
 *
 * DOS COSAS QUE NO PUEDE HACER, Y CONVIENE SABERLAS:
 *
 * 1. NO escribe por debajo del sistema de ficheros. Esa guardia es lo
 *    que hace que esto sea una herramienta y no un arma. Y ademas es el
 *    unico caso imposible de verdad: restaurar el volumen FAT desde un
 *    fichero guardado EN ese mismo volumen no puede funcionar, porque se
 *    estaria destruyendo el fichero del que se lee segun se lee.
 *
 * 2. NO escribe en la flash INTERNA del micro. Esto es la flash SPI. Meter
 *    un gestor de arranque en 0x08000000 es otra cosa y mucho mas
 *    peligrosa: habria que ejecutar el programador desde RAM mientras se
 *    borra el sector desde el que se estaba ejecutando, y un fallo ahi no
 *    tiene vuelta atras desde dentro.
 *
 * `suelo` y `tope` acotan lo permitido: normalmente, desde donde acaba el
 * sistema de ficheros hasta donde acaba el chip, los dos MEDIDOS (ver
 * spi_flash_mira_alta()). Si el fichero es mas pequeño que `len`, se
 * escribe lo que haya.
 *
 * Avanza con el mismo spi_flash_volcado_paso() que el volcado - comparten
 * el cuidado de no llevarse por delante lo que comparta bloque.
 */
uint8_t spi_flash_restaura_abre(const char name8[8], const char ext3[3],
                                uint32_t destino, uint32_t len,
                                uint32_t suelo, uint32_t tope);

/*
 * Borrar un tramo del chip, a pasos. Mismas guardias que restaurar: nunca
 * por debajo del sistema de ficheros, nunca por encima del chip, y solo
 * bloques enteros de 4 kB.
 *
 * Existe por una pregunta concreta que no se puede contestar de otra
 * manera: ¿lee alguien la fuente china de la flash SPI? Demostrar que
 * NADIE LEE algo es imposible desde dentro. Quitarla y mirar, no.
 */
uint8_t spi_flash_borra_abre(uint32_t desde, uint32_t len,
                             uint32_t suelo, uint32_t tope);

/* Si un fichero esta en el disco. */
uint8_t spi_flash_fichero_hay(const char name8[8], const char ext3[3]);

/* Borra un fichero del disco: marca su entrada del directorio y suelta
 * sus clusters en las dos copias de la FAT. Devuelve 0 si no estaba, si
 * el volumen no se entiende o si el borrador comun estaba cogido. La
 * cadena se sigue por la FAT -no se supone que los clusters vayan
 * seguidos-, porque estos ficheros los ha escrito un PC. */
uint8_t spi_flash_fichero_borra(const char name8[8], const char ext3[3]);

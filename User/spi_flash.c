#include "spi_flash.h"
#include "idioma.h"
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

/* Tope de FAT que sabemos cargar de una vez. Una FAT12 de 4096 bytes
 * describe hasta 2730 clusters, de sobra para un volumen de 1 MB con
 * sectores de 512. Si algun dia hace falta mas, esto salta en geo_lee().
 * Vivia mas abajo, junto al resto de la geometria; subio aqui el 24/09/2026
 * porque el borrador s_fat[] se declara antes y lo necesita. */
#define FAT_BYTES_MAX       4096U

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
#ifdef CARGADOR_ARRANQUE
/*
 * EN EL CARGADOR, ESTE BUS VA POR HARDWARE - 01/10/2026.
 *
 * *** El dueno: "el usb sigue sin funcionar" ... "incluso creo que aparece
 * la unidad por un instante, pero luego nada". ***
 *
 * Y la causa es esta funcion. A pelo por las patas son unos 2 us por bit,
 * o sea 16 us por byte: **un sector de 512 bytes tarda 8,2 ms**. La clase
 * de almacenamiento del USB llama a disco_lee() desde la INTERRUPCION del
 * USB, asi que durante esos 8,2 ms el USB no atiende a nada. Y Windows no
 * lee de sector en sector: pide bloques de 8 a 64, o sea entre 65 y 500 ms
 * dentro de la interrupcion. El host da el dispositivo por muerto y lo
 * suelta. "Aparece un instante y luego nada" es justo eso.
 *
 * El de serie no tiene el problema porque usa el SPI por hardware: en
 * 0x08001E56 configura PB3, PB4 y PB5 en funcion alternativa 5, que es
 * SPI0. Son exactamente nuestros tres pines.
 *
 * Aqui se hace igual, y SOLO en el cargador. La aplicacion se queda con el
 * bus a patadas porque alli lo comparte con el tactil (ver la cabecera de
 * este fichero), y eso es lo que no se puede romper. El cargador no usa el
 * tactil, asi que no hay nada que compartir.
 *
 * SPI0 cuelga del APB2, que con el reloj del cargador a 192 MHz son 96.
 * Con divisor 8 salen 12 MHz: 24 veces mas rapido que a patadas, y muy por
 * debajo de los 50 MHz que admite el W25Q16 leyendo con el comando 0x03.
 * Un sector pasa de 8,2 ms a 0,34.
 */
static uint8_t spi_xfer_byte(uint8_t out)
{
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE)) { }
    spi_i2s_data_transmit(SPI0, (uint16_t)out);
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE)) { }
    return (uint8_t)spi_i2s_data_receive(SPI0);
}
#else
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
#endif

void spi_flash_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);

    /* SCLK/MOSI/MISO: same mode/speed touch_init() already configures
     * these pins with - see this file's header comment for why
     * reconfiguring them again here is harmless. */
#ifdef CARGADOR_ARRANQUE
    /* Los mismos tres pines, pero en alternativa 5 = SPI0, igual que el
     * cargador de serie (0x08001E56 del boot.bin). Ver spi_xfer_byte(). */
    {
        uint32_t pines = F_SCLK_PIN | F_MISO_PIN | F_MOSI_PIN;
        spi_parameter_struct cfg;

        rcu_periph_clock_enable(RCU_SPI0);
        gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, pines);
        gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pines);
        gpio_af_set(GPIOB, GPIO_AF_5, pines);

        spi_i2s_deinit(SPI0);
        spi_struct_para_init(&cfg);
        cfg.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
        cfg.device_mode          = SPI_MASTER;
        cfg.frame_size           = SPI_FRAMESIZE_8BIT;
        /* Modo 0: reposo en bajo, muestreo en el primer flanco. Es el que
         * imitaba el bit-bang y el que quiere el W25Q16. */
        cfg.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
        cfg.nss                  = SPI_NSS_SOFT;   /* el CS es PB6, a mano */
        cfg.prescale             = SPI_PSC_8;      /* 96/8 = 12 MHz */
        cfg.endian               = SPI_ENDIAN_MSB;
        spi_init(SPI0, &cfg);
        spi_enable(SPI0);
    }
#else
    gpio_mode_set(F_SCLK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_SCLK_PIN);
    gpio_output_options_set(F_SCLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_SCLK_PIN);
    gpio_mode_set(F_MOSI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_MOSI_PIN);
    gpio_output_options_set(F_MOSI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_MOSI_PIN);
    gpio_mode_set(F_MISO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, F_MISO_PIN);
#endif

    /* This chip's OWN chip-select (PB6) - deselect FIRST, before
     * anything else on this shared bus, same defensive ordering
     * touch_init() uses for its own CS. */
    gpio_mode_set(F_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, F_CS_PIN);
    gpio_output_options_set(F_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, F_CS_PIN);
    f_cs(1);
#ifndef CARGADOR_ARRANQUE
    f_clk(0);
    f_mosi(0);
#endif

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

/* =====================================================================
 * LOS DOS BORRADORES DE 4 kB. 24/09/2026.
 * =====================================================================
 * Aqui habia SIETE buffers de 4 kB declarados dentro de funciones, o
 * sea en la PILA. El comentario que los justificaba decia que la pila
 * vive en los 64 kB del TCM y que ahi un buffer de 4 kB no molesta.
 *
 * La primera mitad era verdad. La segunda no. De esos 64 kB, el DSP y
 * el dibujo tienen apartados 61.076 bytes en variables fijas; para
 * pila quedan 4.456. Y estos buffers se anidan: al guardar los ajustes
 * la cadena real era
 *
 *   main -> tune_encoder_poll -> ajuste_accion -> settings_save_now
 *        -> spi_flash_write_or_update_file
 *        -> write_file_data_and_entry            (4.136 de marco)
 *        -> fat_write_chain
 *        -> spi_flash_block_read_modify_write    (4.120 mas)
 *
 * 10.752 bytes de pila con 4.456 disponibles. La pila se salia 6,3 kB
 * por debajo y pisaba las variables del TCM. Sin fallo, sin aviso: se
 * sobrescribia y el programa seguia. Es un candidato serio a explicar
 * el ZONAALTA.BIN que desaparecio sin que nadie lo borrara.
 *
 * COMO SE COLO: mirando el TAMANO de la region (64 kB) en vez de lo que
 * QUEDABA LIBRE en ella (4,3 kB). El mismo error exacto que el de la
 * geometria de la FAT: dar por supuesto en lugar de medir.
 *
 * LA SOLUCION. Dos borradores estaticos, uno por cada papel distinto:
 *
 *   s_sector[]  la imagen de un sector de 4 kB de la flash, para leer,
 *               modificar y volver a escribir.
 *   s_fat[]     la copia en memoria de la tabla de asignacion.
 *
 * Dos y no uno porque se anidan de verdad: reserva() tiene la FAT
 * cargada mientras llama a lo que usa el sector. Y dos y no siete
 * porque los del mismo papel NUNCA estan vivos a la vez - eso ya no es
 * una promesa, se comprueba de dos maneras:
 *
 *   1. En cada compilacion. "make comprueba" calcula el peor caso de
 *      pila del binario entero recorriendo el grafo de llamadas, y
 *      falla si no cabe. Ver tools/pila.py.
 *   2. En marcha. Cada borrador lleva un cerrojo; si alguna vez alguien
 *      lo pide estando cogido, la operacion se aborta en vez de
 *      corromper, y el contador de choques sale en la ventana de
 *      informacion. Deberia ser cero para siempre.
 *
 * COSTE: 8.192 bytes de .bss. Libres en la SRAM principal quedan
 * 11.596, asi que caben. No es gratis y es el precio correcto: 8 kB de
 * memoria fija a cambio de dejar de pisar la pila.
 */
#define BORRADOR_SECTOR  0U
#define BORRADOR_FAT     1U

static uint8_t  s_sector[FLASH_SECTOR_SIZE];
static uint8_t  s_fat[FAT_BYTES_MAX];
static uint8_t  s_borrador_cogido[2];
static uint16_t s_borrador_choques;

/* Devuelve 0 si el borrador ya estaba cogido: el que llama TIENE que
 * abortar, no seguir. Cada pareja coge/suelta va en la misma funcion. */
static uint8_t borrador_coge(uint8_t cual)
{
    if (s_borrador_cogido[cual]) {
        if (s_borrador_choques < 0xFFFFU) {
            s_borrador_choques++;
        }
        return 0U;
    }
    s_borrador_cogido[cual] = 1U;
    return 1U;
}

static void borrador_suelta(uint8_t cual)
{
    s_borrador_cogido[cual] = 0U;
}

/* Para la ventana de informacion: cuantas veces se pidio un borrador ya
 * cogido desde el arranque. Si esto no es 0, hay un camino nuevo que
 * anida dos usos del mismo borrador y hay que mirarlo. */
uint16_t spi_flash_choques_borrador(void)
{
    return s_borrador_choques;
}

/*
 * ESCRIBE LOS DATOS DE UN FICHERO SIN TOCAR A LOS VECINOS - 30/09/2026.
 *
 * Aqui estaban los dos fallos mas serios que ha tenido este fichero, y los
 * dos salian de la misma linea: rellenar 4 kB de 0xFF, copiar encima los
 * datos y llamar a spi_flash_write_block_4k().
 *
 * 1. SE LLEVABA POR DELANTE A LOS VECINOS. Un bloque son 4 kB, o sea OCHO
 *    clusters de 512. CONFIG.CSV ocupa tres. Los otros cinco se borraban y
 *    se reprogramaban a 0xFF en cada guardado, aunque la FAT dijera que
 *    eran de otro fichero. Reproducido con CHANNEL.CSV, que viene de
 *    fabrica en el volumen: sus 1024 bytes pasaban a 0xFF tras un solo
 *    guardado, y el guardado informaba de que habia ido bien.
 *
 *    La cabecera decia "the whole confirmed-free block becomes this file's
 *    data", y eso solo es cierto en el instante de crearlo: la FAT marca
 *    los clusters USADOS, asi que nada impide que otro tome los de al lado
 *    despues -por ejemplo copiando un fichero desde el PC por USB-.
 *    icao24_db.c ya se protegia con un relleno de 4 kB, pero era un apaño
 *    local a ese fichero.
 *
 * 2. SI EL FICHERO NO EMPEZABA EN FRONTERA DE BLOQUE, ESCRIBIA EN OTRO
 *    SITIO. spi_flash_write_block_4k() hace `& ~4095`, asi que los datos
 *    iban al PRINCIPIO del bloque y los clusters de verdad del fichero se
 *    quedaban a 0xFF. Pasa siempre que (cluster-2) % 8 != 0, o sea con
 *    probabilidad 7/8 en cuanto alguien edita o restaura CONFIG.CSV desde
 *    el PC. Y el sintoma es el peor: cada guardado dice que ha ido bien y
 *    cada arranque lee 0xFF. Ajustes perdidos en silencio.
 *
 * Esto escribe en la direccion DE VERDAD, conserva lo que haya alrededor
 * dentro de cada bloque de 4 kB, y parte el trabajo si el fichero cruza una
 * frontera. Cuesta una lectura de 4 kB mas por bloque; el borrado, que es
 * lo que de verdad tarda, ya estaba.
 */
static void escribe_datos_fichero(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len > 0U) {
        uint32_t off = addr & (uint32_t)(FLASH_SECTOR_SIZE - 1U);
        uint32_t trozo = (uint32_t)FLASH_SECTOR_SIZE - off;

        if (trozo > len) { trozo = len; }
        spi_flash_block_read_modify_write(addr, off, data, trozo);
        addr += trozo;
        data += trozo;
        len  -= trozo;
    }
}

void spi_flash_block_read_modify_write(uint32_t any_addr_in_block,
                                        uint32_t modify_offset_in_block,
                                        const uint8_t *new_bytes,
                                        uint32_t new_len)
{
    /* Ver el comentario gordo de arriba: esto era un uint8_t de 4 kB en
     * la pila y por eso la pila se salia. Ahora es el borrador comun. */
    uint32_t block_addr = any_addr_in_block & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    uint32_t i;

    if (!borrador_coge(BORRADOR_SECTOR)) {
        return;
    }
    spi_flash_read(block_addr, s_sector, FLASH_SECTOR_SIZE);
    for (i = 0U; i < new_len; i++) {
        s_sector[modify_offset_in_block + i] = new_bytes[i];
    }
    spi_flash_write_block_4k(block_addr, s_sector);
    borrador_suelta(BORRADOR_SECTOR);
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
/*
 * ======================================================================
 * LA GEOMETRIA DEL VOLUMEN SE LEE, NO SE SUPONE - 24/09/2026
 * ======================================================================
 *
 * *** El dueño del proyecto, despues de que Windows le dijera que el disco
 * D: "no tiene formato": "formatearlo seria como borrar el 1mb bajo?" ***
 *
 * Estas cinco cifras estaban escritas a fuego, deducidas del hardware hace
 * meses. Y eran correctas... para el volumen que traia la radio de
 * fabrica. En cuanto alguien formatea el pendrive desde un PC, el sistema
 * operativo elige SU distribucion -para un volumen de 1 MB lo normal es 1
 * sector reservado en vez de 4- y entonces el codigo sigue escribiendo
 * donde dicen las constantes, que ya no es donde esta la FAT.
 *
 * Eso no avisa. No falla al formatear: falla la primera vez que la radio
 * guarda algo, y lo que se lleva por delante es el sector de arranque del
 * volumen por donde entra el firmware.
 *
 * Es el mismo fallo que llevo todo el dia cazando en otros sitios -dar algo
 * por supuesto en vez de medirlo- y lo tenia en el sitio mas peligroso de
 * todos sin verlo. FatFs, que es lo que usa el gestor de arranque, lleva
 * haciendolo bien desde siempre: lee el sector de arranque.
 *
 * ASI QUE AHORA SE LEE. Y sobre todo: SE COMPRUEBA. Este driver no sabe
 * manejar cualquier FAT12 -da por hecho sectores de 512 bytes, un sector
 * por cluster (de ahi que un bloque de borrado sean 8 clusters justos) y
 * dos copias de la FAT- asi que lo que hace es mirar si la que hay es una
 * de las que sabe. Si lo es, escribe. Si no, NO ESCRIBE y lo dice.
 *
 * Un driver que se niega es reparable. Uno que corrompe en silencio, no.
 */

/* FAT_BYTES_MAX esta arriba, junto a FLASH_SECTOR_SIZE: lo necesita el
 * borrador s_fat[], que se declara antes que esto. */
#define SECTOR_BYTES        512U    /* lo unico que este driver sabe manejar */

typedef struct {
    uint8_t  vale;        /* 1 = leida y de las que sabemos manejar */
    uint16_t bps;         /* bytes por sector */
    uint8_t  spc;         /* sectores por cluster */
    uint16_t res;         /* sectores reservados = LBA de la primera FAT */
    uint8_t  nfat;        /* cuantas copias de la FAT */
    uint16_t fat_sec;     /* sectores que ocupa cada copia */
    uint16_t raiz_ent;    /* entradas del directorio raiz */
    uint32_t sectores;    /* sectores totales del volumen */
    uint32_t raiz_lba;
    uint32_t datos_lba;
    uint32_t clusters;
    uint8_t  porque;      /* si no vale, cual de las comprobaciones fallo */
} fat_geo_t;

static fat_geo_t s_geo;

static uint8_t geo_lee(void);

/* ---------------------------------------------------------------------
 * LA GEOMETRIA, IMPOSIBLE DE USAR SIN HABERLA LEIDO. 25/09/2026.
 * ---------------------------------------------------------------------
 * Ayer se metio geo_lee() -leer la distribucion real del volumen en vez de
 * darla por supuesta- en los tres caminos NUEVOS: anadir trozo, reservar y
 * recortar. Y se quedaron fuera los que ya estaban, entre ellos
 * spi_flash_write_or_update_file(), que es por donde se guardan LOS
 * AJUSTES.
 *
 * Lo que pasaba entonces: s_geo todo a ceros, DATA_CLUSTER_COUNT = 0, el
 * driver creia que el volumen no tiene ni un cluster libre y abortaba con
 * "no free block". En silencio. O sea que CONFIG.CSV no se guardaba nunca,
 * salvo que antes se hubiera entrado en alguna pantalla que si leyera la
 * geometria -y entonces funcionaba hasta apagar-. El dueno lo noto como
 * "siempre que arranco me pone rapida y tengo que cambiarla", y llevaba asi
 * desde que se introdujo geo_lee().
 *
 * EL ARREGLO NO ES ANADIR LA LLAMADA EN VEINTE SITIOS: eso es exactamente
 * el error que se acaba de cometer, y el siguiente que anada una funcion lo
 * repetira. Los macros de geometria pasan a leer por AQUI, y aqui se lee el
 * volumen si todavia no se ha leido. Asi no hay forma de usar la geometria
 * sin tenerla.
 *
 * Los caminos de escritura siguen llamando a geo_lee() ellos mismos al
 * entrar, y eso NO sobra: esto la lee si falta, pero ellos la vuelven a
 * leer SIEMPRE, que es lo que hace falta cuando el volumen puede haber
 * cambiado por USB entre una operacion y la siguiente. Y ademas es lo que
 * permite fallar con un motivo claro cuando la distribucion no es de las
 * que sabemos manejar, en vez de seguir con ceros.
 */
static const fat_geo_t *geo(void)
{
    if (!s_geo.vale) {
        (void)geo_lee();
    }
    return &s_geo;
}

/*
 * Por que se ha rechazado una geometria. Sale a la pantalla de Informacion,
 * asi que hay que poder distinguirlas: "no escribo" sin decir por que es lo
 * mismo que no decir nada.
 */
enum {
    GEO_OK = 0,
    GEO_SIN_LEER,     /* todavia no se ha mirado */
    GEO_NO_ES_FAT,    /* el sector 0 no tiene pinta de sector de arranque */
    GEO_SECTOR,       /* sectores que no son de 512 bytes */
    GEO_CLUSTER,      /* mas de un sector por cluster */
    GEO_COPIAS,       /* no son dos copias de la FAT */
    GEO_FAT_GRANDE,   /* la FAT no cabe en el buffer de pila */
    GEO_DESALINEADO   /* los datos no empiezan en frontera de bloque de borrado */
};

/* Las constantes de antes, ahora mirando a lo que se leyo. Se mantienen los
 * mismos nombres a proposito: asi el resto del fichero no cambia ni una
 * linea, y lo unico que se mueve es de donde salen los numeros. */
#define FAT1_LBA            (geo()->res)
#define FAT_SECTORS         (geo()->fat_sec)
#define FAT_BYTES           ((uint32_t)geo()->fat_sec * geo()->bps)
#define DATA_START_SECTOR   (geo()->datos_lba)
#define DATA_CLUSTER_COUNT  (geo()->clusters)
#define CLUSTERS_PER_BLOCK  (FLASH_SECTOR_SIZE / SECTOR_BYTES) /* 8 - exige 1 sector/cluster, comprobado en geo_lee() */

/* El directorio raiz tambien sale del sector de arranque - ver geo_lee(). */
#define ROOT_DIR_LBA           (geo()->raiz_lba)
/* Se queda FIJO en 512: dimensiona buffers de pila y geo_lee() se niega a
 * trabajar con cualquier otro tamaño de sector. */
#define ROOT_DIR_SECTOR_BYTES  SECTOR_BYTES

uint8_t spi_flash_geometria(uint32_t *fat1_lba, uint32_t *raiz_lba,
                            uint32_t *datos_lba, uint32_t *clusters)
{
    const fat_geo_t *g = geo();

    if (g == (const fat_geo_t *)0 || !g->vale) { return 0U; }
    if (fat1_lba)  { *fat1_lba  = g->res; }
    if (raiz_lba)  { *raiz_lba  = g->raiz_lba; }
    if (datos_lba) { *datos_lba = g->datos_lba; }
    if (clusters)  { *clusters  = g->clusters; }
    return 1U;
}


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


/*
 * Lee el sector de arranque y decide si esta es una FAT12 de las que este
 * driver sabe manejar.
 *
 * Las cuatro exigencias no son capricho, cada una sujeta codigo concreto:
 *
 *   sectores de 512      dimensiona todos los buffers de pila del fichero
 *   1 sector por cluster CLUSTERS_PER_BLOCK vale 8 por eso; con 2 o 4, un
 *                        bloque de borrado dejaria de ser un numero entero
 *                        de clusters y todo el reparto se cae
 *   2 copias de la FAT   fat_vuelca() escribe exactamente dos
 *   datos alineados a    spi_flash_reserva() entrega direcciones de bloque
 *   4 kB                 de borrado; si el area de datos no empieza en una
 *                        frontera, un "bloque" del fichero caeria a caballo
 *                        de dos y borrar uno se llevaria al vecino
 *
 * Se relee cada vez que se pide en vez de cachear: formatear el pendrive
 * desde el PC no pasa por aqui, asi que un valor cacheado se quedaria
 * describiendo un volumen que ya no existe. Cuesta una lectura de 512
 * bytes, unos 8 ms por este bus.
 */
static uint8_t geo_lee(void)
{
    uint8_t  bs[64];
    uint32_t datos_ent;

    s_geo.vale = 0U;
    s_geo.porque = GEO_NO_ES_FAT;

    spi_flash_read(0UL, bs, sizeof bs);

    s_geo.bps      = (uint16_t)((uint16_t)bs[11] | ((uint16_t)bs[12] << 8));
    s_geo.spc      = bs[13];
    s_geo.res      = (uint16_t)((uint16_t)bs[14] | ((uint16_t)bs[15] << 8));
    s_geo.nfat     = bs[16];
    s_geo.raiz_ent = (uint16_t)((uint16_t)bs[17] | ((uint16_t)bs[18] << 8));
    s_geo.fat_sec  = (uint16_t)((uint16_t)bs[22] | ((uint16_t)bs[23] << 8));
    s_geo.sectores = (uint32_t)bs[19] | ((uint32_t)bs[20] << 8);
    if (s_geo.sectores == 0UL) {
        s_geo.sectores = (uint32_t)bs[32] | ((uint32_t)bs[33] << 8)
                       | ((uint32_t)bs[34] << 16) | ((uint32_t)bs[35] << 24);
    }

    /* Lo minimo para que esto sea un sector de arranque y no ruido. */
    if (s_geo.bps == 0U || s_geo.spc == 0U || s_geo.res == 0U ||
        s_geo.nfat == 0U || s_geo.fat_sec == 0U || s_geo.sectores == 0UL) {
        return 0U;
    }
    if (s_geo.sectores > (1UL << 24)) { return 0U; }

    /* Y ahora, si es una de las que sabemos. */
    if (s_geo.bps != SECTOR_BYTES)          { s_geo.porque = GEO_SECTOR;   return 0U; }
    if (s_geo.spc != 1U)                    { s_geo.porque = GEO_CLUSTER;  return 0U; }
    if (s_geo.nfat != 2U)                   { s_geo.porque = GEO_COPIAS;   return 0U; }
    if (((uint32_t)s_geo.fat_sec * s_geo.bps) > FAT_BYTES_MAX) {
        s_geo.porque = GEO_FAT_GRANDE;      return 0U;
    }

    s_geo.raiz_lba  = (uint32_t)s_geo.res + ((uint32_t)s_geo.nfat * s_geo.fat_sec);
    datos_ent       = ((uint32_t)s_geo.raiz_ent * 32UL + s_geo.bps - 1UL) / s_geo.bps;
    s_geo.datos_lba = s_geo.raiz_lba + datos_ent;
    if (s_geo.datos_lba >= s_geo.sectores) { return 0U; }
    s_geo.clusters  = (s_geo.sectores - s_geo.datos_lba) / s_geo.spc;

    if (((s_geo.datos_lba * s_geo.bps) & (FLASH_SECTOR_SIZE - 1U)) != 0U) {
        s_geo.porque = GEO_DESALINEADO;     return 0U;
    }

    s_geo.porque = GEO_OK;
    s_geo.vale = 1U;
    return 1U;
}

const char *spi_flash_geo_txt(void)
{
    (void)geo_lee();
    switch (s_geo.porque) {
    case GEO_OK:          return tr("se puede escribir", "writable");
    case GEO_NO_ES_FAT:   return tr("no es un FAT12", "not a FAT12");
    case GEO_SECTOR:      return tr("sector no es de 512", "sector is not 512");
    case GEO_CLUSTER:     return tr("cluster de varios sec", "multi-sector cluster");
    case GEO_COPIAS:      return tr("no hay 2 copias FAT", "no 2 FAT copies");
    case GEO_FAT_GRANDE:  return tr("FAT demasiado grande", "FAT too large");
    case GEO_DESALINEADO: return tr("datos sin alinear", "data not aligned");
    default:              return tr("sin mirar", "not checked");
    }
}

uint8_t spi_flash_puede_escribir(void)
{
    return geo_lee();
}

/* FAT12 entries are packed 2-per-3-bytes - the standard unpack
 * algorithm, same as every other FAT12 implementation. */
static uint16_t fat12_entry(const uint8_t *fat, uint32_t cluster)
{
    uint32_t off = cluster + (cluster / 2U);
    uint16_t val = (uint16_t)(fat[off] | ((uint16_t)fat[off + 1U] << 8));

    return (cluster & 1U) ? (uint16_t)(val >> 4) : (uint16_t)(val & 0x0FFFU);
}

void spi_flash_probe_fat_scan(spi_flash_fat_scan_t *out);
static void probe_fat_scan_interno(spi_flash_fat_scan_t *out);

void spi_flash_probe_fat_scan(spi_flash_fat_scan_t *out)
{
    if (!borrador_coge(BORRADOR_FAT)) {
        out->found_free_block = 0;
        return;
    }
    probe_fat_scan_interno(out);
    borrador_suelta(BORRADOR_FAT);
}

/* El borrador comun, con el nombre corto que usaba la variable local que
 * habia aqui. Es un #define y no un "uint8_t *fat = s_fat" A PROPOSITO:
 * con el puntero, los sizeof(fat) que hay dentro de la funcion pasan de
 * valer 4096 a valer 4, la FAT se lee entera en 4 bytes, el driver se cree
 * que el volumen esta vacio y escribe encima de lo que haya. Lo hice asi
 * primero y eso es exactamente lo que paso: el banco de FAT lo cazo con
 * "se ha estropeado ZONAALTABIN" y el compilador no dijo ni mu. Con el
 * #define, el nombre sigue siendo un array y no hay nada que recordar. */
#define fat s_fat
static void probe_fat_scan_interno(spi_flash_fat_scan_t *out)
{
    /* El buffer de 4 kB que habia aqui vive ahora en .bss - ver el
     * comentario gordo de los borradores, arriba. La envoltura de
     * debajo es la que coge y suelta el cerrojo, y existe justo para
     * no tener que soltarlo en cada uno de los return de esta
     * funcion, que es donde se olvida uno. */
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
#undef fat

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
            /*
             * *** 24/09/2026: el terminador SOLO si detras no hay nada ***
             *
             * write_file_data_and_entry() escribe 64 bytes cuando
             * *out_needs_terminator vale 1: la entrada de 32 y otros 32 de
             * ceros detras, como marca de fin de directorio. Hasta hoy eso
             * se daba por seguro con el argumento de que una ranura a 0x00
             * o a 0xFF solo puede significar que detras tampoco hay nada.
             *
             * El banco sim/fatanade.c monto un directorio con las ranuras
             * 0..5 ocupadas, la 6 a 0xFF y un fichero real en la 7, y esos
             * 32 ceros se llevaron el fichero de la 7 por delante. El
             * argumento era bueno para los directorios que escribe este
             * driver; no para cualquiera que pueda llegar aqui.
             *
             * Se puede discutir si un volumen llega a esa forma en la vida
             * real. Da igual: el coste de comprobarlo es una lectura de un
             * byte que ya esta en `sector`, y lo que se juega es un fichero
             * del usuario -o el update4.bin, que es el unico camino que hay
             * para meter firmware en esta radio-. A ese precio no se apuesta.
             */
            uint32_t sig = (i + 1U) * 32U;
            uint8_t  libre_detras =
                (sig >= sizeof(sector)) ||
                (sector[sig] == 0x00U) || (sector[sig] == 0xFFU) ||
                (sector[sig] == 0xE5U);

            *out_offset = i * 32U;
            *out_needs_terminator = (uint8_t)(libre_detras ? 1U : 0U);
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

    /* Los datos, en su direccion de verdad y sin tocar a los vecinos: ver
     * el comentario de escribe_datos_fichero(). El borrador lo coge y lo
     * suelta cada paso de ahi dentro, asi que aqui ya no se coge - antes se
     * cogia aqui y se llenaba de 0xFF a mano, que era el fallo. */
    escribe_datos_fichero(scan->free_block_byte_addr, data, len);

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

/* Definida mas abajo; la necesita el camino rapido de
 * spi_flash_write_or_update_file() para no fiarse de la cadena. */
static uint8_t cadena_ultimo(uint32_t primero, uint32_t *ultimo, uint32_t *cuantos,
                             uint32_t *saltos);

int spi_flash_write_or_update_file(const char name8[8], const char ext3[3],
                                    const uint8_t *data, uint32_t len)
{
    uint32_t dir_off;
    uint16_t old_cluster;
    uint32_t old_size;
    uint32_t new_num_clusters;
    spi_flash_fat_scan_t scan;

    /* La geometria, SIEMPRE y de nuevo en cada operacion: el volumen puede
     * haber cambiado por USB desde la anterior. geo() la leeria sola si
     * faltara, pero solo una vez; esto la refresca y ademas permite fallar
     * con un motivo en vez de seguir con lo que hubiera. Ver el comentario
     * de geo(). */
    if (!geo_lee()) {
        debug_print("spi_flash_write_or_update_file: la distribucion del volumen no es de las que sabemos - no se escribe\n");
        return 0;
    }
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

        /* ---------------------------------------------------------
         * ¿SIGUE SIENDO BUENA LA CADENA? 25/09/2026.
         * ---------------------------------------------------------
         * El camino rapido de aqui abajo escribe los datos y la entrada y
         * SE SALTA LA FAT ENTERA, porque da por hecho que la cadena de la
         * vez anterior sigue valiendo. Casi siempre es verdad. Pero si no
         * lo es -un volumen que ha pasado por un PC, un apagon a medias, un
         * formateo raro- pasa justo lo que el dueno reporto: el guardado
         * dice que ha ido bien (encuentra la entrada, escribe los datos,
         * actualiza el tamano) y al arrancar no se lee nada, porque la
         * lectura SI sigue la cadena y la cadena no lleva a ninguna parte.
         * "1 ok, 0 mal" por un lado y "0 claves" por el otro, los dos
         * ciertos.
         *
         * Asi que se comprueba antes de fiarse: la cadena tiene que
         * recorrerse entera y tener exactamente los eslabones que dice la
         * entrada. Si no, el camino rapido NO vale y se reconstruye todo.
         * Cuesta leer la FAT una vez por guardado, que al lado de un
         * borrado de 4 kB no es nada.
         */
        {
            uint32_t ultimo = 0U, cuantos = 0U, saltos = 0U;
            uint8_t  buena = cadena_ultimo((uint32_t)old_cluster, &ultimo, &cuantos, &saltos);

            if (!buena || (cuantos != old_num_clusters)) {
                debug_print("spi_flash_write_or_update_file: la cadena de la entrada no cuadra - se reconstruye en vez de usar el camino rapido\n");
                /* Y NO se libera la vieja: si la cadena no se entiende, no
                 * se sabe que clusters son suyos, y liberar a ciegas es
                 * como se pierde lo de al lado. Se deja donde esta -unos
                 * clusters perdidos, que un chkdsk recupera- y el fichero
                 * se escribe entero en sitio bueno. */
                spi_flash_probe_fat_scan(&scan);
                if (!scan.found_free_block) {
                    debug_print("spi_flash_write_or_update_file: cadena rota y ademas no hay bloque libre - abortando\n");
                    return 0;
                }
                if (!write_file_data_and_entry(dir_off, 0U, 1U, &scan, name8, ext3, data, len)) {
                    return 0;
                }
                debug_print("spi_flash_write_or_update_file: entrada reconstruida con cadena nueva\n");
                return 1;
            }
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
             * small FIXED-SIZE CSV - 1536 bytes, o sea TRES clusters de
             * 512, no uno: hasta el 30/09/2026 aqui ponia "a single
             * 512-byte cluster... (both 1)", y de esa frase salia el dar
             * por hecho que el bloque entero era de este fichero, que es
             * el fallo que se arreglo ese dia (ver
             * escribe_datos_fichero()) - so old_num_clusters ==
             * new_num_clusters on essentially every save - meaning THIS path,
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

/* Aqui dentro habia un "uint8_t fat[FAT_BYTES]": un array de tamano
 * VARIABLE, porque FAT_BYTES sale de la geometria leida del volumen. Eso
 * son hasta 4 kB de pila que gcc marca como "dynamic" en el .su y que
 * tools/pila.py NO puede acotar - o sea, justo el tipo de gasto que la
 * comprobacion de pila no ve venir. Pasa al borrador comun.
 *
 * El #define en vez de un puntero: ver la explicacion en
 * probe_fat_scan_interno. Con puntero, sizeof(fat) valdria 4.
 *
 * Y los sizeof(fat) de dentro pasan a decir FAT_BYTES a proposito: el
 * borrador mide 4096 siempre, pero la FAT de verdad suele ser mas corta, y
 * leer de mas en un bus a 16 us por byte se nota. */
#define fat s_fat
static uint32_t read_file_by_name_interno(const char name8[8], const char ext3[3],
                                          uint8_t *out_buf, uint32_t out_buf_size);

uint32_t spi_flash_read_file_by_name(const char name8[8], const char ext3[3],
                                     uint8_t *out_buf, uint32_t out_buf_size)
{
    uint32_t n;

    if (!borrador_coge(BORRADOR_FAT)) {
        return 0U;
    }
    n = read_file_by_name_interno(name8, ext3, out_buf, out_buf_size);
    borrador_suelta(BORRADOR_FAT);
    return n;
}

static uint32_t read_file_by_name_interno(const char name8[8], const char ext3[3],
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

            spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES, fat, FAT_BYTES);
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
#undef fat

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

    /*
     * EL CAMINO ASINCRONO, CON LAS DOS MISMAS CORRECCIONES - 30/09/2026.
     * Ver el comentario de escribe_datos_fichero(): aqui habia exactamente
     * la misma linea, y por tanto los mismos dos fallos.
     *
     * Aqui no se puede partir el trabajo en dos bloques como alli, porque
     * esta maquina de estados escribe UN bloque y luego la entrada de
     * directorio. Asi que si el fichero cruza una frontera de 4 kB, este
     * camino DICE QUE NO (devuelve 0) y quien llama se va al bloqueante,
     * que si sabe partirlo. Decir que no es barato: con CONFIG.CSV pasa en
     * dos de las ocho posiciones posibles dentro del bloque, y lo unico que
     * se pierde es el guardado rapido de esa vez.
     */
    {
        uint32_t addr = (uint32_t)(DATA_START_SECTOR + ((uint32_t)old_cluster - 2U))
                        * ROOT_DIR_SECTOR_BYTES;
        uint32_t bloque = addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
        uint32_t off = addr - bloque;

        if ((off + len) > (uint32_t)FLASH_SECTOR_SIZE) {
            return 0U;   /* cruza de bloque: que lo haga el camino lento */
        }

        /* Se LEE el bloque antes de tocarlo, para conservar lo que haya de
         * los vecinos, y los datos se colocan en su sitio dentro de el. */
        spi_flash_read(bloque, s_async_scratch, FLASH_SECTOR_SIZE);
        for (i = 0U; i < len; i++) {
            s_async_scratch[off + i] = data[i];
        }
        spi_flash_async_write_block_start(&s_async_block_op, bloque, s_async_scratch);
        first_data_sector = 0U;   /* ya no se usa: la direccion la manda `bloque` */
        (void)first_data_sector;
    }

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

/* ======================================================================
 * QUE HAY POR ENCIMA DEL SISTEMA DE FICHEROS - 24/09/2026
 *
 * Ver el comentario de spi_flash_alta_t en spi_flash.h para el porque.
 * Aqui solo el como. SOLO LEE.
 *
 * NO SE FIA DE NADA DE LO ESCRITO. Ni del codigo de capacidad del JEDEC
 * -un chip clonado, o uno que alguien haya cambiado por otro mas grande,
 * contesta lo que le parezca- ni de los 2048 sectores que decia el
 * comentario del 17/08/2026. Las dos cosas se MIDEN aqui:
 *
 *   el tamaño del chip, por ALIAS. Una flash SPI NOR no tiene mas
 *   direcciones que su tamaño: los bits de arriba de la direccion se
 *   pierden, asi que leer en 2 MB de un chip de 2 MB devuelve lo mismo
 *   que leer en 0. Buscando la primera potencia de dos que devuelve lo
 *   mismo que el byte 0 sale la capacidad de verdad, diga lo que diga el
 *   identificador. Funciona porque el sector 0 es el arranque del FAT y
 *   NO es uniforme - si lo fuera, cualquier direccion "coincidiria" y la
 *   medida no valdria, asi que eso tambien se comprueba.
 *
 *   el tamaño del sistema de ficheros, leyendo su propio BPB: bytes por
 *   sector en el desplazamiento 11 y numero de sectores en el 19 (o en
 *   el 32 si el primero es cero, que es lo que hace FAT32).
 *
 * Lo que queda por encima de lo segundo y por debajo de lo primero es lo
 * unico que se mira - y, si algun dia se escribe, lo unico donde.
 * ====================================================================== */

#define ALTA_BLOQUE  4096UL   /* el bloque de borrado del chip */
#define ALTA_MUESTRA 32U      /* bytes que se miran del principio de cada uno */

/* La firma que el Makefile pega al final de cada update4.bin. Si aparece
 * por aqui arriba, es que hay una imagen guardada y esto no se toca. */
static const uint8_t k_firma_update4[8] = {
    0x8FU, 0x25U, 0xC8U, 0x65U, 0x59U, 0x9CU, 0x55U, 0x31U
};

/*
 * Capacidad USABLE, medida por alias. "Usable" y no "real" a proposito:
 * el comando 0x03 lleva tres bytes de direccion, o sea que lo mas alto
 * que se puede pedir es 0xFFFFFF - 16 MB justos-. Un chip de 32 MB
 * necesitaria el modo de cuatro bytes, que este controlador no usa, asi
 * que de uno asi se aprovecharian los primeros 16 MB y ya.
 *
 * Por eso, si no se encuentra alias hasta los 8 MB, la respuesta es 16 MB
 * y no "no se ha podido medir": un chip que no da la vuelta en 8 MB tiene
 * al menos 16, y 16 es todo lo que se puede usar. La primera version
 * devolvia 0 en ese caso, que dejaba sin almacen justo al chip mas
 * grande que tiene sentido poner (el W25Q128).
 *
 * Devuelve 0 solo cuando el metodo no vale: si el sector 0 fuese uniforme,
 * cualquier direccion "coincidiria" y la medida no significaria nada.
 */
static uint32_t alta_mide_tam(void)
{
    uint8_t cero[64], otro[64];
    uint32_t tam;
    uint32_t i;
    uint8_t  uniforme = 1U;

    spi_flash_read(0UL, cero, sizeof cero);
    for (i = 1U; i < sizeof cero; i++) {
        if (cero[i] != cero[0]) { uniforme = 0U; break; }
    }
    if (uniforme) { return 0UL; }   /* el metodo no vale aqui */

    for (tam = 1UL << 17; tam < (1UL << 24); tam <<= 1) {
        uint8_t igual = 1U;
        spi_flash_read(tam, otro, sizeof otro);
        for (i = 0U; i < sizeof cero; i++) {
            if (otro[i] != cero[i]) { igual = 0U; break; }
        }
        if (igual) { return tam; }
    }
    /* No da la vuelta por debajo de 16 MB: tiene 16 MB o mas, y 16 es el
     * tope de lo direccionable con tres bytes. Ver el comentario. */
    return 1UL << 24;
}

/*
 * LA CAPACIDAD DEL CHIP, BARATA Y CACHEADA - 02/10/2026.
 *
 * *** Por el dueño: "el software tiene que ser capaz de montar 1 mega si el
 * chip es de 2, 7 megas si el chip es de 8, y todas las variantes
 * posibles". ***
 *
 * De esto cuelgan el tamaño del disco USB y donde empieza la zona alta, o
 * sea que se pregunta al arrancar y ANTES de enumerar el USB. Por eso no
 * vale spi_flash_mira_alta(), que recorre bloque a bloque y tarda una
 * decima por MB: esto son ocho lecturas de 64 bytes.
 *
 * DOS FUENTES, Y EN ESTE ORDEN, QUE NO ES CAPRICHO:
 *
 *  1. LA MEDIDA POR ALIAS (alta_mide_tam). Un chip de 2 MB leido en 2 MB
 *     devuelve lo que hay en 0, porque la direccion da la vuelta. Eso es
 *     una medida: se comprueba lo que el chip HACE.
 *
 *  2. EL CODIGO JEDEC, solo si la medida no vale. Vale cuando el chip esta
 *     recien borrado: los primeros 64 bytes son todos 0xFF, cualquier
 *     direccion "coincide" y el alias no distingue nada. Justo el caso de
 *     un chip nuevo recien soldado, que es cuando mas falta hace.
 *     Es peor fuente porque es lo que el chip DICE, y los clones mienten.
 *
 * Y UN SUELO DE 2 MB, que es la parte importante. Si las dos fuentes
 * fallan, la respuesta es 2 MB: lo que la radio ha tenido siempre.
 *
 * ANUNCIAR DE MENOS NO ROMPE NADA -se desaprovecha sitio-. ANUNCIAR DE MAS
 * SI: el disco USB le diria a Windows que tiene bloques que no existen, y
 * al escribir ahi la direccion daria la vuelta y machacaria el principio
 * del volumen. O sea que ante la duda, el numero pequeño.
 *
 * El techo son 16 MB porque el comando de lectura (0x03) lleva tres bytes
 * de direccion. Un chip de 32 MB pediria el modo de cuatro bytes, que este
 * controlador no usa; de uno asi se aprovecharian los primeros 16 MB.
 */
#define CAP_SUELO   (2UL * 1024UL * 1024UL)
#define CAP_TECHO   (16UL * 1024UL * 1024UL)

static uint32_t s_cap;        /* 0 = aun no se ha preguntado */

/*
 * Tirar la medida para que la siguiente pregunta vuelva a medir.
 *
 * Existe POR EL BANCO, y lo digo aqui para que nadie la use pensando que
 * sirve para algo mas: en la radio el chip no cambia en caliente. Sin
 * esto, sim/capacidad.c mediria el primer chip simulado y daria por bueno
 * ese numero para los otros tres, o sea que no comprobaria nada.
 *
 * Son ocho bytes de codigo y es lo que hace comprobable toda la cuenta del
 * tamaño del disco. Barato.
 */
void spi_flash_olvida_capacidad(void) { s_cap = 0UL; }

uint32_t spi_flash_capacidad(void)
{
    uint32_t tam;

    if (s_cap != 0UL) { return s_cap; }

    tam = alta_mide_tam();        /* 0 si el metodo no vale aqui */

    if (tam == 0UL) {
        spi_flash_jedec_id_t id;
        spi_flash_read_jedec_id(&id);
        /* El codigo de capacidad es el log2 del tamaño en bytes. Se acota
         * a un rango con sentido antes de desplazar: un 0xFF -un chip que
         * no contesta- daria un desplazamiento indefinido. */
        if (id.capacity_code >= 18U && id.capacity_code <= 24U) {
            tam = 1UL << id.capacity_code;
        }
    }

    if (tam < CAP_SUELO) { tam = CAP_SUELO; }
    if (tam > CAP_TECHO) { tam = CAP_TECHO; }

    s_cap = tam;
    return s_cap;
}

/* Donde acaba el sistema de ficheros, segun EL PROPIO sistema de
 * ficheros. Devuelve 0 si su sector de arranque no tiene sentido. */
static uint32_t alta_mide_fs(void)
{
    uint8_t bs[40];
    uint32_t bps, sec;

    spi_flash_read(0UL, bs, sizeof bs);

    bps = (uint32_t)bs[11] | ((uint32_t)bs[12] << 8);
    sec = (uint32_t)bs[19] | ((uint32_t)bs[20] << 8);
    if (sec == 0UL) {
        sec = (uint32_t)bs[32] | ((uint32_t)bs[33] << 8)
            | ((uint32_t)bs[34] << 16) | ((uint32_t)bs[35] << 24);
    }
    /* Un sector de 512, 1024, 2048 o 4096 bytes y un numero de sectores
     * que no sea absurdo. Cualquier otra cosa es que esto no es un BPB. */
    if (bps != 512UL && bps != 1024UL && bps != 2048UL && bps != 4096UL) {
        return 0UL;
    }
    if (sec == 0UL || sec > (1UL << 24)) { return 0UL; }
    return bps * sec;
}

void spi_flash_mira_alta(spi_flash_alta_t *out)
{
    spi_flash_jedec_id_t id;
    uint8_t  buf[ALTA_MUESTRA];
    uint8_t  fin[8];
    uint32_t base, tope, paso, b, i;

    if (out == 0) { return; }

    for (i = 0U; i < 8U; i++) { out->mapa[i] = 0U; out->cab[i] = 0U; }
    out->firma_hay = 0U;
    out->firma_addr = 0UL;
    out->bloques_con_datos = 0UL;
    out->bloques_mirados = 0UL;

    spi_flash_read_jedec_id(&id);
    out->jedec[0] = id.manufacturer_id;
    out->jedec[1] = id.memory_type;
    out->jedec[2] = id.capacity_code;
    /* Lo que DICE el identificador, para poder compararlo con lo que se
     * mide. El convenio es log2(bytes) en el tercer byte. */
    out->bytes_dice = (id.capacity_code >= 0x10U && id.capacity_code <= 0x1AU)
                      ? (1UL << id.capacity_code) : 0UL;
    out->bytes_mide = alta_mide_tam();
    out->fs_bytes   = alta_mide_fs();

    /* La zona que interesa: desde donde acaba el sistema de ficheros
     * hasta donde acaba el chip. Si alguna de las dos medidas ha
     * fallado, no se inventa ninguna - se sale sin mirar nada, que es
     * mejor que mirar el sitio equivocado. */
    base = out->fs_bytes;
    tope = out->bytes_mide;
    if (base == 0UL || tope == 0UL || tope <= base) { return; }

    out->base = base;
    out->tope = tope;

    spi_flash_read(base, out->cab, 8U);

    paso = (tope - base) / 8UL;
    if (paso == 0UL) { paso = 1UL; }

    for (b = base; b < tope; b += ALTA_BLOQUE) {
        uint8_t hay = 0U;

        spi_flash_read(b, buf, ALTA_MUESTRA);
        for (i = 0U; i < ALTA_MUESTRA; i++) {
            if (buf[i] != 0xFFU) { hay = 1U; break; }
        }

        /* Y los ocho ULTIMOS bytes del bloque, que es donde cae la firma
         * de una imagen de update4.bin: 0x40000 es multiplo de 4096, asi
         * que el final de la imagen es el final de un bloque. */
        spi_flash_read(b + ALTA_BLOQUE - 8UL, fin, 8U);
        for (i = 0U; i < 8U; i++) {
            if (fin[i] != 0xFFU) { hay = 1U; }
        }
        /* Dos recorridos y no uno con dos condiciones: mezclados, el corte
         * del segundo se lleva por delante lo que le faltaba por mirar al
         * primero, y un bloque con datos se contaria como borrado. */
        for (i = 0U; i < 8U; i++) {
            if (fin[i] != k_firma_update4[i]) { break; }
        }
        if (i == 8U && !out->firma_hay) {
            out->firma_hay = 1U;
            out->firma_addr = b + ALTA_BLOQUE - 8UL;
        }

        out->bloques_mirados++;
        if (hay) {
            uint32_t k = (b - base) / paso;
            out->bloques_con_datos++;
            if (k > 7UL) { k = 7UL; }
            out->mapa[k] = 1U;
        }
    }
}

/* ======================================================================
 * VOLCAR LA ZONA ALTA A UN FICHERO QUE YA EXISTE
 * Ver el comentario de spi_flash_volcado_abre() en spi_flash.h.
 * ====================================================================== */

/*
 * POR QUE FALLA, CUANDO FALLA.
 *
 * La primera version devolvia 0 por CUATRO motivos distintos -no hay
 * zona, no esta el fichero, esta troceado, mide cero- y la pantalla los
 * enseñaba todos como "falta VOLCADO.BIN". El fichero estaba puesto, o
 * sea que el mensaje señalaba al sitio equivocado y no habia forma de
 * saber cual de los cuatro era sin adivinar.
 *
 * Es el mismo fallo que se corrigio en el almacen de imagenes dos etapas
 * antes -ver imgs_veredicto()- y aqui estaba repetido. Un "no" que no
 * dice de que es un "no" cuesta mas caro que el problema que tapa.
 */
static uint8_t s_vol_porque = SPI_VOL_NADA;

static const uint8_t *s_vol_mem; /* si no es 0, se lee de memoria y no del chip */
static uint8_t  s_vol_restaura;  /* 1 = del fichero AL chip, en vez de al reves */
static uint8_t  s_vol_borra;     /* 1 = solo borrar, sin leer nada */
static uint32_t s_vol_origen;   /* de donde se lee, cuando es del chip */
static uint32_t s_vol_destino;  /* donde empieza el fichero, en el chip */

/*
 * EL CURSOR SOBRE LA CADENA DE CLUSTERS - 28/09/2026.
 *
 * Cuando el fichero de origen esta TROCEADO no vale con sumar el
 * desplazamiento a una direccion: hay que ir saltando de cluster en
 * cluster. Esto es lo que lleva por donde va.
 *
 * NO SE GUARDA UNA TABLA DE TROZOS, y eso es a proposito: en esta radio
 * quedaban 228 bytes de SRAM libres y una tabla de dieciseis trozos son
 * 129. Como la copia va SIEMPRE hacia delante, basta con recordar en que
 * cluster estamos y leer del chip la entrada de la FAT que toca - dos
 * bytes- cada vez que se agota uno. Son 1.024 lecturas de dos bytes para
 * medio megabyte, unos treinta milisegundos en total sobre una copia que
 * dura medio minuto.
 */
static void vol_lee_fichero(uint32_t off, uint8_t *dst, uint32_t n);

static uint16_t s_vol_cl_ini;   /* el primero, para poder volver a empezar */
static uint16_t s_vol_cl;       /* cluster en el que va la lectura */
static uint32_t s_vol_cl_off;   /* desplazamiento del fichero donde empieza */
static uint8_t  s_vol_cadena;   /* 1 = leer por la cadena, no por direccion */
static uint32_t s_vol_total;
static uint32_t s_vol_hechos;

/*
 * La misma maquinaria, leyendo de MEMORIA en vez del chip.
 *
 * Sirve para sacar la flash interna del micro -el gestor de arranque, que
 * es lo unico de lo que no hay copia-. Esa flash esta mapeada en memoria,
 * asi que leerla es seguir un puntero: no hace falta nada especial, y
 * ademas funciona aunque la proteccion de lectura impida llegar por SWD,
 * porque esa proteccion frena al depurador, no al codigo que ya corre
 * dentro.
 *
 * Todo lo demas -encontrar el fichero, comprobar que sus clusters son
 * seguidos, escribir por bloques sin tocar la FAT- es exactamente igual.
 */
uint8_t spi_flash_volcado_abre_mem(const char name8[8], const char ext3[3],
                                   const uint8_t *origen, uint32_t len)
{
    /* El 0 de `origen` de la version de chip no puede distinguirse de una
     * direccion valida, asi que el selector es este puntero: se pone
     * DESPUES de abrir, que es cuando ya se sabe que hay volcado. */
    if (origen == 0) { return 0U; }
    if (!spi_flash_volcado_abre(name8, ext3, 0xFFFFFFF0UL, len)) { return 0U; }
    s_vol_mem = origen;
    return 1U;
}

/*
 * DONDE ESTA UN FICHERO EN EL CHIP.
 *
 * Lo usan las DOS direcciones -volcar hacia el fichero y restaurar desde
 * el- y por eso vive aqui una sola vez: encontrarlo, comprobar que sus
 * clusters son seguidos y traducir eso a "empieza en esta direccion y
 * mide tanto" es el MISMO saber en los dos casos. Dos copias de eso es
 * como se desincronizan las cosas.
 */
/* Aqui dentro habia un "uint8_t fat[FAT_BYTES]": un array de tamano
 * VARIABLE, porque FAT_BYTES sale de la geometria leida del volumen. Eso
 * son hasta 4 kB de pila que gcc marca como "dynamic" en el .su y que
 * tools/pila.py NO puede acotar - o sea, justo el tipo de gasto que la
 * comprobacion de pila no ve venir. Pasa al borrador comun.
 *
 * El #define en vez de un puntero: ver la explicacion en
 * probe_fat_scan_interno. Con puntero, sizeof(fat) valdria 4.
 *
 * Y los sizeof(fat) de dentro pasan a decir FAT_BYTES a proposito: el
 * borrador mide 4096 siempre, pero la FAT de verdad suele ser mas corta, y
 * leer de mas en un bus a 16 us por byte se nota. */
#define fat s_fat
static uint8_t fichero_busca_interno(const char name8[8], const char ext3[3],
                                     uint32_t *addr, uint32_t *bytes,
                                     uint16_t *c0_out, uint8_t troceado_ok);

static uint8_t fichero_busca(const char name8[8], const char ext3[3],
                             uint32_t *addr, uint32_t *bytes)
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) {
        return 0U;
    }
    r = fichero_busca_interno(name8, ext3, addr, bytes, 0, 0U);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

/*
 * IGUAL, PERO ADMITIENDO QUE EL FICHERO ESTE TROCEADO - 28/09/2026.
 *
 * *** El dueño, probando la carga de BD.BIN: "el datos.bin no lo ha
 * flsehado" ... "el fichero esta, te lo aseguro". ***
 *
 * Y estaba. Lo que pasaba es que fichero_busca() EXIGE que la cadena de
 * clusters sea seguida, y su comentario explicaba por que:
 *
 *     "si no lo fuera habria que llevar un mapa de cluster a
 *      desplazamiento, y con ese mapa mal escrito los bytes acabarian en
 *      el sitio equivocado SIN QUE NADA SE QUEJE"
 *
 * Esa razon era buena. Ya no lo es: desde que las bases de datos van en
 * un BD.BIN con una suma por seccion (ver zona_alta.h), un mapa mal
 * escrito NO pasa desapercibido - la verificacion lo pilla, y ademas
 * antes de borrar el fichero del disco-.
 *
 * Y hacia falta, porque el motivo de que el fichero se trocee es NUESTRO:
 * al juntar las dos bases en una, el bloque que hay que meter seguido
 * paso de 258 kB a 512, y en un volumen de 1 MB que ya ha tenido ficheros
 * encima Windows no tiene medio mega seguido que darle.
 *
 * Y UNA COSA MAS QUE COSTO UN RATO: fichero_busca() devuelve 0 por cuatro
 * motivos -no esta, mide cero, el cluster no vale, esta troceado- y quien
 * llamaba lo contaba como "falta el fichero". En la pantalla salia "falta
 * BD.BIN" con el fichero delante. El motivo de verdad esta en
 * s_vol_porque desde siempre; lo que faltaba era mirarlo.
 */
static uint8_t fichero_busca_cadena(const char name8[8], const char ext3[3],
                                    uint32_t *bytes, uint16_t *c0)
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) { return 0U; }
    r = fichero_busca_interno(name8, ext3, 0, bytes, c0, 1U);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

static uint8_t fichero_busca_interno(const char name8[8], const char ext3[3],
                                     uint32_t *addr, uint32_t *bytes,
                                     uint16_t *c0_out, uint8_t troceado_ok)
{
    uint8_t sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i, sec;

    s_vol_porque = SPI_VOL_NO_ESTA;

    /*
     * El directorio raiz ENTERO, sus 32 sectores, y no solo el primero
     * como hacen las funciones de mas arriba. Esas se escribieron para
     * CONFIG.CSV, que lo crea esta misma radio en las primeras
     * posiciones; un fichero copiado desde Windows puede caer mas
     * adelante, sobre todo si ademas lleva entrada de nombre largo.
     */
    for (sec = 0U; sec < 32U; sec++) {
        spi_flash_read((ROOT_DIR_LBA + sec) * ROOT_DIR_SECTOR_BYTES,
                       sector, sizeof(sector));

        for (i = 0U; i < (sizeof(sector) / 32U); i++) {
            const uint8_t *e = &sector[i * 32U];
            uint8_t match = 1U;
            uint32_t j;

            if (e[0] == 0x00U) { return 0U; }   /* fin del directorio */
            if ((e[0] == 0xE5U) || (e[11] == 0x0FU) ||
                ((e[11] & 0x18U) != 0U)) { continue; }

            for (j = 0U; j < 8U; j++) {
                if (e[j] != (uint8_t)name8[j]) { match = 0U; break; }
            }
            for (j = 0U; match && (j < 3U); j++) {
                if (e[8U + j] != (uint8_t)ext3[j]) { match = 0U; break; }
            }
            if (!match) { continue; }

            {
                uint16_t c0 = (uint16_t)(e[26] | ((uint16_t)e[27] << 8));
                uint32_t size = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                              | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
                uint16_t c, esperado;
                uint32_t n = 0U;

                if (size == 0U) { s_vol_porque = SPI_VOL_VACIO; return 0U; }
                if (c0 < 2U || c0 >= (2U + DATA_CLUSTER_COUNT)) {
                    s_vol_porque = SPI_VOL_VACIO;
                    return 0U;
                }

                /*
                 * La cadena tiene que ser SEGUIDA. No por pereza: si no
                 * lo fuera habria que llevar un mapa de cluster a
                 * desplazamiento, y con ese mapa mal escrito los bytes
                 * acabarian en el sitio equivocado sin que nada se
                 * queje. Pedirla seguida convierte eso en un "no" claro,
                 * y en un volumen casi vacio Windows la deja seguida.
                 */
                spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES, fat, FAT_BYTES);
                c = c0; esperado = c0;
                while ((c >= 2U) && (c < 0xFF8U)) {
                    if (!troceado_ok && (c != esperado)) {
                        s_vol_porque = SPI_VOL_TROCEADO;
                        return 0U;
                    }
                    n++;
                    esperado = (uint16_t)(c + 1U);
                    c = fat12_entry(fat, c);
                    /* Una cadena que da mas vueltas que clusters hay es
                     * una cadena con un bucle: no es "troceado", es una
                     * FAT rota, y decir lo primero manda a mirar al sitio
                     * equivocado. */
                    if (n > DATA_CLUSTER_COUNT) {
                        s_vol_porque = SPI_VOL_CORTO;
                        return 0U;
                    }
                }
                /*
                 * La cadena tiene que dar para el tamaño que dice el
                 * directorio, este troceada o no: si no da, el fichero
                 * esta a medias y copiarlo seria copiar basura.
                 */
                if (n * ROOT_DIR_SECTOR_BYTES < size) {
                    s_vol_porque = SPI_VOL_CORTO;
                    return 0U;
                }
                if (c0_out) { *c0_out = c0; }

                if (addr)  { *addr = (DATA_START_SECTOR + (uint32_t)c0 - 2U)
                                     * ROOT_DIR_SECTOR_BYTES; }
                if (bytes) { *bytes = size; }
                s_vol_porque = SPI_VOL_OK;
                return 1U;
            }
        }
    }
    return 0U;   /* no esta */
}
#undef fat

uint8_t spi_flash_volcado_abre(const char name8[8], const char ext3[3],
                               uint32_t origen, uint32_t len)
{
    uint32_t ini = 0U, size = 0U;

    s_vol_total = 0U;
    s_vol_hechos = 0U;
    s_vol_mem = 0;
    s_vol_restaura = 0U;
    s_vol_borra = 0U;
    s_vol_cadena = 0U;
    if (len == 0U) { s_vol_porque = SPI_VOL_ZONA; return 0U; }
    if (!fichero_busca(name8, ext3, &ini, &size)) { return 0U; }

    /*
     * Si el fichero es mas pequeño que la zona, se vuelca LO QUE QUEPA en
     * vez de decir que no. Decir que no obligaria a acertar el tamaño del
     * fichero a la primera, y el que lo crea no sabe -ni tiene por que
     * saber- cuanto mide la zona de arriba de SU chip.
     */
    if (size < len) { len = size; }

    /* Y que no se pise con lo que se va a leer, que seria escribir encima
     * de la fuente segun se copia. */
    if (origen < ini + len && origen + len > ini) {
        s_vol_porque = SPI_VOL_SOLAPA;
        return 0U;
    }

    s_vol_cadena  = 0U;
    s_vol_origen  = origen;
    s_vol_destino = ini;
    s_vol_total   = len;
    s_vol_hechos  = 0U;
    s_vol_porque  = SPI_VOL_OK;
    return 1U;
}

/*
 * EL CAMINO DE VUELTA: de un fichero del volumen a una direccion del chip.
 *
 * Ver el comentario de spi_flash_restaura_abre() en spi_flash.h para el
 * porque y, sobre todo, para las dos cosas que NO puede hacer.
 */
uint8_t spi_flash_restaura_abre(const char name8[8], const char ext3[3],
                                uint32_t destino, uint32_t len,
                                uint32_t suelo, uint32_t tope)
{
    uint32_t ini = 0U, size = 0U;

    s_vol_total = 0U;
    s_vol_hechos = 0U;
    s_vol_mem = 0;
    s_vol_restaura = 0U;
    s_vol_borra = 0U;
    s_vol_cadena = 0U;

    /*
     * EL DESTINO TIENE QUE ESTAR POR ENCIMA DEL SISTEMA DE FICHEROS.
     *
     * Esta es la guardia que hace que esto sea una herramienta y no un
     * arma. Escribir por debajo seria destruir el volumen... incluido el
     * propio fichero del que se esta leyendo, o sea que ni siquiera
     * podria funcionar. Y de paso deja fuera el unico caso de verdad
     * imposible: restaurar el sistema de ficheros desde un fichero
     * guardado EN ese sistema de ficheros.
     */
    if (suelo == 0U || tope == 0U || destino < suelo) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }
    if (len == 0U || destino + len > tope || destino + len < destino) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }

    /*
     * SE ADMITE TROCEADO - 28/09/2026. Ver fichero_busca_cadena(): el
     * motivo por el que el fichero se trocea es nuestro (un BD.BIN de
     * medio mega en un volumen de uno), y la suma por seccion de la zona
     * alta pilla cualquier error del recorrido antes de que llegue a
     * usarse. `ini` ya no hace falta: la lectura va por la cadena.
     */
    {
        uint16_t c0 = 0U;

        if (!fichero_busca_cadena(name8, ext3, &size, &c0)) { return 0U; }
        ini = 0UL;
        s_vol_cl_ini = c0;
        s_vol_cl = c0;
        s_vol_cl_off = 0UL;
        s_vol_cadena = 1U;
    }
    /*
     * SI NO CABE, NO SE COPIA. 27/09/2026.
     *
     * Aqui solo se recortaba hacia abajo -"si el fichero es mas pequeno,
     * copia menos"- y un fichero MAS GRANDE que el hueco se aceptaba tal
     * cual: se copiaban los primeros `len` bytes y se devolvia que todo
     * habia ido bien. Media copia de un ICAO24.BIN no es un fichero roto,
     * es uno que el lector rechaza entero... y para entonces la carga
     * automatica ya ha borrado el original del disco. O sea que el unico
     * sitio donde estaba el fichero era ese.
     *
     * El comentario de aviones_arranca() decia que pasarle `tope` era lo
     * que impedia esto. No lo impedia: el guardia de arriba mira el `len`
     * PEDIDO, no lo que mide el fichero.
     */
    if (size > len) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }
    if (size < len) { len = size; }

    s_vol_origen  = destino;   /* aqui `origen` guarda el destino del chip */
    s_vol_destino = ini;       /* y `destino` donde estan los datos: el fichero */
    s_vol_total   = len;
    s_vol_hechos  = 0U;
    s_vol_restaura = 1U;
    s_vol_porque  = SPI_VOL_OK;
    return 1U;
}

/*
 * BORRAR UN TRAMO. Existe por una pregunta concreta: ¿usa alguien la
 * fuente china que hay en la flash SPI? No se puede demostrar que NADIE
 * LEE algo, pero si se puede quitar y mirar que pasa.
 *
 * Mismas guardias que restaurar - nunca por debajo del sistema de
 * ficheros, nunca por encima del chip- y el mismo avance a pasos.
 */
uint8_t spi_flash_borra_abre(uint32_t desde, uint32_t len,
                             uint32_t suelo, uint32_t tope)
{
    s_vol_total = 0U;
    s_vol_hechos = 0U;
    s_vol_mem = 0;
    s_vol_restaura = 0U;
    s_vol_borra = 0U;
    s_vol_cadena = 0U;

    if (suelo == 0U || tope == 0U || desde < suelo) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }
    if (len == 0U || desde + len > tope || desde + len < desde) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }
    /* Solo bloques enteros: borrar es una operacion de bloque y pedir
     * medio bloque solo puede significar que quien llama se ha
     * equivocado de cuenta. */
    if ((desde & (FLASH_SECTOR_SIZE - 1U)) != 0U ||
        (len & (FLASH_SECTOR_SIZE - 1U)) != 0U) {
        s_vol_porque = SPI_VOL_DESTINO;
        return 0U;
    }

    s_vol_cadena = 0U;
    s_vol_origen = desde;
    s_vol_total  = len;
    s_vol_borra  = 1U;
    s_vol_porque = SPI_VOL_OK;
    return 1U;
}

/* Si un fichero esta en el disco. Lo usa la interfaz para no dejar borrar
 * la fuente sin tener antes a mano con que devolverla. */
uint8_t spi_flash_fichero_hay(const char name8[8], const char ext3[3])
{
    /*
     * POR LA CADENA, no exigiendola seguida - 28/09/2026. "Esta el
     * fichero" y "esta el fichero Y ademas seguido" son dos preguntas
     * distintas, y esta funcion se llama para la primera. Con la version
     * estricta, un BD.BIN troceado contestaba "no esta" y el arranque
     * se iba sin decir nada. Ver fichero_busca_cadena().
     */
    return fichero_busca_cadena(name8, ext3, 0, 0);
}

uint8_t spi_flash_volcado_porque(void) { return s_vol_porque; }

const char *spi_flash_volcado_porque_txt(void)
{
    switch (s_vol_porque) {
    case SPI_VOL_OK:        return tr("listo", "ready");
    case SPI_VOL_ZONA:      return tr("no hay zona arriba", "no upper area");
    case SPI_VOL_NO_ESTA:   return tr("no encuentro el .BIN", "cannot find the .BIN");
    case SPI_VOL_VACIO:     return tr("el .BIN mide cero", "the .BIN is empty");
    case SPI_VOL_TROCEADO:  return tr("el .BIN está troceado", "the .BIN is fragmented");
    case SPI_VOL_CORTO:     return tr("el .BIN está a medias", "the .BIN is incomplete");
    case SPI_VOL_SOLAPA:    return tr("origen pisa destino", "source overlaps target");
    case SPI_VOL_DESTINO:   return tr("destino no permitido", "target not allowed");
    default:                return tr("tocar", "tap");
    }
}

static int8_t volcado_paso_interno(void);

int8_t spi_flash_volcado_paso(void)
{
    int8_t r;

    if (!borrador_coge(BORRADOR_SECTOR)) {
        return -1;
    }
    r = volcado_paso_interno();
    borrador_suelta(BORRADOR_SECTOR);
    return r;
}

static int8_t volcado_paso_interno(void)
{
    /* El buffer de 4 kB que habia aqui vive ahora en .bss - ver el
     * comentario gordo de los borradores, arriba. La envoltura de
     * debajo es la que coge y suelta el cerrojo, y existe justo para
     * no tener que soltarlo en cada uno de los return de esta
     * funcion, que es donde se olvida uno. */
    uint8_t *buf = s_sector;
    uint32_t addr, bloque, off, n;

    if (s_vol_total == 0U) { return -1; }
    if (s_vol_hechos >= s_vol_total) { s_vol_total = 0U; return 0; }

    /*
     * EN LA VUELTA, lo que se escribe es el CHIP y lo que se lee es el
     * FICHERO: los dos papeles cambian de sitio. El resto del paso -leer
     * el bloque entero, cambiar solo el trozo que toca, borrarlo y
     * volver a escribirlo- es identico, y por eso comparten funcion: asi
     * el cuidado de no llevarse por delante lo que comparte bloque se
     * escribe una vez y vale para las dos direcciones.
     */
    if (s_vol_borra) {
        spi_flash_sector_erase_4k(s_vol_origen + s_vol_hechos);
        s_vol_hechos += FLASH_SECTOR_SIZE;
        if (s_vol_hechos >= s_vol_total) { return 0; }
        return 1;
    }

    if (s_vol_restaura) {
        uint32_t a = s_vol_origen + s_vol_hechos;   /* destino en el chip */
        uint32_t b = a & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
        uint32_t o = a - b;
        uint32_t m = FLASH_SECTOR_SIZE - o;
        if (m > s_vol_total - s_vol_hechos) { m = s_vol_total - s_vol_hechos; }

        spi_flash_read(b, buf, FLASH_SECTOR_SIZE);
        vol_lee_fichero(s_vol_hechos, &buf[o], m);
        spi_flash_write_block_4k(b, buf);

        s_vol_hechos += m;
        if (s_vol_hechos >= s_vol_total) { return 0; }
        return 1;
    }

    addr   = s_vol_destino + s_vol_hechos;
    bloque = addr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    off    = addr - bloque;
    n      = FLASH_SECTOR_SIZE - off;
    if (n > s_vol_total - s_vol_hechos) { n = s_vol_total - s_vol_hechos; }

    /* Leer el bloque entero, cambiar SOLO el trozo que es del fichero, y
     * volver a escribirlo. Asi un bloque que comparta sitio con otro
     * fichero conserva lo que no es nuestro. */
    spi_flash_read(bloque, buf, FLASH_SECTOR_SIZE);
    if (s_vol_mem != 0) {
        uint32_t k;
        for (k = 0U; k < n; k++) { buf[off + k] = s_vol_mem[s_vol_hechos + k]; }
    } else {
        spi_flash_read(s_vol_origen + s_vol_hechos, &buf[off], n);
    }
    spi_flash_write_block_4k(bloque, buf);

    s_vol_hechos += n;
    if (s_vol_hechos >= s_vol_total) { return 0; }
    return 1;
}

/*
 * La entrada de la FAT de un cluster, leida DEL CHIP en vez de de una
 * copia en RAM. Son dos bytes; en FAT12 dos entradas comparten tres, asi
 * que la de indice impar son los doce bits de arriba.
 */
static uint16_t fat12_del_chip(uint16_t c)
{
    uint8_t b[2];
    uint32_t off = (uint32_t)c + ((uint32_t)c / 2U);
    uint16_t v;

    spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES + off, b, 2U);
    v = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return (c & 1U) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFFU);
}

/*
 * Lee `n` bytes del fichero de origen a partir del desplazamiento `off`,
 * saltando de cluster en cluster. El cursor solo va hacia delante, que es
 * como lo usa la copia; si alguien pidiera hacia atras se vuelve a
 * empezar desde el primer cluster en vez de leer cualquier cosa.
 */
static void vol_lee_fichero(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!s_vol_cadena) {
        spi_flash_read(s_vol_destino + off, dst, n);
        return;
    }
    if (off < s_vol_cl_off) {
        s_vol_cl = (uint16_t)s_vol_cl_ini;
        s_vol_cl_off = 0UL;
    }
    while (n > 0UL) {
        uint32_t dentro, m;

        while ((off >= s_vol_cl_off + ROOT_DIR_SECTOR_BYTES)
               && (s_vol_cl >= 2U) && (s_vol_cl < 0xFF8U)) {
            s_vol_cl = fat12_del_chip(s_vol_cl);
            s_vol_cl_off += ROOT_DIR_SECTOR_BYTES;
        }
        if ((s_vol_cl < 2U) || (s_vol_cl >= 0xFF8U)) {
            /* Se acabo la cadena antes que el fichero. No deberia pasar
             * -fichero_busca_interno() comprueba que da- pero leer
             * cualquier cosa seria peor que dejar ceros. */
            while (n-- > 0UL) { *dst++ = 0U; }
            return;
        }
        dentro = off - s_vol_cl_off;
        m = ROOT_DIR_SECTOR_BYTES - dentro;
        if (m > n) { m = n; }
        spi_flash_read((DATA_START_SECTOR + (uint32_t)s_vol_cl - 2UL)
                       * ROOT_DIR_SECTOR_BYTES + dentro, dst, m);
        dst += m; off += m; n -= m;
    }
}

uint32_t spi_flash_volcado_hechos(void) { return s_vol_hechos; }
uint32_t spi_flash_volcado_total(void)  { return s_vol_total; }

/*
 * ======================================================================
 * ANADIR UN TROZO A UN FICHERO DEL PENDRIVE - 24/09/2026
 * ======================================================================
 *
 * *** Por el dueno del proyecto: "pos haz un boton que grabe un trozo
 * cada vez que le doy", "trozo 1, luego trozo 1+2", "asi cada vez que le
 * doy", "y cuando me canse de darle te paso el fichero" ***
 *
 * POR QUE ESTO ANTES QUE LA GALERIA
 * ---------------------------------
 * Para guardar imagenes hace falta escribir ficheros grandes en el
 * volumen FAT. Y ese volumen es por donde entra el firmware: update4.bin
 * se copia ahi y el gestor de arranque lo lee de ahi. Si se corrompe, la
 * radio se queda sin forma de recibir la siguiente version, y no hay
 * marcha atras desde dentro.
 *
 * Un escritor de FAT que "parece correcto" es exactamente lo que se come
 * un update4.bin. Asi que primero se prueba con 4 kB de relleno en un
 * fichero de usar y tirar, un trozo por pulsacion, y entre pulsacion y
 * pulsacion lo valida WINDOWS - que es un comprobador de FAT de verdad,
 * sobre el chip de verdad, gratis. Si algo va mal, va mal con un fichero
 * de prueba y no con la imagen del firmware al lado.
 *
 * ENCAJE CON LO QUE YA HABIA
 * --------------------------
 * fat_write_chain() solo admite hasta CLUSTERS_PER_BLOCK clusters
 * empezando en frontera de bloque, o sea 8 de 512 = 4096 bytes justos. Un
 * trozo por pulsacion es exactamente eso, asi que cada pulsacion cae
 * clavada en la primitiva que ya lleva meses probada contra hardware. No
 * hay codigo nuevo de FAT: hay un orden nuevo de llamadas a lo de antes.
 *
 * EL ORDEN ES UNA PROPIEDAD DE SEGURIDAD, NO UNA CASUALIDAD
 * ---------------------------------------------------------
 * Si se va la luz a media pulsacion, el volumen tiene que quedar en un
 * estado que no sea corrupcion. Por eso se hace en este orden y no en
 * otro:
 *
 *   1. los datos           los clusters siguen libres segun la FAT:
 *                          se ha escrito en sitio de nadie. Inocuo.
 *   2. encadenar los 8     ahora estan ocupados pero no los alcanza
 *      nuevos entre si     ningun fichero: "clusters perdidos". fsck se
 *                          queja, pero no hay nada roto.
 *   3. el ultimo viejo     el fichero ya los alcanza, pero su tamano
 *      apunta al primero   dice menos: la cola simplemente no se cuenta.
 *      nuevo               Inocuo.
 *   4. el tamano nuevo     completo.
 *
 * Al reves -el tamano primero- dejaria un fichero diciendo que tiene mas
 * datos de los que hay encadenados, y eso SI es corrupcion. Cada paso
 * degrada hacia algo benigno; ninguno hacia algo roto.
 *
 * DONDE ESTA LA GUARDA
 * --------------------
 * El unico borrado que hace esto es el del bloque de datos, y su
 * direccion sale de spi_flash_probe_fat_scan(), que solo da por libre un
 * bloque cuyos OCHO clusters tienen la entrada de FAT a 0x000. No se
 * borra nunca un bloque que no este certificado vacio por la FAT misma.
 *
 * Es bloqueante, del orden de medio segundo (cuatro ciclos de borrado y
 * programado, y el borrado de 4 kB de un W25Q son ~50 ms que pone el
 * chip y no se pueden acelerar por mas rapido que se le echen los bytes).
 * Eso congela el espectro medio segundo. Se acepta porque es una accion
 * explicita y destructiva que el usuario acaba de pedir con el dedo: la
 * regla de esta casa es que lo lento no pase de sorpresa, no que no pase.
 */

#define ANADE_TROZO        FLASH_SECTOR_SIZE      /* 4096: un bloque de borrado */
#define ANADE_CLUSTERS     CLUSTERS_PER_BLOCK     /* 8 clusters de 512 */
#define ANADE_TOPE_TROZOS  200U                   /* 800 kB, tope de cordura */

/*
 * Pone UNA entrada de la FAT12, en las dos copias.
 *
 * fat_write_chain() escribe una cadena entera; esto escribe una sola
 * entrada, que es lo que hace falta para que el ultimo cluster de la
 * cadena vieja deje de decir "fin" y pase a apuntar al trozo nuevo. Van
 * las dos copias porque un sistema de ficheros con las dos FAT distintas
 * es exactamente lo que hace que un comprobador declare el volumen
 * dañado.
 */
static void fat_pon_una(uint32_t cluster, uint16_t valor)
{
    uint32_t off = cluster + (cluster / 2U);
    uint8_t  copia;

    for (copia = 0U; copia < 2U; copia++) {
        uint32_t lba = FAT1_LBA + ((uint32_t)copia * FAT_SECTORS);
        uint32_t abs = (lba * ROOT_DIR_SECTOR_BYTES) + off;
        uint32_t blq = abs & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
        uint8_t  buf[2];

        spi_flash_read(abs, buf, 2U);
        fat12_pack_entry(buf, 0U, cluster, valor);
        spi_flash_block_read_modify_write(abs, abs - blq, buf, 2U);
    }
}

/*
 * Recorre la cadena de un fichero y devuelve su ultimo cluster y cuantos
 * tiene. Devuelve 0 si la cadena no tiene sentido -un cluster fuera de
 * rango, o mas eslabones que clusters hay en el volumen, que es como se
 * detecta un bucle sin fiarse de nada-.
 */
static uint8_t cadena_ultimo_interno(uint32_t primero, uint32_t *ultimo, uint32_t *cuantos,
                                     uint32_t *saltos);

static uint8_t cadena_ultimo(uint32_t primero, uint32_t *ultimo, uint32_t *cuantos,
                             uint32_t *saltos)
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) {
        return 0U;
    }
    r = cadena_ultimo_interno(primero, ultimo, cuantos, saltos);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

/* El borrador comun, con el nombre corto que usaba la variable local que
 * habia aqui. Es un #define y no un "uint8_t *fat = s_fat" A PROPOSITO:
 * con el puntero, los sizeof(fat) que hay dentro de la funcion pasan de
 * valer 4096 a valer 4, la FAT se lee entera en 4 bytes, el driver se cree
 * que el volumen esta vacio y escribe encima de lo que haya. Lo hice asi
 * primero y eso es exactamente lo que paso: el banco de FAT lo cazo con
 * "se ha estropeado ZONAALTABIN" y el compilador no dijo ni mu. Con el
 * #define, el nombre sigue siendo un array y no hay nada que recordar. */
#define fat s_fat
static uint8_t cadena_ultimo_interno(uint32_t primero, uint32_t *ultimo, uint32_t *cuantos,
                                     uint32_t *saltos)
{
    /* El buffer de 4 kB que habia aqui vive ahora en .bss - ver el
     * comentario gordo de los borradores, arriba. La envoltura de
     * debajo es la que coge y suelta el cerrojo, y existe justo para
     * no tener que soltarlo en cada uno de los return de esta
     * funcion, que es donde se olvida uno. */
    uint32_t c = primero, n = 1U, atras = 0U;

    if (primero < 2U || primero >= (2U + DATA_CLUSTER_COUNT)) { return 0U; }
    spi_flash_read(FAT1_LBA * ROOT_DIR_SECTOR_BYTES, fat, sizeof fat);

    for (;;) {
        uint16_t sig = fat12_entry(fat, c);
        if (sig >= 0x0FF8U) { break; }               /* fin de cadena */
        if (sig < 2U || sig >= (2U + DATA_CLUSTER_COUNT)) { return 0U; }
        if (n >= DATA_CLUSTER_COUNT) { return 0U; }  /* bucle */
        /* Un salto HACIA ATRAS quiere decir que el fichero esta ocupando
         * un hueco que dejo otro al borrarse: fragmentacion de verdad.
         * Hacia delante y no consecutivo tambien es un salto, pero el que
         * interesa medir es este, que es el caso raro. */
        if (sig < c) { atras++; }
        c = sig;
        n++;
    }
    *ultimo = c;
    *cuantos = n;
    if (saltos) { *saltos = atras; }
    return 1U;
}
#undef fat

/*
 * Cuanto mide un fichero del volumen, segun su entrada de directorio.
 *
 * Se mira la ENTRADA y no la cadena a proposito: fichero_busca() exige
 * que el fichero sea contiguo, y uno escrito a trozos deja de serlo en
 * cuanto el bloque libre que toca no va pegado al anterior. Devuelve 0
 * si no existe.
 */
uint8_t spi_flash_fichero_tam(const char name8[8], const char ext3[3], uint32_t *tam)
{
    /*
     * POR EL DIRECTORIO ENTERO, no solo por su primer sector -
     * 28/09/2026. dir_find_entry() mira dieciseis entradas y se escribio
     * para CONFIG.CSV, que lo crea esta misma radio en las primeras
     * posiciones. Un fichero copiado desde Windows a un volumen que ya ha
     * tenido cosas encima cae mas adelante, y entonces esto contestaba
     * "no esta" sobre un fichero que si esta.
     */
    return fichero_busca_cadena(name8, ext3, tam, 0);
}

uint8_t spi_flash_anade_trozo(const char name8[8], const char ext3[3],
                              const uint8_t *trozo, spi_anade_r_t *porque)
{
    spi_flash_fat_scan_t scan;

    /* Antes que nada: ¿es esta una FAT12 de las que sabemos manejar? Ver el
     * comentario gordo de geo_lee(). Escribir sin mirar esto es lo que le
     * costo el volumen al dueño del proyecto. */
    if (!geo_lee()) {
        if (porque) { *porque = SPI_ANADE_GEOMETRIA; }
        return 0U;
    }
    uint32_t dir_off = 0UL, tam = 0UL, ultimo = 0UL, clusters = 0UL;
    uint16_t primero = 0U;

    if (porque) { *porque = SPI_ANADE_OK; }
    if (trozo == 0) { if (porque) { *porque = SPI_ANADE_RARO; } return 0U; }

    spi_flash_probe_fat_scan(&scan);
    if (!scan.found_free_block) {
        if (porque) { *porque = SPI_ANADE_SIN_SITIO; }
        return 0U;
    }

    /* Primera pulsacion: el fichero no existe todavia, y crear uno de un
     * bloque es justo lo que spi_flash_write_new_file() ya sabe hacer. */
    if (!dir_find_entry(name8, ext3, &dir_off, &primero, &tam)) {
        if (!spi_flash_write_new_file(&scan, name8, ext3, trozo, ANADE_TROZO)) {
            if (porque) { *porque = SPI_ANADE_DIR_LLENO; }
            return 0U;
        }
        return 1U;
    }

    /* Este fichero lo escribimos nosotros a trozos enteros. Un tamano que
     * no sea multiplo del trozo significa que lo ha tocado otro, y
     * entonces no es nuestro y no se amplia. */
    if (tam == 0UL || (tam % ANADE_TROZO) != 0UL) {
        if (porque) { *porque = SPI_ANADE_RARO; }
        return 0U;
    }
    if ((tam / ANADE_TROZO) >= ANADE_TOPE_TROZOS) {
        if (porque) { *porque = SPI_ANADE_TOPE; }
        return 0U;
    }
    if (!cadena_ultimo(primero, &ultimo, &clusters, 0) ||
        clusters != (tam / ROOT_DIR_SECTOR_BYTES)) {
        /* La cadena y el tamano no cuentan lo mismo: algo no cuadra y
         * este no es sitio para adivinar. */
        if (porque) { *porque = SPI_ANADE_CADENA; }
        return 0U;
    }

    /* 1. los datos, en el bloque que la FAT acaba de certificar vacio */
    spi_flash_write_block_4k(scan.free_block_byte_addr, trozo);

    /* 2. los ocho clusters nuevos, encadenados entre si y terminados */
    fat_write_chain(FAT1_LBA, scan.free_block_first_cluster, ANADE_CLUSTERS);
    fat_write_chain(FAT1_LBA + FAT_SECTORS, scan.free_block_first_cluster, ANADE_CLUSTERS);

    /* 3. el ultimo de la cadena vieja deja de decir "fin" */
    fat_pon_una(ultimo, (uint16_t)scan.free_block_first_cluster);

    /* 4. y ya se puede decir que el fichero mide mas */
    {
        uint32_t nuevo = tam + ANADE_TROZO;
        uint8_t  t[4];

        t[0] = (uint8_t)(nuevo & 0xFFU);
        t[1] = (uint8_t)((nuevo >> 8) & 0xFFU);
        t[2] = (uint8_t)((nuevo >> 16) & 0xFFU);
        t[3] = (uint8_t)((nuevo >> 24) & 0xFFU);
        spi_flash_block_read_modify_write(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES,
                                          dir_off + 28U, t, 4U);
    }
    return 1U;
}

/*
 * Cuantos clusters tiene la cadena de un fichero y cuantas veces salta
 * hacia atras.
 *
 * Recorre la FAT entera, o sea unos 3 kB por un bus que va a 16 us el
 * byte: del orden de 50 ms. NO llamar al pintar una celda - se llama una
 * vez, al grabar, y se guarda el resultado.
 */
uint8_t spi_flash_fichero_cadena(const char name8[8], const char ext3[3],
                                 uint32_t *clusters, uint32_t *saltos)
{
    uint32_t dir_off = 0UL, tam = 0UL, ultimo = 0UL, n = 0UL, atras = 0UL;
    uint16_t primero = 0U;

    if (!dir_find_entry(name8, ext3, &dir_off, &primero, &tam)) { return 0U; }
    if (!cadena_ultimo(primero, &ultimo, &n, &atras)) { return 0U; }
    if (clusters) { *clusters = n; }
    if (saltos)   { *saltos = atras; }
    return 1U;
}

/*
 * El numero mas alto entre los ficheros que se llaman <pre><NNN>.<ext>.
 *
 * Sirve para elegir el nombre del siguiente sin preguntar uno por uno: una
 * sola lectura del sector de directorio en vez de mil busquedas, que a
 * 16 us el byte serian ocho segundos. Devuelve 0 si no hay ninguno, asi
 * que el primero siempre es el 001.
 */
uint32_t spi_flash_dir_max_indice(const char pre[4], const char ext3[3])
{
    uint8_t  sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i, max = 0UL;

    spi_flash_read(ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES, sector, sizeof sector);
    for (i = 0U; i < (sizeof(sector) / 32U); i++) {
        const uint8_t *e = &sector[i * 32U];
        uint32_t n = 0UL;
        uint8_t  j, ok = 1U;

        if (e[0] == 0x00U || e[0] == 0xE5U || e[0] == 0xFFU) { continue; }
        if ((e[11] & 0x0FU) == 0x0FU) { continue; }   /* trozo de nombre largo */
        for (j = 0U; j < 4U; j++) { if (e[j] != (uint8_t)pre[j]) { ok = 0U; } }
        for (j = 0U; j < 3U; j++) { if (e[8U + j] != (uint8_t)ext3[j]) { ok = 0U; } }
        if (!ok) { continue; }
        /* Las tres cifras, y la octava posicion en blanco: "SSTV12" no
         * cuenta, ni "SSTVABC". */
        if (e[7] != (uint8_t)' ') { continue; }
        for (j = 4U; j < 7U; j++) {
            if (e[j] < (uint8_t)'0' || e[j] > (uint8_t)'9') { ok = 0U; break; }
            n = (n * 10UL) + (uint32_t)(e[j] - (uint8_t)'0');
        }
        if (ok && n > max) { max = n; }
    }
    return max;
}

/*
 * Donde empieza EN EL CHIP el primer cluster de un fichero.
 *
 * Hace falta para poder reescribir su cabecera despues, sin exigir que el
 * fichero entero sea contiguo -que es lo que pide fichero_busca() y lo que
 * un fichero escrito a trozos deja de cumplir en cuanto el hueco libre que
 * toca no va pegado al anterior-. El PRIMER cluster si esta siempre en la
 * entrada de directorio, y como spi_flash_anade_trozo() reparte de bloque
 * de borrado en bloque de borrado, ese primer cluster empieza justo en
 * frontera de 4 kB: los 4096 primeros bytes del fichero son un bloque
 * entero y se pueden reescribir de una pieza.
 */
uint8_t spi_flash_fichero_cabeza(const char name8[8], const char ext3[3],
                                 uint32_t *addr)
{
    uint32_t dir_off = 0UL, tam = 0UL;
    uint16_t primero = 0U;

    if (!dir_find_entry(name8, ext3, &dir_off, &primero, &tam)) { return 0U; }
    if (primero < 2U || primero >= (2U + DATA_CLUSTER_COUNT)) { return 0U; }
    if (addr) {
        *addr = (DATA_START_SECTOR + ((uint32_t)primero - 2UL)) * ROOT_DIR_SECTOR_BYTES;
    }
    return 1U;
}

/* Reescribe los primeros `len` bytes de un fichero (como mucho un bloque de
 * borrado). Es lectura-modificacion-escritura de ese bloque, asi que no
 * levanta bits y no toca lo que haya detras dentro del mismo bloque. */
uint8_t spi_flash_fichero_cabecera_pon(const char name8[8], const char ext3[3],
                                       const uint8_t *datos, uint32_t len)
{
    uint32_t addr = 0UL;

    if (len == 0UL || len > FLASH_SECTOR_SIZE) { return 0U; }
    if (!spi_flash_fichero_cabeza(name8, ext3, &addr)) { return 0U; }
    if ((addr & (FLASH_SECTOR_SIZE - 1U)) != 0U) { return 0U; }  /* no alineado: no es nuestro */
    spi_flash_block_read_modify_write(addr, 0U, datos, len);
    return 1U;
}

const char *spi_flash_anade_porque_txt(spi_anade_r_t r)
{
    switch (r) {
    case SPI_ANADE_OK:         return tr("hecho", "done");
    case SPI_ANADE_SIN_SITIO:  return tr("el disco está lleno", "the disk is full");
    case SPI_ANADE_DIR_LLENO:  return tr("directorio lleno", "directory full");
    case SPI_ANADE_CADENA:     return tr("la cadena no cuadra", "the chain does not add up");
    case SPI_ANADE_TOPE:       return tr("ya tiene 200 trozos", "already has 200 chunks");
    case SPI_ANADE_RARO:       return tr("el fichero es de otro", "the file belongs to another");
    case SPI_ANADE_GEOMETRIA:  return tr("formato no reconocido", "format not recognised");
    default:                   return "?";
    }
}

/*
 * ======================================================================
 * RESERVAR UN FICHERO ENTERO DE GOLPE - 24/09/2026
 * ======================================================================
 *
 * POR QUE, CON NUMEROS
 * --------------------
 * spi_flash_anade_trozo() monta el fichero de 4 kB en 4 kB, y cada
 * añadido paga el precio completo: buscar sitio en la FAT (3 kB de
 * lectura), recorrer la cadena (otros 3), escribir el bloque de datos, y
 * CUATRO ciclos de lectura-modificacion-escritura -releer y reescribir
 * 4 kB enteros para cambiar dos bytes-. El banco lo midio: 53 kB por el
 * bus para escribir 4 kB utiles, o sea 1205 ms por bloque.
 *
 * Para las fotos de SSTV eso no vale. Un bloque se llena cada 4,3
 * renglones a 24 bits, que en Scottie 2 son 1,19 s: escribir iria mas
 * lento que recibir y el anillo de lineas se desbordaria. Medido:
 *
 *     Martin 1,  BMP 24   un bloque cada 1,90 s   ->  63 % del tiempo
 *     Scottie 2, BMP 24   un bloque cada 1,19 s   -> 101 %  NO LLEGA
 *     Martin 2,  BMP 24   un bloque cada 0,97 s   -> 124 %  NO LLEGA
 *
 * Reservando la foto entera al abrirla, la cadena se escribe UNA vez y a
 * partir de ahi cada bloque es un borrado y un programado en una
 * direccion ya conocida: unos 130 ms. Nueve veces menos, y deja de
 * importar el modo y el formato.
 *
 * COMO SE ESCRIBE UNA CADENA LARGA SIN PAGARLA CARA
 * -------------------------------------------------
 * Porque se exige que el hueco sea SEGUIDO. Con los clusters
 * consecutivos, sus entradas de FAT tambien lo son: 61 bloques son 488
 * entradas de 12 bits, o sea 732 bytes corridos. Y la FAT entera de este
 * volumen mide 3072 bytes, que caben en dos bloques de borrado:
 *
 *     FAT1  bytes 2048..5119   -> bloques 0 y 1
 *     FAT2  bytes 5120..8191   -> bloque 1
 *     raiz  bytes 8192..       -> bloque 2
 *
 * Asi que toda la reserva son tres ciclos de lectura-modificacion-
 * escritura para las dos copias de la FAT y uno para el directorio. Unos
 * 600 ms, una sola vez por foto.
 *
 * Un hueco seguido tambien significa que puede NO HABERLO aunque sobre
 * sitio, si el volumen esta fragmentado. Se dice y no se empieza, que es
 * mejor que abrir una foto que no va a caber.
 */

/* Donde vive cada cosa, en bytes desde el principio del chip. */
#define FAT1_OFF   (FAT1_LBA * ROOT_DIR_SECTOR_BYTES)
#define FAT2_OFF   ((FAT1_LBA + FAT_SECTORS) * ROOT_DIR_SECTOR_BYTES)
#define RAIZ_OFF   (ROOT_DIR_LBA * ROOT_DIR_SECTOR_BYTES)

/* Vuelca una copia entera de la FAT desde un buffer de FAT_BYTES,
 * partiendola por los bloques de borrado que cruce. */
static void fat_vuelca(uint32_t off, const uint8_t *fat)
{
    uint32_t hecho = 0U;

    while (hecho < FAT_BYTES) {
        uint32_t abs = off + hecho;
        uint32_t blq = abs & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
        uint32_t cabe = (blq + FLASH_SECTOR_SIZE) - abs;
        uint32_t n = ((FAT_BYTES - hecho) < cabe) ? (FAT_BYTES - hecho) : cabe;

        spi_flash_block_read_modify_write(abs, abs - blq, &fat[hecho], n);
        hecho += n;
    }
}

static uint8_t reserva_interno(const char name8[8], const char ext3[3],
                               uint32_t bytes, uint32_t *addr, spi_anade_r_t *porque);

uint8_t spi_flash_reserva(const char name8[8], const char ext3[3],
                          uint32_t bytes, uint32_t *addr, spi_anade_r_t *porque)
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) {
        if (porque) { *porque = SPI_ANADE_GEOMETRIA; }
        return 0U;
    }
    r = reserva_interno(name8, ext3, bytes, addr, porque);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

/* El borrador comun, con el nombre corto que usaba la variable local que
 * habia aqui. Es un #define y no un "uint8_t *fat = s_fat" A PROPOSITO:
 * con el puntero, los sizeof(fat) que hay dentro de la funcion pasan de
 * valer 4096 a valer 4, la FAT se lee entera en 4 bytes, el driver se cree
 * que el volumen esta vacio y escribe encima de lo que haya. Lo hice asi
 * primero y eso es exactamente lo que paso: el banco de FAT lo cazo con
 * "se ha estropeado ZONAALTABIN" y el compilador no dijo ni mu. Con el
 * #define, el nombre sigue siendo un array y no hay nada que recordar. */
#define fat s_fat
static uint8_t reserva_interno(const char name8[8], const char ext3[3],
                               uint32_t bytes, uint32_t *addr, spi_anade_r_t *porque)
{
    /* El buffer de 4 kB que habia aqui vive ahora en .bss - ver el
     * comentario gordo de los borradores, arriba. La envoltura de
     * debajo es la que coge y suelta el cerrojo, y existe justo para
     * no tener que soltarlo en cada uno de los return de esta
     * funcion, que es donde se olvida uno. */
    uint8_t  entrada[64];
    uint32_t bloques = bytes / FLASH_SECTOR_SIZE;
    uint32_t clusters = bloques * CLUSTERS_PER_BLOCK;
    uint32_t primero = 0U, b, i, dir_off = 0U;
    uint8_t  terminador = 0U, hallado = 0U;
    uint32_t seguidos = 0U;

    if (porque) { *porque = SPI_ANADE_OK; }
    if (!geo_lee()) {
        if (porque) { *porque = SPI_ANADE_GEOMETRIA; }
        return 0U;
    }
    if (bytes == 0UL || (bytes % FLASH_SECTOR_SIZE) != 0UL) {
        if (porque) { *porque = SPI_ANADE_RARO; }
        return 0U;
    }

    /* El hueco en el directorio, antes de tocar nada: si no lo hay, no
     * tiene sentido haber reservado clusters que luego no alcanza nadie. */
    if (!dir_find_end_marker_offset(&dir_off, &terminador)) {
        if (porque) { *porque = SPI_ANADE_DIR_LLENO; }
        return 0U;
    }
    if ((dir_off + (terminador ? 64U : 32U)) > ROOT_DIR_SECTOR_BYTES) {
        if (porque) { *porque = SPI_ANADE_DIR_LLENO; }
        return 0U;
    }

    spi_flash_read(FAT1_OFF, fat, sizeof fat);

    /*
     * Bloques ENTEROS libres y seguidos. Se mira por bloques y no por
     * clusters sueltos porque el borrado va por bloques: medio bloque
     * libre no sirve de nada, y empezar a media frontera obligaria a
     * releer para no pisar al vecino.
     */
    for (b = 0U; b < (DATA_CLUSTER_COUNT / CLUSTERS_PER_BLOCK); b++) {
        uint32_t c0 = 2U + (b * CLUSTERS_PER_BLOCK);
        uint8_t  libre = 1U;

        for (i = c0; i < c0 + CLUSTERS_PER_BLOCK; i++) {
            if (fat12_entry(fat, i) != 0x000U) { libre = 0U; break; }
        }
        if (libre) {
            if (seguidos == 0U) { primero = c0; }
            seguidos++;
            if (seguidos == bloques) { hallado = 1U; break; }
        } else {
            seguidos = 0U;
        }
    }
    if (!hallado) {
        /* Puede haber sitio de sobra y aun asi no caber: lo que falta es un
         * hueco SEGUIDO. */
        if (porque) { *porque = SPI_ANADE_SIN_SITIO; }
        return 0U;
    }

    /* La cadena entera, en el buffer. Cada cluster apunta al siguiente y el
     * ultimo dice fin. */
    for (i = 0U; i < clusters; i++) {
        uint32_t c = primero + i;
        uint16_t v = ((i + 1U) < clusters) ? (uint16_t)(c + 1U) : 0x0FFFU;
        fat12_pack_entry(fat, c + (c / 2U), c, v);
    }
    fat_vuelca(FAT1_OFF, fat);
    fat_vuelca(FAT2_OFF, fat);

    /* Y la entrada de directorio, ya con el tamaño definitivo: el fichero
     * nace completo y lo unico que falta es rellenarlo. */
    for (i = 0U; i < (terminador ? 64U : 32U); i++) { entrada[i] = 0x00U; }
    for (i = 0U; i < 8U; i++) { entrada[i] = (uint8_t)name8[i]; }
    for (i = 0U; i < 3U; i++) { entrada[8U + i] = (uint8_t)ext3[i]; }
    entrada[11] = 0x20U;                                  /* ATTR_ARCHIVE */
    entrada[16] = 0x21U; entrada[17] = 0x5CU;
    entrada[18] = 0x21U; entrada[19] = 0x5CU;
    entrada[24] = 0x21U; entrada[25] = 0x5CU;
    entrada[26] = (uint8_t)(primero & 0xFFU);
    entrada[27] = (uint8_t)((primero >> 8) & 0xFFU);
    entrada[28] = (uint8_t)(bytes & 0xFFU);
    entrada[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    entrada[30] = (uint8_t)((bytes >> 16) & 0xFFU);
    entrada[31] = (uint8_t)((bytes >> 24) & 0xFFU);
    spi_flash_block_read_modify_write(RAIZ_OFF, dir_off, entrada,
                                      terminador ? 64U : 32U);

    if (addr) {
        *addr = (DATA_START_SECTOR + (primero - 2U)) * ROOT_DIR_SECTOR_BYTES;
    }
    return 1U;
}
#undef fat

/*
 * Recorta un fichero reservado a los bytes que de verdad se han usado.
 *
 * POR QUE HACE FALTA
 * ------------------
 * spi_flash_reserva() pide la foto entera por delante, y eso esta bien
 * mientras la foto acabe. Si se corta -se pierde la señal, o era ruido
 * que colo un renglon por casualidad- los bloques que no se llegaron a
 * escribir se quedarian ocupados para siempre: cuatro sustos y el
 * pendrive lleno de ficheros de 240 kB con una linea dentro.
 *
 * Como la reserva es SEGUIDA, recortar es barato: el cluster nuevo ultimo
 * pasa a decir fin y los de detras vuelven a cero. Todo cae en el mismo
 * buffer de la FAT que ya se lee entero, asi que es el mismo precio que
 * reservar - unos 600 ms, una sola vez.
 *
 * `bytes` se redondea hacia arriba a bloque de borrado: no tiene sentido
 * liberar medio bloque cuando el borrado va por bloques enteros.
 */
static uint8_t recorta_interno(const char name8[8], const char ext3[3], uint32_t bytes);

/*
 * BORRAR UN FICHERO DEL DISCO - 25/09/2026.
 *
 * *** Idea del dueno: "lo deberia de borrar el propio firmware una vez lo
 * ha cargado, igual que hace con los update". ***
 *
 * Tenia razon y faltaba la pieza: este driver sabia crear, ampliar,
 * recortar y sobrescribir, pero no borrar. ICAO24.BIN son medio mega en un
 * disco de pocos megas, y una vez esta en la zona alta de la flash no
 * pinta nada ahi.
 *
 * EL ORDEN IMPORTA Y NO ES EL INTUITIVO. Se marca PRIMERO la entrada del
 * directorio y DESPUES se sueltan los clusters:
 *
 *   entrada primero  ->  si se va la luz en medio quedan clusters
 *                        ocupados sin dueno. Un chkdsk los recupera y
 *                        mientras tanto no pasa nada.
 *   clusters primero ->  si se va la luz en medio queda una entrada VIVA
 *                        apuntando a clusters que ya estan libres. El
 *                        siguiente fichero que se escriba se mete encima
 *                        y eso ya es corrupcion de verdad.
 *
 * Perder sitio es reversible; que dos ficheros compartan clusters no.
 *
 * LA CADENA SE RECORRE, NO SE SUPONE. recorta_interno() da por hecho que
 * los clusters van seguidos, y para los ficheros que escribe la radio es
 * cierto porque se los reserva ella. Este no: ICAO24.BIN lo ha copiado
 * Windows, y Windows lo parte por donde le da la gana. Asi que se sigue
 * la FAT eslabon a eslabon, con tope de cordura para no colgarse si la
 * cadena tiene un bucle.
 */
/*
 * Como dir_find_entry() pero por los 32 sectores del directorio raiz, y
 * devolviendo el desplazamiento ABSOLUTO de la entrada -no el relativo al
 * primer sector-, que es lo que hace falta para poder escribir en ella
 * este en el sector que este.
 */
static uint8_t dir_busca_todo(const char name8[8], const char ext3[3],
                              uint32_t *out_off, uint16_t *out_c0, uint32_t *out_tam)
{
    uint8_t sector[ROOT_DIR_SECTOR_BYTES];
    uint32_t i, sec;

    for (sec = 0U; sec < 32U; sec++) {
        uint32_t base = (ROOT_DIR_LBA + sec) * ROOT_DIR_SECTOR_BYTES;

        spi_flash_read(base, sector, sizeof(sector));
        for (i = 0U; i < (sizeof(sector) / 32U); i++) {
            const uint8_t *e = &sector[i * 32U];
            uint8_t match = 1U;
            uint32_t j;

            if (e[0] == 0x00U) { return 0U; }    /* fin del directorio */
            if ((e[0] == 0xE5U) || (e[11] == 0x0FU) ||
                ((e[11] & 0x18U) != 0U)) { continue; }

            for (j = 0U; j < 8U; j++) {
                if (e[j] != (uint8_t)name8[j]) { match = 0U; break; }
            }
            for (j = 0U; match && (j < 3U); j++) {
                if (e[8U + j] != (uint8_t)ext3[j]) { match = 0U; break; }
            }
            if (!match) { continue; }

            *out_off = base + (i * 32U);
            *out_c0  = (uint16_t)(e[26] | ((uint16_t)e[27] << 8));
            *out_tam = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                     | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
            return 1U;
        }
    }
    return 0U;
}

static uint8_t fichero_borra_interno(const char name8[8], const char ext3[3]);

uint8_t spi_flash_fichero_borra(const char name8[8], const char ext3[3])
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) {
        return 0U;
    }
    r = fichero_borra_interno(name8, ext3);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

#define fat s_fat
static uint8_t fichero_borra_interno(const char name8[8], const char ext3[3])
{
    uint8_t  marca = 0xE5U;      /* "aqui hubo un fichero" en FAT */
    uint32_t dir_off = 0UL, tam = 0UL;
    uint16_t primero = 0U;
    uint32_t abs, blq, c, n;

    if (!geo_lee()) { return 0U; }
    /*
     * LOS 32 SECTORES DEL DIRECTORIO, NO EL PRIMERO. 27/09/2026.
     *
     * Aqui habia un dir_find_entry(), que mira SOLO el primer sector -16
     * entradas-. Pero el fichero que se va a borrar es el mismo que
     * encontro fichero_busca(), que mira los 32 a proposito y lleva el
     * aviso escrito encima: "un fichero copiado desde Windows puede caer
     * mas adelante, sobre todo si ademas lleva entrada de nombre largo".
     *
     * O sea que con el disco algo lleno el fichero se copiaba bien y no
     * se borraba nunca, en silencio. Y como la carga es automatica al
     * arrancar, eso son treinta segundos de copia y 127 bloques de flash
     * reescritos EN CADA ENCENDIDO, para siempre, sin forma de pararlo
     * desde la radio.
     */
    if (!dir_busca_todo(name8, ext3, &dir_off, &primero, &tam)) { return 0U; }

    /* 1. La entrada. El bloque se calcula en vez de darlo por alineado:
     * RAIZ_OFF no tiene por que caer en frontera de 4 kB y el offset que
     * pide block_read_modify_write es desde el principio del BLOQUE, no
     * desde RAIZ_OFF. */
    abs = dir_off;                 /* ya es absoluto: ver dir_busca_todo() */
    blq = abs & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
    spi_flash_block_read_modify_write(abs, abs - blq, &marca, 1U);

    /* 2. Y la cadena, si la entrada apuntaba a algun sitio con sentido.
     * Un fichero de 0 bytes no tiene cadena y no es un error. */
    if ((primero >= 2U) && (primero < (2U + DATA_CLUSTER_COUNT))) {
        spi_flash_read(FAT1_OFF, fat, sizeof fat);
        c = primero;
        for (n = 0U; n < DATA_CLUSTER_COUNT; n++) {
            uint16_t sig = fat12_entry(fat, c);
            fat12_pack_entry(fat, c + (c / 2U), c, 0x000U);
            if ((sig < 2U) || (sig >= (2U + DATA_CLUSTER_COUNT))) {
                break;                      /* fin de cadena, o cadena rota */
            }
            c = sig;
        }
        fat_vuelca(FAT1_OFF, fat);
        fat_vuelca(FAT2_OFF, fat);
    }
    return 1U;
}
#undef fat

uint8_t spi_flash_recorta(const char name8[8], const char ext3[3], uint32_t bytes)
{
    uint8_t r;

    if (!borrador_coge(BORRADOR_FAT)) {
        return 0U;
    }
    r = recorta_interno(name8, ext3, bytes);
    borrador_suelta(BORRADOR_FAT);
    return r;
}

/* El borrador comun, con el nombre corto que usaba la variable local que
 * habia aqui. Es un #define y no un "uint8_t *fat = s_fat" A PROPOSITO:
 * con el puntero, los sizeof(fat) que hay dentro de la funcion pasan de
 * valer 4096 a valer 4, la FAT se lee entera en 4 bytes, el driver se cree
 * que el volumen esta vacio y escribe encima de lo que haya. Lo hice asi
 * primero y eso es exactamente lo que paso: el banco de FAT lo cazo con
 * "se ha estropeado ZONAALTABIN" y el compilador no dijo ni mu. Con el
 * #define, el nombre sigue siendo un array y no hay nada que recordar. */
#define fat s_fat
static uint8_t recorta_interno(const char name8[8], const char ext3[3], uint32_t bytes)
{
    /* Ver el comentario gordo de los borradores, arriba. */
    uint8_t  tam4[4];
    uint32_t dir_off = 0UL, viejo = 0UL;
    uint16_t primero = 0U;
    uint32_t bloques, quedan, tenia, i;

    if (!geo_lee()) { return 0U; }
    if (!dir_find_entry(name8, ext3, &dir_off, &primero, &viejo)) { return 0U; }
    if (primero < 2U || primero >= (2U + DATA_CLUSTER_COUNT)) { return 0U; }

    bloques = (bytes + FLASH_SECTOR_SIZE - 1UL) / FLASH_SECTOR_SIZE;
    if (bloques == 0UL) { bloques = 1UL; }
    quedan = bloques * CLUSTERS_PER_BLOCK;
    tenia  = (viejo + ROOT_DIR_SECTOR_BYTES - 1UL) / ROOT_DIR_SECTOR_BYTES;
    if (quedan >= tenia) { return 1U; }   /* no sobra nada: nada que hacer */

    spi_flash_read(FAT1_OFF, fat, sizeof fat);
    /* El nuevo ultimo dice fin... */
    fat12_pack_entry(fat, (primero + quedan - 1U) + ((primero + quedan - 1U) / 2U),
                     primero + quedan - 1U, 0x0FFFU);
    /* ...y los de detras vuelven a estar libres. */
    for (i = quedan; i < tenia; i++) {
        uint32_t c = primero + i;
        fat12_pack_entry(fat, c + (c / 2U), c, 0x000U);
    }
    fat_vuelca(FAT1_OFF, fat);
    fat_vuelca(FAT2_OFF, fat);

    /* Y el tamaño, que es lo ultimo: mientras la entrada diga de mas, lo
     * peor que hay es una cola que no se lee. Al reves -el tamaño primero-
     * quedaria un fichero diciendo que tiene datos en clusters que ya
     * estan libres, y eso si es corrupcion. */
    bytes = bloques * FLASH_SECTOR_SIZE;
    tam4[0] = (uint8_t)(bytes & 0xFFU);
    tam4[1] = (uint8_t)((bytes >> 8) & 0xFFU);
    tam4[2] = (uint8_t)((bytes >> 16) & 0xFFU);
    tam4[3] = (uint8_t)((bytes >> 24) & 0xFFU);
    spi_flash_block_read_modify_write(RAIZ_OFF, dir_off + 28U, tam4, 4U);
    return 1U;
}
#undef fat

#include "i2c_bitbang.h"
#include "gd32f4xx.h"
#include "debug_uart.h"

#define I2C_SCL_PORT GPIOB
#define I2C_SCL_PIN  GPIO_PIN_8
#define I2C_SDA_PORT GPIOB
#define I2C_SDA_PIN  GPIO_PIN_9

/*
 * FORCED -O0 (30/07/2026): this loop's timing was never calibrated to
 * an instruction count (see the comment below) - it was only ever
 * "however long 200 NOPs take at whatever optimization level the
 * project happened to build at". When the project-wide CFLAGS moved
 * from -O0 to -O2 (for the demod/filter framerate fix), this same
 * loop got compiled tighter and the real wall-clock delay shrank,
 * slicing into the I2C bit-bang edge timing on a bus shared by BOTH
 * the AIC3204 codec and the MS5351 clock synth - the actual cause of
 * the "espectro muy alto / saltos / ruido continuo" regression (a
 * garbled I2C write during codec or LO setup, not the low-IF/tuning
 * changes that were tried and reverted first). Pinning just this
 * function to -O0 keeps its timing exactly as before, independent of
 * whatever CFLAGS the rest of the project builds at from now on. If
 * this ever needs to change, replace the whole approach with a real
 * timer (see touch.c's g_msticks-based note for the same issue),
 * don't just delete this attribute.
 */
__attribute__((optimize("O0")))
static void delay_i2c(void)
{
    /* ~100kHz aproximado a instruccion, igual de sin calibrar que el
     * delay de touch.c - de sobra de margen para I2C estandar, y no es
     * critico (no estamos cerca del limite de velocidad del bus). */
    volatile uint32_t i;
    for (i = 0; i < 200U; i++) {
        __NOP();
    }
}

static inline void scl(uint8_t level)
{
    gpio_bit_write(I2C_SCL_PORT, I2C_SCL_PIN, level ? SET : RESET);
}

static inline void sda(uint8_t level)
{
    gpio_bit_write(I2C_SDA_PORT, I2C_SDA_PIN, level ? SET : RESET);
}

static inline uint8_t sda_read(void)
{
    return (gpio_input_bit_get(I2C_SDA_PORT, I2C_SDA_PIN) == SET) ? 1 : 0;
}

void i2c_bitbang_init(void)
{
    uint8_t i;

    rcu_periph_clock_enable(RCU_GPIOB);

    gpio_mode_set(I2C_SCL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, I2C_SCL_PIN);
    gpio_output_options_set(I2C_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, I2C_SCL_PIN);
    gpio_mode_set(I2C_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, I2C_SDA_PIN);
    gpio_output_options_set(I2C_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, I2C_SDA_PIN);

    scl(1);
    sda(1);
    delay_i2c();

    debug_print_dec("i2c_bitbang: SCL en reposo (deberia ser 1)",
                     (gpio_input_bit_get(I2C_SCL_PORT, I2C_SCL_PIN) == SET) ? 1 : 0);
    debug_print_dec("i2c_bitbang: SDA en reposo (deberia ser 1)", sda_read());

    if (!sda_read()) {
        /*
         * SDA atascada en bajo - sintoma clasico de un esclavo que se
         * quedo a media transaccion (p.ej. un ACK a medio dar) de un
         * intento anterior (reset previo, glitch de alimentacion, etc.).
         * Recuperacion estandar de bus I2C: generar hasta 9 pulsos de
         * SCL con SDA liberada. Cualquier esclavo que estuviera
         * reteniendo SDA para meter un bit de dato la soltara en algun
         * punto de esos 9 pulsos (el numero maximo de bits de una
         * transaccion I2C), y despues forzamos una condicion STOP limpia
         * para dejar el bus en reposo de verdad.
         */
        debug_print("i2c_bitbang: SDA atascada en bajo, recuperando bus (hasta 9 pulsos SCL)...\n");
        for (i = 0; i < 9; i++) {
            scl(0);
            delay_i2c();
            scl(1);
            delay_i2c();
            if (sda_read()) {
                break;
            }
        }
        /* condicion STOP: SDA sube mientras SCL esta alto */
        sda(0);
        delay_i2c();
        scl(1);
        delay_i2c();
        sda(1);
        delay_i2c();

        debug_print_dec("i2c_bitbang: SDA tras recuperacion (deberia ser 1)", sda_read());
    }
}

static void i2c_start(void)
{
    sda(1);
    scl(1);
    delay_i2c();
    sda(0);
    delay_i2c();
    scl(0);
    delay_i2c();
}

static void i2c_stop(void)
{
    sda(0);
    delay_i2c();
    scl(1);
    delay_i2c();
    sda(1);
    delay_i2c();
}

/* Escribe un byte MSB primero y devuelve 1 si el esclavo respondio ACK. */
static uint8_t i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++) {
        sda((byte & 0x80) ? 1 : 0);
        byte = (uint8_t)(byte << 1);
        delay_i2c();
        scl(1);
        delay_i2c();
        scl(0);
    }

    /* noveno pulso: liberar SDA y leer el ACK del esclavo (0 = ACK) */
    sda(1);
    delay_i2c();
    scl(1);
    delay_i2c();
    ack = (sda_read() == 0) ? 1 : 0;
    scl(0);

    return ack;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0;

    sda(1); /* liberar SDA para que el esclavo pueda conducirla */
    for (i = 0; i < 8; i++) {
        delay_i2c();
        scl(1);
        delay_i2c();
        byte = (uint8_t)((byte << 1) | sda_read());
        scl(0);
    }

    /* noveno pulso: mandamos nosotros el ACK/NACK */
    sda(ack ? 0 : 1);
    delay_i2c();
    scl(1);
    delay_i2c();
    scl(0);
    sda(1);

    return byte;
}

uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint8_t len)
{
    uint8_t i;

    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr << 1))) {
        i2c_stop();
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!i2c_write_byte(data[i])) {
            i2c_stop();
            return 0;
        }
    }
    i2c_stop();
    return 1;
}

uint8_t i2c_read(uint8_t addr, uint8_t *data, uint8_t len)
{
    uint8_t i;

    i2c_start();
    if (!i2c_write_byte((uint8_t)((addr << 1) | 0x01U))) {
        i2c_stop();
        return 0;
    }
    for (i = 0; i < len; i++) {
        data[i] = i2c_read_byte(i < (len - 1)); /* ACK todos menos el ultimo */
    }
    i2c_stop();
    return 1;
}

void i2c_scan(uint8_t addr_from, uint8_t addr_to, void (*on_found)(uint8_t addr))
{
    uint8_t addr;

    for (addr = addr_from; addr <= addr_to; addr++) {
        uint8_t ack;

        i2c_start();
        ack = i2c_write_byte((uint8_t)(addr << 1));
        i2c_stop();

        if (ack && on_found != NULL) {
            on_found(addr);
        }

        if (addr == addr_to) {
            break; /* evita wraparound si addr_to == 0x7F (uint8_t) */
        }
    }
}

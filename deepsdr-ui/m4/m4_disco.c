/*
 * LO QUE CUESTA EL ALMACENAMIENTO Y LOS DATOS, EN INSTRUCCIONES DE UN
 * CORTEX-M4 DE VERDAD.
 *
 * EL PORQUE, Y EN QUE SE DIFERENCIA DEL BANCO DE AUDIO. El camino de
 * audio corre 375 veces por segundo y lo que importa de el es el tanto por
 * ciento del segundo que se lleva. Esto NO: el directorio se recorre al
 * arrancar, CONFIG.CSV se guarda cuando sueltas el mando, la tabla de
 * aviones se consulta cuando entra un avion. Lo que importa aqui es
 * CUANTO DURA UNA VEZ, en milisegundos, y si eso se nota en el audio o en
 * la pantalla mientras pasa.
 *
 * La referencia para leerlo: a 200 MHz, 200.000 instrucciones = 1 ms. Un
 * bloque de audio son 2,67 ms (256 muestras a 96 kHz) y un cuadro de
 * pantalla comodo son 33 ms. O sea:
 *
 *     por debajo de   500.000 instrucciones (2,5 ms) no se ve;
 *     entre 0,5 y 6 millones (2,5 a 30 ms) se come cuadros;
 *     por encima de 6 millones (30 ms) se nota con el dedo.
 *
 * Y todo esto x1,7 para la radio de verdad (ver m4/LEEME.md).
 *
 * ======================================================================
 * LO QUE SE MIDE Y LO QUE NO: EL COSTE DE CPU DEL DRIVER, NO EL DEL CHIP
 * ======================================================================
 *
 * Debajo de User/spi_flash.c no hay un W25Q16: hay un taponamiento, un
 * chip de mentira que vive en un array de 2 MB de la RAM del banco y que
 * habla el mismo protocolo A NIVEL DE PIN -CS, reloj, MOSI y MISO-. La
 * idea esta copiada de sim/w25q_sim.c, pero el codigo es de aqui: el
 * encargo pide un solo fichero.
 *
 * Eso quiere decir que el spi_xfer_byte() del firmware corre DE VERDAD,
 * bit a bit, con sus ocho vueltas por byte. Lo que NO esta, y por eso no
 * se mide, es:
 *
 *   - EL TIEMPO DEL CHIP. Un borrado de 4 kB son 45 ms TIPICOS y 400 de
 *     maximo segun la hoja de caracteristicas, y un programado de pagina
 *     unos 0,7 ms. Aqui el chip contesta "libre" siempre, asi que
 *     spi_wait_busy() vuelve en seguida. En la radio, un guardado de
 *     CONFIG.CSV son esos 45 ms de borrado MAS lo que sale aqui.
 *   - EL COSTE REAL DE MOVER UN PIN. gpio_bit_write() en la radio es un
 *     store al registro BOP del GPIOB; aqui es una llamada que ademas
 *     mete el bit en la maquina de estados del chip de mentira. O sea
 *     que el bit-bang sale MAS CARO aqui que en la placa.
 *
 * Por eso el banco empieza midiendo lo que cuesta UN BYTE por el bus de
 * tres maneras, y cada fila que toca la flash dice ademas cuantos bytes ha
 * movido:
 *
 *   1. con el chip de mentira detras   - lo que se mide en las filas;
 *   2. con el pin como en la radio     - el mismo bit-bang, pero con el
 *      cuerpo del gpio_bit_write() del GD32 (una rama y un store);
 *   3. lo mismo menos el puntero a funcion con el que este banco cambia
 *      de uno a otro, que en la radio no existe y son 32 llamadas por
 *      byte. Ese tercer numero es el de la radio.
 *
 * La columna "ajustado" es:
 *
 *     ajustado = medido - bytes x (1 - 3)
 *
 * o sea lo mismo con el bus de la radio en vez del de aqui. Lo que sigue
 * faltando para la radio de verdad es el x1,7 del LEEME y el tiempo del
 * chip, que no es CPU: durante el borrado la CPU puede hacer otra cosa si
 * se lo monta, y el guardado asincrono de spi_flash.c lo hace.
 *
 * ======================================================================
 * LOS DATOS SON DE VERDAD
 * ======================================================================
 *
 *   - un volumen FAT12 de 1 MB con la distribucion que deja
 *     `mkfs.fat -F 12 -R 4 -f 2 -r 512 -s 1 -S 512` (la misma que usan
 *     los bancos de sim/), con su directorio raiz poblado: CONFIG.CSV,
 *     MEM.CSV, CHANNEL.CSV, un UPDATE.BIN, diez fotos de SSTV y de fax,
 *     dos entradas borradas y un SYSTEM~1 de Windows;
 *   - el CONFIG.CSV NO esta escrito a mano: lo genera el propio
 *     settings.c con build_csv() en el primer guardado, asi que lo que
 *     despues lee settings_load() son sus claves de verdad;
 *   - el MEM.CSV lo genera banco.c con cuatro bancos y memorias puestas;
 *   - una zona alta con su directorio ('DSDR') y una tabla de aviones de
 *     20.000 registros con el formato de tools/icao24.py;
 *   - y el mapa de datos/prueba.map, el mismo que usa sim/mapa_niveles.c,
 *     metido en la zona alta con .incbin. Si ese fichero no esta, se
 *     compila con -DSIN_MAPA_BIN y las dos filas del mapa troceado salen
 *     como "no medido".
 */
#include <stdint.h>
#include <string.h>
#include "semi.h"

#include "gd32f4xx.h"
#include "spi_flash.h"
#include "zona_alta.h"
#include "settings.h"
#include "banco.h"
#include "icao24_db.h"
#include "dxcc.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include "mapa_bin.h"
#include "mapa_eti.h"
#include "touch.h"
#include "spectrum.h"

void pila_di(unsigned long maximo);

int g_fallos;

/* ====================================================================
 * EL CHIP DE MENTIRA, A NIVEL DE PIN
 * ==================================================================== */

#define CHIP_BYTES   (2U * 1024U * 1024U)
#define CHIP_MASC    (CHIP_BYTES - 1U)

static uint8_t s_chip[CHIP_BYTES];

/* Los pines, tal y como los define User/spi_flash.c. */
#define P_SCLK   GPIO_PIN_3
#define P_MISO   GPIO_PIN_4
#define P_MOSI   GPIO_PIN_5
#define P_CS     GPIO_PIN_6

static uint8_t  s_mosi;      /* lo que el maestro tiene puesto en MOSI */
static uint8_t  s_clk;       /* nivel actual del reloj */
static uint8_t  s_sal;       /* el byte de respuesta, desplazandose */
static uint8_t  s_sal_sig;   /* el siguiente, cargado en el flanco de bajada */
static uint8_t  s_carga;     /* hay uno nuevo esperando */
static uint8_t  s_ent;       /* el byte que esta entrando */
static uint8_t  s_nbit;      /* cuantos bits lleva */
static uint32_t s_nbyte;     /* cuantos bytes van desde que bajo el CS */
static uint8_t  s_cmd;
static uint32_t s_dir;       /* la direccion de la orden en curso */
static uint8_t  s_wel;       /* write enable latch */

/* Lo que ha movido el bus, para poder pasar las medidas a la radio. */
static unsigned long s_bus_bytes;

static void chip_borra(uint32_t a, uint32_t n)
{
    uint32_t i;

    a &= CHIP_MASC;
    for (i = 0U; i < n; i++) { s_chip[(a + i) & CHIP_MASC] = 0xFFU; }
}

/* Un byte entero ha entrado. Se decide que contesta el chip en el
 * SIGUIENTE, que es lo que hace un W25Q de verdad: la respuesta va un byte
 * por detras de la orden. */
static void chip_byte(uint8_t b)
{
    uint8_t r = 0xFFU;

    s_bus_bytes++;

    if (s_nbyte == 0U) {
        s_cmd = b;
        s_dir = 0UL;
        switch (b) {
        case 0x9FU: r = 0xEFU; break;                 /* JEDEC: Winbond */
        case 0x05U: r = 0x00U; break;                 /* estado: nunca ocupado */
        case 0x06U: s_wel = 1U; r = 0x00U; break;     /* write enable */
        default:    r = 0xFFU; break;
        }
    } else if (s_cmd == 0x9FU) {
        /* EF 40 15: un W25Q16 de 2 MB (2^21). */
        r = (s_nbyte == 1U) ? 0x40U : 0x15U;
    } else if (s_cmd == 0x05U) {
        r = 0x00U;
    } else if (s_cmd == 0x4BU) {
        /* El numero de serie: cuatro de relleno y ocho de numero. */
        r = (s_nbyte >= 4U) ? (uint8_t)(0xA0U + s_nbyte) : 0x00U;
    } else if (s_cmd == 0x03U || s_cmd == 0x02U ||
               s_cmd == 0x20U || s_cmd == 0xD8U) {
        if (s_nbyte <= 3U) {
            s_dir = (s_dir << 8) | (uint32_t)b;
            if (s_nbyte == 3U) {
                if (s_cmd == 0x20U) {
                    chip_borra(s_dir & ~4095UL, 4096UL);
                    s_wel = 0U;
                } else if (s_cmd == 0xD8U) {
                    chip_borra(s_dir & ~65535UL, 65536UL);
                    s_wel = 0U;
                }
                r = s_chip[s_dir & CHIP_MASC];
            }
        } else if (s_cmd == 0x03U) {
            s_dir++;
            r = s_chip[s_dir & CHIP_MASC];
        } else if (s_cmd == 0x02U) {
            /* Grabar solo BAJA bits, y la pagina de 256 da la vuelta. */
            s_chip[s_dir & CHIP_MASC] &= b;
            s_dir = (s_dir & ~255UL) | ((s_dir + 1UL) & 255UL);
        }
    }

    s_sal_sig = r;
    s_carga = 1U;
    s_nbyte++;
}

static void chip_subida(void)
{
    s_ent = (uint8_t)((s_ent << 1) | s_mosi);
    s_nbit++;
    if (s_nbit == 8U) { chip_byte(s_ent); s_nbit = 0U; s_ent = 0U; }
}

static void chip_bajada(void)
{
    if (s_carga) { s_sal = s_sal_sig; s_carga = 0U; }
    else         { s_sal = (uint8_t)(s_sal << 1); }
}

/*
 * LOS PINES, CON DOS CAMINOS Y UNA RAZON.
 *
 * `s_pin_w`/`s_pin_r` son punteros para poder medir lo mismo DOS veces:
 * una con el chip de mentira detras -que es lo que hace falta para que el
 * driver lea datos que existen- y otra con unos pines tontos que solo
 * guardan en una variable volatil, que es lo mas parecido que hay aqui al
 * store al registro BOP de la radio. La diferencia entre las dos es lo que
 * cuesta el chip de mentira, y eso es lo que se resta para "ajustado".
 *
 * El puntero cuesta lo mismo en los dos caminos, asi que en la resta se va.
 */
static void pin_w_chip(uint32_t pin, uint32_t v)
{
    if (pin == P_MOSI) { s_mosi = (uint8_t)v; return; }
    if (pin == P_SCLK) {
        if (v) { if (!s_clk) { s_clk = 1U; chip_subida(); } }
        else   { if (s_clk)  { s_clk = 0U; chip_bajada(); } }
        return;
    }
    if (pin == P_CS) {
        if (v == 0U) {
            s_nbyte = 0U; s_nbit = 0U; s_ent = 0U;
            s_sal = 0xFFU; s_carga = 0U; s_clk = 0U;
        }
    }
}

/*
 * Y EL OTRO CAMINO: el pin como lo mueve la radio. Esto es, linea por
 * linea, el cuerpo del gpio_bit_write() de Firmware/Source/gd32f4xx_gpio.c
 * -una rama y un store al registro BOP o al BC- con la RAM haciendo de
 * registro. Lo mismo con la lectura, que alli es un AND sobre ISTAT.
 *
 * noinline a proposito: hace falta poder llamarlo DIRECTAMENTE y por
 * puntero y comparar, que es como se mide lo que cuesta el puntero.
 */
static volatile uint32_t s_pin_tonto;
static volatile uint32_t s_bop, s_bc, s_istat = 0xFFFFFFFFUL;

__attribute__((noinline))
static void pin_w_tonto(uint32_t pin, uint32_t v)
{
    if (v != 0UL) { s_bop = pin; } else { s_bc = pin; }
}

static uint32_t pin_r_chip(uint32_t pin)
{
    (void)pin;
    return (uint32_t)((s_sal & 0x80U) ? 1U : 0U);
}

__attribute__((noinline))
static uint32_t pin_r_tonto(uint32_t pin)
{
    return ((s_istat & pin) != 0UL) ? 1UL : 0UL;
}

static void (*s_pin_w)(uint32_t, uint32_t) = pin_w_chip;
static uint32_t (*s_pin_r)(uint32_t) = pin_r_chip;

void gpio_bit_write(uint32_t port, uint32_t pin, bit_status v)
{
    (void)port;
    s_pin_w(pin, (uint32_t)v);
}
void gpio_bit_set(uint32_t port, uint32_t pin) { (void)port; s_pin_w(pin, 1UL); }
void gpio_bit_reset(uint32_t port, uint32_t pin) { (void)port; s_pin_w(pin, 0UL); }
bit_status gpio_input_bit_get(uint32_t port, uint32_t pin)
{
    (void)port;
    return s_pin_r(pin) ? SET : RESET;
}
void gpio_mode_set(uint32_t p, uint32_t m, uint32_t u, uint32_t q)
{ s_pin_tonto = p ^ m ^ u ^ q; }
void gpio_output_options_set(uint32_t p, uint32_t o, uint32_t s, uint32_t q)
{ s_pin_tonto = p ^ o ^ s ^ q; }
void rcu_periph_clock_enable(uint32_t p) { s_pin_tonto = p; }

/* ====================================================================
 * LOS TAPONES DE LOS VECINOS
 *
 * settings.c lee y escribe media radio por sus getters. Ninguno de ellos
 * toca la flash, asi que taparlos no cambia lo que se mide: lo que se mide
 * es armar el texto y moverlo.
 * ==================================================================== */

volatile uint32_t g_msticks;
const palette_t *g_pal = &k_pal_oscura;

static touch_calibration_t s_cal = { 320U, 3760U, 380U, 3700U, 0U, 0U, 1U };

void touch_get_calibration(touch_calibration_t *out) { *out = s_cal; }
void touch_set_calibration(const touch_calibration_t *cal) { s_cal = *cal; }

static uint32_t s_xtal = 25000000UL;
uint32_t ms5351_get_xtal_hz(void) { return s_xtal; }
void ms5351_set_xtal_hz(uint32_t hz) { s_xtal = hz; }

uint8_t backlight_get_percent(void) { return 70U; }

static spectrum_style_t s_estilo = SPECTRUM_STYLE_HEATMAP;
void spectrum_set_style(spectrum_style_t s) { s_estilo = s; }
spectrum_style_t spectrum_get_style(void) { return s_estilo; }

static uint8_t s_blanco = 1U;
void spectrum_set_heatmap_trace_white(uint8_t w) { s_blanco = w; }
uint8_t spectrum_get_heatmap_trace_white(void) { return s_blanco; }

static spectrum_palette_t s_paleta = SPECTRUM_PALETTE_CLASSIC_GREEN;
spectrum_palette_t spectrum_get_palette(void) { return s_paleta; }
const char *spectrum_palette_clave(uint8_t i)
{
    (void)i;
    return "CLASSIC_GREEN";
}
int16_t spectrum_palette_de_clave(const char *s, uint32_t n)
{
    if (n == 13U && s[0] == 'C' && s[1] == 'L') { return 10; }
    return -1;
}

static char s_grid[8] = "IN80DK";
const char *ft8_decoder_get_own_grid(void) { return s_grid; }
void ft8_decoder_set_own_grid(const char *g, int len)
{
    int i;
    for (i = 0; i < len && i < 7; i++) { s_grid[i] = g[i]; }
    s_grid[(i < 7) ? i : 7] = '\0';
}

/* ====================================================================
 * EL VOLUMEN FAT12, MONTADO A MANO COMO LO DEJA EL PC
 * ==================================================================== */

#define BPS        512U
#define SPC        1U
#define RES        4U
#define NFAT       2U
#define FATSEC     6U
#define RAIZ_ENT   512U
#define SECTORES   2048U

#define RAIZ_LBA   (RES + NFAT * FATSEC)              /* 16 */
#define RAIZ_OFF   (RAIZ_LBA * BPS)                   /* 8192 */
#define DATOS_LBA  (RAIZ_LBA + (RAIZ_ENT * 32U) / BPS) /* 48 */
#define DATOS_OFF  (DATOS_LBA * BPS)                  /* 24576 */
#define FAT1_OFF   (RES * BPS)                        /* 2048 */
#define FAT_BYTES  (FATSEC * BPS)                     /* 3072 */
#define CLUSTERS   (SECTORES - DATOS_LBA)             /* 2000 */

static uint32_t s_proximo = 2UL;   /* el cluster libre por donde voy */

static void fat_pon(uint32_t c, uint16_t v)
{
    uint32_t o = c + (c / 2U);
    uint8_t copia;

    for (copia = 0U; copia < NFAT; copia++) {
        uint8_t *f = &s_chip[FAT1_OFF + (uint32_t)copia * FAT_BYTES + o];
        uint16_t p = (uint16_t)((uint16_t)f[0] | ((uint16_t)f[1] << 8));

        p = (c & 1U) ? (uint16_t)((p & 0x000FU) | (uint16_t)(v << 4))
                     : (uint16_t)((p & 0xF000U) | (uint16_t)(v & 0x0FFFU));
        f[0] = (uint8_t)(p & 0xFFU);
        f[1] = (uint8_t)(p >> 8);
    }
}

/* Una entrada de directorio con su cadena seguida, como la deja un PC. */
static void pon_fichero(const char *n11, uint32_t bytes, uint8_t attr,
                        uint8_t relleno)
{
    uint32_t clus = (bytes + BPS - 1U) / BPS;
    uint32_t primero = s_proximo;
    uint32_t off, c;
    uint8_t *e;

    if (clus == 0U) { clus = 1U; }

    for (off = RAIZ_OFF; off < DATOS_OFF; off += 32U) {
        if (s_chip[off] == 0x00U || s_chip[off] == 0xE5U) { break; }
    }
    e = &s_chip[off];
    memcpy(e, n11, 11U);
    e[11] = attr;
    e[22] = 0x00U; e[23] = 0x5CU;            /* hora */
    e[24] = 0x41U; e[25] = 0x59U;            /* fecha */
    e[26] = (uint8_t)(primero & 0xFFU);
    e[27] = (uint8_t)(primero >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);
    e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU);
    e[31] = (uint8_t)((bytes >> 24) & 0xFFU);

    for (c = primero; c < primero + clus; c++) {
        fat_pon(c, (uint16_t)((c + 1U < primero + clus) ? (c + 1U) : 0x0FFFU));
    }
    if (relleno) {
        memset(&s_chip[DATOS_OFF + (primero - 2U) * BPS], relleno, bytes);
    }
    s_proximo = primero + clus;
}

static void borra_entrada(const char *n11)
{
    uint32_t off;

    for (off = RAIZ_OFF; off < DATOS_OFF; off += 32U) {
        if (memcmp(&s_chip[off], n11, 11U) == 0) { s_chip[off] = 0xE5U; return; }
    }
}

static void formatea(void)
{
    uint8_t *b = s_chip;

    memset(s_chip, 0xFF, CHIP_BYTES);
    memset(s_chip, 0x00, DATOS_OFF);

    b[0] = 0xEBU; b[1] = 0x3CU; b[2] = 0x90U;
    memcpy(&b[3], "mkfs.fat", 8U);
    b[11] = (uint8_t)(BPS & 0xFFU); b[12] = (uint8_t)(BPS >> 8);
    b[13] = (uint8_t)SPC;
    b[14] = (uint8_t)RES; b[15] = 0U;
    b[16] = (uint8_t)NFAT;
    b[17] = (uint8_t)(RAIZ_ENT & 0xFFU); b[18] = (uint8_t)(RAIZ_ENT >> 8);
    b[19] = (uint8_t)(SECTORES & 0xFFU); b[20] = (uint8_t)(SECTORES >> 8);
    b[21] = 0xF8U;
    b[22] = (uint8_t)FATSEC; b[23] = 0U;
    b[24] = 32U; b[26] = 2U;                 /* sectores por pista, cabezas */
    b[38] = 0x29U;                           /* firma del BPB extendido */
    memcpy(&b[43], "DEEPSDR    ", 11U);
    memcpy(&b[54], "FAT12   ", 8U);
    b[510] = 0x55U; b[511] = 0xAAU;

    /* Las dos primeras entradas de la FAT, en las dos copias. */
    {
        uint8_t copia;
        for (copia = 0U; copia < NFAT; copia++) {
            uint8_t *f = &s_chip[FAT1_OFF + (uint32_t)copia * FAT_BYTES];
            f[0] = 0xF8U; f[1] = 0xFFU; f[2] = 0xFFU;
        }
    }

    /* La etiqueta del volumen, como la primera entrada de la raiz. */
    {
        uint8_t *e = &s_chip[RAIZ_OFF];
        memcpy(e, "DEEPSDR    ", 11U);
        e[11] = 0x08U;
    }
    s_proximo = 2UL;
}

/*
 * UN DIRECTORIO CON HISTORIA, que es lo que tiene una radio que lleva
 * meses encendida: las fotos de SSTV y de fax que se han ido guardando, el
 * CHANNEL.CSV del firmware de fabrica, una carpeta de Windows y un par de
 * entradas borradas. Lo que NO se pone aqui son CONFIG.CSV y MEM.CSV: esos
 * los escribe el propio firmware mas abajo, para que su contenido sea el de
 * verdad y no uno inventado.
 */
static void puebla(void)
{
    pon_fichero("SYSTEM~1   ", 512U, 0x12U, 0x00U);   /* carpeta oculta */
    pon_fichero("CHANNEL CSV", 727U, 0x20U, 0x41U);
    pon_fichero("UPDATE  BIN", 131072U, 0x20U, 0x5AU);
    pon_fichero("SSTV0001BMP", 32768U, 0x20U, 0xA5U);
    pon_fichero("SSTV0002BMP", 32768U, 0x20U, 0xA5U);
    pon_fichero("SSTV0003BMP", 32768U, 0x20U, 0xA5U);
    pon_fichero("SSTV0004BMP", 32768U, 0x20U, 0xA5U);
    pon_fichero("WFX00001BMP", 40960U, 0x20U, 0x5AU);
    pon_fichero("WFX00002BMP", 40960U, 0x20U, 0x5AU);
    pon_fichero("WFX00003BMP", 40960U, 0x20U, 0x5AU);
    pon_fichero("BORRADO1TXT", 2048U, 0x20U, 0x11U);
    pon_fichero("LOG     TXT", 8192U, 0x20U, 0x22U);
    pon_fichero("BORRADO2DAT", 4096U, 0x20U, 0x33U);
    pon_fichero("NOTAS   TXT", 1024U, 0x20U, 0x44U);
    borra_entrada("BORRADO1TXT");
    borra_entrada("BORRADO2DAT");
}

/* ====================================================================
 * LA ZONA ALTA: DIRECTORIO, TABLA DE AVIONES Y MAPA
 * ==================================================================== */

#define I24_N        20000UL
#define I24_TIPOS    200UL
#define I24_NOMBRE   24UL
#define I24_TAM      (16UL + 5UL * I24_N + I24_TIPOS * I24_NOMBRE)

#ifndef SIN_MAPA_BIN
/*
 * EL MAPA, METIDO CON .incbin Y NO ESCRITO A MANO. Son 70 kB; ponerlos
 * como un array de C en este fichero serian 400 kB de fuente para algo
 * que ya esta en el arbol. El fichero es el mismo que usa
 * sim/mapa_niveles.c, asi que lo que se mide es el mapa de verdad.
 *
 * La ruta es relativa al directorio desde el que se compila
 * (deepsdr-ui). Sin el fichero, -DSIN_MAPA_BIN deja fuera las dos filas
 * del mapa troceado y el banco lo dice.
 */
__asm__(".section .rodata\n"
        ".balign 4\n"
        ".globl k_mapa_blob\n"
        "k_mapa_blob:\n"
        ".incbin \"datos/prueba.map\"\n"
        "k_mapa_blob_fin:\n"
        ".globl k_mapa_blob_n\n"
        ".balign 4\n"
        "k_mapa_blob_n: .word k_mapa_blob_fin - k_mapa_blob\n"
        ".text\n");
extern const uint8_t k_mapa_blob[];
extern const uint32_t k_mapa_blob_n;
#endif

static uint32_t za_dir_off;   /* donde empieza la tabla de aviones */

static void pon_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void puebla_zona_alta(void)
{
    uint32_t base = za_base();
    uint32_t i;
    uint8_t *d;

    /* --- la tabla de aviones, con el formato de tools/icao24.py --- */
    za_dir_off = ZA_CAB;                       /* 4096 */
    d = &s_chip[base + za_dir_off];
    d[0] = 'I'; d[1] = '2'; d[2] = '4'; d[3] = 'B'; d[4] = 1U;
    d[5] = (uint8_t)I24_NOMBRE;
    d[6] = (uint8_t)(I24_TIPOS & 0xFFU);
    d[7] = (uint8_t)(I24_TIPOS >> 8);
    pon_u32(&d[8], I24_N);
    pon_u32(&d[12], 16UL + 5UL * I24_N);       /* donde empiezan los nombres */
    for (i = 0UL; i < I24_N; i++) {
        uint32_t k = 0x000100UL + i * 200UL;   /* ordenada, que es lo que exige la busqueda binaria */
        uint8_t *r = &d[16UL + 5UL * i];
        uint16_t t = (uint16_t)(i % I24_TIPOS);

        r[0] = (uint8_t)(k >> 16); r[1] = (uint8_t)(k >> 8); r[2] = (uint8_t)k;
        r[3] = (uint8_t)(t & 0xFFU); r[4] = (uint8_t)(t >> 8);
    }
    for (i = 0UL; i < I24_TIPOS; i++) {
        uint8_t *n = &d[16UL + 5UL * I24_N + i * I24_NOMBRE];
        memcpy(n, "Boeing 737-800          ", I24_NOMBRE);
        n[0] = (uint8_t)('A' + (i % 26UL));
    }

    /* --- el mapa, donde lo pone el firmware --- */
#ifndef SIN_MAPA_BIN
    memcpy(&s_chip[za_mapa()], k_mapa_blob, k_mapa_blob_n);
#endif

    /* --- y el directorio 'DSDR' que dice donde esta cada cosa --- */
    {
        uint8_t *c = &s_chip[base];
        uint32_t tam = za_dir_off + I24_TAM;
        uint32_t v;

        memset(c, 0, 16U + 16U);
        c[0] = 'D'; c[1] = 'S'; c[2] = 'D'; c[3] = 'R';
        c[4] = 1U; c[5] = 1U;                 /* version, una seccion */
        pon_u32(&c[8], tam);
        pon_u32(&c[16], ZA_TIPO_AVIONES);
        pon_u32(&c[20], za_dir_off);
        pon_u32(&c[24], I24_TAM);
        pon_u32(&c[28], zona_alta_suma(&s_chip[base + za_dir_off], I24_TAM,
                                       ZA_SUMA_INICIO));
        v = zona_alta_suma(c, 12U, ZA_SUMA_INICIO);
        v = zona_alta_suma(&c[16], 16U, v);
        pon_u32(&c[12], v);
    }
}

/* ====================================================================
 * LA MEDIDA
 * ==================================================================== */

/* Lo que cuesta un byte por el bus: con el chip de mentira detras, con los
 * pines de la radio, y lo que de eso se lleva el puntero a funcion con el
 * que el banco cambia de uno a otro. Se rellenan en calibra(). */
static unsigned long s_caro, s_barato, s_radio;

static unsigned long dur(unsigned long a, unsigned long b, unsigned long n)
{
    return (((a - b) & 0xFFFFFFUL) * 40UL) / n;
}

static void pr_ms(unsigned long ins)
{
    unsigned long us = ins / 200UL;

    prn(us / 1000UL); pr(",");
    prn((us % 1000UL) / 100UL);
    prn((us % 100UL) / 10UL);
    prn(us % 10UL);
    pr(" ms");
}

static void rellena(const char *q)
{
    char t[40];
    unsigned i = 0U;

    while (q[i] != '\0' && i < 36U) { t[i] = q[i]; i++; }
    while (i < 36U) { t[i] = ' '; i++; }
    t[i] = '\0';
    pr("  "); pr(t);
}

/*
 * Una fila: lo medido, los bytes que ha movido por el bus, y lo mismo
 * descontando lo que el chip de mentira cobra de mas por esos bytes.
 */
static unsigned long fila(const char *que, unsigned long a, unsigned long b,
                          unsigned long n, unsigned long bytes)
{
    unsigned long ins = dur(a, b, n);
    unsigned long por = bytes / ((n == 0UL) ? 1UL : n);
    unsigned long sobra = por * ((s_caro > s_radio) ? (s_caro - s_radio) : 0UL);
    unsigned long aj = (ins > sobra) ? (ins - sobra) : 0UL;

    rellena(que);
    prn(ins); pr(" ins = "); pr_ms(ins);
    if (bytes != 0UL) {
        pr("   | bus "); prn(por); pr(" B  -> ajustado ");
        prn(aj); pr(" ins = "); pr_ms(aj);
    }
    pr("\n");
    return aj;
}

/*
 * Que la operacion de verdad HIZO lo que iba a hacer. No es un tope: es
 * que una llamada que se vuelve por la puerta -sin sitio, geometria que no
 * vale, fichero que no esta- sale baratisima, y una tabla llena de esas
 * mediria el camino de no hacer nada.
 */
static void salio(const char *que, int bien)
{
    pr("      -> "); pr(que); pr(": ");
    pr(bien ? "hecho" : "NO SE HIZO (la fila de arriba no vale)");
    pr("\n");
}

static unsigned long s_bus0;
static void bus_cero(void) { s_bus0 = s_bus_bytes; }
static unsigned long bus_va(void) { return s_bus_bytes - s_bus0; }

/* ------------------------------------------------------------------ */

static uint8_t s_buf[4096];
static char s_txt[2560];
static settings_loaded_t s_set;
static settings_extra_t s_extra;
static mapa_caja_t s_caja;
static mapa_gesto_t s_gesto;

static void pinta_mapa(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &s_caja);
}

static void pinta_eti(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &s_caja);
    mapa_eti_dibuja(s);
}

/* La ventana de la pantalla del mapa en centesimas de grado, igual que
 * sim/mapa_niveles.c. 800x318 px, 2,2222 px por grado al zoom 1. */
static void ventana(uint8_t esc, int32_t lon_c, int32_t lat_c,
                    int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t w = (int32_t)(80000.0f / (2.2222f * (float)esc));
    int32_t h = (int32_t)(31800.0f / (2.2222f * (float)esc));

    *x0 = lon_c - w / 2; *x1 = lon_c + w / 2;
    *y0 = lat_c - h / 2; *y1 = lat_c + h / 2;
}

static void calibra(void)
{
    unsigned long a, b;
    int r;

    /* Con el chip detras: lo que cuesta de verdad cada byte de los numeros
     * de abajo. 512 bytes es un sector. */
    a = tic(); for (r = 0; r < 8; r++) { spi_flash_read(0UL, s_buf, 512U); } b = tic();
    s_caro = dur(a, b, 8UL * 512UL);

    /* Con los pines tontos: el bit-bang del driver y nada mas, que es lo
     * mas parecido que hay aqui a la radio. Lee basura a proposito; lo que
     * se mide es el camino, no el dato. */
    s_pin_w = pin_w_tonto;
    s_pin_r = pin_r_tonto;
    a = tic(); for (r = 0; r < 8; r++) { spi_flash_read(0UL, s_buf, 512U); } b = tic();
    s_barato = dur(a, b, 8UL * 512UL);

    /*
     * Y lo que cuesta el puntero a funcion con el que el banco cambia de
     * un camino a otro. En la radio no hay puntero: spi_flash.c llama a
     * gpio_bit_write() directamente. Son 32 llamadas por byte -cuatro por
     * bit-, asi que lo que cueste el salto indirecto se multiplica.
     */
    {
        unsigned long indir, directo, u;

        a = tic();
        for (u = 0UL; u < 2000UL; u++) {
            s_pin_w(P_MOSI, 1UL); s_pin_w(P_SCLK, 1UL);
            (void)s_pin_r(P_MISO); s_pin_w(P_SCLK, 0UL);
        }
        b = tic();
        indir = dur(a, b, 2000UL * 4UL);

        a = tic();
        for (u = 0UL; u < 2000UL; u++) {
            pin_w_tonto(P_MOSI, 1UL); pin_w_tonto(P_SCLK, 1UL);
            (void)pin_r_tonto(P_MISO); pin_w_tonto(P_SCLK, 0UL);
        }
        b = tic();
        directo = dur(a, b, 2000UL * 4UL);

        /* Y ahora si, el chip de vuelta: lo de arriba tenia que correr con
         * los mismos pines tontos de la medida de s_barato, o la
         * diferencia saldria siendo la maquina de estados del chip. */
        s_pin_w = pin_w_chip;
        s_pin_r = pin_r_chip;

        s_radio = (indir > directo)
                ? (s_barato - 32UL * (indir - directo))
                : s_barato;

        pr("  Un byte por el bus SPI, bit-bang del driver incluido:\n");
        pr("    con el chip de mentira detras      "); prn(s_caro);
        pr(" instrucciones/byte\n");
        pr("    con el pin como en la radio        "); prn(s_barato);
        pr(" instrucciones/byte\n");
        pr("    menos el puntero del banco ("); prn(indir - directo);
        pr("/llamada x32)  "); prn(s_radio);
        pr(" instrucciones/byte  <- la radio\n");
        pr("  La columna \"ajustado\" es lo medido menos los bytes del bus\n");
        pr("  por la diferencia entre la primera fila y la ultima.\n\n");
    }
}

int main(void)
{
    unsigned long a, b;
    int r;
    uint32_t i;

    /*
     * SE QUITA UNALIGN_TRP, Y SOLO ESE BIT.
     *
     * m4/start.c enciende DIV_0_TRP y UNALIGN_TRP para que un acceso
     * desalineado se pare en seco con el registro delante. Esta bien para
     * los bancos de calculo, pero aqui no se puede: el memcpy() de la
     * biblioteca -que lo usan este fichero montando el volumen, banco.c y
     * mapa_bin.c- mueve los bytes de cuatro en cuatro en cuanto puede, y
     * una entrada de directorio FAT empieza en el byte 43 de un sector. En
     * la radio eso es LEGAL: el firmware no pone UNALIGN_TRP y el Cortex-M4
     * resuelve el acceso desalineado a la RAM el solo. Dejarlo puesto aqui
     * mediria un fallo que en la placa no existe.
     *
     * DIV_0_TRP se queda: esa trampa no la salta nadie por hacer su
     * trabajo. Y se quita DESPUES de que start.c la ponga, o sea aqui.
     */
    *(volatile uint32_t *)0xE000ED14UL &= ~(1UL << 3);
    __asm__ volatile("dsb"); __asm__ volatile("isb");

    reloj_on();

    pr("\nEL ALMACENAMIENTO Y LOS DATOS, EN UN CORTEX-M4 DE VERDAD\n");
    pr("1 ms = 200.000 instrucciones a 200 MHz. En la radio, x1,7.\n\n");

    /* --- el volumen y la zona alta, montados --- */
    formatea();
    puebla();

    spi_flash_init();
    puebla_zona_alta();

    pr("Volumen FAT12 de 1 MB, 2.000 clusters de 512 B, 512 entradas de\n");
    pr("raiz. El bus rapido quedo ");
    pr(spi_flash_bus_rapido() ? "PUESTO" : "DESPACIO");
    pr(".\n\n");

    calibra();

    /* --- y los dos ficheros del firmware, escritos por el firmware --- */
    {
        for (i = 0U; i < SET_X_N; i++) { s_extra.v[i] = (int32_t)(i + 1U); }
        s_extra.presentes = ~(uint64_t)0;
        settings_save_now(14074000UL, DEMOD_MODE_USB, 100UL, AUDIO_BW_2K3,
                          -24, 0U, 20, 30U, 1U, 1U, 2U, &s_extra);

        banco_init();
        for (i = 0U; i < BANCO_N; i++) {
            uint8_t k;
            banco_nombre_pon((uint8_t)i, "Aficionados");
            for (k = 0U; k < BANCO_MEM_N; k++) {
                banco_mem_t m;
                m.hz = 7000000UL + (uint32_t)i * 1000000UL + (uint32_t)k * 5000UL;
                m.modo = 3U; m.paso = 2U;
                m.fil_lo = 300U; m.fil_hi = 2700U; m.cw = 0U;
                (void)banco_graba((uint8_t)i, k, &m);
            }
        }
        (void)banco_guarda(s_txt, sizeof s_txt);
    }

    /* ================================================================
     * 1. EL VOLUMEN: FAT, DIRECTORIO Y FICHEROS
     * ================================================================ */
    pr("1. EL VOLUMEN FAT12 (User/spi_flash.c)\n\n");

    {
        uint32_t f1, rz, dl, cl;

        spi_flash_geo_olvida();
        bus_cero();
        a = tic(); (void)spi_flash_geometria(&f1, &rz, &dl, &cl); b = tic();
        (void)fila("leer el sector de arranque", a, b, 1UL, bus_va());
    }

    {
        /* Ya leida: las demas llamadas no vuelven al chip. */
        uint32_t f1, rz, dl, cl;
        bus_cero();
        a = tic(); for (r = 0; r < 50; r++) { (void)spi_flash_geometria(&f1, &rz, &dl, &cl); } b = tic();
        (void)fila("la misma, ya cacheada", a, b, 50UL, bus_va());
    }

    {
        spi_flash_fat_scan_t sc;
        bus_cero();
        a = tic(); spi_flash_probe_fat_scan(&sc); b = tic();
        (void)fila("recorrer la FAT entera", a, b, 1UL, bus_va());
        pr("      (");
        prn(sc.free_data_clusters); pr(" clusters libres de ");
        prn(sc.total_data_clusters); pr(")\n");
    }

    {
        bus_cero();
        a = tic(); for (r = 0; r < 4; r++) { (void)spi_flash_fichero_hay("SSTV0003", "BMP"); } b = tic();
        (void)fila("buscar un fichero que esta", a, b, 4UL, bus_va());
    }

    {
        bus_cero();
        a = tic(); for (r = 0; r < 4; r++) { (void)spi_flash_fichero_hay("NOHAY   ", "XXX"); } b = tic();
        (void)fila("buscar uno que NO esta", a, b, 4UL, bus_va());
    }

    {
        bus_cero();
        a = tic(); for (r = 0; r < 2; r++) { (void)spi_flash_dir_max_indice("SSTV", "BMP"); } b = tic();
        (void)fila("recorrer la raiz entera", a, b, 2UL, bus_va());
    }

    {
        uint32_t tam = 0UL;
        bus_cero();
        a = tic(); for (r = 0; r < 4; r++) { (void)spi_flash_fichero_tam("UPDATE  ", "BIN", &tam); } b = tic();
        (void)fila("el tamano de un fichero", a, b, 4UL, bus_va());
    }

    {
        bus_cero();
        a = tic(); for (r = 0; r < 2; r++) {
            (void)spi_flash_read_file_by_name("CHANNEL ", "CSV", s_buf, sizeof s_buf);
        } b = tic();
        (void)fila("leer un fichero de 727 B", a, b, 2UL, bus_va());
    }

    {
        bus_cero();
        a = tic(); (void)spi_flash_read_file_by_name("SSTV0001", "BMP", s_buf, sizeof s_buf); b = tic();
        (void)fila("leer 4 kB de uno de 32 kB", a, b, 1UL, bus_va());
    }

    {
        uint32_t addr = 0UL;
        spi_anade_r_t porque;
        bus_cero();
        uint8_t ok;
        a = tic(); ok = spi_flash_reserva("RESERVA ", "DAT", 65536UL, &addr, &porque); b = tic();
        (void)fila("reservar 64 kB seguidos", a, b, 1UL, bus_va());
        salio("reserva", ok);
    }

    {
        spi_anade_r_t porque;
        memset(s_buf, 0x7EU, sizeof s_buf);
        bus_cero();
        uint8_t ok;
        a = tic(); ok = spi_flash_anade_trozo("TROZOS  ", "BIN", s_buf, &porque); b = tic();
        (void)fila("anadir el PRIMER trozo 4 kB", a, b, 1UL, bus_va());
        salio("primer trozo", ok);

        bus_cero();
        ok = 1U;
        a = tic(); for (r = 0; r < 4; r++) {
            ok &= spi_flash_anade_trozo("TROZOS  ", "BIN", s_buf, &porque);
        } b = tic();
        (void)fila("anadir otro trozo de 4 kB", a, b, 4UL, bus_va());
        salio("los cuatro trozos", ok);
    }

    {
        /* El camino que de verdad usa el firmware para guardar: borrar el
         * bloque y volverlo a escribir. */
        memset(s_buf, 0x5AU, 1536U);
        bus_cero();
        int ok;
        a = tic(); ok = spi_flash_write_or_update_file("PRUEBA  ", "CSV", s_buf, 1536U); b = tic();
        (void)fila("crear un fichero de 1,5 kB", a, b, 1UL, bus_va());
        salio("creado", ok);

        bus_cero();
        a = tic(); for (r = 0; r < 2; r++) {
            (void)spi_flash_write_or_update_file("PRUEBA  ", "CSV", s_buf, 1536U);
        } b = tic();
        (void)fila("reescribirlo (el caso normal)", a, b, 2UL, bus_va());
    }

    {
        bus_cero();
        uint8_t ok;
        a = tic(); ok = spi_flash_fichero_borra("PRUEBA  ", "CSV"); b = tic();
        (void)fila("borrar un fichero", a, b, 1UL, bus_va());
        salio("borrado", ok);
    }

    {
        uint32_t lkb = 0UL, skb = 0UL;
        uint16_t hue = 0U, ran = 0U;
        bus_cero();
        a = tic(); (void)spi_flash_sitio(&lkb, &skb, &hue, &ran); b = tic();
        (void)fila("cuanto sitio queda", a, b, 1UL, bus_va());
    }

    /* ================================================================
     * 2. LOS AJUSTES
     * ================================================================ */
    pr("\n2. LOS AJUSTES (User/settings.c)\n\n");

    {
        bus_cero();
        uint8_t ok = 1U;
        a = tic(); for (r = 0; r < 2; r++) { ok &= settings_load(&s_set); } b = tic();
        (void)fila("settings_load() entero", a, b, 2UL, bus_va());
        salio("CONFIG.CSV leido y entendido", ok && s_set.have_vfo_hz);
    }

    {
        unsigned long con, sin_arm;
        uint32_t n;

        n = spi_flash_read_file_by_name("CONFIG  ", "CSV", s_buf, sizeof s_buf);
        pr("      (el CONFIG.CSV que genera settings.c mide ");
        prn(n); pr(" bytes, ");
        /* Que no se haya quedado a medias en el buffer de 2112 de
         * settings.c: un CSV cortado se leeria mas barato de la cuenta y
         * la medida de al lado valdria menos. */
        pr((n > 0U && s_buf[n - 1U] == (uint8_t)'\n') ? "entero" : "CORTADO");
        pr(")\n");

        bus_cero();
        a = tic(); for (r = 0; r < 2; r++) {
            settings_save_now(14074000UL, DEMOD_MODE_USB, 100UL, AUDIO_BW_2K3,
                              -24, 0U, 20, 30U, 1U, 1U, 2U, &s_extra);
        } b = tic();
        con = fila("settings_save_now() entero", a, b, 2UL, bus_va());

        bus_cero();
        a = tic(); for (r = 0; r < 2; r++) {
            (void)spi_flash_write_or_update_file("CONFIG  ", "CSV", s_buf, n);
        } b = tic();
        sin_arm = fila("  de eso, escribirlo", a, b, 2UL, bus_va());

        /*
         * Y EL CAMINO QUE DE VERDAD USA LA RADIO AL SOLTAR EL MANDO: el
         * guardado a trozos. settings_save_now() es el bloqueante -lo usa
         * el asistente de calibracion del tactil-; settings_poll() arranca
         * este y lo va empujando un paso por vuelta del bucle principal.
         * Lo que decide si se ve o no es EL PASO MAS LARGO, no el total.
         */
        {
            spi_flash_async_status_t st;
            unsigned long peor = 0UL, total = 0UL, pasos = 0UL, t;

            if (spi_flash_async_save_start("CONFIG  ", "CSV", s_buf, n)) {
                bus_cero();
                do {
                    a = tic(); st = spi_flash_async_save_poll(); b = tic();
                    t = dur(a, b, 1UL);
                    total += t;
                    if (t > peor) { peor = t; }
                    pasos++;
                } while (st == SPI_FLASH_ASYNC_BUSY && pasos < 4000UL);

                rellena("guardado A TROZOS, todo junto");
                prn(total); pr(" ins = "); pr_ms(total);
                pr("   | bus "); prn(bus_va()); pr(" B\n");
                rellena("  el paso MAS LARGO de los ");
                prn(pasos); pr("\n");
                rellena("  (eso es lo que se ve)");
                prn(peor); pr(" ins = "); pr_ms(peor); pr("\n");
                salio("terminado bien", st == SPI_FLASH_ASYNC_DONE);
            } else {
                salio("arrancar el guardado a trozos", 0);
            }
        }

        rellena("  de eso, ARMAR el texto");
        prn((con > sin_arm) ? (con - sin_arm) : 0UL);
        pr(" ins = "); pr_ms((con > sin_arm) ? (con - sin_arm) : 0UL);
        pr("   (por diferencia: build_csv() es static)\n");
    }

    /* ================================================================
     * 3. LOS BANCOS
     * ================================================================ */
    pr("\n3. LOS BANCOS DE MEMORIAS (User/banco.c)\n\n");

    {
        uint32_t n;

        bus_cero();
        uint8_t ok = 1U;
        a = tic(); for (r = 0; r < 2; r++) { ok &= banco_carga(s_txt, sizeof s_txt); } b = tic();
        (void)fila("banco_carga() entero", a, b, 2UL, bus_va());
        salio("MEM.CSV leido y entendido", ok && banco_usadas(0U) == BANCO_MEM_N);

        bus_cero();
        ok = 1U;
        a = tic(); for (r = 0; r < 2; r++) { ok &= banco_guarda(s_txt, sizeof s_txt); } b = tic();
        (void)fila("banco_guarda() entero", a, b, 2UL, bus_va());
        salio("MEM.CSV escrito", ok);

        a = tic(); for (r = 0; r < 20; r++) { n = banco_a_csv(s_txt, sizeof s_txt); } b = tic();
        (void)fila("  solo armar el CSV", a, b, 20UL, 0UL);
        pr("      (el MEM.CSV mide "); prn(n); pr(" bytes)\n");

        a = tic(); for (r = 0; r < 20; r++) { (void)banco_de_csv(s_txt, n); } b = tic();
        (void)fila("  solo entender el CSV", a, b, 20UL, 0UL);
    }

    /* ================================================================
     * 4. LAS DOS TABLAS QUE SE CONSULTAN POR CADA AVION Y CADA INDICATIVO
     * ================================================================ */
    pr("\n4. LAS BUSQUEDAS (User/icao24_db.c, User/dxcc.c)\n\n");

    {
        char nombre[40];
        int ok;

        salio("el directorio de la zona alta",
              zona_alta_abre(spi_flash_read) ? 1 : 0);

        /* La primera lleva ademas abrir el fichero. */
        bus_cero();
        a = tic(); (void)icao24_db_lookup(0x000100UL + 10000UL * 200UL, nombre, sizeof nombre); b = tic();
        (void)fila("el PRIMER avion (abre+busca)", a, b, 1UL, bus_va());

        bus_cero();
        ok = 1;
        a = tic(); for (r = 0; r < 20; r++) {
            ok &= icao24_db_lookup(0x000100UL + (uint32_t)r * 937UL * 200UL % (I24_N * 200UL),
                                   nombre, sizeof nombre) ? 1 : 0;
        } b = tic();
        (void)fila("un avion que ESTA", a, b, 20UL, bus_va());
        salio("los 20 encontrados, con modelo", ok);

        bus_cero();
        a = tic(); for (r = 0; r < 20; r++) {
            (void)icao24_db_lookup(0x000101UL + (uint32_t)r * 311UL, nombre, sizeof nombre);
        } b = tic();
        (void)fila("un avion que NO esta", a, b, 20UL, bus_va());
        pr("      (busqueda binaria sobre "); prn(I24_N);
        pr(" aviones: unas 15 lecturas de 5 B)\n");
    }

    {
        static const char *const k_ind[8] = {
            "EA4ABC", "G4VNM", "JA1XYZ", "W1AW", "EA8URL",
            "DL1ABC", "VK6ZZZ", "ZZ9QQQ"
        };
        const char *p = 0;

        a = tic(); for (r = 0; r < 40; r++) { p = dxcc_pais(k_ind[r & 7]); } b = tic();
        (void)fila("dxcc_pais(), un indicativo", a, b, 40UL, 0UL);
        pr("      (");
        prn(dxcc_cuantos()); pr(" prefijos, recorridos ENTEROS: gana el mas largo)\n");
        (void)p;
    }

    /* ================================================================
     * 5. EL MAPA
     * ================================================================ */
    pr("\n5. EL MAPA (User/mapa.c, mapa_bin.c, mapa_eti.c)\n\n");

    mapa_pon(&s_caja, 0, 0, 800, 318);

    {
        a = tic(); for (r = 0; r < 20; r++) { (void)mapa_ventana(&s_caja); } b = tic();
        (void)fila("mapa_ventana(), una vez por cuadro", a, b, 20UL, 0UL);
    }

    {
        int16_t px, py;
        a = tic(); for (r = 0; r < 200; r++) {
            (void)mapa_grados_a_px(&s_caja, -3.7f, 40.4f, &px, &py);
        } b = tic();
        (void)fila("colocar un punto (grados)", a, b, 200UL, 0UL);

        a = tic(); for (r = 0; r < 200; r++) {
            (void)mapa_e4_a_px(&s_caja, -37000L, 404000L, &px, &py);
        } b = tic();
        (void)fila("colocar un avion (grados e4)", a, b, 200UL, 0UL);

        a = tic(); for (r = 0; r < 200; r++) {
            (void)mapa_loc_a_px(&s_caja, "JO20HI", &px, &py);
        } b = tic();
        (void)fila("colocar un localizador", a, b, 200UL, 0UL);
    }

    {
        int32_t lo, la;
        a = tic(); for (r = 0; r < 200; r++) {
            (void)mapa_loc_a_cent("JO20HI", &lo, &la);
        } b = tic();
        (void)fila("un localizador a grados", a, b, 200UL, 0UL);
    }

    {
        mapa_gesto_empieza(&s_gesto, 400, 150);
        a = tic(); for (r = 0; r < 100; r++) {
            int16_t dx, dy;
            (void)mapa_gesto_mueve(&s_gesto, (int16_t)(400 + (r & 15)), 150, &dx, &dy);
        } b = tic();
        (void)fila("un paso de arrastre", a, b, 100UL, 0UL);
    }

    {
        (void)mapa_ventana(&s_caja);
        bus_cero();
        a = tic(); gfx2_render(0, 0, 800, 318, pinta_mapa, 0); b = tic();
        (void)fila("PINTAR el mapa entero, zoom 1", a, b, 1UL, bus_va());
    }

#ifndef SIN_MAPA_BIN
    {
        mapa_bin_r_t rr;
        int32_t x0, y0, x1, y1;

        /* El mapa, tal y como lo abre la radio: desde la flash. */
        bus_cero();
        a = tic(); rr = mapa_bin_abre_flash(spi_flash_read, za_mapa(), k_mapa_blob_n); b = tic();
        (void)fila("abrir el mapa de la flash", a, b, 1UL, bus_va());
        if (rr != MAPA_BIN_OK) {
            pr("      *** el mapa no vale: "); pr(mapa_bin_porque_txt(rr)); pr("\n");
        } else {
            mapa_bin_fuente(spi_flash_read, za_mapa());

            ventana(4U, 1000L, 4500L, &x0, &y0, &x1, &y1);
            bus_cero();
            a = tic(); (void)mapa_bin_ventana(4U, x0, y0, x1, y1); b = tic();
            (void)fila("cargar la ventana al zoom 4", a, b, 1UL, bus_va());

            ventana(8U, 1000L, 4500L, &x0, &y0, &x1, &y1);
            bus_cero();
            a = tic(); (void)mapa_bin_ventana(8U, x0, y0, x1, y1); b = tic();
            (void)fila("cargar la ventana al zoom 8", a, b, 1UL, bus_va());

            /* Y arrastrando un pixel: casi siempre NO recarga. */
            bus_cero();
            a = tic(); for (r = 0; r < 20; r++) {
                ventana(8U, 1000L + (int32_t)r, 4500L, &x0, &y0, &x1, &y1);
                (void)mapa_bin_ventana(8U, x0, y0, x1, y1);
            } b = tic();
            (void)fila("la misma, arrastrando 1 px", a, b, 20UL, bus_va());

            (void)mapa_zoom(&s_caja, 4U, 400, 150);
            (void)mapa_ventana(&s_caja);
            bus_cero();
            a = tic(); gfx2_render(0, 0, 800, 318, pinta_mapa, 0); b = tic();
            (void)fila("PINTAR, zoom 4, CON tierra", a, b, 1UL, bus_va());
            (void)mapa_zoom(&s_caja, 1U, 400, 150);
            (void)mapa_ventana(&s_caja);

            bus_cero();
            a = tic(); gfx2_render(0, 0, 800, 318, pinta_mapa, 0); b = tic();
            (void)fila("PINTAR, zoom 1, CON tierra", a, b, 1UL, bus_va());
        }
    }
#else
    pr("  El mapa troceado NO SE HA MEDIDO: compilado con -DSIN_MAPA_BIN\n");
    pr("  (falta datos/prueba.map).\n");
#endif

    {
        static const char *const k_call[12] = {
            "EA4ABC", "G4VNM", "ON4WS", "K1ABC", "JA1XYZ", "LU2AB",
            "VK6ZZ", "ZL3QQ", "SV1AA", "DL9XX", "I0ABC", "F5XYZ"
        };
        static const char *const k_loc[12] = {
            "IN80DK", "IO90XX", "JO20HI", "FN42AA", "PM95BB", "FF95CC",
            "OF88DD", "RF80EE", "KM56FF", "JO62GG", "JN61HH", "JN18II"
        };
        mapa_eti_dx_t dx;
        uint8_t k;

        a = tic(); for (r = 0; r < 10; r++) {
            mapa_eti_empieza(&s_caja, "IN80DK");
            for (k = 0U; k < 12U; k++) {
                memset(&dx, 0, sizeof dx);
                memcpy(dx.call, k_call[k], strlen(k_call[k]) + 1U);
                memcpy(dx.loc, k_loc[k], 7U);
                dx.snr = -12; dx.minuto = (uint16_t)(600U + k);
                (void)mapa_eti_pon(&dx);
            }
            mapa_eti_cierra();
        } b = tic();
        (void)fila("COLOCAR 12 etiquetas sin pisarse", a, b, 10UL, 0UL);

        bus_cero();
        a = tic(); gfx2_render(0, 0, 800, 318, pinta_eti, 0); b = tic();
        (void)fila("  y PINTARLAS, con el mapa debajo", a, b, 1UL, bus_va());

        a = tic(); for (r = 0; r < 100; r++) { (void)mapa_eti_toca(400, 150); } b = tic();
        (void)fila("BUSCAR que etiqueta se ha tocado", a, b, 100UL, 0UL);
    }

    /* ================================================================
     * 6. LA ZONA ALTA
     * ================================================================ */
    pr("\n6. LA ZONA ALTA (User/zona_alta.c)\n\n");

    {
        uint32_t ad, tm;

        bus_cero();
        a = tic(); for (r = 0; r < 10; r++) { (void)zona_alta_abre(spi_flash_read); } b = tic();
        (void)fila("abrir el directorio", a, b, 10UL, bus_va());

        bus_cero();
        a = tic(); for (r = 0; r < 100; r++) {
            (void)zona_alta_donde(ZA_TIPO_AVIONES, &ad, &tm);
        } b = tic();
        (void)fila("buscar una seccion", a, b, 100UL, bus_va());

        bus_cero();
        uint8_t ok;
        a = tic(); ok = zona_alta_verifica(); b = tic();
        (void)fila("comprobar la suma de la tabla", a, b, 1UL, bus_va());
        salio("la suma cuadra", ok);
        pr("      (");
        prn(I24_TAM / 1024UL); pr(" kB leidos byte a byte por el bus)\n");
    }

    pr("\n");
    pila_di(0UL);
    pr("\n");
    pr("  Bytes movidos por el bus en toda la corrida: ");
    prn(s_bus_bytes); pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}

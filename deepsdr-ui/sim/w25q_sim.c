/*
 * Un W25Q16 simulado A NIVEL DE PIN, para poder compilar aqui el
 * User/spi_flash.c del firmware SIN TOCARLE UNA LINEA.
 *
 * POR QUE A NIVEL DE PIN Y NO MAS ARRIBA
 * --------------------------------------
 * Lo facil habria sido sustituir spi_flash_read() y compania por
 * versiones que escriben en un array. Pero entonces el banco mediria la
 * logica de FAT12 y nada mas, y se dejaria fuera justo lo que puede
 * estar mal sin que se note: como se codifica cada orden, los tres bytes
 * de direccion, el limite de 256 bytes de la pagina, el sondeo del bit
 * de ocupado. Un fallo ahi no da un resultado raro en el PC; da un
 * volumen corrupto en la radio.
 *
 * Asi que lo que se sucedanea son los CUATRO PINES -CS, reloj, MOSI y
 * MISO-, y por encima corre el spi_xfer_byte() de verdad, bit a bit,
 * contra una maquina de estados que habla el mismo protocolo que el chip
 * real. Lo que se mide aqui es literalmente el codigo que corre en la
 * placa.
 *
 * Y el chip no es complaciente. Hace cumplir las tres reglas que de
 * verdad importan en una NOR:
 *
 *   - grabar solo BAJA bits de 1 a 0. Subir uno es un fallo del banco.
 *   - grabar o borrar sin haber mandado WRITE_ENABLE antes es un fallo.
 *   - borrar solo va en bloques de 4 kB alineados.
 *
 * Ademas cuenta borrados y grabaciones, y marca una franja INTOCABLE
 * para poder afirmar que una operacion no se salio de donde debia.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "w25q_sim.h"

/*
 * EL CHIP PUEDE MEDIR OTRA COSA QUE 2 MB - 02/10/2026.
 *
 * El array se reserva del tamaño maximo direccionable con tres bytes
 * (16 MB) y lo que decide donde DA LA VUELTA la direccion es
 * g_chip_bytes, que se cambia con w25q_tam(). El codigo de capacidad del
 * JEDEC sale de ahi tambien, porque un chip de verdad dice lo que mide.
 *
 * Hace falta para el banco de sim/capacidad.c: el firmware saca de la
 * capacidad el tamaño del disco USB y donde empieza la zona alta, y eso
 * no se puede comprobar con un solo tamaño.
 */
#define CHIP_MAX     (16U * 1024U * 1024U)
#define PAGINA       256U
#define BLOQUE       4096U

static uint32_t g_chip_bytes = 2U * 1024U * 1024U;

uint8_t  g_chip[CHIP_MAX];

void w25q_tam(uint32_t bytes)
{
    g_chip_bytes = bytes;
}

uint32_t w25q_tam_actual(void) { return g_chip_bytes; }

/*
 * UN CHIP QUE NO DA LA VUELTA - 11/10/2026.
 *
 * Por omision la direccion da la vuelta al llegar al final, que es lo que
 * hace un W25Q de verdad y de lo que se aprovecha alta_mide_tam() para
 * medir el tamaño sin fiarse del JEDEC. Pero no todos lo hacen: hay chips
 * -y clones- que por encima de su final contestan 0xFF.
 *
 * Con esto puesto, el banco puede comprobar lo unico que de verdad importa
 * de esa medida: que cuando NO encuentra el alias, el firmware no se
 * inventa 16 MB. Anunciar de mas es lo que rompe el volumen.
 */
static uint8_t g_sin_alias;
void w25q_sin_alias(uint8_t si) { g_sin_alias = (uint8_t)(si != 0U); }

/* El byte que toca leer en esta direccion, con o sin vuelta. */
static uint8_t chip_lee(uint32_t a)
{
    if (a >= g_chip_bytes && g_sin_alias) { return 0xFFU; }
    return g_chip[a % g_chip_bytes];
}

/* El log2 del tamaño, que es lo que el JEDEC pone en el tercer byte. */
static uint8_t jedec_capacidad(void)
{
    uint8_t n = 0U;
    uint32_t v = g_chip_bytes;
    while (v > 1U) { v >>= 1; n++; }
    return n;
}
uint32_t g_borrados, g_grabados;
uint64_t g_bits;   /* bits que han pasado por el bus, para poder cronometrar */
int      g_fallos_chip;
uint32_t g_intocable_ini = 0xFFFFFFFFU, g_intocable_fin = 0xFFFFFFFFU;

/*
 * UNA QUEJA DEL CHIP QUE NADIE ESPERABA TIENE QUE PODER TUMBAR EL BANCO
 * - 15/10/2026.
 *
 * Pedido por el usuario.
 *
 * sim/borrado.c llevaba tiempo imprimiendo
 *
 *     CHIP: grabar intenta SUBIR un bit (0x103000)   MAL
 *     TODO CORRECTO: borrado grande dentro de su sitio
 *
 * y las dos lineas eran ciertas. La queja es la del paso 3b, que PROVOCA
 * una programacion sin borrar a proposito para demostrar que el chip la
 * caza -sin esa demostracion, el paso 3a no estaria probando nada-. O sea
 * que el banco estaba bien y lo que mentia era la palabra "MAL".
 *
 * Pero el arreglo no puede ser callar esa linea, porque entonces el dia
 * que un banco provoque una queja DE VERDAD y no mire `g_fallos_chip`,
 * volveria a acabar en verde y nadie se enteraria. Asi que hay dos cosas:
 *
 *   - `g_chip_espera`, que el banco pone a 1 mientras esta buscando la
 *     queja: entonces se imprime "esperado" en vez de "MAL".
 *   - y las que llegan con el flag a 0 se cuentan aparte y, si al acabar
 *     queda alguna sin reclamar, el programa SALE CON ERROR por su
 *     cuenta, diga lo que diga el main().
 *
 * Esto segundo vale para los diez bancos que enlazan este chip, no solo
 * para el que tenia el problema: un banco verde con una queja del chip
 * dentro deja de ser posible.
 */
int      g_chip_espera;
uint32_t g_chip_sueltas;

static void chip_al_salir(void)
{
    if (g_chip_sueltas != 0U) {
        printf("\n  HAY FALLOS: el chip se quejo %u vez(ces) y el banco no lo\n"
               "  reclamo. Si la queja se buscaba, pon g_chip_espera a 1\n"
               "  mientras se provoca; si no, es un fallo de verdad.\n",
               (unsigned)g_chip_sueltas);
        fflush(stdout);
        _exit(1);
    }
}

static void mal(const char *que, uint32_t dato)
{
    static int puesto;
    printf("  CHIP: %s (0x%06X)   %s\n", que, dato,
           g_chip_espera ? "esperado" : "MAL");
    g_fallos_chip++;
    if (!g_chip_espera) {
        g_chip_sueltas++;
        if (!puesto) { puesto = 1; atexit(chip_al_salir); }
    }
}

static void mira_intocable(uint32_t ini, uint32_t len, const char *que)
{
    uint32_t fin = ini + len;
    if (g_intocable_ini == 0xFFFFFFFFU) { return; }
    if (ini < g_intocable_fin && fin > g_intocable_ini) {
        mal(que, ini);
    }
}

/* --------------------------------------------- maquina de estados SPI */
static uint8_t  s_cs = 1U, s_clk = 0U, s_mosi = 0U, s_miso = 0U;
static uint8_t  s_wel;              /* write enable latch */
static uint8_t  s_bit;              /* bit dentro del byte en curso, 0=MSB */
static uint8_t  s_in;               /* byte que esta entrando */
static uint8_t  s_out;              /* byte que esta saliendo */
static uint32_t s_nbyte;            /* cuantos bytes van en esta orden */
static uint8_t  s_cmd;
static uint32_t s_addr;
static uint8_t  s_pag[PAGINA];      /* lo que se acumula de un PAGE_PROGRAM */
static uint32_t s_pag_n;

static void cierra_orden(void)
{
    if (s_cmd == 0x02U && s_pag_n > 0U) {
        /* PAGE PROGRAM: se aplica al soltar CS, como el chip real. */
        uint32_t i;
        if (!s_wel) { mal("PAGE_PROGRAM sin WRITE_ENABLE", s_addr); }
        mira_intocable(s_addr, s_pag_n, "PAGE_PROGRAM en zona intocable");
        for (i = 0U; i < s_pag_n; i++) {
            /* Una pagina da la vuelta sobre si misma, como el chip real. */
            uint32_t a = (s_addr & ~(PAGINA - 1U)) | ((s_addr + i) & (PAGINA - 1U));
            uint8_t  nuevo = (uint8_t)(g_chip[a] & s_pag[i]);
            if ((uint8_t)(s_pag[i] & ~g_chip[a]) != 0U) {
                mal("grabar intenta SUBIR un bit", a);
            }
            g_chip[a] = nuevo;
        }
        g_grabados++;
        s_wel = 0U;
    }
    s_pag_n = 0U;
    s_cmd = 0U;
    s_nbyte = 0U;
}

/* Un byte entero ha entrado. Decide que sale y que pasa. */
static void byte_entrado(uint8_t v)
{
    if (s_nbyte == 0U) {
        s_cmd = v;
        s_addr = 0U;
        s_pag_n = 0U;
        s_out = 0xFFU;
        switch (s_cmd) {
        case 0x06U: s_wel = 1U; break;                    /* WRITE ENABLE */
        case 0x04U: s_wel = 0U; break;                    /* WRITE DISABLE */
        case 0x05U: s_out = (uint8_t)(s_wel ? 0x02U : 0x00U); break; /* STATUS1: nunca ocupado */
        case 0x9FU: s_out = 0xEFU; break;                 /* JEDEC: Winbond */
        default: break;
        }
        s_nbyte++;
        return;
    }

    switch (s_cmd) {
    case 0x9FU:  /* EF 40 <log2 del tamaño>: 15 = W25Q16, 17 = W25Q64 */
        s_out = (s_nbyte == 1U) ? 0x40U
              : ((s_nbyte == 2U) ? jedec_capacidad() : 0xFFU);
        break;
    case 0x05U:
        s_out = (uint8_t)(s_wel ? 0x02U : 0x00U);
        break;
    case 0x03U:  /* READ: 3 bytes de direccion y luego sale dato */
        if (s_nbyte <= 3U) {
            s_addr = (s_addr << 8) | v;
            s_out = (s_nbyte == 3U) ? chip_lee(s_addr) : 0xFFU;
        } else {
            s_addr++;
            s_out = chip_lee(s_addr);
        }
        break;
    case 0x02U:  /* PAGE PROGRAM: 3 de direccion y luego datos */
        if (s_nbyte <= 3U) {
            s_addr = (s_addr << 8) | v;
        } else if (s_pag_n < PAGINA) {
            s_pag[s_pag_n++] = v;
        }
        break;
    case 0xD8U:  /* BLOCK ERASE 64K - 05/10/2026, ver spi_flash_borrado_paso() */
        if (s_nbyte <= 3U) {
            s_addr = (s_addr << 8) | v;
            if (s_nbyte == 3U) {
                uint32_t b = s_addr & ~(uint32_t)(65536U - 1U);
                if (!s_wel) { mal("BLOCK_ERASE sin WRITE_ENABLE", s_addr); }
                if ((s_addr & (uint32_t)(65536U - 1U)) != 0U) {
                    mal("BLOCK_ERASE con direccion no alineada", s_addr);
                }
                mira_intocable(b, 65536U, "BLOCK_ERASE en zona intocable");
                memset(&g_chip[b % g_chip_bytes], 0xFF, 65536U);
                g_borrados++;
                s_wel = 0U;
            }
        }
        break;
    case 0x20U:  /* SECTOR ERASE 4K */
        if (s_nbyte <= 3U) {
            s_addr = (s_addr << 8) | v;
            if (s_nbyte == 3U) {
                uint32_t b = s_addr & ~(BLOQUE - 1U);
                if (!s_wel) { mal("SECTOR_ERASE sin WRITE_ENABLE", s_addr); }
                if ((s_addr & (BLOQUE - 1U)) != 0U) {
                    /* El chip real lo acepta e ignora los bits bajos; aqui
                     * se avisa, porque en este proyecto significa que
                     * alguien calculo mal una direccion. */
                    mal("SECTOR_ERASE con direccion no alineada", s_addr);
                }
                mira_intocable(b, BLOQUE, "SECTOR_ERASE en zona intocable");
                memset(&g_chip[b % g_chip_bytes], 0xFF, BLOQUE);
                g_borrados++;
                s_wel = 0U;
            }
        }
        break;
    default:
        break;
    }
    s_nbyte++;
}

/* --------------------------------------------------- los cuatro pines */
void w25q_cs(uint8_t nivel)
{
    if (nivel == 0U && s_cs != 0U) {      /* CS baja: empieza una orden */
        s_bit = 0U; s_in = 0U; s_nbyte = 0U; s_cmd = 0U; s_pag_n = 0U;
    } else if (nivel != 0U && s_cs == 0U) {
        cierra_orden();
    }
    s_cs = nivel;
}

void w25q_clk(uint8_t nivel)
{
    if (s_cs != 0U) { s_clk = nivel; return; }

    if (nivel != 0U && s_clk == 0U) {     /* flanco de subida */
        g_bits++;
        s_in = (uint8_t)((s_in << 1) | (s_mosi & 1U));
        /* El firmware lee MISO justo DESPUES de subir el reloj, asi que
         * el bit que toca tiene que estar puesto ya al volver de aqui. */
        s_miso = (uint8_t)((s_out >> (7U - s_bit)) & 1U);
        s_bit++;
        if (s_bit == 8U) {
            s_bit = 0U;
            byte_entrado(s_in);
            s_in = 0U;
        }
    }
    s_clk = nivel;
}

void w25q_mosi(uint8_t nivel) { s_mosi = nivel; }
uint8_t w25q_miso(void)       { return s_miso; }

void w25q_reset(void)
{
    memset(g_chip, 0xFF, sizeof g_chip);
    s_cs = 1U; s_clk = 0U; s_mosi = 0U; s_miso = 0U; s_wel = 0U;
    s_bit = 0U; s_in = 0U; s_out = 0xFFU; s_nbyte = 0U; s_cmd = 0U; s_pag_n = 0U;
    g_borrados = 0U; g_grabados = 0U; g_fallos_chip = 0; g_bits = 0U;
    g_chip_espera = 0;   /* g_chip_sueltas NO se borra: es la cuenta de toda la corrida */
    g_intocable_ini = g_intocable_fin = 0xFFFFFFFFU;
}

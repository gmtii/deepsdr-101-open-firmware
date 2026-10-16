/*
 * LA FLASH INTERNA SIMULADA, CON LAS REGLAS DE VERDAD DEL FMC.
 *
 * No es un array y ya: si lo fuera, el banco aprobaria codigo que en la
 * placa no funciona. Las dos reglas que importan y que aqui se cumplen:
 *
 *   1. borrar deja 0xFF, y se borra por SECTORES, no por bytes. El mapa
 *      de sectores del GD32F4xx es el del manual (tabla 2-1): cuatro de
 *      16 kB, uno de 64 kB, y de ahi en adelante de 128 kB.
 *   2. grabar solo puede BAJAR bits. Escribir 0xFF sobre un byte que ya
 *      vale 0x00 no lo devuelve a 0xFF. Si el codigo intenta grabar sobre
 *      algo sin borrar, aqui sale un fallo contado en vez de un resultado
 *      que parece bueno.
 *
 * La segunda es la que caza el error tipico de un cargador: borrar menos
 * sectores de los que se van a escribir.
 */
#include "fmc_sim.h"
#include <string.h>

static uint8_t  s_mem[FMC_SIM_TAM];
static uint32_t s_borrados, s_grabados, s_fallos;

/* Donde empieza y cuanto mide el sector que contiene `addr`. */
static uint32_t sector_de(uint32_t addr, uint32_t *tam)
{
    uint32_t off = addr - FMC_SIM_BASE;

    if (off < 0x10000UL)  { *tam = 0x4000UL;  return (off & ~0x3FFFUL); }
    if (off < 0x20000UL)  { *tam = 0x10000UL; return 0x10000UL; }
    *tam = 0x20000UL;
    return (off & ~0x1FFFFUL);
}

void fmc_sim_reset(void)
{
    memset(s_mem, 0xFF, sizeof s_mem);
    s_borrados = s_grabados = s_fallos = 0U;
}

uint8_t *fmc_sim_mem(void)      { return s_mem; }
uint32_t fmc_sim_borrados(void) { return s_borrados; }
uint32_t fmc_sim_grabados(void) { return s_grabados; }
uint32_t fmc_sim_fallos(void)   { return s_fallos; }

static void sim_lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t off = addr - FMC_SIM_BASE;
    if (off + len > FMC_SIM_TAM) { memset(buf, 0, len); return; }
    memcpy(buf, &s_mem[off], len);
}

static uint8_t sim_borra(uint32_t addr)
{
    uint32_t tam, ini;
    if (addr < FMC_SIM_BASE || addr >= FMC_SIM_BASE + FMC_SIM_TAM) { return 0U; }
    ini = sector_de(addr, &tam);
    memset(&s_mem[ini], 0xFF, tam);
    s_borrados++;
    return 1U;
}

static uint8_t sim_escribe(uint32_t addr, const uint8_t *d, uint32_t len)
{
    uint32_t off = addr - FMC_SIM_BASE, i;

    if (addr < FMC_SIM_BASE || off + len > FMC_SIM_TAM) { return 0U; }
    for (i = 0U; i < len; i++) {
        /* la regla 2: la flash hace AND, no asignacion */
        if ((uint8_t)(s_mem[off + i] & d[i]) != d[i]) { s_fallos++; }
        s_mem[off + i] &= d[i];
    }
    s_grabados += len;
    return 1U;
}

static const carga_fmc_t k_iface = { sim_lee, sim_borra, sim_escribe };
const carga_fmc_t *fmc_sim_iface(void) { return &k_iface; }

#include "hfdl_ram.h"
#include "ft8_shared_ram.h"
#include "waterfall.h"

/*
 * Ver hfdl_ram.h. Aqui solo vive el cerrojo: si el modo no esta activo, la
 * region no existe, y quien la pida se lleva un NULL en vez de un puntero a
 * los pixeles de la cascada.
 */
static uint8_t s_cogida;

void hfdl_ram_coge(void)
{
    waterfall_presta(1U);
    s_cogida = 1U;
}

void hfdl_ram_suelta(void)
{
    s_cogida = 0U;
    waterfall_presta(0U);
}

uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out)
{
    if (!s_cogida) {
        if (capacity_out != (void *)0) { *capacity_out = 0U; }
        return (uint8_t *)0;
    }
    /* Despues de la cabecera reservada - ver hfdl_ram.h. */
    if (capacity_out != (void *)0) {
        *capacity_out = (uint32_t)sizeof g_ft8_shared_ram - HFDL_RAM_CABECERA;
    }
    return (uint8_t *)&g_ft8_shared_ram + HFDL_RAM_CABECERA;
}

uint8_t *hfdl_ram_cabecera(void)
{
    if (!s_cogida) {
        return (uint8_t *)0;
    }
    return (uint8_t *)&g_ft8_shared_ram;
}

uint8_t *hfdl_ram_todo(uint32_t *capacity_out)
{
    if (!s_cogida) {
        if (capacity_out != (void *)0) { *capacity_out = 0U; }
        return (uint8_t *)0;
    }
    if (capacity_out != (void *)0) {
        *capacity_out = (uint32_t)sizeof g_ft8_shared_ram;
    }
    return (uint8_t *)&g_ft8_shared_ram;
}

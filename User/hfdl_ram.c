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
    /*
     * BORRAR AL DEVOLVER, NO SOLO BAJAR EL CERROJO - 30/09/2026.
     *
     * Aqui solo estaba el waterfall_presta(0U), y eso deja en el buffer lo
     * ultimo que escribio el decodificador. En cuanto el cerrojo baja,
     * waterfall_blit() vuelve a tratar esos bytes como PIXELES y pinta las
     * 72 filas enteras -hasta 54.432 bytes- del ecualizador de HFDL o del
     * espectrograma de WSPR interpretados como colores de la paleta.
     *
     * Se ve al salir de HFDL, WSPR, AIS, ALE o JTTY a cualquier modo
     * analogico, y tarda en irse solo: la cascada repone UNA fila por
     * volcado, o sea 72 volcados, que son 2,4 s a "Rapida" y 24 s a "Muy
     * lenta".
     *
     * FT8 ya lo hacia -ver ft8_modo_stop(), que llama a waterfall_init()
     * justo detras-, y los cinco modos que entraron despues copiaron el
     * stop() sin esa linea. Se pone AQUI, en el unico sitio por donde los
     * cinco devuelven el prestamo, para que el sexto que llegue no tenga
     * que acordarse.
     */
    waterfall_presta(0U);
    waterfall_init();   /* el memset que los vuelve a hacer pixeles */
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

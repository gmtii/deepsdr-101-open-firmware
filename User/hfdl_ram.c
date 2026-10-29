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
    /*
     * Despues de la cabecera reservada - ver hfdl_ram.h.
     *
     * Y LA CAPACIDAD SALE DE LA UNION ENTERA, NO DEL MIEMBRO - apuntado el
     * 29/10/2026, revision a fondo.
     *
     * El comentario de ft8_shared_ram.h dice que desde el 11/10 HFDL "lo
     * pide por su nombre" y que por eso su capacidad ya no depende del
     * ancho de un buffer de otro. Esa frase NO es verdad hoy: el miembro
     * mayor de la union es el de FT8 (69.852, mas que los 66.256 de
     * hfdl_buf), y esta linea mide la UNION, asi que lo que HFDL puede
     * repartir sigue dependiendo de la ventana de FT8.
     *
     * Se deja asi A PROPOSITO, y no por dejadez: medir el miembro daria
     * 51.088, que es EXACTAMENTE lo que pide el giro 6 -margen cero-,
     * mientras que medir la union da 54.684 y deja 3.596 de sobra. Cambiarlo
     * seria tirar margen real para que un comentario dijera la verdad.
     *
     * Lo que SI se hace es poner la red: el _Static_assert de abajo no deja
     * que la union encoja por debajo de lo que HFDL necesita. Asi, el dia
     * que alguien estreche la ventana de FT8, se entera al compilar y no
     * cuando un giro 6 deja de caber en antena.
     */
    if (capacity_out != (void *)0) {
        *capacity_out = (uint32_t)sizeof g_ft8_shared_ram - HFDL_RAM_CABECERA;
    }
    return (uint8_t *)&g_ft8_shared_ram + HFDL_RAM_CABECERA;
}

/*
 * LA RED DE LA QUE HABLA hfdl_scope_get_hfdl_ram(): la union no puede
 * encoger por debajo de lo que HFDL necesita. Hoy sobran 3.596 bytes.
 */
_Static_assert(sizeof(g_ft8_shared_ram) >= HFDL_RAM_TOTAL,
               "la union se ha quedado por debajo de lo que pide HFDL: "
               "mira HFDL_RAM_TOTAL en hfdl_ram.h");

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

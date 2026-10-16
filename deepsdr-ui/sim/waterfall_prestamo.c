/*
 * waterfall_prestamo.c - el prestamo de la memoria de la cascada, sin la
 * cascada.  27/09/2026.
 *
 * Los modos digitales grandes -FT8, HFDL y WSPR- viven dentro de los bytes
 * de la cascada (ver ft8_shared_ram.h), y quien reparte ese prestamo es
 * waterfall.c. Pero waterfall.c ademas PINTA, asi que enlazarlo entero en
 * un banco arrastra gfx2, el IPA y el bus de la pantalla.
 *
 * De todo eso, lo unico que un banco del ciclo de WSPR necesita son el
 * buffer y el cerrojo. Aqui estan, y nada mas.
 *
 * El cerrojo NO es de adorno aunque aqui solo haya un modo: es lo que hace
 * que wspr_modo_start() se niegue si otro tiene la memoria, y el banco
 * comprueba justamente que entrar y salir del modo lo deja bien puesto. Un
 * tapon que devolviera siempre "libre" se comeria esa comprobacion entera.
 */
#include "ft8_shared_ram.h"
#include "waterfall.h"

ft8_shared_ram_t g_ft8_shared_ram;

static uint8_t s_prestada;

void    waterfall_presta(uint8_t si) { s_prestada = si ? 1U : 0U; }
uint8_t waterfall_prestada(void)     { return s_prestada; }

/*
 * Y EL BORRADO, desde el 30/09/2026.
 *
 * hfdl_ram_suelta() llama ahora a waterfall_init() al devolver el prestamo,
 * porque sin eso lo ultimo que escribio el decodificador se quedaba en el
 * buffer y la cascada lo pintaba como pixeles. Aqui no hay cascada que
 * pintar, pero el memset SI tiene que estar: es lo que el banco del ciclo de
 * WSPR comprueba cuando mira que salir del modo deja la memoria limpia, y un
 * tapon vacio se comeria esa comprobacion igual que se la comeria un cerrojo
 * que dijera siempre "libre".
 */
#include <string.h>

void waterfall_init(void)
{
    memset(&g_ft8_shared_ram, 0, sizeof g_ft8_shared_ram);
}

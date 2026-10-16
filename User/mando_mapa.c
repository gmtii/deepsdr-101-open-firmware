#include "mando_mapa.h"

uint8_t mando_mapa_gira(uint8_t mapa_visible, uint8_t modo_con_mapa,
                        uint8_t long_press, int32_t detentes,
                        int destino, int destino_sintonia)
{
    if (!mapa_visible || !modo_con_mapa) { return 0U; }
    if (long_press)                      { return 0U; }
    if (detentes == 0)                   { return 0U; }
    /* Ver la cabecera: enganchado a algo, el mando es del usuario. */
    if (destino != destino_sintonia)     { return 0U; }
    return 1U;
}

/*
 * EL BANCO VA AQUI DENTRO porque la regla es una sola linea y lo que hay
 * que probar es la TABLA entera de destinos, no la linea. Un banco que
 * probara "volumen si, sintonia no" seria el mismo error que se arregla:
 * comprobar los dos casos que alguien reclamo en vez de la regla.
 */
uint8_t mando_mapa_selftest(void)
{
    const int SINTONIA = 0;
    int d;

    /* Con la sintonia -nadie ha pedido nada- el giro es del mapa. */
    if (!mando_mapa_gira(1U, 1U, 0U, 1, SINTONIA, SINTONIA)) { return 0U; }
    if (!mando_mapa_gira(1U, 1U, 0U, -3, SINTONIA, SINTONIA)) { return 0U; }

    /* Con CUALQUIER otro destino enganchado, NO. Los doce de main.c van
     * del 0 al 11; se barren hasta 31 por si alguien añade mas. */
    for (d = 1; d <= 31; d++) {
        if (mando_mapa_gira(1U, 1U, 0U, 1, d, SINTONIA)) { return 0U; }
    }

    /* Y las tres puertas de siempre, que no cambian. */
    if (mando_mapa_gira(0U, 1U, 0U, 1, SINTONIA, SINTONIA)) { return 0U; }
    if (mando_mapa_gira(1U, 0U, 0U, 1, SINTONIA, SINTONIA)) { return 0U; }
    if (mando_mapa_gira(1U, 1U, 1U, 1, SINTONIA, SINTONIA)) { return 0U; }
    if (mando_mapa_gira(1U, 1U, 0U, 0, SINTONIA, SINTONIA)) { return 0U; }

    return 1U;
}

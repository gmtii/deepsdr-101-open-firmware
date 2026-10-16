/*
 * El acelerador grafico, apagado. 25/09/2026.
 *
 * En la radio el IPA convierte indices de paleta a RGB565 sin gastar CPU.
 * Aqui no hay IPA, y ipa_blit_hay() devolviendo 0 es exactamente lo que el
 * firmware ya sabe manejar: waterfall_blit() mira esa respuesta y se va por
 * el camino de la CPU, que es el que este banco quiere recorrer.
 */
#include "ipa_blit.h"

void    ipa_blit_init(void) { }
uint8_t ipa_blit_hay(void)  { return 0U; }
void    ipa_blit_pon(uint8_t on) { (void)on; }
uint16_t ipa_blit_fallos(void) { return 0U; }

uint8_t ipa_blit_l8(const uint8_t *src, uint16_t src_salto,
                    uint16_t *dst, uint16_t dst_salto,
                    uint16_t w, uint16_t h, const uint16_t *cmap)
{
    (void)src; (void)src_salto; (void)dst; (void)dst_salto;
    (void)w; (void)h; (void)cmap;
    return 0U;   /* no hay acelerador: que lo haga la CPU */
}

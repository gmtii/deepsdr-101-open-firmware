#ifndef LCD_DMA_H
#define LCD_DMA_H

/*
 * Cabecera SUSTITUTA para compilacion en host (simulador).
 *
 * Declara la misma API que User/lcd_dma.h del firmware. La implementacion
 * de mentira vive en sim/lcd_sim.c y dice siempre "no hay DMA", que es lo
 * correcto aqui: el simulador comprueba PIXELES, y el camino sin DMA pinta
 * exactamente los mismos. Lo que el DMA cambia es quien espera al bus, y
 * aqui no hay bus al que esperar.
 */
#include <stdint.h>

void     lcd_dma_init(void);
uint8_t  lcd_dma_hay(void);
void     lcd_dma_pon(uint8_t on);
uint8_t  lcd_dma_manda(const uint16_t *px, uint32_t n);
uint8_t  lcd_dma_espera(void);
uint16_t lcd_dma_fallos(void);

#endif /* LCD_DMA_H */

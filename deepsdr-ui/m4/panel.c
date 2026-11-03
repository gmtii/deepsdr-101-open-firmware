#include <stdint.h>
/* El panel: con el DMA de pantalla ENCENDIDO el empuje no lo hace la CPU,
 * asi que aqui es un store y nada mas. Lo que se mide es componer. */
volatile uint16_t g_panel;
void rm68120_write_cmd(uint16_t c){ g_panel=c; }
void rm68120_write_data(uint16_t d){ g_panel=d; }
void rm68120_write_cmd_data(uint16_t c,uint16_t d){ g_panel=c; g_panel=d; }
uint16_t rm68120_read_data(void){ return g_panel; }
void rm68120_set_window(uint16_t a,uint16_t b,uint16_t c,uint16_t d){ (void)a;(void)b;(void)c;(void)d; }
void rm68120_fill_screen(uint16_t c){ (void)c; }
void rm68120_init(void){}
/* El ciclo del bus del panel, como en el firmware con el DMA puesto. */
volatile uint32_t g_lcd_ciclos;
void rm68120_ciclo(void){ g_lcd_ciclos++; }
/* El blit por DMA del espectro: en la radio lo hace el DMA y la CPU sigue.
 * Aqui no cuesta nada, o sea el caso MEJOR para el espectro. */
volatile unsigned long g_blits;
unsigned char gfx_blit_arranca(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t*p){
    (void)x;(void)y;(void)w;(void)h;(void)p; g_blits++; return 1; }
void gfx_blit_espera(void){ g_blits++; }
void gfx_vigila(uint16_t x,uint16_t y,uint16_t w,uint16_t h){ (void)x;(void)y;(void)w;(void)h; }
uint32_t gfx_vigilancia(void){ return 0; }
/* El acelerador IPA: en la radio vuelca la cascada sin CPU. Aqui tampoco
 * cuesta, o sea otra vez el caso MEJOR para la cascada. */
unsigned char ipa_blit_hay(void){ return 1; }
unsigned char ipa_blit_l8(uint16_t x,uint16_t y,uint16_t w,uint16_t h,
                          const unsigned char *src,uint32_t paso,
                          const uint16_t *lut){
    (void)x;(void)y;(void)w;(void)h;(void)src;(void)paso;(void)lut;
    g_blits++; return 1; }

/* El panel real: los mismos accesos mapeados en memoria que rm68120_exmc.c */
#include <stdint.h>
#define LCD_REG (*(volatile uint16_t *)0x60000000UL)
#define LCD_DAT (*(volatile uint16_t *)0x60020000UL)
void rm68120_write_cmd(uint16_t c){ LCD_REG=c; }
void rm68120_write_data(uint16_t d){ LCD_DAT=d; }
void rm68120_write_cmd_data(uint16_t c,uint16_t d){ LCD_REG=c; LCD_DAT=d; }
uint16_t rm68120_read_data(void){ return LCD_DAT; }
void rm68120_set_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){
    rm68120_write_cmd_data(0x2A00,(x0>>8)&0xFF); rm68120_write_cmd_data(0x2A01,x0&0xFF);
    rm68120_write_cmd_data(0x2A02,(x1>>8)&0xFF); rm68120_write_cmd_data(0x2A03,x1&0xFF);
    rm68120_write_cmd_data(0x2B00,(y0>>8)&0xFF); rm68120_write_cmd_data(0x2B01,y0&0xFF);
    rm68120_write_cmd_data(0x2B02,(y1>>8)&0xFF); rm68120_write_cmd_data(0x2B03,y1&0xFF);
    rm68120_write_cmd(0x2C00);
}
void rm68120_fill_screen(uint16_t c){ uint32_t n=800UL*480UL;
    rm68120_set_window(0,0,799,479); while(n--) LCD_DAT=c; }
void rm68120_init(void){}

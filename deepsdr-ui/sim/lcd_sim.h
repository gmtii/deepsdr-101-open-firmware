#ifndef LCD_SIM_H
#define LCD_SIM_H

#include <stdint.h>

#define LCD_W 800
#define LCD_H 480

/*
 * Contadores de coste del bus EXMC.
 *
 * El panel cuelga del EXMC mapeado en memoria: escribir un pixel es un
 * store de 16 bits a LCD_DAT. Lo que NO es gratis es abrir ventana:
 * rm68120_set_window() son 8 write_cmd_data() (CASET x4 + RASET x4) mas
 * un write_cmd(RAMWR), y cada write_cmd_data() son DOS accesos al bus
 * (uno al registro de comando, otro al de dato). Total: 17 accesos por
 * ventana, frente a 1 acceso por pixel volcado.
 *
 * Contando accesos por separado podemos medir, sin hardware, cuanto de
 * cada frame se gasta en montar ventanas en vez de en pintar pixeles -
 * que es exactamente el problema de gfx_char() en el codigo actual.
 */
typedef struct {
    uint64_t windows;      /* llamadas a rm68120_set_window() */
    uint64_t bus_writes;   /* accesos totales de 16 bits al bus EXMC */
    uint64_t pixels;       /* escrituras que acabaron siendo un pixel visible */
    uint64_t overdraw;     /* pixeles escritos sobre un pixel ya escrito este frame */
} lcd_cost_t;

void     lcd_sim_reset(uint16_t fill);
void     lcd_sim_cost_reset(void);
lcd_cost_t lcd_sim_cost(void);

/* Tiempo estimado del frame, en microsegundos, para unos timings EXMC
 * dados. cycles_per_access = (bus_latency + addr_setup + addr_hold +
 * data_setup) tal como se programan en exmc_norsram_init(). ahb_mhz es
 * el reloj del AHB que alimenta el EXMC. */
double   lcd_sim_est_us(unsigned cycles_per_access, double ahb_mhz);

const uint16_t *lcd_sim_fb(void);
int      lcd_sim_write_ppm(const char *path);

#endif /* LCD_SIM_H */

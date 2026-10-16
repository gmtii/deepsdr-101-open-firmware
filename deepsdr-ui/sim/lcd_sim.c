/*
 * Simulador del panel RM68120 sobre canvas en memoria.
 *
 * Reimplementa la API de rm68120_exmc.h respetando la semantica real del
 * controlador: tras rm68120_set_window(x0,y0,x1,y1), cada escritura de
 * dato coloca un pixel y avanza el cursor en X; al pasar de x1 vuelve a
 * x0 e incrementa Y. Al pasar de y1 el controlador real envuelve a y0;
 * aqui hacemos lo mismo para reproducir tambien los desbordes.
 */

#include "lcd_sim.h"
#include "rm68120_exmc.h"

/* Lo declara hostinc/rm68120_exmc.h para que gfx.c compile igual aqui que en
 * el firmware. Aqui no se usa para nada: rm68120_ciclo() devuelve cero en el
 * host, asi que esto se queda a cero. Existe solo para que enlace. */
volatile uint32_t g_lcd_ciclos;

uint32_t rm68120_ciclo(void)
{
    return 0U;   /* aqui no se miden tiempos, se comprueban pixeles */
}

/* El DMA de la pantalla, de mentira: aqui no hay bus al que esperar, asi que
 * dice que no lo hay y todo se va por el camino de la CPU - que pinta
 * exactamente los mismos pixeles, que es lo que este simulador comprueba. */
void     lcd_dma_init(void) { }
uint8_t  lcd_dma_hay(void) { return 0U; }
void     lcd_dma_pon(uint8_t on) { (void)on; }
uint8_t  lcd_dma_manda(const uint16_t *px, uint32_t n) { (void)px; (void)n; return 0U; }
uint8_t  lcd_dma_espera(void) { return 1U; }
uint16_t lcd_dma_fallos(void) { return 0U; }
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint16_t s_fb[LCD_W * LCD_H];
static uint8_t  s_touched[LCD_W * LCD_H];

static uint16_t s_x0, s_y0, s_x1, s_y1;
static uint16_t s_cx, s_cy;

static lcd_cost_t s_cost;

/*
 * BORRAR EL PANEL DEL SIMULADOR TAMBIEN SE AVISA - 05/10/2026.
 *
 * El espectro se salta las bandas que no han cambiado desde el cuadro
 * anterior (ver "LAS BANDAS QUE NO HACE FALTA VOLCAR" en spectrum.c). En la
 * radio eso lo cubre el chivato de gfx.c, porque todo lo que pinta pasa por
 * ahi; aqui no, porque lcd_sim_reset() escribe el framebuffer a pelo. Sin
 * este aviso el banco de la imagen patron se saltaba bandas sobre un panel
 * en negro y el de la captura de pantalla sacaba 5.024 pixeles distintos.
 *
 * Va AQUI y no en cada banco, por lo mismo de siempre: lo que hay que
 * acordarse de llamar en siete sitios se olvida en el octavo.
 *
 * Declaracion debil porque la mayoria de los bancos que usan este fichero
 * no enlazan spectrum.c. Donde si esta, gana la de verdad.
 */
__attribute__((weak)) void spectrum_invalida(void) { }

void lcd_sim_reset(uint16_t fill)
{
    size_t i;

    spectrum_invalida();
    for (i = 0; i < (size_t)LCD_W * LCD_H; i++) {
        s_fb[i] = fill;
    }
    memset(s_touched, 0, sizeof(s_touched));
    s_x0 = s_y0 = 0;
    s_x1 = LCD_W - 1;
    s_y1 = LCD_H - 1;
    s_cx = s_cy = 0;
    lcd_sim_cost_reset();
}

void lcd_sim_cost_reset(void)
{
    memset(&s_cost, 0, sizeof(s_cost));
    memset(s_touched, 0, sizeof(s_touched));
}

lcd_cost_t lcd_sim_cost(void) { return s_cost; }

double lcd_sim_est_us(unsigned cycles_per_access, double ahb_mhz)
{
    double cycles = (double)s_cost.bus_writes * (double)cycles_per_access;
    return cycles / ahb_mhz; /* ciclos / (ciclos por us) */
}

const uint16_t *lcd_sim_fb(void) { return s_fb; }

/* --- API del driver --- */

void rm68120_write_cmd(uint16_t cmd)
{
    (void)cmd;
    s_cost.bus_writes += 1; /* un store a LCD_REG */
}

void rm68120_write_cmd_data(uint16_t cmd, uint16_t data)
{
    (void)cmd; (void)data;
    s_cost.bus_writes += 2; /* LCD_REG = cmd; LCD_DAT = data; */
}

uint16_t rm68120_read_data(void) { return 0; }

void rm68120_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    s_x0 = x0; s_y0 = y0; s_x1 = x1; s_y1 = y1;
    s_cx = x0; s_cy = y0;

    s_cost.windows += 1;
    /* CASET x4 + RASET x4, cada uno cmd+dato = 2 accesos -> 16, mas RAMWR */
    s_cost.bus_writes += 8 * 2 + 1;
}

void rm68120_write_data(uint16_t data)
{
    s_cost.bus_writes += 1;

    if (s_cx < LCD_W && s_cy < LCD_H) {
        size_t idx = (size_t)s_cy * LCD_W + s_cx;
        s_fb[idx] = data;
        s_cost.pixels += 1;
        if (s_touched[idx]) {
            s_cost.overdraw += 1;
        } else {
            s_touched[idx] = 1;
        }
    }

    if (s_cx >= s_x1) {
        s_cx = s_x0;
        if (s_cy >= s_y1) {
            s_cy = s_y0;
        } else {
            s_cy++;
        }
    } else {
        s_cx++;
    }
}

void rm68120_fill_screen(uint16_t color)
{
    uint32_t n = (uint32_t)LCD_W * LCD_H;
    rm68120_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    while (n--) {
        rm68120_write_data(color);
    }
}

void rm68120_init(void) { }

/* --- Salida --- */

int lcd_sim_write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    size_t i;
    if (!f) return -1;

    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (i = 0; i < (size_t)LCD_W * LCD_H; i++) {
        uint16_t c = s_fb[i];
        /* RGB565 -> RGB888 replicando los bits altos, que es lo que hace
         * de verdad un panel de 16 bits al expandir a su driver interno:
         * asi el blanco 0xFFFF sale 255,255,255 y no 248,252,248. */
        uint8_t r = (uint8_t)((c >> 11) & 0x1F);
        uint8_t g = (uint8_t)((c >> 5)  & 0x3F);
        uint8_t b = (uint8_t)(c & 0x1F);
        uint8_t out[3];
        out[0] = (uint8_t)((r << 3) | (r >> 2));
        out[1] = (uint8_t)((g << 2) | (g >> 4));
        out[2] = (uint8_t)((b << 3) | (b >> 2));
        fwrite(out, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/*
 * LO QUE CUESTA LEER EL PANEL TACTIL, EN INSTRUCCIONES.
 *
 * POR QUE HACE FALTA EL NUMERO. El sintoma: arrastrar el dedo por el
 * espectro ralentiza la radio. La primera sospecha -los repintados, que es
 * lo que ralentizaba el mando antes de la V6.44- se arreglo y SIGUIO
 * ralentizando. O sea que habia otra cosa, y la otra cosa tiene que estar
 * en el unico camino que recorre el dedo y no recorre el mando.
 *
 * Ese camino es touch_read_raw(), y no es barato por accidente: el
 * XPT2046 se lee por SPI DE BIT-BANG, con una espera clavada a -O0 entre
 * flanco y flanco (delay_us_approx(), misma historia que delay_i2c() en
 * i2c_bitbang.c). Y se lee CINCO VECES POR EJE para filtrar el ruido del
 * panel resistivo, o sea DIEZ transacciones de 24 bits cada una.
 *
 * Mientras el dedo esta apoyado, eso corre en CADA vuelta del bucle
 * principal. El mando no paga nada de esto.
 *
 * LO QUE ESTO MIDE Y LO QUE NO. Mide instrucciones, igual que m4_ft8.c y
 * m4_i2c.c, y con la misma advertencia: QEMU no modela la espera de la
 * flash ni el bus, asi que el chip de verdad tarda mas. Sirve para saber
 * si el orden de magnitud es "se nota" o "da igual", y para comparar
 * versiones.
 */
#include "semi.h"
void pila_di(unsigned long maximo);
#include "gd32f4xx.h"
#include "touch.h"

int g_fallos;
unsigned long g_msticks;
uint32_t SystemCoreClock = 200000000UL;
void debug_print(const char*s){(void)s;}
void debug_print_dec(const char*s,uint32_t v){(void)s;(void)v;}
void debug_print_hex(const char*s,uint32_t v){(void)s;(void)v;}
void debug_print_hex32(const char*s,uint32_t v){(void)s;(void)v;}
void gfx_fill_rect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c)
{ (void)x;(void)y;(void)w;(void)h;(void)c; }

extern volatile FlagStatus g_nivel_pin;

/* La interrupcion del pin de pulsacion: aqui no hay NVIC que armar y lo
 * que se mide es la transaccion, no el arranque. */
volatile uint32_t g_exti;
void syscfg_exti_line_config(uint8_t p,uint8_t l){ g_exti=(uint32_t)p^l; }
void exti_init(exti_line_enum l,exti_mode_enum m,exti_trig_type_enum t)
{ g_exti=(uint32_t)l^(uint32_t)m^(uint32_t)t; }
void exti_interrupt_flag_clear(exti_line_enum l){ g_exti=(uint32_t)l; }
void nvic_irq_enable(uint8_t n,uint8_t p,uint8_t s){ g_exti=(uint32_t)n^p^s; }

int main(void)
{
    unsigned long a, b, v;
    int r;
    uint16_t rx, ry;

    reloj_on();
    /* PENIRQ activo a nivel bajo: el stub tiene que decir "dedo apoyado" o
     * touch_read_raw() se vuelve en la primera linea y no mide nada. */
    g_nivel_pin = RESET;
    touch_init();
    pr("\nLEER EL PANEL TACTIL, EN UN CORTEX-M4 DE VERDAD\n\n");

    a = tic();
    for (r = 0; r < 20; r++) { (void)touch_read_raw(&rx, &ry); }
    b = tic();
    v = mide("una lectura del panel          ", a, b, 20);

    {
        unsigned long us = v / 200UL;

        pr("      "); prn(us); pr(" us cada vez que se mira el panel. Son diez\n");
        pr("      transacciones de 24 bits por bit-bang, cinco por eje\n");
        pr("      (TOUCH_SAMPLES), con la espera clavada a -O0 entre flancos.\n");
        pr("      Eso es lo que cuesta leer bien, y no se toca.\n\n");

        /*
         * LA VUELTA MAS CORTA. Con el espectro pintando, una vuelta del
         * bucle mide de 1.700 a 4.000 us, medido por el cable con
         * tools/cat_vuelta.ps1. Si leer el panel se llevara mas de la
         * DECIMA PARTE de la vuelta mas corta en CADA vuelta, el arrastre
         * se notaria - y es exactamente lo que pasaba hasta la V6.52,
         * porque se leia en cada vuelta.
         */
        if (us * 10UL > 1700UL) {
            pr("      Leyendo en CADA vuelta eso seria mas de una decima de\n");
            pr("      la vuelta mas corta: se nota. Por eso hay cadencia.\n\n");
        }
    }

    /*
     * Y AHORA LO QUE DE VERDAD PAGA EL BUCLE - V6.52.
     *
     * touch_read() no mira el panel mas de una vez cada
     * TOUCH_CADENCIA_MS; entre medias devuelve la respuesta guardada. Se
     * simula el bucle principal a su ritmo mas rapido -una vuelta cada 2
     * ms- durante 200 ms y se mide el coste TOTAL, que repartido entre
     * las vueltas da lo que de verdad cuesta el tactil por vuelta.
     */
    {
        unsigned long turnos = 100UL;   /* 100 vueltas de 2 ms = 200 ms */
        unsigned long i, por_vuelta;

        g_msticks = 0UL;
        a = tic();
        for (i = 0; i < turnos; i++) {
            uint16_t cx, cy;
            g_msticks += 2UL;
            (void)touch_read(&cx, &cy);
        }
        b = tic();
        v = mide("cien vueltas del bucle de 2 ms ", a, b, 1);

        por_vuelta = v / (200UL * turnos);
        pr("      "); prn(por_vuelta);
        pr(" us por vuelta de media con el dedo apoyado.\n\n");

        /*
         * EL VEREDICTO. Sobre la vuelta mas corta de 1.700 us, el tactil
         * tiene que quedarse por debajo del 12%, o sea 200 us. Con
         * cadencia de 8 ms y vueltas de 2 ms sale una lectura de cada
         * cuatro: 775/4 = 194 us. Sin cadencia serian los 775 enteros.
         */
        if (por_vuelta > 200UL) {
            pr("      *** EL TACTIL SE LLEVA MAS DE 200 US POR VUELTA ***\n");
            g_fallos++;
        } else {
            pr("      cabe en una vuelta sin que se note.\n");
        }
    }

    pila_di(8192UL);
    pr(g_fallos ? "\n  HAY FALLOS\n\n" : "\n  TODO CORRECTO\n\n");
    salir(); return g_fallos ? 1 : 0;
}

/*
 * LO QUE CUESTA EL BUS I2C, EN INSTRUCCIONES.
 *
 * El bus del oscilador y del codec es de bit-bang y su espera esta clavada
 * a -O0 a proposito (ver delay_i2c() en i2c_bitbang.c: acelerarla sola
 * rompio la recepcion el 30/07/2026). Esto dice lo que cuesta, para que la
 * proxima vez que alguien sospeche del I2C no haya que estimarlo:
 *
 *     un byte por el bus ......  195 us   (o sea unos 50 kHz de reloj)
 *     resintonizar en zona ....  1,76 ms
 *
 * Con eso: una resintonia por vuelta de bucle mientras se gira el mando son
 * 1,8 ms, que NO explican que la cascada baje a la mitad. Medido y
 * descartado, que es para lo que existe.
 */
#include "semi.h"
void pila_di(unsigned long maximo);
#include "ms5351.h"
#include "i2c_bitbang.h"

int g_fallos;
unsigned long g_msticks;
uint32_t SystemCoreClock = 200000000UL;
void debug_print(const char*s){(void)s;}
void debug_print_dec(const char*s,uint32_t v){(void)s;(void)v;}
void debug_print_hex(const char*s,uint32_t v){(void)s;(void)v;}
void debug_print_hex32(const char*s,uint32_t v){(void)s;(void)v;}
void rf_lpf_init(void){}
uint8_t rf_lpf_select(uint32_t f){(void)f;return 1U;}

int main(void)
{
    unsigned long a,b,v; int r;
    reloj_on();
    i2c_bitbang_init();
    pr("\nEL BUS I2C, EN UN CORTEX-M4 DE VERDAD\n\n");

    a=tic(); for(r=0;r<20;r++){ static const uint8_t d[1]={0x55};
                                (void)i2c_write(0x60U,d,1U); } b=tic();
    v=mide("un byte por el bus             ", a,b,20);
    /*
     * POR LOS DOS LADOS. Ver horquilla() en semi.h y delay_i2c() en
     * i2c_bitbang.c: esa espera no esta calibrada a instrucciones, es "lo
     * que tarden 200 NOP con el compilador que toque", y clavarla a -O0 la
     * protege del resto del proyecto pero no de un compilador distinto.
     *
     * El suelo son 18.000 instrucciones = 90 us por byte = 11 us de bit, o
     * sea 5,5 us de medio ciclo. El minimo del I2C estandar es 4,7 us de
     * SCL alto y 4,0 de bajo: con eso queda margen, y por debajo se entra
     * en la zona donde ya fallo una vez.
     */
    horquilla("un byte", v, 18000UL, 45000UL);

    (void)ms5351_set_lo_freq(7102000UL);
    a=tic(); for(r=0;r<20;r++){
        (void)ms5351_set_lo_freq(7102000UL+(uint32_t)(r*1000)); } b=tic();
    v=mide("resintonizar dentro de zona    ", a,b,20);
    tope("resintonia", v, 400000UL);

    pr("\n");
    pila_di(4096UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); }
    else          { pr("  TODO CORRECTO\n\n"); }
    salir(); return 0;
}

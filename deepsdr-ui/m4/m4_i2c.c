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

    /*
     * ==================================================================
     * Y RESINTONIZAR CAMBIANDO DE DIVISOR, QUE ES LO QUE HACE UN DEDO
     * ==================================================================
     *
     * *** Medido en la radio por cable el 03/11/2026, arrastrando el dedo
     * por el espectro CON EL NCO APAGADO: cada llamada a apply_lo_tune()
     * cuesta de 5.753 a 7.395 us, treinta y ocho veces por segundo, y la
     * vuelta del bucle se va a 56.567 us. Con el NCO puesto, 13 us. ***
     *
     * La medida de arriba daba 1.763 us y por eso yo daba el asunto por
     * cerrado. Lo que mide son pasos de 1 kHz, que NO cambian el divisor:
     * ahi set_lo_freq() escribe los ocho bytes de la PLL y se acabo.
     *
     * Arrastrando el dedo el dial se va kilohercios de golpe y el divisor
     * SI cambia, y entonces se entra en la rama de `div != s_last_div`:
     * dos Multisynth mas (ocho bytes cada uno), cuatro registros sueltos,
     * la mascara de salida y un RESET DE LA PLL. Son veintidos bytes de
     * I2C de mas sobre los ocho de antes, a 195 us el byte.
     *
     * Y el reset de la PLL no es solo tiempo: el comentario de ms5351.c
     * dice con todas las letras que produce un CHASQUIDO AUDIBLE. O sea
     * que arrastrar con el NCO apagado no solo frena la radio, ademas
     * chasquea treinta y ocho veces por segundo.
     *
     * Dos frecuencias separadas 96 kHz, que es lo que mide el espectro de
     * lado a lado: un barrido de pantalla completa con el dedo.
     */
    (void)ms5351_set_lo_freq(7102000UL);
    a=tic(); for(r=0;r<20;r++){
        (void)ms5351_set_lo_freq((r & 1) ? 7198000UL : 7102000UL); } b=tic();
    v=mide("un barrido de pantalla (96 kHz)", a,b,20);
    tope("barrido de pantalla", v, 400000UL);

    /*
     * Y UN CAMBIO DE BANDA ENTERO, por si la rama cara de set_lo_freq()
     * -la de `div != s_last_div`, con veintidos bytes de I2C de mas y un
     * reset de la PLL- fuera la que paga un arrastre.
     *
     * NO LO ES, Y ESTE ES EL NEGATIVO: las TRES medidas dan el mismo
     * numero. Un paso de un kilohercio, un barrido de pantalla de noventa
     * y seis, y saltar de cuarenta a veinte metros cuestan 1.763 us los
     * tres. O sea que resintonizar cuesta LO QUE CUESTA, siempre, y la
     * unica palanca que queda es CUANTAS VECES se hace.
     *
     * Que es justo lo que dice la radio. Medido por cable el 03/11/2026
     * arrastrando el dedo CON EL NCO APAGADO, el tramo del arrastre llega
     * a 14.625 us POR CUADRO -dos resintonias por cuadro, treinta y ocho
     * por segundo-; con el NCO puesto, 18 us, porque el oscilador se
     * queda aparcado y no se reprograma.
     *
     * (Y los 1.763 de aqui son el SUELO de QEMU, que no modela la espera
     * de la flash: en la radio salen unos 6.800. La espera del bit-bang
     * son 200 NOP a -O0 leidos de flash, que es justo donde este banco
     * mide de menos. Ver la cabecera de m4/LEEME.md.)
     */
    (void)ms5351_set_lo_freq(7102000UL);
    a=tic(); for(r=0;r<20;r++){
        (void)ms5351_set_lo_freq((r & 1) ? 14200000UL : 7102000UL); } b=tic();
    v=mide("cambiar de banda (7 a 14 MHz)  ", a,b,20);
    tope("cambio de banda", v, 1600000UL);

    pr("\n");
    pila_di(4096UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); }
    else          { pr("  TODO CORRECTO\n\n"); }
    salir(); return 0;
}

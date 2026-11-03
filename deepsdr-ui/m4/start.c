/*
 * EL ARRANQUE DEL BANCO DEL M4, Y LAS TRAMPAS.
 *
 * Esto no es solo poner la FPU y saltar a main(). Es la mitad del valor de
 * correr el firmware en un Cortex-M4 de verdad en vez de en el PC:
 *
 *   - ACCESOS DESALINEADOS. En el PC pasan desapercibidos; en el M4 son
 *     lentos cuando la memoria los aguanta y un fallo de bus cuando no
 *     -el EXMC de la pantalla, por ejemplo-. Con UNALIGN_TRP puesto, el
 *     que haya se para en seco con el registro delante en vez de salir
 *     "bien" y fallar un dia en la radio.
 *   - DIVIDIR ENTRE CERO. Lo mismo: en el PC es infinito, aqui es un
 *     fallo de uso con su nombre.
 *   - LOS FALLOS, CON NOMBRE Y DIRECCION. Antes los dieciseis vectores
 *     apuntaban todos a _reset: un HardFault reiniciaba el banco en
 *     silencio y la prueba volvia a empezar desde el principio, sin decir
 *     nada. Ahora cada uno dice quien es, que registro de estado tiene y
 *     por que PC se cayo, y sale con error para que el make se entere.
 *   - LA PILA, MEDIDA CORRIENDO. Se pinta un patron debajo del tope y al
 *     final se mira hasta donde llego de verdad. El .su del compilador
 *     suma marcos sueltos; esto mide la vuelta entera con sus llamadas
 *     anidadas, sus interrupciones y sus variables locales de verdad.
 */
#include <stdint.h>
#include "semi.h"

extern uint32_t _sdata,_edata,_sbss,_ebss,_sidata,_estack;
int main(void);

#define SCB_CCR    (*(volatile uint32_t*)0xE000ED14UL)
#define SCB_SHCSR  (*(volatile uint32_t*)0xE000ED24UL)
#define SCB_CFSR   (*(volatile uint32_t*)0xE000ED28UL)
#define SCB_HFSR   (*(volatile uint32_t*)0xE000ED2CUL)
#define SCB_MMFAR  (*(volatile uint32_t*)0xE000ED34UL)
#define SCB_BFAR   (*(volatile uint32_t*)0xE000ED38UL)

/* ------------------------------------------------------------------ */
/* LA PILA, MEDIDA CORRIENDO                                           */

/* Cuanto se pinta por debajo del tope. 64 kB es mas que de sobra -la pila
 * del firmware son 8- y pintar los cuatro megas de la RAM del banco con
 * icount puesto se nota en el reloj de pared. */
#define PILA_PINTADA  (128U * 1024U)
#define PILA_PATRON   0xA5A5A5A5UL

static uint32_t *pila_suelo(void)
{
    return (uint32_t *)((uint8_t *)&_estack - PILA_PINTADA);
}

static void pila_pinta(void)
{
    uint32_t *p = pila_suelo();
    uint32_t *t = (uint32_t *)&_estack;
    uint32_t  aqui;              /* el marco de esta misma funcion */

    /* Hasta DEBAJO de donde esta la pila ahora mismo, no hasta el tope:
     * pisar el marco propio es volver a un sitio que ya no existe. Ocho
     * palabras de aire por el retorno y los registros guardados. */
    t = (uint32_t *)((uint32_t)&aqui - 32U);
    while (p < t) { *p++ = PILA_PATRON; }
}

/* Cuantos bytes de pila se han usado de verdad. Cero si el patron sigue
 * entero hasta el suelo -o sea que no se sabe, porque se desbordo lo
 * pintado-. */
unsigned long pila_usada(void)
{
    uint32_t *p = pila_suelo();
    uint32_t *t = (uint32_t *)&_estack;

    while (p < t && *p == PILA_PATRON) { p++; }
    return (unsigned long)((uint8_t *)t - (uint8_t *)p);
}

/* Y que lo diga el banco, con tope. Igual que tope(): informa Y se pone
 * rojo, porque una pila que crece sin que nadie mire es como se llega a
 * pisar las variables de al lado en la radio. */
void pila_di(unsigned long maximo)
{
    unsigned long u = pila_usada();

    pr("\n    pila usada de verdad           ");
    prn(u); pr(" bytes de ");
    prn((unsigned long)PILA_PINTADA); pr(" pintados\n");
    if (u == 0UL) {
        pr("      *** MAL *** el patron sigue entero: no se ha medido\n");
        g_fallos++;
    } else if (u >= (unsigned long)PILA_PINTADA - 64UL) {
        pr("      *** MAL *** se ha comido lo pintado: mide mas\n");
        g_fallos++;
    } else if (maximo && u > maximo) {
        pr("      tope "); prn(maximo); pr("  *** MAL ***\n");
        g_fallos++;
    } else if (maximo) {
        pr("      tope "); prn(maximo); pr("  ok\n");
    }
}

/* ------------------------------------------------------------------ */
/* LOS FALLOS, CON NOMBRE                                              */

static const char *const k_fallo[6] = {
    "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "sin vector"
};

/* marco: el que apilo el procesador -r0 r1 r2 r3 r12 lr pc xpsr-. */
void _fallo(uint32_t *marco, uint32_t cual)
{
    uint32_t cfsr = SCB_CFSR;

    pr("\n  *** ");
    pr(k_fallo[(cual < 6U) ? cual : 5U]);
    pr(" ***\n");
    pr("      PC   "); prn((unsigned long)marco[6]); pr("\n");
    pr("      LR   "); prn((unsigned long)marco[5]); pr("\n");
    pr("      CFSR "); prn((unsigned long)cfsr); pr("\n");
    pr("      HFSR "); prn((unsigned long)SCB_HFSR); pr("\n");
    /* Y lo que de verdad se quiere leer, en palabras: */
    if (cfsr & 0x01000000UL) { pr("      acceso DESALINEADO\n"); }
    if (cfsr & 0x02000000UL) { pr("      division entre CERO\n"); }
    if (cfsr & 0x00010000UL) { pr("      instruccion no definida\n"); }
    if (cfsr & 0x00020000UL) { pr("      estado EPSR invalido (thumb?)\n"); }
    if (cfsr & 0x00000100UL) { pr("      fallo de bus al apilar\n"); }
    if (cfsr & 0x00008000UL) { pr("      BFAR vale: "); prn((unsigned long)SCB_BFAR); pr("\n"); }
    if (cfsr & 0x00000080UL) { pr("      MMFAR vale: "); prn((unsigned long)SCB_MMFAR); pr("\n"); }
    g_fallos++;
    salir();
    for (;;) { }
}

#define FALLO(nombre, codigo)                                   \
    __attribute__((naked)) void nombre(void)                    \
    {                                                           \
        __asm__ volatile(                                       \
            "tst lr, #4\n\t"                                    \
            "ite eq\n\t"                                        \
            "mrseq r0, msp\n\t"                                 \
            "mrsne r0, psp\n\t"                                 \
            "mov r1, %0\n\t"                                    \
            "b _fallo\n\t"                                      \
            : : "I"(codigo) : "r0", "r1");                      \
    }

FALLO(_nmi, 0)
FALLO(_hard, 1)
FALLO(_mem,  2)
FALLO(_bus,  3)
FALLO(_uso,  4)
FALLO(_otro, 5)

/* ------------------------------------------------------------------ */

void _reset(void)
{
    /* FPU: CP10/CP11 acceso completo, que el codigo va en hard-float */
    *(volatile uint32_t*)0xE000ED88UL |= (0xFu<<20);
    __asm__ volatile("dsb");  __asm__ volatile("isb");
    { uint32_t *s=&_sidata,*d=&_sdata; while(d<&_edata) *d++=*s++;
      d=&_sbss; while(d<&_ebss) *d++=0; }

    /* Las trampas, DESPUES de copiar .data: el bucle de arriba es palabra
     * a palabra y alineado, pero el patron de la pila no puede pisar lo
     * que aun no se ha copiado. */
    SCB_SHCSR |= (1UL<<16) | (1UL<<17) | (1UL<<18);  /* mem, bus y uso con nombre propio */
    SCB_CCR   |= (1UL<<3)  | (1UL<<4);               /* DIV_0_TRP y UNALIGN_TRP */
    __asm__ volatile("dsb");  __asm__ volatile("isb");

    pila_pinta();
    main();
    for(;;){}
}

__attribute__((section(".vectors"))) __attribute__((used))
void *const g_vec[16] = { (void*)&_estack, (void*)_reset,
    (void*)_nmi, (void*)_hard, (void*)_mem, (void*)_bus, (void*)_uso,
    (void*)_otro,(void*)_otro,(void*)_otro,(void*)_otro,(void*)_otro,
    (void*)_otro,(void*)_otro,(void*)_otro,(void*)_otro };

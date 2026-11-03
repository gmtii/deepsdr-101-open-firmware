#include <stdint.h>
static inline int semi(int op, void *arg){
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm__ volatile ("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void pr(const char *s){ semi(0x04,(void*)s); }
static void prn(unsigned long v){
    char b[16]; int i=15; b[i--]=0;
    if(!v) b[i--]='0';
    while(v){ b[i--]=(char)('0'+(v%10)); v/=10; }
    pr(&b[i+1]);
}
/* Y el `make` se entera: con fallos se sale por el codigo de error, que
 * qemu convierte en un estado distinto de cero. Un banco que imprime MAL y
 * deja pasar la compilacion no vigila nada. */
extern int g_fallos;
static void salir(void){ semi(0x18,(void*)(g_fallos ? 0x20023UL : 0x20026UL)); }
#define SYST_CSR (*(volatile uint32_t*)0xE000E010UL)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014UL)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018UL)
static void reloj_on(void){ SYST_RVR=0x00FFFFFF; SYST_CVR=0; SYST_CSR=5; }
static unsigned long tic(void){ return SYST_CVR & 0xFFFFFFUL; }
/* 1 tic = 40 instrucciones (calibrado). El GD32 va a 200 MHz. */
extern int g_fallos;

static unsigned long mide(const char *que, unsigned long a, unsigned long b,
                          unsigned long n){
    unsigned long ins = (((a-b)&0xFFFFFFUL)*40UL)/n;
    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" instrucciones  = ");
    prn(ins/200UL); pr(" us a 200 MHz\n");
    return ins;
}

/*
 * CON SUELO Y TECHO, que es lo que hace falta para los bucles de espera.
 *
 * tope() solo mira por arriba, y eso vale para el dibujo: ahi lo malo es
 * que alguien meta trabajo. Para una espera clavada a instrucciones -el
 * delay_i2c() del bus del oscilador y del codec- lo malo es lo CONTRARIO:
 * que una version del compilador la compile mas apretada y los flancos del
 * I2C se queden por debajo del minimo. Eso ya paso una vez, el 30/07/2026,
 * y se noto como ruido continuo en recepcion, no como un fallo del bus.
 *
 * Asi que estas esperas se vigilan por los dos lados.
 */
static void horquilla(const char *que, unsigned long ins,
                      unsigned long minimo, unsigned long maximo){
    pr("      horquilla "); prn(minimo); pr("..."); prn(maximo); pr("  ");
    if (ins < minimo)      { pr("*** MAL: SE HA ACELERADO ***\n"); g_fallos++; }
    else if (ins > maximo) { pr("*** MAL: SE HA FRENADO ***\n");   g_fallos++; }
    else                   { pr("ok\n"); }
    (void)que;
}

/* Con tope: el banco no solo informa, tambien se pone rojo. Los topes van
 * con holgura sobre lo medido, no al ras: avisan de que alguien ha vuelto a
 * meter trabajo, no de que otra version del compilador ordene dos
 * instrucciones de otra manera. */
static void tope(const char *que, unsigned long ins, unsigned long maximo){
    pr("      tope "); prn(maximo); pr("  ");
    if (ins <= maximo) { pr("ok\n"); }
    else { pr("*** MAL ***\n"); g_fallos++; (void)que; }
}

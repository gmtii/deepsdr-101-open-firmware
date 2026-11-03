/*
 * EL LADO DEL PC de la comparacion de numeros. Ver m4/numeros_cuerpo.h.
 *
 * Escribe EXACTAMENTE lo mismo que m4/m4_numeros.c. El make los corre a los
 * dos y los compara: si difieren, el simulador y la radio no estan dando el
 * mismo resultado con el mismo codigo, que es lo que esto existe para
 * cazar.
 */
#include <stdio.h>

int g_fallos;
static void di(const char *s)                   { printf("%s\n", s); }
static void dih(const char *s, unsigned long v) { printf("%s 0x%08lX\n", s, v & 0xFFFFFFFFUL); }
static void din(const char *s, unsigned long v) { printf("%s %lu\n", s, v); }

/* En el PC la pila son megas: no dice nada de la radio. Ver numeros_cuerpo.h. */
static void pila_marca(const char *q) { (void)q; }
static void pila_tabla(void) { }

#include "numeros_cuerpo.h"

int main(void)
{
    numeros_corre();
    return 0;
}

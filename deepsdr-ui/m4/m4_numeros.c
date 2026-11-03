/*
 * EL LADO DEL CORTEX-M4 de la comparacion de numeros. Ver
 * m4/numeros_cuerpo.h, que es donde esta lo que se mide; aqui solo se dice
 * como se escribe. El gemelo del PC es sim/numeros_pc.c.
 */
#include "semi.h"
void pila_di(unsigned long maximo);

int g_fallos;

static void prh(unsigned long v)
{
    static const char hx[] = "0123456789ABCDEF";
    char b[11]; int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; i++) { b[2 + i] = hx[(v >> (28 - 4 * i)) & 0xFUL]; }
    b[10] = '\0';
    pr(b);
}
static void di(const char *s)            { pr(s); pr("\n"); }
static void dih(const char *s, unsigned long v) { pr(s); pr(" "); prh(v); pr("\n"); }
static void din(const char *s, unsigned long v) { pr(s); pr(" "); prn(v); pr("\n"); }

/*
 * LA PILA, TROZO A TROZO. Se apunta cuanto se lleva usado en cada marca y
 * se imprime al final, fuera de lo que se compara con el PC: lo que mide
 * es la RADIO, y en el PC no significa nada.
 *
 * La resta entre dos marcas dice lo que se llevo el trozo de en medio. El
 * .su del compilador suma marcos sueltos; esto mide la vuelta de verdad,
 * con sus llamadas anidadas.
 */
unsigned long pila_usada(void);
static const char *s_pila_q[8];
static unsigned long s_pila_v[8];
static unsigned char s_pila_n;

static void pila_marca(const char *q)
{
    if (s_pila_n < 8U) { s_pila_q[s_pila_n] = q; s_pila_v[s_pila_n] = pila_usada(); s_pila_n++; }
}
static void pila_tabla(void)
{
    unsigned char i;
    unsigned long ant = 0UL;
    pr("LA PILA, ACUMULADA, MEDIDA CORRIENDO\n");
    for (i = 0U; i < s_pila_n; i++) {
        pr("    "); pr(s_pila_q[i]); pr("   ");
        prn(s_pila_v[i]); pr(" bytes");
        if (s_pila_v[i] > ant) { pr("   (+"); prn(s_pila_v[i] - ant); pr(")"); }
        pr("\n");
        if (s_pila_v[i] > ant) { ant = s_pila_v[i]; }
    }
    pr("\n");
}

#include "numeros_cuerpo.h"

int main(void)
{
    numeros_corre();
    pila_di(8192UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}

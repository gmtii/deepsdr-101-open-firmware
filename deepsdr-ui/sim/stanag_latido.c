/*
 * CUANTAS VECES POR SEGUNDO SE REPINTA EL PANEL DE STANAG.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * NO ESTABA COLGADA. El bucle principal iba tan lento que se perdia las
 * pulsaciones del boton. Y la causa era que stanag_modo_latido() sumaba
 * s_medidas, que sube ONCE VECES POR SEGUNDO -medido sobre el stanag_det.c
 * del firmware, 11,7/s con 300 s de audio-. main.c repinta el panel ENTERO
 * cada vez que ese numero cambia.
 *
 * ESTE FALLO HA SALIDO TRES VECES, Y LAS TRES SON EL MISMO:
 *
 *   1. en el demodulador del 4285 (02/10/2026): se arreglo ahi y se dejo el
 *      camino del identificador como estaba.
 *   2. en el 4415 que entregue en la V3.89: devolvia "hay noticia" en cada
 *      dibit, setenta y cinco veces por segundo.
 *   3. en el identificador, que es el de ahora, y llevaba ahi desde el
 *      principio.
 *
 * Tres veces el mismo fallo en una semana es que faltaba ESTO: un banco que
 * cuente los repintados. Los comentarios de stanag_modo.c lo explicaban de
 * maravilla y no impidieron ninguna de las tres.
 *
 * COMO SE MIDE. Se alimenta el modo con audio igual que la radio y se mira
 * stanag_modo_latido() una vez por vuelta del bucle, como hace main.c,
 * contando cuantas veces CAMBIA por segundo de audio. El reloj de
 * milisegundos corre a su ritmo de verdad.
 *
 * LOS TOPES, Y POR QUE SON TAN BAJOS. Historia de este numero en el panel de
 * "Auto" con el bucle a 60 vueltas por segundo:
 *
 *     al principio        14,7 /s   (el latido sumaba la cuenta de medidas)
 *     V3.92                4,0 /s   (el reloj, cuatro por segundo, pase lo que pase)
 *     V3.93                0,4 /s   (se repinta cuando CAMBIA el texto, y ya)
 *
 * Y no basto con la V3.92: el usuario siguio diciendo "se sigue tostando y no
 * dejandome darle a los botones". Cuatro repintados por segundo de un panel
 * entero por un bus SPI se notan igual, y en reposo los cuatro dejaban la
 * pantalla exactamente como estaba.
 *
 * Asi que los topes son 1,0 en reposo y 4,0 con señal: por encima de eso
 * alguien ha vuelto a repintar por reloj en vez de por contenido.
 */
#include "stanag_modo.h"
#include "stanag_det.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-44s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

static float g_ram[STANAG_DET_FLOATS];
static int   g_prestada;
void hfdl_ram_coge(void)   { g_prestada = 1; }
void hfdl_ram_suelta(void) { g_prestada = 0; }
uint8_t *hfdl_ram_todo(uint32_t *cap)
{
    if (!g_prestada) { if (cap) { *cap = 0U; } return 0; }
    if (cap) { *cap = (uint32_t)sizeof g_ram; }
    return (uint8_t *)(void *)g_ram;
}
uint8_t waterfall_prestada(void) { return (uint8_t)g_prestada; }
volatile uint32_t g_msticks = 0U;

/* Los mide main.c en la radio. Aqui, cero: el banco no dibuja nada, asi que
 * no hay vuelta de bucle que medir. Se cumplen para poder enlazar. */
uint16_t ui_bucle_peor_us(void) { return 0U; }
uint16_t ui_polls_peor_us(void) { return 0U; }

static unsigned long sem = 7777u;
static float ruido(void)
{
    sem = sem * 1103515245u + 12345u;
    return (float)((long)((sem >> 16) & 0x7FFFu) - 16383) / 16383.0f;
}

/*
 * SEGUNDOS de audio por el modo, mirando el latido una vez por vuelta.
 * `vueltas_s` es a que velocidad va el bucle principal de la radio; se
 * prueban dos, porque el fallo justamente se agrava cuanto mas rapido va.
 */
static double cuenta(int segundos, int vueltas_s, int con_tono)
{
    const uint32_t BL = 256U;
    uint32_t total = (uint32_t)segundos * 12000U;
    uint32_t hechas = 0U;
    uint32_t ant = 0xFFFFFFFFU;
    unsigned long cambios = 0U;
    long t = 0;
    float buf[256];
    double por_vuelta = 12000.0 / (double)vueltas_s;  /* muestras por vuelta */
    double deuda = 0.0;

    g_msticks = 0U;
    stanag_modo_borra();

    while (hechas < total) {
        uint32_t i;
        for (i = 0U; i < BL; i++) {
            double x = 0.3 * ruido();
            if (con_tono) {
                /* una rafaga periodica de 1,5 kHz: hace que el detector mida
                 * cosas distintas y el panel vaya sacando renglones */
                double fase = fmod((double)t / 12000.0, 0.10667);
                if (fase < 0.05) { x += 0.8 * sin(2.0 * M_PI * 1500.0 * (double)t / 12000.0); }
            }
            buf[i] = (float)x;
            t++;
        }
        stanag_modo_mete(buf, (uint16_t)BL);
        hechas += BL;
        g_msticks = (uint32_t)((double)hechas / 12.0);   /* 12 muestras por ms */

        deuda += (double)BL;
        while (deuda >= por_vuelta) {
            uint32_t l;
            deuda -= por_vuelta;
            stanag_modo_poll();
            l = stanag_modo_latido();
            if (l != ant) { cambios++; ant = l; }
        }
    }
    return (double)cambios / (double)segundos;
}

int main(void)
{
    char det[128];
    double a, b, c;

    printf("\nREPINTADOS POR SEGUNDO DEL PANEL DE STANAG\n");
    printf("==========================================\n\n");

    if (!stanag_modo_start()) { printf("  el modo no arranca\n"); return 1; }
    stanag_modo_forma_pon(STANAG_AUTO);

    a = cuenta(20, 60, 0);
    printf("  Auto, ruido,        bucle a  60 vueltas/s ->  %.1f repintados/s\n", a);
    b = cuenta(20, 200, 0);
    printf("  Auto, ruido,        bucle a 200 vueltas/s ->  %.1f repintados/s\n", b);
    c = cuenta(20, 60, 1);
    printf("  Auto, con señal,    bucle a  60 vueltas/s ->  %.1f repintados/s\n", c);
    printf("\n");

    snprintf(det, sizeof det, "(%.1f/s con el bucle a 60)", a);
    di("en reposo no pasa de uno por segundo", a <= 1.0, det);
    snprintf(det, sizeof det, "(%.1f/s con el bucle a 200)", b);
    di("y no sube porque el bucle vaya mas rapido", b <= 1.0, det);
    snprintf(det, sizeof det, "(%.1f/s)", c);
    di("ni se dispara porque haya señal que identificar", c <= 4.0, det);

    stanag_modo_stop();
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

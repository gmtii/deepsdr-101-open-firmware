/*
 * Banco del modo: del periodo medido al NOMBRE.
 *
 * El banco del detector comprueba que mide bien. Este comprueba lo otro:
 * que al numero medido se le pone la etiqueta correcta, y -mas importante-
 * que cuando NO es ninguna de las conocidas lo dice en vez de forzar la
 * mas cercana.
 *
 * La señal de prueba no es un 4285 de verdad: es una señal con trama de
 * 256 simbolos a 2400 baudios, que es lo que hace que un 4285 sea
 * reconocible por aqui. Ver sim/stanag_det_test.c.
 */
#include "stanag_modo.h"
#include "stanag_det.h"
#include "stanag_rx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define FS  12000U
#define SPS 5U

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/*
 * La memoria que en la radio presta la cascada. Aqui es un array estatico,
 * y los cuatro ganchos de hfdl_ram/waterfall se cumplen a mano: asi el
 * banco prueba stanag_modo.c de verdad -con su camino de pedir y soltar-
 * sin arrastrar media radio detras.
 */
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

/* El reloj de milisegundos lo lleva main.c en la radio; aqui corre a mano.
 * Lo usa stanag_modo_latido(), que es lo que hace que el panel se repinte
 * aunque no haya nada que detectar. */
volatile uint32_t g_msticks = 0U;

/* Los mide main.c en la radio. Aqui, cero: el banco no dibuja nada, asi que
 * no hay vuelta de bucle que medir. Se cumplen para poder enlazar. */
/* uint32_t, que es lo que declara main.c. Estaban como uint16_t: el
 * enlazador de C no compara tipos entre ficheros, asi que no se quejaba
 * -y el dia que uno de estos devolviera algo de verdad, el banco habria
 * leido media palabra-. 19/10/2026. */
uint32_t ui_bucle_peor_us(void)  { return 0U; }
uint32_t ui_polls_peor_us(void)  { return 0U; }
uint32_t ui_dibujo_peor_us(void) { return 0U; }

static unsigned long s_sem = 7777u;
static float uni(void)
{
    s_sem = s_sem * 1103515245u + 12345u;
    return (float)((s_sem >> 16) & 0x7FFFu) / 32767.0f;
}

static void genera(float *x, uint32_t n, uint32_t pre, uint32_t dat)
{
    static uint8_t preamb[512];
    uint32_t i, k = 0U, trama = pre + dat;
    unsigned long guarda = s_sem;

    s_sem = 0xACE1u;
    for (i = 0U; i < pre && i < 512U; i++) { preamb[i] = (uint8_t)(uni() * 7.99f); }
    s_sem = guarda;

    while (k < n) {
        uint32_t s;
        for (s = 0U; s < trama && k < n; s++) {
            uint8_t simb = (s < pre) ? preamb[s] : (uint8_t)(((uint32_t)(uni()*8.0f)) & 7U);
            float ph = (2.0f * 3.14159265f * (float)simb) / 8.0f;
            uint32_t m;
            for (m = 0U; m < SPS && k < n; m++) {
                x[k] = cosf(2.0f*3.14159265f*1800.0f*((float)k/(float)FS) + ph);
                k++;
            }
        }
    }
}

static void corre(const float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0U; i + 64U <= n; i += 64U) {
        stanag_modo_mete(&x[i], 64U);
        stanag_modo_poll();
    }
}

int main(void)
{
    static float x[FS * 4U];

    printf("\n=== El modo: del periodo al nombre ===\n\n");

    printf("  --- una trama de 106,67 ms, que es la del 4285\n");
    di("arranca y coge la memoria prestada", stanag_modo_start() == 1U);
    di("y se da por activo", stanag_modo_activo() == 1U);
    genera(x, FS * 4U, 80U, 176U);
    corre(x, FS * 4U);
    printf("      estado: %s\n", stanag_modo_estado_txt());
    printf("      renglon: %s\n", stanag_modo_linea(0));
    di("mide el periodo del 4285",
       stanag_modo_periodo() >= 10560U && stanag_modo_periodo() <= 10780U);
    di("y lo IDENTIFICA como 4285", stanag_modo_encaja() == STANAG_4285);

    printf("\n  --- una trama que no es de nadie conocido\n");
    {
        /* 200 simbolos a 2400 baudios son 83,3 ms: no esta en la tabla y
         * ademas no es multiplo ni divisor de ninguna que lo este. */
        stanag_modo_stop();
        di("al parar suelta la memoria", g_prestada == 0);
        stanag_modo_start();
        genera(x, FS * 4U, 60U, 140U);
        corre(x, FS * 4U);
        printf("      estado: %s\n", stanag_modo_estado_txt());
        printf("      renglon: %s\n", stanag_modo_linea(0));
        di("la mide igual", stanag_modo_periodo() >= 8200U && stanag_modo_periodo() <= 8460U);
        di("y NO se la atribuye a nadie", stanag_modo_encaja() == 0U);
    }

    /*
     * QUE NUNCA SE QUEDE LA PANTALLA EN BLANCO SIN EXPLICACION.
     *
     * Pedido por el usuario, 02/10/2026. Y no salia:
     * el entrelazado largo tarda diez segundos en soltar el primer
     * caracter y hasta entonces el panel no tenia ni un renglon. Esto
     * exige que SIEMPRE haya algo que leer.
     */
    printf("\n  --- mientras no hay texto, que diga que esta haciendo\n");
    {
        uint8_t f;
        int bien = 1;
        for (f = 0U; f < STANAG_FORMAS; f++) {
            stanag_modo_stop();
            stanag_modo_forma_pon(f);
            if (!stanag_modo_start()) { continue; }
            if (stanag_modo_lineas() == 0U) { bien = 0; }
            if (stanag_modo_linea(0)[0] == '\0') { bien = 0; }
            if (stanag_modo_estado_txt()[0] == '\0') { bien = 0; }
            printf("      %-6s %-28s | %s\n", stanag_modo_forma_txt(f),
                   stanag_modo_estado_txt(), stanag_modo_linea(0));
        }
        di("todas las posiciones dicen algo, ninguna deja el panel vacio", bien);
    }
    {
        /* y el 4481 TIENE que demodular: es el 4285, esta medido */
        stanag_modo_stop();
        stanag_modo_forma_pon(STANAG_4481);
        (void)stanag_modo_start();
        di("el 4481 demodula, no solo identifica", stanag_modo_demod() == 1U);
        /*
         * EL BOTON BORRAR LLEGA HASTA EL DEMODULADOR.
         *
         * Pedido por el usuario, 02/10/2026.
         *
         * stanag_modo_borra() vaciaba los renglones del identificador,
         * que demodulando no se pintan. Aqui se comprueba el cable:
         * se le mete señal hasta que el demodulador cuenta algo, y
         * despues de borrar ese contador tiene que estar a cero. Si el
         * cable se vuelve a caer, esto se pone rojo.
         */
        {
            stanag_rx_est_t ea, eb;
            corre(x, FS * 4U);
            stanag_rx_estado(&ea);
            di("demodulando, el contador sube", (ea.busquedas + ea.corr_max) > 0U);
            stanag_modo_borra();
            stanag_rx_estado(&eb);
            di("y Borrar llega hasta el demodulador",
               eb.busquedas == 0U && eb.corr_max == 0U);
        }
        stanag_modo_stop();
        stanag_modo_forma_pon(STANAG_4529);
        (void)stanag_modo_start();
        di("y el 4529 no, que es otra forma de onda", stanag_modo_demod() == 0U);
        stanag_modo_stop();
        stanag_modo_forma_pon(STANAG_AUTO);
        (void)stanag_modo_start();
    }
    {
        di("y arranca en 600 bps, que es donde hay texto en claro",
           stanag_modo_vel() == STANAG_RX_600);
        di("con entrelazado largo", stanag_modo_entre() == 1U);
    }

    printf("\n  --- el boton de la forma de onda\n");
    {
        uint8_t f;
        int bien = 1;
        for (f = 0U; f < STANAG_FORMAS; f++) {
            stanag_modo_forma_pon(f);
            if (stanag_modo_forma() != f) { bien = 0; }
            if (stanag_modo_forma_txt(f)[0] == '\0') { bien = 0; }
        }
        di("las cinco posiciones se ponen y tienen rotulo", bien);
        stanag_modo_forma_pon(99U);
        di("y una que no existe cae en Auto", stanag_modo_forma() == STANAG_AUTO);
    }

    stanag_modo_stop();
    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

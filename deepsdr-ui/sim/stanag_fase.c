/*
 * LA FASE DE BYTE DEL 4285: EL FALLO QUE DEJABA EL TEXTO ILEGIBLE.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * El Viterbi suelta bits, uno detras de otro, sin principio. Hasta el
 * 08/10/2026 el demodulador los partia en bytes de ocho empezando por
 * donde cayera el primero, y eso es acertar una de ocho.
 *
 * Con una grabacion no se puede ver: la unica que hay acierta de
 * casualidad. Se ve con el transmisor de sim/stanag_tx.h, que genera la
 * forma de onda y puede CORRER LA FASE A PROPOSITO metiendo bits de
 * relleno delante del mensaje. Lo que este banco hace es, para cada
 * combinacion de velocidad y entrelazado, probar los ocho desfases y
 * exigir la frase en los ocho.
 *
 * Y lo hace DOS VECES:
 *
 *   con marcas  - el mensaje lleva SOM delante y EOM detras, que es lo
 *                 que manda la norma. La fase sale exacta de ahi.
 *   sin marcas  - como la grabacion del aire, que engancha con el
 *                 mensaje ya empezado y no trae SOM. Aqui la fase tiene
 *                 que salir de contar caracteres imprimibles, que es lo
 *                 unico que queda.
 *
 * El segundo caso es el que importa: una radio que solo lee mensajes
 * que empiezan despues de sintonizar no sirve de nada.
 *
 * EL TRANSMISOR NO SE VALIDA SOLO. Un transmisor equivocado y un
 * receptor equivocado del mismo modo se entienden perfectamente. Por eso
 * la primera prueba de todas es 300 bps con entrelazado largo, que es
 * justo lo que lleva la grabacion del aire con la que el receptor ya
 * esta validado: si el transmisor mintiera, esa no saldria.
 */
#include "stanag_tx.h"
#include "stanag_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-52s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

#define FRASE "This is a Stanag4481 test message. "

static float   trabajo[STANAG_RX_FLOATS];
static stx_t   tx;
static uint8_t bits[600000];
static float   aud[12000 * 40];
static char    todo[8192];

/* Sintetiza y demodula. Devuelve 1 si salio la frase. */
static int prueba(int vel, int largo, int desfase, int marcas, unsigned *ber)
{
    uint32_t nb, na, i;
    stanag_rx_est_t e;

    stx_init(&tx, vel, largo);
    tx.sin_marcas = (uint8_t)(marcas ? 0 : 1);
    tx.desfase    = (uint8_t)desfase;
    nb = stx_mensaje(&tx, FRASE, bits, sizeof bits);
    na = stx_audio(&tx, bits, nb, aud, sizeof aud / sizeof *aud);

    stanag_rx_auto_pon(0);
    stanag_rx_largo_pon((uint8_t)largo);
    stanag_rx_velocidad_pon((uint8_t)vel);
    stanag_rx_formato_pon(STANAG_RX_ASCII);
    if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) { return -1; }
    for (i = 0U; i + 64U <= na; i += 64U) {
        stanag_rx_mete(&aud[i], 64U);
        (void)stanag_rx_paso();
    }
    stanag_rx_estado(&e);
    if (ber) { *ber = e.ber; }
    todo[0] = '\0';
    for (i = 0U; i < stanag_rx_nlineas(); i++) {
        strncat(todo, stanag_rx_linea((uint8_t)i), sizeof todo - strlen(todo) - 1U);
    }
    return (strstr(todo, "Stanag4481 test") != 0) ? 1 : 0;
}

int main(void)
{
    int v, L, d, m;
    char det[160];

    printf("\nLA FASE DE BYTE DEL 4285\n");
    printf("========================\n\n");

    /* 1. el transmisor contra la realidad ------------------------------- */
    {
        unsigned ber = 0U;
        int ok = prueba(STANAG_RX_300, 1, 0, 1, &ber);
        snprintf(det, sizeof det, "300/largo, ber %u %%", ber);
        di("el transmisor genera un 4285 que el receptor lee", ok == 1, det);
        if (ok != 1) {
            printf("\n  sin esto lo de abajo no significa nada: se para aqui.\n\n");
            return 1;
        }
    }

    /* 2. las once combinaciones, los ocho desfases, con y sin marcas ---- */
    for (m = 1; m >= 0; m--) {
        int total = 0, bien = 0;
        printf("\n  %s:\n", m ? "con SOM y EOM, que es lo que manda la norma"
                              : "SIN marcas, como la grabacion del aire");
        printf("      %-5s %-6s  desfase 0 1 2 3 4 5 6 7\n", "vel", "entr");
        for (L = 1; L >= 0; L--) {
            for (v = 0; v < STANAG_RX_VELOCIDADES; v++) {
                char fila[64];
                int n = 0, salen = 0, salta = 0;
                for (d = 0; d < 8; d++) {
                    int r = prueba(v, L, d, m, 0);
                    if (r < 0) { salta = 1; break; }
                    fila[n++] = (char)(r ? '.' : 'X');
                    fila[n++] = ' ';
                    if (r) { salen++; }
                    total++;
                }
                fila[n] = '\0';
                if (salta) { continue; }
                bien += salen;
                printf("      %-5s %-6s          %s %s\n",
                       stanag_rx_velocidad_txt((uint8_t)v), L ? "largo" : "corto",
                       fila, (salen == 8) ? "" : "  <-- AQUI");
            }
        }
        snprintf(det, sizeof det, "%d de %d", bien, total);
        di(m ? "sale la frase en los ocho desfases, con marcas"
             : "y tambien sin marcas, solo contando imprimibles",
           bien == total, det);
    }

    printf("\n  (un punto es que salio la frase; una X que no)\n");
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

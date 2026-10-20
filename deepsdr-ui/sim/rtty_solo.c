/*
 * EL RTTY, AJUSTANDOSE SOLO.
 *
 * Se arranca el decodificador MAL PUESTO a proposito -tonos donde no
 * estan, velocidad que no es, polaridad del reves- y se mira si llega el
 * solo a sacar el boletin. Que es lo que le va a pasar al usuario cada vez
 * que se tropiece con un RTTY que no sabe de que es.
 *
 * Y la otra mitad, que importa igual: arrancando BIEN puesto, que no toque
 * nada. Un ajuste automatico que se mueve cuando ya va bien es peor que no
 * tenerlo.
 */
#include "rtty.h"
#include "rtty_auto.h"
#include "rtty_scope.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos;

static void di(const char *q, int bien)
{
    printf("    %-52s %s\n", q, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* Una pasada entera. Devuelve el texto en `out`. */
static void corre(const char *fich, float mark, float space, float baud,
                  char *out, size_t max, int ensena)
{
    FILE *f = fopen(fich, "rb");
    float buf[RTTY_BLOCK_SAMPLES];
    size_t n, nt = 0U;
    unsigned bloques = 0U;
    uint16_t vista = 0U;
    char c;

    if (!f) { printf("    no puedo abrir %s\n", fich); fallos++; out[0]='\0'; return; }
    rtty_init();
    rtty_scope_init();
    rtty_auto_reset();
    rtty_auto_set(1U);
    rtty_set_enabled(1U);
    rtty_set_mark_space_hz(mark, space);
    rtty_set_baud(baud);

    while ((n = fread(buf, sizeof buf[0], RTTY_BLOCK_SAMPLES, f)) == RTTY_BLOCK_SAMPLES) {
        rtty_process(buf, RTTY_BLOCK_SAMPLES);
        rtty_scope_feed(buf, RTTY_BLOCK_SAMPLES);
        /* el bucle principal mira el oscilador y el auto unas pocas veces
         * por segundo, no en cada bloque */
        if ((++bloques % 12U) == 0U) {
            rtty_scope_poll(); rtty_auto_poll();
            if (ensena > 1 && vista != rtty_salud_n()) {
                vista = rtty_salud_n();
                printf("        v%-3u salud %3u%%  marca %6.0f esp %6.0f  %6.2f Bd\n",
                       (unsigned)rtty_salud_n(), (unsigned)rtty_salud_pc(),
                       (double)rtty_get_mark_hz(), (double)rtty_get_space_hz(),
                       (double)rtty_get_baud());
            }
        }
        while (rtty_get_char(&c)) { if (nt < max - 1U) { out[nt++] = c; } }
    }
    fclose(f);
    out[nt] = '\0';
    printf("      acaba en  marca %.0f  espacio %.0f  %.2f Bd   "
           "(tonos movidos %u, velocidad %u, inversion %u)\n",
           (double)rtty_get_mark_hz(), (double)rtty_get_space_hz(),
           (double)rtty_get_baud(), (unsigned)rtty_auto_tonos_veces(),
           (unsigned)rtty_auto_baud_veces(), (unsigned)rtty_auto_inv_veces());
    if (ensena) {
        unsigned i;
        printf("      ");
        for (i = 0U; i < nt; i++) {
            char q = out[i];
            putchar((q == '\r' || q == '\n') ? ' ' : ((q >= 32 && q < 127) ? q : '.'));
        }
        printf("\n");
    }
    {
        uint16_t p[4]; uint8_t i;
        rtty_auto_puntos(p);
        printf("      rejilla: 45,45=%.3f  50=%.3f  75=%.3f  100=%.3f\n",
               p[0]/1000.0, p[1]/1000.0, p[2]/1000.0, p[3]/1000.0);
        (void)i;
    }
}

/* Cuantas veces aparece una aguja en el pajar. */
static int cuenta(const char *h, const char *a)
{
    int n = 0; const char *p = h;
    while ((p = strstr(p, a)) != 0) { n++; p++; }
    return n;
}

int main(int argc, char **argv)
{
    static char t[16384];
    const char *f = (argc > 1) ? argv[1] : "sim/muestras/rtty3712.f32";

    printf("\n  EL RTTY AJUSTANDOSE SOLO  (%s)\n", f);

    printf("\n  1. arrancando TODO mal: tonos a 2525/2075, 100 Bd\n");
    corre(f, 2525.0f, 2075.0f, 100.0f, t, sizeof t, 2);
    di("acaba en 50 baudios", rtty_get_baud() > 49.0f && rtty_get_baud() < 51.0f);
    di("acaba con los tonos encima de la señal (centro 1140-1165 Hz)",
       (rtty_get_mark_hz() + rtty_get_space_hz()) * 0.5f > 1140.0f &&
       (rtty_get_mark_hz() + rtty_get_space_hz()) * 0.5f < 1165.0f);
    /* Veintiseis segundos de grabacion y unos veinte en converger desde el
     * ajuste mas malo posible: lo que se le puede pedir es que al FINAL
     * este decodificando, no que saque el boletin entero. */
    di("acaba decodificando (sale \"Z:\" en el ultimo tercio)",
       cuenta(t + (strlen(t) * 2) / 3, "Z:") >= 1);

    printf("\n  2. arrancando BIEN puesto: no tiene que tocar nada\n");
    corre(f, 1374.0f, 927.0f, 50.0f, t, sizeof t, 0);
    di("no cambia la velocidad", rtty_auto_baud_veces() == 0U);
    di("no se da la vuelta", rtty_auto_inv_veces() == 0U);
    di("y sigue sacando el boletin", cuenta(t, "Z:") >= 2);

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

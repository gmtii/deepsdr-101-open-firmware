/*
 * EL 4285 DE VERDAD, ROTO A PROPOSITO CON MULTITRAYECTO.
 *
 * Pedido por el usuario.
 *
 * Antes de escribir una linea del 4539 o del 4415 hay que saber UNA cosa:
 * donde se rompe hoy el demodulador que ya tenemos, y cuanto de eso es culpa
 * del eco. Porque lo que le falta a los dos STANAG nuevos -un ecualizador- no
 * es una pieza suelta: es exactamente lo que le falta TAMBIEN al 4285 que ya
 * funciona, y aqui hay una grabacion de verdad con la que medirlo.
 *
 * La grabacion limpia decodifica PERFECTA: sondeos 100%, BER 0%, y sale el
 * texto. O sea que sobre ella no se puede demostrar nada: no hay margen. Asi
 * que este banco la ROMPE, con un canal de dos rayos de retardo y
 * profundidad conocidos, y busca el punto exacto en el que deja de salir el
 * texto.
 *
 * Eso da la vara de medir que hace falta para todo lo que venga despues: si
 * un ecualizador vale para algo, ese punto se tiene que mover.
 *
 * EL CANAL. Dos rayos, el segundo retrasado `tau` milisegundos y `a` veces
 * mas flojo:
 *
 *     y[n] = x[n] + a * x[n - tau*fs]
 *
 * A 2400 baudios un simbolo dura 0,417 ms, asi que 1 ms de retardo son ya
 * dos simbolos y medio de interferencia entre simbolos. Los retardos tipicos
 * de HF a media distancia van de 0,5 a 3 ms, y eso es justo lo que no puede
 * un modem de tono serie sin ecualizador.
 */
#include "stanag_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-50s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* --- lector de wav, el mismo que usa sim/stanag_rx_aire.c --------------- */

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out, uint16_t *can_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12];
    uint16_t canales = 1U, bits = 16U;
    uint32_t hz = 0UL;
    float *x = 0;

    if (!f) { printf("  no puedo abrir %s\n", ruta); return 0; }
    if (fread(cab, 1, 12, f) != 12U) { fclose(f); return 0; }
    for (;;) {
        uint8_t ch[8];
        uint32_t len;
        if (fread(ch, 1, 8, f) != 8U) { break; }
        len = le32(&ch[4]);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fm[16];
            if (fread(fm, 1, (len < 16U) ? len : 16U, f) != ((len < 16U) ? len : 16U)) { break; }
            canales = le16(&fm[2]); hz = le32(&fm[4]); bits = le16(&fm[14]);
            if (len > 16U) { fseek(f, (long)(len - 16U), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t n = len / (uint32_t)((bits / 8U) * canales);
            uint32_t i;
            int16_t *raw = (int16_t *)malloc(len);
            if (!raw) { break; }
            if (fread(raw, 1, len, f) != len) { free(raw); break; }
            x = (float *)malloc((size_t)n * canales * sizeof *x);
            for (i = 0U; i < n * canales; i++) { x[i] = (float)raw[i] / 32768.0f; }
            free(raw);
            *n_out = n; *hz_out = hz; *can_out = canales;
            fclose(f);
            return x;
        } else {
            fseek(f, (long)len, SEEK_CUR);
        }
        if (len & 1U) { fseek(f, 1L, SEEK_CUR); }
    }
    fclose(f);
    return 0;
}

/* --- una pasada entera por el demodulador de la radio ------------------- */

#define FS_RX 12000.0f

static float trabajo[STANAG_RX_FLOATS];
static char  todo[8192];

typedef struct { unsigned sondeos, ber, tramas, engancha; int texto; } res_t;

static res_t pasada(const float *y, uint32_t m)
{
    res_t r;
    stanag_rx_est_t e;
    uint32_t i;

    memset(&r, 0, sizeof r);
    stanag_rx_largo_pon(1U);
    stanag_rx_velocidad_pon(STANAG_RX_300);
    if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) { return r; }

    for (i = 0U; i + 64U <= m; i += 64U) {
        stanag_rx_mete(&y[i], 64U);
        (void)stanag_rx_paso();
    }
    stanag_rx_estado(&e);

    todo[0] = '\0';
    for (i = 0U; i < stanag_rx_nlineas(); i++) {
        strncat(todo, stanag_rx_linea((uint8_t)i), sizeof todo - strlen(todo) - 1U);
    }
    r.sondeos  = e.sondeos;
    r.ber      = e.ber;
    r.tramas   = e.tramas;
    r.engancha = e.engancha;
    /* La frase que lleva dentro la grabacion. Es la unica prueba que no se
     * puede falsear: un Viterbi SIEMPRE devuelve bits con buena cara. */
    r.texto    = (strstr(todo, "Stanag") != 0) || (strstr(todo, "test message") != 0);
    return r;
}

/* el canal de dos rayos */
static void canal(const float *x, float *y, uint32_t n, float tau_ms, float a)
{
    uint32_t d = (uint32_t)((tau_ms / 1000.0f) * FS_RX + 0.5f);
    uint32_t i;
    for (i = 0U; i < n; i++) {
        y[i] = x[i] + ((i >= d) ? (a * x[i - d]) : 0.0f);
    }
}

int main(int argc, char **argv)
{
    uint32_t n = 0UL, hz = 0UL, m, i;
    uint16_t canales = 1U;
    float *x, *y, *z;
    double paso;
    res_t limpio;
    char det[128];

    if (argc < 2) { printf("uso: %s fichero.wav\n", argv[0]); return 2; }
    printf("\nEL 4285 CONTRA EL MULTITRAYECTO\n\n");

    x = lee_wav(argv[1], &n, &hz, &canales);
    if (!x) { return 2; }

    /* a 12 kHz, que es lo que come el demodulador */
    m = (uint32_t)((double)n * (double)FS_RX / (double)hz);
    y = (float *)malloc((size_t)m * sizeof *y);
    z = (float *)malloc((size_t)m * sizeof *z);
    paso = (double)hz / (double)FS_RX;
    for (i = 0U; i < m; i++) {
        double t = (double)i * paso;
        uint32_t k = (uint32_t)t;
        double fr = t - (double)k;
        if ((k + 1U) >= n) { k = n - 2U; fr = 1.0; }
        y[i] = (float)((1.0 - fr) * (double)x[k * canales] + fr * (double)x[(k + 1U) * canales]);
    }
    printf("  %u muestras a %u Hz -> %u a 12 kHz (%.1f s)\n\n",
           (unsigned)n, (unsigned)hz, (unsigned)m, (double)m / 12000.0);

    /* --- la referencia: la grabacion tal cual --------------------------- */
    limpio = pasada(y, m);
    snprintf(det, sizeof det, "sondeos %u%%  BER %u%%  tramas %u",
             limpio.sondeos, limpio.ber, limpio.tramas);
    di("la grabacion limpia sale entera", limpio.texto && limpio.sondeos >= 90U, det);

    /* --- y ahora rota ---------------------------------------------------- */
    printf("\n  con un eco al 70%% del principal, segun el retardo\n");
    printf("      %-9s %8s %6s %7s %s\n", "retardo", "sondeos", "BER", "tramas", "texto");
    {
        static const float taus[7] = { 0.2f, 0.5f, 0.8f, 1.2f, 1.7f, 2.5f, 3.5f };
        int ultimo_bueno = -1;
        unsigned peor_sondeos = 100U;
        int k;
        for (k = 0; k < 7; k++) {
            res_t r;
            canal(y, z, m, taus[k], 0.70f);
            r = pasada(z, m);
            printf("      %6.1f ms %7u%% %5u%% %7u   %s\n",
                   (double)taus[k], r.sondeos, r.ber, r.tramas,
                   r.texto ? "SI" : "no");
            if (r.texto) { ultimo_bueno = k; }
            if (r.sondeos < peor_sondeos) { peor_sondeos = r.sondeos; }
        }
        if (ultimo_bueno >= 0) {
            snprintf(det, sizeof det, "aguanta hasta %.1f ms", (double)taus[ultimo_bueno]);
        } else {
            snprintf(det, sizeof det, "no aguanta ni 0,2 ms");
        }
        /*
         * *** Y AQUI SALIO ALGO QUE NO ME ESPERABA, 07/10/2026. ***
         *
         * El 4285 a 300 bps con entrelazado largo AGUANTA LOS 3,5 ms. Los
         * sondeos bajan del 100% al 77 -el eco esta haciendo daño de verdad,
         * el canal funciona- y aun asi el BER se queda en 0% y la frase sale
         * entera. El Viterbi y el entrelazador se lo comen.
         *
         * Eso cambia una suposicion con la que se venia trabajando: que lo
         * que les falta a los STANAG nuevos -un ecualizador- le hacia falta
         * tambien al 4285. A 300 bps NO. La interferencia entre simbolos
         * aprieta donde hay poca correccion de errores por simbolo y
         * constelaciones de mas de dos puntos, o sea en las velocidades
         * ALTAS del 4539, no aqui.
         *
         * Asi que esto no exige que se rompa: exige que el canal haga daño
         * MEDIBLE -si no, el banco no estaria midiendo nada- y deja escrito
         * donde esta hoy la frontera, para poder compararla el dia que haya
         * un ecualizador de verdad en el camino.
         */
        {
            int danyo = (int)limpio.sondeos - (int)peor_sondeos;
            snprintf(det, sizeof det, "los sondeos bajan %d puntos, hasta %u%%",
                     danyo, peor_sondeos);
            di("el eco hace daño medible en los sondeos", danyo >= 15, det);
        }
        snprintf(det, sizeof det, "aguanta hasta %.1f ms",
                 (double)((ultimo_bueno >= 0) ? taus[ultimo_bueno] : 0.0f));
        di("y aun asi el texto sigue saliendo (300 bps, largo)",
           ultimo_bueno == 6, det);
    }

    printf("\n  y con el eco a 1,2 ms, segun lo fuerte que sea\n");
    printf("      %-9s %8s %6s %7s %s\n", "eco", "sondeos", "BER", "tramas", "texto");
    {
        static const float aes[6] = { 0.2f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f };
        int k, ultimo = -1;
        unsigned ult_sond = 0U, ult_ber = 0U;
        for (k = 0; k < 6; k++) {
            res_t r;
            canal(y, z, m, 1.2f, aes[k]);
            r = pasada(z, m);
            printf("      %7.0f%% %7u%% %5u%% %7u   %s\n",
                   (double)(aes[k] * 100.0f), r.sondeos, r.ber, r.tramas,
                   r.texto ? "SI" : "no");
            if (r.texto) { ultimo = k; }
            if (k == 5) { ult_sond = r.sondeos; ult_ber = r.ber; }
        }
        /*
         * Con el eco al 100% -dos rayos iguales, el caso en el que la
         * respuesta del canal tiene un CERO JUSTO EN LA CIRCUNFERENCIA
         * UNIDAD y una parte del espectro desaparece del todo- los sondeos
         * se quedan en la mitad y el BER sube al 2%, y la frase SIGUE
         * saliendo. Queda escrito: es el techo contra el que habra que
         * comparar cualquier cosa que se añada.
         */
        snprintf(det, sizeof det, "con dos rayos iguales: sondeos %u%%, BER %u%%",
                 ult_sond, ult_ber);
        di("ni con el eco al 100% pierde el texto", ultimo == 5, det);
    }

    free(x); free(y); free(z);
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

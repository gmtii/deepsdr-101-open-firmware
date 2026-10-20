/*
 * Banco de la COLA DE PITIDOS (User/bip.c del firmware, sin cambios).
 *
 * LO QUE SE MIDE: que la cola se pueda usar MUCHAS veces. El fallo que
 * motiva este banco no era que sonara mal, era que despues de la primera
 * captura la cola se quedaba marcada como llena para siempre y todas las
 * peticiones siguientes se tiraban en silencio.
 *
 * Y la causa estaba en el reparto: la vaciaba la interrupcion, dentro de
 * su bucle de muestras, asi que si el ultimo tramo acababa JUSTO con la
 * ultima muestra del bloque -que a 96 kHz y bloques de 256 pasa siempre
 * con los 120 ms del pitido de "hecho": son 45 bloques exactos- el bucle
 * terminaba antes de llegar a vaciarla.
 *
 * Por eso aqui no se prueba "suena": se prueba VEINTE VECES SEGUIDAS la
 * secuencia entera de una captura -aviso de empezar, un rato, aviso de
 * acabar- y se exige que las veinte metan audio. Y se hace con tamaños de
 * bloque que dividen exacto y con otros que no, porque el caso que fallaba
 * es justo el que divide exacto.
 */
#include "bip.h"
#include <stdio.h>
#include <string.h>

static int fallos;

static void mal(const char *que, unsigned v)
{
    printf("FALLO: %s (%u)\n", que, v);
    fallos++;
}

/* Deja correr n bloques de audio y devuelve cuantas muestras han salido
 * distintas de cero, que es "ha sonado algo". */
static unsigned corre(unsigned bloques, unsigned n, float fs)
{
    int16_t buf[2048];
    unsigned b, k, suena = 0U;

    for (b = 0U; b < bloques; b++) {
        memset(buf, 0, sizeof buf);
        bip_mete(buf, n, fs);
        for (k = 0U; k < n; k++) { if (buf[2U * k] != 0) { suena++; } }
    }
    return suena;
}

static void una_tanda(unsigned n, float fs, const char *como)
{
    unsigned v;

    for (v = 0U; v < 20U; v++) {
        unsigned s1, s2;

        bip_empieza();                 /* 900 - silencio - 900, 200 ms */
        s1 = corre(400U, n, fs);       /* de sobra para que acabe */
        if (s1 == 0U) { mal("el aviso de empezar no ha sonado", v); break; }

        (void)corre(50U, n, fs);       /* el rato de la captura */

        bip_listo();                   /* 1200, 120 ms */
        s2 = corre(400U, n, fs);
        if (s2 == 0U) { mal("el aviso de acabar no ha sonado", v); break; }

        if (bip_sonando()) { mal("se queda creyendo que suena algo", v); break; }
    }
    printf("  %-28s 20 capturas seguidas: %s\n", como, fallos ? "NO" : "si");
}

int main(void)
{
    /* El caso que fallaba: 96 kHz y 256 muestras. 120 ms son 11.520
     * muestras, o sea 45 bloques EXACTOS. */
    una_tanda(256U, 96000.0f, "96 kHz, 256 (el de la radio)");
    una_tanda(256U, 48000.0f, "48 kHz, 256");
    una_tanda(100U, 44100.0f, "44,1 kHz, 100 (no cuadra)");
    una_tanda(512U, 192000.0f, "192 kHz, 512");

    /* Y que pedir mas de los que caben no rompa nada: se tiran los de mas,
     * pero la cola tiene que seguir sirviendo despues. */
    {
        unsigned k;
        for (k = 0U; k < 50U; k++) { bip_pide(1000U, 10U); }
        (void)corre(2000U, 256U, 96000.0f);
        bip_listo();
        if (corre(400U, 256U, 96000.0f) == 0U) {
            mal("la cola se queda muda despues de desbordarla", 0U);
        }
    }

    if (fallos == 0) { printf("TODO CORRECTO: la cola de pitidos no se atasca\n"); }
    return fallos ? 1 : 0;
}

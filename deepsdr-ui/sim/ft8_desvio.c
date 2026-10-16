/*
 * EL DESVIO DE LA CAPTURA DE FT8 - 25/09/2026.
 *
 * POR QUE EXISTE
 * --------------
 * Capturar una ranura dura 14,88 s y decodificarla se lleva lo suyo: las dos
 * cosas no caben en los 15 s que dura una ranura. O sea que al cerrar una hay
 * que empezar la siguiente YA, y entonces la captura nueva escribe encima de
 * lo que el decodificador todavia esta leyendo.
 *
 * Antes eso se resolvia copiando mag[] ENTERO a un segundo buffer -23.808
 * bytes- y decodificando de la copia. Ahora se aparta lo NUEVO en vez de lo
 * viejo: mientras se decodifica, la captura escribe en un buffer pequeno
 * (6.144 bytes) y al acabar se vuelca al principio de mag[].
 *
 * Eso libero 17.664 bytes, con los que la banda de dibujo de gfx2 volvio a
 * 16 filas y el espectro a su velocidad. Pero mueve el sitio donde escribe la
 * captura segun un estado, y ese es justo el tipo de cambio que falla en
 * silencio: si el desvio se cuela y escribe en mag[], el decodificador lee
 * datos a medio pisar y saca menos mensajes. Nadie ve un fallo; se ve una
 * radio que decodifica peor.
 *
 * QUE COMPRUEBA, sobre los BYTES de verdad y no sobre lo que devuelve nadie:
 *
 *  1. Con la captura desviada, mag[] no cambia ni un byte por muchos
 *     subbloques que entren. Esta es LA comprobacion.
 *  2. is_full() no puede dar verdadero mientras esta desviada.
 *  3. Al reintegrar, lo capturado aparece al principio de mag[] y en el
 *     orden correcto, y num_blocks queda en lo que se capturo.
 *  4. Reiniciar la ranura estando desviada reinicia EL DESVIO, no mag[].
 *     (Esto es lo que pasa de verdad cada 15 s: el borde de ranura del reloj
 *     cae unas decimas despues de llenarse la captura.)
 *  5. Si la decodificacion tarda mas que el desvio entero, lo que no cabe se
 *     pierde PERO SE CUENTA. Un desbordamiento mudo seria el mismo fallo de
 *     antes con otro nombre.
 */
#include "ft8_shared_ram.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft1024.h"
#include "waterfall.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-58s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

static unsigned long firma_mag(void)
{
    const unsigned char *p = (const unsigned char *)g_ft8_shared_ram.ft8.mag;
    unsigned long h = 2166136261UL;
    size_t i;
    for (i = 0U; i < sizeof g_ft8_shared_ram.ft8.mag; i++) {
        h ^= p[i]; h *= 16777619UL;
    }
    return h;
}

/* Un subbloque con un tono cuya frecuencia depende de `semilla`, para que
 * cada uno deje una huella distinta en mag[] y se pueda comprobar el ORDEN,
 * no solo que "hay algo". */
static void mete(int semilla)
{
    static int16_t x[FT8_ADAPTER_SUBBLOCK_SIZE];
    int i;
    for (i = 0; i < FT8_ADAPTER_SUBBLOCK_SIZE; i++) {
        int k = ((i * (3 + semilla)) & 0x3F) - 32;
        x[i] = (int16_t)(k * 300);
    }
    ft8_waterfall_feed_subblock(x);
}

int main(void)
{
    unsigned long antes, despues;
    int i;

    printf("EL DESVIO DE LA CAPTURA DE FT8\n");
    printf("  mag %lu bytes, desvio %lu (%d bloques de %d)\n\n",
           (unsigned long)sizeof g_ft8_shared_ram.ft8.mag,
           (unsigned long)sizeof g_ft8_shared_ram.ft8.lado,
           FT8_ADAPTER_LADO_BLOQUES,
           FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS);

    waterfall_presta(1U);
    ft8_fft1024_init();
    ft8_waterfall_reset();

    /* --- normal: escribir mueve mag[] --------------------------------- */
    printf("  CAPTURA NORMAL\n");
    pasa("arranca sin desviar", ft8_waterfall_desviada() == 0U);
    antes = firma_mag();
    mete(1);
    despues = firma_mag();
    pasa("un subbloque SI cambia mag[]", antes != despues);

    /* --- desviada: mag[] congelado ------------------------------------ */
    printf("\n  CAPTURA DESVIADA (mientras se decodifica)\n");
    ft8_waterfall_cierra_ranura();
    pasa("queda marcada como desviada", ft8_waterfall_desviada() == 1U);
    pasa("is_full() es falso aunque num_blocks siga alto",
         ft8_waterfall_is_full() == false);

    antes = firma_mag();
    for (i = 0; i < 8; i++) { mete(10 + i); }
    despues = firma_mag();
    pasa("8 subbloques desviados NO tocan ni un byte de mag[]", antes == despues);

    /* --- el borde de ranura del reloj, estando desviada ---------------- */
    ft8_waterfall_reset();
    despues = firma_mag();
    pasa("reiniciar la ranura desviada tampoco toca mag[]", antes == despues);
    pasa("y sigue desviada", ft8_waterfall_desviada() == 1U);

    /* --- reintegrar: aparece al principio y en orden ------------------- */
    printf("\n  AL REINTEGRAR\n");
    {
        static uint8_t esperado[FT8_ADAPTER_LADO_BLOQUES
                                * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR
                                * FT8_ADAPTER_NUM_BINS];
        int paso = FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS;
        int n = 5;

        for (i = 0; i < n; i++) { mete(20 + i); }
        memcpy(esperado, g_ft8_shared_ram.ft8.lado, (size_t)(n * paso));

        ft8_waterfall_reintegra();

        pasa("ya no esta desviada", ft8_waterfall_desviada() == 0U);
        pasa("lo capturado aparece al principio de mag[], byte a byte",
             memcmp(g_ft8_shared_ram.ft8.mag, esperado, (size_t)(n * paso)) == 0);
        pasa("num_blocks queda en lo que se capturo de verdad",
             ft8_waterfall_get()->num_blocks == (int)n);
    }

    antes = firma_mag();
    mete(99);
    pasa("y la captura vuelve a escribir en mag[]", firma_mag() != antes);

    /* --- desbordar el desvio: se pierde, pero se cuenta ---------------- */
    printf("\n  SI LA DECODIFICACION TARDA DEMASIADO\n");
    {
        uint16_t d0 = ft8_waterfall_desbordes();
        ft8_waterfall_cierra_ranura();
        for (i = 0; i < FT8_ADAPTER_LADO_BLOQUES + 3; i++) { mete(30 + i); }
        pasa("lo que no cabe en el desvio se cuenta, no se pierde callando",
             ft8_waterfall_desbordes() > d0);
        printf("      desbordes: %u (metidos %d bloques en un desvio de %d)\n",
               (unsigned)(ft8_waterfall_desbordes() - d0),
               FT8_ADAPTER_LADO_BLOQUES + 3, FT8_ADAPTER_LADO_BLOQUES);
        ft8_waterfall_reintegra();
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

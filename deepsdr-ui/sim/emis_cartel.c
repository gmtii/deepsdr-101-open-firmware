/*
 * LO QUE CUESTA EL CARTEL DE LA CABECERA - 20/10/2026
 * ==================================================
 *
 * De una medida en antena: la fila "Vuelta del bucle" marco un pico de
 * 167 ms y, con el instrumento nuevo que dice de quien es, el tramo
 * culpable salio `pre` - todo lo que va ANTES del dibujo. Ahi dentro, lo
 * unico que puede durar cientos de milisegundos es la flash: el bus es de
 * patadas por GPIO y la base de emisoras son 206 kB.
 *
 * El camino es este, y pasa por franja_arriba_tick() -> emisoras_tick():
 *
 *     emisoras_busca(hz, 600, minuto, dia, r, 3)
 *     emisoras_texto(nombre)      <- tres cadenas mas
 *     emisoras_pais(pais)
 *
 * y se dispara al cambiar de FRECUENCIA (cuando se asienta el dial) o de
 * MINUTO. O sea: una vez por minuto siempre, y una por cada retoque del
 * dial.
 *
 * Esto cuenta los bytes que pasan por el bus en una de esas, sobre el
 * EMISORAS.BIN DE VERDAD, y los pasa a milisegundos con lo que cuesta un
 * byte en la radio. No hace falta la radio para saber si 167 ms cuadran.
 *
 * EL BUS: desde el 05/10/2026 va "rapido", o sea sin retardos, y un bit son
 * las cuatro escrituras a GPIO de spi_xfer_byte() -del orden de 100 ns-, o
 * sea unos 0,8 us por byte. Con el bus lento son 16 us por byte, veinte
 * veces mas. Las dos cuentas salen abajo porque las dos existen: el bus se
 * queda lento si la comprobacion del arranque no cuadra.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "emisoras.h"

static FILE *g_f;
static long  g_tam, g_lect, g_bytes;

static void lee(uint32_t a, uint8_t *b, uint32_t n)
{
    g_lect++; g_bytes += (long)n;
    memset(b, 0, n);
    if ((long)a >= g_tam) { return; }
    fseek(g_f, (long)a, SEEK_SET);
    if (fread(b, 1, n, g_f) != n) { /* el relleno de ceros ya esta puesto */ }
}

/* Lo que hace emis_pon() en main.c, sin la parte de pintar. */
static void un_cartel(uint32_t hz, uint16_t min_utc)
{
    emisoras_r_t r[3];
    char tmp[24];
    uint8_t n;

    n = emisoras_busca(hz, 600UL, min_utc, EMISORAS_DIA_CUALQUIERA, r, 3U);
    if (n == 0U) { return; }
    emisoras_texto(r[0].nombre, tmp, (uint8_t)sizeof tmp);
    emisoras_pais(r[0].pais, tmp, (uint8_t)sizeof tmp);
    if (r[0].sitio  != EMISORAS_SIN_TEXTO) { emisoras_texto(r[0].sitio,  tmp, (uint8_t)sizeof tmp); }
    if (r[0].idioma != EMISORAS_SIN_TEXTO) { emisoras_texto(r[0].idioma, tmp, (uint8_t)sizeof tmp); }
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "datos/EMISORAS.BIN";
    /* Dos de onda corta con emisora, dos de aficionados sin ninguna, y la
     * de la foto: 7.135 en 40 m, que es donde salio el pico. */
    static const uint32_t k_hz[] = {
        7135000UL, 7200000UL, 9600000UL, 15200000UL, 171000UL, 14074000UL
    };
    static const char *const k_que[] = {
        "7.135 kHz (40 m, el de la foto)", "7.200 kHz (40 m)",
        "9.600 kHz (31 m)", "15.200 kHz (19 m)",
        "171 kHz (onda larga)", "14.074 kHz (20 m, FT8)"
    };
    unsigned i;
    long peor = 0;
    /*
     * Y UN TOPE, para que esto sea un guardian y no solo una medida. La
     * busqueda es binaria sobre una tabla ordenada por frecuencia: lo que
     * lee son unas pocas docenas de bytes por salto, mas las cadenas del
     * resultado. Si alguien la reescribe y vuelve a recorrer la tabla
     * entera -206 kB por este bus son 170 ms- aqui se pone rojo.
     */
    const long TOPE = 4096;

    printf("\n== LO QUE CUESTA EL CARTEL DE LA CABECERA ==\n\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) {
        printf("  (no esta %s: me salto el banco)\n\n", ruta);
        return 0;
    }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    if (!emisoras_abre(0UL, lee)) { printf("  no abre la base\n"); return 1; }
    printf("  la base mide %ld bytes\n\n", g_tam);

    printf("    %-34s %8s %9s %9s %9s\n",
           "frecuencia", "lecturas", "bytes", "rapido", "lento");
    for (i = 0; i < sizeof k_hz / sizeof k_hz[0]; i++) {
        g_lect = 0; g_bytes = 0;
        un_cartel(k_hz[i], 720U);
        if (g_bytes > peor) { peor = g_bytes; }
        printf("    %-34s %8ld %9ld %7.1f ms %7.1f ms\n",
               k_que[i], g_lect, g_bytes,
               (double)g_bytes * 0.8 / 1000.0,
               (double)g_bytes * 16.0 / 1000.0);
    }

    printf("\n  Lo peor son %ld bytes, o sea %.1f ms con el bus rapido.\n",
           peor, (double)peor * 0.8 / 1000.0);
    printf("  Y esto corre UNA VEZ POR MINUTO -cambia el minuto, se\n");
    printf("  rehace el cartel- y otra por cada retoque del dial.\n");
    if (peor > TOPE) {
        printf("\n  *** MAL: el tope son %ld bytes. Alguien ha puesto a\n", TOPE);
        printf("      recorrer la tabla entera en el camino de la cabecera.\n\n");
        return 1;
    }
    printf("  El tope son %ld bytes y no se pasa  ok\n\n", TOPE);
    return 0;
}

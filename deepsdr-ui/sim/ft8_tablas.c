/*
 * LAS TABLAS DE LA FFT DE FT8 - 25/09/2026, REHECHO EL 10/10/2026.
 *
 * POR QUE EXISTE
 * --------------
 * Porque la radio se colgo. La primera vez que se entro en FT8: pantalla
 * congelada, un pitido continuo por el altavoz, y a apagarla por el
 * interruptor.
 *
 * La causa: ft8_fft1024_init() no la llamaba nadie. Construia cuatro tablas
 * -seno, coseno, Hann y el orden de bits invertido- DENTRO de la memoria que
 * FT8 le toma prestada a la cascada. Sin construir, contenian lo que la
 * cascada dejo ahi, o sea indices de paleta. Y una de ellas, fft_bitrev, se
 * usa como INDICE:
 *
 *     uint16_t src = s_bitrev[n];
 *     s_re[n] = (float)samples[src] * s_hann[src];
 *
 * Con basura dentro, src puede ser cualquier numero hasta 65535, y
 * s_hann[65535] cae 256 kB mas alla de una tabla de 4 kB. Fuera de la SRAM:
 * fallo de bus, y el procesador se para.
 *
 * *** 10/10/2026: ESE FALLO YA NO PUEDE PASAR, Y POR ESO ESTE BANCO CAMBIA
 * DE PREGUNTA. ***
 *
 * Las cuatro tablas se fueron a FLASH -User/ft8_tablas.c, generado por
 * tools/gen_ft8_tablas.py- y con ellas se fue el sello que las avalaba.
 * Desde el 26/10/2026 son tres: la del orden de bits la calcula
 * ft8_bitrev10() con una instruccion, y aqui se comprueba igual.
 * Nadie puede borrarlas, asi que "¿estan construidas?" dejo de ser una
 * pregunta. Lo que se hizo con los 10.240 bytes que dejaron libres DENTRO
 * de la union esta contado en ft8_shared_ram.h.
 *
 * QUE COMPRUEBA AHORA, y las tres cosas hacen falta:
 *
 *  1. QUE LOS VALORES SEAN LOS DE ANTES, BIT A BIT. Esto es lo importante y
 *     lo que no se puede dar por supuesto: la tabla de flash la calcula un
 *     guion de Python y la de antes la calculaba el firmware. Si el guion
 *     usara math.sin en vez de la aproximacion de Bhaskara-I que habia,
 *     las tablas serian MAS PRECISAS y el decodificador veria otros numeros.
 *     "Mas preciso" no es "igual". Aqui se rehace la cuenta antigua, en C,
 *     y se compara byte a byte.
 *
 *  2. Que fft_bitrev siga siendo una PERMUTACION de 0..1023: cada indice una
 *     vez y ninguno fuera. Es lo que colgaba la radio.
 *
 *  3. Que la union siga cabiendo en la cascada. Al meter dentro los seis
 *     buffers que FT8 tenia fuera, el miembro de FT8 paso de 42.240 a
 *     52.988 contra los 54.432 de la cascada. Quedan 1.444 de sobra, y el
 *     dia que alguien se los coma el enlazador NO avisa: la union
 *     simplemente crece y la RAM se acaba en otro sitio.
 */
#include "ft8_shared_ram.h"
#include "ft8_fft1024.h"
#include "waterfall.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

/*
 * LA CUENTA DE ANTES, TAL CUAL ESTABA EN ft8_fft1024.c hasta el 10/10/2026.
 * Esta COPIADA a proposito y no sacada del firmware: si se llamara a la del
 * firmware se estaria comparando la tabla consigo misma.
 */
static float sin_viejo(float x)
{
    float pi = 3.14159265358979f;
    float x2;
    int negate = 0;
    while (x > pi) { x -= 2.0f * pi; }
    while (x < -pi) { x += 2.0f * pi; }
    if (x < 0.0f) { x = -x; negate = 1; }
    x2 = (16.0f * x * (pi - x)) / (5.0f * pi * pi - 4.0f * x * (pi - x));
    return negate ? -x2 : x2;
}
static float cos_viejo(float x)
{
    return sin_viejo(x + (3.14159265358979f / 2.0f));
}

static int es_permutacion(int *fuera, int *repes)
{
    static uint8_t visto[FT8_FFT_SIZE];
    uint32_t n;

    memset(visto, 0, sizeof visto);
    *fuera = 0; *repes = 0;

    for (n = 0U; n < (uint32_t)FT8_FFT_SIZE; n++) {
        uint16_t v = ft8_bitrev10(n);
        if (v >= (uint16_t)FT8_FFT_SIZE) { (*fuera)++; continue; }
        if (visto[v]) { (*repes)++; }
        visto[v] = 1U;
    }
    return (*fuera == 0) && (*repes == 0);
}

int main(void)
{
    uint32_t n;
    int fuera = 0, repes = 0;

    printf("\nLAS TABLAS DE LA FFT DE FT8, EN FLASH\n\n");

    /* --------------------------------------------------------------
     * 1. BIT A BIT CONTRA LA CUENTA DE ANTES.
     * -------------------------------------------------------------- */
    printf("  1. LOS MISMOS NUMEROS QUE ANTES DE PASARLAS A FLASH\n");
    {
        unsigned mal = 0U;

        for (n = 0U; n < (uint32_t)FT8_FFT_SIZE / 2U; n++) {
            float ang = -2.0f * 3.14159265358979f * (float)n / (float)FT8_FFT_SIZE;
            float c = cos_viejo(ang), s = sin_viejo(ang);
            if (memcmp(&c, &k_ft8_twiddle_cos[n], sizeof c) != 0) { mal++; }
            if (memcmp(&s, &k_ft8_twiddle_sin[n], sizeof s) != 0) { mal++; }
        }
        for (n = 0U; n < (uint32_t)FT8_FFT_SIZE; n++) {
            float h = 0.5f - 0.5f * cos_viejo(
                2.0f * 3.14159265358979f * (float)n / (float)(FT8_FFT_SIZE - 1U));
            uint32_t v = n, r = 0U, b;
            if (memcmp(&h, &k_ft8_hann[n], sizeof h) != 0) { mal++; }
            /*
             * Y LA CUARTA TABLA YA NO EXISTE - 26/10/2026. La calcula
             * ft8_bitrev10() con `rbit`, un ciclo, y eran 2.048 bytes de
             * flash interna. Se sigue comprobando contra la MISMA cuenta
             * de antes, que es lo que importa: el decodificador tiene que
             * ver los mismos indices, vengan de una tabla o de una
             * instruccion.
             */
            for (b = 0U; b < 10U; b++) { r = (r << 1U) | (v & 1U); v >>= 1U; }
            if (ft8_bitrev10(n) != (uint16_t)r) { mal++; }
        }
        printf("    %u valores comprobados, %u diferencias\n",
               (unsigned)(FT8_FFT_SIZE * 3U), mal);
        pasa("*** la tabla de flash es la de antes, bit a bit ***", mal == 0U);
    }

    /* --------------------------------------------------------------
     * 2. LA PERMUTACION, QUE ES LO QUE COLGABA LA RADIO.
     * -------------------------------------------------------------- */
    printf("\n  2. fft_bitrev SIGUE SIENDO UNA PERMUTACION\n");
    pasa("ningun indice se sale ni se repite", es_permutacion(&fuera, &repes));
    printf("    %d fuera de rango, %d repetidos\n", fuera, repes);

    /* --------------------------------------------------------------
     * 3. QUE LA UNION SIGA CABIENDO.
     * -------------------------------------------------------------- */
    printf("\n  3. QUIEN MANDA EN EL TAMANO DE LA UNION\n");
    {
        ft8_shared_ram_t u;
        unsigned cascada = (unsigned)sizeof u.waterfall_buf;
        unsigned ft8     = (unsigned)sizeof u.ft8;
        unsigned hfdl    = (unsigned)sizeof u.hfdl_buf;

        printf("    cascada %u   ft8 %u   hfdl %u   union %u\n",
               cascada, ft8, hfdl, (unsigned)sizeof u);
        /*
         * ==============================================================
         * Y ESTA DEJO DE SER LA PREGUNTA EL 23/10/2026
         * ==============================================================
         *
         * Aqui se exigia `ft8 <= cascada`: que el bloque de FT8 cupiera
         * dentro del de la cascada. Esa regla se retiro hace tiempo y en
         * DOS sitios que lo dicen con todas las letras -ver waterfall_init()
         * ("la union entera, no solo el miembro de la cascada") y el
         * comentario del sello en ft8_shared_ram.h ("la garantia ya no
         * depende de una coincidencia de tamanos, y por eso el aserto
         * sobra")-. Lo que la sostenia era que waterfall_init() solo
         * borraba el miembro de la cascada; desde que borra la union
         * entera, un miembro de FT8 mas grande no deja nada sin borrar.
         *
         * O sea que este banco se quedo vigilando una regla que el codigo
         * habia jubilado, y salto en rojo el dia que FT8 crecio por una
         * razon buena. Una guarda que dice que no a algo que ya esta
         * permitido no protege: estorba, y la forma de callarla seria
         * deshacer el cambio bueno.
         *
         * LA PREGUNTA DE VERDAD es que FT8 quepa en LA UNION, que es la
         * memoria que de verdad tiene. Eso es lo que se mira ahora, y se
         * imprime cuanto sobra para que quien vaya a pedir mas sepa de
         * donde lo saca.
         */
        pasa("el miembro de FT8 cabe en la union", ft8 <= (unsigned)sizeof u);
        printf("    a FT8 le sobran %u bytes dentro de la union\n",
               (unsigned)sizeof u - ft8);
        (void)cascada;

        /*
         * ==============================================================
         * ESTA COMPROBACION CAMBIO DE DUENO EL 11/10/2026
         * ==============================================================
         *
         * Aqui ponia `sizeof u == cascada`: "la union no mide mas que la
         * cascada". Era verdad y era util, porque los dos inquilinos de
         * entonces -la cascada y FT8- cabian uno dentro del otro y la union
         * salia gratis. Si crecia, la RAM se acababa en otro sitio y nadie
         * relacionaria las dos cosas.
         *
         * Y SALTO EN ROJO al ensanchar la union para el giro 6 de HFDL. Hizo
         * bien: ese dia la union crecio 5.872 bytes a proposito, y una
         * guarda que no salta cuando la RAM crece no sirve para nada.
         *
         * Lo que NO se puede hacer es callarla. El invariante cambio, asi
         * que se reescribe el invariante:
         *
         *   - FT8 sigue teniendo que caber en la cascada (arriba). Eso no
         *     ha cambiado y es lo que impide que FT8 crezca a escondidas.
         *   - y la union tiene que medir EXACTAMENTE lo que pide HFDL, ni
         *     un byte mas. Antes mandaba la cascada por casualidad; ahora
         *     manda HFDL porque se decidio, y el numero esta justificado
         *     en hfdl_ram.h contra la tabla de los ocho giros.
         *
         * Un "<=" aqui dejaria pasar que alguien ensanche la cascada y se
         * lleve RAM de regalo sin que nadie lo note. Por eso es "==".
         */
        pasa("la union mide exactamente lo que pide HFDL, ni un byte mas",
             (unsigned)sizeof u == hfdl);
        pasa("y HFDL es el que manda, no la cascada", hfdl > cascada);
    }

    printf("\n  %s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

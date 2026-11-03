/*
 * ======================================================================
 * LO QUE CUESTA PINTAR UN SALTO - 15/10/2026
 * ======================================================================
 *
 * Asi que el coste tiene que estar en otro sitio, y la sospecha es el
 * REPINTADO. spectrum_draw() se salta las bandas de 16 filas cuya traza no
 * ha cambiado (s_parcial_hay / s_saltadas). Con un paso de sintonia de
 * 1 kHz la traza casi no se mueve y se salta casi todo; con un salto de
 * emisora el dial se va kilohercios de golpe, la traza cambia entera y no
 * se salta nada.
 *
 * Esto lo MIDE, con el User/spectrum.c de verdad: pinta un cuadro, luego
 * otro con la traza movida poco, y otro con la traza movida mucho, y
 * pregunta cuantas bandas se ha saltado cada uno.
 *
 * Y LA SOSPECHA ERA FALSA. El banco la mato a la primera, y el porque esta
 * en el propio spectrum.c: una banda se salta cuando NINGUN tramo pinta
 * nada en sus filas, o sea solo el cielo que queda POR ENCIMA del pico mas
 * alto. No se compara la traza de este cuadro con la del anterior en
 * ningun sitio. Asi que lo que se ahorra depende de lo ALTAS que sean las
 * señales, no de cuanto se haya movido el dial: un salto de veinte
 * kilohercios cuesta de pintar exactamente lo mismo que un paso de uno.
 *
 * Esto se queda escrito y midiendose porque un negativo medido vale: la
 * proxima vez que alguien -yo el primero- culpe al repintado de que la
 * cascada va lenta al sintonizar, aqui esta el numero que dice que no.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-52s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

#define BINS  1024
#define ANCHO 756
#define ALTO  208

static float g_db[BINS];

/* Un panorama con unas cuantas emisoras, desplazado `corr` bins. */
static void panorama(int corr)
{
    int i, k;
    for (i = 0; i < BINS; i++) { g_db[i] = -110.0f + 3.0f * sinf((float)i * 0.11f); }
    for (k = 0; k < 9; k++) {
        int c = ((k * 113 + 37) + corr) % BINS;
        int w;
        for (w = -3; w <= 3; w++) {
            int x = c + w;
            if (x < 0 || x >= BINS) { continue; }
            g_db[x] = -40.0f + 4.0f * (float)(w < 0 ? -w : w);
        }
    }
}

static uint32_t pinta(void)
{
    uint32_t salt = 0U, tot = 0U;
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);
    spectrum_parcial_diag(&salt, &tot);
    (void)tot;
    return salt;
}

/*
 * Y LO MISMO PERO MOVIENDO SOLO LA MARCA, con la traza clavada. `marca`
 * es el desplazamiento en pixeles y `banda` enciende el tinte de la banda
 * de paso entre `blo` y `bhi`.
 */
static uint32_t pinta_marca(int16_t marca, uint8_t banda,
                            int16_t blo, int16_t bhi)
{
    uint32_t salt = 0U, tot = 0U;
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  marca, banda, blo, bhi);
    spectrum_parcial_diag(&salt, &tot);
    (void)tot;
    return salt;
}

int main(void)
{
    uint32_t s_igual, s_poco, s_mucho, tot = 0U;

    printf("\n== LO QUE CUESTA PINTAR UN SALTO ==\n\n");
    lcd_sim_reset(0x0000);

    /* Dos cuadros iguales para que el parcial se active (hace falta un
     * cuadro anterior con el que comparar). */
    panorama(0); (void)pinta();
    panorama(0); s_igual = pinta();
    spectrum_parcial_diag(0, &tot);

    /* Un paso de sintonia de 1 kHz a 96 kHz de ventana son ~11 bins de
     * 1024... pero lo que mueve la traza es la SEÑAL, no la ventana: con
     * 1 kHz las emisoras se corren un pelin. */
    panorama(0); (void)pinta();
    panorama(1); s_poco = pinta();

    /* Un salto de emisora son kilohercios: la traza cambia entera. */
    panorama(0); (void)pinta();
    panorama(60); s_mucho = pinta();

    printf("  bandas de 16 filas en la traza: %u\n\n", (unsigned)tot);
    printf("    %-40s %u de %u\n", "cuadro igual, bandas saltadas",
           (unsigned)s_igual, (unsigned)tot);
    printf("    %-40s %u de %u\n", "la traza se mueve un poco (1 kHz)",
           (unsigned)s_poco, (unsigned)tot);
    printf("    %-40s %u de %u\n", "la traza se mueve mucho (un salto)",
           (unsigned)s_mucho, (unsigned)tot);
    printf("\n");

    /*
     * ==================================================================
     * Y LA MARCA, QUE ES LO QUE DE VERDAD MUEVE UN DEDO - 03/11/2026
     * ==================================================================
     *
     * *** Medido en la radio con "DD;": arrastrando el dedo, las bandas
     * saltadas bajan de 26 a 15 de 52 y la traza pasa de 6.600 a 11.400
     * us. Y los rotulos de emisoras estaban APAGADOS, que era mi
     * sospecha. ***
     *
     * Con el NCO puesto el oscilador se queda aparcado y LA TRAZA NO SE
     * MUEVE: lo unico que se mueve por la pantalla es la marca de
     * demodulacion y el tinte de la banda de paso. Las dos viven en la
     * PLANTILLA DE FILA, no en la traza (ver "LAS DOS PLANTILLAS DE
     * FILA" en spectrum.c).
     *
     * Y la plantilla entra en el atajo por firmas_por_tramo():
     *
     *     if (vale && (f != s_firma_tramo[t])) { s_tramo_techo[t] = 0U; }
     *
     * Techo cero quiere decir "pinta desde la primera fila". O sea que un
     * tramo cuya plantilla haya cambiado se repinta ENTERO de arriba
     * abajo; y como una banda solo se salta si NINGUN tramo pinta en sus
     * filas, un solo tramo sucio deja la banda sin saltar.
     *
     * Esto lo mide con la traza CLAVADA: lo unico que cambia entre cuadro
     * y cuadro es donde esta la marca.
     */
    {
        /*
         * CERO, NO ANCHO/2. El desplazamiento es RELATIVO al centro
         * -center_mark_col = w/2 + offset, ver spectrum.c-, asi que con
         * ANCHO/2 la marca se va al borde derecho y SE TOPA ahi. La
         * primera version de esta medida pasaba ANCHO/2 y despues +2 y
         * +32: los tres topaban en la misma columna, o sea que el banco
         * decia "mover la marca no cuesta nada" porque la marca NO SE
         * MOVIA. Copiado de pinta(), que pasa lo mismo y para lo que hace
         * -medir la traza- da igual.
         */
        const int16_t m0 = 0;
        uint32_t q_quieta, q_poco, q_tramo, q_banda;

        /*
         * TRES CUADROS IGUALES ANTES DE CADA MEDIDA, y no uno.
         *
         * El atajo mezcla el techo de ESTE cuadro con el del anterior
         * (s_tramo_techo_ant), asi que con un solo cuadro de asiento la
         * medida arrastra lo que hubiera dos cuadros atras. La primera
         * version daba 2 saltadas quieta y 4 moviendose -moverse salia
         * MAS barato que no moverse-, que es la firma de un banco que
         * mide el pasado y no lo que cree.
         */
#define ASIENTO(mk, bd, lo, hi) do {            \
            int a_;                              \
            panorama(0);                         \
            for (a_ = 0; a_ < 3; a_++) {         \
                (void)pinta_marca((mk), (bd), (lo), (hi)); \
            }                                    \
        } while (0)

        ASIENTO(m0, 0U, 0, 0);
        q_quieta = pinta_marca(m0, 0U, 0, 0);

        /* Dentro del mismo tramo de 16 columnas: la firma no cambia. */
        ASIENTO(m0, 0U, 0, 0);
        q_poco = pinta_marca((int16_t)(m0 + 2), 0U, 0, 0);

        /* Cruzando a otro tramo: dos firmas cambian, la de antes y la de
         * ahora. */
        ASIENTO(m0, 0U, 0, 0);
        q_tramo = pinta_marca((int16_t)(m0 + 32), 0U, 0, 0);

        /* Y con el tinte de la banda de paso puesto, que es mucho mas
         * ancho que la marca y barre mas tramos. */
        ASIENTO(m0, 1U, (int16_t)(m0 - 20), (int16_t)(m0 + 20));
        q_banda = pinta_marca((int16_t)(m0 + 32), 1U,
                              (int16_t)(m0 + 12), (int16_t)(m0 + 52));
#undef ASIENTO

        printf("  -- y con la TRAZA CLAVADA, moviendo solo la marca --\n");
        printf("    %-44s %u de %u\n", "la marca tampoco se mueve",
               (unsigned)q_quieta, (unsigned)tot);
        printf("    %-44s %u de %u\n", "la marca se mueve 2 px (mismo tramo)",
               (unsigned)q_poco, (unsigned)tot);
        printf("    %-44s %u de %u\n", "la marca cruza de tramo (32 px)",
               (unsigned)q_tramo, (unsigned)tot);
        printf("    %-44s %u de %u\n", "y con el tinte de la banda de paso",
               (unsigned)q_banda, (unsigned)tot);
        printf("\n");

        /*
         * LO QUE SALE, Y LO QUE NO SALE.
         *
         * Mover la marca cuesta DOS bandas de 52, se cruce de tramo o no:
         * la marca son tres columnas, asi que moverla dos pixeles ya
         * cambia la firma de su tramo y ese se repinta de arriba abajo
         * (techo cero en firmas_por_tramo()).
         *
         * Y CRUZAR DE TRAMO NO CUESTA MAS QUE NO CRUZAR. Eso mata la
         * explicacion que yo traia: pensaba que el salto grande ensuciaba
         * los tramos de en medio, y no: solo el de antes y el de ahora,
         * que son los mismos dos que ensucia un salto de dos pixeles.
         *
         * O SEA QUE LA MARCA NO EXPLICA LA RADIO. Alli, arrastrando, las
         * bandas saltadas bajan de 26 a 15 -ONCE bandas- y aqui, con la
         * traza clavada, la marca se lleva DOS. Las otras nueve tienen que
         * venir de que la traza SI cambia, y eso no es repintado de mas:
         * es señal de verdad.
         */
        di(q_poco < q_quieta,
           "mover la marca cuesta, aunque no cruce de tramo",
           "la marca son tres columnas y ya cambia la firma de su tramo");
        di(q_tramo == q_poco,
           "y cruzar de tramo no cuesta MAS que no cruzar",
           "se ensucian el tramo de antes y el de ahora, ni uno mas");
        di(q_quieta - q_poco <= 4U,
           "la marca se lleva pocas bandas, no once",
           "las once de la radio no son la marca: la traza cambia de "
           "verdad");
        printf("\n");
    }

    di(s_igual > 0U && s_igual < tot,
       "se salta el cielo de encima de las señales, y solo eso",
       "si se saltara todo o nada, el parcial no estaria haciendo su trabajo");
    /*
     * ==================================================================
     * AQUI SE EXIGIA IGUALDAD EXACTA, Y SE AFLOJA A UNA BANDA - 30/10/2026.
     * ==================================================================
     *
     * `s_poco == s_mucho` se cumplia -9 y 9 de 52- mientras el contorno
     * venia de fabrica con TRES pasadas de suavizado: tres pasadas
     * ensanchan los picos lo bastante como para que el borde del cielo
     * caiga en la misma banda de 16 filas con la traza movida un bin o
     * movida sesenta. Bajado el contorno a UNA pasada -30/10/2026, pedido
     * por el usuario- la traza es mas afilada y el borde del cielo cae una
     * banda mas arriba en uno de los dos casos: 8 y 7.
     *
     * LO QUE ESTE BANCO DEFIENDE no es la igualdad al bit, es que el
     * ahorro NO SEA PROPORCIONAL al salto del dial, porque esa era la
     * hipotesis que mato: si lo fuera, el repintado explicaria que la
     * cascada vaya lenta al saltar de emisora. Y sigue muerta, con el
     * numero delante: mover la traza SESENTA veces mas cuesta UNA banda
     * mas de 52, o sea un 2 %. Proporcional seria sesenta veces.
     *
     * Por eso lo que se exige ahora es eso mismo escrito como numero, y no
     * un "casi igual" a ojo.
     *
     * ==================================================================
     * Y PASA A SER UNA PROPORCION, PORQUE LA BANDA YA NO ES LA MISMA
     * 02/11/2026.
     * ==================================================================
     *
     * La banda era TODO el ancho por cuatro filas. Desde que el panel se
     * monta por lonjas de columnas -ver "LA BANDA, MAS ALTA Y MENOS
     * ANCHA" en spectrum.c- son 192 columnas por dieciseis filas: el
     * mismo numero de bandas, 52, pero cada una cubre un rectangulo
     * distinto, y una banda alta es mas dificil de saltarse que una baja
     * porque le cabe mas cielo debajo.
     *
     * O sea que "una banda" ya no mide lo que medía, y exigir el mismo
     * numero absoluto seria comparar dos unidades distintas. Lo que este
     * banco defiende no era el uno: era que el ahorro no fuese
     * PROPORCIONAL al salto. Asi que se escribe asi, como tanto por
     * ciento del total.
     *
     * Y los pixeles que de verdad salen por el bus no cambiaron ni uno:
     * eso lo mide sim/espectro_oro.c, que sigue dando 158.132 accesos por
     * cuadro entero, los mismos que antes de las lonjas.
     */
    {
        int dif = (int)s_poco - (int)s_mucho;
        char d[120];
        if (dif < 0) { dif = -dif; }
        snprintf(d, sizeof d,
                 "mover la traza 60 veces mas cuesta %d banda(s) de %u, "
                 "no 60 veces mas", dif, (unsigned)tot);
        printf("    %-52s %s\n", "cuanto depende del salto del dial", d);
        di(dif * 10 <= (int)tot,
           "y saltarse bandas NO depende de cuanto se movio el dial",
           "si dependiera, el repintado si explicaria la cascada lenta al saltar");
    }
    {
        char d[110];
        snprintf(d, sizeof d,
                 "un salto repinta %u bandas y un paso %u",
                 (unsigned)(tot - s_mucho), (unsigned)(tot - s_poco));
        printf("    %-52s %s\n", "lo que cuesta de pintar cada uno", d);
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

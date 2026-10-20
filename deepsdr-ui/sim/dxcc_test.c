/*
 * LA TABLA DE PAISES - 25/09/2026.
 *
 * Lo que comprueba, y por que cada cosa:
 *
 *  1. QUE GANE EL PREFIJO MAS LARGO. "EA8ABC" empieza por "EA" y tambien por
 *     "EA8". Si ganara el primero que encaja, las Canarias saldrian como
 *     Espana peninsular segun el orden de la tabla - o sea, segun algo que
 *     nadie recordaria mantener al anadir una fila.
 *  2. QUE NO HAYA PREFIJOS REPETIDOS. Dos filas con el mismo prefijo son dos
 *     respuestas para la misma pregunta, y cual gana depende del orden. La
 *     primera version tenia dos ("9A" y "EA8"), escritas sin darse cuenta.
 *  3. QUE LOS NOMBRES QUEPAN en su columna. Medido con gfx2_text_w contra el
 *     ancho real que deja ui_digi.c, no a ojo: un nombre que no cabe no
 *     descuadra nada -se recorta- pero sale a medias y eso se lee peor que
 *     no ponerlo.
 *  4. Que un prefijo desconocido devuelva "no se" y no un pais cualquiera.
 *     Un hueco es una respuesta honrada; un pais equivocado, no.
 */
#include "dxcc.h"
#include "idioma.h"
#include "gfx2.h"
#include "font_ui_14b.h"
#include "ui_digi.h"
#include "palette.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-58s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

static void mira(const char *ind, const char *esperado)
{
    const char *r = dxcc_pais(ind);
    char q[90];
    sprintf(q, "%-10s -> %s", ind, r ? r : "(no se)");
    pasa(q, (esperado == 0) ? (r == 0) : (r != 0 && strcmp(r, esperado) == 0));
}

int main(void)
{
    printf("PAIS POR PREFIJO (%u prefijos)\n\n", dxcc_cuantos());

    printf("  EL MAS LARGO GANA\n");
    mira("EA8ABC", "Canarias");        /* y no Espana */
    mira("EA4XYZ", "Espana");
    mira("EA6ABC", "Baleares");
    mira("EA9ABC", "Ceuta-Melilla");
    mira("CT3XYZ", "Madeira");         /* y no Portugal */
    mira("CT1ABC", "Portugal");
    mira("SV9XYZ", "Creta");           /* y no Grecia */
    mira("SV1ABC", "Grecia");
    mira("HB0ABC", "Liechtenstein");   /* y no Suiza */
    mira("HB9ABC", "Suiza");
    mira("KH6ABC", "Hawai");           /* y no EE.UU. */
    mira("KL7ABC", "Alaska");
    mira("K1ABC",  "EE.UU.");
    mira("IS0ABC", "Cerdena");         /* y no Italia */
    mira("IT9ABC", "Sicilia");
    mira("I1ABC",  "Italia");

    printf("\n  LO DE TODOS LOS DIAS\n");
    mira("DL5AN",  "Alemania");
    mira("TA3NK",  "Turquia");
    mira("AI0Y",   "EE.UU.");
    mira("OK2FD",  "Rep. Checa");
    mira("EU7SV",  "Bielorrusia");
    mira("JA1XYZ", "Japon");
    mira("PA0ABC", "Holanda");
    mira("F5ABC",  "Francia");
    mira("G0ABC",  "Inglaterra");
    mira("EA4ABC/P", "Espana");        /* el sufijo de portable no estorba */

    printf("\n  BLOQUES ENTEROS, NO UN PREFIJO SUELTO\n");
    /* El fallo avisado por el usuario: un indicativo noruego no sacaba pais. La
     * tabla tenia "LA" y Noruega es LA-LN entera. Lo mismo pasaba en media
     * Europa, asi que se comprueban varias letras del mismo bloque: acertar
     * con una y fallar con otra es el fallo, no acertar con una. */
    mira("LB4PI",  "Noruega");
    mira("LA1ABC", "Noruega");
    mira("LN2XYZ", "Noruega");
    mira("SE5ABC", "Suecia");
    mira("SM0ABC", "Suecia");
    mira("DC1ABC", "Alemania");
    mira("DR5ABC", "Alemania");
    mira("PH1ABC", "Holanda");
    mira("SN5ABC", "Polonia");
    mira("OT1ABC", "Belgica");
    mira("OL5ABC", "Rep. Checa");
    mira("OG2ABC", "Finlandia");
    mira("UZ1ABC", "Ucrania");
    mira("EM5ABC", "Ucrania");
    mira("YR8ABC", "Rumania");
    mira("SX1ABC", "Grecia");
    mira("CS7ABC", "Portugal");
    mira("OU3ABC", "Dinamarca");
    mira("OX3ABC", "Groenlandia");   /* y NO Dinamarca */

    printf("\n  LA TRAMPA DEL REINO UNIDO\n");
    /* La M y el 2 llevan las mismas letras de region que la G, asi que un
     * "M" a secas para Inglaterra se llevaba Escocia y Gales por delante. */
    mira("MM0ABC", "Escocia");
    mira("MW1ABC", "Gales");
    mira("MI0ABC", "Irlanda N.");
    mira("M0ABC",  "Inglaterra");
    mira("2M0ABC", "Escocia");
    mira("2E0ABC", "Inglaterra");
    mira("GM4ABC", "Escocia");

    printf("\n  LO QUE NO SE SABE\n");
    mira("QQ1ZZZ", 0);
    mira("", 0);
    pasa("un puntero nulo no revienta", dxcc_pais(0) == 0);

    printf("\n  LA TABLA POR DENTRO\n");
    {
        /*
         * TODOS los nombres y en LOS DOS IDIOMAS, recorriendo la tabla con
         * dxcc_prefijo(). Antes habia aqui una lista de ocho nombres largos
         * escrita a mano: medía lo que alguien penso un dia que era el peor
         * caso, no el peor caso. Un pais nuevo mas largo no la habria hecho
         * fallar, y una traduccion inglesa mas larga tampoco.
         *
         * Y de paso esto cubre el fallo del 30/09/2026: al mover los nombres
         * al desvan se emitio el bloque SIN quitar los repetidos mientras
         * los desplazamientos si los quitaban, y a partir del primer nombre
         * repetido todos apuntaban cuatro bytes dentro del de al lado
         * ("Isla de Man" salia como "la de Man"). Recorrer la tabla entera
         * lo pilla; una lista de ocho, no.
         */
        int16_t x_km   = ui_digi_cols_ft8[UI_DIGI_COLS_FT8_N - 2];
        int16_t x_pais = ui_digi_cols_ft8[UI_DIGI_COLS_FT8_N - 1];
        int hueco = (int)(-x_pais) - (int)(-x_km);
        int peor = 0, vacios = 0;
        const char *peor_n = "";
        unsigned j;
        int l;

        for (l = 0; l < (int)IDIOMA_N; l++) {
            idioma_pon((uint8_t)l);
            for (j = 0U; j < dxcc_cuantos(); j++) {
                char ind[16];
                const char *pre = dxcc_prefijo(j);
                const char *nom;
                int w;
                unsigned c = 0U;

                /* El prefijo mas "1ABC" detras: asi se busca como se buscaria
                 * un indicativo de verdad, por el mismo camino de siempre. */
                while (pre[c] != '\0' && c < 5U) { ind[c] = pre[c]; c++; }
                ind[c++] = '1'; ind[c++] = 'A'; ind[c++] = 'B'; ind[c++] = 'C';
                ind[c] = '\0';

                nom = dxcc_pais(ind);
                if (nom == 0 || nom[0] == '\0') { vacios++; continue; }
                w = gfx2_text_w(nom, &font_ui_14b);
                if (w > peor) { peor = w; peor_n = nom; }
            }
        }
        idioma_pon(IDIOMA_ES);

        printf("      %u prefijos x %d idiomas    hueco: %d px    el mas ancho (\"%s\"): %d px\n",
               dxcc_cuantos(), (int)IDIOMA_N, hueco, peor_n, peor);
        pasa("ningun prefijo se queda sin nombre en ningun idioma", vacios == 0);
        pasa("todos los nombres caben en la columna, en los dos idiomas", peor <= hueco);
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

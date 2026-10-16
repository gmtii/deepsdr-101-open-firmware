/*
 * ======================================================================
 * gfx2_text_cabe() DA LO MISMO QUE QUITAR LETRAS DE UNA EN UNA
 * 13/10/2026
 * ======================================================================
 *
 * ui_digi.c recortaba los campos de las columnas asi:
 *
 *     while (n > 0 && x + gfx2_text_w(campo, f) > lim) { campo[--n] = 0; }
 *
 * o sea quitando UNA letra y volviendo a medir la cadena ENTERA. Es
 * cuadratico, y medir la cadena entera es recorrerla letra a letra
 * buscando cada glifo en los 22 rangos de la fuente. Medido en un
 * repintado del panel de FT8 con mensajes de los que si hay que recortar:
 * 58.420 letras medidas, contra 5.580 si no hay nada que recortar. El
 * recorte costaba diez veces lo que costaba pintar.
 *
 * gfx2_text_cabe() lo hace en una pasada hacia delante. ESTE BANCO
 * COMPRUEBA QUE DA EXACTAMENTE EL MISMO RESULTADO, porque una
 * optimizacion que cambia el dibujo no es una optimizacion: se recorren
 * cadenas de verdad -con acentos, que en UTF-8 son dos bytes- contra
 * TODOS los limites de 0 a 320 px, y se compara letra a letra con el
 * bucle viejo.
 */
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14b.h"
#include "font_ui_14.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

/* El bucle VIEJO, copiado tal cual de ui_digi.c, para comparar. */
static uint8_t recorte_viejo(const char *str, const gfx2_font_t *f, int16_t tope)
{
    static char campo[160];
    uint8_t n;

    n = (uint8_t)strlen(str);
    if (n > sizeof campo - 1U) { n = (uint8_t)(sizeof campo - 1U); }
    memcpy(campo, str, n);
    campo[n] = '\0';
    while (n > 0U && gfx2_text_w(campo, f) > tope) { campo[--n] = '\0'; }
    return n;
}

int main(void)
{
    static const char *k[] = {
        "CQ EA4XYZ IN80",
        "W1ABC/R KH7Z/P EM12 OOO",
        "K1ABC RR73; W9XYZ <KH1/KH7Z> -11",
        "Esperando señal",
        "Telegrafía Morse, ñandú, áéíóú",
        "AM síncrona, banda alta",
        "",
        "A",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
        "12:30\t1200\t-08",
        "Dominican Rep."
    };
    static const gfx2_font_t *fuentes[2];
    unsigned i, fu;
    long casos = 0, partidas = 0;

    fuentes[0] = &font_ui_14b;
    fuentes[1] = &font_ui_14;

    printf("\n== gfx2_text_cabe() CONTRA EL RECORTE DE UNA EN UNA ==\n\n");

    for (fu = 0; fu < 2; fu++) {
        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            int16_t tope;
            int malos = 0;

            for (tope = -4; tope <= 320; tope++) {
                uint8_t viejo = recorte_viejo(k[i], fuentes[fu], tope);
                int16_t ancho = -1;
                uint8_t nuevo = gfx2_text_cabe(k[i], fuentes[fu], tope, &ancho);

                casos++;
                /*
                 * DONDE NO COINCIDEN, EL NUEVO TIENE RAZON.
                 *
                 * El bucle viejo cortaba BYTES -`campo[--n] = 0`- y en
                 * UTF-8 una letra acentuada son dos. O sea que podia
                 * dejar media "ñ" al final del campo: un byte suelto que
                 * la fuente dibuja como otra cosa o como nada. No es una
                 * diferencia de criterio, es un fallo que llevaba ahi
                 * desde que las columnas recortan.
                 *
                 * gfx2_text_cabe() corta siempre en el principio de una
                 * letra. Lo que se exige entonces es: no pasarse del
                 * limite, no quitar mas de una letra de mas, y acabar en
                 * frontera de caracter.
                 */
                if (viejo != nuevo) {
                    uint8_t cont = (uint8_t)k[i][viejo - 1];
                    int ok = (nuevo < viejo) && (viejo - nuevo <= 3)
                           && ((cont & 0xC0U) == 0xC0U
                               || ((uint8_t)k[i][viejo] & 0xC0U) == 0x80U);

                    if (!ok) { malos++; }
                    else { partidas++; }
                    continue;
                }
                /* Y el ancho que devuelve tiene que ser el de verdad. */
                if (nuevo > 0U) {
                    static char trozo[160];
                    memcpy(trozo, k[i], nuevo); trozo[nuevo] = '\0';
                    if (gfx2_text_w(trozo, fuentes[fu]) != ancho) { malos++; }
                    if (ancho > tope) { malos++; }
                }
            }
            printf("    %-34s %s\n",
                   k[i][0] ? k[i] : "(cadena vacia)",
                   malos ? "*** MAL ***"
                         : "igual, o corta la letra entera");
            if (malos) { fallos++; }
        }
    }
    printf("\n    %ld casos comparados en las dos fuentes\n", casos);
    printf("    %ld en los que el recorte VIEJO partia una letra por la mitad\n",
           partidas);
    if (partidas == 0) {
        printf("    *** MAL: ningun caso con letra partida: el banco no esta\n"
               "        midiendo lo que cree. Hacen falta cadenas con acentos.\n");
        fallos++;
    }

    /* Y el caso tonto: puntero nulo. */
    {
        int16_t a = 99;
        if (gfx2_text_cabe(0, &font_ui_14b, 100, &a) != 0U || a != 0) {
            printf("    *** MAL: con cadena nula no devuelve cero ***\n");
            fallos++;
        } else {
            printf("    con cadena nula devuelve cero                  ok\n");
        }
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

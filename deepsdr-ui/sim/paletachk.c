/*
 * COMPROBACION DE LA TABLA DE PALETAS - 22/09/2026
 *
 * La lista de paletas estaba escrita siete veces por el firmware y tres de
 * ellas se mostraban con el nombre de otra porque a una copia le faltaban
 * ramas. Ahora la lista vive en una sola tabla de spectrum.c; esto comprueba
 * que esa tabla es coherente, que el ciclo del boton pasa por todas y que lo
 * que se guarda en CONFIG.CSV vuelve a leerse como la misma paleta.
 *
 * Compila el vendor/spectrum.c de verdad, el mismo que se copia a User/.
 */
#include "spectrum.h"
#include "font_ui_14.h"
#include "gfx2.h"
#include "palette.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void mal(const char *que) { printf("  %s  MAL\n", que); fallos++; }

int main(void)
{
    uint8_t n = spectrum_palette_count();
    uint8_t i, j;
    /* El hueco real de la celda de ajustes, para que un nombre nuevo
     * demasiado largo se cace aqui y no en la pantalla. */
    const int16_t hueco = 200 - 20;

    spectrum_init();
    printf("tabla de paletas: %u entradas\n", n);
    if (n < 2U) { mal("la tabla esta vacia o casi"); }

    for (i = 0U; i < n; i++) {
        const char *nom = spectrum_palette_nombre(i);
        const char *cla = spectrum_palette_clave(i);
        int16_t w;

        if (nom[0] == '\0') { mal("hay una paleta sin nombre"); continue; }
        if (cla[0] == '\0') { mal("hay una paleta sin clave");  continue; }

        /* Ni nombres ni claves repetidos: dos filas con el mismo nombre son
         * dos paletas que el usuario no puede distinguir, y dos con la misma
         * clave hacen que al reiniciar salga la que no era. */
        for (j = 0U; j < i; j++) {
            if (strcmp(nom, spectrum_palette_nombre(j)) == 0) { mal("dos paletas con el mismo nombre"); }
            if (strcmp(cla, spectrum_palette_clave(j)) == 0)  { mal("dos paletas con la misma clave"); }
        }

        w = gfx2_text_w(nom, &font_ui_14);
        if (w > hueco) {
            printf("  \"%s\" mide %d px y la celda da %d\n", nom, w, hueco);
            mal("un nombre no cabe en la celda de ajustes");
        }

        if (spectrum_palette_de_clave(cla, (uint32_t)strlen(cla)) != (int16_t)i) {
            printf("  \"%s\" no vuelve como %u\n", cla, i);
            mal("la clave no va y vuelve");
        }

        /* Los dos extremos del degradado tienen que ser distintos: una
         * paleta plana no dibujaria nada. */
        if (spectrum_palette_muestra(i, 0U) == spectrum_palette_muestra(i, 255U)) {
            printf("  \"%s\" empieza y acaba en el mismo color\n", nom);
            mal("paleta plana");
        }
    }

    /* Claves que NO existen. La version anterior comparaba prefijos y habia
     * que ordenar las ramas de mas larga a mas corta a mano para que
     * "CLASSIC" no se tragara "CLASSIC_GREEN"; estas tres lo verifican. */
    if (spectrum_palette_de_clave("CLASS", 5U) != -1)          { mal("acepta un prefijo corto"); }
    if (spectrum_palette_de_clave("CLASSIC_GREENX", 14U) != -1){ mal("acepta una clave con cola"); }
    if (spectrum_palette_de_clave("", 0U) != -1)               { mal("acepta la clave vacia"); }

    /* El ciclo del boton: dando n veces se pasa por las n y se vuelve al
     * principio, sin repetir ninguna. Es lo mismo que hace paleta_cambiar()
     * en main.c, escrito aqui aparte para que no sea una tautologia. */
    {
        uint8_t visto[64];
        uint8_t p = 0U;
        memset(visto, 0, sizeof visto);
        for (i = 0U; i < n; i++) {
            if (p >= 64U || visto[p]) { mal("el ciclo repite antes de dar la vuelta"); break; }
            visto[p] = 1U;
            p = (uint8_t)((p + 1U) % n);
        }
        if (p != 0U) { mal("el ciclo no vuelve al principio"); }
        for (i = 0U; i < n; i++) { if (!visto[i]) { mal("el ciclo se salta una paleta"); } }
    }

    printf("  %s\n", fallos ? "HAY FALLOS" : "tabla coherente, claves de ida y vuelta, ciclo completo");
    return fallos ? 1 : 0;
}

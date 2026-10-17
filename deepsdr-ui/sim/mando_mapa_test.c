/*
 * DE QUIEN ES EL MANDO CON EL MAPA PUESTO - 15/10/2026
 *
 * *** De un informe: con el mapa en pantalla los controles dejaban de
 * responder. ***
 *
 * Ver User/mando_mapa.h para el fallo y la regla. Este banco existe por
 * un motivo concreto: el fallo se habia pagado ya DOS veces antes -con la
 * rejilla y con el teclado, el 05/10- y las dos se arreglo parcheando el
 * destino que alguien reclamo. Un banco que probara "volumen si, sintonia
 * no" repetiria ese error. Este recorre la tabla ENTERA.
 */
#include <stdio.h>
#include "mando_mapa.h"

static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

/* Los destinos de main.c, en su orden. Si alguien añade uno y no lo pone
 * aqui, el barrido de abajo lo caza igual: va hasta 31. */
static const char *k_nombre[] = {
    "sintonia", "volumen", "brillo", "escala", "squelch", "suavizado",
    "ganancia", "reductor de ruido", "desplazamiento RTTY", "tono de CW",
    "filtro (ancho de banda)", "un ajuste cualquiera (Func)"
};
#define N_DESTINOS (int)(sizeof k_nombre / sizeof k_nombre[0])

int main(void)
{
    const int SINTONIA = 0;
    int d, malos = 0;

    printf("\n== DE QUIEN ES EL MANDO CON EL MAPA PUESTO ==\n\n");

    printf("  1. CON EL MANDO EN SU SITIO (sintonia): el giro es del mapa\n");
    di(mando_mapa_gira(1U, 1U, 0U,  1, SINTONIA, SINTONIA),
       "girando a la derecha hace zoom", "con el mapa puesto el mando hace zoom");

    printf("\n  2. ENGANCHADO A ALGO: el mando es del usuario, no del mapa\n");
    for (d = 1; d < N_DESTINOS; d++) {
        char t[96];
        snprintf(t, sizeof t, "con el mando en %s, el mapa NO se lo queda", k_nombre[d]);
        di(!mando_mapa_gira(1U, 1U, 0U, 1, d, SINTONIA), t,
           "esto es el fallo del informe: la botonera dice una cosa y el mando hace otra");
    }
    for (d = N_DESTINOS; d <= 31; d++) {
        if (mando_mapa_gira(1U, 1U, 0U, 1, d, SINTONIA)) { malos++; }
    }
    di(malos == 0, "y ningun destino futuro se cuela por el camino",
       "alguien ha añadido un destino y la regla no lo cubre");

    printf("\n  3. LAS PUERTAS DE SIEMPRE, QUE NO CAMBIAN\n");
    di(!mando_mapa_gira(0U, 1U, 0U, 1, SINTONIA, SINTONIA),
       "sin mapa en pantalla, el giro no es suyo", "");
    di(!mando_mapa_gira(1U, 0U, 0U, 1, SINTONIA, SINTONIA),
       "en un modo que no tiene mapa, tampoco", "");
    di(!mando_mapa_gira(1U, 1U, 1U, 1, SINTONIA, SINTONIA),
       "con pulsacion larga, tampoco (es 'sacame de aqui')", "");
    di(!mando_mapa_gira(1U, 1U, 0U, 0, SINTONIA, SINTONIA),
       "y sin giro no hay nada que repartir", "");

    printf("\n  4. LA AUTOPRUEBA DEL PROPIO MODULO\n");
    di(mando_mapa_selftest() != 0U, "mando_mapa_selftest()", "");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

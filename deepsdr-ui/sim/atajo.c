/*
 * EL GESTO DE LA SALIDA DE EMERGENCIA, PROBADO EN EL PC.
 *
 * Esto corre el boot/atajo_gesto.c del cargador -el de verdad, no una
 * copia- contra secuencias de patas inventadas a mano. El criterio no es
 * "no revienta", son dos cosas que tienen que cumplirse A LA VEZ:
 *
 *   SALE cuando toca:  pulsado y girando, hacia donde sea.
 *   NO SALE cuando no: pulsado y quieto (eso es modo actualizacion),
 *                      sin pulsar, soltando a mitad, y -la importante-
 *                      con las lineas A/B rebotando por ruido.
 *
 * El rebote es el que justifica el banco entero. La primera version contaba
 * CAMBIOS de estado, y dos lineas con ruido dan cambios a puñados sin que
 * nadie toque el mando: la radio se habria ido a DFU sola, con la pantalla
 * negra, y el usuario habria pensado que el cargador nuevo no arranca.
 * Contando pasos con signo, el rebote se cancela. Aqui se comprueba.
 */
#include "atajo_gesto.h"
#include <stdio.h>
#include <stdlib.h>

static int fallos = 0;

static void bien(const char *que, int ok)
{
    printf("    %-56s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

/* La secuencia de estados de un giro: 0,1,3,2,0,1,3,2... hacia un lado,
 * y al reves hacia el otro. Es el codigo Gray de dos bits. */
static const uint32_t k_gray[4] = { 0U, 1U, 3U, 2U };

/* Corre el automata dandole 'muestras' lecturas. 'paso' es cuantos cuartos
 * de paso avanza el mando entre muestra y muestra (0 = quieto, negativo =
 * al otro lado). 'suelta_en' es la muestra en la que se suelta el boton
 * (-1 = no se suelta). Devuelve lo que contesto el automata. */
static atajo_r corre(int muestras, int paso, int suelta_en, int pulsado_al_empezar)
{
    atajo_t a;
    atajo_r r;
    int i, g = 0;

    r = atajo_empieza(&a, (uint32_t)pulsado_al_empezar, k_gray[0]);

    for (i = 0; (i < muestras) && (r == ATAJO_SIGUE); i++) {
        uint32_t boton = ((suelta_en >= 0) && (i >= suelta_en)) ? 0U : 1U;
        g = (g + paso) & 3;
        if (g < 0) { g += 4; }
        r = atajo_muestra(&a, boton, k_gray[g]);
    }
    return r;
}

int main(void)
{
    printf("\nEL ATAJO DE DFU DEL CARGADOR (boot/atajo_gesto.c)\n\n");

    printf("  cuando SI tiene que salir\n");
    bien("pulsado y girando a la derecha",
         corre(300, +1, -1, 1) == ATAJO_SI);
    bien("pulsado y girando a la izquierda",
         corre(300, -1, -1, 1) == ATAJO_SI);
    /*
     * EL GIRO BRUSCO, QUE ES EL QUE ROMPIO LA PRIMERA VERSION.
     *
     * A 5 ms por lectura, un giro vivo se saltaba cuartos de paso: lo que
     * llegaba era un salto de dos bits, que vale cero porque es lo que nos
     * protege del ruido, y el gesto no salia NUNCA por mucho que se girara.
     * Se arreglo muestreando a 2 ms (ver atajo_gesto.h).
     *
     * Aqui se prueba lo que de verdad puede pasar: un giro irregular que de
     * vez en cuando se come una lectura. Que se pierda algun paso vale; lo
     * que no vale es no llegar nunca.
     */
    bien("giro brusco e irregular, perdiendo lecturas de vez en cuando",
         ({ atajo_t a; atajo_r r; int i, g = 0;
            r = atajo_empieza(&a, 1U, k_gray[0]);
            for (i = 0; (i < 600) && (r == ATAJO_SIGUE); i++) {
                g = (g + (((i % 5) == 0) ? 2 : 1)) & 3;
                r = atajo_muestra(&a, 1U, k_gray[g]);
            }
            r == ATAJO_SI; }));

    /* Y el caso imposible, documentado como tal: DOS cuartos de paso en
     * TODAS las lecturas serian 100 topes por segundo, unas 6.000 vueltas
     * por minuto a mano. No se exige que salga; se deja escrito para que
     * nadie lo persiga creyendo que es un fallo. */
    bien("(dos cuartos en cada lectura es fisicamente imposible: no sale)",
         corre(600, +2, -1, 1) == ATAJO_NO);
    bien("girando despacio pero sin parar, entra dentro de la ventana",
         corre(300, +1, -1, 1) == ATAJO_SI);

    printf("\n  cuando NO tiene que salir\n");
    bien("sin pulsar el boton, aunque gire",
         corre(300, +1, -1, 0) == ATAJO_NO);
    bien("pulsado y QUIETO: eso es modo actualizacion",
         corre(300, 0, -1, 1) == ATAJO_NO);
    bien("pulsado y quieto, y ademas contesta pronto",
         ({ atajo_t a; atajo_r r; int i;
            r = atajo_empieza(&a, 1U, k_gray[0]);
            for (i = 0; (i < 300) && (r == ATAJO_SIGUE); i++) {
                r = atajo_muestra(&a, 1U, k_gray[0]);
            }
            (r == ATAJO_NO) && (i <= (int)ATAJO_MUESTRAS_SECA + 1); }));
    bien("girando pero soltando antes de los 8 cuartos de paso",
         corre(300, +1, 4, 1) == ATAJO_NO);

    printf("\n  el ruido, que es lo que de verdad podia colarse\n");
    {
        /* Las dos lineas rebotando entre dos estados vecinos: cambia el
         * estado en TODAS las muestras, pero no avanza. */
        atajo_t a;
        atajo_r r;
        int i, cambios = 0;
        uint32_t prev = k_gray[0];

        r = atajo_empieza(&a, 1U, k_gray[0]);
        for (i = 0; (i < 1000) && (r == ATAJO_SIGUE); i++) {
            uint32_t ab = k_gray[(i & 1)];
            if (ab != prev) { cambios++; prev = ab; }
            r = atajo_muestra(&a, 1U, ab);
        }
        bien("rebote entre dos estados: cientos de cambios...", cambios > 20);
        bien("...y aun asi NO salta a DFU", r == ATAJO_NO);
    }
    {
        /* Ruido peor: saltos de dos bits a la vez, que son imposibles en un
         * mando de verdad. La tabla los manda a cero. */
        atajo_t a;
        atajo_r r;
        int i;
        r = atajo_empieza(&a, 1U, 0U);
        for (i = 0; (i < 1000) && (r == ATAJO_SIGUE); i++) {
            r = atajo_muestra(&a, 1U, (i & 1) ? 3U : 0U);   /* 00 <-> 11 */
        }
        bien("saltos imposibles de dos bits: tampoco saltan", r == ATAJO_NO);
    }
    {
        /* Y el caso mixto, que es el realista: un cuarto de giro suelto
         * -alguien que roza el mando- mas ruido. No llega a 8. */
        atajo_t a;
        atajo_r r;
        int i;
        r = atajo_empieza(&a, 1U, k_gray[0]);
        r = atajo_muestra(&a, 1U, k_gray[1]);
        r = atajo_muestra(&a, 1U, k_gray[2]);
        for (i = 0; (i < 1000) && (r == ATAJO_SIGUE); i++) {
            r = atajo_muestra(&a, 1U, k_gray[(i & 1) ? 1 : 2]);
        }
        bien("un roce de 2 cuartos mas ruido: se queda corto", r == ATAJO_NO);
    }

    printf("\n  la ventana\n");
    bien("girando un cuarto cada 3 muestras, 8 cuartos caben en 1,2 s",
         ({ atajo_t a; atajo_r r; int i, g = 0;
            r = atajo_empieza(&a, 1U, k_gray[0]);
            for (i = 0; (i < 400) && (r == ATAJO_SIGUE); i++) {
                if ((i % 3) == 0) { g = (g + 1) & 3; }
                r = atajo_muestra(&a, 1U, k_gray[g]);
            }
            r == ATAJO_SI; }));

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

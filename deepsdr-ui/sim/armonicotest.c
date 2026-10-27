/*
 * EL AVISADOR DE ARMONICOS, CONTRA EL CASO QUE LO TRAJO.
 *
 * La prueba principal de este banco es EXACTAMENTE ese caso, con los numeros
 * de verdad: dial 3.411 kHz, LO 3.387, y el FT8 de 30 metros en 10.136
 * entrando por el tercer armonico. Si algun dia esto deja de decirlo, el
 * banco se pone rojo.
 *
 * Y lo que mas importa del diseño tambien se prueba: que la tabla de bandas
 * NO vive aqui dentro. Quien llama pasa una funcion que recorre la suya, asi
 * que una banda nueva en la radio aparece aqui sola. Este banco usa una
 * tabla de juguete para poder poner los casos limite a mano.
 */
#include "armonico.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;
static void di(const char *que, int bien, const char *det)
{
    printf("    %-54s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!bien) { fallos++; }
}

/* --- una tabla de juguete, con los casos que importan -------------------- */

typedef struct { uint32_t lo, hi; const char *n; } rango_t;

static const rango_t k_tabla[] = {
    { 3500000UL,  3800000UL,  "80 m"          },
    { 7000000UL,  7200000UL,  "40 m"          },
    { 10100000UL, 10150000UL, "30 m"          },   /* el del caso real */
    { 14000000UL, 14350000UL, "20 m"          },
    { 9400000UL,  9900000UL,  "31 m difusion" },
    { 0UL,        0UL,        "fila vacia"    },   /* se tiene que saltar */
    { 5000000UL,  4000000UL,  "al reves"      },   /* y esta tambien */
    { 20000000UL, 20000000UL, "frecuencia suelta" },
};
#define N_TABLA ((uint16_t)(sizeof k_tabla / sizeof k_tabla[0]))

static uint8_t rangos(uint16_t i, uint32_t *lo, uint32_t *hi, const char **nom)
{
    if (i >= N_TABLA) { return 0U; }
    *lo = k_tabla[i].lo; *hi = k_tabla[i].hi; *nom = k_tabla[i].n;
    return 1U;
}

/*
 * Y UNA TABLA PARA EL RECORTE: cuatro bandas apiladas donde cae el SEGUNDO
 * armonico y una sola donde cae el TERCERO.
 *
 * Es el caso que se daba en la radio de verdad: en la tabla real, 40 m,
 * 41 m y SSTV 40 comparten los 7,16 MHz, o sea el mismo armonico, y se
 * gastaban tres de los cuatro huecos. Aqui se pone a proposito para que
 * quepa en una tabla de juguete.
 *
 * Con el LO en 3.580 kHz: x2 = 7.160 (cuatro bandas) y x3 = 10.740 (una).
 * El x3 esta VEINTE DECIBELIOS por encima del x2, asi que tiene que ser el
 * que sobreviva al recorte y el que salga el primero.
 */
static const rango_t k_apila[] = {
    { 7100000UL,  7200000UL,  "doble A" },
    { 7110000UL,  7200000UL,  "doble B" },
    { 7120000UL,  7200000UL,  "doble C" },
    { 7130000UL,  7200000UL,  "doble D" },
    { 10700000UL, 10800000UL, "triple"  },
};
#define N_APILA ((uint16_t)(sizeof k_apila / sizeof k_apila[0]))

static uint8_t apila(uint16_t i, uint32_t *lo, uint32_t *hi, const char **nom)
{
    if (i >= N_APILA) { return 0U; }
    *lo = k_apila[i].lo; *hi = k_apila[i].hi; *nom = k_apila[i].n;
    return 1U;
}

/* una tabla que dice que no a todo, para el caso de "no hay tabla" */
static uint8_t ninguno(uint16_t i, uint32_t *lo, uint32_t *hi, const char **n)
{
    (void)i; (void)lo; (void)hi; (void)n;
    return 0U;
}

int main(void)
{
    char det[128];
    arm_hit_t h[8];
    uint8_t k, n;

    printf("\nEL AVISADOR DE ARMONICOS\n\n");

    /* ------------------------------------------------------------------ */
    printf("  el caso de verdad: dial 3.411 kHz, LO 3.387\n");
    n = arm_busca(3387000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
    for (k = 0U; k < n && k < 8U; k++) {
        printf("      x%u  %8lu Hz  ->  %-16s %+6ld Hz\n",
               (unsigned)h[k].n, (unsigned long)h[k].f_hz, h[k].nombre,
               (long)h[k].off_hz);
    }
    snprintf(det, sizeof det, "%u encontrados", (unsigned)n);
    di("encuentra algo", n >= 1U, det);
    di("y es el tercer armonico", n >= 1U && h[0].n == 3U,
       n >= 1U ? h[0].nombre : "nada");
    di("y la banda es la de 30 m", n >= 1U && strcmp(h[0].nombre, "30 m") == 0,
       n >= 1U ? h[0].nombre : "nada");
    snprintf(det, sizeof det, "%lu Hz", (unsigned long)(n ? h[0].f_hz : 0UL));
    di("3 x 3.387 = 10.161 kHz", n >= 1U && h[0].f_hz == 10161000UL, det);

    /* ------------------------------------------------------------------ */
    printf("\n  donde NO hay nada, no se inventa nada\n");
    {
        /* 2.000 kHz: x2=4.000, x3=6.000, x4=8.000, x5=10.000 (fuera de
         * 10.100-10.150 por 100 kHz), x6=12.000, x7=14.000 -> 20 m, que
         * empieza justo en 14.000. Asi que este tiene que encontrar UNO. */
        n = arm_busca(2000000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%u", (unsigned)n);
        di("en 2.000 kHz encuentra el x7 en 20 m", n == 1U && h[0].n == 7U, det);

        /* 1.111 kHz: ningun multiplo cae en ninguna banda de la tabla */
        n = arm_busca(1111000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%u encontrados", (unsigned)n);
        di("en 1.111 kHz no encuentra ninguno", n == 0U, det);
    }

    /* ------------------------------------------------------------------ */
    /*
     * EL ORDEN, Y ESTE ASERTO ERA EL FALLO - 24/10/2026.
     *
     * Decia "el 2.o entra mucho mas fuerte que el 7.o" y exigia que el 2.o
     * saliera el primero. Es falso: una cuadrada de ciclo 50 % NO TIENE
     * armonicos pares, y los que se ven salen del error de ciclo. Medido en
     * sim/qsd_armonico.c, con un 1 % de error el 2.o queda en -32 dB; el
     * 3.o esta en -9,5 dB SIEMPRE, porque sale de la propia cuadrada.
     *
     * O sea que el banco tenia congelado el criterio equivocado, que es la
     * peor forma de tenerlo: una comprobacion que defiende un fallo.
     */
    printf("\n  el orden: por el nivel que cabe esperar, no por el numero\n");
    {
        /* 3.550 kHz: x2 = 7.100 -> 40 m, y x4 = 14.200 -> 20 m. Los dos son
         * PARES, asi que entre ellos manda el mas bajo. */
        n = arm_busca(3550000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        for (k = 0U; k < n && k < 8U; k++) {
            printf("      x%u  %-16s  %s  (%u,%u dB por debajo)\n",
                   (unsigned)h[k].n, h[k].nombre,
                   h[k].par ? "DOS copias" : "una copia ",
                   (unsigned)(arm_nivel_dbx10(h[k].n) / 10U),
                   (unsigned)(arm_nivel_dbx10(h[k].n) % 10U));
        }
        snprintf(det, sizeof det, "%u encontrados, el primero x%u",
                 (unsigned)n, (unsigned)(n ? h[0].n : 0U));
        di("encuentra los dos, y entre dos pares manda el mas bajo",
           n == 2U && h[0].n == 2U && h[1].n == 4U, det);
        di("y los dos van marcados como pareja simetrica",
           n == 2U && h[0].par == 1U && h[1].par == 1U, 0);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  un impar manda sobre un par, aunque sea de orden mayor\n");
    {
        /*
         * El 3.o esta a -9,5 dB y el 2.o a -30: con los dos en la lista, el
         * sospechoso principal es el 3.o. Es justo lo contrario de lo que
         * hacia antes.
         */
        uint8_t vistos2 = 0U, vistos3 = 0U, orden_ok = 0U;
        uint32_t lo;

        for (lo = 1000000UL; lo <= 5000000UL; lo += 1000UL) {
            n = arm_busca(lo, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
            if (n >= 2U) {
                uint8_t k2 = 0U, k3 = 0U;
                for (k = 0U; k < n && k < 8U; k++) {
                    if (h[k].n == 2U) { k2 = 1U; }
                    if (h[k].n == 3U) { k3 = 1U; }
                }
                if (k2 && k3) {
                    vistos2 = 1U; vistos3 = 1U;
                    orden_ok = (uint8_t)(h[0].n == 3U);
                    if (!orden_ok) { break; }
                }
            }
        }
        snprintf(det, sizeof det, "%s",
                 (vistos2 && vistos3) ? "hay casos con el 2.o y el 3.o a la vez"
                                      : "la tabla de juguete no da ese caso");
        if (vistos2 && vistos3) {
            di("con el 2.o y el 3.o juntos, manda el 3.o", orden_ok != 0U, det);
        } else {
            printf("    (%s)\n", det);
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n  el filtro de entrada deja fuera lo que no puede llegar\n");
    {
        uint8_t sin_filtro, con_filtro;

        /* 3.550 kHz con la tabla de juguete: x2 y x4 caen en banda. Con el
         * techo del filtro en 10 MHz, x4 = 14,2 MHz no llega. */
        sin_filtro = arm_busca(3550000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        con_filtro = arm_busca(3550000UL, 96000UL, 10000000UL, rangos, N_TABLA,
                               h, 8U);
        snprintf(det, sizeof det, "sin filtro %u, con el techo en 10 MHz %u",
                 (unsigned)sin_filtro, (unsigned)con_filtro);
        di("un armonico por encima del filtro no se cuenta",
           sin_filtro == 2U && con_filtro == 1U && h[0].n == 2U, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  las filas rotas de la tabla se saltan sin quejarse\n");
    {
        /*
         * La tabla de juguete lleva a proposito una fila vacia (0,0), una
         * con los limites al reves y una frecuencia suelta (lo == hi). Las
         * dos primeras no pueden salir NUNCA; la tercera si, porque una
         * frecuencia suelta es un rango legitimo de ancho cero.
         */
        int vacia = 0, reves = 0, suelta = 0;
        uint32_t lo;
        for (lo = 1000000UL; lo <= 11000000UL; lo += 7000UL) {
            n = arm_busca(lo, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
            for (k = 0U; k < n && k < 8U; k++) {
                if (strcmp(h[k].nombre, "fila vacia") == 0) { vacia++; }
                if (strcmp(h[k].nombre, "al reves") == 0)   { reves++; }
                if (strcmp(h[k].nombre, "frecuencia suelta") == 0) { suelta++; }
            }
        }
        snprintf(det, sizeof det, "vacia %d, al reves %d", vacia, reves);
        di("ni la fila vacia ni la de limites al reves salen nunca",
           vacia == 0 && reves == 0, det);
        snprintf(det, sizeof det, "%d veces", suelta);
        di("y una frecuencia suelta SI sale", suelta > 0, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  los casos que revientan un bucle mal escrito\n");
    {
        n = arm_busca(0UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        di("con el LO a cero no devuelve nada", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, 0UL, ninguno, N_TABLA, h, 8U);
        di("con una tabla que no da nada, tampoco", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, 0UL, rangos, 0U, h, 8U);
        di("con una tabla de cero filas, tampoco", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, 0UL, rangos, N_TABLA, h, 1U);
        snprintf(det, sizeof det, "escribe 1, cuenta %u", (unsigned)n);
        di("con sitio para uno, cuenta todos pero escribe uno", n >= 1U, det);
        /*
         * Y el desbordamiento de n*LO. Con el LO cerca del tope de 32 bits,
         * un bucle que multiplique sin mirar da vueltas y empieza a enseñar
         * bandas al azar en vez de parar.
         */
        n = arm_busca(4000000000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%u encontrados", (unsigned)n);
        di("con un LO absurdo no se desborda ni inventa bandas", n == 0U, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  el desvio se mide a lo que CAE DENTRO, no al centro del rango\n");
    {
        /*
         * 31 m de radiodifusion son 500 kHz de ancho; con el x3 de 3.200
         * -9.600 kHz- la banda entera esta encima, y el centro del rango
         * -9.650- cae a +50 kHz, FUERA del tramo de 96. Decir "+50 kHz" de
         * algo que se esta oyendo dentro de +-48 seria mentir.
         */
        n = arm_busca(3200000UL, 96000UL, 0UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%+ld Hz", (long)(n ? h[0].off_hz : 0L));
        di("el desvio se queda dentro del tramo",
           n >= 1U && h[0].off_hz > -48000L && h[0].off_hz < 48000L, det);
    }

    /* ------------------------------------------------------------------
     * *** EL RECORTE SE QUEDA CON EL MAS FUERTE, NO CON EL PRIMERO ***
     * ------------------------------------------------------------------
     * Aqui se escribia por orden de armonico y se ordenaba por nivel
     * DESPUES, sobre lo que ya habia quedado dentro. Con hueco para cuatro
     * y cuatro bandas en el x2, el x3 -veinte dB mas fuerte- no llegaba a
     * escribirse nunca, y main.c solo enseña el primero.
     *
     * El banco no lo veia porque llamaba con max=1 y solo exigia n>=1, o
     * sea sin mirar CUAL sobrevive, y porque su tabla de juguete no tenia
     * bandas solapadas que pudieran llenar el hueco.
     * ------------------------------------------------------------------ */
    printf("\n  el recorte se queda con el armonico MAS FUERTE\n");
    {
        arm_hit_t g[4];
        uint8_t t;

        t = arm_busca(3580000UL, 96000UL, 0UL, apila, N_APILA, g, 4U);
        snprintf(det, sizeof det, "cuenta %u, el primero es x%u %s",
                 (unsigned)t, (unsigned)g[0].n, g[0].nombre);
        di("con cuatro bandas en el x2, el x3 no se pierde",
           t == 5U && g[0].n == 3U, det);

        /* Y con sitio para UNO, el que queda tiene que ser el x3. */
        t = arm_busca(3580000UL, 96000UL, 0UL, apila, N_APILA, g, 1U);
        snprintf(det, sizeof det, "el que queda es x%u", (unsigned)g[0].n);
        di("con sitio para uno, el que queda es el mas fuerte",
           t == 5U && g[0].n == 3U, det);
    }

    /* ------------------------------------------------------------------
     * *** EL DESVIO VA EN EL EJE DE LA PANTALLA ***
     * ------------------------------------------------------------------
     * El 3.o y el 7.o salen en el lado CONTRARIO -el desfase entre I y Q
     * vale n*90 grados, y con n%4==3 son -90, o sea que la banda lateral
     * se da la vuelta-. Lo mide sim/qsd_armonico.c. Aqui se comprueba que
     * arm_busca() ya entrega el numero con el eje bueno, que es lo que la
     * fila de Informacion imprime.
     * ------------------------------------------------------------------ */
    printf("\n  el desvio sale en el eje de la pantalla\n");
    {
        arm_hit_t g[8];
        uint8_t t;

        /*
         * El caso de portada, con los numeros de verdad: LO 3.387, x3 en
         * 10.161, y 30 m (10.100-10.150) por DEBAJO en radiofrecuencia.
         * Al darse la vuelta, en pantalla tiene que salir POR ENCIMA.
         */
        t = arm_busca(3387000UL, 96000UL, 0UL, rangos, N_TABLA, g, 8U);
        snprintf(det, sizeof det, "x%u %s a %+ld Hz",
                 (unsigned)g[0].n, g[0].nombre, (long)g[0].off_hz);
        di("el 3.o sale al otro lado del que cae en RF",
           t >= 1U && g[0].n == 3U && g[0].off_hz > 0L, det);

        /*
         * Y un armonico PAR sale por los DOS lados, asi que lo que se
         * entrega es el MODULO: un signo mandaria a buscarlo a un sitio
         * cuando esta en dos. El campo `par` es el que lo dice.
         */
        t = arm_busca(3580000UL, 96000UL, 0UL, apila, N_APILA, g, 8U);
        {
            uint8_t j, pares = 0U, con_signo = 0U;

            for (j = 0U; j < (t < 8U ? t : 8U); j++) {
                if (!g[j].par) { continue; }
                pares++;
                if (g[j].off_hz < 0L) { con_signo++; }
            }
            snprintf(det, sizeof det, "%u pares, %u con signo",
                     (unsigned)pares, (unsigned)con_signo);
            di("los pares salen marcados y sin signo",
               pares >= 1U && con_signo == 0U, det);
        }
    }

    /* ------------------------------------------------------------------
     * *** Y LA TABLA DE NIVELES, CONTRA LA FISICA ***
     * ------------------------------------------------------------------
     * Los impares caen con 1/n. Los PARES no: su nivel lo fija el error de
     * ciclo de trabajo y es el MISMO para todos -sin(k*pi*D)/(k*pi) con
     * D = 0,5+e y k par da (-1)^m * e, sin k dentro-. Lo mide tambien
     * sim/qsd_armonico.c: 2.o y 4.o salen los dos a -31,7 dB.
     *
     * Aqui la tabla decia -30, -36 y -40, o sea que se inventaba un orden
     * entre los pares. Y desde el arreglo del recorte, este numero ya no
     * solo ordena: decide quien sobrevive.
     * ------------------------------------------------------------------ */
    printf("\n  la tabla de niveles dice lo que mide la fisica\n");
    {
        snprintf(det, sizeof det, "x2 %u  x4 %u  x6 %u (decimas de dB)",
                 (unsigned)arm_nivel_dbx10(2U), (unsigned)arm_nivel_dbx10(4U),
                 (unsigned)arm_nivel_dbx10(6U));
        di("todos los pares valen lo mismo",
           arm_nivel_dbx10(2U) == arm_nivel_dbx10(4U)
           && arm_nivel_dbx10(4U) == arm_nivel_dbx10(6U), det);

        snprintf(det, sizeof det, "x3 %u  x5 %u  x7 %u",
                 (unsigned)arm_nivel_dbx10(3U), (unsigned)arm_nivel_dbx10(5U),
                 (unsigned)arm_nivel_dbx10(7U));
        di("y los impares siguen cayendo con 1/n",
           arm_nivel_dbx10(3U) < arm_nivel_dbx10(5U)
           && arm_nivel_dbx10(5U) < arm_nivel_dbx10(7U), det);

        di("y cualquier impar es mas fuerte que cualquier par",
           arm_nivel_dbx10(7U) < arm_nivel_dbx10(2U), 0);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

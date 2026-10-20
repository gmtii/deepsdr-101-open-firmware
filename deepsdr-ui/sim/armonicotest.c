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
    n = arm_busca(3387000UL, 96000UL, rangos, N_TABLA, h, 8U);
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
        n = arm_busca(2000000UL, 96000UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%u", (unsigned)n);
        di("en 2.000 kHz encuentra el x7 en 20 m", n == 1U && h[0].n == 7U, det);

        /* 1.111 kHz: ningun multiplo cae en ninguna banda de la tabla */
        n = arm_busca(1111000UL, 96000UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%u encontrados", (unsigned)n);
        di("en 1.111 kHz no encuentra ninguno", n == 0U, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  el orden importa: el 2.o entra mucho mas fuerte que el 7.o\n");
    {
        /* 3.550 kHz: x2 = 7.100 -> 40 m, y x4 = 14.200 -> 20 m */
        n = arm_busca(3550000UL, 96000UL, rangos, N_TABLA, h, 8U);
        for (k = 0U; k < n && k < 8U; k++) {
            printf("      x%u  %-16s\n", (unsigned)h[k].n, h[k].nombre);
        }
        snprintf(det, sizeof det, "%u encontrados, el primero x%u",
                 (unsigned)n, (unsigned)(n ? h[0].n : 0U));
        di("encuentra los dos y el 2.o armonico sale primero",
           n == 2U && h[0].n == 2U && h[1].n == 4U, det);
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
            n = arm_busca(lo, 96000UL, rangos, N_TABLA, h, 8U);
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
        n = arm_busca(0UL, 96000UL, rangos, N_TABLA, h, 8U);
        di("con el LO a cero no devuelve nada", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, ninguno, N_TABLA, h, 8U);
        di("con una tabla que no da nada, tampoco", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, rangos, 0U, h, 8U);
        di("con una tabla de cero filas, tampoco", n == 0U, 0);
        n = arm_busca(3387000UL, 96000UL, rangos, N_TABLA, h, 1U);
        snprintf(det, sizeof det, "escribe 1, cuenta %u", (unsigned)n);
        di("con sitio para uno, cuenta todos pero escribe uno", n >= 1U, det);
        /*
         * Y el desbordamiento de n*LO. Con el LO cerca del tope de 32 bits,
         * un bucle que multiplique sin mirar da vueltas y empieza a enseñar
         * bandas al azar en vez de parar.
         */
        n = arm_busca(4000000000UL, 96000UL, rangos, N_TABLA, h, 8U);
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
        n = arm_busca(3200000UL, 96000UL, rangos, N_TABLA, h, 8U);
        snprintf(det, sizeof det, "%+ld Hz", (long)(n ? h[0].off_hz : 0L));
        di("el desvio se queda dentro del tramo",
           n >= 1U && h[0].off_hz > -48000L && h[0].off_hz < 48000L, det);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

/*
 * BANCO DEL HUSO HORARIO Y DE LA TABLA DE CIUDADES. 10/10/2026.
 *
 * QUE COMPRUEBA Y POR QUE
 * -----------------------
 * Hasta esta version la radio tenia ESPAÑA CABLEADA en tres sitios y daba la
 * hora de Madrid en Sidney sin avisar. Lo que la sustituye son dos cosas que
 * no se pueden mirar y dar por buenas:
 *
 *   - un desplazamiento en minutos con una prioridad entre fuentes (lo que
 *     dice una emisora le gana a lo que dice una tabla);
 *   - cinco reglas de horario de verano, que es calendario, que es
 *     exactamente donde este proyecto ya se ha equivocado antes y donde el
 *     error NO SE VE: sale una hora plausible, una semana al año.
 *
 * La fuente de la verdad es sim/husos_generado.h, que sale de la base de
 * husos del sistema (zoneinfo) y NO de la regla del firmware: 24.804
 * instantes de cinco zonas reales, densos en los dos dias del cambio de cada
 * año y sueltos el resto. Ver tools/gen_husos.py.
 *
 * LO QUE ESTE BANCO YA HA ENCONTRADO, ANTES DE QUEDARSE VERDE
 * ----------------------------------------------------------
 *   1. Tres de las cinco reglas estan escritas en HORA LOCAL y se estaban
 *      comparando contra UTC. En Sidney las 02:00 locales son las 16:00 UTC
 *      del dia anterior: catorce horas de error dos dias al año.
 *   2. La regla de Chile NO ES "el domingo que hace N": cambia el primer
 *      SABADO de septiembre a las 24:00, que en 2024 fue el dia 8 -el
 *      segundo domingo- y en 2025 el 7 -el primero-. Por eso la tabla guarda
 *      "el primer domingo a partir del dia D" y no un ordinal.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "husos.h"
#include "hora_comun.h"
#include "husos_generado.h"

static int fallos = 0;
static void comprueba(const char *q, int ok)
{
    printf("    %-58s %s\n", q, ok ? "OK" : "NO");
    if (!ok) { fallos++; }
}

/* ---------------------------------------------------------------------
 * Un calendario de juguete, a proposito distinto del del firmware: dias
 * desde el 1/1/2000. Sirve para comprobar hora_suma_minutos() sin volver a
 * usar hora_suma_horas(), que es justo lo que se quiere verificar.
 * --------------------------------------------------------------------- */
/* Minutos que tiene el siglo 2000..2099: 36.525 dias (25 bisiestos, porque
 * 2000 lo fue y 2100 cae fuera). */
#define SIGLO  (36525L * 1440L)

static long jdn(int d, int m, int a)
{
    long y = 2000 + a, mm = m, dias = 0, i;

    for (i = 2000; i < y; i++) {
        dias += ((i % 4 == 0 && i % 100 != 0) || i % 400 == 0) ? 366 : 365;
    }
    {
        static const int t[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
        for (i = 1; i < mm; i++) {
            dias += t[i];
            if (i == 2 && (((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))) { dias++; }
        }
    }
    return dias + d - 1;
}

int main(void)
{
    unsigned i;

    printf("\nHUSO HORARIO Y TABLA DE CIUDADES\n");

    /* ================================================================
     * 1. LA PRIORIDAD ENTRE FUENTES. Una emisora le gana a una tabla.
     * ================================================================ */
    printf("\n  1. DE DONDE SALE EL HUSO, Y QUIEN LE GANA A QUIEN\n");
    hora_huso_pon(60);
    comprueba("una tabla lo pone", hora_huso() == 60);
    comprueba("y no esta medido", hora_huso_medido() == 0U);
    hora_huso_pon(330);
    comprueba("otra tabla lo vuelve a poner", hora_huso() == 330);
    hora_huso_mide(600);
    comprueba("una emisora lo pone", hora_huso() == 600);
    comprueba("y queda marcado como medido", hora_huso_medido() == 1U);
    hora_huso_pon(60);
    comprueba("*** y ya NINGUNA tabla lo pisa ***", hora_huso() == 600);
    hora_huso_mide(-300);
    comprueba("pero otra emisora si", hora_huso() == -300);
    hora_huso_mide(32000);
    comprueba("se recorta por arriba", hora_huso() == HORA_HUSO_MAX);
    hora_huso_mide(-32000);
    comprueba("y por abajo", hora_huso() == HORA_HUSO_MIN);

    /* ================================================================
     * 2. SUMAR MINUTOS A UNA FECHA. Lo que hacia falta para +5:30.
     * ================================================================ */
    printf("\n  2. hora_suma_minutos() CONTRA UN CALENDARIO DISTINTO\n");
    {
        static const int16_t k_min[] = { 0, 30, 45, -30, 60, -60, 330, 345,
                                         -720, 840, 570, -210 };
        unsigned malos = 0, n = 0;
        int a, m, d, h, mi, k;

        for (a = 0; a < 100; a += 7) {
            for (m = 1; m <= 12; m++) {
                int ultimo = hora_dias_del_mes((uint8_t)m, (uint8_t)a);
                for (d = 1; d <= ultimo; d += 9) {
                    for (h = 0; h < 24; h += 5) {
                        for (mi = 0; mi < 60; mi += 23) {
                            for (k = 0; k < (int)(sizeof k_min / sizeof k_min[0]); k++) {
                                uint8_t hh = (uint8_t)h, mm = (uint8_t)mi;
                                uint8_t dd = (uint8_t)d, me = (uint8_t)m;
                                uint8_t aa = (uint8_t)a, ds = 0U;
                                long t0, t1;

                                hora_suma_minutos(k_min[k], &hh, &mm, &dd,
                                                  &me, &aa, &ds);
                                /* El mismo instante contado en minutos desde
                                 * el 1/1/2000, por el otro calendario. */
                                t0 = jdn(d, m, a) * 1440L + h * 60L + mi + k_min[k];
                                t1 = jdn(dd, me, aa) * 1440L + hh * 60L + mm;
                                n++;
                                /*
                                 * MODULO EL SIGLO, Y ESO NO ES UN APAÑO.
                                 * El firmware maneja el año en dos cifras
                                 * -porque asi lo emiten las cinco emisoras-
                                 * y por tanto 2000 menos media hora da el
                                 * 31/12/99, que son 36.525 dias de mas. La
                                 * alternativa seria inventarse un año 1999
                                 * que las emisoras no pueden decir. Que la
                                 * vuelta sea EXACTAMENTE el siglo se
                                 * comprueba aparte, justo debajo.
                                 */
                                if (((t0 % SIGLO) + SIGLO) % SIGLO
                                    != ((t1 % SIGLO) + SIGLO) % SIGLO) { malos++; }
                                if (ds != hora_dia_semana(dd, me, aa)) { malos++; }
                            }
                        }
                    }
                }
            }
        }
        printf("    %u sumas comprobadas\n", n);
        comprueba("todas cuadran, y el dia de la semana tambien", malos == 0U);
    }
    {
        /* LA VUELTA DEL SIGLO, mirada de frente y no esquivada. */
        uint8_t h = 0U, mi = 0U, d = 1U, m = 1U, a = 0U, ds = 0U;

        hora_suma_minutos(-30, &h, &mi, &d, &m, &a, &ds);
        printf("    01/01/00 00:00 menos 30 min -> %02u/%02u/%02u %02u:%02u\n",
               d, m, a, h, mi);
        comprueba("hacia atras desde el 2000 se da la vuelta al siglo",
                  d == 31U && m == 12U && a == 99U && h == 23U && mi == 30U);
        h = 23U; mi = 30U; d = 31U; m = 12U; a = 99U;
        hora_suma_minutos(30, &h, &mi, &d, &m, &a, &ds);
        comprueba("y hacia delante desde el 2099 tambien",
                  d == 1U && m == 1U && a == 0U && h == 0U && mi == 0U);
    }

    /* ================================================================
     * 3. LAS CINCO REGLAS, CONTRA zoneinfo.
     * ================================================================ */
    printf("\n  3. LAS CINCO REGLAS CONTRA LA BASE DE HUSOS DEL SISTEMA\n");
    {
        unsigned malos[HUSO_DST_N];
        unsigned vistos[HUSO_DST_N];
        static const char *k_nom[HUSO_DST_N] =
            { "-", "EU", "USA", "AU", "NZ", "CL" };
        unsigned r;

        memset(malos, 0, sizeof malos);
        memset(vistos, 0, sizeof vistos);
        for (i = 0U; i < (unsigned)N_HUSOS; i++) {
            const huso_caso_t *c = &k_husos[i];
            uint8_t v = husos_verano(c->r, (int16_t)k_huso_base[c->r],
                                     c->d, c->m, c->a, c->h);
            vistos[c->r]++;
            if (v != c->v) { malos[c->r]++; }
        }
        for (r = 1U; r < HUSO_DST_N; r++) {
            printf("    %-4s %6u instantes, %u fallos\n",
                   k_nom[r], vistos[r], malos[r]);
        }
        for (r = 1U; r < HUSO_DST_N; r++) {
            char q[64];
            sprintf(q, "la regla %s acierta los %u instantes", k_nom[r], vistos[r]);
            comprueba(q, malos[r] == 0U);
        }
    }

    /* ================================================================
     * 4. QUE EL BANCO SEPA FALLAR. Un banco que no se ha visto en rojo
     *    no esta probado: aqui se rompe la regla a proposito -un dia
     *    antes el cambio de primavera- y TIENE que saltar.
     * ================================================================ */
    printf("\n  4. Y QUE ESTO SEPA PONERSE EN ROJO\n");
    {
        unsigned malos = 0;

        for (i = 0U; i < (unsigned)N_HUSOS; i++) {
            const huso_caso_t *c = &k_husos[i];
            /* Se le quita una hora a la entrada: si la ventana del cambio no
             * estuviera siendo mirada de verdad, esto saldria igual. */
            uint8_t v = husos_verano(c->r, (int16_t)(k_huso_base[c->r] + 60),
                                     c->d, c->m, c->a, c->h);
            if (v != c->v) { malos++; }
        }
        printf("    con el huso base corrido una hora: %u fallos\n", malos);
        comprueba("*** desplazar la base una hora SI rompe el banco ***",
                  malos > 0U);
    }

    /* ================================================================
     * 5. LA TABLA DE CIUDADES.
     * ================================================================ */
    printf("\n  5. LA TABLA DE CIUDADES\n");
    {
        unsigned n = husos_n(), malos = 0, j;

        for (i = 0U; i < n; i++) {
            const huso_ciudad_t *c = husos_fila((uint8_t)i);

            if (c->base < HORA_HUSO_MIN || c->base > HORA_HUSO_MAX) { malos++; }
            if (c->base % 15 != 0) { malos++; }        /* no existe ninguno que no */
            if (c->regla >= HUSO_DST_N) { malos++; }
            if (c->lat_d < -900 || c->lat_d > 900) { malos++; }
            if (c->lon_d < -1800 || c->lon_d > 1800) { malos++; }
            if (c->nombre[0] == '\0') { malos++; }
            /* que quepa el cero final: el campo es char[14] sin margen */
            if (strnlen(c->nombre, sizeof c->nombre) >= sizeof c->nombre) { malos++; }
            for (j = 0U; j < i; j++) {
                if (strcmp(husos_fila((uint8_t)j)->nombre, c->nombre) == 0) { malos++; }
            }
        }
        printf("    %u ciudades\n", n);
        comprueba("todas con huso, regla, nombre y coordenadas validos",
                  malos == 0U);
    }

    /* ================================================================
     * 6. EL ORDEN POR CERCANIA. Lo unico para lo que sirve el locator.
     * ================================================================ */
    printf("\n  6. ORDENADAS POR EL LOCALIZADOR\n");
    {
        struct { int32_t lat, lon; const char *cerca; const char *quien; } k[] = {
            {  404375,  -35417, "Madrid",       "el usuario, IN80fp"   },
            {  475000,  190000, "Budapest",     "Hungria"            },
            {  357000, 1397000, "Tokio",        "Tokio"              },
            { -368000, 1748000, "Auckland",     "Nueva Zelanda"      },
            {  610000, -1499000,"Anchorage",    "Alaska"             },
            { -346000, -584000, "Buenos Aires", "Argentina"          },
            {  641000, -219000, "Reikiavik",    "Islandia"           },
            {  286000,  772000, "Delhi",        "la India"           }
        };
        uint8_t orden[64];
        unsigned t;

        for (t = 0U; t < sizeof k / sizeof k[0]; t++) {
            char q[80];
            const char *sale;

            husos_ordena(k[t].lat, k[t].lon, orden);
            sale = husos_fila(orden[0])->nombre;
            sprintf(q, "desde %s sale %s", k[t].quien, sale);
            comprueba(q, strcmp(sale, k[t].cerca) == 0);
        }

        /*
         * EL ANTIMERIDIANO, que es lo que rompe la version facil. Un punto a
         * 179 E y otro a 179 W estan a 220 km; restando longitudes salen 358
         * grados. Desde las Fiyi (178 E) lo mas cerca tiene que ser Auckland,
         * que esta al otro lado en longitud pero a 2.100 km.
         */
        husos_ordena(-180000, 1780000, orden);
        comprueba("*** y cruzando el antimeridiano tambien (Fiyi -> Auckland)",
                  strcmp(husos_fila(orden[0])->nombre, "Auckland") == 0);
    }

    /* ================================================================
     * 7. QUE LOS DOS NUMEROS DE CADA FILA SEAN DOS. Si "ahora" nunca se
     *    separase de "base", la columna sobraria y la regla no haria nada.
     * ================================================================ */
    printf("\n  7. EL HUSO DE HOY CONTRA EL DE INVIERNO\n");
    {
        unsigned n = husos_n(), con_regla = 0, movidas = 0;

        for (i = 0U; i < n; i++) {
            const huso_ciudad_t *c = husos_fila((uint8_t)i);
            int16_t enero = husos_ahora((uint8_t)i, 15U, 1U, 26U, 12U);
            int16_t julio = husos_ahora((uint8_t)i, 15U, 7U, 26U, 12U);

            if (c->regla != HUSO_DST_NO) { con_regla++; }
            if (enero != julio) { movidas++; }
            if (c->regla == HUSO_DST_NO && enero != c->base) { fallos++; }
        }
        printf("    %u con regla, %u cambian entre enero y julio\n",
               con_regla, movidas);
        comprueba("las que tienen regla se mueven y las que no, no",
                  movidas == con_regla && con_regla > 0U);
    }

    printf("\n  %s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

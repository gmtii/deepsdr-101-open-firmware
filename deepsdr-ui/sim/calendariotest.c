/*
 * BANCO DEL CALENDARIO COMPARTIDO (User/hora_comun.c). 23/09/2026.
 *
 * QUE COMPRUEBA Y POR QUE. Tres de las cinco emisoras de hora no dan la hora
 * de pared española: MSF y WWV dan UTC y RBU da hora de Moscu. Convertirlas
 * necesita saber si esta vigente el horario de verano, y eso se decide con
 * una regla de calendario -ultimo domingo de marzo, ultimo domingo de
 * octubre, a las 01:00 UTC- que no se puede comprobar mirandola.
 *
 * Un fallo aqui no se ve: el reloj se pondria una hora desviado UNA SEMANA
 * AL AÑO, justo en los dias del cambio, y diciendo que todo fue bien. Por eso
 * este banco compara contra una lista de fechas de cambio calculadas aparte
 * (tools/cambios_horarios.py, que las saca de la regla europea con el
 * calendario de Python) y ademas barre TODOS los dias de 2000 a 2099
 * comprobando el dia de la semana contra la formula de Zeller, que es una
 * implementacion distinta de la de Sakamoto que usa el firmware.
 */
#include <stdio.h>
#include <stdint.h>
#include "hora_comun.h"
#include "husos.h"
#include "cambios_generado.h"

static int fallos = 0;

static void mal(const char *que, int a, int b)
{
    printf("    FALLO %s: %d != %d\n", que, a, b);
    fallos++;
}

/* Zeller congruence: otra formula, a proposito, para que un error de
 * transcripcion en Sakamoto no se copie aqui tal cual. Devuelve 1=lunes. */
static int zeller(int d, int m, int y)
{
    int k, j, h;
    if (m < 3) { m += 12; y -= 1; }
    k = y % 100;
    j = y / 100;
    h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    /* h: 0=sabado, 1=domingo, 2=lunes ... */
    return ((h + 5) % 7) + 1;
}

static int bisiesto(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static int dias_mes(int m, int y)
{
    static const int t[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && bisiesto(y)) { return 29; }
    return t[m];
}

int main(void)
{
    int y, m, d, n = 0;

    printf("calendario compartido\n");

    /* 1. Dia de la semana: todos los dias de 2000 a 2099. */
    for (y = 2000; y <= 2099; y++) {
        for (m = 1; m <= 12; m++) {
            for (d = 1; d <= dias_mes(m, y); d++) {
                int a = hora_dia_semana((uint8_t)d, (uint8_t)m, (uint8_t)(y - 2000));
                int b = zeller(d, m, y);
                n++;
                if (a != b) {
                    printf("    FALLO dia de semana %04d-%02d-%02d: %d != %d\n",
                           y, m, d, a, b);
                    fallos++;
                    if (fallos > 5) { return 1; }
                }
            }
        }
    }
    printf("  dia de la semana: %d dias de 2000 a 2099, todos iguales que Zeller\n", n);

    /* 2. Bisiestos y dias del mes. */
    for (y = 2000; y <= 2099; y++) {
        if ((int)hora_bisiesto((uint8_t)(y - 2000)) != bisiesto(y)) {
            mal("bisiesto", y, bisiesto(y));
        }
        for (m = 1; m <= 12; m++) {
            if ((int)hora_dias_del_mes((uint8_t)m, (uint8_t)(y - 2000)) != dias_mes(m, y)) {
                mal("dias del mes", m, dias_mes(m, y));
            }
        }
    }
    printf("  bisiestos y dias por mes: 100 años correctos\n");

    /* 3. Dia del año -> fecha, ida y vuelta, todos los dias de 100 años. */
    n = 0;
    for (y = 2000; y <= 2099; y++) {
        int doy = 0;
        for (m = 1; m <= 12; m++) {
            for (d = 1; d <= dias_mes(m, y); d++) {
                uint8_t dd = 0, mm = 0;
                doy++;
                if (!hora_dia_del_anno_a_fecha((uint16_t)doy, (uint8_t)(y - 2000), &dd, &mm)
                    || dd != (uint8_t)d || mm != (uint8_t)m) {
                    printf("    FALLO doy %d de %d -> %02u/%02u, esperaba %02d/%02d\n",
                           doy, y, dd, mm, d, m);
                    fallos++;
                    if (fallos > 5) { return 1; }
                }
                n++;
            }
        }
        /* El ordinal que no existe tiene que ser rechazado, no inventado. */
        {
            uint8_t dd = 0, mm = 0;
            if (hora_dia_del_anno_a_fecha((uint16_t)(doy + 1), (uint8_t)(y - 2000), &dd, &mm)) {
                mal("doy imposible aceptado", doy + 1, 0);
            }
        }
    }
    printf("  dia del año -> fecha: %d dias correctos, y el que no existe se rechaza\n", n);

    /*
     * 4. Horario de verano, contra zoneinfo Europe/Madrid (sim/cambios_generado.h,
     *    que genera tools/gen_cambios.py). Marzo y octubre enteros hora a hora
     *    de 2000 a 2099, mas muestras del resto del año.
     *
     *    *** 10/10/2026: ESTO MEDIA hora_verano_espanna(), QUE YA NO EXISTE. ***
     *
     *    Al salir España del firmware, esa funcion se quedo sin llamadas y
     *    ademas era UNA SEGUNDA IMPLEMENTACION de la regla europea: la otra es
     *    la fila HUSO_DST_EU de husos.c. Dos sitios que contestan la misma
     *    pregunta es exactamente lo que la cabecera de hora_comun.h dice que
     *    este proyecto ya ha pagado varias veces, asi que se borro y este
     *    banco pasa a medir la que queda. Las muestras son las mismas, y por
     *    eso esta comprobacion no ha perdido nada: lo que cambia es a quien
     *    se le pregunta.
     *
     *    El 60 es el huso de invierno de Madrid, que la regla europea no usa
     *    -sus horas ya son UTC- pero husos_verano() pide igual porque tres de
     *    las cinco reglas si lo necesitan.
     */
    {
        int k, malos = 0;
        for (k = 0; k < N_CAMBIOS; k++) {
            const cambio_t *c = &k_cambios[k];
            int v = husos_verano(HUSO_DST_EU, 60, c->d, c->m, c->a, c->h);
            if (v != (int)c->v) {
                if (malos < 5) {
                    printf("    FALLO verano %02u/%02u/%02u %02u:00 UTC -> %d, zoneinfo dice %u\n",
                           c->d, c->m, c->a, c->h, v, c->v);
                }
                malos++;
                fallos++;
            }
        }
        printf("  horario de verano: %d instantes UTC contra zoneinfo, %d discrepancias\n",
               N_CAMBIOS, malos);
    }

    /* 5. Suma de horas: arrastre de dia, mes y año en los dos sentidos. */
    {
        struct { int8_t s; uint8_t h, d, m, a; uint8_t eh, ed, em, ea; } k_casos[] = {
            {  1U, 23U, 31U, 12U, 26U,  0U,  1U,  1U, 27U },  /* fin de año */
            { -1,   0U,  1U,  1U, 27U, 23U, 31U, 12U, 26U },  /* y al reves */
            {  2U, 23U, 28U,  2U, 24U,  1U, 29U,  2U, 24U },  /* 29 de febrero bisiesto */
            {  2U, 23U, 28U,  2U, 26U,  1U,  1U,  3U, 26U },  /* y sin bisiesto */
            { -3,   1U,  1U,  3U, 24U, 22U, 29U,  2U, 24U },  /* hacia atras a un 29 */
            {  1U, 12U, 15U,  6U, 26U, 13U, 15U,  6U, 26U },  /* sin arrastre */
        };
        unsigned k;
        for (k = 0; k < sizeof(k_casos) / sizeof(k_casos[0]); k++) {
            uint8_t h = k_casos[k].h, d = k_casos[k].d, m = k_casos[k].m;
            uint8_t a = k_casos[k].a, ds = 0U;
            hora_suma_horas(k_casos[k].s, &h, &d, &m, &a, &ds);
            if (h != k_casos[k].eh || d != k_casos[k].ed
                || m != k_casos[k].em || a != k_casos[k].ea) {
                printf("    FALLO suma %+d a %02u/%02u/%02u %02u:00 -> %02u/%02u/%02u %02u:00,"
                       " esperaba %02u/%02u/%02u %02u:00\n",
                       k_casos[k].s, k_casos[k].d, k_casos[k].m, k_casos[k].a, k_casos[k].h,
                       d, m, a, h, k_casos[k].ed, k_casos[k].em, k_casos[k].ea, k_casos[k].eh);
                fallos++;
            }
            if (ds != hora_dia_semana(d, m, a)) {
                mal("dia de semana tras sumar", ds, hora_dia_semana(d, m, a));
            }
        }
        printf("  suma de horas: %u casos de arrastre (fin de año, 29 de febrero, atras)\n",
               (unsigned)(sizeof(k_casos) / sizeof(k_casos[0])));
    }

    if (fallos == 0) { printf("  TODO CORRECTO (0 fallos)\n"); }
    else             { printf("  %d FALLOS\n", fallos); }
    return fallos ? 1 : 0;
}

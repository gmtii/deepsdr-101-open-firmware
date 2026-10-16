/*
 * EL CALENDARIO DEL RTC, CONTRA UNA REFERENCIA QUE NO ES LA NUESTRA.
 *
 * Compila ../User/rtc_cal.c de verdad, no una copia.
 *
 * POR QUE EXISTE - 13/10/2026.
 *
 * rtc_hw.c se reescribio entero ese dia. Toca registros del GD32, asi
 * que aqui no se puede compilar y no hay banco que lo mire: lo unico
 * que se puede comprobar es la aritmetica, y por eso la aritmetica se
 * saco a un fichero aparte. Si se hubiera quedado dentro de rtc_hw.c,
 * un error en los bisiestos no se habria visto hasta que la radio diera
 * el 1 de marzo de 2028.
 *
 * LA REFERENCIA NO ES CODIGO NUESTRO. Las fechas de abajo salen de la
 * tabla de dias del calendario gregoriano y de dias conocidos y
 * comprobables (el 1/1/1970 fue jueves; el 1/1/2000, sabado). Comparar
 * nuestra cuenta contra nuestra cuenta no demuestra nada.
 */
#include <stdio.h>
#include <string.h>
#include "rtc_cal.h"

static int fallos = 0;

static void ok(const char *que, long visto, long espera)
{
    int bien = (visto == espera);
    if (!bien) { fallos++; }
    printf("  %-46s %10ld  %s\n", que, visto, bien ? "bien" : "MAL");
    if (!bien) { printf("  %-46s %10ld  <- se esperaba\n", "", espera); }
}

int main(void)
{
    int i;

    printf("\nBCD, ida y vuelta\n");
    {
        int malos = 0, idos = 0;
        for (i = 0; i <= 99; i++) {
            uint8_t b = rtc_cal_dec_a_bcd((uint8_t)i);
            if (rtc_cal_bcd_a_dec(b) != (uint8_t)i) { malos++; }
            idos++;
        }
        ok("los 100 valores vuelven iguales", malos, 0);
        ok("0x59 son cincuenta y nueve, no ochenta y nueve",
           rtc_cal_bcd_a_dec(0x59U), 59);
        /* Y UN NIBBLE IMPOSIBLE NO SE INVENTA UN NUMERO. 0xFF es lo que
         * da un registro sin inicializar; si se tradujera a 165, eso
         * viaja hasta la pantalla con cara de hora. */
        ok("0xFF no es 165, es 'no se'", rtc_cal_bcd_a_dec(0xFFU), 0);
        ok("0x0A tampoco", rtc_cal_bcd_a_dec(0x0AU), 0);
    }

    printf("\nDias desde 1970\n");
    ok("1970-01-01 es el dia cero",   rtc_cal_dias_desde_1970(1970,1,1), 0);
    ok("1970-01-02 es el uno",        rtc_cal_dias_desde_1970(1970,1,2), 1);
    ok("1971-01-01 son 365",          rtc_cal_dias_desde_1970(1971,1,1), 365);
    ok("1973-01-01 son 1096 (72 fue bisiesto)",
       rtc_cal_dias_desde_1970(1973,1,1), 1096);
    ok("2000-01-01 son 10957",        rtc_cal_dias_desde_1970(2000,1,1), 10957);
    ok("2026-01-01 son 20454",        rtc_cal_dias_desde_1970(2026,1,1), 20454);

    printf("\nBisiestos, que es donde se rompen estas cuentas\n");
    /* 2000 SI es bisiesto (divisible por 400) y 1900 NO lo era
     * (divisible por 100 pero no por 400). Es la regla que casi todo
     * el mundo escribe mal la primera vez. */
    ok("29/02/2000 existe y el 1/3 va justo detras",
       rtc_cal_dias_desde_1970(2000,3,1) - rtc_cal_dias_desde_1970(2000,2,28), 2);
    ok("en 2026, que no es bisiesto, van seguidos",
       rtc_cal_dias_desde_1970(2026,3,1) - rtc_cal_dias_desde_1970(2026,2,28), 1);
    ok("2100 no sera bisiesto",
       rtc_cal_dias_desde_1970(2100,3,1) - rtc_cal_dias_desde_1970(2100,2,28), 1);
    ok("2024 lo fue",
       rtc_cal_dias_desde_1970(2024,3,1) - rtc_cal_dias_desde_1970(2024,2,28), 2);

    printf("\nDias de la semana, contra dias conocidos\n");
    ok("1970-01-01 fue jueves (4)",   rtc_cal_dia_semana(1970,1,1), 4);
    ok("2000-01-01 fue sabado (6)",   rtc_cal_dia_semana(2000,1,1), 6);
    ok("2026-10-13 es martes (2)",    rtc_cal_dia_semana(2026,10,13), 2);
    ok("2024-02-29 fue jueves (4)",   rtc_cal_dia_semana(2024,2,29), 4);
    {
        /* Y QUE NO SE SALGA DEL 1..7 NUNCA. Un cero o un ocho aqui
         * son un dia de la semana que el RTC no sabe guardar. */
        int fuera = 0;
        uint16_t y; uint8_t m, d;
        for (y = 2000; y <= 2099; y++)
          for (m = 1; m <= 12; m++)
            for (d = 1; d <= 28; d++) {
                uint8_t s = rtc_cal_dia_semana(y, m, d);
                if (s < 1U || s > 7U) { fuera++; }
            }
        ok("33.600 fechas, ninguna fuera de 1..7", fuera, 0);
    }

    printf("\nSegundos de epoca\n");
    ok("1970-01-01 00:00:00 es cero",
       (long)rtc_cal_epoca(1970,1,1,0,0,0), 0);
    ok("1970-01-01 00:00:01 es uno",
       (long)rtc_cal_epoca(1970,1,1,0,0,1), 1);
    ok("2000-01-01 00:00:00 son 946684800",
       (long)rtc_cal_epoca(2000,1,1,0,0,0), 946684800L);
    ok("2026-10-13 12:00:00 son 1.791.892.800",
       (long)rtc_cal_epoca(2026,10,13,12,0,0),
       (long)(20454L + 285L) * 86400L + 43200L);
    {
        /* MONOTONO SEGUNDO A SEGUNDO durante un dia entero que cruza
         * un cambio de mes. Un calendario que retrocede un segundo
         * hace que "hace N segundos" salga enorme, y eso es lo que
         * lee la pantalla. */
        int atras = 0;
        uint32_t ant = rtc_cal_epoca(2026,2,28,23,59,0);
        uint8_t h, mi, s;
        for (h = 0; h < 24; h++)
          for (mi = 0; mi < 60; mi++)
            for (s = 0; s < 60; s++) {
                uint32_t e = rtc_cal_epoca(2026,3,1,h,mi,s);
                if (e <= ant) { atras++; }
                ant = e;
            }
        ok("86.400 segundos seguidos, ninguno hacia atras", atras, 0);
    }

    printf("\nFechas imposibles\n");
    ok("31 de febrero: no",            rtc_cal_valida(2026,2,31,0,0,0), 0);
    ok("29/02 en año no bisiesto: no", rtc_cal_valida(2026,2,29,0,0,0), 0);
    ok("29/02 en bisiesto: si",        rtc_cal_valida(2024,2,29,0,0,0), 1);
    /*
     * LOS DOS SIGLOS, Y NO ESTABAN - 13/10/2026.
     *
     * Este banco ya daba verde con la regla del 400 ROTA a proposito, y
     * eso es un banco que no prueba lo que dice. La razon: la cuenta de
     * dias calcula los bisiestos por su cuenta -yoe/4 - yoe/100 - y no
     * pasa por bisiesto(), asi que la unica puerta a esa funcion es
     * rtc_cal_valida()... y las dos fechas que habia, 2024 y 2026, se
     * deciden con el "% 4" a secas. Ninguna llegaba al "% 100" ni al
     * "% 400".
     *
     * 2000 fue bisiesto por el 400 y 2100 no lo sera por el 100. Son
     * las dos unicas fechas que distinguen la regla completa de la
     * regla a medias.
     */
    ok("29/02/2000 si (divisible por 400)",
       rtc_cal_valida(2000,2,29,0,0,0), 1);
    /*
     * OJO CON ESTA: pasa, pero NO por la regla del 100.
     *
     * Se escribio creyendo que ejercia el "% 100" de bisiesto(), y no
     * lo hace: rtc_cal_valida() rechaza 2100 antes de mirar el mes,
     * porque el RTC guarda el año en dos cifras BCD y su rango es
     * 2000..2099. O sea que daba verde por el motivo equivocado, que
     * es peor que dar rojo.
     *
     * Se queda, pero diciendo lo que mide de verdad. La regla completa
     * la cubre el bloque de dias, mas arriba, que llega a 2100 porque
     * dias_desde_1970 no tiene ese tope.
     */
    ok("2100 ni se mira: fuera del rango del RTC",
       rtc_cal_valida(2100,2,29,0,0,0), 0);
    ok("1999 tampoco, por abajo",
       rtc_cal_valida(1999,6,15,0,0,0), 0);
    ok("31 de abril: no",              rtc_cal_valida(2026,4,31,0,0,0), 0);
    ok("mes 13: no",                   rtc_cal_valida(2026,13,1,0,0,0), 0);
    ok("dia 0: no",                    rtc_cal_valida(2026,1,0,0,0,0), 0);
    ok("las 24:00: no",                rtc_cal_valida(2026,1,1,24,0,0), 0);
    /* EL SEGUNDO 60 SI SE ADMITE: es el intercalar, y una emisora de
     * tiempo lo manda de verdad una vez cada varios años. Rechazarlo
     * seria tirar justo la trama en la que el patron dice algo. */
    ok("el segundo 60 (intercalar): si",
       rtc_cal_valida(2026,12,31,23,59,60), 1);
    ok("el 61 ya no",                  rtc_cal_valida(2026,12,31,23,59,61), 0);

    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}

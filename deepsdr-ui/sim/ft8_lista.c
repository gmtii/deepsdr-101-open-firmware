/*
 * ======================================================================
 * EL FILTRO DE CQ DE LA LISTA DE FT8 - 13/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Son veinte lineas, y por eso mismo hay banco: las veinte lineas del
 * toque del mapa de ayer tambien eran veinte, estaban en main.c -que el
 * simulador no compila- y la radio se quedo sin responder al toque. Lo
 * que se mide:
 *
 *   1) QUE ES UN CQ. El mensaje es el quinto campo, no el principio del
 *      renglon, y "CQDX..." como indicativo NO es un CQ.
 *
 *   2) QUE SE MIRAN TODOS LOS GUARDADOS Y NO LOS QUE CABEN. Esta es la
 *      unica trampa de verdad: con dieciseis mensajes de los que solo
 *      tres son CQ, y quince renglones de panel, recortar primero y
 *      filtrar despues pierde las CQ que esten por debajo del corte. Un
 *      filtro que busca en media lista no filtra.
 *
 *   3) QUE EL ORDEN ES DEL MAS VIEJO AL MAS NUEVO, que es como pinta el
 *      panel, y que si pasan mas de los que caben se quedan LOS NUEVOS.
 */
#include "ft8_lista.h"
#include <stdio.h>
#include <string.h>

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-60s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* La lista, con 0 = el mas nuevo, igual que ft8_modo.c. */
#define LARGO 16
static char g_lin[LARGO][96];
static uint8_t g_n;

static const char *linea(uint8_t i)
{
    return (i < g_n) ? g_lin[i] : "";
}

/* Mete un renglon COMO EL MAS NUEVO, empujando a los demas: es lo que
 * hace linea_nueva() en ft8_modo.c. */
static void mete(const char *msg)
{
    int i;

    for (i = LARGO - 1; i > 0; i--) {
        memcpy(g_lin[i], g_lin[i - 1], sizeof g_lin[0]);
    }
    snprintf(g_lin[0], sizeof g_lin[0], "12:30\t1200\t-08\t+0,1\t%s\t\t", msg);
    if (g_n < (uint8_t)LARGO) { g_n++; }
}

static void vacia(void) { g_n = 0U; memset(g_lin, 0, sizeof g_lin); }

int main(void)
{
    uint8_t idx[LARGO];
    uint8_t n;

    printf("\n== EL FILTRO DE CQ DE LA LISTA DE FT8 ==\n");

    /* ---------------------------------------------------------------- */
    printf("\n  1) que cuenta como CQ\n\n");
    {
        static const struct { const char *msg; int cq; } k[] = {
            { "CQ EA1ABC IN80",     1 },
            { "CQ DX JA5AFW PM64",  1 },
            { "CQ",                 1 },
            { "EA1ABC JA5AFW -10",  0 },
            { "JA5AFW EA1ABC RR73", 0 },
            { "CQDX1AA IN80",       0 },   /* un indicativo que empieza por CQ */
            { "TEST CQ EA1ABC",     0 }    /* CQ, pero no al principio */
        };
        unsigned i;
        int bien = 1;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            char l[96];
            int r;

            snprintf(l, sizeof l, "12:30\t1200\t-08\t+0,1\t%s\t\t", k[i].msg);
            r = ft8_lista_es_cq(l) ? 1 : 0;
            printf("     %-22s %s\n", k[i].msg, r ? "es CQ" : "no");
            if (r != k[i].cq) { bien = 0; }
        }
        /* Y el renglon partido: sin los cuatro tabuladores no hay mensaje. */
        if (ft8_lista_es_cq("12:30\t1200\tCQ EA1ABC IN80")) { bien = 0; }
        if (ft8_lista_es_cq(0)) { bien = 0; }
        exige(bien, "el CQ se busca en el quinto campo y pide separador detras");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2) sin filtro: los que caben, los mas nuevos, de viejo a nuevo\n\n");
    {
        unsigned i;

        vacia();
        for (i = 0; i < 16; i++) {
            char m[32];
            snprintf(m, sizeof m, "EA%uAA EB%uBB -0%u", i, i, i % 10);
            mete(m);
        }
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, idx);
        printf("     16 guardados, 15 renglones -> %u, del %u al %u\n",
               (unsigned)n, (unsigned)idx[0], (unsigned)idx[n - 1U]);
        exige(n == 15U, "salen quince");
        exige(idx[0] == 14U, "empieza por el segundo mas viejo (el mas viejo no cabe)");
        exige(idx[n - 1U] == 0U, "y acaba en el mas nuevo");
        {
            int ordena = 1;
            for (i = 1; i < n; i++) { if (idx[i] >= idx[i - 1]) { ordena = 0; } }
            exige(ordena, "y el orden es de viejo a nuevo");
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3) LA TRAMPA: las CQ que estan por debajo del corte\n\n");
    {
        unsigned i;

        /*
         * Dieciseis mensajes y solo TRES CQ, puestas a proposito entre las
         * mas VIEJAS: son las que se caen si alguien recorta a quince
         * renglones y despues filtra.
         */
        vacia();
        mete("CQ EA1AAA IN80");            /* indice 15 al final */
        mete("CQ EA2BBB IN81");
        mete("CQ EA3CCC IN82");
        for (i = 0; i < 13; i++) {
            char m[32];
            snprintf(m, sizeof m, "DL%uXX F%uYY R-12", i, i);
            mete(m);
        }
        n = ft8_lista_escoge(linea, g_n, 1U, 15U, idx);
        printf("     16 guardados, 3 CQ y las tres entre las mas viejas -> %u\n",
               (unsigned)n);
        exige(n == 3U, "salen las tres, no las que hubiera en los quince de arriba");
        {
            int bien = 1;
            for (i = 0; i < n; i++) {
                if (!ft8_lista_es_cq(linea(idx[i]))) { bien = 0; }
            }
            exige(bien, "y las tres son CQ de verdad");
        }
        exige(n == 3U && idx[0] == 15U && idx[2] == 13U,
              "en orden de viejo a nuevo: 15, 14, 13");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4) mas CQ de las que caben: se quedan las nuevas\n\n");
    {
        unsigned i;

        vacia();
        for (i = 0; i < 16; i++) {
            char m[32];
            snprintf(m, sizeof m, "CQ EA%uAA IN8%u", i, i % 10);
            mete(m);
        }
        n = ft8_lista_escoge(linea, g_n, 1U, 5U, idx);
        printf("     16 CQ en 5 renglones -> %u, del %u al %u\n",
               (unsigned)n, (unsigned)idx[0], (unsigned)idx[n - 1U]);
        exige(n == 5U, "salen cinco");
        exige(idx[0] == 4U && idx[n - 1U] == 0U,
              "y son las CINCO MAS NUEVAS, no las cinco primeras que se vieron");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5) los casos tontos\n\n");
    {
        vacia();
        exige(ft8_lista_escoge(linea, 0U, 0U, 15U, idx) == 0U,
              "lista vacia: cero renglones");
        mete("DL1XX F1YY R-12");
        exige(ft8_lista_escoge(linea, g_n, 1U, 15U, idx) == 0U,
              "ninguna CQ con el filtro puesto: cero, y no la ultima por si acaso");
        exige(ft8_lista_escoge(0, 4U, 0U, 15U, idx) == 0U, "sin lista, cero");
        exige(ft8_lista_escoge(linea, g_n, 0U, 0U, idx) == 0U, "sin renglones, cero");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

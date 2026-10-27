/*
 * ======================================================================
 * EL FILTRO DE CQ DE LA LISTA DE FT8 - 13/10/2026
 * ======================================================================
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
 *
 *   4) EL SCROLL - 23/10/2026. Lo que pidio el usuario tiene dos mitades
 *      y la segunda es la que se rompe sola: pegado al final la ventana
 *      sigue a lo nuevo, pero SUBIDA tiene que quedarse sobre los mismos
 *      mensajes mientras entran otros por abajo. Eso no sale gratis: si
 *      `subidos` no crece con la lista, cada mensaje nuevo te desplaza lo
 *      que estabas leyendo un renglon. Aqui se mide contando los que
 *      pasan antes y despues, que es lo que hace main.c.
 *
 *      Y el recorte: subir mas de lo que hay tiene que dejar la ventana
 *      en el renglon mas viejo, no en blanco por encima del principio.
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
    uint8_t sub = 0U;   /* el scroll: 0 es pegado a lo nuevo */

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
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, &sub, idx);
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
        n = ft8_lista_escoge(linea, g_n, 1U, 15U, &sub, idx);
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
        n = ft8_lista_escoge(linea, g_n, 1U, 5U, &sub, idx);
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
        exige(ft8_lista_escoge(linea, 0U, 0U, 15U, &sub, idx) == 0U,
              "lista vacia: cero renglones");
        mete("DL1XX F1YY R-12");
        exige(ft8_lista_escoge(linea, g_n, 1U, 15U, &sub, idx) == 0U,
              "ninguna CQ con el filtro puesto: cero, y no la ultima por si acaso");
        exige(ft8_lista_escoge(0, 4U, 0U, 15U, &sub, idx) == 0U, "sin lista, cero");
        exige(ft8_lista_escoge(linea, g_n, 0U, 0U, &sub, idx) == 0U, "sin renglones, cero");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  6) el scroll: subir enseña lo viejo, y se recorta\n\n");
    {
        unsigned i;

        vacia();
        for (i = 0; i < 16; i++) {
            char m[32];
            snprintf(m, sizeof m, "EA%uAA EB%uBB -0%u", i, i, i % 10);
            mete(m);
        }

        /* Pegado al final: el mas viejo (indice 15) NO cabe. */
        sub = 0U;
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, &sub, idx);
        exige(n == 15U && idx[0] == 14U, "pegado al final, el mas viejo se queda fuera");

        /* Un renglon arriba: entra el mas viejo y se cae el mas nuevo. */
        sub = 1U;
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, &sub, idx);
        printf("     subido 1 -> del %u al %u\n", (unsigned)idx[0], (unsigned)idx[n - 1U]);
        exige(n == 15U && idx[0] == 15U, "subido uno, entra el mas viejo");
        exige(idx[n - 1U] == 1U, "y se cae el mas nuevo");
        exige(sub == 1U, "y uno era subible, asi que no se recorta");

        /* Mas de lo que hay: se queda en el tope, no en blanco. */
        sub = 200U;
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, &sub, idx);
        exige(sub == 1U, "subir doscientos con uno por encima se recorta a uno");
        exige(n == 15U && idx[0] == 15U, "y la ventana queda en el mas viejo");

        /* Lista mas corta que el panel: no hay nada por encima. */
        vacia();
        mete("EA1AA EB1BB -01");
        sub = 5U;
        n = ft8_lista_escoge(linea, g_n, 0U, 15U, &sub, idx);
        exige(n == 1U && sub == 0U, "con un solo renglon no hay donde subir");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  7) SUBIDA, LA VENTANA SE QUEDA SOBRE LOS MISMOS MENSAJES\n\n");
    {
        unsigned i;
        uint8_t visto[LARGO], an;
        char guardado[96];

        /*
         * ESTE ES EL CASO QUE ROMPIO EL PRIMER INTENTO, y por eso esta
         * medido con el TEXTO del renglon y no con su indice: el indice
         * sigue valiendo 12 antes y despues, pero el mensaje que hay
         * dentro no es el mismo. Comparar indices daba verde con la
         * ventana resbalando.
         *
         * La lista va LLENA a proposito: es entonces cuando entran
         * mensajes sin que cambie el numero de renglones, y cuando contar
         * renglones no sirve de nada.
         */
        vacia();
        for (i = 0; i < LARGO; i++) {
            char m[32];
            snprintf(m, sizeof m, "EA%uAA EB%uBB -0%u", i, i, i % 10);
            mete(m);
        }
        sub = 3U;
        an = ft8_lista_escoge(linea, g_n, 0U, 10U, &sub, visto);
        exige(an == 10U, "diez renglones a la vista, subido tres");
        snprintf(guardado, sizeof guardado, "%s", linea(visto[0]));
        printf("     arriba del todo: %.24s...\n", guardado + 22);

        /* Entran DOS. La lista estaba llena: no cambia cuantos hay. */
        mete("NUEVO1 AA -01");
        mete("NUEVO2 BB -02");
        exige(ft8_lista_cuantos(linea, g_n, 0U) == (uint8_t)LARGO,
              "con la lista llena, contar renglones no detecta nada");

        /* Lo que SI detecta: dos mensajes nuevos, los dos se enseñan. */
        sub = (uint8_t)(sub + ft8_lista_nuevos(linea, 2U, 0U));
        n = ft8_lista_escoge(linea, g_n, 0U, 10U, &sub, visto);
        printf("     y ahora:         %.24s...\n", linea(visto[0]) + 22);
        exige(n == 10U, "siguen diez a la vista");
        exige(sub == 5U, "el dedo sube dos, que son los que han entrado");
        exige(strcmp(linea(visto[0]), guardado) == 0,
              "y arriba del todo esta EL MISMO MENSAJE que antes");

        /* ---------------------------------------------------------- */
        /* Y con el filtro de CQ puesto solo empujan los que se enseñan. */
        vacia();
        for (i = 0; i < 8; i++) {
            char m[32];
            snprintf(m, sizeof m, "CQ V%uAA IN8%u", i, i % 10);
            mete(m);
        }
        sub = 2U;
        an = ft8_lista_escoge(linea, g_n, 1U, 5U, &sub, visto);
        exige(an == 5U, "ocho CQ, cinco renglones, subido dos");
        snprintf(guardado, sizeof guardado, "%s", linea(visto[0]));

        /* Entran tres, pero solo UNO es CQ. */
        mete("DL1XX F1YY R-12");
        mete("CQ NUEVO IN99");
        mete("DL2XX F2YY R-13");
        sub = (uint8_t)(sub + ft8_lista_nuevos(linea, 3U, 1U));
        n = ft8_lista_escoge(linea, g_n, 1U, 5U, &sub, visto);
        printf("     filtro CQ: entran tres, uno es CQ, subido %u\n", (unsigned)sub);
        exige(sub == 3U, "solo empuja el que se enseña");
        exige(strcmp(linea(visto[0]), guardado) == 0,
              "y arriba del todo sigue el mismo");

        /* Pegado al final NO se toca nada: lo nuevo tiene que entrar. */
        sub = 0U;
        an = ft8_lista_escoge(linea, g_n, 1U, 5U, &sub, visto);
        snprintf(guardado, sizeof guardado, "%s", linea(visto[an - 1U]));
        exige(strstr(guardado, "CQ NUEVO") != 0,
              "pegado al final, abajo del todo esta lo ultimo que entro");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  8) el dedo, en renglones\n\n");
    {
        int16_t acum = 0;
        uint8_t s2 = 0U;

        exige(ft8_lista_arrastra(&acum, 5, 18U, &s2) == 0U && s2 == 0U,
              "cinco pixeles no son un renglon");
        exige(acum == 5, "pero se guardan para la proxima");
        exige(ft8_lista_arrastra(&acum, 14, 18U, &s2) == 1U && s2 == 1U,
              "cinco mas catorce si: el dedo abajo sube la lista");
        exige(acum == 1, "y sobra un pixel");

        /* Y hacia arriba vuelve. */
        acum = 0;
        exige(ft8_lista_arrastra(&acum, -18, 18U, &s2) == 1U && s2 == 0U,
              "el dedo arriba la baja");
        exige(ft8_lista_arrastra(&acum, -18, 18U, &s2) == 0U && s2 == 0U,
              "y por debajo de cero no hay nada");

        /* Un arrastre largo de golpe. */
        acum = 0; s2 = 0U;
        exige(ft8_lista_arrastra(&acum, 90, 18U, &s2) == 1U && s2 == 5U,
              "noventa pixeles son cinco renglones");

        /* Los casos tontos. */
        exige(ft8_lista_arrastra(0, 18, 18U, &s2) == 0U, "sin acumulador, cero");
        exige(ft8_lista_arrastra(&acum, 18, 0U, &s2) == 0U, "renglon de cero, cero");
        exige(ft8_lista_arrastra(&acum, 18, 18U, 0) == 0U, "sin scroll, cero");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/*
 * EL ANILLO DEL USB, MACHACADO.
 *
 * Este es el unico trozo del CDC que se puede medir sin la radio enchufada,
 * y da la casualidad de que es tambien donde este proyecto se rompe: once
 * averias de estado compartido entre una interrupcion y el bucle principal.
 *
 * Lo que se busca no es "¿mete y saca bien?" -eso sale a la primera-, sino
 * las tres formas en que un anillo falla de verdad:
 *
 *   1. EN LA VUELTA. Todo va bien durante los primeros 255 bytes y en el
 *      256 el indice da la vuelta. Por eso aqui no se prueba una vez: se
 *      dan miles de vueltas con tamaños que no son divisores del anillo.
 *   2. LLENO LEIDO COMO VACIO. Si no se desperdicia un hueco, cabeza ==
 *      cola significa las dos cosas y un anillo lleno se vacia solo.
 *   3. PISAR LO QUE NO SE HA LEIDO. Meter mas de lo que cabe tiene que
 *      meter lo que quepa y DECIRLO, no sobrescribir en silencio.
 *
 * Y al final, dos hilos de verdad peleandose: uno metiendo una secuencia
 * conocida y otro sacandola, comprobando que lo que sale es exactamente lo
 * que entro, en orden, sin perder ni repetir un byte.
 */
#include "usb_cdc_anillo.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* El del CAT mide 256; el del audio, 2048. El mismo codigo lleva los dos y
 * por eso el banco los prueba los dos. */
#define ANILLO_TAM 256U

static int fallos = 0;

static void ok(const char *que, int bien, const char *det)
{
    printf("  %-50s %s%s%s\n", que, bien ? "ok" : "MAL",
           det ? "   " : "", det ? det : "");
    if (!bien) { fallos++; }
}

static anillo_t a;
static uint8_t  s_sitio[ANILLO_TAM];

/* Antes el buffer vivia dentro de la estructura y anillo_reset() bastaba.
 * Ahora el sitio lo pone el que usa el anillo, asi que el banco tambien. */
static void pon(void)
{
    if (anillo_pon(&a, s_sitio, ANILLO_TAM) == 0U) {
        printf("  anillo_pon fallo con un tamaño que SI es potencia de dos\n");
        fallos++;
    }
}

static void basicos(void)
{
    uint8_t b[64], s[64];
    uint16_t n;

    printf("\n  lo elemental\n");
    pon();
    ok("recien puesto esta vacio", anillo_lleno(&a) == 0U, NULL);
    {
        char d[60];
        snprintf(d, sizeof d, "%u de %u", anillo_hueco(&a), ANILLO_TAM);
        ok("y el hueco es el tamaño menos uno",
           anillo_hueco(&a) == (ANILLO_TAM - 1U), d);
    }

    memset(b, 0xAB, sizeof b);
    n = anillo_mete(&a, b, 10U);
    ok("mete diez", n == 10U, NULL);
    ok("y dice que hay diez", anillo_lleno(&a) == 10U, NULL);
    n = anillo_saca(&a, s, sizeof s);
    ok("saca los diez y no mas", n == 10U, NULL);
    ok("y son los que entraron", memcmp(b, s, 10) == 0, NULL);
    ok("y vuelve a estar vacio", anillo_lleno(&a) == 0U, NULL);
    ok("sacar de uno vacio devuelve cero",
       anillo_saca(&a, s, sizeof s) == 0U, NULL);
}

static void lleno(void)
{
    uint8_t b[ANILLO_TAM + 16U], s[ANILLO_TAM + 16U];
    uint16_t n;
    char d[80];

    printf("\n  el borde: lleno\n");
    pon();
    memset(b, 0x5A, sizeof b);

    n = anillo_mete(&a, b, ANILLO_TAM - 1U);
    snprintf(d, sizeof d, "%u", n);
    ok("cabe el tamaño menos uno", n == (ANILLO_TAM - 1U), d);
    ok("y ya no cabe ni uno mas", anillo_mete(&a, b, 1U) == 0U, NULL);
    /*
     * ESTA ES LA QUE IMPORTA. Si no se guardara un hueco, aqui cabeza y
     * cola valdrian lo mismo y anillo_lleno() diria CERO con el anillo
     * lleno hasta arriba: el que saca creeria que no hay nada y los 255
     * bytes se quedarian ahi para siempre.
     */
    snprintf(d, sizeof d, "dice %u", anillo_lleno(&a));
    ok("un anillo lleno NO se lee como vacio",
       anillo_lleno(&a) == (ANILLO_TAM - 1U), d);

    n = anillo_mete(&a, b, 50U);
    ok("meter en uno lleno no mete nada", n == 0U, NULL);

    n = anillo_saca(&a, s, sizeof s);
    ok("y sale todo lo que habia", n == (ANILLO_TAM - 1U), NULL);
    ok("sin que se haya pisado nada",
       memcmp(b, s, ANILLO_TAM - 1U) == 0, NULL);

    /* Meter mas de lo que cabe: mete lo que quepa y lo dice. */
    pon();
    n = anillo_mete(&a, b, (uint16_t)(ANILLO_TAM + 10U));
    snprintf(d, sizeof d, "pedi %u y metio %u", ANILLO_TAM + 10U, n);
    ok("pedir mas de lo que cabe no pisa nada", n == (ANILLO_TAM - 1U), d);
}

static void vueltas(void)
{
    /*
     * LA VUELTA, QUE ES DONDE SE ROMPEN LOS ANILLOS.
     *
     * Trozos de 7 y de 13, que no dividen a 256: asi la vuelta cae cada vez
     * en un sitio distinto y se recorren todas las posiciones posibles del
     * corte, en vez de la misma una y otra vez.
     */
    uint8_t b[64], s[64];
    uint32_t puesto = 0UL, sacado = 0UL;
    int malos = 0, i;
    char d[80];

    printf("\n  la vuelta\n");
    pon();

    for (i = 0; i < 20000; i++) {
        uint16_t np = (i & 1) ? 7U : 13U;
        uint16_t ns = (i & 1) ? 13U : 7U;
        uint16_t k, n;

        for (k = 0U; k < np; k++) {
            b[k] = (uint8_t)((puesto + k) & 0xFFUL);
        }
        n = anillo_mete(&a, b, np);
        puesto += n;

        n = anillo_saca(&a, s, ns);
        for (k = 0U; k < n; k++) {
            if (s[k] != (uint8_t)((sacado + k) & 0xFFUL)) { malos++; }
        }
        sacado += n;
    }
    snprintf(d, sizeof d, "%lu bytes por el anillo", (unsigned long)sacado);
    ok("miles de vueltas con trozos de 7 y 13", malos == 0, d);
    ok("y no se perdio ni aparecio ninguno",
       (puesto - sacado) == (uint32_t)anillo_lleno(&a), NULL);
}

/* ---------------- los dos hilos ---------------- */
#define CUANTOS  2000000UL

static volatile int s_fin;
static uint32_t s_malos, s_sacados;

static void *consume(void *p)
{
    uint8_t s[97];

    (void)p;
    while ((s_fin == 0) || (anillo_lleno(&a) != 0U)) {
        uint16_t n = anillo_saca(&a, s, sizeof s);
        uint16_t k;

        for (k = 0U; k < n; k++) {
            if (s[k] != (uint8_t)((s_sacados + k) & 0xFFUL)) { s_malos++; }
        }
        s_sacados += n;
    }
    return NULL;
}

static void hilos(void)
{
    pthread_t h;
    uint8_t b[64];
    uint32_t puesto = 0UL;
    char d[90];

    printf("\n  y ahora los dos a la vez, de verdad\n");
    pon();
    s_fin = 0; s_malos = 0U; s_sacados = 0UL;

    if (pthread_create(&h, NULL, consume, NULL) != 0) {
        ok("lanzar el hilo que saca", 0, NULL);
        return;
    }
    while (puesto < CUANTOS) {
        uint16_t np = (uint16_t)(1U + (puesto % 61U));   /* tamaño variable */
        uint16_t k, n;

        if (np > sizeof b) { np = sizeof b; }
        for (k = 0U; k < np; k++) {
            b[k] = (uint8_t)((puesto + k) & 0xFFUL);
        }
        n = anillo_mete(&a, b, np);
        puesto += n;
    }
    s_fin = 1;
    pthread_join(h, NULL);

    snprintf(d, sizeof d, "%lu bytes, %lu llegaron",
             (unsigned long)puesto, (unsigned long)s_sacados);
    ok("dos millones de bytes de un hilo a otro, en orden",
       (s_malos == 0U) && (s_sacados == puesto), d);
}

static void tamanos(void)
{
    static uint8_t grande[2048];
    static anillo_t g;
    uint8_t b[300], s[300];
    uint32_t puesto = 0UL, sacado = 0UL;
    int i, malos = 0;
    char d[80];

    printf("\n  el otro tamaño: el anillo del audio\n");

    ok("un tamaño que no es potencia de dos se rechaza",
       anillo_pon(&g, grande, 1000U) == 0U, NULL);
    ok("y entonces no acepta nada", anillo_mete(&g, b, 10U) == 0U, NULL);
    ok("un buffer nulo tambien se rechaza",
       anillo_pon(&g, (uint8_t *)0, 2048U) == 0U, NULL);

    ok("2048 si vale", anillo_pon(&g, grande, 2048U) == 1U, NULL);
    snprintf(d, sizeof d, "%u", anillo_hueco(&g));
    ok("y el hueco es 2047", anillo_hueco(&g) == 2047U, d);

    /* Trozos de 192, que es lo que mide una trama de audio, contra 300 que
     * no divide a 2048: la vuelta cae cada vez en otro sitio. */
    for (i = 0; i < 20000; i++) {
        uint16_t k, n;

        for (k = 0U; k < 192U; k++) {
            b[k] = (uint8_t)((puesto + k) & 0xFFUL);
        }
        n = anillo_mete(&g, b, 192U);
        puesto += n;
        n = anillo_saca(&g, s, (uint16_t)((i & 1) ? 192U : 300U));
        for (k = 0U; k < n; k++) {
            if (s[k] != (uint8_t)((sacado + k) & 0xFFUL)) { malos++; }
        }
        sacado += n;
    }
    snprintf(d, sizeof d, "%lu bytes", (unsigned long)sacado);
    ok("tramas de audio dando vueltas en el de 2048", malos == 0, d);
}

int main(void)
{
    printf("\nEL ANILLO ENTRE LA INTERRUPCION DEL USB Y EL BUCLE\n");
    basicos();
    tamanos();
    lleno();
    vueltas();
    hilos();
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO",
           fallos);
    return fallos ? 1 : 0;
}

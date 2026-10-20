/*
 * Banco del NAVTEX (SITOR-B).
 *
 * Compila User/navtex.c DEL FIRMWARE, no una copia. Y fabrica la señal con
 * la MISMA tabla del alfabeto que usa el decodificador (navtex_car_a_cod()):
 * si el banco tuviera su propia tabla, lo unico que se estaria comprobando
 * es que dos copias coinciden, que no demuestra nada. Asi, un error en la
 * tabla no se cancela solo - se nota en el bit order, en el sincronismo de
 * caracter o en nada, pero no se esconde.
 *
 * LO QUE ESTE BANCO NO PUEDE COMPROBAR: que el orden de los 7 bits sea el
 * mismo que usa el mundo real. Transmisor y receptor son los dos de aqui, y
 * si los dos estuvieran del reves saldria igual de bien. Eso lo fija la
 * norma (el relleno "alfa" es 0001111 y la peticion de repeticion es
 * 1100110, que leidos con el primer bit arriba dan 0x0F y 0x66) y solo lo
 * confirma la antena.
 */
#include "navtex.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define FS      12000.0
#define BAUD    100.0
#define CENTRO  1700.0
#define SHIFT   170.0

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* ------------------------------------------------------------- generador */

/* El texto, a codigos CCIR 476, metiendo los cambios letras/cifras donde
 * hagan falta. Devuelve cuantos codigos ha puesto. */
static int texto_a_codigos(const char *t, uint8_t *out, int max)
{
    int n = 0, figuras = 0;

    while (*t && n < max - 2) {
        uint8_t c = navtex_car_a_cod(*t, figuras);
        if (c == 0xFF) {
            /* No esta en el modo actual: probar el otro y cambiar. */
            uint8_t c2 = navtex_car_a_cod(*t, !figuras);
            if (c2 == 0xFF) { t++; continue; }   /* no esta en el alfabeto */
            figuras = !figuras;
            out[n++] = figuras ? NAVTEX_COD_FIGS : NAVTEX_COD_LTRS;
            c = c2;
        }
        out[n++] = c;
        t++;
    }
    return n;
}

/*
 * La trama de SITOR-B: los caracteres van en las posiciones PARES y su copia
 * cinco posiciones despues, en las impares. Lo que queda sin llenar es
 * relleno. Delante van unas cuantas posiciones de fase (la alternancia de
 * peticion de repeticion y relleno que manda una emisora antes del mensaje).
 */
#define FASE_N 160  /* posiciones de fase (11 s, como una emisora de verdad); PAR */

static int trama(const uint8_t *cod, int n, uint8_t *out, int max)
{
    int total = FASE_N + 2 * n + 5;
    int i;

    if (total > max) { return 0; }
    for (i = 0; i < total; i++) { out[i] = NAVTEX_COD_ALFA; }
    for (i = 0; i < FASE_N; i++) {
        out[i] = (i & 1) ? NAVTEX_COD_ALFA : NAVTEX_COD_REP;
    }
    for (i = 0; i < n; i++) {
        out[FASE_N + 2 * i] = cod[i];          /* primera copia */
        out[FASE_N + 2 * i + 5] = cod[i];      /* la repeticion, 35 bits despues */
    }
    return total;
}

/* ------------------------------------------------------------- la señal */

typedef struct {
    double fase;       /* fase de la portadora de audio, continua */
    const uint8_t *tr;
    int n;
    long muestra;
    double ppm;
    unsigned long semilla;
    int bits_rotos;    /* cuantos bits se han invertido a proposito */
    int roto_cada;     /* 0 = ninguno */
} tx_t;

static double tono(tx_t *t)
{
    double bit_dur = FS / (BAUD * (1.0 + t->ppm * 1e-6));
    long ib = (long)(t->muestra / bit_dur);      /* que bit del flujo */
    int car = (int)(ib / 7);
    int pos = (int)(ib % 7);
    int bit, f;

    if (car >= t->n) { car = t->n - 1; }
    /* B1 PRIMERO, Y B1 ES EL BIT DE ABAJO. Hasta el 02/10/2026 esto mandaba
     * (6 - pos), o sea del reves, y el banco no se enteraba porque la tabla
     * con la que fabrica es la misma con la que lee: las dos estaban
     * giradas a la vez. Lo cogio una grabacion de verdad - ver el comentario
     * de vuelve7() en navtex.c. */
    bit = (t->tr[car] >> pos) & 1;

    /* Rafagas de error: cada roto_cada bits se invierte uno. Sirve para ver
     * si la diversidad temporal rescata lo que la primera copia perdio. */
    if (t->roto_cada && (ib % t->roto_cada) == 0 && ib > 0) {
        bit = !bit;
        t->bits_rotos++;
    }

    f = bit ? 1 : 0;
    t->fase += 2.0 * M_PI * (CENTRO + (f ? SHIFT * 0.5 : -SHIFT * 0.5)) / FS;
    if (t->fase > 2.0 * M_PI) { t->fase -= 2.0 * M_PI; }
    t->muestra++;
    return sin(t->fase);
}

static double ruido(tx_t *t)
{
    double a, b;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    a = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    t->semilla = t->semilla * 1103515245UL + 12345UL;
    b = (double)((t->semilla >> 16) & 0x7FFF) / 32767.0 - 0.5;
    return a + b;
}

/* Corre la trama entera y devuelve lo decodificado. */
static void corre(tx_t *t, double sigma, char *salida, int max)
{
    long total = (long)((double)t->n * 7.0 * FS / BAUD) + (long)FS;
    long k;
    float buf[NAVTEX_BLOCK_SAMPLES];
    uint32_t j = 0;
    int o = 0;

    for (k = 0; k < total; k++) {
        buf[j++] = (float)(tono(t) + sigma * ruido(t));
        if (j == NAVTEX_BLOCK_SAMPLES) {
            char c;
            navtex_process(buf, NAVTEX_BLOCK_SAMPLES);
            j = 0;
            while (navtex_get_char(&c)) {
                if (o < max - 1) { salida[o++] = c; }
            }
        }
    }
    salida[o] = '\0';
}

/* Cuantos caracteres de `esperado` aparecen, en orden, en `salida`. */
static int parecido(const char *esperado, const char *salida)
{
    int i = 0, j = 0;
    while (esperado[i] && salida[j]) {
        if (esperado[i] == salida[j]) { i++; }
        j++;
    }
    return i;
}

static const char k_msg[] =
    "ZCZC GA42 SECURITE NAVAREA III 0942 UTC. BUOY UNLIT POSITION 36 12N 005 21W. "
    "GALE WARNING IN FORCE UNTIL 1800 UTC. CANCEL THIS MESSAGE 251200 UTC. NNNN";

/*
 * Un WAV de 16 bits mono, tal y como lo deja ffmpeg en out/muestras. No se
 * mira la cabecera campo a campo a proposito: el Makefile es quien garantiza
 * que son 12 kHz mono, y aqui solo hacen falta las muestras.
 */
static uint32_t lee_wav(const char *ruta, float *dest, uint32_t max)
{
    FILE *f = fopen(ruta, "rb");
    unsigned char cab[44];
    uint32_t n = 0U;

    if (f == NULL) { return 0U; }
    if (fread(cab, 1U, 44U, f) != 44U) { fclose(f); return 0U; }
    while (n < max) {
        short v;
        if (fread(&v, 2U, 1U, f) != 1U) { break; }
        dest[n++] = (float)v / 32768.0f;
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t cod[512];
    static uint8_t tr[2048];
    static char salida[4096];
    int nc, nt;

    printf("banco del NAVTEX (SITOR-B)\n");

    /* ------------------------------------------------------------------ */
    printf("\n1. el alfabeto CCIR 476\n");
    {
        int i, validos = 0;
        for (i = 0; i < 128; i++) { if (navtex_codigo_valido((uint8_t)i)) { validos++; } }
        printf("    codigos con exactamente cuatro unos: %d de 128 (%.0f%%)\n",
               validos, 100.0 * validos / 128.0);
        comprueba(validos == 35, "son 35, que es lo que dice la norma");

        comprueba(navtex_codigo_valido(NAVTEX_COD_ALFA), "el relleno es un codigo valido");
        comprueba(navtex_codigo_valido(NAVTEX_COD_REP), "la peticion de repeticion tambien");
        comprueba(navtex_codigo_valido(NAVTEX_COD_LTRS), "y los dos cambios de modo");
        comprueba(navtex_codigo_valido(NAVTEX_COD_FIGS), "");

        /* Que la tabla y su inversa cuadren en los dos sentidos. */
        {
            int ok = 1;
            for (i = 0; i < 128; i++) {
                char c = navtex_cod_a_car((uint8_t)i, 0);
                if (c != 0 && navtex_car_a_cod(c, 0) != (uint8_t)i) { ok = 0; }
                if (c != 0 && !navtex_codigo_valido((uint8_t)i)) { ok = 0; }
            }
            comprueba(ok, "todo lo que imprime la tabla es un codigo valido y vuelve");
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n2. señal limpia\n");
    nc = texto_a_codigos(k_msg, cod, (int)sizeof cod);
    nt = trama(cod, nc, tr, (int)sizeof tr);
    printf("    %d caracteres -> %d codigos -> %d posiciones (%.1f s de emision)\n",
           (int)strlen(k_msg), nc, nt, nt * 7.0 / BAUD);
    comprueba(nt > 0, "la trama cabe");
    {
        tx_t t; navtex_info_t in;
        memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 12345UL;
        navtex_start((float)FS);
        corre(&t, 0.0, salida, (int)sizeof salida);
        navtex_info(&in);
        printf("    sale: \"%.100s%s\"\n", salida, strlen(salida) > 100 ? "..." : "");
        printf("    enganchado %u/%u  calidad %.3f  buenos %u  malos %u  cabecera \"%s\"\n",
               in.bit_sync, in.car_sync, (double)in.calidad,
               in.car_ok, in.car_mal, in.tiene_cab ? in.cab : "-");
        comprueba(in.bit_sync, "se engancha el reloj de bit");
        comprueba(in.car_sync, "y el sincronismo de caracter");
        comprueba(strstr(salida, "GALE WARNING IN FORCE") != NULL,
                  "el texto sale literal");
        comprueba(in.tiene_cab && strcmp(in.cab, "GA42") == 0,
                  "la cabecera ZCZC B1B2B3B4 sale bien");
        comprueba(in.mensajes == 1U, "y el mensaje se da por cerrado con NNNN");
        navtex_stop();
    }

    /* ------------------------------------------------------------------ */
    printf("\n3. con ruido\n");
    printf("    (la señal es un seno de amplitud 1 y el ruido va en multiplos de eso,\n"
           "     medido en TODA la banda de audio. El filtro adaptado integra un bit\n"
           "     entero, o sea 100 Hz de los 6000 que trae el ruido: por eso aguanta\n"
           "     mucho mas de lo que parece. El barrido llega hasta que se rompe,\n"
           "     porque un banco que no rompe nada no mide nada.)\n");
    {
        static const double k_sig[] = { 0.3, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
        unsigned i;
        int esperados = 0;
        {
            const char *p = k_msg;
            while (*p) { if (navtex_car_a_cod(*p, 0) != 0xFF || navtex_car_a_cod(*p, 1) != 0xFF) { esperados++; } p++; }
        }
        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            tx_t t; navtex_info_t in; int ac;
            memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 999UL + i;
            navtex_start((float)FS);
            corre(&t, k_sig[i], salida, (int)sizeof salida);
            navtex_info(&in);
            ac = parecido(k_msg, salida);
            printf("    ruido %4.1f:  %3d de %3d caracteres en orden (%3.0f%%)   "
                   "calidad %.2f   rescatados %u\n",
                   k_sig[i], ac, esperados, 100.0 * ac / esperados,
                   (double)in.calidad, in.rescatados);
            if (k_sig[i] <= 3.0) {
                comprueba(ac > esperados * 9 / 10,
                          "hasta tres veces la señal se lee entero");
            }
            navtex_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n4. error de velocidad del transmisor\n");
    {
        static const double k_ppm[] = { -500.0, -100.0, 0.0, 100.0, 500.0 };
        unsigned i;
        for (i = 0; i < sizeof k_ppm / sizeof k_ppm[0]; i++) {
            tx_t t; navtex_info_t in;
            memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 4242UL;
            t.ppm = k_ppm[i];
            navtex_start((float)FS);
            corre(&t, 0.1, salida, (int)sizeof salida);
            navtex_info(&in);
            {
                float ppm; float niv; uint8_t d;
                navtex_lazos_dbg(&ppm, &niv, &d);
                printf("    %+6.0f ppm:  cabecera \"%s\"  texto %s   "
                       "(el lazo mide %+6.0f ppm)\n",
                       k_ppm[i], in.tiene_cab ? in.cab : "-",
                       strstr(salida, "GALE WARNING") ? "entero" : "ROTO",
                       (double)ppm);
            }
            comprueba(strstr(salida, "GALE WARNING") != NULL,
                      "el lazo de reloj sigue al transmisor");
            navtex_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n5. lo que rescata la diversidad temporal\n");
    printf("    (se invierte un bit cada N: es el caso que la diversidad esta\n"
           "     puesta para arreglar, porque las dos copias van a 350 ms)\n");
    {
        static const int k_cada[] = { 0, 200, 100, 50, 30 };
        unsigned i;
        int esperados = (int)strlen(k_msg);
        for (i = 0; i < sizeof k_cada / sizeof k_cada[0]; i++) {
            tx_t t; navtex_info_t in; int ac;
            memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 7UL;
            t.roto_cada = k_cada[i];
            navtex_start((float)FS);
            corre(&t, 0.05, salida, (int)sizeof salida);
            navtex_info(&in);
            ac = parecido(k_msg, salida);
            printf("    un bit roto cada %3d (%4d bits): %3d de %3d caracteres, "
                   "rescatados por la copia %u\n",
                   k_cada[i], t.bits_rotos, ac, esperados, in.rescatados);
            navtex_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n6. polaridad invertida (la banda lateral contraria)\n");
    {
        tx_t t; navtex_info_t in;
        /* El transmisor manda igual; lo que se cambia es el receptor. Con la
         * polaridad del reves NO tiene que salir nada: si saliera, es que el
         * conmutador no hace nada. */
        memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 1UL;
        navtex_start((float)FS);
        navtex_set_invertido(1U);
        corre(&t, 0.05, salida, (int)sizeof salida);
        navtex_info(&in);
        printf("    del reves: \"%.40s\"\n", salida);
        comprueba(strstr(salida, "GALE WARNING") == NULL,
                  "del reves no sale el mensaje, como tiene que ser");
        navtex_stop();

        memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 1UL;
        navtex_start((float)FS);
        navtex_set_invertido(0U);
        corre(&t, 0.05, salida, (int)sizeof salida);
        comprueba(strstr(salida, "GALE WARNING") != NULL,
                  "y al ponerlo bien vuelve a salir");
        navtex_stop();
    }

    /* ------------------------------------------------------------------ */
    /*
     * 7. UNA GRABACION DE VERDAD.
     *
     * Todo lo de arriba fabrica la señal con navtex_car_a_cod(), o sea con la
     * misma tabla con la que luego la lee. Eso comprueba que el decodificador
     * es coherente CONSIGO MISMO, que no es poco, pero no comprueba que lo
     * sea con el aire - y de hecho no lo era: el orden de los bits del
     * caracter estaba girado y las dos tablas estaban giradas a la vez, asi
     * que estas seis pruebas pasaban igual. La unica forma de cogerlo es una
     * señal que no haya fabricado este fichero.
     *
     * La grabacion es una emision de Oostende Radio (OST) en SITOR-B, con los
     * tonos centrados en 2204 Hz. Empieza a mitad de emision - no trae la
     * señal de fase - que es ademas el caso que no enganchaba.
     */
    /* ------------------------------------------------------------------ */
    /*
     * 6b. UN CAMBIO A LETRAS PERDIDO NO PUEDE COSTAR EL MENSAJE ENTERO.
     *
     * No era ruido: eran las MISMAS letras impresas por la columna de las
     * cifras. Se habia perdido un cambio a letras y emite() no tenia forma
     * de volver - solo lo sacaba de ahi otro codigo de letras recibido
     * limpio, y hasta que llegue uno el mensaje entero sale en cifras.
     *
     * Aqui se fabrica justo eso: el mensaje de siempre, con un numero por
     * medio (asi hay un cambio a cifras y su vuelta a letras), y se rompen
     * las DOS copias del codigo que vuelve a letras. Sin arreglo, todo lo
     * que viene detras sale en cifras.
     */
    printf("\n6b. un cambio a letras perdido\n");
    {
        static const char k_msg2[] =
            "ZCZC AB12 TEST 0942 UTC.\rSEGUNDA LINEA EN LETRAS NNNN";
        tx_t t; navtex_info_t in;
        int i, roto = -1;

        nc = texto_a_codigos(k_msg2, cod, (int)sizeof cod);
        /* el ultimo cambio a LETRAS del mensaje */
        for (i = 0; i < nc; i++) {
            if (cod[i] == NAVTEX_COD_LTRS) { roto = i; }
        }
        nt = trama(cod, nc, tr, (int)sizeof tr);
        if (roto >= 0 && nt > 0) {
            /* las dos copias de ese codigo, a basura valida */
            tr[FASE_N + 2 * roto]     = NAVTEX_COD_ALFA;
            tr[FASE_N + 2 * roto + 5] = NAVTEX_COD_ALFA;
        }
        memset(&t, 0, sizeof t); t.tr = tr; t.n = nt; t.semilla = 7UL;
        navtex_start((float)FS);
        corre(&t, 0.0, salida, (int)sizeof salida);
        navtex_info(&in);
        printf("    sale: \"%.90s\"\n", salida);
        comprueba(strstr(salida, "SEGUNDA LINEA EN LETRAS") != NULL,
                  "tras el salto de linea vuelve a letras solo");
        navtex_stop();
    }

    {
        /*
         * Cada grabacion con su centro de tonos y con lo que TIENE que salir
         * de ella. Son dos a proposito: la de Oostende es limpia -bajada de
         * internet- y la de 8424 kHz la grabo el usuario con el movil delante
         * de la radio, con desvanecimiento y con el codec del movil encima.
         * La segunda es la que de verdad aprieta.
         */
        static const struct { const char *nom; float centro; const char *txt1;
                              const char *txt2; } k_grab[] = {
            { "sitorb_tfc",  2204.0f, "QTC LIST",   "FEC MODE" },
            { "navtex8424",  1108.0f, "HNOMENON",   "SYMBOYLIO" },
        };
        int a;

        printf("\n7. grabaciones de verdad\n");
        for (a = 1; a < argc; a++) {
            static float aud[4000000];
            uint32_t n, i;
            navtex_info_t in;
            int o = 0, g, cual = -1;
            char c;

            for (g = 0; g < (int)(sizeof k_grab / sizeof k_grab[0]); g++) {
                if (strstr(argv[a], k_grab[g].nom) != NULL) { cual = g; }
            }
            if (cual < 0) { continue; }

            n = lee_wav(argv[a], aud, 4000000U);
            printf("\n    %s  (tonos en %.0f Hz, el ajuste en %.0f)\n",
                   k_grab[cual].nom, (double)k_grab[cual].centro,
                   (double)NAVTEX_CENTRO_HZ);
            comprueba(n > 12000U * 5U, "la grabacion se lee y dura algo");
            if (n <= 12000U * 5U) { continue; }

            /*
             * SIN TOCAR EL CENTRO. Es lo que de verdad hay que probar: en la
             * radio nadie llama a navtex_set_centro_hz() -no se llama desde
             * ningun sitio- asi que siempre escucha en el de omision, 1700
             * Hz. Si la prueba le pusiera el centro bueno estaria probando
             * algo que no pasa nunca.
             */
            navtex_start((float)FS);
            navtex_set_invertido(0U);
            salida[0] = '\0';
            for (i = 0U; i + NAVTEX_BLOCK_SAMPLES <= n; i += NAVTEX_BLOCK_SAMPLES) {
                navtex_process(&aud[i], NAVTEX_BLOCK_SAMPLES);
                while (navtex_get_char(&c)) {
                    if (o < (int)sizeof salida - 1) { salida[o++] = c; }
                }
            }
            salida[o] = '\0';
            navtex_info(&in);
            printf("    sale: \"%.110s\"\n", salida);
            printf("    enganchado %u/%u  calidad %.2f  buenos %u  malos %u\n",
                   in.bit_sync, in.car_sync, (double)in.calidad,
                   in.car_ok, in.car_mal);
            comprueba(in.bit_sync && in.car_sync,
                      "engancha entrando a mitad, sin señal de fase");
            printf("    centro medido: %+d Hz sobre el ajuste (tocaba %+d)\n",
                   (int)in.desvio_hz,
                   (int)(k_grab[cual].centro - NAVTEX_CENTRO_HZ));
            {
                int err = (int)in.desvio_hz
                        - (int)(k_grab[cual].centro - NAVTEX_CENTRO_HZ);
                if (err < 0) { err = -err; }
                comprueba(err <= 60, "y lo encuentra solo, con menos de 60 Hz de error");
            }
            {
                char q[80];
                snprintf(q, sizeof q, "y sale \"%s\"", k_grab[cual].txt1);
                comprueba(strstr(salida, k_grab[cual].txt1) != NULL, q);
                snprintf(q, sizeof q, "y tambien \"%s\"", k_grab[cual].txt2);
                comprueba(strstr(salida, k_grab[cual].txt2) != NULL, q);
            }
            comprueba(in.car_mal * 10U <= in.car_ok,
                      "con menos de un 10% de caracteres rotos");
            navtex_stop();
        }

        /*
         * Y CON RUIDO SOLO NO PUEDE ENGANCHAR. El criterio nuevo es mas
         * blando que el que habia, asi que hay que demostrar que sigue
         * sabiendo decir que no.
         */
        {
            static float aud[12000 * 40];
            unsigned long sem = 99UL;
            uint32_t i;
            navtex_info_t in;
            for (i = 0U; i < (uint32_t)(sizeof aud / sizeof aud[0]); i++) {
                sem = sem * 1103515245UL + 12345UL;
                aud[i] = (float)((double)((sem >> 16) & 0x7FFF) / 32767.0 - 0.5);
            }
            navtex_start((float)FS);
            navtex_set_centro_hz(1700.0f);
            salida[0] = '\0';
            { int o2 = 0; char c2;
              for (i = 0U; i + NAVTEX_BLOCK_SAMPLES <= (uint32_t)(sizeof aud / sizeof aud[0]);
                   i += NAVTEX_BLOCK_SAMPLES) {
                navtex_process(&aud[i], NAVTEX_BLOCK_SAMPLES);
                while (navtex_get_char(&c2)) {
                    if (o2 < (int)sizeof salida - 1) { salida[o2++] = c2; }
                }
              }
              salida[o2] = '\0'; }
            navtex_info(&in);
            printf("\n    40 s de ruido: engancha %u, saca %u caracteres\n",
                   in.car_sync, (unsigned)strlen(salida));
            comprueba(!in.car_sync, "con ruido solo NO engancha");
            comprueba(strlen(salida) == 0U, "y no se inventa ni una letra");
            navtex_stop();
        }
    }
    if (0) {
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

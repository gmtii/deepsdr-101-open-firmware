/*
 * Banco del noise blanker (User/nb.c).
 *
 * Los umbrales de k_umbral[] NO se eligen a ojo: salen de aqui. Lo que se
 * mide es lo unico que importa de un blanker, y son DOS cosas a la vez:
 *
 *   1. cuanto ruido de pulsos quita        (si no quita, no sirve)
 *   2. cuanto daña la señal cuando NO hay pulsos  (si la daña, es peor que
 *                                                  no tenerlo)
 *
 * Un blanker demasiado agresivo saca un numero estupendo en (1) y arruina
 * (2), que es justo el fallo que no se ve escuchando un rato: la señal suena
 * "algo rara" y nadie sabe por que. Por eso las dos columnas van juntas.
 *
 * La señal de prueba es un tono complejo (una portadora), que es lo que ve
 * el blanker antes del filtro de canal. El ruido es gaussiano de fondo mas
 * pulsos cortos, que es el modelo de una valla electrica o el encendido de
 * un motor: muy altos, muy breves, repetidos.
 */
#include "nb.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define N      256U        /* muestras por bloque, como SDR_RX_BLOCK_SAMPLES */
#define FS     96000.0
#define TONO   5000.0      /* portadora a 5 kHz del centro */
static double g_amp_port = 2000.0;   /* amplitud de la portadora, ver los escenarios */
#define BLOQUES 400        /* ~1 s de audio */

static unsigned long s_rnd = 12345UL;
static double uni(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return (double)((s_rnd >> 16) & 0x7FFFUL) / 32767.0;
}
static double gauss(void)
{
    double u1 = uni(), u2 = uni();
    if (u1 < 1e-9) { u1 = 1e-9; }
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

/* Potencia del error respecto a la referencia limpia, en dB relativos a la
 * potencia de la señal. Menos es mejor. */
static double err_db(const float *i, const float *q,
                     const float *ri, const float *rq, unsigned n)
{
    double es = 0.0, ee = 0.0;
    unsigned k;
    for (k = 0; k < n; k++) {
        double di = (double)i[k] - (double)ri[k];
        double dq = (double)q[k] - (double)rq[k];
        ee += di * di + dq * dq;
        es += (double)ri[k] * (double)ri[k] + (double)rq[k] * (double)rq[k];
    }
    if (ee <= 0.0) { return -99.0; }
    return 10.0 * log10(ee / es);
}

/* Genera un bloque: portadora + ruido de fondo (+ pulsos si pulsos!=0).
 * Devuelve tambien la referencia LIMPIA (solo portadora) para medir daño. */
static void genera(unsigned bloque, int pulsos, double amp_pulso,
                   float *i, float *q, float *ri, float *rq)
{
    unsigned k;
    for (k = 0; k < N; k++) {
        double t = (double)(bloque * N + k) / FS;
        double s_i = g_amp_port * cos(6.283185307 * TONO * t);
        double s_q = g_amp_port * sin(6.283185307 * TONO * t);

        ri[k] = (float)s_i;
        rq[k] = (float)s_q;
        i[k]  = (float)(s_i + 60.0 * gauss());
        q[k]  = (float)(s_q + 60.0 * gauss());
    }
    if (!pulsos) { return; }
    /* Tres pulsos por bloque, de 2 muestras cada uno (~21 us a 96 kHz),
     * en sitios que cambian de bloque a bloque. */
    for (k = 0; k < 3U; k++) {
        unsigned pos = (unsigned)(uni() * (double)(N - 4U)) + 1U;
        unsigned j;
        for (j = pos; j < pos + 2U; j++) {
            i[j] += (float)(amp_pulso * (uni() > 0.5 ? 1.0 : -1.0));
            q[j] += (float)(amp_pulso * (uni() > 0.5 ? 1.0 : -1.0));
        }
    }
}

/*
 * Corre BLOQUES bloques.
 *
 * Devuelve el error contra la portadora LIMPIA, que es lo que mide si el
 * blanker sirve. Y por *daño deja aparte el error contra su PROPIA ENTRADA,
 * que es lo que mide si estropea. Esa segunda medida hay que hacerla asi y
 * no contra la portadora limpia: la primera version de este banco comparaba
 * el daño contra la portadora y le salia -27 dB incluso con el blanker
 * APAGADO, porque lo que estaba midiendo era el ruido gaussiano de fondo,
 * no el blanker. El unico daño que se le puede imputar al blanker es lo que
 * el blanker cambia.
 */
static double corre(uint8_t nivel, int pulsos, double amp,
                    double *pct_borrado, double *dano)
{
    float i[N], q[N], ri[N], rq[N], ei[N], eq[N];
    double acc = 0.0, acc_d = 0.0;
    unsigned long borradas = 0UL;
    int b, contados = 0;

    nb_reset();
    nb_set_nivel(nivel);
    s_rnd = 12345UL;

    for (b = 0; b < BLOQUES; b++) {
        genera((unsigned)b, pulsos, amp, i, q, ri, rq);
        memcpy(ei, i, sizeof ei);   /* la entrada tal cual, para el daño */
        memcpy(eq, q, sizeof eq);
        borradas += nb_process(i, q, N);
        /* Los diez primeros bloques no cuentan: el suelo aun se esta
         * asentando y medir ahi seria medir el arranque, no el regimen. */
        if (b >= 10) {
            acc   += err_db(i, q, ri, rq, N);
            acc_d += err_db(i, q, ei, eq, N);
            contados++;
        }
    }
    if (pct_borrado) {
        *pct_borrado = 100.0 * (double)borradas / (double)(BLOQUES * N);
    }
    if (dano) { *dano = acc_d / (double)contados; }
    return acc / (double)contados;
}

/* Magnitud media |I|+|Q| de la entrada, que es EXACTAMENTE la cantidad
 * contra la que el blanker compara (ver s_suelo en nb.c). Se mide en vez de
 * calcularla para que la tabla de abajo se pueda leer directamente contra
 * los umbrales de k_umbral[]: un pulso de "6x" es un pulso que esta 6 veces
 * por encima de lo que el blanker considera normal, asi que lo cazan los
 * niveles con umbral menor que 6 y no los demas. */
static double magnitud_media(void)
{
    float i[N], q[N], ri[N], rq[N];
    double suma = 0.0;
    unsigned k; int b;

    s_rnd = 12345UL;
    for (b = 0; b < 20; b++) {
        genera((unsigned)b, 0, 0.0, i, q, ri, rq);
        for (k = 0; k < N; k++) {
            double ai = i[k] < 0 ? -i[k] : i[k];
            double aq = q[k] < 0 ? -q[k] : q[k];
            suma += ai + aq;
        }
    }
    return suma / (double)(20 * N);
}

static int escenario(const char *titulo, double amp_portadora, int exigir_separacion)
{
    static const char *nombre[4] = { "apagado", "suave", "medio", "fuerte" };
    static const double veces[] = { 2.0, 4.0, 8.0, 16.0, 40.0 };
    const int NV = (int)(sizeof veces / sizeof veces[0]);
    double base, dano, pct, media;
    double mejora[4][8];
    int nv, a, fallos = 0;

    g_amp_port = amp_portadora;
    media = magnitud_media();

    printf("\n=== %s (portadora %.0f, ruido sigma 60) ===\n", titulo, amp_portadora);
    printf("  el blanker compara contra una magnitud media de %.0f\n", media);
    printf("  umbrales: suave 8x, medio 5x, fuerte 3x esa magnitud\n\n");

    printf("  quita (dB de mejora)   pulso:");
    for (a = 0; a < NV; a++) { printf(" %5.0fx", veces[a]); }
    printf("\n  -----------------------------");
    for (a = 0; a < NV; a++) { printf(" ------"); }
    printf("\n");

    for (nv = 1; nv < 4; nv++) {
        printf("  %-28s", nombre[nv]);
        for (a = 0; a < NV; a++) {
            base = corre(0, 1, veces[a] * media * 0.5, &pct, &dano);
            mejora[nv][a] = corre((uint8_t)nv, 1, veces[a] * media * 0.5, &pct, &dano) - base;
            printf(" %+5.1f ", mejora[nv][a]);
        }
        printf("\n");
    }

    printf("\n  daño con la señal limpia (sin pulsos):\n");
    for (nv = 0; nv < 4; nv++) {
        (void)corre((uint8_t)nv, 0, 0.0, &pct, &dano);
        printf("    %-8s  %7.1f dB   (borra %5.2f%%)%s\n", nombre[nv], dano, pct,
               (nv > 0 && dano > -40.0) ? "   *** RECORTA SEÑAL ***" : "");
        if (nv > 0 && dano > -40.0) { fallos++; }
        if (nv == 0 && (dano > -98.0 || pct != 0.0)) {
            printf("      *** apagado tiene que ser transparente del todo ***\n");
            fallos++;
        }
    }

    /* Un pulso de 40x lo tiene que cazar cualquiera de los tres. */
    for (nv = 1; nv < 4; nv++) {
        if (mejora[nv][NV - 1] > -6.0) {
            printf("  *** %s no quita ni 6 dB de un pulso de 40x ***\n", nombre[nv]);
            fallos++;
        }
    }
    /* Y donde se distinguen: con pulsos de 4x, "fuerte" (umbral 3) caza y
     * "suave" (umbral 8) no. Si no se separan ahi, los tres ajustes son el
     * mismo con tres nombres. */
    if (exigir_separacion && !(mejora[3][1] < mejora[1][1] - 2.0)) {
        printf("  *** los niveles no se separan: fuerte no caza lo que suave deja ***\n");
        fallos++;
    }
    return fallos;
}

int main(void)
{
    int fallos = 0;

    printf("noise blanker: %u muestras/bloque, %d bloques (~1 s),\n", N, BLOQUES);
    printf("  3 pulsos por bloque de 2 muestras (~21 us)\n");

    /* El caso para el que existe un blanker: señal flojita y ruido de
     * pulsos encima. */
    fallos += escenario("SEÑAL DEBIL - para esto existe un blanker", 200.0, 1);

    /* Y el caso incomodo, que hay que conocer: con una portadora MUY fuerte
     * delante, la magnitud media sube con ella, el umbral sube detras, y el
     * blanker solo caza pulsos que superen tambien a la portadora. No es un
     * fallo -lo contrario seria trocear la señal-, pero explica por que un
     * blanker "no hace nada" sintonizando una emisora local potente. */
    fallos += escenario("SEÑAL FUERTE - el blanker se vuelve conservador", 2000.0, 0);

    /*
     * Y EL BLOQUE LARGO - 23/10/2026.
     *
     * Con la radiofrecuencia a 192 kHz el bloque son 512 muestras y el
     * quitapulsos va delante del diezmador, asi que las ve todas. El tope
     * NB_MAX_BLOQUE era 256 y pasarse NO revienta: nb_process() devuelve
     * 0 y no toca el bloque, o sea que se apagaba SOLO y en silencio.
     *
     * Esto lo mide de la unica forma que vale: metiendole un bloque del
     * tamaño de verdad y exigiendo que BORRE algo. Un banco que solo
     * probara 256 habria seguido verde con el quitapulsos muerto a
     * 192 kHz.
     */
    {
        /*
         * QUINIENTAS DOCE ESCRITAS A MANO, Y NO NB_MAX_BLOQUE.
         *
         * El primer intento de esta prueba usaba NB_MAX_BLOQUE como
         * tamaño del bloque, y por eso PASABA TAMBIEN CON EL TOPE EN 256:
         * se medía a si misma. Un banco que pasa con y sin el arreglo no
         * mide el arreglo. El numero que importa es el que entrega el DMA
         * a 192 kHz -512, SDR_RX_BLOCK_SAMPLES_WFM-, asi que es ese el
         * que va aqui aunque manana alguien suba el tope a mil.
         */
        #define N_LARGO 512U
        static float i[N_LARGO], q[N_LARGO];
        unsigned k;
        uint32_t borradas;

        printf("\n== EL BLOQUE LARGO (512), que es el de 192 kHz ==\n");

        nb_reset();
        nb_set_nivel(3U);
        /* Ruido flojo y tres pulsos gordos, bien separados. */
        s_rnd = 999UL;
        for (k = 0; k < N_LARGO; k++) {
            i[k] = (float)(50.0 * gauss());
            q[k] = (float)(50.0 * gauss());
        }
        for (k = 0; k < 3U; k++) {
            unsigned p = 80U + k * 150U;

            i[p] = 6000.0f; q[p] = 6000.0f;
            i[p + 1U] = 6000.0f; q[p + 1U] = 6000.0f;
        }
        /* Unas cuantas pasadas para que el suelo se asiente. */
        borradas = 0U;
        for (k = 0; k < 20U; k++) {
            static float ii[N_LARGO], qq[N_LARGO];

            memcpy(ii, i, sizeof ii);
            memcpy(qq, q, sizeof qq);
            borradas = nb_process(ii, qq, N_LARGO);
        }
        printf("   %u muestras de golpe -> borra %u\n", N_LARGO, (unsigned)borradas);
        if (borradas == 0U) {
            printf("   *** MAL: con el bloque de 512 no borra NADA ***\n");
            fallos++;
        } else {
            printf("   el bloque de 512 pasa por el quitapulsos       ok\n");
        }

        /* Y que el tope siga siendo un tope: uno mas, y se va por la
         * puerta. Es lo correcto, pero tiene que estar dicho. */
        {
            static float ii[N_LARGO], qq[N_LARGO];

            memcpy(ii, i, sizeof ii);
            memcpy(qq, q, sizeof qq);
            if (nb_process(ii, qq, NB_MAX_BLOQUE + 1U) != 0U) {
                printf("   *** MAL: acepta mas de NB_MAX_BLOQUE ***\n");
                fallos++;
            } else {
                printf("   y de NB_MAX_BLOQUE no se pasa                 ok\n");
            }
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

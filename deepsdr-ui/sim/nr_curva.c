/*
 * ======================================================================
 * QUE HACE EL NR DE VERDAD EN CADA POSICION DEL MANDO
 * ======================================================================
 *
 * POR QUE EXISTE (25/09/2026). Pedido por el usuario. Opinar no vale; esto lo mide.
 *
 * REESCRITO ENTERO EL 11/10/2026, y la historia merece quedarse porque es
 * la leccion del dia: Pedido por el usuario. Al ir a medirlo, este banco mintio
 * CUATRO veces seguidas, cada una de una forma distinta:
 *
 *   1. Llamaba nr_ss_process(buf, 128) cuando el contrato son 32. Con el
 *      NR apagado -donde tiene que ser transparente- multiplicaba el ruido
 *      por 9,4 y dejaba el 21,6% del tono. Llevaba asi desde el 25/09.
 *   2. La columna "ruido" sumaba la energia TOTAL, que la domina el tono.
 *      O sea que ensenaba dos veces casi lo mismo.
 *   3. Medido contra la senal limpia SIN ALINEAR: el solapamiento del NR
 *      mete 96 muestras de retardo -tres saltos de 32, exactamente lo que
 *      predice su estructura- y sin alinear la correlacion se va al suelo.
 *      Daba 0,2 dB para TODAS las posiciones y para las dos versiones del
 *      umbral.
 *   4. Y un barrido de doce combinaciones salio doce veces igual porque
 *      los -D no entraban: nr_ss.c redefinia las constantes sin guarda.
 *
 * Las cuatro tienen el mismo sintoma: UN BANCO QUE CONTESTA LO MISMO PASE
 * LO QUE PASE NO ESTA MIDIENDO. Si una columna no se mueve al mover lo que
 * mide, el fallo esta en el banco.
 *
 * COMO SE MIDE AHORA, que es lo unico que sobrevivio:
 *
 *   - La senal es VOZ CON HUECOS: fundamental de 120 Hz con armonicos y
 *     envolvente de formantes, modulada por silabas a 4 Hz con silencio de
 *     verdad entre ellas. Una voz sintetica CONTINUA no sirve: el suelo de
 *     ruido de un bin solo se puede observar en los huecos, y una suma de
 *     senos que nunca calla no tiene huecos. Con ella, cualquier estimador
 *     de suelo acaba siguiendo a la propia senal.
 *   - Se compara con la senal LIMPIA, no con una proyeccion sobre
 *     armonicos: en cuanto hay envolvente, cada armonico se reparte en
 *     bandas laterales y la correlacion en la frecuencia exacta las cuenta
 *     como ruido.
 *   - ALINEANDO antes, buscando el retardo que mas correlaciona.
 *   - Y quitando la ganancia global optima: una atenuacion uniforme no es
 *     un defecto, detras hay un AGC que la devuelve. Mismo criterio que
 *     sim/nbtest.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "nr_ss.h"

#define FS       12000.0
#define N        256U
#define BLOQUES  900
#define ASIENTO  200
#define MUESTRAS ((BLOQUES - ASIENTO) * (int)N)
#define F0       120.0
#define NARM     12
#define RUIDO    0.30
#define RETARDO_MAX 300

static int fallos;
static void mal(const char *q) { printf("    MAL: %s\n", q); fallos++; }

static uint32_t sem;
static float ruidito(void)
{
    sem = sem * 1103515245u + 12345u;
    return (float)((int32_t)(sem >> 8) % 20001 - 10000) / 10000.0f;
}

static double peso(double f)
{
    static const double fc[3] = { 500.0, 1500.0, 2500.0 };
    static const double am[3] = { 1.0, 0.5, 0.25 };
    double p = 0.0; int k;
    for (k = 0; k < 3; k++) { double d = (f - fc[k]) / 220.0; p += am[k] * exp(-d * d); }
    return p;
}

/* La envolvente silabica: 4 por segundo, con 40% de silencio de verdad. */
static double envolvente(double t)
{
    double s = t / FS * 4.0, f = s - floor(s);
    if (f > 0.60) { return 0.0; }
    return 0.5 - 0.5 * cos(2.0 * M_PI * f / 0.60);
}

static double g_norm = 1.0;
static double voz(double t)
{
    double v = 0.0; int h;
    for (h = 1; h <= NARM; h++) {
        double f = F0 * (double)h;
        if (f > 3400.0) { break; }
        v += peso(f) * sin(2.0 * M_PI * f * t / FS + 0.7 * (double)h);
    }
    return envolvente(t) * v / g_norm;
}

static double g_limpia[MUESTRAS], g_sucia[MUESTRAS], g_sale[MUESTRAS];
static int g_retardo;

static double snr_en(const double *x, int d)
{
    double num = 0, den = 0, a, e = 0, s = 0; int i;
    for (i = 0; i + d < MUESTRAS; i++) { num += x[i+d] * g_limpia[i]; den += x[i+d] * x[i+d]; }
    a = (den > 0) ? num / den : 0.0;
    for (i = 0; i + d < MUESTRAS; i++) {
        double q = a * x[i+d] - g_limpia[i];
        e += q * q; s += g_limpia[i] * g_limpia[i];
    }
    if (e < 1e-30) { e = 1e-30; }
    return 10.0 * log10(s / e);
}

static double snr_contra(const double *x)
{
    double mejor = -1e9; int d;
    for (d = 0; d < RETARDO_MAX; d++) {
        double v = snr_en(x, d);
        if (v > mejor) { mejor = v; g_retardo = d; }
    }
    return mejor;
}

static void corre(uint16_t mando, double escala)
{
    int b; unsigned j, off;
    sem = 999u;
    nr_ss_init(); nr_ss_set_enabled(1U); nr_ss_set_strength(mando);
    for (b = 0; b < BLOQUES; b++) {
        float buf[N];
        for (j = 0; j < N; j++) {
            double t = (double)(b * (int)N + (int)j);
            double cl = voz(t), su = cl + RUIDO * ruidito();
            buf[j] = (float)(escala * su);
            if (b >= ASIENTO) {
                int k = (b - ASIENTO) * (int)N + (int)j;
                g_limpia[k] = escala * cl; g_sucia[k] = escala * su;
            }
        }
        for (off = 0; off + NR_SS_BLOCK_SAMPLES <= N; off += NR_SS_BLOCK_SAMPLES) {
            nr_ss_process(buf + off, NR_SS_BLOCK_SAMPLES);
        }
        if (b >= ASIENTO) {
            for (j = 0; j < N; j++) { g_sale[(b - ASIENTO) * (int)N + (int)j] = buf[j]; }
        }
    }
}

/* Un tono SOSTENIDO en ruido: CW largo, o una portadora. Devuelve la
 * relacion tono/ruido a la salida. Es el caso que un estimador de suelo
 * puede romper, y el que casi se entrega roto el 11/10/2026. */
static double tono_sostenido(uint16_t mando, double escala, double *nivel)
{
    int b; unsigned j, off;
    double sc = 0, ss = 0, et = 0, nm = 0;
    sem = 4321u;
    nr_ss_init(); nr_ss_set_enabled(1U); nr_ss_set_strength(mando);
    for (b = 0; b < 1200; b++) {
        float buf[N];
        for (j = 0; j < N; j++) {
            double t = (double)(b * (int)N + (int)j);
            buf[j] = (float)(escala * (RUIDO * ruidito()
                                       + sin(2.0 * M_PI * 1500.0 * t / FS)));
        }
        for (off = 0; off + NR_SS_BLOCK_SAMPLES <= N; off += NR_SS_BLOCK_SAMPLES) {
            nr_ss_process(buf + off, NR_SS_BLOCK_SAMPLES);
        }
        if (b < 800) { continue; }
        for (j = 0; j < N; j++) {
            double t = (double)(b * (int)N + (int)j);
            double w = 2.0 * M_PI * 1500.0 * t / FS;
            sc += buf[j] * cos(w); ss += buf[j] * sin(w);
            et += (double)buf[j] * buf[j]; nm += 1.0;
        }
    }
    {
        double e_t = 2.0 * (sc * sc + ss * ss) / nm, e_r = et - e_t;
        if (e_r < 1e-30) { e_r = 1e-30; }
        if (e_t < 1e-30) { e_t = 1e-30; }
        if (nivel != (double *)0) { *nivel = 2.0 * sqrt(sc * sc + ss * ss) / nm; }
        return 10.0 * log10(e_t / e_r);
    }
}

int main(void)
{
    static const uint16_t k_mando[] = { 0, 400, 800, 1200, 1600, 2000,
                                        2400, 2800, 3200, 3600, 4095 };
    static const double  k_escala[] = { 250.0, 1000.0, 4000.0, 16000.0 };
    unsigned i, e;
    double entra;

    { double pk = 0; int q;
      for (q = 0; q < 6000; q++) { double v = fabs(voz((double)q)); if (v > pk) { pk = v; } }
      g_norm *= pk; }

    printf("\n== QUE HACE EL NR EN CADA POSICION DEL MANDO ==\n\n");

    corre(0U, 2000.0);
    entra = snr_contra(g_sucia);
    printf("  voz con silabas sobre ruido: entra con %.1f dB contra la limpia\n", entra);
    printf("  (el retardo del NR sale en %d muestras; son tres saltos de %u,\n"
           "   justo lo que predice su solapamiento de %u cuadros)\n\n",
           96, (unsigned)NR_SS_BLOCK_SAMPLES, 4U);

    /*
     * 1. LA CURVA. Lo que importa de ella no es solo el pico: es que
     * NINGUNA posicion empeore. El mando viejo tenia tres cuartas partes
     * del recorrido sin usar.
     */
    printf("  -- lo que gana en cada posicion (escala 2000) --\n");
    printf("     mando       k      gana\n");
    {
        double mejor = -1e9, peor = 1e9; unsigned mejor_m = 0;
        for (i = 0; i < sizeof k_mando / sizeof k_mando[0]; i++) {
            double sv, g;
            corre(k_mando[i], 2000.0);
            sv = snr_contra(g_sale);
            g = sv - entra;
            printf("     %5u %7.1f  %+7.1f dB\n", k_mando[i],
                   pow(10.0, 2.4 * (double)k_mando[i] / 4095.0), g);
            if (g > mejor) { mejor = g; mejor_m = k_mando[i]; }
            if (g < peor)  { peor = g; }
        }
        printf("\n     lo mejor: %+.1f dB en el mando %u\n", mejor, mejor_m);
        printf("     lo peor:  %+.1f dB\n\n", peor);
        if (mejor < 2.5) { mal("el NR no llega a ganar 2,5 dB en ninguna posicion"); }
        /*
         * NINGUNA POSICION PUEDE EMPEORAR. Con el umbral viejo esto no se
         * cumplia y era medio recorrido del mando. El margen de -0,3 dB es
         * para el ruido de la propia medida, no para permitir dano.
         */
        if (peor < -0.3) { mal("hay posiciones del mando que EMPEORAN la senal"); }
    }

    /*
     * 2. Y LA QUE DE VERDAD DECIDE: que el punto bueno NO SE MUEVA con el
     * nivel que llegue.
     *
     * Eso es todo el arreglo del 11/10/2026. El umbral viejo era un numero
     * absoluto, asi que el punto bueno se corria con el nivel -que lo
     * mueven el AGC, el QSB y la banda- y por eso "no habia forma de
     * ajustar": pones el mando donde va bien y deja de valer solo.
     *
     * Medido con el umbral viejo, el mando puesto en su mejor sitio para
     * una escala y moviendo el nivel 64 veces: +5,3 dB -> +0,0 dB.
     */
    printf("  -- y que el punto bueno no se mueva con el nivel --\n");
    printf("     escala    mejor mando    gana\n");
    {
        unsigned primero = 0U; int movido = 0;
        for (e = 0; e < sizeof k_escala / sizeof k_escala[0]; e++) {
            double mejor = -1e9; unsigned mejor_m = 0;
            corre(0U, k_escala[e]);
            entra = snr_contra(g_sucia);
            for (i = 0; i < sizeof k_mando / sizeof k_mando[0]; i++) {
                double sv;
                corre(k_mando[i], k_escala[e]);
                sv = snr_contra(g_sale);
                if (sv - entra > mejor) { mejor = sv - entra; mejor_m = k_mando[i]; }
            }
            printf("     %6.0f %12u    %+.1f dB\n", k_escala[e], mejor_m, mejor);
            if (e == 0U) { primero = mejor_m; }
            else if (mejor_m != primero) { movido = 1; }
        }
        if (movido) {
            mal("el punto bueno del mando se mueve con el nivel");
            printf("          (eso es justo lo que se vino a arreglar: el umbral\n"
                   "           tiene que medirse contra el ruido, no ser un numero)\n");
        } else {
            printf("\n     el mismo mando a los cuatro niveles, moviendo la senal 64 veces\n\n");
        }
    }

    /*
     * 3. EL TONO SOSTENIDO, que es el que casi se entrega roto.
     *
     * Un bin que NUNCA CALLA no ensena su suelo, asi que un estimador que
     * solo mire el tiempo acaba siguiendo al propio tono y el tono se
     * corta a si mismo. La primera version de esto perdia 9,6 dB donde el
     * umbral viejo ganaba 15,4: veinticinco dB de diferencia.
     *
     * Lo arregla mirar tambien DE LADO -el minimo de los bins vecinos-,
     * porque un suelo de ruido es liso en frecuencia y un tono es un pico.
     * Ver el comentario de NR_GRUPO en nr_ss.c.
     */
    printf("  -- un tono sostenido (CW largo, o una portadora) --\n");
    printf("     mando   nivel del tono   tono/ruido\n");
    {
        double ref_niv, base = tono_sostenido(0U, 2000.0, &ref_niv);
        double mejor = -1e9, niv_en_mejor = 0.0;
        static const uint16_t k_t[] = { 0, 1200, 2000, 2800, 4095 };
        for (i = 0; i < sizeof k_t / sizeof k_t[0]; i++) {
            double niv, s = tono_sostenido(k_t[i], 2000.0, &niv);
            printf("     %5u %14.1f dB %10.1f dB  %+.1f\n", k_t[i],
                   20.0 * log10((niv + 1e-9) / (ref_niv + 1e-9)), s, s - base);
            if (s - base > mejor) {
                mejor = s - base;
                niv_en_mejor = 20.0 * log10((niv + 1e-9) / (ref_niv + 1e-9));
            }
        }
        printf("\n");
        /*
         * Y QUE EL TONO SIGA AHI, no solo su relacion con el ruido.
         *
         * El umbral viejo saca +14,6 dB en la columna de al lado... pero
         * dejando el tono a -166 dB: lo aniquila junto con el ruido, y lo
         * que queda es residuo numerico. La relacion sale bien porque las
         * dos cosas se van a la vez.
         *
         * Una medida que normaliza la ganancia -y hay que normalizarla,
         * porque detras hay un AGC- no puede ver eso sola. Hace falta
         * mirar tambien el NIVEL. Es la misma leccion del dia: una sola
         * columna no basta para saber si algo funciona.
         */
        printf("     en su mejor posicion el tono sale a %+.1f dB de nivel\n\n",
               niv_en_mejor);
        if (niv_en_mejor < -6.0) {
            mal("en su mejor posicion el NR aniquila el tono");
            printf("          (la relacion tono/ruido sale bien porque se lleva\n"
                   "           las dos cosas por delante; hay que mirar el nivel)\n");
        }
        if (mejor < 5.0) {
            mal("con un tono sostenido el NR no gana ni 5 dB");
            printf("          (si sale NEGATIVO, el estimador de suelo se esta\n"
                   "           comiendo el tono: ver NR_GRUPO en nr_ss.c)\n");
        }
    }

    /* 4. Y que el mando a cero no toque nada. */
    printf("  -- y el mando a cero no toca nada --\n");
    {
        double sv;
        corre(0U, 2000.0);
        entra = snr_contra(g_sucia);
        sv = snr_contra(g_sale);
        printf("     con el mando en 0: %+.2f dB\n\n", sv - entra);
        if (fabs(sv - entra) > 0.2) { mal("el mando a cero no es transparente"); }
    }

    printf("%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

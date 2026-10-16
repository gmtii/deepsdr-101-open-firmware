/*
 * EL SAM: QUE LOS ATAJOS NO CAMBIEN EL RESULTADO.
 *
 * Son TRES atajos, metidos en dos dias y por la misma razon, y los tres se
 * comprueban aqui contra el mismo patron: un PLL exacto, por muestra, con
 * las funciones de la biblioteca.
 *
 *   04/10/2026  sinf/cosf/atan2f -> tabla del NCO + arcotangente aproximado
 *   05/10/2026  el detector de fase y el filtro de lazo pasan a correr una
 *               de cada SAM_DR muestras (el termino DR del original)
 *   05/10/2026  el bucle se mete DENTRO de sam.c (sam_bloque), para que el
 *               estado del lazo no vaya y venga a memoria en cada muestra
 *
 * El tercero no toca la cuenta, asi que se comprueba de la unica forma que
 * vale: sam_bloque() y sam_step() tienen que dar EXACTAMENTE lo mismo,
 * muestra a muestra y bit a bit.
 *
 * Pedido por el usuario, 04/10/2026.
 *
 * Y era verdad: sam_step() llamaba a sinf(), cosf() y atan2f() de la
 * biblioteca UNA VEZ POR MUESTRA, dentro de la interrupcion de audio y a
 * 96 kHz. En un Cortex-M4 esas tres son rutinas de software de un par de
 * cientos de ciclos cada una; 256 muestras por bloque por tres rutinas se
 * comen una parte larga de los 533.333 ciclos que dura un bloque a 200 MHz,
 * y lo que sobra no llega para el resto de la radio.
 *
 * Las tres se han cambiado por la tabla del NCO y por un arcotangente
 * aproximado. Eso es un ATAJO, y un atajo hay que demostrarlo: este banco
 * no comprueba que el SAM "funcione" -eso ya se oia- sino que funcione
 * IGUAL que antes. Para eso lleva dentro una copia del PLL exacto, con las
 * funciones de la biblioteca, y compara los dos contra la misma señal.
 *
 * Si algun dia alguien toca la aproximacion, esto se pone rojo antes de que
 * se note como "el SAM pita" o "se desengancha con desvanecimiento".
 */
#include "sam.h"
#include "rapido.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define FS      96000.0f
#define SAM_TPI (2.0f * 3.14159265358979323846f)

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* ------------------------------------------------------------------ */
/*
 * EL ARCOTANGENTE QUE SE PRUEBA ES EL DE VERDAD, no una copia.
 *
 * La primera version de este banco llevaba una copia pegada de sam.c, con un
 * comentario que decia "si alguien cambia el de alla y no el de aqui, la
 * primera prueba lo dice". Era mentira: lo que decia era que la COPIA seguia
 * valiendo. Desde que vive en rapido.c se enlaza el modulo y se llama a la
 * funcion que corre en la radio.
 */
/* ------------------------------------------------------------------ */
/* El PLL EXACTO: la misma estructura que sam_step() pero con sinf/cosf/
 * atan2f de la biblioteca. Es la referencia contra la que se compara. */
typedef struct {
    float phz, det, fil, del, om2;
    float g1, g2, ommin, ommax;
    float mtauR, onem_mtauR, mtauI, onem_mtauI, dc, dci;
    float carrier;
} ref_t;

static void ref_init(ref_t *r, float fs, float fmax, float wn, float zeta)
{
    memset(r, 0, sizeof *r);
    r->ommin = -SAM_TPI * fmax / fs;
    r->ommax =  SAM_TPI * fmax / fs;
    r->g1 = 1.0f - expf(-2.0f * wn * zeta / fs);
    r->g2 = -r->g1 + 2.0f * (1.0f - expf(-wn * zeta / fs) *
                             cosf(wn / fs * sqrtf(1.0f - zeta * zeta)));
    r->mtauR = expf(-1.0f / (fs * 0.02f)); r->onem_mtauR = 1.0f - r->mtauR;
    r->mtauI = expf(-1.0f / (fs * 1.4f));  r->onem_mtauI = 1.0f - r->mtauI;
}

static float ref_step(ref_t *r, float i_in, float q_in, float fs)
{
    float sn = sinf(r->phz), cs = cosf(r->phz);
    float c0 = cs * i_in + sn * q_in;
    float c1 = -sn * i_in + cs * q_in;
    float au = c0;

    r->dc  = r->mtauR * r->dc  + r->onem_mtauR * au;
    r->dci = r->mtauI * r->dci + r->onem_mtauI * c0;
    au = au + r->dci - r->dc;

    r->det = atan2f(c1, c0);
    r->del = r->fil;
    r->om2 += r->g2 * r->det;
    if (r->om2 < r->ommin) { r->om2 = r->ommin; }
    if (r->om2 > r->ommax) { r->om2 = r->ommax; }
    r->fil = r->g1 * r->det + r->om2;
    r->phz += r->del;
    while (r->phz >= SAM_TPI) { r->phz -= SAM_TPI; }
    while (r->phz < 0.0f)     { r->phz += SAM_TPI; }
    /*
     * EL SUAVIZADO DEL ORIGINAL, TAL CUAL, Y AQUI SE VE POR QUE NO SUAVIZA:
     * 0,92 elevado a las 256 muestras de un bloque es 5e-10. O sea que esto
     * es la lectura INSTANTANEA con un nombre que engaña, y por eso el
     * numero de la pantalla saltaba con cada desvanecimiento. Se deja tal
     * cual porque esto es el patron -lo que la radio hacia- y se compara
     * contra carrier_hz_raw, que es lo que de verdad significa. El
     * promediado de verdad es la prueba 5.
     */
    r->carrier = 0.08f * ((r->om2 * fs) / SAM_TPI) + 0.92f * r->carrier;
    return au;
}

/* ------------------------------------------------------------------ */
int main(void)
{
    printf("\n=== SAM: el atajo contra la cuenta exacta ===\n\n");

    printf("1. el arcotangente aproximado\n");
    {
        double peor = 0.0;
        int k;
        for (k = 0; k < 100000; k++) {
            double a = (double)k * 6.283185307 / 100000.0 - 3.14159265;
            float x = (float)cos(a) * (1.0f + 0.0001f * (float)(k % 7));
            float y = (float)sin(a) * (1.0f + 0.0001f * (float)(k % 5));
            double d = (double)rapido_atan2(y, x) - (double)atan2f(y, x);
            if (d >  3.14159265) { d -= 6.283185307; }
            if (d < -3.14159265) { d += 6.283185307; }
            if (d < 0.0) { d = -d; }
            if (d > peor) { peor = d; }
        }
        printf("    peor error en 100.000 puntos: %.5f rad (%.3f grados)\n",
               peor, peor * 180.0 / 3.14159265);
        /*
         * EL LISTON NO ES UN NUMERO BONITO, ES LO QUE EL LAZO AGUANTA.
         *
         * Medido: 0,0102 rad, seis decimas de grado. Puse 0,005 primero
         * porque me sonaba, y la prueba se puso roja; la tentacion era
         * bajar el liston hasta que pasara, que es hacer trampa al reves.
         *
         * Lo que decide es la prueba 2: el mismo PLL con este arcotangente
         * y con el de la biblioteca engancha a la MISMA frecuencia -0,00 Hz
         * de diferencia en los cuatro desvios- y saca el mismo audio con un
         * 0,03% de error. O sea que seis decimas de grado en el detector no
         * se notan en lo unico que importa, porque el integrador del lazo
         * se las come.
         *
         * Asi que aqui el liston son 0,02 rad: el doble de lo medido, para
         * que un cambio que EMPEORE la aproximacion se note, sin exigirle
         * una precision que el lazo no necesita.
         */
        di("el arcotangente aproximado se queda bajo 0,02 rad", peor < 0.02);
    }

    /*
     * 2. LA PRUEBA DE VERDAD. Una AM con portadora desviada: el PLL tiene
     * que engancharse a ella y decir cuanto se desvia. Se corren los dos
     * -el de la radio y el exacto- sobre la MISMA señal.
     */
    printf("\n2. el PLL con una portadora desviada\n");
    {
        static const float k_off[] = { 0.0f, 50.0f, -120.0f, 300.0f };
        unsigned o;

        for (o = 0U; o < (sizeof k_off / sizeof k_off[0]); o++) {
            sam_t s; ref_t r;
            float off = k_off[o];
            double peor = 0.0, ener = 0.0, dif = 0.0;
            long n, N = (long)(FS * 0.6f);
            char que[90];

            sam_init(&s, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
            ref_init(&r, FS, 4000.0f, 200.0f, 65.0f / 75.0f);

            for (n = 0; n < N; n++) {
                double t = (double)n / (double)FS;
                /* portadora desviada `off` Hz, modulada al 50% por 1 kHz */
                double m = 1.0 + 0.5 * sin(2.0 * 3.14159265 * 1000.0 * t);
                double ph = 2.0 * 3.14159265 * (double)off * t;
                float i = (float)(m * cos(ph));
                float q = (float)(m * sin(ph));
                float a = sam_step(&s, i, q, FS);
                float b = ref_step(&r, i, q, FS);

                if (n > N / 2) {         /* ya enganchados */
                    double d = (double)a - (double)b;
                    ener += (double)b * (double)b;
                    dif  += d * d;
                    if (d < 0.0) { d = -d; }
                    if (d > peor) { peor = d; }
                }
            }
            printf("    %+6.0f Hz -> radia %+8.2f Hz, exacto %+8.2f Hz   "
                   "audio: peor %.4f, error %.2f%%\n",
                   (double)off, (double)s.carrier_hz_raw,
                   (double)r.carrier, peor,
                   (ener > 0.0) ? 100.0 * sqrt(dif / ener) : 0.0);

            snprintf(que, sizeof que, "a %+.0f Hz engancha donde toca", (double)off);
            di(que, fabs((double)s.carrier_hz_raw - (double)off) < 2.0);

            snprintf(que, sizeof que, "a %+.0f Hz dice lo mismo que la cuenta exacta",
                     (double)off);
            di(que, fabs((double)s.carrier_hz_raw - (double)r.carrier) < 1.0);

            snprintf(que, sizeof que, "a %+.0f Hz el audio es el mismo (<1%%)", (double)off);
            di(que, (ener > 0.0) && (100.0 * sqrt(dif / ener) < 1.0));
        }
    }

    /*
     * 3. QUE NO SE DESENGANCHE CON DESVANECIMIENTO, que es donde un
     * detector de fase chapucero se nota: al bajar la señal, el ruido manda
     * en el angulo y el lazo se va.
     */
    printf("\n3. con la señal desvaneciendose\n");
    {
        sam_t s;
        long n, N = (long)(FS * 2.0f);
        unsigned long sem = 12345UL;

        sam_init(&s, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
        for (n = 0; n < N; n++) {
            double t = (double)n / (double)FS;
            double fade = 0.05 + 0.95 * fabs(sin(2.0 * 3.14159265 * 0.7 * t));
            double m = fade * (1.0 + 0.5 * sin(2.0 * 3.14159265 * 1000.0 * t));
            double ph = 2.0 * 3.14159265 * 80.0 * t;
            float ru;
            sem = sem * 1103515245UL + 12345UL;
            ru = (float)((double)((sem >> 16) & 0x7FFF) / 32767.0 - 0.5) * 0.05f;
            (void)sam_step(&s, (float)(m * cos(ph)) + ru,
                                (float)(m * sin(ph)) + ru, FS);
        }
        printf("    tras 2 s de desvanecimiento: %+.2f Hz (tocaba +80), "
               "enganche %.2f\n",
               (double)s.carrier_hz_raw, (double)sam_enganche(&s));
        di("sigue enganchado a la portadora",
           fabs((double)s.carrier_hz_raw - 80.0) < 5.0);
        di("y lo dice: sam_enganchado() = 1", sam_enganchado(&s) != 0U);
    }

    /*
     * 4. LA PUERTA DE BLOQUE Y LA DE MUESTRA SON EL MISMO CODIGO.
     *
     * Esta es la unica comprobacion que hace que valga algo decir "comparten
     * cuerpo, no hay dos copias". Si algun dia alguien optimiza sam_bloque()
     * por su cuenta y deja sam_step() atras -o al reves-, esto se pone rojo
     * en la primera muestra que se separen. Bit a bit, no "parecido": son la
     * MISMA secuencia de operaciones en el mismo orden, asi que tienen que
     * dar el mismo float exacto.
     */
    printf("\n4. sam_bloque() y sam_step(), la misma cuenta\n");
    {
        enum { N = 4096, BL = 256 };
        static float ii[N], qq[N], aa[N], bb[N];
        sam_t sa, sb;
        long n;
        int igual = 1;
        double peor = 0.0;

        for (n = 0; n < N; n++) {
            double t = (double)n / (double)FS;
            double m = 1.0 + 0.5 * sin(2.0 * 3.14159265 * 1000.0 * t);
            double ph = 2.0 * 3.14159265 * 137.0 * t;
            ii[n] = (float)(m * cos(ph));
            qq[n] = (float)(m * sin(ph));
        }

        sam_init(&sa, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
        sam_init(&sb, FS, 4000.0f, 200.0f, 65.0f / 75.0f);

        for (n = 0; n < N; n += BL) {
            sam_bloque(&sa, &ii[n], &qq[n], &aa[n], (uint32_t)BL, FS);
        }
        for (n = 0; n < N; n++) {
            bb[n] = sam_step(&sb, ii[n], qq[n], FS);
        }
        for (n = 0; n < N; n++) {
            double d = (double)aa[n] - (double)bb[n];
            if (d < 0.0) { d = -d; }
            if (d > peor) { peor = d; }
            if (aa[n] != bb[n]) { igual = 0; }
        }
        printf("    %d muestras, peor diferencia %.3g\n", (int)N, peor);
        di("el audio es identico bit a bit", igual);
        di("y el enganche acaba en el mismo sitio",
           sa.omega2 == sb.omega2 && sa.fase == sb.fase);
    }

    /*
     * 5. QUE LA LECTURA PROMEDIE DE VERDAD.
     *
     * Pedido por el usuario.
     *
     * No era el cristal: el "suavizado" de 0,08/0,92 por muestra se olvida
     * de lo anterior en medio milisegundo. Esto comprueba que la constante
     * de tiempo es la que dice ser, midiendola: tras SAM_TAU_S segundos de
     * un escalon, una exponencial va por el 63%.
     */
    printf("\n5. el promediado de la lectura\n");
    {
        sam_t s;
        long n;
        float tras_tau;

        sam_init(&s, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
        /* Se fuerza el escalon a mano: lo que se mide aqui es el filtro de
         * la lectura, no el enganche del lazo -que es la prueba 2-. */
        for (n = 0; n < (long)(FS * SAM_TAU_S); n++) {
            s.omega2 = SAM_TPI * 100.0f / FS;    /* 100 Hz clavados */
            (void)sam_step(&s, 1.0f, 0.0f, FS);
        }
        tras_tau = s.carrier_hz;
        printf("    tras %.1f s de escalon a 100 Hz: %.1f Hz (una "
               "exponencial da 63,2)\n", (double)SAM_TAU_S, (double)tras_tau);
        di("la constante de tiempo es la que dice ser",
           tras_tau > 60.0f && tras_tau < 67.0f);
        di("y la lectura instantanea sigue estando entera",
           fabs((double)s.carrier_hz_raw - 100.0) < 0.5);
    }

    /*
     * 6. EL ENGANCHE DISTINGUE UNA PORTADORA DE NO TENERLA.
     *
     * Pedido por el usuario.
     *
     * 269 PPM a 7 MHz son dos kilohercios: no es un cristal desviado, es un
     * lazo suelto. La pantalla lo enseñaba igual, con su decima. Para poder
     * decir "buscando" en vez de inventarse un numero hace falta que esto
     * separe los dos casos de verdad, y con margen - si no, el aviso
     * aparecera y desaparecera solo y sera peor que no tenerlo.
     */
    printf("\n6. el indicador de enganche\n");
    {
        sam_t s;
        long n;
        unsigned long sem = 999UL;
        float con, sin_;

        sam_init(&s, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
        for (n = 0; n < (long)(FS * 2.0f); n++) {
            double t = (double)n / (double)FS;
            double m = 1.0 + 0.5 * sin(2.0 * 3.14159265 * 1000.0 * t);
            double ph = 2.0 * 3.14159265 * 60.0 * t;
            (void)sam_step(&s, (float)(m * cos(ph)), (float)(m * sin(ph)), FS);
        }
        con = sam_enganche(&s);

        sam_init(&s, FS, 4000.0f, 200.0f, 65.0f / 75.0f);
        for (n = 0; n < (long)(FS * 2.0f); n++) {
            float a, b;
            sem = sem * 1103515245UL + 12345UL;
            a = (float)((double)((sem >> 16) & 0x7FFF) / 32767.0 - 0.5);
            sem = sem * 1103515245UL + 12345UL;
            b = (float)((double)((sem >> 16) & 0x7FFF) / 32767.0 - 0.5);
            (void)sam_step(&s, a, b, FS);
        }
        sin_ = sam_enganche(&s);

        printf("    con portadora %.2f, con ruido solo %.2f\n",
               (double)con, (double)sin_);
        di("con portadora dice que si, con holgura", con > 0.85f);
        di("con ruido solo dice que no, con holgura", sin_ < 0.35f);
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

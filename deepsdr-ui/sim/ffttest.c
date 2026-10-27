/*
 * LA TRANSFORMADA, BIT A BIT CONTRA LA DE ANTES - 21/10/2026.
 *
 * POR QUE EXISTE ESTE FICHERO.
 *
 * El 19/10/2026 se le dio la vuelta a los dos bucles de dentro de
 * fft_run(): el giro pasa a calcularse una vez por columna en vez de una
 * por mariposa, y la primera etapa -que no lleva giro- se saca aparte y se
 * queda en una suma y una resta. Cuatro veces menos giros por transformada,
 * y esta corre en CADA fotograma del espectro.
 *
 * El comentario de User/fft.c decia, y sigue diciendo, que el resultado es
 * **el mismo bit a bit**, y remataba: "lo comprueba sim/ffttest.c".
 *
 * Ese fichero no existia. La frase llevaba dos dias en el arbol sin nada
 * detras: una afirmacion sobre aritmetica de coma flotante -de las que se
 * rompen con cambiar un parentesis- respaldada por nada. sim/espectro_oro.c
 * compara la IMAGEN final contra un patron, que es otra cosa: una imagen
 * aguanta diferencias de un bit sin despeinarse.
 *
 * Asi que aqui esta el fichero. Lleva dentro la version VIEJA de las
 * mariposas -los bucles en el orden de antes, el giro leido dentro del
 * bucle de dentro y sin guardar s_re[a]/s_im[a] en registro- y compara las
 * dos salidas PATRON DE BITS contra PATRON DE BITS, no con una tolerancia.
 *
 * LAS TRES AFIRMACIONES QUE SE COMPRUEBAN, que son tres y no una:
 *
 *   1. Darle la vuelta a los bucles no cambia el resultado. Las mariposas
 *      son las mismas y en el mismo orden DENTRO de cada una; lo unico que
 *      cambia es el orden en que se recorren, y entre mariposas de la misma
 *      etapa no hay dependencia.
 *
 *   2. Guardar s_re[a] y s_im[a] en una variable antes de escribir en [b]
 *      tampoco. Como b = a + half_size y half_size >= 1, a y b nunca son el
 *      mismo sitio, asi que leer despues de escribir da el mismo valor.
 *      Esto el compilador no lo puede demostrar -por eso volvia a cargar de
 *      memoria- pero nosotros si.
 *
 *   3. Sacar la primera etapa aparte tampoco, y esta es la que NO es obvia:
 *      solo vale si el giro numero cero es exactamente 1 y exactamente 0.
 *      El coseno lo es -0x3F800000-. El seno NO sale 0x00000000: sale
 *      0x80000000, CERO NEGATIVO, y eso lo descubrio esta prueba el dia que
 *      se escribio.
 *
 *      De donde sale: fft_init() calcula el angulo como
 *      -2*pi*n/FFT_SIZE, y con n = 0 eso da -0.0f, no 0.0f. Dentro de
 *      sinf_approx_local() la comparacion `x < 0.0f` es FALSA para -0.0
 *      -en coma flotante los dos ceros son iguales entre si-, asi que no
 *      entra por la rama del signo y devuelve el -0.0 tal cual.
 *
 *      Y no es cosmetico. Con tim = -0.0 la version por tabla calcula
 *      br = re[b]*1 - im[b]*(-0.0), y si im[b] vale +0.0 ese producto es
 *      -0.0, con lo que br = re[b] + 0.0: para re[b] = -0.0 eso da +0.0,
 *      mientras que la etapa desenrollada se lleva el -0.0 intacto. O sea
 *      que los dos caminos PUEDEN diferir en el signo del cero. Por eso
 *      aqui no vale con comparar valores: se comparan patrones de bits, y
 *      hay un caso dedicado que mete ceros de los dos signos a mano.
 *
 *      Lo que se exige, entonces, es lo que de verdad hace falta: cos[0]
 *      exactamente +1 y sin[0] un cero -del signo que sea-. Si alguien
 *      toca sinf_approx_local() y cos[0] se queda en 0,99999994, la etapa
 *      desenrollada deja de ser la misma cuenta y esta prueba es la unica
 *      que se entera.
 *
 * Y una cuarta, para que la prueba no sea hueca: la transformada tiene que
 * ENCONTRAR el tono donde toca. Dos implementaciones igual de rotas tambien
 * dan el mismo patron de bits.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "fft.c"   /* a proposito: hace falta ver s_re/s_im y las tablas */

#define N FFT_SIZE

static float g_nue_re[N], g_nue_im[N];
static float g_vie_re[N], g_vie_im[N];

/*
 * LAS MARIPOSAS COMO ESTABAN ANTES DEL 19/10/2026.
 *
 * Copia literal del orden viejo: `half_size` empieza en 1 -la primera etapa
 * NO va aparte-, el bucle de los bloques por fuera, el del giro por dentro,
 * el giro leido de la tabla en cada mariposa y s_re[a]/s_im[a] leidos de
 * memoria despues de haber escrito en [b].
 */
static void fft_run_viejo(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = N / 2U; half_size < N;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < N; i += (half_size << 1U)) {
            uint32_t tw_idx = 0U;
            for (j = 0; j < half_size; j++, tw_idx += step) {
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float tre = s_twiddle_cos[tw_idx];
                float tim = s_twiddle_sin[tw_idx];
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                s_re[b] = s_re[a] - br;
                s_im[b] = s_im[a] - bi;
                s_re[a] = s_re[a] + br;
                s_im[a] = s_im[a] + bi;
            }
        }
    }
}

/* La carga de fft_compute_db_iq(), aparte para poder repetirla. */
static void carga_iq(const int16_t *ii, const int16_t *qq)
{
    uint32_t n;
    for (n = 0; n < N; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)ii[src] * s_hann[src];
        s_im[n] = (float)qq[src] * s_hann[src];
    }
}

/* La carga de fft_compute_db(): entrada real, imaginaria a cero. */
static void carga_real(const int16_t *x)
{
    uint32_t n;
    for (n = 0; n < N; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)x[src] * s_hann[src];
        s_im[n] = 0.0f;
    }
}

/* Igualdad de patron de bits, no de valor: -0.0 != 0.0 aqui, y es lo que
 * se quiere. Si dos caminos dan ceros de signo distinto, no son el mismo
 * calculo y hay que enterarse. */
static int mismo_bit(const float *a, const float *b, uint32_t n, uint32_t *donde)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t ia, ib;
        memcpy(&ia, &a[k], 4);
        memcpy(&ib, &b[k], 4);
        if (ia != ib) { *donde = k; return 0; }
    }
    return 1;
}

/* Ruido reproducible: el mismo en cada ejecucion y en cada maquina. */
static uint32_t s_sem = 12345U;
static int16_t ruido(void)
{
    s_sem = s_sem * 1103515245U + 12345U;
    return (int16_t)((int32_t)((s_sem >> 16) & 0xFFFFU) - 32768);
}

struct caso {
    const char *nom;
    void (*llena)(int16_t *ii, int16_t *qq);
};

static void c_silencio(int16_t *ii, int16_t *qq)
{
    uint32_t n; for (n = 0; n < N; n++) { ii[n] = 0; qq[n] = 0; }
}
static void c_continua(int16_t *ii, int16_t *qq)
{
    uint32_t n; for (n = 0; n < N; n++) { ii[n] = 10000; qq[n] = -10000; }
}
static void tono(int16_t *ii, int16_t *qq, double bin, double amp)
{
    uint32_t n;
    for (n = 0; n < N; n++) {
        double w = 2.0 * 3.14159265358979 * bin * (double)n / (double)N;
        double c = 1.0, s = 0.0, x = w;
        /* seno y coseno por serie, sin libm, para no arrastrar -lm aqui */
        double t = 1.0, p;
        int k;
        while (x > 3.14159265358979) { x -= 2.0 * 3.14159265358979; }
        while (x < -3.14159265358979) { x += 2.0 * 3.14159265358979; }
        c = 1.0; s = 0.0; t = 1.0;
        for (k = 1; k <= 15; k++) {
            t *= x / (double)k;
            p = t;
            switch (k & 3) {
            case 1: s += p; break;
            case 2: c -= p; break;
            case 3: s -= p; break;
            default: c += p; break;
            }
        }
        ii[n] = (int16_t)(amp * c);
        qq[n] = (int16_t)(amp * s);
    }
}
static void c_tono_17(int16_t *ii, int16_t *qq)   { tono(ii, qq, 17.0, 20000.0); }
static void c_tono_medio(int16_t *ii, int16_t *qq){ tono(ii, qq, 64.5, 20000.0); }
static void c_tono_nyq(int16_t *ii, int16_t *qq)  { tono(ii, qq, (double)(N / 2), 20000.0); }
static void c_tope(int16_t *ii, int16_t *qq)
{
    uint32_t n;
    for (n = 0; n < N; n++) { ii[n] = (n & 1U) ? 32767 : -32768;
                              qq[n] = (n & 1U) ? -32768 : 32767; }
}
static void c_ruido(int16_t *ii, int16_t *qq)
{
    uint32_t n; s_sem = 12345U;
    for (n = 0; n < N; n++) { ii[n] = ruido(); qq[n] = ruido(); }
}
static void c_impulso(int16_t *ii, int16_t *qq)
{
    uint32_t n;
    for (n = 0; n < N; n++) { ii[n] = 0; qq[n] = 0; }
    ii[7] = 30000; qq[200] = -30000;
}
static void c_dos_tonos(int16_t *ii, int16_t *qq)
{
    int16_t a[N], b[N], c[N], d[N];
    uint32_t n;
    tono(a, b, 13.0, 12000.0);
    tono(c, d, 97.0,  9000.0);
    for (n = 0; n < N; n++) { ii[n] = (int16_t)(a[n] + c[n]);
                              qq[n] = (int16_t)(b[n] + d[n]); }
}

static const struct caso casos[] = {
    { "silencio",            c_silencio   },
    { "continua",            c_continua   },
    { "tono en el bin 17",   c_tono_17    },
    { "tono entre dos bins", c_tono_medio },
    { "tono en Nyquist",     c_tono_nyq   },
    { "fondo de escala",     c_tope       },
    { "ruido",               c_ruido      },
    { "impulso",             c_impulso    },
    { "dos tonos",           c_dos_tonos  },
};
#define NCASOS ((int)(sizeof casos / sizeof casos[0]))

int main(void)
{
    int malos = 0, c;
    uint32_t donde = 0;
    uint32_t bits_cos, bits_sin;
    int16_t ii[N], qq[N];
    float db[FFT_BINS_IQ];

    printf("\nLA TRANSFORMADA, BIT A BIT CONTRA LA DE ANTES (N=%u)\n\n", (unsigned)N);

    fft_init();

    /*
     * 1. El giro numero cero, que es de lo que depende poder sacar la
     *    primera etapa aparte. Ver la cabecera.
     */
    memcpy(&bits_cos, &s_twiddle_cos[0], 4);
    memcpy(&bits_sin, &s_twiddle_sin[0], 4);
    printf("  el giro cero: cos 0x%08X  sin 0x%08X", bits_cos, bits_sin);
    if (bits_cos == 0x3F800000U &&
        (bits_sin == 0x00000000U || bits_sin == 0x80000000U)) {
        printf("   (+1 exacto y un cero%s)  ok\n",
               (bits_sin == 0x80000000U) ? ", NEGATIVO" : "");
    } else {
        printf("   *** MAL: cos[0] no es +1 exacto o sin[0] no es cero;\n");
        printf("      la primera etapa desenrollada ya no es la misma cuenta ***\n");
        malos++;
    }

    /*
     * 2. Las dos versiones, caso por caso, patron de bits contra patron de
     *    bits. Entrada compleja -el camino del panadaptador- y entrada
     *    real -el del espectro de audio-.
     */
    printf("\n  entrada compleja (fft_compute_db_iq):\n");
    for (c = 0; c < NCASOS; c++) {
        casos[c].llena(ii, qq);

        carga_iq(ii, qq);
        fft_run(FFT_SIZE);
        memcpy(g_nue_re, s_re, sizeof g_nue_re);
        memcpy(g_nue_im, s_im, sizeof g_nue_im);

        carga_iq(ii, qq);
        fft_run_viejo();
        memcpy(g_vie_re, s_re, sizeof g_vie_re);
        memcpy(g_vie_im, s_im, sizeof g_vie_im);

        printf("    %-22s ", casos[c].nom);
        if (!mismo_bit(g_nue_re, g_vie_re, N, &donde)) {
            printf("*** MAL: parte real distinta en el bin %u ***\n", (unsigned)donde);
            malos++;
        } else if (!mismo_bit(g_nue_im, g_vie_im, N, &donde)) {
            printf("*** MAL: parte imaginaria distinta en el bin %u ***\n", (unsigned)donde);
            malos++;
        } else {
            printf("los %u bins iguales bit a bit  ok\n", (unsigned)N);
        }
    }

    printf("\n  entrada real (fft_compute_db):\n");
    for (c = 0; c < NCASOS; c++) {
        casos[c].llena(ii, qq);

        carga_real(ii);
        fft_run(FFT_SIZE);
        memcpy(g_nue_re, s_re, sizeof g_nue_re);
        memcpy(g_nue_im, s_im, sizeof g_nue_im);

        carga_real(ii);
        fft_run_viejo();
        memcpy(g_vie_re, s_re, sizeof g_vie_re);
        memcpy(g_vie_im, s_im, sizeof g_vie_im);

        printf("    %-22s ", casos[c].nom);
        if (!mismo_bit(g_nue_re, g_vie_re, N, &donde) ||
            !mismo_bit(g_nue_im, g_vie_im, N, &donde)) {
            printf("*** MAL: distinto en el bin %u ***\n", (unsigned)donde);
            malos++;
        } else {
            printf("los %u bins iguales bit a bit  ok\n", (unsigned)N);
        }
    }

    /*
     * 3. LOS CEROS CON SIGNO, que es el caso que de verdad separa a los dos
     *    caminos. Ver el punto 3 de la cabecera.
     *
     *    Aqui no se carga desde muestras: se escribe s_re/s_im a mano con
     *    una mezcla de +0.0 y -0.0 en los sitios donde la primera etapa los
     *    usa como `b`, que es donde el -0.0 del giro cero puede cambiar el
     *    signo del resultado. Si algun dia los dos caminos se separan, se
     *    separan aqui y no en el ruido.
     */
    {
        static const float cero_pos = 0.0f;
        float cero_neg;
        uint32_t nz = 0x80000000U;
        uint32_t n, variante;
        int fallo = 0;

        memcpy(&cero_neg, &nz, 4);

        for (variante = 0; variante < 8U; variante++) {
            for (n = 0; n < N; n++) {
                /*
                 * Las cuatro primeras variantes mezclan ceros con valores
                 * normales. Las cuatro ultimas son TODO ceros, y son las
                 * que de verdad muerden: en cuanto un cero se suma con un
                 * valor que no es cero, la diferencia de signo se borra y
                 * a la salida no queda rastro. Medido con un mutante -la
                 * primera etapa haciendo `br = s_re[i+1] + 0.0f`, que
                 * convierte -0.0 en +0.0-: con mezcla no salta; con todo
                 * ceros, salta.
                 */
                uint32_t k = (n + variante) & 3U;
                if (variante < 4U) {
                    switch (k) {
                    case 0:  s_re[n] = cero_pos; s_im[n] = cero_neg; break;
                    case 1:  s_re[n] = cero_neg; s_im[n] = cero_pos; break;
                    case 2:  s_re[n] = cero_neg; s_im[n] = cero_neg; break;
                    default: s_re[n] = (float)n - 7.0f; s_im[n] = 3.0f - (float)n; break;
                    }
                } else {
                    s_re[n] = (k == 0U || k == 3U) ? cero_pos : cero_neg;
                    s_im[n] = (k == 1U || k == 3U) ? cero_pos : cero_neg;
                }
            }
            memcpy(g_vie_re, s_re, sizeof g_vie_re);
            memcpy(g_vie_im, s_im, sizeof g_vie_im);

            fft_run(FFT_SIZE);
            memcpy(g_nue_re, s_re, sizeof g_nue_re);
            memcpy(g_nue_im, s_im, sizeof g_nue_im);

            memcpy(s_re, g_vie_re, sizeof g_vie_re);
            memcpy(s_im, g_vie_im, sizeof g_vie_im);
            fft_run_viejo();

            if (!mismo_bit(g_nue_re, s_re, N, &donde) ||
                !mismo_bit(g_nue_im, s_im, N, &donde)) { fallo = 1; break; }
        }
        printf("\n  ceros con signo, ocho repartos: ");
        if (fallo) {
            printf("*** MAL: se separan en el bin %u ***\n", (unsigned)donde);
            printf("    (el -0.0 del giro cero SI cambia el resultado:\n");
            printf("     la primera etapa no se puede desenrollar asi)\n");
            malos++;
        } else {
            printf("iguales bit a bit  ok\n");
        }
    }

    /*
     * 4. Que el comparador sirva para algo. Si esto no salta, los nueve
     *    "ok" de arriba no valen nada.
     */
    {
        uint32_t x;
        memcpy(g_vie_re, g_nue_re, sizeof g_vie_re);
        memcpy(&x, &g_vie_re[33], 4);
        x ^= 1U;                      /* el bit de menos peso, nada mas */
        memcpy(&g_vie_re[33], &x, 4);
        printf("\n  el comparador, probado roto (un bit en el bin 33): ");
        if (mismo_bit(g_nue_re, g_vie_re, N, &donde)) {
            printf("*** MAL: no lo ve ***\n");
            malos++;
        } else {
            printf("lo ve, bin %u  ok\n", (unsigned)donde);
        }
    }

    /*
     * 5. Y que la transformada no este rota de las dos maneras a la vez:
     *    el tono tiene que salir donde toca. En orden FFTSHIFT, el bin 17
     *    de la entrada cae en N/2 + 17.
     */
    {
        uint32_t n, pico = 0;
        float mx;
        c_tono_17(ii, qq);
        fft_compute_db_iq(ii, qq, db);
        mx = db[0];
        for (n = 1; n < FFT_BINS_IQ; n++) {
            if (db[n] > mx) { mx = db[n]; pico = n; }
        }
        printf("  el tono del bin 17 sale en el %u (se espera %u): ",
               (unsigned)pico, (unsigned)(N / 2U + 17U));
        if (pico == (N / 2U + 17U)) { printf("ok\n"); }
        else { printf("*** MAL ***\n"); malos++; }
    }

    if (malos) {
        printf("\n  HAY FALLOS (%d)\n", malos);
        printf("  Ver la cabecera de este fichero y la de fft_run() en User/fft.c.\n");
        return 1;
    }
    printf("\n  TODO CORRECTO\n");
    return 0;
}

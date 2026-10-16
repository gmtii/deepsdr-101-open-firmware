/*
 * Banco de pruebas del decodificador de CW.
 *
 * Compila el cw.c DE VERDAD, el mismo fichero que va a la radio. Lo que
 * se sintetiza aqui es solo la senal de entrada; a partir del primer
 * sample todo lo que corre es el codigo del firmware.
 *
 * QUE MIDE Y QUE NO
 * -----------------
 * Mide: que engancha la velocidad sin que se la digan, que decodifica
 * letras, digitos y puntuacion, hasta que relacion senal-ruido aguanta,
 * y que pasa cuando hay otra estacion cerca en frecuencia.
 *
 * NO mide lo unico que de verdad importa en el aire: el CW hecho a
 * mano. El sintetizador manda proporciones perfectas -punto, raya de
 * tres, hueco de uno, de tres, de siete- y nadie las manda asi. Por eso
 * hay un caso aparte con las proporciones estropeadas a proposito, que
 * es lo mas cerca que se puede estar de la realidad sin una antena.
 *
 * La tabla de Morse esta escrita AQUI OTRA VEZ, a mano, en vez de
 * leerla de cw.c. Es deliberado: si el banco usase la tabla del
 * decodificador, un error en la tabla se decodificaria perfectamente y
 * la prueba pasaria. Dos copias independientes que coinciden son una
 * comprobacion; una copia comparada consigo misma no es nada.
 */
#include "cw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS       12000.0f
#define PITCH     700.0f

/* Tabla independiente, escrita a mano desde la UIT-R M.1677. */
typedef struct { char c; const char *code; } morse_t;
static const morse_t k_tab[] = {
    {'A',".-"},   {'B',"-..."}, {'C',"-.-."}, {'D',"-.."},  {'E',"."},
    {'F',"..-."}, {'G',"--."},  {'H',"...."}, {'I',".."},   {'J',".---"},
    {'K',"-.-"},  {'L',".-.."}, {'M',"--"},   {'N',"-."},   {'O',"---"},
    {'P',".--."}, {'Q',"--.-"}, {'R',".-."},  {'S',"..."},  {'T',"-"},
    {'U',"..-"},  {'V',"...-"}, {'W',".--"},  {'X',"-..-"}, {'Y',"-.--"},
    {'Z',"--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},{'4',"....-"},
    {'5',"....."},{'6',"-...."},{'7',"--..."},{'8',"---.."},{'9',"----."},
    {'.',".-.-.-"},{',',"--..--"},{'?',"..--.."},{'/',"-..-."},
    {'=',"-...-"},{'+',".-.-."},{'-',"-....-"},{':',"---..."},
    {'(',"-.--."},{')',"-.--.-"},{'"',".-..-."},{'\'',".----."},
    {'@',".--.-."},{'!',"-.-.--"},{';',"-.-.-."},{'_',"..--.-"},
};
#define NTAB ((int)(sizeof(k_tab) / sizeof(k_tab[0])))

static const char *code_of(char c)
{
    int i;
    for (i = 0; i < NTAB; i++) { if (k_tab[i].c == c) { return k_tab[i].code; } }
    return 0;
}

/* --- ruido gaussiano reproducible: LCG + Box-Muller -------------------- */
static uint32_t s_rng = 12345u;
/* Con esto a 1, synth() mete golpes cortos dentro de los silencios: es
 * lo que hace un ruido impulsivo en la banda, y lo que el decodificador
 * tiene que aprender a no leer como elementos. */
static int g_chasquidos = 0;
static float g_baches = 0.0f;   /* periodo en s, 0 = sin baches */
static float uni(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)((s_rng >> 8) & 0xFFFFFFu) / 16777216.0f;
}
static float gauss(void)
{
    float u1 = uni(), u2 = uni();
    if (u1 < 1.0e-7f) { u1 = 1.0e-7f; }
    return sqrtf(-2.0f * logf(u1)) * cosf(6.283185307f * u2);
}

/*
 * Proporciones del codigo. Las nominales son 1/3 para punto/raya y
 * 1/3/7 para los huecos. `w` permite estropearlas para imitar a una
 * persona: w.dash es cuanto dura la raya en puntos, w.gap_l el hueco
 * entre letras y w.gap_w el de palabra.
 */
typedef struct { float dash, gap_e, gap_l, gap_w; } ratios_t;
static const ratios_t k_perfecto = { 3.0f, 1.0f, 3.0f, 7.0f };

/*
 * Sintetiza el mensaje. La envolvente de manipulacion se construye
 * primero como un 0/1 y luego se convoluciona con una ventana de Hann:
 * eso da exactamente los flancos de coseno alzado que usa cualquier
 * emisora decente, y evita los chasquidos que un 0/1 crudo mete en todo
 * el espectro -y que aqui falsearian la prueba a favor, porque un
 * flanco infinitamente rapido es mas facil de detectar que uno real.
 */
/*
 * `tono_hz` es el tono que EMITE la estacion, que no tiene por que ser
 * el que el decodificador tiene ajustado. Esa diferencia era el agujero
 * del banco: durante todo el desarrollo se sintetizo siempre en PITCH
 * exacto, o sea que se estaba probando el unico caso que en la radio de
 * verdad no se da nunca. El primer fallo con antena fue justo ese.
 */
static float *synth(const char *text, float wpm, float snr_db,
                    const ratios_t *w, float qrm_db, float tono_hz, size_t *n_out)
{
    float dot_ms = 1200.0f / wpm;
    float lead_ms = 2600.0f, tail_ms = 1500.0f;
    /* primera pasada: duracion total */
    float total_ms = lead_ms + tail_ms;
    const char *p;
    float gap_pending = 0.0f;
    size_t n, i;
    float *env, *out, *tmp;
    int rise_n, k;
    float amp = 1.0f, sigma, qrm_amp;
    double ph = 0.0, qph = 0.0;
    size_t pos;

    for (p = text; *p; p++) {
        const char *c;
        if (*p == ' ') { gap_pending = w->gap_w; continue; }
        c = code_of(*p);
        if (!c) { continue; }
        total_ms += gap_pending * dot_ms;
        for (k = 0; c[k]; k++) {
            if (k) { total_ms += w->gap_e * dot_ms; }
            total_ms += (c[k] == '-' ? w->dash : 1.0f) * dot_ms;
        }
        gap_pending = w->gap_l;
    }

    n = (size_t)(total_ms * 0.001f * FS) + 64u;
    env = (float *)calloc(n, sizeof(float));
    out = (float *)calloc(n, sizeof(float));
    tmp = (float *)calloc(n, sizeof(float));

    /* segunda pasada: pintar la envolvente 0/1 */
    pos = (size_t)(lead_ms * 0.001f * FS);
    gap_pending = 0.0f;
    for (p = text; *p; p++) {
        const char *c;
        if (*p == ' ') { gap_pending = w->gap_w; continue; }
        c = code_of(*p);
        if (!c) { continue; }
        pos += (size_t)(gap_pending * dot_ms * 0.001f * FS);
        for (k = 0; c[k]; k++) {
            size_t len;
            if (k) { pos += (size_t)(w->gap_e * dot_ms * 0.001f * FS); }
            len = (size_t)((c[k] == '-' ? w->dash : 1.0f) * dot_ms * 0.001f * FS);
            for (i = 0; i < len && pos + i < n; i++) { env[pos + i] = 1.0f; }
            pos += len;
        }
        gap_pending = w->gap_l;
    }

    /* flancos: Hann de 2*subida. 5 ms, o un cuarto del punto si el punto
     * es mas corto que eso -a 60 PPM el punto dura 20 ms y un flanco de
     * 5 ms ya seria un cuarto del elemento. */
    {
        float rise_ms = 5.0f;
        if (rise_ms > dot_ms * 0.25f) { rise_ms = dot_ms * 0.25f; }
        rise_n = (int)(rise_ms * 0.001f * FS);
        if (rise_n < 2) { rise_n = 2; }
    }
    {
        int L = 2 * rise_n;
        float sum = 0.0f;
        float *win = (float *)malloc((size_t)L * sizeof(float));
        for (k = 0; k < L; k++) {
            win[k] = 0.5f - 0.5f * cosf(6.283185307f * (float)k / (float)(L - 1));
            sum += win[k];
        }
        for (i = 0; i < n; i++) {
            float acc = 0.0f;
            for (k = 0; k < L; k++) {
                long j = (long)i - (long)k + rise_n;
                if (j >= 0 && (size_t)j < n) { acc += env[(size_t)j] * win[k]; }
            }
            tmp[i] = acc / sum;
        }
        free(win);
    }

    if (g_chasquidos) {
        /* Un golpe de 3 ms cada ~180 ms, siempre dentro de un silencio:
         * demasiado corto para ser un punto a cualquier velocidad
         * razonable, pero mas largo que el suelo absoluto de 7 saltos. */
        /* 22 ms cada 1,2 s. Los dos numeros importan: 22 ms pasan el
         * suelo absoluto de 7 saltos (18,7 ms) pero no llegan a medio
         * punto a 20 PPM (30 ms), que es justo la franja que antes se
         * colaba como E o T. Y 1,2 s de separacion es ruido impulsivo
         * de verdad: mas seguido que eso ya no es un chasquido, es una
         * segunda estacion, y entonces lo que se estaria midiendo es
         * otra cosa. */
        size_t k, paso = (size_t)(1.200f * FS), largo = (size_t)(0.022f * FS);
        for (k = (size_t)(lead_ms * 0.001f * FS); k + largo < n; k += paso) {
            size_t q; int libre = 1;
            for (q = 0; q < largo; q++) { if (tmp[k + q] > 0.05f) { libre = 0; break; } }
            if (libre) { for (q = 0; q < largo; q++) { tmp[k + q] = 0.9f; } }
        }
    }

    if (g_baches) {
        /*
         * Lo contrario del chasquido: un agujero DENTRO de un elemento.
         * Es lo que hace el desvanecimiento rapido y el ruido impulsivo
         * que resta en vez de sumar, y es el caso que faltaba en este
         * banco entero.
         *
         * Mismas dos cifras que el chasquido, por simetria: 22 ms pasan
         * el suelo absoluto de 7 saltos pero no llegan a medio punto a
         * 20 PPM, o sea que caen en la misma franja. La diferencia es
         * donde se meten. Y cada 0,35 s en vez de cada 1,2 porque las
         * marcas ocupan menos de la mitad del tiempo: con 1,2 s casi
         * ninguno caeria dentro de una.
         *
         * El periodo sale de medir, no de elegir. Barrido del error
         * con el mismo texto, antes y despues de la fusion:
         *
         *     bache cada   0,25 s   0,35   0,60   0,90   1,40   2,00
         *     V3.76         71,2 %  66,7   31,8   39,4   13,6    4,5
         *     con fusion    42,4 %  40,9    6,1    9,1    0,0    0,0
         *
         * Se usa 0,9 s: un agujero por segundo es desvanecimiento
         * creible, y ahi la diferencia es de 39 a 9, o sea cuatro
         * veces. El liston se pone en 15 %, que deja margen por los dos
         * lados -el codigo arreglado tiene 1,6 veces de holgura y el
         * anterior falla por 2,6- en vez de pegarse a ninguno de los
         * dos numeros. Mas apretado que 0,6 s ya no lo arregla nadie y
         * la prueba dejaria de medir el arreglo para medir el limite.
         *
         * El agujero solo se abre si el elemento sigue a los dos lados,
         * con margen: si se comiera un borde no seria un bache, seria
         * un elemento mas corto, y eso ya lo mide otra prueba.
         */
        size_t k, paso = (size_t)(g_baches * FS), largo = (size_t)(0.022f * FS);
        size_t margen = (size_t)(0.012f * FS);
        for (k = (size_t)(lead_ms * 0.001f * FS); k + largo + margen < n; k += paso) {
            size_t q; int dentro = 1;
            if (k < margen) { continue; }
            for (q = k - margen; q < k + largo + margen; q++) {
                if (tmp[q] < 0.95f) { dentro = 0; break; }
            }
            if (dentro) { for (q = 0; q < largo; q++) { tmp[k + q] = 0.0f; } }
        }
    }

    /*
     * Ruido. La relacion se define en un ancho de referencia de 500 Hz,
     * que es el filtro de CW tipico, no en los 6 kHz de audio: decir
     * "0 dB" sobre el ancho entero sonaria mucho peor de lo que es y
     * daria una cifra que no se puede comparar con nada.
     *
     *   potencia de senal con la tecla bajada = A^2/2
     *   potencia de ruido en 500 Hz           = sigma^2 * 500/6000
     */
    sigma = amp * sqrtf(6.0f / powf(10.0f, snr_db / 10.0f));
    qrm_amp = (qrm_db > -90.0f) ? amp * powf(10.0f, qrm_db / 20.0f) : 0.0f;

    for (i = 0; i < n; i++) {
        float v = amp * tmp[i] * (float)sin(ph);
        ph += 6.283185307 * (double)tono_hz / (double)FS;
        if (qrm_amp > 0.0f) {
            /* otra estacion 400 Hz mas arriba, con la tecla bajada todo
             * el rato: el peor caso para un detector de un solo bin */
            v += qrm_amp * (float)sin(qph);
            qph += 6.283185307 * (double)(tono_hz + 400.0f) / (double)FS;
        }
        out[i] = v + sigma * gauss();
    }

    free(env);
    free(tmp);
    *n_out = n;
    return out;
}

/* Quita espacios de los extremos y colapsa los dobles, para que la
 * comparacion sea sobre el texto y no sobre el espaciado. */
static void tidy(char *s)
{
    char *r = s, *w = s;
    while (*r == ' ') { r++; }
    while (*r) {
        if (*r == ' ' && (w == s || *(w - 1) == ' ')) { r++; continue; }
        *w++ = *r++;
    }
    while (w > s && *(w - 1) == ' ') { w--; }
    *w = '\0';
}

/* Distancia de edicion, para dar una tasa de error por caracter en vez
 * de un "coincide / no coincide" que no dice nada de cuanto falla. */
static int leven(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b), i, j;
    int *prev = (int *)malloc((size_t)(lb + 1) * sizeof(int));
    int *cur  = (int *)malloc((size_t)(lb + 1) * sizeof(int));
    int res;

    for (j = 0; j <= lb; j++) { prev[j] = j; }
    for (i = 1; i <= la; i++) {
        cur[0] = i;
        for (j = 1; j <= lb; j++) {
            int del = prev[j] + 1, ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + (a[i - 1] != b[j - 1]);
            int m = del < ins ? del : ins;
            cur[j] = m < sub ? m : sub;
        }
        memcpy(prev, cur, (size_t)(lb + 1) * sizeof(int));
    }
    res = prev[lb];
    free(prev); free(cur);
    return res;
}

/* Una pasada completa: sintetiza, mete el audio en bloques de 32 por el
 * decodificador real y recoge lo que sale. */
static void run(const char *text, float wpm, float snr_db, const ratios_t *w,
                float qrm_db, float tono_hz, char *got, size_t got_sz,
                float *wpm_est, float *det_hz)
{
    size_t n, off, g = 0;
    float *buf = synth(text, wpm, snr_db, w, qrm_db, tono_hz, &n);

    cw_init();
    cw_set_pitch_hz(PITCH);
    cw_set_enabled(1u);

    for (off = 0; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
        char c;
        cw_process(buf + off, CW_BLOCK_SAMPLES);
        while (cw_get_char(&c)) {
            if (g + 1 < got_sz) { got[g++] = c; }
        }
    }
    got[g] = '\0';
    *wpm_est = cw_get_wpm();
    if (det_hz) { *det_hz = cw_get_detect_hz(); }
    free(buf);
}

/*
 * Un caso se mide sobre varias realizaciones de ruido distintas y se
 * promedia. Con una sola, la tabla del barrido se llenaba de valores
 * que subian y bajaban sin sentido entre casillas vecinas: no era el
 * decodificador portandose raro, era que cada casilla veia un ruido
 * diferente y una sola tirada no dice nada. La semilla se fija al
 * entrar, asi que la tabla sale igual en cada ejecucion.
 */
#define N_REAL 4

/*
 * Como caso(), pero ignorando el primer caracter de los dos lados. Ver
 * el texto del primer caso de main() para por que ese caracter se mide
 * aparte en vez de exigirlo.
 */
static float caso_cola(const char *text, float wpm, float snr, const ratios_t *w,
                       float qrm, int verboso)
{
    char got[512];
    float est = 0.0f, err = 0.0f, det = 0.0f;
    int r;

    for (r = 0; r < N_REAL; r++) {
        const char *a, *b;
        s_rng = 1000u + (uint32_t)r * 7919u;
        run(text, wpm, snr, w, qrm, PITCH, got, sizeof(got), &est, &det);
        tidy(got);
        a = text + 1;
        b = (got[0] != '\0') ? got + 1 : got;
        err += (float)leven(a, b) / (float)strlen(a);
        if (verboso && r == 0) {
            printf("      esperado: %s\n", text);
            printf("      obtenido: %s\n", got);
            printf("      PPM real %.0f, estimada %.1f\n", (double)wpm, (double)est);
        }
    }
    return err / (float)N_REAL;
}

/* Version general: `tono` es lo que emite la estacion. */
static float caso_tono(const char *text, float wpm, float snr, const ratios_t *w,
                       float qrm, float tono, int verboso)
{
    char got[512];
    float est = 0.0f, err = 0.0f, det = 0.0f;
    int r;

    for (r = 0; r < N_REAL; r++) {
        s_rng = 1000u + (uint32_t)r * 7919u;
        run(text, wpm, snr, w, qrm, tono, got, sizeof(got), &est, &det);
        tidy(got);
        err += (float)leven(text, got) / (float)strlen(text);
        if (verboso && r == 0) {
            printf("      esperado: %s\n", text);
            printf("      obtenido: %s\n", got);
            printf("      PPM real %.0f, estimada %.1f;  tono emitido %.0f Hz,"
                   " enganchado a %.0f Hz\n",
                   (double)wpm, (double)est, (double)tono, (double)det);
        }
    }
    return err / (float)N_REAL;
}

static float caso(const char *text, float wpm, float snr, const ratios_t *w,
                  float qrm, int verboso)
{
    char got[512];
    float est = 0.0f, err = 0.0f, det = 0.0f;
    int r;

    for (r = 0; r < N_REAL; r++) {
        s_rng = 1000u + (uint32_t)r * 7919u;
        run(text, wpm, snr, w, qrm, PITCH, got, sizeof(got), &est, &det);
        tidy(got);
        err += (float)leven(text, got) / (float)strlen(text);
        if (verboso && r == 0) {
            printf("      esperado: %s\n", text);
            printf("      obtenido: %s\n", got);
            printf("      PPM real %.0f, estimada %.1f\n", (double)wpm, (double)est);
        }
    }
    return err / (float)N_REAL;
}

/*
 * EL TOPE.
 *
 * Con el ruido que fabrica ESTE banco salen 0 caracteres en sesenta
 * segundos, y con el fichero de ruido blanco de numpy a sigma 0,1 metido
 * por out/cwaire salian 41 hasta V3.76. Los dos eran "ruido" y daban
 * resultados opuestos, o sea que el modelo de ruido de aqui no es el del
 * aire y aquel cero no demostraba nada.
 *
 * Desde V3.77 el fichero de ruido blanco tambien da 0, y la FSK de
 * 8.565 kHz pasa de 9 caracteres a 0. El tope baja de 50 a 10: sigue
 * estando para que no EMPEORE, pero ahora por encima de una medida que
 * vale, no de una que se contradecia con la de al lado.
 *
 * Lo que SIGUE sin estar resuelto es la señal debil: la grabacion de SVO
 * hundida a 12 dB o menos no sale ni antes ni ahora. Eso no es la puerta,
 * es el detector, y es otro problema.
 */
#define PASO_BACHE 0.900f
#define CW_RUIDO_TOPE 10

int main_viejo(void)
{
    static const char *k_msg = "CQ CQ DE EA4ABC K";
    static const char *k_largo =
        "THE QUICK BROWN FOX 0123456789 DE EA4ABC/P RST 599 QTH MADRID 73 =";
    static const float k_wpm[] = { 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f };
    static const float k_snr[] = { 20.0f, 12.0f, 8.0f, 5.0f, 3.0f, 0.0f, -3.0f };
    static const float k_qrm[] = { 0.0f, 3.0f, 6.0f, 10.0f };
    static const float k_desvio[] = { -700.0f, -500.0f, -350.0f, -250.0f, 0.0f, 250.0f, 350.0f, 500.0f, 700.0f };
    int i, j, fallos = 0;

    printf("\n=== CW: mensaje corto, sin ruido, 20 PPM ===\n");
    printf("      Se exige exacto A PARTIR DEL SEGUNDO caracter. El primero\n");
    printf("      sale mal casi siempre y no es un fallo que se pueda quitar:\n");
    printf("      el umbral se calcula a partir del nivel de la senal, o sea\n");
    printf("      que el primer elemento de una transmision es justo el que\n");
    printf("      define el nivel con el que habria que haberlo medido. Un\n");
    printf("      decodificador causal no puede salir de ahi, y un operador\n");
    printf("      humano tampoco: por eso se pide repetir el indicativo.\n");
    {
        float e = caso_cola(k_msg, 20.0f, 40.0f, &k_perfecto, -99.0f, 1);
        printf("      error a partir del 2o caracter: %.1f %%\n", (double)(e * 100.0f));
        if (e > 0.0f) { fallos++; printf("      *** FALLA: sin ruido el resto tiene que ser exacto\n"); }
    }

    printf("\n=== CW: alfabeto, digitos y puntuacion, 25 PPM, 20 dB ===\n");
    {
        float e = caso(k_largo, 25.0f, 20.0f, &k_perfecto, -99.0f, 1);
        printf("      error %.1f %%\n", (double)(e * 100.0f));
        if (e > 0.05f) { fallos++; printf("      *** FALLA: se esperaba <5 %%\n"); }
    }

    printf("\n=== CW: barrido velocidad x senal-ruido (error por caracter, %%) ===\n");
    printf("      La estimacion de velocidad arranca siempre en 20 PPM y se\n");
    printf("      busca la vida sola; el error incluye el enganche inicial.\n\n");
    printf("      PPM  ");
    for (j = 0; j < (int)(sizeof(k_snr) / sizeof(k_snr[0])); j++) {
        printf("%6.0f", (double)k_snr[j]);
    }
    printf("   dB (en 500 Hz)\n");
    for (i = 0; i < (int)(sizeof(k_wpm) / sizeof(k_wpm[0])); i++) {
        printf("      %3.0f  ", (double)k_wpm[i]);
        for (j = 0; j < (int)(sizeof(k_snr) / sizeof(k_snr[0])); j++) {
            float e = caso(k_largo, k_wpm[i], k_snr[j], &k_perfecto, -99.0f, 0);
            printf("%6.0f", (double)(e * 100.0f));
        }
        printf("\n");
    }

    printf("\n=== CW: proporciones de persona, no de maquina ===\n");
    {
        /* raya un poco corta, huecos de letra y de palabra estirados:
         * el defecto tipico de quien manda con manipulador de palanca */
        static const ratios_t k_humano = { 2.6f, 1.15f, 4.2f, 9.0f };
        float e = caso(k_largo, 18.0f, 15.0f, &k_humano, -99.0f, 1);
        printf("      error %.1f %%\n", (double)(e * 100.0f));
        if (e > 0.10f) { fallos++; printf("      *** FALLA: se esperaba <10 %%\n"); }
    }

    printf("\n=== CW: chasquidos de ruido en las pausas ===\n");
    printf("      Golpes cortos metidos a mano DENTRO de los silencios, que\n");
    printf("      es lo que en el aire produce letras sueltas de un solo\n");
    printf("      elemento (E y T) que nadie ha mandado.\n\n");
    printf("      Lo que el minimo relativo ARREGLA son esas letras: con la\n");
    printf("      regla quitada salen dos E de mas (\"01234569E DE\" y\n");
    printf("      \"RSTE599\"), con ella puesta no sale ninguna.\n\n");
    printf("      Lo que NO arregla, y por eso el listón esta en 25 %% y no\n");
    printf("      en 2, son los golpes que caen PEGADOS a un elemento de\n");
    printf("      verdad: ahi no hay longitud minima que valga, porque el\n");
    printf("      golpe y el elemento llegan como uno solo. De ahi salen\n");
    printf("      \"KUICK\" y \"BRN\", y para eso haria falta un filtro en el\n");
    printf("      detector, no una regla de duracion.\n\n");
    {
        char got[512]; float est, det; float e;
        s_rng = 4242u;
        g_chasquidos = 1;
        run(k_largo, 20.0f, 12.0f, &k_perfecto, -99.0f, PITCH, got, sizeof(got), &est, &det);
        g_chasquidos = 0;
        tidy(got);
        e = (float)leven(k_largo, got) / (float)strlen(k_largo);
        printf("      obtenido: %s\n", got);
        printf("      error %.1f %%  (se admite hasta 25 %%)\n", (double)(e * 100.0f));
        if (e > 0.25f) { fallos++; printf("      *** FALLA\n"); }
    }

    printf("\n=== CW: baches dentro de los elementos ===\n");
    printf("      Agujeros cortos metidos a mano DENTRO de las marcas, que\n");
    printf("      es lo que hace el desvanecimiento rapido. Es el caso\n");
    printf("      simetrico del anterior y NO ESTABA en el banco.\n\n");
    printf("      Importa porque un bache no quita un elemento: lo PARTE.\n");
    printf("      Una raya con un agujero en medio llega como dos marcas\n");
    printf("      de media raya, que se clasifican como dos puntos: donde\n");
    printf("      ponia T sale I, y la velocidad estimada se va al doble.\n");
    printf("      Esa es la basura de una sola letra que se ve en antena\n");
    printf("      alrededor de una señal que por lo demas se entiende.\n\n");
    {
        char got[512]; float est, det; float e;
        s_rng = 4242u;
        g_baches = PASO_BACHE;
        run(k_largo, 20.0f, 12.0f, &k_perfecto, -99.0f, PITCH, got, sizeof(got), &est, &det);
        g_baches = 0.0f;
        tidy(got);
        e = (float)leven(k_largo, got) / (float)strlen(k_largo);
        printf("      obtenido: %s\n", got);
        printf("      PPM real 20, estimada %.1f\n", (double)est);
        printf("      error %.1f %%  (se admite hasta 15 %%)\n", (double)(e * 100.0f));
        if (e > 0.15f) { fallos++; printf("      *** FALLA\n"); }
    }

    printf("\n=== CW: la estacion NO emite en el tono ajustado ===\n");
    printf("      El decodificador tiene 700 Hz puestos y la estacion emite\n");
    printf("      donde le toca. Este caso NO EXISTIA en el banco: se\n");
    printf("      sintetizaba siempre en 700 Hz exactos, o sea que se estaba\n");
    printf("      probando el unico caso que con antena no se da nunca. El\n");
    printf("      primer fallo en la radio fue justo este.\n\n");
    printf("      desvio del tono:  ");
    for (j = 0; j < 9; j++) { printf("%7d", (int)k_desvio[j]); }
    printf("   Hz\n");
    printf("      error:            ");
    for (j = 0; j < 9; j++) {
        float e = caso_tono(k_largo, 20.0f, 12.0f, &k_perfecto, -99.0f,
                            PITCH + k_desvio[j], 0);
        printf("%6.0f%%", (double)(e * 100.0f));
        /*
         * EL LISTON, 07/10/2026. Antes se exigia +-250 Hz, que es el ancho
         * del peine de sondas, y a 300 se daba por hecho que fallaba. No
         * fallaba: con nueve sondas el error a 300 era del 1%, o sea que
         * NADIE habia medido el limite y el numero del comentario era una
         * suposicion sobre el propio codigo.
         *
         * Medido de verdad, barriendo hasta 700:
         *
         *     sondas   peine     desvio que decodifica
         *        9    +-250 Hz        +-350 Hz
         *       13    +-375 Hz        +-500 Hz
         *       17    +-500 Hz        +-500 Hz (a 700 falla el 55%)
         *
         * El peine no es el limite porque cada sonda es un Goertzel de 32
         * muestras a 12 kHz, o sea un bin de 375 Hz de ancho. Con trece
         * sondas se exige +-500, que es lo medido, y 700 queda fuera a
         * proposito para que el limite siga estando MEDIDO y no supuesto.
         */
        if (k_desvio[j] >= -500.0f && k_desvio[j] <= 500.0f && e > 0.10f) {
            fallos++;
            printf("  *** FALLA a %d Hz\n", (int)k_desvio[j]);
        }
    }
    printf("\n      (el peine es +-375 Hz y el limite util +-500, medido.\n");
    printf("       700 queda fuera a proposito.)\n");
    caso_tono(k_largo, 20.0f, 12.0f, &k_perfecto, -99.0f, PITCH + 190.0f, 1);

    printf("\n=== CW: interferencia de otra estacion ===\n");
    printf("      Portadora continua a +400 Hz. El detector es un solo bin\n");
    printf("      de 64 muestras con ventana rectangular, o sea que a 400 Hz\n");
    printf("      solo la rechaza unos 24 dB: lo que llega no es un nivel\n");
    printf("      constante sino un BATIDO, y el batido es lo que puede\n");
    printf("      mover la tecla. Por eso este caso mide el umbral, no el\n");
    printf("      filtro.\n\n");
    printf("      nivel de la interferencia:");
    { int d; for (d = 0; d < 4; d++) { printf("%7d", (int)k_qrm[d]); } printf("   dB\n"); }
    printf("\n      Lo que se exige aqui NO es que decodifique, es que no\n");
    printf("      MIENTA. Con una portadora al mismo nivel que la senal, la\n");
    printf("      respuesta correcta es callarse: un hueco se ve, una letra\n");
    printf("      inventada no. Asi que se mide de lo que SALE cuanto es\n");
    printf("      correcto, y se admite tanto decodificar bien como no\n");
    printf("      decodificar nada.\n\n");
    printf("      nivel:       ");
    for (j = 0; j < 4; j++) { printf("%9d", (int)k_qrm[j]); }
    printf("   dB\n");
    printf("      resultado:   ");
    for (j = 0; j < 4; j++) {
        char g[512]; float est, det; float e;
        size_t salida;

        s_rng = 1000u;
        run(k_largo, 20.0f, 20.0f, &k_perfecto, k_qrm[j], PITCH, g, sizeof(g), &est, &det);
        tidy(g);
        salida = strlen(g);
        e = (float)leven(k_largo, g) / (float)strlen(k_largo);

        if (salida * 10U < strlen(k_largo)) {
            printf("%9s", "callado");          /* no ha dicho nada: correcto */
        } else if (e <= 0.20f) {
            printf("%8.0f%%", (double)(e * 100.0f));
        } else {
            printf("%9s", "MIENTE");
            fallos++;
        }
    }
    printf("\n      enganche:    ");
    for (j = 0; j < 4; j++) {
        char g[512]; float est, det;
        s_rng = 1000u;
        run(k_largo, 20.0f, 20.0f, &k_perfecto, k_qrm[j], PITCH, g, sizeof(g), &est, &det);
        printf("%7.0fHz", (double)det);
    }
    printf("\n");
    printf("      (el enganche se queda en 700 Hz en los cuatro casos: el\n");
    printf("       banco no se deja llevar por la portadora, aunque sea mas\n");
    printf("       fuerte, porque puntua la manipulacion y no el nivel)\n");
    printf("      LIMITE: a +20 dB la otra estacion tapa esta del todo, y\n");
    printf("      no es cosa del umbral sino del filtro: por el lobulo\n");
    printf("      lateral de -24 dB llega 4 dB por encima de la deseada.\n");
    printf("      La salida correcta ahi es estrechar el filtro de la radio\n");
    printf("      o moverse, no pedirle mas al decodificador.\n");

    /*
     * SOLO RUIDO, Y DURANTE UN RATO LARGO - 06/10/2026.
     *
     * Pedido por el usuario.
     *
     * Esta prueba existia y pasaba en verde... con VEINTE SEGUNDOS de
     * ruido. Con sesenta salian 41 caracteres. O sea que la prueba no
     * estaba midiendo si el decodificador calla: estaba midiendo si calla
     * lo bastante poco rato como para que no le diera tiempo a hablar.
     *
     * Y el usuario no escucha veinte segundos: escucha cuarenta minutos. La
     * diferencia entre las dos cifras ES el fallo que el reporto.
     *
     * Se queda en sesenta segundos y con un tope de caracteres en vez de
     * exigir cero: a dia de hoy NO se consigue cero sin matar tambien las
     * señales debiles -medido: subiendo CW_GOOD_N de 8 a 16 el ruido baja
     * de 41 a 9 caracteres pero la grabacion real de SVO deja de salir
     * entera-. El tope deja el problema MEDIDO y a la vista en vez de
     * taparlo, que es lo que hacia la prueba de veinte segundos.
     */
    printf("\n=== CW: solo ruido durante un minuto ===\n");
    {
        char got[2048]; float est;
        int n;
        run("", 60.0f, 0.0f, &k_perfecto, -99.0f, PITCH, got, sizeof(got), &est, 0);
        tidy(got);
        n = (int)strlen(got);
        printf("      salida: \"%.60s\" (%d caracteres en 60 s)\n", got, n);
        if (n > CW_RUIDO_TOPE) {
            fallos++;
            printf("      *** FALLA: mas de %d caracteres de ruido en un "
                   "minuto\n", CW_RUIDO_TOPE);
        }
    }

    printf("\n%s\n", fallos ? "HAY FALLOS" : "todo correcto");
    return fallos ? 1 : 0;
}

/* ---- medida: un chasquido corto y lo que le hace a la estimacion ---- */
int main(int argc, char **argv)
{
    static const char *k_t =
        "THE QUICK BROWN FOX 0123456789 DE EA4ABC/P RST 599 QTH MADRID 73 =";
    size_t n, off, g;
    float *buf;
    char got[2048];
    float est, det;
    int chasquidos = (argc > 1) ? atoi(argv[1]) : 1;
    int c;

    buf = synth(k_t, 28.0f, 20.0f, &k_perfecto, -99.0f, PITCH, &n);
    /*
     * Chasquidos PEGADOS a un elemento, no en mitad del silencio: es lo
     * que hace el QRM de verdad, y es lo unico que llega al estimador.
     * Uno suelto en medio de una pausa lo absorbe el filtro de tramos.
     */
    {
        /* busca los flancos de bajada y mete el chasquido justo despues */
        size_t p, puestos = 0;
        float prev = 0.0f;
        for (p = (size_t)(FS * 3.0); p + 2000 < n && (int)puestos < chasquidos; p++) {
            float a = fabsf(buf[p]);
            if (prev > 0.25f && a < 0.05f) {
                size_t k, L = (size_t)(0.025 * FS);
                size_t q = p + (size_t)(0.012 * FS);
                double ph = 0.0;
                for (k = 0; k < L; k++) {
                    ph += 6.283185307 * PITCH / FS;
                    buf[q + k] += 0.9f * (float)sin(ph);
                }
                puestos++;
                p += (size_t)(FS * 0.8);
            }
            prev = (a > prev) ? a : (prev * 0.999f);
        }
    }
    cw_init(); cw_set_pitch_hz(PITCH); cw_set_enabled(1u);
    g = 0;
    for (off = 0; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
        char ch;
        cw_process(buf + off, CW_BLOCK_SAMPLES);
        while (cw_get_char(&ch)) { if (g + 1 < sizeof got) got[g++] = ch; }
    }
    got[g] = '\0';
    est = cw_get_wpm(); det = cw_get_detect_hz();
    printf("  %d chasquido%s de 25 ms: %.1f ppm (son 28), error %.0f%%  [%.40s]\n",
           chasquidos, chasquidos == 1 ? "" : "s", (double)est,
           100.0 * (double)leven(k_t, got) / (double)strlen(k_t), got);
    free(buf);
    (void)det;
    return 0;
}

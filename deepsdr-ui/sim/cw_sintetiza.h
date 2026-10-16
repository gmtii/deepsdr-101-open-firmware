/*
 * EL SINTETIZADOR DE MORSE DEL SIMULADOR, EN UN SOLO SITIO.
 *
 * Esto estaba DENTRO de sim/cwtest.c, y el 09/10/2026 hizo falta en un
 * segundo banco -sim/cw_vara.c, el que mide cuanto se mueve la estimacion
 * de velocidad-. Copiarlo habria sido la septima copia a mano de este
 * proyecto, y las seis anteriores se separaron todas: por eso se saca aqui
 * en vez de duplicarse.
 *
 * Lo que vive aqui es SOLO la señal de entrada: la tabla de Morse escrita
 * a mano desde la UIT-R M.1677, el ruido gaussiano reproducible y el
 * sintetizador. Nada de esto es codigo del firmware. A partir del primer
 * sample, lo que corre en los dos bancos es cw.c de verdad.
 *
 * La tabla SIGUE SIN LEERSE de cw.c, y eso es deliberado: si el banco
 * usara la tabla del decodificador, un error en ella se decodificaria
 * perfectamente y la prueba pasaria. Dos copias independientes que
 * coinciden son una comprobacion; una copia comparada consigo misma no es
 * nada. Lo que se ha quitado es la copia entre BANCOS, no esa.
 */
#ifndef CW_SINTETIZA_H
#define CW_SINTETIZA_H

/*
 * ES UNA CABECERA CON EL CODIGO DENTRO, Y NO POR PEREZA. El fichero que
 * dice que ficheros compila cada banco -el Makefile del simulador- es uno
 * de los que el puente del disco NO deja escribir, asi que un .c nuevo
 * obligaria otra vez a pedirle al usuario que tocara un Makefile a mano.
 * Metido aqui como `static`, los dos bancos lo cogen con un #include y no
 * hay nada que añadir a ninguna regla. Ver GNUmakefile en el firmware para
 * la otra mitad de esta misma piedra.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS       12000.0f
#define PITCH     700.0f

/*
 * Proporciones del codigo. Las nominales son 1/3 para punto/raya y 1/3/7
 * para los huecos. `w` permite estropearlas para imitar a una persona:
 * w.dash es cuanto dura la raya en puntos, w.gap_l el hueco entre letras
 * y w.gap_w el de palabra.
 */
typedef struct { float dash, gap_e, gap_l, gap_w; } ratios_t;

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
static uint32_t g_rng = 12345u;
/* Con esto a 1, synth() mete golpes cortos dentro de los silencios: es
 * lo que hace un ruido impulsivo en la banda, y lo que el decodificador
 * tiene que aprender a no leer como elementos. */
static int g_chasquidos = 0;
static float g_baches = 0.0f;   /* periodo en s, 0 = sin baches */
static float uni(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFFFFu) / 16777216.0f;
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

#endif /* CW_SINTETIZA_H */

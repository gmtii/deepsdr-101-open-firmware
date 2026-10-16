/*
 * EL BOTON DE APRENDIZAJE DEL CW, Y LO QUE CUESTA CADA LECTURA.
 *
 * Pedido por el usuario, 08/10/2026.
 *
 * No hay una buena y una mala: hay dos suposiciones sobre quien teclea.
 *
 *   APAGADO: la raya dura TRES puntos. Es la norma y es lo que manda
 *       casi todo el mundo, asi que suponerlo es informacion de verdad
 *       sobre la señal y el que supone gana al que mide.
 *
 *   ENCENDIDO: no se supone nada. Se mide donde caen los cortos y donde
 *       los largos y la frontera va entre los dos montones.
 *
 * Este banco mide las tres cosas que tienen que ser verdad para que el
 * boton valga de algo:
 *
 *   1. Que APAGADO no cambie NADA. Si el boton apagado mueve una sola
 *      letra respecto al cw.c de siempre, el boton no es un boton: es un
 *      cambio de comportamiento con una etiqueta encima.
 *
 *   2. Que ENCENDIDO lea a 4XZ, que apagado no se lee.
 *
 *   3. Que ENCENDIDO siga CALLANDO con ruido. Esta es la importante. El
 *      modelo de "cada elemento cerca de 1 o de 3 puntos" es parte de lo
 *      que impide que el decodificador escriba sobre ruido, y aqui se
 *      cambia por "cada elemento cerca de su propio monton". Lo que
 *      sigue defendiendo es que los dos montones tienen que estar
 *      SEPARADOS, y el ruido -duraciones exponenciales, un solo monton-
 *      no sabe separarlos. Si eso no fuera cierto, el boton seria una
 *      fabrica de basura con cara de texto y habria que tirarlo.
 *
 * Y mide tambien LO QUE CUESTA, que es lo que justifica que el boton
 * exista en vez de ir encendido siempre.
 */
#include "cw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-50s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

#define FS 12000.0f

/* --- lee un .f32 crudo, que es lo que come cw_process() -------------- */
static float *lee_f32(const char *ruta, size_t *n_out)
{
    FILE *f = fopen(ruta, "rb");
    long n;
    float *x;
    if (!f) { return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    x = (float *)malloc((size_t)n);
    if (!x) { fclose(f); return 0; }
    if (fread(x, 1, (size_t)n, f) != (size_t)n) { free(x); fclose(f); return 0; }
    fclose(f);
    *n_out = (size_t)n / sizeof(float);
    return x;
}

static void pasa(const float *x, size_t n, float tono, uint8_t aprende,
                 char *got, size_t got_sz)
{
    size_t off, g = 0;
    cw_init();
    cw_set_pitch_hz(tono);
    cw_set_aprende(aprende);
    cw_set_enabled(1u);
    for (off = 0; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
        char c;
        cw_process(x + off, CW_BLOCK_SAMPLES);
        while (cw_get_char(&c)) { if (g + 1 < got_sz) { got[g++] = c; } }
    }
    got[g] = '\0';
}

/* cuantas veces aparece `aguja` dentro de `pajar` */
static int cuenta(const char *pajar, const char *aguja)
{
    int n = 0;
    const char *p = pajar;
    while ((p = strstr(p, aguja)) != 0) { n++; p++; }
    return n;
}

static uint32_t sem = 20261008UL;
static float ruido_blanco(void)
{
    sem = (sem * 1103515245UL) + 12345UL;
    return ((float)((sem >> 9) & 0xFFFFU) / 32768.0f) - 1.0f;
}

/*
 * RUIDO DE BANDA, QUE NO ES RUIDO BLANCO - 08/10/2026.
 *
 * El ruido blanco es demasiado facil: sus duraciones son exponenciales
 * de un solo monton y el decodificador lo tumba sin despeinarse. Lo que
 * hay de verdad en 4 MHz son chasquidos estaticos -impulsos de duracion
 * variable, que SI pueden formar dos montones separados por casualidad-
 * y desvanecimiento lento que mueve el umbral.
 *
 * Importa porque el aprendizaje relaja justo la condicion que mira si
 * cada elemento cae donde debe, y hay que comprobarlo contra el ruido
 * dificil y no contra el facil.
 */
static void ruido_de_banda(float *x, size_t n)
{
    size_t i, k;
    for (i = 0; i < n; i++) { x[i] = 0.25f * ruido_blanco(); }
    for (k = 0; k < (n / 4000u); k++) {                 /* ~3 chasquidos/s */
        size_t p = (size_t)((double)(n - 600u) * (double)((ruido_blanco() + 1.0f) * 0.5f));
        size_t L = (size_t)(30.0f + 370.0f * (ruido_blanco() + 1.0f) * 0.5f);
        float  a = 0.5f + 2.5f * (ruido_blanco() + 1.0f) * 0.5f;
        float  f = 600.0f + 300.0f * (ruido_blanco() + 1.0f) * 0.5f;
        size_t j;
        if (p + L >= n) { continue; }
        for (j = 0; j < L; j++) {
            x[p + j] += a * expf(-(float)j / ((float)L / 3.0f))
                      * sinf(6.283185307f * f * (float)j / FS);
        }
    }
    for (i = 0; i < n; i++) {                            /* desvanecimiento */
        x[i] *= 1.0f + 0.6f * sinf(6.283185307f * (float)i / (FS * 7.0f));
    }
}

int main(int argc, char **argv)
{
    char apagado[8192], encendido[8192];
    char det[200];
    size_t n = 0;
    float *x;

    printf("\nEL BOTON DE APRENDIZAJE DEL CW\n");
    printf("==============================\n\n");

    if (argc < 2) { printf("  uso: %s cw4xz_4330b.f32\n", argv[0]); return 2; }
    x = lee_f32(argv[1], &n);
    if (!x) { printf("  no puedo abrir %s\n", argv[1]); return 2; }
    printf("  %s: %.1f s\n", argv[1], (double)n / 12000.0);
    printf("  es 4XZ, la naval israeli de Haifa, mandando VVV DE 4XZ en bucle.\n");
    printf("  manda los puntos de dos tamanos y las rayas de dos tamanos:\n");
    printf("  40 y 80 ms los unos, 160 y 200 los otros. Fuera de norma.\n\n");

    pasa(x, n, 700.0f, 0u, apagado,   sizeof apagado);
    pasa(x, n, 700.0f, 1u, encendido, sizeof encendido);

    printf("  apagado:   %.76s\n", apagado);
    printf("  encendido: %.76s\n\n", encendido);

    /*
     * LO PRIMERO, Y ES LO QUE MAS IMPORTA: que APAGADO no cambie nada.
     *
     * Un boton que apagado ya altera el comportamiento no es un boton,
     * es un cambio con una etiqueta encima. Se comprueba contra la
     * salida EXACTA que daba el cw.c de antes de existir el boton sobre
     * esta misma grabacion, copiada aqui tal cual. Si alguien toca el
     * camino de siempre, esto se pone rojo aunque el resto siga verde.
     */
    {
        static const char *k_antes =
            "UERTX GE ?XTD 4XNET PET EEA J TAE 3XTS TM T ZT IET WV ";
        di("apagado da exactamente lo que daba antes del boton",
           strcmp(apagado, k_antes) == 0, (strcmp(apagado, k_antes) == 0) ? 0 : apagado);
    }
    {
        int a = cuenta(apagado, "4XZ"), b = cuenta(encendido, "4XZ");
        snprintf(det, sizeof det, "el indicativo sale %d %s apagado y %d encendido",
                 a, (a == 1) ? "vez" : "veces", b);
        di("encendido saca el indicativo y apagado no", (b > a) && (b >= 2), det);
    }
    {
        int a = cuenta(apagado, "VVV DE"), b = cuenta(encendido, "VVV DE");
        snprintf(det, sizeof det, "\"VVV DE\" sale %d %s apagado y %d encendido",
                 a, (a == 1) ? "vez" : "veces", b);
        di("y la llamada entera tambien", b > a, det);
    }

    /* --- la otra grabacion, la fuerte --------------------------------- */
    if (argc > 2) {
        size_t n2 = 0;
        float *x2 = lee_f32(argv[2], &n2);
        if (x2) {
            char a2[8192], e2[8192];
            printf("\n  y la otra grabacion del mismo dia, mas fuerte (%.1f s):\n",
                   (double)n2 / 12000.0);
            pasa(x2, n2, 979.0f, 0u, a2, sizeof a2);
            pasa(x2, n2, 979.0f, 1u, e2, sizeof e2);
            printf("    apagado:   %.70s\n", a2[0] ? a2 : "(nada)");
            printf("    encendido: %.70s\n", e2);
            snprintf(det, sizeof det, "%u caracteres apagado, %u encendido",
                     (unsigned)strlen(a2), (unsigned)strlen(e2));
            di("tambien la lee, y apagado no sale nada",
               (strstr(e2, "VVV DE 4XZ") != 0) && (strlen(a2) == 0u), det);
            free(x2);
        }
    }

    /* --- LO IMPORTANTE: que encendido siga callando con ruido ---------- */
    printf("\n  un minuto de ruido blanco, con el boton ENCENDIDO:\n");
    {
        size_t m = (size_t)(60.0f * FS), i;
        float *r = (float *)malloc(m * sizeof *r);
        char g[4096];
        for (i = 0; i < m; i++) { r[i] = 0.3f * ruido_blanco(); }
        pasa(r, m, 700.0f, 1u, g, sizeof g);
        snprintf(det, sizeof det, "%u caracteres en 60 s", (unsigned)strlen(g));
        di("no escribe NADA sobre ruido blanco", strlen(g) == 0u, det);
        if (strlen(g) != 0u) { printf("      solto: %.70s\n", g); }
        free(r);
    }
    printf("\n  y 90 s de ruido de BANDA, que es el dificil:\n");
    {
        size_t m = (size_t)(90.0f * FS);
        float *r = (float *)malloc(m * sizeof *r);
        char g[4096];
        ruido_de_banda(r, m);
        pasa(r, m, 700.0f, 1u, g, sizeof g);
        snprintf(det, sizeof det, "%u caracteres en 90 s", (unsigned)strlen(g));
        di("tampoco sobre chasquidos y desvanecimiento", strlen(g) == 0u, det);
        if (strlen(g) != 0u) { printf("      solto: %.70s\n", g); }
        free(r);
    }

    free(x);
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}

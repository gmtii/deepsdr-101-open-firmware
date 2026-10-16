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
#include "cw_sintetiza.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
        g_rng = 1000u + (uint32_t)r * 7919u;
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
        g_rng = 1000u + (uint32_t)r * 7919u;
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
        g_rng = 1000u + (uint32_t)r * 7919u;
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

int main(void)
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
        g_rng = 4242u;
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
        g_rng = 4242u;
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

        g_rng = 1000u;
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
        g_rng = 1000u;
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

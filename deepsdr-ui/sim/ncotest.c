/*
 * BANCO DEL NCO (User/nco.c).
 *
 * Tres cosas, y las tres son numeros medidos, no opiniones:
 *
 *  1. QUE SEA EL MISMO. A Fs/4 exactos, el NCO tiene que producir la MISMA
 *     secuencia que la rotacion de signos que ya lleva demod_am.c (1, -j,
 *     -1, +j). Si no, el dia que se encienda el NCO las bandas laterales
 *     salen al reves y nadie sabe por que. Esta es la prueba que ata lo
 *     nuevo a lo viejo.
 *
 *  2. QUE ESTE DONDE DICE. Error de frecuencia despues de redondear al
 *     incremento entero, y que la fase NO se vaya acumulando a lo largo de
 *     millones de muestras.
 *
 *  3. QUE ESTE LIMPIO. Nivel del peor espurio (SFDR) con y sin
 *     interpolacion. El numero de la interpolacion es el que justifica
 *     pagar dos multiplicaciones por muestra; si no se midiera, seria fe.
 */
#include "nco.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define FS 96000.0f
static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    if (!ok) { fallos++; }
    printf("  %-58s %s\n", que, ok ? "ok" : "*** MAL ***");
}

/* --- 1. identico a la rotacion de signos a Fs/4 ------------------------- */
static void prueba_fs4(void)
{
    nco_t o;
    float i[64], q[64], ri[64], rq[64];
    float peor = 0.0f;
    int n;

    printf("\n1. a Fs/4, el NCO contra la rotacion de signos de demod_am.c\n");
    for (n = 0; n < 64; n++) {
        i[n] = ri[n] = (float)sin(0.37 * n + 0.1);
        q[n] = rq[n] = (float)cos(0.11 * n - 0.4);
    }
    /* la referencia: lo que hace demod_am.c hoy, copiado tal cual */
    for (n = 0; n < 64; n += 4) {
        float h1, h2;
        h1 =  rq[n + 1]; h2 = -ri[n + 1]; ri[n + 1] = h1; rq[n + 1] = h2;
        h1 = -ri[n + 2]; h2 = -rq[n + 2]; ri[n + 2] = h1; rq[n + 2] = h2;
        h1 = -rq[n + 3]; h2 =  ri[n + 3]; ri[n + 3] = h1; rq[n + 3] = h2;
    }
    nco_fase_cero(&o);
    nco_freq(&o, FS / 4.0f, FS);
    nco_mezcla(&o, i, q, 64);
    for (n = 0; n < 64; n++) {
        float d = fabsf(i[n] - ri[n]);
        if (d > peor) { peor = d; }
        d = fabsf(q[n] - rq[n]);
        if (d > peor) { peor = d; }
    }
    printf("    peor diferencia muestra a muestra: %.3e\n", peor);
    comprueba(peor < 1e-6f, "el NCO a Fs/4 ES la rotacion de signos");
}

/* --- 2. frecuencia y deriva de fase ------------------------------------ */
static void prueba_freq(void)
{
    int fallos_antes;
    /* 57 kHz va con Fs = 192000, que es donde vive de verdad (la FM ancha):
     * a 96 kHz, 57 kHz esta por encima de Nyquist y lo que sale es su alias,
     * -39 kHz. No es un fallo del NCO, es que esa frecuencia no cabe ahi -
     * y conviene que el banco lo diga en voz alta, porque el RDS depende de
     * eso y confundirse sale caro. */
    static const struct { float hz; float fs; } k_hz[] = {
        {     10.0f,  96000.0f }, {   1000.0f,  96000.0f },
        { 12345.678f, 96000.0f }, {  24000.0f,  96000.0f },
        { -24000.0f,  96000.0f }, {  47999.0f,  96000.0f },
        {  57000.0f, 192000.0f }, { -57000.0f, 192000.0f },
    };
    nco_t o;
    unsigned k;

    printf("\n2. frecuencia pedida contra frecuencia real\n");
    fallos_antes = fallos;
    for (k = 0; k < sizeof(k_hz) / sizeof(k_hz[0]); k++) {
        float real, err;
        nco_freq(&o, k_hz[k].hz, k_hz[k].fs);
        real = nco_freq_real(&o, k_hz[k].fs);
        err = fabsf(real - k_hz[k].hz);
        printf("    Fs %6.0f   %10.3f Hz  ->  %14.8f Hz   error %.2e Hz\n",
               k_hz[k].fs, k_hz[k].hz, real, err);
        if (err > 1e-4f) { fallos++; }
    }
    /*
     * EL VEREDICTO MIRA EL CONTADOR, no la constante 1.
     *
     * *** 28/09/2026. Aqui ponia `comprueba(1, ...)`. El bucle de arriba SI
     * sumaba a `fallos`, o sea que el programa acababa devolviendo 1 y la
     * suite se paraba; pero el renglon que lee una persona decia "ok"
     * pasara lo que pasara. Comprobado compilandolo contra un nco.c con 5 Hz
     * de error metidos a mano: los ocho casos salian con "error 5.00e+00 Hz"
     * y debajo ponia "ok".
     *
     * Es la tercera vez que aparece esta forma en el proyecto -ver
     * sim/jtty_aire.c y sim/mapa_pant.c, que ya la cazaron y la dejaron
     * escrita-, asi que aqui va otra vez: un veredicto que no puede decir
     * MAL es peor que no tenerlo, porque ocupa el sitio del que si podria.
     */
    comprueba(fallos == fallos_antes, "todas por debajo de 0,0001 Hz de error");

    /* deriva: 10 millones de muestras (104 s a 96 kHz) y mirar la fase */
    {
        double esperada, real_, err;
        long n, N = 10000000L;
        float si, so;
        nco_fase_cero(&o);
        nco_freq(&o, 1000.0f, FS);
        for (n = 0; n < N; n++) { nco_paso(&o, &si, &so); }
        esperada = fmod((double)N * (double)o.inc, 4294967296.0);
        real_ = (double)o.fase;
        err = fabs(esperada - real_);
        printf("    tras %ld muestras (%.0f s) la fase se desvia %.0f LSB de 2^32\n",
               N, N / FS, err);
        comprueba(err < 1.0, "la fase no deriva: el acumulador es entero");
    }
}

/* --- 3. pureza --------------------------------------------------------- */
/*
 * MEDIR LA PUREZA SIN MEDIR EL METODO.
 *
 * El primer intento uso una ventana de Hann y aparto seis bins alrededor de
 * la portadora. Salio -63,6 dBc y casi me lo creo. No era el oscilador: era
 * la propia ventana. Los lobulos laterales de Hann andan por ahi a seis bins
 * de distancia, asi que lo que estaba midiendo era el banco.
 *
 * La forma correcta es el MUESTREO COHERENTE: se elige una frecuencia que
 * cae EXACTAMENTE en un bin, y entonces no hay fuga ninguna y no hace falta
 * ventana. Con el acumulador entero eso se puede pedir de verdad: si la
 * frecuencia es k*Fs/N con N potencia de dos, el incremento sale entero y la
 * senal cierra el periodo sin resto.
 *
 * Y para saber donde esta el suelo del metodo, se mide lo mismo con un seno
 * calculado en doble precision. Lo que ese suelo diga es lo que el banco NO
 * puede ver; todo lo que este por encima es del oscilador.
 */
#define NFFT 8192
#define KBIN 607                 /* primo con 8192: no coincide con ningun armonico */

static void dft_db(const float *x, int n, float *db)
{
    int k, t;
    for (k = 0; k <= n / 2; k++) {
        double re = 0.0, im = 0.0;
        for (t = 0; t < n; t++) {
            double a = 2.0 * M_PI * k * t / n;
            re += x[t] * cos(a);
            im -= x[t] * sin(a);
        }
        db[k] = (float)(10.0 * log10(re * re + im * im + 1e-300));
    }
}

static float sfdr(const float *x)
{
    static float db[NFFT / 2 + 1];
    int k;
    float pico, espurio = -999.0f;
    dft_db(x, NFFT, db);
    pico = db[KBIN];
    for (k = 1; k <= NFFT / 2; k++) {
        if (k == KBIN) { continue; }
        if (db[k] > espurio) { espurio = db[k]; }
    }
    return espurio - pico;
}

static void prueba_pureza(void)
{
    static float x[NFFT], ref[NFFT];
    nco_t o;
    int n;
    float s_nco, s_ref;
    float hz = (float)KBIN * FS / (float)NFFT;

    printf("\n3. pureza del oscilador, muestreo coherente en %.4f Hz\n", hz);

    nco_fase_cero(&o);
    nco_freq(&o, hz, FS);
    for (n = 0; n < NFFT; n++) {
        float sn, cs;
        nco_paso(&o, &sn, &cs);
        x[n] = sn;
    }
    for (n = 0; n < NFFT; n++) {
        ref[n] = (float)sin(2.0 * M_PI * KBIN * n / NFFT);
    }
    s_ref = sfdr(ref);
    s_nco = sfdr(x);
    printf("    suelo del metodo (seno en doble precision)  %7.1f dBc\n", s_ref);
    printf("    tabla de 1024 con interpolacion lineal      %7.1f dBc\n", s_nco);
    comprueba(s_nco < -80.0f, "espurios del NCO por debajo de -80 dBc");
    comprueba(s_nco > s_ref + 3.0f,
              "y por encima del suelo del banco: se esta midiendo el NCO");

    /*
     * El muestreo coherente es el MEJOR caso y hay que decirlo: con N
     * potencia de dos, que la senal cierre el periodo obliga al incremento a
     * ser multiplo de 2^19, o sea que los 19 bits bajos de la fase son
     * siempre cero y la interpolacion solo visita ocho posiciones distintas
     * dentro de cada celda de la tabla. Es justo donde menos se nota el
     * error.
     *
     * Para el caso general se mide de otra manera, sin espectro y sin
     * ventana: se resta el seno de VERDAD (doble precision) de la misma
     * fase entera y se mira lo que queda. Eso no depende del metodo de
     * analisis, y vale para cualquier frecuencia por fea que sea.
     */
    {
        double sum = 0.0, pico = 0.0;
        nco_fase_cero(&o);
        nco_freq(&o, 7123.456f, FS);         /* fea a proposito */
        for (n = 0; n < NFFT; n++) {
            float sn, cs;
            double exacto;
            uint32_t fase = o.fase;
            nco_paso(&o, &sn, &cs);
            exacto = sin(2.0 * M_PI * (double)fase / 4294967296.0);
            {
                double e = (double)sn - exacto;
                sum += e * e;
                if (fabs(e) > pico) { pico = fabs(e); }
            }
        }
        {
            double rms = sqrt(sum / NFFT);
            double snr = 20.0 * log10(rms / 0.70710678);   /* seno: rms = 1/raiz(2) */
            printf("    error contra el seno exacto, frecuencia fea:\n");
            printf("      pico %.3e   rms %.3e   =  %.1f dB por debajo de la portadora\n",
                   pico, rms, snr);
            comprueba(snr < -95.0, "la potencia total del error por debajo de -95 dBc");
        }
    }
}

/* --- 4b. ONDA LARGA: donde el aparcado se sale de la cuenta ------------- */
/*
 * Pedido por el usuario, 09/10/2026.
 *
 * Esa ultima frase es la que dice donde mirar: irse y volver arregla cosas
 * que dependen de DONDE ESTABA EL OSCILADOR, y lo unico que recuerda donde
 * estaba es el aparcado del NCO.
 *
 * El banco de arriba prueba el aparcado SOLO en 7,15 MHz. Y la radio llega
 * hasta 30 kHz -TUNE_MIN_HZ, bajado el 01/09/2026 para alcanzar DCF77-,
 * donde el dial es MAS PEQUENO que la distancia de aparcado: la cuenta
 * `freq - off_max` se va por debajo de cero, y como devuelve uint32_t, da
 * la vuelta. Eso no es un oscilador raro: son 4.294 MHz.
 *
 * Aqui se mide en la banda donde vive el problema.
 */
static void prueba_aparcado_lf(void)
{
    const uint32_t OFF = 24000U;
    const uint32_t MIN = 6000U;
    /* Los DOS: el que habia (44.000) y el que hay desde el 09/10/2026
     * (40.000). La funcion recibe el tope como parametro, asi que probarla
     * con los dos es gratis y deja el banco valiendo si alguien lo vuelve a
     * mover. */
    static const uint32_t k_max[] = { 40000U, 44000U };
    uint32_t MAX;
    uint32_t m;
    static const uint32_t k_dial[] = { 30000U, 35000U, 40000U, 44000U,
                                       50000U, 66600U, 77500U, 91300U };
    uint32_t i;

    printf("\n4b. el aparcado en onda larga (la radio baja a 30 kHz)\n");
    printf("    se viene de ARRIBA, o sea por la rama de bajar\n");

    for (m = 0U; m < sizeof k_max / sizeof k_max[0]; m++) {
    MAX = k_max[m];
    printf("\n    con un tope de paseo de %u Hz:\n", MAX);
    for (i = 0U; i < sizeof k_dial / sizeof k_dial[0]; i++) {
        uint32_t f = k_dial[i];
        /* un oscilador previo muy por encima: fuerza la rama de bajada */
        uint32_t lo = nco_lo_aparcado(f, f + 200000U, OFF, MIN, MAX);
        int64_t  d  = (int64_t)f - (int64_t)lo;

        printf("    dial %6u  ->  oscilador %10u   distancia %+9lld Hz",
               f, lo, (long long)d);
        if (lo == 0U || lo > f) {
            printf("   <- IMPOSIBLE\n");
        } else if (d < (int64_t)MIN) {
            printf("   <- la fuga del oscilador se cuela\n");
        } else {
            printf("\n");
        }
        /*
         * Las dos unicas condiciones que de verdad importan, y ninguna de
         * las dos la miraba nadie: que el oscilador quede POR DEBAJO de la
         * sintonia -si no, la distancia es negativa y el paseo se va al
         * lado equivocado- y que la fuga, que se auto-mezcla a cero, quede
         * fuera del filtro de canal.
         */
        comprueba(lo > 0U && lo < f, "el oscilador queda por debajo del dial");
        comprueba((int64_t)f - (int64_t)lo >= (int64_t)MIN,
                  "la distancia no baja del minimo");
        comprueba((int64_t)f - (int64_t)lo <= (int64_t)MAX,
                  "la distancia no pasa del maximo");
    }
    }
}

/* --- 4. la regla de aparcado del oscilador ----------------------------- */
static void prueba_aparcado(void)
{
    const uint32_t OFF = 24000U;      /* Fs/4 a 96 kHz: donde aparca la 1a vez */
    const uint32_t MIN = 6000U;       /* la fuga del oscilador no entra aqui */
    const uint32_t MAX = 44000U;
    uint32_t f0 = 7150000U;
    uint32_t lo, lo0;
    int cambios, k;

    printf("\n4. donde se aparca el oscilador\n");

    /* sin oscilador previo: a la posicion clasica */
    lo = nco_lo_aparcado(f0, 0U, OFF, MIN, MAX);
    printf("    primera vez        %u -> oscilador %u (distancia %d)\n",
           f0, lo, (int)(f0 - lo));
    comprueba(lo == f0 - OFF, "sin oscilador previo, a la posicion clasica");

    /* dentro de la ventana: no se toca */
    lo0 = lo;
    comprueba(nco_lo_aparcado(f0 + 15000U, lo0, OFF, MIN, MAX) == lo0,
              "15 kHz arriba: el oscilador NO se mueve");
    comprueba(nco_lo_aparcado(f0 - 15000U, lo0, OFF, MIN, MAX) == lo0,
              "15 kHz abajo: el oscilador NO se mueve");

    /* los bordes, exactos */
    comprueba(nco_lo_aparcado(lo0 + MAX, lo0, OFF, MIN, MAX) == lo0,
              "justo en el borde de arriba: todavia no se mueve");
    comprueba(nco_lo_aparcado(lo0 + MAX + 1U, lo0, OFF, MIN, MAX) != lo0,
              "un hercio mas alla: reaparca");
    comprueba(nco_lo_aparcado(lo0 + MIN, lo0, OFF, MIN, MAX) == lo0,
              "justo en la distancia minima: todavia no se mueve");
    comprueba(nco_lo_aparcado(lo0 + MIN - 1U, lo0, OFF, MIN, MAX) != lo0,
              "un hercio por debajo de la minima: reaparca");

    /*
     * LA PRUEBA QUE IMPORTA. Barrer 30 kHz en pasos de 100 Hz -300 clics del
     * mando- y contar cuantas veces habria que reprogramar el oscilador. Con
     * el NCO puesto tiene que ser CERO: eso es, literalmente, lo que hace
     * que el espectro y el waterfall se queden quietos. Si esto sale
     * distinto de cero, el NCO no esta haciendo su trabajo por muy bien que
     * suene el audio.
     */
    lo = nco_lo_aparcado(f0, 0U, OFF, MIN, MAX);
    cambios = 0;
    for (k = 1; k <= 300; k++) {
        uint32_t nuevo = nco_lo_aparcado(f0 + (uint32_t)(100 * k), lo, OFF, MIN, MAX);
        if (nuevo != lo) { cambios++; lo = nuevo; }
    }
    printf("    300 clics de 100 Hz (30 kHz): el oscilador se reprograma %d veces\n",
           cambios);
    comprueba(cambios <= 1, "subiendo 30 kHz se reprograma una vez como mucho");

    /* bajando, que es la otra mitad y donde la primera version fallaba */
    lo = nco_lo_aparcado(f0, 0U, OFF, MIN, MAX);
    cambios = 0;
    for (k = 1; k <= 300; k++) {
        uint32_t nuevo = nco_lo_aparcado(f0 - (uint32_t)(100 * k), lo, OFF, MIN, MAX);
        if (nuevo != lo) { cambios++; lo = nuevo; }
    }
    printf("    300 clics hacia abajo (30 kHz): se reprograma %d veces\n", cambios);
    comprueba(cambios <= 1, "bajando 30 kHz tambien se reprograma una vez como mucho");

    /* y un barrido largo, para ver cada cuanto salta */
    lo = nco_lo_aparcado(f0, 0U, OFF, MIN, MAX);
    cambios = 0;
    for (k = 1; k <= 2000; k++) {
        uint32_t nuevo = nco_lo_aparcado(f0 + (uint32_t)(100 * k), lo, OFF, MIN, MAX);
        if (nuevo != lo) { cambios++; lo = nuevo; }
    }
    printf("    2000 clics (200 kHz): se reprograma %d veces, o sea cada %.0f kHz\n",
           cambios, cambios ? 200.0 / cambios : 0.0);
    printf("    (la ventana util es de %u kHz: de %u a %u kHz por encima del oscilador)\n",
           (MAX - MIN) / 1000U, MIN / 1000U, MAX / 1000U);
    comprueba(cambios >= 4 && cambios <= 7,
              "en un barrido largo salta cada ~38 kHz, no en cada clic");
}

int main(void)
{
    printf("banco del NCO\n");
    nco_init();
    prueba_fs4();
    prueba_freq();
    prueba_pureza();
    prueba_aparcado();
    prueba_aparcado_lf();
    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

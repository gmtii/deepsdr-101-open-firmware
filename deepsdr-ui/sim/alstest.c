/*
 * Banco de ALS162 (la parte de fase de User/dcf77.c).
 *
 * Aqui hay una trampa que no tenian ni DCF77 ni MSF, y es la razon de que
 * este banco exista: la señal que llega NO es el triangulo de fase limpio.
 * Viene montada sobre el giro del oscilador local, que en esta radio a
 * 162 kHz se genera con un temporizador de pasos de ~190 Hz - o sea que
 * puede haber cientos de vueltas por segundo encima de un radian de
 * modulacion. Si el paso alto que quita esa deriva no esta bien, no se
 * decodifica nada, y en la radio seria indistinguible de no recibir.
 *
 * Asi que aqui se emite la fase CON deriva, y de varias magnitudes, para ver
 * hasta donde aguanta.
 */
#include "dcf77.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS     1500.0f   /* cuatro por bloque, ver el comentario del desenrollado */
#define PI2    6.28318530718

static unsigned long s_rnd = 5150UL;
static double uni(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return (double)((s_rnd >> 16) & 0x7FFFUL) / 32767.0;
}
static double gauss(void)
{
    double a = uni(), b = uni();
    if (a < 1e-9) { a = 1e-9; }
    return sqrt(-2.0 * log(a)) * cos(PI2 * b);
}

/*
 * *** ESTE COMENTARIO DECIA UNA COSA QUE NO ES VERDAD - 10/10/2026. ***
 *
 * Decia: "Mismos 59 bits que DCF77: el reparto de 20..58 es identico, que es
 * todo lo que se decodifica."
 *
 * La segunda mitad es falsa. decodifica() -que es el decodificador de DCF77,
 * y ALS162 lo usa TAL CUAL- rechaza tambien por los bits 0, 17 y 18, que
 * estan FUERA de ese 20..58:
 *
 *     b[0]  != 0          -> fuera
 *     b[20] != 1          -> fuera
 *     b[17] == b[18]      -> fuera
 *
 * O sea que la trama que fabrica este banco tiene esos cuatro bits puestos
 * como los de DCF77 porque yo di por hecho que Allouis los reparte igual, y
 * el banco no comprueba esa suposicion: la ENCARNA. Sale verde sea verdad o
 * no, que es el unico resultado que un banco no puede dar.
 *
 * El caso 5 de abajo existe para que eso se vea. No puede decir cual es el
 * reparto de verdad -para eso hace falta la norma o una radio delante- pero
 * si puede enseñar que, si Allouis difiere en uno solo de esos tres bits,
 * esta radio no decodifica NADA y ademas dice por que.
 */
static void pon_bcd(uint8_t *b, int ini, int n, int v)
{
    static const int peso[8] = { 1, 2, 4, 8, 10, 20, 40, 80 };
    int k;
    for (k = n - 1; k >= 0; k--) {
        if (v >= peso[k]) { b[ini + k] = 1; v -= peso[k]; } else { b[ini + k] = 0; }
    }
}
static void pon_par(uint8_t *b, int ini, int fin)
{
    int k, n = 0;
    for (k = ini; k < fin; k++) { n += b[k]; }
    b[fin] = (uint8_t)(n & 1);
}
static void trama(uint8_t *b, int h, int m, int verano)
{
    int k;
    for (k = 0; k < 59; k++) { b[k] = 0; }
    b[17] = (uint8_t)(verano ? 1 : 0);
    b[18] = (uint8_t)(verano ? 0 : 1);
    b[20] = 1;
    pon_bcd(b, 21, 7, m);  pon_par(b, 21, 28);
    pon_bcd(b, 29, 6, h);  pon_par(b, 29, 35);
    pon_bcd(b, 36, 6, 15);
    pon_bcd(b, 42, 3, 1);
    pon_bcd(b, 45, 5, 6);
    pon_bcd(b, 50, 8, 26);
    pon_par(b, 36, 58);
}

/*
 * La forma del elemento: +1 rad en 25 ms, -2 rad en 50 ms, +1 rad en 25 ms.
 * Devuelve la desviacion de fase en el instante t (ms) desde que empieza.
 */
static double elemento(double t_ms)
{
    if (t_ms < 0.0  || t_ms >= 100.0) { return 0.0; }
    if (t_ms < 25.0)  { return  1.0 * (t_ms / 25.0); }
    if (t_ms < 75.0)  { return  1.0 - 2.0 * ((t_ms - 25.0) / 50.0); }
    return -1.0 + 1.0 * ((t_ms - 75.0) / 25.0);
}

/* Emite un minuto. deriva_hz = vueltas por segundo del oscilador local. */
static double g_giro = 0.0;   /* la fase acumulada del oscilador local */

/*
 * *** LOS DATOS NO HORARIOS DEL RESTO DEL SEGUNDO - 10/10/2026. ***
 *
 * La norma de Allouis: "Additional non-timing data is sent by phase
 * modulation during the rest of each second", y "the second marker (and
 * data bit) is always preceded by 100 ms without any phase modulation".
 *
 * Este banco NO los emitia. Emitia un segundo con uno o dos elementos y
 * novecientos milisegundos de silencio, que es una señal que no existe, y
 * por eso salia verde contra un firmware que en antena no decodifica nada.
 *
 * Medido sobre la grabacion de verdad (sim/als_real.c): entre cuatro y seis
 * elementos por segundo, repartidos por todo el segundo. Aqui se emiten
 * entre tres y cinco de relleno, con la MISMA forma -que es lo que hace
 * dificil el problema: no se distinguen por la forma, solo por el silencio
 * de delante-, desde los 250 ms y dejando los ultimos 100 ms limpios para
 * que el elemento del segundo siguiente cumpla la norma.
 *
 * `relleno` a 0 emite como antes, para poder comparar las dos cosas.
 */
static int g_relleno = 1;

static double datos_del_resto(int seg, double t_ms)
{
    /* Pseudoaleatorio pero REPETIBLE y funcion solo de (seg, k): asi dos
     * tandas con la misma semilla emiten exactamente lo mismo. */
    unsigned h = (unsigned)seg * 2654435761u + 12345u;
    double mod = 0.0;
    int j;

    if (!g_relleno) { return 0.0; }
    for (j = 0; j < 5; j++) {
        double ini;
        h = h * 1103515245u + 12345u;
        if (((h >> 20) & 3u) == 0u) { continue; }   /* a veces no hay */
        /* entre 250 y 850 ms, en rejilla de 100 ms */
        ini = 250.0 + 100.0 * (double)((h >> 8) % 6u);
        mod += elemento(t_ms - ini);
    }
    return mod;
}

static void emite(const uint8_t *b, double ruido, double deriva_hz, int sin_senal)
{
    int seg, k;
    double giro = g_giro;


    for (seg = 0; seg < 60; seg++) {
        int n = (int)(FS + 0.5f);
        for (k = 0; k < n; k++) {
            double t_ms = 1000.0 * (double)k / FS;
            double mod = 0.0;

            if (!sin_senal) {
                if (seg < 59) {
                    mod = elemento(t_ms);
                    if (b[seg]) { mod += elemento(t_ms - 100.0); }  /* el 1 son dos */
                }
                /* Y el resto del segundo, TAMBIEN EN EL 59: lo que falta en
                 * el 59 es el elemento HORARIO, no la modulacion entera. */
                mod += datos_del_resto(seg, t_ms);
            }
            giro += PI2 * deriva_hz / FS;
            if (giro > 1e6 || giro < -1e6) { giro = fmod(giro, PI2); }
            {
                /*
                 * *** POR I/Q Y POR dcf77_fase_iq(), COMO LA RADIO -
                 * 10/10/2026. ***
                 *
                 * Antes esto metia la fase directamente en dcf77_feed(), o
                 * sea que se saltaba la extraccion de fase ENTERA: el paso
                 * bajo sobre I y Q y el atan2. Y eso hacia que este banco y
                 * el de la grabacion midieran dos cosas distintas sin que
                 * se notara.
                 *
                 * Se noto asi: el pico a pico del triangulo reconstruido
                 * salia 2,56 aqui y 1,60 con la grabacion, para el MISMO
                 * elemento nominal de +-1 rad. Medida la fase de la
                 * grabacion de verdad en ventanas de 100 ms: p90 2,04 rad,
                 * maximo 2,09. Y el elemento de aqui va de +1 a -1, o sea
                 * 2,00 clavados. LOS DOS MODELAN LO MISMO.
                 *
                 * La diferencia no era amplitud: era FORMA. El elemento de
                 * aqui tiene esquinas instantaneas -ancho de banda
                 * infinito- y ningun receptor ve eso; la grabacion venia
                 * por un receptor de verdad. Reconstruir el triangulo a
                 * partir de un escalon perfecto da un 60% mas.
                 *
                 * Pasando por donde pasa la radio, las esquinas se limitan
                 * igual que en antena y los dos bancos vuelven a medir la
                 * misma señal. Y el ruido se suma donde se suma de verdad
                 * -en I y en Q, antes del angulo-, que no es lo mismo que
                 * sumarlo al angulo ya hecho.
                 */
                double ph = mod + giro;
                double I = cos(ph) + ruido * gauss();
                double Q = sin(ph) + ruido * gauss();
                dcf77_feed(dcf77_fase_iq((float)I, (float)Q));
            }
        }
    }
    g_giro = giro;
}

typedef struct { int ok, minutos; dcf_info_t inf; } res_t;

static res_t corre(int h, int m, double ruido, double deriva, int sin_senal, int mins)
{
    uint8_t b[59];
    res_t r;
    int i, hh = h, mm = m;

    memset(&r, 0, sizeof r);
    s_rnd = 5150UL;
    g_giro = 0.0;   /* cada prueba empieza de cero, como una radio al encenderla */
    dcf77_start(HORA_ALS162, FS);
    for (i = 0; i < mins; i++) {
        int nh = hh, nm = mm + 1;
        if (nm > 59) { nm = 0; nh = (nh + 1) % 24; }
        trama(b, nh, nm, 1);
        emite(b, ruido, deriva, sin_senal);
        hh = nh; mm = nm;
        dcf77_info(&r.inf);
        r.minutos = i + 1;
        if (r.inf.estado == DCF_LISTO) { r.ok = 1; break; }
    }
    dcf77_stop();
    return r;
}

static int fallos = 0;
static void comprueba(const char *q, int ok)
{
    printf("    %-52s %s\n", q, ok ? "OK" : "NO");
    if (!ok) { fallos++; }
}

int main(void)
{
    res_t r;

    printf("ALS162: fase a %.0f muestras/s, elemento de +1/-2/+1 rad en 100 ms\n", FS);
    printf("  (un elemento = bit 0, dos = bit 1, ninguno = segundo 59)\n\n");

    printf("  1. SEÑAL LIMPIA, SIN DERIVA\n");
    r = corre(18, 40, 0.0, 0.0, 0, 6);
    printf("    %d min -> %02u:%02u  %02u/%02u/%02u\n", r.minutos,
           r.inf.hora, r.inf.minuto, r.inf.dia, r.inf.mes, r.inf.anno);
    comprueba("sincroniza", r.ok);
    comprueba("la hora es la correcta (18:43)",
              r.inf.hora == 18U && r.inf.minuto == 43U);
    comprueba("la fecha es la correcta", r.inf.dia == 15U && r.inf.mes == 6U);

    printf("\n  2. CON DERIVA DEL OSCILADOR - la prueba de fuego\n");
    {
        double d[] = { 1.0, 10.0, 50.0, 190.0, 500.0 };
        int i, ultimo = -1;
        for (i = 0; i < 5; i++) {
            r = corre(9, 0, 0.02, d[i], 0, 8);
            printf("    %6.0f Hz de error de oscilador  %s",
                   d[i], r.ok ? "sincroniza" : "no sincroniza");
            if (r.ok) { printf("  (%02u:%02u)", r.inf.hora, r.inf.minuto); ultimo = i; }
            printf("\n");
        }
        comprueba("aguanta los ~190 Hz de paso del temporizador", ultimo >= 3);
    }

    printf("\n  3. CON RUIDO DE FASE\n");
    {
        double n[] = { 0.15, 0.50, 1.00, 1.60, 2.20 };
        int i, ultimo = -1, el_primero = 0;
        for (i = 0; i < 5; i++) {
            r = corre(21, 10, n[i], 190.0, 0, 8);
            printf("    ruido %.2f rad (%2.0f%% del elemento)  %s\n", n[i], 100 * n[i],
                   r.ok ? "sincroniza" : "no sincroniza");
            if (r.ok) { ultimo = i; if (i == 0) { el_primero = 1; } }
        }
        /* 0,15 rad es el limite duro de leer una fase: por encima, el atan2
         * pierde ciclos y no hay filtro que lo arregle. Ver TRI_LPF_HZ.
         *
         * SE MIRA EL CASO 0, NO "alguno". Aqui ponia `ultimo >= 0`, que solo
         * dice "alguno de los cinco sincronizo": si ALS dejara de aguantar
         * los 0,15 rad pero enganchara por casualidad con 2,20, el renglon
         * seguiria diciendo lo mismo. La etiqueta nombra un caso concreto,
         * asi que tiene que mirar ese caso - como ya hace la comprobacion
         * de doce lineas mas arriba, que usa `ultimo >= 3`. */
        comprueba("aguanta 0,15 rad de ruido de fase", el_primero);
    }

    printf("\n  4. SIN SEÑAL - LO IMPORTANTE\n");
    {
        double n[] = { 0.3, 1.0, 3.0 };
        int i;
        for (i = 0; i < 3; i++) {
            r = corre(0, 0, n[i], 190.0, 1, 12);
            printf("    solo ruido %.1f rad -> %s (buenas %u, malas %u)\n", n[i],
                   r.ok ? "*** SE INVENTA UNA HORA ***" : "no sincroniza",
                   r.inf.tramas_ok, r.inf.tramas_malas);
            comprueba("      NO se inventa una hora", !r.ok);
        }
    }

    /*
     * ==================================================================
     * 5. LA SUPOSICION QUE ESTE BANCO ENCARNABA, PUESTA A LA VISTA
     * ==================================================================
     *
     * *** Contexto, 10/10/2026. En un video, un usuario ruso saca DCF77 y
     * RBU en antena, las dos en "Time received and confirmed". Nadie ha
     * sacado nunca ALS162. ***
     *
     * Y eso NO se reparte como uno esperaria, porque las cinco emisoras no
     * comparten las mismas dos piezas:
     *
     *   emisora   entrada de audio      decodificador de trama   en aire
     *   DCF77     feed_envolvente()     decodifica()             SI
     *   MSF       feed_envolvente()     decodifica_msf()         no
     *   ALS162    feed_fase()           decodifica()             no
     *   RBU       rbu_feed()            el suyo, hora_rbu.c      SI
     *   WWV/BPM   wwv_feed()            el suyo, hora_wwv.c      no
     *
     * LO QUE EL VIDEO DEMUESTRA: decodifica() funciona con señal de verdad
     * -lo prueba DCF77-, y hora_rbu.c entero tambien.
     *
     * LO QUE NO DEMUESTRA, Y ES LA TRAMPA: que "la fase funciona, mira
     * RBU". RBU es de fase, si, pero NO PASA POR feed_fase(): tiene modulo
     * propio. Asi que feed_fase() -el camino de ALS162- sigue sin que nadie
     * lo haya visto funcionar nunca fuera de este banco.
     *
     * ALS162 es la unica que apila DOS cosas sin comprobar: una entrada de
     * audio que nadie ha visto en antena y el decodificador de OTRA
     * emisora. Y este banco daba por buena la segunda.
     *
     * Lo que se mide aqui: si Allouis difiere en UNO SOLO de los tres bits
     * fijos, no se decodifica nada. Si sale verde, la suposicion es
     * load-bearing y hay que comprobarla en antena; no es un adorno.
     */
    printf("\n  5. SI ALLOUIS NO REPARTE LOS BITS FIJOS COMO DCF77\n");
    printf("     (la suposicion que este banco encarnaba - ver su comentario)\n");
    {
        static const struct { const char *que; int bit; int val; int motivo; }
        k[] = {
            { "el bit 0 no es 0",        0,  1, DCF_MOT_B0   },
            { "el bit 20 no es 1",      20,  0, DCF_MOT_B20  },
            { "los 17 y 18 no se oponen", 18, -1, DCF_MOT_Z1Z2 },
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            uint8_t b[59];
            int j;

            s_rnd = 5150UL;
            g_giro = 0.0;
            dcf77_start(HORA_ALS162, FS);
            for (j = 0; j < 4; j++) {
                trama(b, 9, 30 + j, 1);
                if (k[i].val < 0) { b[k[i].bit] = b[17]; }   /* iguales: Z1==Z2 */
                else              { b[k[i].bit] = (uint8_t)k[i].val; }
                emite(b, 0.0, 0.0, 0);
            }
            dcf77_info(&r.inf);
            dcf77_stop();

            printf("    %-28s -> %s, motivo %u, malas %u\n", k[i].que,
                   (r.inf.estado == DCF_LISTO) ? "SINCRONIZA" : "no sincroniza",
                   (unsigned)dcf77_motivo(), (unsigned)r.inf.tramas_malas);
            comprueba("      no decodifica nada", r.inf.estado != DCF_LISTO);
            /* Y QUE LO DIGA. El motivo entro en la V4.25 para separar "no se
             * completan" de "se completan y se tiran", y hasta aqui nada lo
             * ejercitaba: un numero de diagnostico que ningun banco mira es
             * un numero en el que no se puede confiar el dia que haga
             * falta. */
            comprueba("      y dice por que puerta", dcf77_motivo() == (uint8_t)k[i].motivo);
            comprueba("      contando la trama como mala", r.inf.tramas_malas > 0U);
        }
    }

    printf("\n  %s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

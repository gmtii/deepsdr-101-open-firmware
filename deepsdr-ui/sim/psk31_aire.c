/*
 * PSK31 DE PUNTA A PUNTA: DE LA PORTADORA AL TEXTO.
 *
 * Pedido por el usuario.
 *
 * Aqui se fabrica una emision de PSK31 como la que saldria del altavoz
 * -portadora de audio, vueltas de fase con su envolvente de coseno, su
 * error de sintonia y su ruido- y se le da al receptor de verdad, el mismo
 * psk31.c que corre en la radio.
 *
 * QUE COMPRUEBA Y QUE NO, que despues de lo del localizador de WSPR
 * conviene dejarlo escrito:
 *
 *   SI. El modulador de aqui esta escrito a partir de la descripcion del
 *   modo, no llamando a psk31.c: la envolvente de coseno, la codificacion
 *   diferencial y el orden de los bits del varicode se hacen otra vez.
 *   Si el receptor tuviera del reves cualquiera de esos tres convenios
 *   -que un cero es media vuelta, que el primer bit de la tira se manda
 *   primero, que el separador es "00"- aqui NO se cancelaria: saldria
 *   texto distinto.
 *
 *   NO. La     "1010101011", "1011011011", "1011101101", "1101110111",
    "1011101011", "1101011111", "1011101111", "1011111101",
    "1011111111", "11101111", "11101", "1101101111",
    "1011011101", "11111", "1101110101", "1110101011",
    "1011110111", "1011110101", "1110101101", "1110101111",
    "1101011011", "1101101011", "1101101101", "1101010111",
    "1101111011", "1101111101", "1110110111", "1101010101",
    "1101011101", "1110111011", "1011111011", "1101111111",
    "1", "111111111", "101011111", "111110101",
    "111011011", "1011010101", "1010111011", "101111111",
    "11111011", "11110111", "101101111", "111011111",
    "1110101", "110101", "1010111", "110101111",
    "10110111", "10111101", "11101101", "11111111",
    "101110111", "101011011", "101101011", "110101101",
    "110101011", "110110111", "11110101", "110111101",
    "111101101", "1010101", "111010111", "1010101111",
    "1010111101", "1111101", "11101011", "10101101",
    "10110101", "1110111", "11011011", "11111101",
    "101010101", "1111111", "111111101", "101111101",
    "11010111", "10111011", "11011101", "10101011",
    "11010101", "111011101", "10101111", "1101111",
    "1101101", "101010111", "110110101", "101011101",
    "101110101", "101111011", "1010101101", "111110111",
    "111101111", "111111011", "1010111111", "101101101",
    "1011011111", "1011", "1011111", "101111",
    "101101", "11", "111101", "1011011",
    "101011", "1101", "111101011", "10111111",
    "11011", "111011", "1111", "111",
    "111111", "110111111", "10101", "10111",
    "101", "110111", "1111011", "1101011",
    "11011111", "1011101", "111010101", "1010110111",
    "110111011", "1010110101", "1011010111", "1110110101" del varicode sale del mismo sitio en los dos lados
 *   (psk31_varicode.txt, comprobado identico contra fldigi, SDRangel y
 *   LinPSK). Una errata en la tabla se cancelaria igual que se cancelo la
 *   del localizador. Por eso la tabla NO se comprueba aqui sino en
 *   tools/varicode_check.py, que lee el .txt y el .c y los compara: un
 *   sitio de fuera mirando a los dos, que es la unica forma de que un
 *   error compartido no se cancele.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include "psk31.h"

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

#define FS       12000.0
#define BAUDIO   31.25
#define MPS      384                  /* muestras por simbolo: 12000/31,25 */
#define MAX_SIM  20000

/* El varicode como tiras de texto, que es como lo publica el estandar. */
static const char *const k_vc[128] = {
    "1010101011", "1011011011", "1011101101", "1101110111",
    "1011101011", "1101011111", "1011101111", "1011111101",
    "1011111111", "11101111", "11101", "1101101111",
    "1011011101", "11111", "1101110101", "1110101011",
    "1011110111", "1011110101", "1110101101", "1110101111",
    "1101011011", "1101101011", "1101101101", "1101010111",
    "1101111011", "1101111101", "1110110111", "1101010101",
    "1101011101", "1110111011", "1011111011", "1101111111",
    "1", "111111111", "101011111", "111110101",
    "111011011", "1011010101", "1010111011", "101111111",
    "11111011", "11110111", "101101111", "111011111",
    "1110101", "110101", "1010111", "110101111",
    "10110111", "10111101", "11101101", "11111111",
    "101110111", "101011011", "101101011", "110101101",
    "110101011", "110110111", "11110101", "110111101",
    "111101101", "1010101", "111010111", "1010101111",
    "1010111101", "1111101", "11101011", "10101101",
    "10110101", "1110111", "11011011", "11111101",
    "101010101", "1111111", "111111101", "101111101",
    "11010111", "10111011", "11011101", "10101011",
    "11010101", "111011101", "10101111", "1101111",
    "1101101", "101010111", "110110101", "101011101",
    "101110101", "101111011", "1010101101", "111110111",
    "111101111", "111111011", "1010111111", "101101101",
    "1011011111", "1011", "1011111", "101111",
    "101101", "11", "111101", "1011011",
    "101011", "1101", "111101011", "10111111",
    "11011", "111011", "1111", "111",
    "111111", "110111111", "10101", "10111",
    "101", "110111", "1111011", "1101011",
    "11011111", "1011101", "111010101", "1010110111",
    "110111011", "1010110101", "1011010111", "1110110101"
};

/*
 * LA ESCALA DEL AUDIO. Vale 1 en todo el banco menos en la ultima seccion.
 *
 * Y esa seccion existe por un fallo que este banco NO PODIA VER: el AFC
 * elevaba al cuadrado dos acumuladores que ya llevan |z|^4, o sea |z|^8, y
 * a la escala de la radio (el AGC apunta a 18000) eso se sale de float y
 * sale +inf. Aqui la emision valia amplitud ~1 y el ruido ~1, asi que |z|
 * era ~400 y los cuadrados cabian de sobra. Un banco que mide con numeros
 * pequeños no puede ver un desbordamiento que solo pasa con los grandes.
 */
static double s_ruido_esc = 1.0;

static double gauss(void)
{
    double u1 = (rand() + 1.0) / (RAND_MAX + 2.0);
    double u2 = (rand() + 1.0) / (RAND_MAX + 2.0);

    return s_ruido_esc * sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

/*
 * TEXTO -> SIMBOLOS. Tres pasos, los tres escritos aqui a mano:
 *
 *   1. cada caracter es su tira del varicode, y detras "00".
 *   2. el reposo -antes y despues- son ceros seguidos.
 *   3. DIFERENCIAL: un cero da media vuelta, un uno no hace nada.
 */
static int a_simbolos(const char *txt, int reposo, signed char *sim)
{
    int n = 0, i;
    signed char v = 1;
    const char *p;

    for (i = 0; i < reposo; i++) { v = (signed char)-v; sim[n++] = v; }
    for (p = txt; *p != '\0'; p++) {
        const char *c = k_vc[(unsigned char)*p & 0x7F];
        const char *q;

        for (q = c; *q != '\0'; q++) {
            if (*q == '0') { v = (signed char)-v; }
            sim[n++] = v;
        }
        /* el separador */
        v = (signed char)-v; sim[n++] = v;
        v = (signed char)-v; sim[n++] = v;
    }
    for (i = 0; i < reposo; i++) { v = (signed char)-v; sim[n++] = v; }
    return n;
}

/*
 * SIMBOLOS -> AUDIO. La envolvente de PSK31 no salta: cada simbolo se
 * pinta con medio coseno que vale 1 en su centro y 0 en los centros de
 * los vecinos, asi que cuando hay vuelta de fase la amplitud PASA POR
 * CERO. Eso es lo que hace que ocupe 31 Hz y no 300, y es tambien de
 * donde el receptor saca el reloj.
 */
/*
 * LA EMISION DEL VECINO, para medir el rechazo al de al lado.
 *
 * En 14.070 las emisiones de PSK31 estan cada 50 Hz o menos, y el ancho de
 * una es 31 Hz: el vecino no es un caso raro, es el caso normal. Si esta a
 * cero no se suma nada y todo se comporta como siempre.
 */
static int          s_vec_n;
static double       s_vec_amp, s_vec_df;
static signed char  s_vec[MAX_SIM];

static void emite(const signed char *sim, int nsim, double f0, double df,
                  double amp, unsigned semilla, int trozo_n)
{
    long total = (long)nsim * MPS;
    long i;
    float trozo[600];
    int j = 0;

    srand(semilla);
    for (i = 0; i < total; i++) {
        double t = (double)i / MPS;          /* en simbolos */
        int k = (int)t;
        double frac = t - k;                 /* 0..1 dentro del simbolo */
        double env_a = 0.5 * (1.0 + cos(3.14159265358979 * frac));
        double env_b = 1.0 - env_a;
        double base = 0.0;
        double v;

        if (k < nsim)        { base += sim[k] * env_a; }
        if (k + 1 < nsim)    { base += sim[k + 1] * env_b; }

        v = amp * base * cos(6.283185307 * (f0 + df) * (double)i / FS)
          + gauss();

        if (s_vec_n > 0) {
            double b2_ = 0.0;
            int kk = k % s_vec_n;
            int k2 = (k + 1) % s_vec_n;

            b2_ += s_vec[kk] * env_a;
            b2_ += s_vec[k2] * env_b;
            v += s_vec_amp * b2_
                 * cos(6.283185307 * (f0 + s_vec_df) * (double)i / FS + 0.7);
        }
        trozo[j++] = (float)v;
        if (j == trozo_n) { psk31_process(trozo, (uint32_t)j); j = 0; }
    }
    if (j > 0) { psk31_process(trozo, (uint32_t)j); }
}

static void recoge(char *sal, int max)
{
    int n = 0;
    char c;

    while (n < max - 1 && psk31_get_char(&c)) { sal[n++] = c; }
    sal[n] = '\0';
}

/* La racha mas larga del texto esperado que aparece tal cual en la salida. */
static int seguidos(const char *esp, const char *sal)
{
    int n = (int)strlen(esp), i, mejor = 0;

    for (i = 0; i < n; i++) {
        int j;

        for (j = n - i; j > mejor; j--) {
            char t[128];

            if (j >= (int)sizeof t) { continue; }
            memcpy(t, esp + i, (size_t)j); t[j] = '\0';
            if (strstr(sal, t) != NULL) { if (j > mejor) { mejor = j; } break; }
        }
    }
    return mejor;
}

/* Cuenta cuantos caracteres del esperado salen seguidos y en orden. */
static int cuantos_cuadran(const char *esp, const char *sal)
{
    const char *p = strstr(sal, esp);

    return (p != NULL) ? (int)strlen(esp) : 0;
}

static int prueba(const char *txt, double df, double amp, unsigned semilla,
                  int callado, char *sal, int salmax)
{
    static signed char sim[MAX_SIM];
    int n;

    psk31_start(12000.0f);
    psk31_set_centro_hz(1000.0f);
    n = a_simbolos(txt, 48, sim);
    emite(sim, n, 1000.0, df, amp, semilla, 600);
    recoge(sal, salmax);
    if (!callado) {
        printf("    df %+5.1f Hz  amp %5.2f  ->  \"%s\"\n", df, amp, sal);
    }
    return cuantos_cuadran(txt, sal) != 0;
}

/*
 * ===================================================================
 *  MODO MEDIDA: ¿QUE FILTRO ADAPTADO ES MEJOR?  (30/09/2026)
 * ===================================================================
 *
 * Pedido por el usuario.
 *
 * Se uso para comparar tres formas de filtro adaptado -el rectangulo de un
 * simbolo que psk31.c lleva, una ventana de coseno de un simbolo y la de dos
 * simbolos, que es el filtro adaptado de verdad al pulso de PSK31-. Gano el
 * rectangulo por la mano: los numeros y el razonamiento estan en psk31.c,
 * junto al filtro. El aparato se queda porque estos dos numeros son los del
 * receptor y conviene poder volver a medirlos cuando se le toque algo.
 *
 * Lo que mide:
 *
 *   1. SENSIBILIDAD. Barrido fino de amplitud con muchas semillas, y de
 *      ahi la amplitud a la que se acierta el 50 % de las letras. En dB,
 *      que es como se comparan dos receptores.
 *   2. RECHAZO AL DE AL LADO. La misma señal con un vecino 12 dB mas
 *      fuerte a +31,25 Hz, que es donde el rectangulo tiene un cero
 *      exacto y la ventana de coseno no.
 *
 * Las dos a la vez, porque cada forma gana una: quedarse solo con la
 * primera es como elegir un filtro mirando la hoja de caracteristicas por
 * la mitad.
 */
/*
 * DESPLAZAMIENTO DE LAS SEMILLAS. Sirve para repetir la misma medida con
 * OTRO ruido y ver cuanto baila el resultado: sin eso, una diferencia de
 * 0,1 dB entre dos filtros no se sabe si es el filtro o es el ruido de
 * aquella tanda. Es la barra de error, y sin ella los numeros de abajo no
 * se pueden comparar entre si.
 */
static unsigned s_sem0;

static double al_50(const char *t, double vec_amp, double vec_df, int semillas)
{
    char sal[512];
    double amp, ant_a = 0.0, ant_p = 0.0;
    int esperados = (int)strlen(t);

    for (amp = 0.60; amp >= 0.12; amp *= 0.9) {
        int total = 0, k;
        double pc;

        s_vec_amp = vec_amp * amp;
        s_vec_df  = vec_df;
        for (k = 0; k < semillas; k++) {
            (void)prueba(t, 0.0, amp, (unsigned)(1000 + s_sem0 + k), 1, sal, sizeof sal);
            total += seguidos(t, sal);
        }
        pc = 100.0 * total / (double)(semillas * esperados);
        printf("      amp %5.3f (%+6.2f dB)  %5.1f %%\n",
               amp, 20.0 * log10(amp), pc);
        if (pc < 50.0 && ant_p >= 50.0) {
            /* interpolacion lineal en dB entre los dos puntos de alrededor */
            double d1 = 20.0 * log10(ant_a), d2 = 20.0 * log10(amp);
            return d2 + (50.0 - pc) * (d1 - d2) / (ant_p - pc);
        }
        ant_a = amp; ant_p = pc;
    }
    return 0.0;   /* nunca bajo del 50 %: no hay umbral en el barrido */
}

static int medir(void)
{
    const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    printf("\n  SENSIBILIDAD Y RECHAZO AL DE AL LADO\n");

    printf("\n    solo ruido\n");
    {
        double u;
        s_vec_n = 0;
        u = al_50(t, 0.0, 0.0, 40);
        printf("    --> el 50 %% cae en %+6.2f dB\n", u);
    }

    printf("\n    rechazo al de al lado: señal clara y el vecino subiendo\n");
    {
        /*
         * Aqui la señal buena se deja QUIETA en una amplitud que sin vecino
         * da el 100 %, y lo que sube es el VECINO. Lo que se busca es
         * cuantos dB por encima puede estar antes de tumbarla: eso es el
         * rechazo, y es el numero que decide si vale la pena en 14.070,
         * donde hay una emision cada 50 Hz.
         *
         * La primera version de esto subia las dos a la vez y salia 4 % en
         * las tres formas: un vecino 12 dB mas fuerte se las lleva por
         * delante a todas, asi que medido ahi no se distinguen. Una medida
         * en la que todo falla igual no compara nada.
         */
        double rel, ant_r = 0.0, ant_p = 0.0, umbral = 0.0;
        char sal2[512];
        int esperados = (int)strlen(t);

        s_vec_n = a_simbolos("VECINO QRM DE EA1ZZZ", 48, s_vec);
        for (rel = 0.05; rel <= 8.01; rel *= 1.3) {
            int total = 0, k;
            double pc;

            s_vec_amp = rel * 1.0;
            s_vec_df  = 31.25;
            for (k = 0; k < 16; k++) {
                (void)prueba(t, 0.0, 1.0, (unsigned)(3000 + s_sem0 + k), 1,
                             sal2, sizeof sal2);
                total += seguidos(t, sal2);
            }
            pc = 100.0 * total / (double)(16 * esperados);
            printf("      vecino %+6.2f dB  %5.1f %%\n", 20.0 * log10(rel), pc);
            if (pc < 50.0 && ant_p >= 50.0 && umbral == 0.0) {
                double d1 = 20.0 * log10(ant_r), d2 = 20.0 * log10(rel);
                umbral = d1 + (ant_p - 50.0) * (d2 - d1) / (ant_p - pc);
            }
            ant_r = rel; ant_p = pc;
        }
        s_vec_n = 0;
        s_vec_amp = 0.0;
        printf("    --> aguanta un vecino de hasta %+6.2f dB\n", umbral);
    }
    return 0;
}

int main(int argc, char **argv)
{
    char sal[512];

    if (argc > 1 && strcmp(argv[1], "medir") == 0) {
        if (argc > 2) { s_sem0 = (unsigned)atoi(argv[2]); }
        return medir();
    }

    printf("\n  PSK31: DE LA PORTADORA AL TEXTO\n");

    printf("\n  sintonizado y sin ruido apreciable\n");
    {
        int ok = prueba("CQ CQ DE EA4XYZ", 0.0, 8.0, 1, 0, sal, sizeof sal);

        exige(ok, "sale el texto que se metio");
        if (!ok) {
            printf("\n  (sin esto lo de abajo no dice nada)\n");
            printf("\n%s (%d fallos)\n", "HAY FALLOS", ++fallos);
            return 1;
        }
    }

    printf("\n  y con minusculas, numeros y signos\n");
    {
        const char *t = "Hola Oscar, esto es psk31 a 31,25 baudios. 73!";

        exige(prueba(t, 0.0, 8.0, 3, 0, sal, sizeof sal),
              "el alfabeto entero pasa entero");
    }

    printf("\n  desintonizado: el margen del diferencial es +-7,8 Hz\n");
    {
        /*
         * EL LIMITE NO ES UN AJUSTE, ES ARITMETICA. La deteccion
         * diferencial mide el giro de fase entre dos simbolos seguidos.
         * Un desvio de sintonia de f hercios mete un giro de 2*pi*f/31,25
         * por simbolo ademas del de los datos. Cuando f llega a la mitad
         * del baudio -15,6 Hz- ese giro vale media vuelta, o sea
         * EXACTAMENTE lo que significa un cero: los dos simbolos posibles
         * se intercambian y no hay forma de distinguirlos. La mitad de
         * eso, +-7,8 Hz, es hasta donde se puede llegar.
         *
         * Se pierden las primeras letras mientras se engancha; se cuenta
         * la racha mas larga, no la tira entera.
         */
        const char *t = "PRUEBA DE MARGEN DE SINTONIA";
        double df;

        for (df = -9.0; df <= 9.0; df += 3.0) {
            int n;
            uint8_t c; int16_t e;

            (void)prueba(t, df, 8.0, 5, 1, sal, sizeof sal);
            n = seguidos(t, sal);
            psk31_diag(&c, &e);
            printf("    %+5.1f Hz  ->  %2d de %2d letras   cal %3u  error medido %+6.2f Hz\n",
                   df, n, (int)strlen(t), c, e / 100.0);
            if (df >= -6.0 && df <= 6.0) {
                exige(n >= (int)strlen(t) - 6, "dentro de +-6 Hz tiene que salir el texto");
            }
            if (df <= -9.0 || df >= 9.0) {
                exige(n < 8, "pasado el limite NO puede dar por bueno un texto");
            }
        }
    }

  printf("\n  bajando la señal hasta que se pierde\n");
    {
        /*
         * SE CUENTAN CARACTERES, NO TANDAS. Un decodificador de PSK31
         * pierde SIEMPRE las primeras letras de una emision: el reloj de
         * simbolo y la sintonia fina tardan un par de decenas de simbolos
         * en engancharse, y eso son cuatro o cinco caracteres. Pedir la
         * tira entera daba un acantilado falso en el banco -"4 de 4" y
         * justo debajo "0 de 4"- cuando lo que pasaba de verdad era que
         * a esa señal le faltaban las dos primeras letras y las
         * veinticuatro siguientes estaban bien.
         */
        const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        double amp;

        for (amp = 8.0; amp >= 0.05; amp /= 2.0) {
            int total = 0, k;

            for (k = 0; k < 4; k++) {
                (void)prueba(t, 0.0, amp, (unsigned)(200 + k), 1, sal, sizeof sal);
                total += seguidos(t, sal);
            }
            {
                uint8_t c; int16_t e;
                int pc = (total * 100) / (4 * (int)strlen(t));

                psk31_diag(&c, &e);
                printf("    amplitud %5.2f  (%+5.1f dB en 2,5 kHz)  ->  %3d%% de las letras"
                       "   cal %3u  err %+6.2f Hz\n",
                       amp, 10.0 * log10(amp * amp * 2.4), pc, c, e / 100.0);
                if (amp >= 1.0) {
                    exige(pc >= 90, "con señal clara tienen que salir casi todas");
                }
            }
        }
    }

  printf("\n  y lo que NO puede pasar: escribir con ruido puro\n");
    {
        static signed char sim[MAX_SIM];
        int n, k, sucios = 0;

        for (k = 0; k < 6; k++) {
            psk31_start(12000.0f);
            psk31_set_centro_hz(1000.0f);
            n = a_simbolos("", 300, sim);
            emite(sim, n, 1000.0, 0.0, 0.0, (unsigned)(900 + k), 600);
            recoge(sal, sizeof sal);
            if (sal[0] != '\0') {
                sucios++;
                printf("      se invento \"%s\"\n", sal);
            }
        }
        printf("      %d de 6 tandas de ruido escribieron algo\n", sucios);
        exige(sucios == 0, "seis tandas de ruido y ni una letra");
    }

  printf("\n  y con ruido puro A LA ESCALA DE LA RADIO, la sintonia no se va\n");
    {
        /*
         * *** El fallo que este banco no veia, 30/09/2026. ***
         *
         * El AFC solo debe corregir cuando lo que llega SE PARECE a PSK31,
         * y esa puerta es la coherencia. Con el audio a la escala de la
         * radio, el calculo de la coherencia se desbordaba a +inf y la
         * puerta pasaba siempre: el seguimiento se iba a los topes de
         * +-12 Hz escuchando ruido, y la rama que devuelve la sintonia a
         * donde dijo el usuario no se alcanzaba nunca.
         *
         * Aqui se mide lo unico que importa desde fuera: despues de un rato
         * de ruido, cuanto se ha movido la sintonia.
         */
        /*
         * SE MIDE LA CONSECUENCIA, NO EL NUMERO DE DENTRO. Lo que de verdad
         * importa no es cuanto vale el seguimiento sino si, DESPUES de un
         * rato de ruido, una emision de verdad se sigue decodificando. Si
         * el seguimiento se ha ido a los topes, no: este mismo banco mide
         * mas arriba que a +-9 Hz ya no sale el texto.
         *
         * Por eso aqui no se llama a psk31_start() entre el ruido y la
         * señal: la gracia es que el receptor llegue a la emision con lo
         * que el ruido le haya dejado puesto, igual que en la radio.
         */
        static signed char sim[MAX_SIM];
        const char *t2 = "CQ CQ DE EA4XYZ";
        int n, k, malos = 0;

        s_ruido_esc = 18000.0;         /* AGC_TARGET de demod_am.c */
        for (k = 0; k < 6; k++) {
            psk31_start(12000.0f);
            psk31_set_centro_hz(1000.0f);

            n = a_simbolos("", 400, sim);          /* primero, ruido a solas */
            emite(sim, n, 1000.0, 0.0, 0.0, (unsigned)(1700 + k), 600);
            recoge(sal, sizeof sal);

            n = a_simbolos(t2, 48, sim);           /* y ahora la emision */
            emite(sim, n, 1000.0, 0.0, 18000.0, (unsigned)(1800 + k), 600);
            recoge(sal, sizeof sal);
            if (cuantos_cuadran(t2, sal) == 0) {
                malos++;
                printf("      tras el ruido ya no lee: \"%s\"\n", sal);
            }
        }
        s_ruido_esc = 1.0;
        printf("      %d de 6 tandas se quedaron sordas tras el ruido\n", malos);
        exige(malos == 0,
              "tras un rato de ruido fuerte sigue leyendo una emision limpia");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

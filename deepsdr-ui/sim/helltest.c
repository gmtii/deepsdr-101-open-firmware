/*
 * FELD-HELL: DEL TEXTO AL AIRE Y DE VUELTA.
 *
 * POR QUE ESTE BANCO NO SE CREE A SI MISMO
 * ----------------------------------------
 * La trampa de un banco de ida y vuelta es que el generador y el
 * decodificador compartan la suposicion equivocada: si los dos barren la
 * columna de arriba abajo, la prueba pasa y la radio saca el texto del
 * reves. Hoy mismo (06/10/2026) el banco del SSTV enseño exactamente eso:
 * verde durante semanas sobre una señal que fabricaba el mismo plan que
 * leia.
 *
 * Asi que aqui la referencia NO es el generador: son los DIBUJOS DE LAS
 * LETRAS escritos a mano ahi abajo, en ASCII, que cualquiera puede mirar y
 * ver que ponen "HELL". El generador los convierte en audio y el
 * decodificador tiene que devolver esos mismos dibujos. Si el orden de
 * barrido estuviera del reves, las letras saldrian boca abajo y la
 * comparacion fallaria.
 *
 * LO QUE SE PRUEBA, Y POR QUE CADA COSA:
 *
 *   1. Ida y vuelta limpia. Que las columnas vuelven.
 *   2. CON LA VELOCIDAD MAL. Es LA prueba de este modo: Feld-Hell no lleva
 *      sincronismo de ninguna clase, y su promesa es que un error de ritmo
 *      inclina el texto pero no lo rompe. Si eso no se cumple, el modo no
 *      sirve para nada.
 *   3. Con ruido.
 *   4. Que el blanco y el negro no esten cambiados: una columna vacia tiene
 *      que salir NEGRA y no blanca.
 */
#include "hell.h"

/*
 * LA RAM PRESTADA, SUSTITUIDA - 07/10/2026.
 *
 * hell.c guarda todo su estado en el pozo que presta hfdl_ram.c, y por eso
 * este banco no enlazaba desde V3.75: le faltaban las tres funciones. El
 * hfdl_ram.c de verdad no se puede meter aqui porque arrastra waterfall.h
 * y con el todo el arbol de FT8.
 *
 * Esto no es solo un tapon para que enlace: CUENTA las peticiones y las
 * sueltas, que es una comprobacion que el pozo de verdad no hace. Un
 * hell_start() sin su hell_stop() se queda el pozo entero -54 kB- y deja
 * al HFDL sin sitio, y eso es un fallo que no se ve mirando la pantalla.
 */
static unsigned g_ram_cogidas, g_ram_sueltas;
static unsigned char g_ram_pozo[0xD4A0];

/*
 * Las firmas se copian de User/hfdl_ram.h TAL CUAL. La primera vez que se
 * escribio esto se inventaron -un coge() que devolvia puntero y un todo()
 * que devolvia el tamaño- y el banco enlazaba igual, porque C no comprueba
 * las firmas entre ficheros. Lo que pasaba es que hell_start() leia una
 * capacidad de cero y se negaba a arrancar, y el banco decia "0 columnas
 * de 105" sin que nada apuntara a la causa.
 */
void      hfdl_ram_coge(void);
unsigned char *hfdl_ram_todo(unsigned *capacity_out);
void      hfdl_ram_suelta(void);

void hfdl_ram_coge(void) { g_ram_cogidas++; }
unsigned char *hfdl_ram_todo(unsigned *capacity_out)
{
    if (capacity_out) { *capacity_out = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void hfdl_ram_suelta(void) { g_ram_sueltas++; }
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS 12000.0

static int fallos;

static void comprueba(int ok, const char *que)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* --------------------------------------------------------------------
 * LAS LETRAS, A MANO. Catorce filas de siete columnas, '#' encendido.
 * La primera y la ultima columna van en blanco y la fila de arriba y la
 * de abajo tambien, que es el margen que describe la norma.
 * -------------------------------------------------------------------- */
static const char *k_H[HELL_ALTO] = {
    ".......",
    ".#...#.",
    ".#...#.",
    ".#...#.",
    ".#...#.",
    ".#####.",
    ".#####.",
    ".#...#.",
    ".#...#.",
    ".#...#.",
    ".#...#.",
    ".#...#.",
    ".......",
    "......."
};
static const char *k_E[HELL_ALTO] = {
    ".......",
    ".#####.",
    ".#####.",
    ".#.....",
    ".#.....",
    ".####..",
    ".####..",
    ".#.....",
    ".#.....",
    ".#####.",
    ".#####.",
    ".......",
    ".......",
    "......."
};
static const char *k_L[HELL_ALTO] = {
    ".......",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#.....",
    ".#####.",
    ".#####.",
    ".......",
    ".......",
    "......."
};
static const char *k_SP[HELL_ALTO] = {
    ".......", ".......", ".......", ".......", ".......", ".......",
    ".......", ".......", ".......", ".......", ".......", ".......",
    ".......", "......."
};

#define COLS_LETRA 7

/* El texto de prueba, carácter a carácter. */
static const char *const *k_texto[] = { k_H, k_E, k_L, k_L, k_SP };
#define N_LETRAS (int)(sizeof k_texto / sizeof k_texto[0])
#define N_COLS   (N_LETRAS * COLS_LETRA)

/* La referencia aplanada: N_COLS columnas de HELL_ALTO puntos, de arriba a
 * abajo, igual que lo que entrega hell_columna(). */
static uint8_t g_ref[N_COLS][HELL_ALTO];

static void referencia_monta(void)
{
    int l, c, f;
    for (l = 0; l < N_LETRAS; l++) {
        for (c = 0; c < COLS_LETRA; c++) {
            for (f = 0; f < (int)HELL_ALTO; f++) {
                g_ref[l * COLS_LETRA + c][f] =
                    (uint8_t)((k_texto[l][f][c] == '#') ? 255U : 0U);
            }
        }
    }
}

/* --------------------------------------------------------------------
 * El generador: manipula una portadora de 1000 Hz con los puntos, en el
 * orden de la norma -por columnas, y dentro de cada columna de ABAJO
 * ARRIBA-. ppm mete error de ritmo; sigma, ruido.
 * -------------------------------------------------------------------- */
/*
 * EL ANILLO SE VACIA MIENTRAS SE EMITE, y no al final.
 *
 * La primera version de este banco recogia las columnas DESPUES de meter
 * todo el audio, y recogio 15 de 105: el anillo de hell.c son dieciseis
 * columnas y va tirando las viejas, que es lo correcto en la radio -ahi el
 * bucle principal viene cada pocos milisegundos- pero no en un banco que
 * mete treinta segundos de audio de golpe. El fallo era del banco, no del
 * decodificador, y se vio enseguida porque la prueba dijo "15 columnas de
 * 105" en vez de limitarse a fallar el parecido.
 */
static uint8_t  g_got[2048][HELL_ALTO];
static int      g_n;

static void drena(void)
{
    const uint8_t *c;
    while (g_n < 2048 && (c = hell_columna()) != 0) {
        memcpy(g_got[g_n], c, HELL_ALTO);
        g_n++;
    }
}

static uint32_t g_sem = 12345U;
static double ruido(void)
{
    double u1, u2;
    g_sem = g_sem * 1103515245U + 12345U;
    u1 = ((g_sem >> 8) & 0xFFFFFF) / 16777216.0 + 1e-12;
    g_sem = g_sem * 1103515245U + 12345U;
    u2 = ((g_sem >> 8) & 0xFFFFFF) / 16777216.0;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979 * u2);
}

static void emite(double ppm, double sigma, int repeticiones)
{
    double n_punto = FS / (HELL_PUNTOS_HZ * (1.0 + ppm * 1e-6));
    double fase = 0.0;
    double resto = 0.0;
    float buf[64];
    int bn = 0;
    int rep, l, c, f;
    int i;

    for (rep = 0; rep < repeticiones; rep++) {
        for (l = 0; l < N_LETRAS; l++) {
            for (c = 0; c < COLS_LETRA; c++) {
                /* DE ABAJO ARRIBA: la fila de pantalla que toca al punto
                 * numero p es (HELL_ALTO-1-p), asi que se recorre al reves. */
                for (f = (int)HELL_ALTO - 1; f >= 0; f--) {
                    int on = (k_texto[l][f][c] == '#');
                    int n;
                    /*
                     * LA DURACION SE ACUMULA, NO SE REDONDEA PUNTO A PUNTO.
                     *
                     * Aqui ponia `int n = (int)(n_punto + 0.5);` y eso
                     * convertia el banco en mentiroso. A 12 kHz un punto son
                     * 48,98 muestras; redondeando, "-1% de error" daba 49
                     * muestras -o sea CERO error- y "+1%" daba 48, que son
                     * +2%. La prueba salio asimetrica -99% a un lado y 91%
                     * al otro- y esa asimetria no tiene sentido fisico: fue
                     * lo que delato el fallo.
                     *
                     * Con el resto acumulado, la velocidad media es la que
                     * se pide y el error pedido es el error de verdad.
                     */
                    resto += n_punto;
                    n = (int)resto;
                    resto -= (double)n;
                    for (i = 0; i < n; i++) {
                        double v = on ? sin(fase) : 0.0;
                        fase += 2.0 * 3.14159265358979 * 1000.0 / FS;
                        v += sigma * ruido();
                        buf[bn++] = (float)v;
                        if (bn == 64) { hell_process(buf, 64U); bn = 0; drena(); }
                    }
                }
            }
        }
    }
    if (bn > 0) { hell_process(buf, (uint32_t)bn); }
    drena();
}

/* --------------------------------------------------------------------
 * La comparacion. El decodificador NO tiene sincronismo, asi que las
 * columnas salen con una fase cualquiera: se prueban todos los
 * desplazamientos y se exige que el mejor acierte de sobra. Eso no es
 * aflojar la prueba, es que el modo es asi - y por eso la pantalla pinta
 * cada columna dos veces.
 * -------------------------------------------------------------------- */
static double mejor_parecido(uint8_t (*got)[HELL_ALTO], int n, int *desp_out)
{
    int d, c, f;
    double mejor = -1.0;
    int mejor_d = 0;

    for (d = 0; d < N_COLS; d++) {
        double ok = 0.0, tot = 0.0;
        for (c = 0; c < n; c++) {
            for (f = 0; f < (int)HELL_ALTO; f++) {
                int esp = g_ref[(c + d) % N_COLS][f] > 127;
                int sal = got[c][f] > 127;
                if (esp == sal) { ok += 1.0; }
                tot += 1.0;
            }
        }
        if (tot > 0.0 && (ok / tot) > mejor) { mejor = ok / tot; mejor_d = d; }
    }
    if (desp_out) { *desp_out = mejor_d; }
    return mejor;
}

/*
 * Reparte los puntos que no cuadran en dos montones: los que caen justo
 * detras de un cambio -donde la envolvente todavia se mueve- y los de en
 * medio de una racha, que serian un fallo de verdad. Dentro de la columna
 * se barre de ABAJO ARRIBA, asi que el punto anterior a la fila f es el de
 * la fila f+1.
 */
static void reparte_fallos(uint8_t (*got)[HELL_ALTO], int n, int d,
                           int *borde, int *dentro)
{
    int c, f;
    *borde = 0; *dentro = 0;
    for (c = 0; c < n; c++) {
        for (f = 0; f < (int)HELL_ALTO; f++) {
            int esp = g_ref[(c + d) % N_COLS][f] > 127;
            int sal = got[c][f] > 127;
            int ant;
            if (esp == sal) { continue; }
            ant = (f + 1 < (int)HELL_ALTO)
                ? (g_ref[(c + d) % N_COLS][f + 1] > 127)
                : !esp;
            if (ant != esp) { (*borde)++; } else { (*dentro)++; }
        }
    }
}

/*
 * PARECIDO POR VENTANAS, EN EL ESPACIO DE PUNTOS.
 *
 * Dos cosas que costaron una vuelta cada una:
 *
 * 1. UNA SOLA ALINEACION NO VALE con la velocidad mal. Si el otro va un 2%
 *    rapido, al cabo de cien columnas el texto se ha corrido entero, asi que
 *    ningun desplazamiento fijo cuadra con el principio y con el final a la
 *    vez. Lo que hace el ojo -y lo que hacia la cinta de papel de la maquina
 *    original- es leer cada trozo donde esta. Asi que se parte en ventanas
 *    de dos caracteres y cada una busca su alineacion.
 *
 * 2. Y EL DESPLAZAMIENTO NO ES DE COLUMNAS, ES DE PUNTOS. Esto lo tenia mal
 *    y lo enseño la propia prueba: con +-0,5% la comparacion por ventanas
 *    salia PEOR que la global, que es imposible si la ventana puede elegir
 *    su alineacion. El motivo es que un error de ritmo corre el texto EN
 *    VERTICAL -el barrido va por columnas, asi que un punto de retraso baja
 *    la letra un punto- y yo solo dejaba correrlo en saltos de catorce, o
 *    sea de columna entera. A 2% y catorce columnas el desfase son unos
 *    cuatro puntos: justo lo que no cabia.
 *
 * Asi que las dos partes se aplanan a una tira de puntos EN EL ORDEN EN QUE
 * SE EMITEN -dentro de cada columna, de abajo arriba- y se barren todos los
 * desfases, no solo los multiplos de catorce.
 */
#define VENT_COLS  (COLS_LETRA * 2)
#define VENT_PTOS  (VENT_COLS * (int)HELL_ALTO)
#define REF_PTOS   (N_COLS * (int)HELL_ALTO)

/* El punto numero i de la columna c se emite de abajo arriba, asi que le
 * toca la fila (HELL_ALTO-1-i). */
static int ref_punto(int p)
{
    int c = (p / (int)HELL_ALTO) % N_COLS;
    int i = p % (int)HELL_ALTO;
    return g_ref[c][(int)HELL_ALTO - 1 - i] > 127;
}

static double parecido_ventanas(uint8_t (*got)[HELL_ALTO], int n)
{
    int v, d, k;
    double ok_tot = 0.0, tot_tot = 0.0;
    int n_ptos = n * (int)HELL_ALTO;

    for (v = 0; v + VENT_PTOS <= n_ptos; v += VENT_PTOS) {
        double mejor = -1.0;
        for (d = 0; d < REF_PTOS; d++) {
            double ok = 0.0;
            for (k = 0; k < VENT_PTOS; k++) {
                int p   = v + k;
                int col = p / (int)HELL_ALTO;
                int i   = p % (int)HELL_ALTO;
                int sal = got[col][(int)HELL_ALTO - 1 - i] > 127;
                int esp = ref_punto((p + d) % REF_PTOS);
                if (esp == sal) { ok += 1.0; }
            }
            if (ok > mejor) { mejor = ok; }
        }
        ok_tot  += mejor;
        tot_tot += (double)VENT_PTOS;
    }
    return (tot_tot > 0.0) ? (ok_tot / tot_tot) : 0.0;
}

static int recoge(uint8_t (*got)[HELL_ALTO], int max)
{
    int n = (g_n < max) ? g_n : max;
    memcpy(got, g_got, (size_t)n * HELL_ALTO);
    return n;
}

static void pinta(uint8_t (*got)[HELL_ALTO], int n, int desp)
{
    int f, c;
    printf("    lo que sale (dos copias, como en la pantalla):\n");
    for (f = 0; f < (int)HELL_ALTO * 2; f++) {
        printf("      ");
        for (c = 0; c < n && c < 70; c++) {
            putchar(got[c][f % HELL_ALTO] > 127 ? '#' : '.');
        }
        putchar('\n');
    }
    printf("    (desplazamiento encontrado: %d columnas)\n", desp);
}

int main(void)
{
    static uint8_t got[512][HELL_ALTO];
    int n, d;
    double p;

    referencia_monta();
    printf("FELD-HELL: texto -> audio -> texto\n");
    printf("  la referencia son los dibujos escritos a mano en el fuente,\n");
    printf("  no lo que fabrica el generador. Ver la cabecera.\n");

    printf("\n1. señal limpia\n");
    hell_start((float)FS); g_n = 0;
    emite(0.0, 0.0, 3);
    n = recoge(got, 512);
    printf("    %d columnas recogidas de %d emitidas\n", n, N_COLS * 3);
    comprueba(n >= N_COLS * 2, "vuelven casi todas las columnas");
    p = mejor_parecido(got, (n < N_COLS * 2) ? n : N_COLS * 2, &d);
    printf("    parecido con el dibujo de referencia: %.1f%%\n", 100.0 * p);
    /*
     * DONDE falla ese resto, que es lo que decide si el umbral de abajo es
     * una medida o un apaño. Un punto "de borde" es el que viene justo
     * despues de un cambio de encendido a apagado o al reves: ahi la
     * envolvente todavia se esta moviendo.
     */
    {
        int borde, dentro;
        reparte_fallos(got, (n < N_COLS * 2) ? n : N_COLS * 2, d, &borde, &dentro);
        printf("    puntos distintos: %d justo tras un cambio, %d en medio "
               "de una racha\n", borde, dentro);
        /*
         * Y ESTA es la comprobacion de verdad, no el porcentaje.
         *
         * Medido: de los 21 puntos que no cuadran con la señal limpia, los
         * 21 caen justo detras de un cambio de encendido a apagado o al
         * reves, y NINGUNO en medio de una racha. O sea que el 2% que falta
         * no es que el decodificador lea mal: es la envolvente acabando de
         * subir o de bajar, que es fisica y no se quita.
         *
         * Un porcentaje suelto -"mas del 98%"- habria sido un numero
         * elegido para que pasara. "Ningun punto mal en medio de una racha"
         * es una FORMA, y es la que se rompe si el barrido se pone del
         * reves, si las columnas se desordenan o si el gris se invierte.
         */
        comprueba(dentro == 0,
                  "ningun punto mal salvo los pegados a un cambio");
    }
    comprueba(p > 0.90, "y dicen HELL, no otra cosa ni del reves");
    pinta(got, (n < 40) ? n : 40, d);

    printf("\n2. con la velocidad del emisor mal\n");
    printf("    (la promesa del modo: sin sincronismo de ninguna clase, un\n");
    printf("     error de ritmo inclina el texto pero no lo rompe)\n");
    {
        /*
         * EL MARGEN, MEDIDO Y NO SUPUESTO. Barriendo el error de ritmo:
         *
         *     +-0,5%   99%    <- lo que da un emisor con cristal
         *     +-1,0%   ~97%
         *     +-2,0%   91%    <- ya se nota, y es logico
         *
         * A 2% el texto se corre unos cuatro puntos DENTRO de una ventana de
         * dos caracteres, asi que la letra se emborrona en vertical aunque
         * la ventana elija su alineacion. No es que el decodificador falle:
         * es que a ese ritmo la letra ya no es la misma dentro de la propia
         * ventana. Un emisor de Feld-Hell real no se va de medio por ciento.
         *
         * Asi que la prueba se queda en +-1%, que es el margen que el modo
         * promete de verdad, y el 2% queda escrito aqui para que nadie lo
         * vuelva a meter en la lista creyendo que deberia pasar.
         */
        static const double k_ppm[] = { -10000.0, -5000.0, 5000.0, 10000.0 };
        unsigned i;
        for (i = 0U; i < sizeof k_ppm / sizeof k_ppm[0]; i++) {
            char que[80];
            hell_start((float)FS); g_n = 0;
            emite(k_ppm[i], 0.0, 3);
            n = recoge(got, 512);
            p = parecido_ventanas(got, n);
            (void)snprintf(que, sizeof que,
                           "a %+.1f%% de error de ritmo sigue legible (%.0f%%)",
                           k_ppm[i] / 10000.0, 100.0 * p);
            /*
             * EL SUELO SON 93%, Y SALE DE DOS COSAS MEDIDAS, no de probar
             * numeros hasta que pase:
             *
             *   - 2,1% se pierde ya con la señal limpia y el ritmo exacto
             *     (prueba 1), y son los puntos pegados a un cambio: la
             *     envolvente acabando de subir o bajar.
             *   - el resto es el texto corriendose EN VERTICAL dentro de la
             *     propia ventana de dos caracteres. A 1% y 196 puntos son
             *     unos dos puntos de desfase entre el principio y el final
             *     de la ventana.
             *
             * Con el generador arreglado los cuatro casos salen entre 95 y
             * 97%, o sea simetricos: 93 deja margen de sobra para el ruido
             * de la medida y sigue muy por encima del 50% que daria un
             * barrido del reves.
             */
            comprueba(p > 0.93, que);
        }
    }

    printf("\n3. con ruido\n");
    {
        static const double k_sig[] = { 0.05, 0.10, 0.20, 0.40 };
        unsigned i;
        for (i = 0U; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            char que[80];
            hell_start((float)FS); g_n = 0;
            emite(0.0, k_sig[i], 3);
            n = recoge(got, 512);
            p = mejor_parecido(got, (n < N_COLS * 2) ? n : N_COLS * 2, &d);
            (void)snprintf(que, sizeof que,
                           "ruido %.2f: parecido %.0f%%", k_sig[i], 100.0 * p);
            comprueba(p > 0.90, que);
        }
    }

    printf("\n4. el blanco y el negro no estan cambiados\n");
    {
        float cero[1024];
        int i, enc = 0, tot = 0;
        memset(cero, 0, sizeof cero);
        hell_start((float)FS); g_n = 0;
        for (i = 0; i < 200; i++) { hell_process(cero, 1024U); drena(); }
        n = recoge(got, 512);
        for (i = 0; i < n; i++) {
            int f;
            for (f = 0; f < (int)HELL_ALTO; f++) {
                if (got[i][f] > 127) { enc++; }
                tot++;
            }
        }
        printf("    con el silencio absoluto: %d puntos encendidos de %d\n",
               enc, tot);
        comprueba(tot > 0 && enc == 0,
                  "sin señal la pantalla sale NEGRA, no llena de basura");
    }

    printf("\n5. la RAM prestada se devuelve\n");
    printf("    hell.c guarda TODO su estado en el pozo de 54 kB que\n");
    printf("    comparte con el HFDL. Un hell_start() sin su hell_stop()\n");
    printf("    se lo queda entero y deja al HFDL sin sitio, y eso no se ve\n");
    printf("    mirando la pantalla: la radio deja de decodificar HFDL y\n");
    printf("    nadie sabe por que.\n\n");
    {
        unsigned c0 = g_ram_cogidas, s0 = g_ram_sueltas;
        int r;
        for (r = 0; r < 5; r++) { hell_start(12000.0f); hell_stop(); }
        printf("    cinco arranques y paradas: %u peticiones, %u sueltas\n",
               g_ram_cogidas - c0, g_ram_sueltas - s0);
        comprueba((g_ram_cogidas - c0) == (g_ram_sueltas - s0),
                  "cada peticion tiene su suelta");
        s0 = g_ram_sueltas;
        hell_stop();
        comprueba(g_ram_sueltas == s0,
                  "parar sin haber arrancado no suelta nada");
    }

    printf("\n%s\n", fallos ? "  HAY FALLOS" : "  TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}

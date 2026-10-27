/*
 * ======================================================================
 * LA MEDIDA CON LA QUE EL BARRIDO DECIDE - 26/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE: el barrido de memorias se entrego DOS VECES mal, y
 * las dos por no medir esto. ***
 *
 *   1a. preguntaba si el silenciador estaba ABIERTO. En onda corta el
 *       ruido de banda ya abre cualquier umbral razonable, asi que se
 *       paraba en la primera memoria con ruido.
 *
 *   1b. y peor: el silenciador SOLO se calcula en AM y en FM estrecha
 *       -lo dice la guarda de demod_am.c-, asi que en banda lateral, que
 *       es donde esta casi todo lo que se barre en onda corta, su lectura
 *       se queda CONGELADA en la ultima que tomo. Preguntarle devolvia
 *       siempre lo mismo pasara lo que pasara.
 *
 * Las dos veces el razonamiento era correcto sobre una medida que no
 * existia. Asi que esto no mira el barrido: mira LA MEDIDA, que es lo que
 * no se habia mirado.
 *
 * QUE EXIGE, y es lo minimo que hace falta para que un barrido funcione:
 *
 *   1. que la lectura SE MUEVA en banda lateral (la del silenciador no);
 *   2. que una emisora quede por encima del ruido con margen de sobra
 *      -el barrido pide SCAN_MARGEN_DB = 10 dB-;
 *   3. que dos frecuencias con SOLO ruido lean casi lo mismo: si el
 *      suelo bailara mas que el margen, el barrido pararia en cualquier
 *      sitio, que es justo lo que pasaba;
 *   4. y que valga en AM, en CW y en FM estrecha, no solo en una.
 */
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * EL MARGEN Y LOS TIEMPOS DEL BARRIDO, SACADOS DE main.c.
 *
 * Estaban copiados a mano con una nota que decia "si alla cambia, aqui
 * hay que cambiarlo". Eso es exactamente lo que no funciona: el dia que
 * cambiaron -27/10/2026, al acortar la vuelta- esta prueba se habria
 * quedado midiendo contra los numeros de la semana anterior y habria
 * seguido saliendo verde. Ahora los saca tools/barrido_dump.py del
 * firmware en cada compilacion.
 */
#include "barrido_consts.h"

/* A 96 kHz y 256 muestras por bloque son 2,667 ms por bloque, o sea 375
 * bloques por segundo. De ahi salen los dos de abajo, que son los mismos
 * tiempos contados en bloques. */
#define BLOQUES_POR_S   375u
#define MS_A_BLOQUES(ms) (((ms) * BLOQUES_POR_S) / 1000u)
#define ESPERA_BLOQUES  MS_A_BLOQUES(SCAN_ESPERA_MS)
#define MIRA_BLOQUES    MS_A_BLOQUES(SCAN_MIRA_MS)

static int fallos = 0;
static int16_t raw[2 * SDR_RX_BLOCK_SAMPLES_WFM];
static unsigned sem = 1u;

static double gauss(void)
{
    double u1, u2;
    sem = sem * 1103515245u + 12345u; u1 = ((sem >> 8) & 0xFFFFFF) / 16777216.0;
    sem = sem * 1103515245u + 12345u; u2 = ((sem >> 8) & 0xFFFFFF) / 16777216.0;
    if (u1 < 1e-12) { u1 = 1e-12; }
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/*
 * Corre `bloques` bloques con una portadora de amplitud `amp` -0 = solo
 * ruido- y ruido de desviacion `sigma`, y devuelve la lectura del nivel.
 */
static float corre_pv(int modo, double amp, double sigma, unsigned semilla,
                      int silabas, float *pico, float *valle);

/* La misma visita, pero con la duracion y la fase de las silabas a
 * eleccion: es lo que hace falta para medir CUANTO hay que mirar, que es
 * la seccion 12. corre_pv() es esta con los valores de siempre. */
static float visita(int modo, double amp, double sigma, unsigned semilla,
                    int silabas, double fase_sil, unsigned espera_bl,
                    unsigned mira_bl, float *pico, float *valle);

static float corre(int modo, double amp, double sigma, unsigned bloques,
                   unsigned semilla)
{
    uint32_t n = SDR_RX_BLOCK_SAMPLES;
    double fs = 96000.0, fase = 0.0, dw;
    double tono = 1000.0;
    unsigned b, k;

    /*
     * LA SEÑAL VA EN LA BANDA LATERAL QUE TOCA, y esto NO es un detalle
     * del banco de pruebas: un receptor en banda lateral inferior tiene
     * que RECHAZAR lo que este por encima de la portadora. Metiendolo
     * siempre por arriba, la prueba de LSB medía el rechazo -0,0 dB- y lo
     * daba por fallo del barrido. El rechazo es correcto; la prueba no.
     */
    if (modo == DEMOD_MODE_LSB) { tono = -1000.0; }

    demod_am_init();
    demod_am_set_active_rate(0u);
    demod_am_set_rf_doble(0u);
    demod_am_set_mode((demod_mode_t)modo);
    demod_am_set_mix_hz(24000.0f);
    demod_am_set_nivel_medir(1u);
    sem = semilla;
    cons_captura_tramas(n);

    dw = 2.0 * M_PI * (24000.0 + tono) / fs;
    for (b = 0; b < bloques; b++) {
        for (k = 0; k < n; k++) {
            double i = amp * cos(fase) + sigma * gauss();
            double q = amp * sin(fase) + sigma * gauss();
            raw[2 * k]      = (int16_t)i;
            raw[2 * k + 1u] = (int16_t)q;
            fase += dw;
        }
        demod_am_process_raw(raw);
    }
    /*
     * LO MISMO QUE LEE EL BARRIDO, modo por modo. Es copia de scan_lee()
     * de main.c a proposito: si alla cambia el reparto y aqui no, esta
     * prueba deja de probar el barrido y nadie se entera. Ver el
     * comentario de alla para por que no es la misma lectura en todos.
     */
    if (modo == DEMOD_MODE_AM || modo == DEMOD_MODE_NFM) {
        return demod_am_get_squelch_level_db();
    }
    return demod_am_get_nivel_db();
}

/*
 * ======================================================================
 * LO QUE EL BARRIDO MIRA DE VERDAD: EL PICO Y EL VALLE DE UNA VISITA
 * ======================================================================
 *
 * corre() de arriba devuelve la lectura al final de muchos bloques, que
 * sirve para comparar dos sitios pero NO es lo que hace el barrido. El
 * barrido espera un cuarto de segundo, mira UN SEGUNDO y apunta lo mas
 * alto y lo mas callado que ve -scan_mira()-. Eso es lo que hay que
 * medir, porque es de donde sale el suelo de cada memoria.
 *
 * `silabas` enciende una envolvente de voz: alguien hablando no es un
 * tono continuo, abre y cierra unas dos veces y media por segundo, y las
 * pausas son justo lo que deja ver el ruido de fondo por debajo.
 */
static float corre_pv(int modo, double amp, double sigma, unsigned semilla,
                      int silabas, float *pico, float *valle)
{
    return visita(modo, amp, sigma, semilla, silabas, 0.0,
                  ESPERA_BLOQUES, MIRA_BLOQUES, pico, valle);
}

static float visita(int modo, double amp, double sigma, unsigned semilla,
                    int silabas, double fase_sil, unsigned espera_bl,
                    unsigned mira_bl, float *pico, float *valle)
{
    uint32_t n = SDR_RX_BLOCK_SAMPLES;
    double fs = 96000.0, fase = 0.0, dw, t = fase_sil;
    double tono = 1000.0;
    unsigned b, k;
    float pi = -9999.0f, va = 9999.0f;

    if (modo == DEMOD_MODE_LSB) { tono = -1000.0; }

    demod_am_init();
    demod_am_set_active_rate(0u);
    demod_am_set_rf_doble(0u);
    demod_am_set_mode((demod_mode_t)modo);
    demod_am_set_mix_hz(24000.0f);
    demod_am_set_nivel_medir(1u);
    sem = semilla;
    cons_captura_tramas(n);

    dw = 2.0 * M_PI * (24000.0 + tono) / fs;
    for (b = 0; b < espera_bl + mira_bl; b++) {
        /* La envolvente va por bloque y no por muestra: una silaba dura
         * cientos de bloques, asi que trocearla mas fino no cambia nada y
         * si enturbiaria de donde sale cada numero. */
        double a = amp;

        if (silabas) {
            a = (sin(2.0 * M_PI * 2.5 * t) > 0.0) ? amp : 0.0;
        }
        for (k = 0; k < n; k++) {
            double i = a * cos(fase) + sigma * gauss();
            double q = a * sin(fase) + sigma * gauss();
            raw[2 * k]      = (int16_t)i;
            raw[2 * k + 1u] = (int16_t)q;
            fase += dw;
        }
        t += (double)n / fs;
        demod_am_process_raw(raw);
        if (b >= espera_bl) {
            float v = (modo == DEMOD_MODE_AM || modo == DEMOD_MODE_NFM)
                      ? demod_am_get_squelch_level_db()
                      : demod_am_get_nivel_db();

            if (v > pi) { pi = v; }
            if (v < va) { va = v; }
        }
    }
    if (pico)  { *pico  = pi; }
    if (valle) { *valle = va; }
    return pi - va;
}

/*
 * ======================================================================
 * 11. CUANTO TARDA LA MEDIDA EN ASENTARSE DESPUES DE UN SALTO
 * ======================================================================
 *
 * *** POR QUE: EL BARRIDO IBA LENTO ***
 * 27/10/2026, visto en la radio: en los canales de servicio se le
 * escapaban las frases cortas.
 *
 * Lo que importa de verdad no es lo que tarde UNA memoria: es LA VUELTA
 * ENTERA. Si alguien suelta una frase de tres segundos en un canal de
 * servicio, el barrido la caza solo si pasa por ese canal mientras dura,
 * o sea que la probabilidad es, mas o menos, lo que dura la frase
 * dividido por lo que dura la vuelta. Con quince memorias a 1,25 s son
 * dieciocho segundos y medio de vuelta: una frase de tres segundos se
 * caza una de cada seis veces. Por eso no se le da tiempo.
 *
 * Y los dos numeros que hacen la vuelta -lo que se espera a que la cadena
 * se asiente y lo que se mira despues- estaban PUESTOS A OJO y con
 * holgura. Asi que aqui se miden: se da un salto de verdad y se cuenta
 * desde que bloque la lectura ya no se mueve.
 *
 * El criterio es honesto: el PRIMER bloque a partir del cual TODAS las
 * lecturas que vienen despues se quedan dentro de `tol` del valor final.
 * No vale "ya casi llega", porque si vuelve a moverse el barrido decide
 * con un numero que todavia estaba de camino.
 */
#define ASIENTA_TOL_DB 0.5f
#define ASIENTA_MAX    400u
#define ASIENTA_VENT    16u   /* 43 ms de alisado, ver abajo */

static float g_curva[ASIENTA_MAX];

/*
 * Da un salto y devuelve en cuantos bloques se asienta la lectura.
 *
 * `rehace` = 1 es el salto que CAMBIA DE MODO: la cadena se deshace y se
 * vuelve a armar, con su silencio de entrada incluido. `rehace` = 0 es el
 * salto que solo mueve el oscilador, que es el caso corriente dentro de
 * un banco: la cadena sigue viva y lo unico que cambia es lo que entra.
 */
static unsigned asienta(int modo, double amp, double sigma, unsigned semilla,
                        int rehace, unsigned bloques, float *final_db)
{
    uint32_t n = SDR_RX_BLOCK_SAMPLES;
    double fs = 96000.0, fase = 0.0, dw;
    double tono = (modo == DEMOD_MODE_LSB) ? -1000.0 : 1000.0;
    unsigned b, k, i;
    float fin;

    if (bloques > ASIENTA_MAX) { bloques = ASIENTA_MAX; }
    dw = 2.0 * M_PI * (24000.0 + tono) / fs;

    demod_am_init();
    demod_am_set_active_rate(0u);
    demod_am_set_rf_doble(0u);
    demod_am_set_mode((demod_mode_t)modo);
    demod_am_set_mix_hz(24000.0f);
    demod_am_set_nivel_medir(1u);
    sem = semilla;
    cons_captura_tramas(n);

    if (!rehace) {
        /* La memoria ANTERIOR: otro ruido y sin emisora, y la cadena ya
         * corriendo. Lo que se mide luego es lo que tarda en olvidarla. */
        for (b = 0; b < 200u; b++) {
            for (k = 0; k < n; k++) {
                raw[2 * k]      = (int16_t)(1800.0 * gauss());
                raw[2 * k + 1u] = (int16_t)(1800.0 * gauss());
            }
            demod_am_process_raw(raw);
        }
    }

    /* Y aqui empieza a contar: esto es el salto. */
    for (b = 0; b < bloques; b++) {
        for (k = 0; k < n; k++) {
            double ii = amp * cos(fase) + sigma * gauss();
            double qq = amp * sin(fase) + sigma * gauss();
            raw[2 * k]      = (int16_t)ii;
            raw[2 * k + 1u] = (int16_t)qq;
            fase += dw;
        }
        demod_am_process_raw(raw);
        g_curva[b] = (modo == DEMOD_MODE_AM || modo == DEMOD_MODE_NFM)
                     ? demod_am_get_squelch_level_db()
                     : demod_am_get_nivel_db();
    }

    /*
     * EL VALOR FINAL NO ES EL ULTIMO BLOQUE, Y ESTO NO ES UN REFINAMIENTO:
     * sin ello la medida sale mal por un factor de diez.
     *
     * La lectura de un canal VACIO no se queda quieta nunca -es ruido-,
     * asi que "el primero que se sale de medio decibelio del ultimo"
     * acababa cayendo donde al ruido le dio por saltar, casi al final de
     * la tirada: salian 700 ms de asentamiento en un canal donde no hay
     * nada que asentar. Lo que se medía era el temblor, no el transitorio.
     *
     * Asi que el final es la MEDIA de la cola, y la banda de aceptacion se
     * abre a tres veces el temblor de esa misma cola cuando el temblor es
     * lo mayor. Lo que queda dentro es el transitorio de verdad.
     */
    {
        /* La cola es LA MITAD de la tirada, no los ultimos cien
         * bloques: el temblor de esta lectura es lento -ver la banda, mas
         * abajo-, asi que una cola corta cae dentro de una sola de sus
         * ondulaciones y la banda sale mas estrecha de lo que la curva se
         * mueve de verdad. */
        unsigned cola = bloques / 2u;
        double su = 0.0, s2 = 0.0, mu, sg;
        float banda;

        for (i = bloques - cola; i < bloques; i++) { su += g_curva[i]; }
        mu = su / (double)cola;
        for (i = bloques - cola; i < bloques; i++) {
            double d = g_curva[i] - mu;

            s2 += d * d;
        }
        sg = sqrt(s2 / (double)cola);
        fin = (float)mu;
        if (final_db) { *final_db = fin; }

        /*
         * Y SE MIRA LA CURVA ALISADA, no bloque a bloque. El barrido
         * tampoco decide con un bloque suelto: va apuntando el pico y el
         * valle durante toda la visita. Lo que hay que medir es cuando la
         * lectura deja de estar SESGADA por la memoria anterior, y un
         * salto suelto del ruido no es sesgo.
         *
         * LA BANDA SALE DE LA PROPIA COLA, y no de sigma partido por raiz
         * de la ventana, porque esa cuenta aqui no vale: la lectura YA
         * viene alisada de dentro -NIVEL_ALPHA = 0,9, o sea una constante
         * de unos diez bloques-, asi que dos bloques seguidos no son dos
         * medidas independientes y dividir por la raiz da una banda tres
         * o cuatro veces mas estrecha de lo que el ruido se mueve de
         * verdad. Con ella salian 800 ms de asentamiento en un canal donde
         * el transitorio se acaba a los 110.
         *
         * Asi que la banda es lo que la curva alisada se mueve EN LA COLA,
         * con un veinte por ciento de holgura. Es empirica a proposito: lo
         * que se busca es el momento en que la curva entra en el margen en
         * el que se va a quedar, y ese margen se mide, no se supone.
         */
        {
            unsigned ii;
            double peor = 0.0;

            for (ii = bloques - cola; ii + ASIENTA_VENT <= bloques; ii++) {
                double a = 0.0;
                unsigned j;

                for (j = ii; j < ii + ASIENTA_VENT; j++) { a += g_curva[j]; }
                a = fabs(a / (double)ASIENTA_VENT - mu);
                if (a > peor) { peor = a; }
            }
            banda = (float)(1.2 * peor);
        }
        (void)sg;
        if (banda < ASIENTA_TOL_DB) { banda = ASIENTA_TOL_DB; }

        /*
         * Y SE BUSCA LA PRIMERA ENTRADA EN LA BANDA, no la ultima salida.
         *
         * La diferencia importa y no es cosmetica. El transitorio de un
         * salto es una caida -o una subida, si la cadena venia muda- que
         * va en UN SOLO SENTIDO: en cuanto entra en la banda, el sesgo de
         * la memoria anterior ya se ha ido, que es lo unico que se queria
         * saber. Buscando la ULTIMA salida se contestaba por la siguiente
         * ondulacion lenta del ruido, que puede caer en cualquier parte de
         * la tirada: por eso un canal vacio en banda lateral salia con 339
         * ms de asentamiento cuando su propia curva esta plana desde los
         * 120.
         */
        for (i = 0u; i + ASIENTA_VENT <= bloques; i++) {
            double a = 0.0;
            unsigned j;

            for (j = i; j < i + ASIENTA_VENT; j++) { a += g_curva[j]; }
            a /= (double)ASIENTA_VENT;
            if (fabs(a - fin) <= banda) { return i + ASIENTA_VENT; }
        }
        return bloques;
    }
}

static void pasa(const char *que, int bien)
{
    printf("    %-54s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

static const char *k_nombre[] = { "USB", "LSB", "AM", "CW", "NFM" };
static const int   k_modo[]   = { DEMOD_MODE_USB, DEMOD_MODE_LSB,
                                  DEMOD_MODE_AM,  DEMOD_MODE_USB,
                                  DEMOD_MODE_NFM };

int main(void)
{
    unsigned m;
    char t[96];

    printf("\nLA MEDIDA CON LA QUE EL BARRIDO DECIDE\n\n");

    /* --------------------------------------------------------------
     * 1. QUE SE MUEVE EN BANDA LATERAL, QUE ES LO QUE NO HACIA
     * -------------------------------------------------------------- */
    printf("  1. EN BANDA LATERAL LA LECTURA SE MUEVE\n");
    {
        float ruido = corre(DEMOD_MODE_USB, 0.0, 300.0, 200u, 11u);
        float emis  = corre(DEMOD_MODE_USB, 3000.0, 300.0, 200u, 11u);

        printf("      solo ruido  %7.1f dB\n", (double)ruido);
        printf("      con emisora %7.1f dB\n", (double)emis);
        pasa("la lectura NO se queda congelada",
             fabsf(emis - ruido) > 1.0f);

        /* Y lo que el silenciador contesta en el mismo caso, que es lo
         * que se preguntaba antes: en banda lateral, nada. */
        printf("      el silenciador, en cambio, dice %.1f dB en los dos\n",
               (double)demod_am_get_squelch_level_db());
    }

    /* --------------------------------------------------------------
     * 2. UNA EMISORA SOBRESALE MAS QUE EL MARGEN
     * -------------------------------------------------------------- */
    printf("\n  2. UNA EMISORA SOBRESALE MAS DE %.0f dB DEL RUIDO\n",
           (double)SCAN_MARGEN_DB);
    for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
        float ruido = corre(k_modo[m], 0.0, 300.0, 200u, 11u);
        float emis  = corre(k_modo[m], 3000.0, 300.0, 200u, 11u);
        float d = emis - ruido;

        snprintf(t, sizeof t, "%s: sobresale %.1f dB", k_nombre[m], (double)d);
        pasa(t, d >= SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 3. Y DOS FRECUENCIAS CON SOLO RUIDO LEEN CASI LO MISMO
     * -------------------------------------------------------------- */
    printf("\n  3. EL SUELO NO BAILA: dos sitios con solo ruido\n");
    {
        float a = corre(DEMOD_MODE_USB, 0.0, 300.0, 200u, 11u);
        float b = corre(DEMOD_MODE_USB, 0.0, 300.0, 200u, 97u);
        float c = corre(DEMOD_MODE_USB, 0.0, 420.0, 200u, 31u);

        printf("      ruido 300 (semilla 11)  %7.1f dB\n", (double)a);
        printf("      ruido 300 (semilla 97)  %7.1f dB\n", (double)b);
        printf("      ruido 420 (40%% mas)     %7.1f dB\n", (double)c);
        snprintf(t, sizeof t, "el mismo ruido lee igual (%.2f dB de diferencia)",
                 (double)fabsf(a - b));
        pasa(t, fabsf(a - b) < 1.0f);
        /*
         * Y con 40% mas de ruido la lectura sube unos 3 dB, que SIGUE
         * siendo menos que el margen: o sea que el barrido no se para por
         * que una banda este mas ruidosa que otra. Eso es lo que separa
         * esta medida de la del silenciador, que es absoluta.
         */
        snprintf(t, sizeof t, "mas ruido no llega al margen (%.1f dB)",
                 (double)(c - a));
        pasa(t, (c - a) < SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 4. Y QUE SABE FALLAR
     * -------------------------------------------------------------- */
    printf("\n  4. Y QUE SABE FALLAR\n");
    {
        /* Una emisora RIDICULA no puede sobresalir: si esto pasara, el
         * umbral estaria comparando cualquier cosa. */
        float ruido = corre(DEMOD_MODE_USB, 0.0, 300.0, 200u, 11u);
        float flojo = corre(DEMOD_MODE_USB, 60.0, 300.0, 200u, 11u);

        printf("      una portadora de 60 cuentas sobre ruido 300: %.1f dB\n",
               (double)(flojo - ruido));
        pasa("no la da por buena", (flojo - ruido) < SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 5. Y UNA EMISORA FLOJA, QUE ES EL CASO QUE IMPORTA
     * --------------------------------------------------------------
     * Las de arriba son portadoras de 3.000 cuentas sobre ruido de 300:
     * se ven de sobra, y por eso no prueban gran cosa. Lo que un barrido
     * tiene que cazar es lo otro, una emisora que apenas se destaca.
     */
    printf("\n  5. UNA EMISORA FLOJA, 600 CUENTAS SOBRE RUIDO 300\n");
    for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
        float ruido = corre(k_modo[m], 0.0, 300.0, 200u, 11u);
        float emis  = corre(k_modo[m], 600.0, 300.0, 200u, 11u);
        float d = emis - ruido;

        snprintf(t, sizeof t, "%s: sobresale %.1f dB", k_nombre[m], (double)d);
        pasa(t, d >= SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 6. Y EL SUELO TAMPOCO BAILA EN LOS OTROS MODOS
     * --------------------------------------------------------------
     * La seccion 3 solo miraba banda lateral, y desde que la lectura no
     * es la misma en todos los modos eso ya no basta: si el suelo bailara
     * en AM o en FM estrecha mas que el margen, el barrido se pararia en
     * cualquier sitio justo en los dos modos que no se estaban mirando.
     */
    printf("\n  6. EL SUELO NO BAILA EN NINGUN MODO\n");
    for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
        float a = corre(k_modo[m], 0.0, 300.0, 200u, 11u);
        float b = corre(k_modo[m], 0.0, 300.0, 200u, 97u);

        snprintf(t, sizeof t, "%s: dos sitios con ruido leen igual (%.2f dB)",
                 k_nombre[m], (double)fabsf(a - b));
        pasa(t, fabsf(a - b) < SCAN_MARGEN_DB * 0.5f);
    }

    /* --------------------------------------------------------------
     * 7. *** EL FALLO DE LA TERCERA VEZ: PARABA EN LA SEGUNDA VUELTA ***
     * --------------------------------------------------------------
     * Habia UN SOLO suelo para el banco entero, el mas bajo de sus quince
     * memorias. La primera vuelta lo aprendia y la segunda decidia con
     * el: por eso se paraba justo entonces.
     *
     * Esto lo reproduce con numeros. Dos memorias, las dos CON SOLO
     * RUIDO, una en una banda tranquila y otra en una ruidosa -ruido 300
     * contra ruido 1800, que en onda corta no es nada raro-.
     */
    printf("\n  7. DOS MEMORIAS CON SOLO RUIDO, EN BANDAS DISTINTAS\n");
    {
        float tranquila, ruidosa, p, v;

        tranquila = corre(DEMOD_MODE_USB, 0.0, 300.0, 200u, 11u);
        ruidosa   = corre(DEMOD_MODE_USB, 0.0, 1800.0, 200u, 23u);

        printf("      banda tranquila (ruido 300)   %7.1f dB\n",
               (double)tranquila);
        printf("      banda ruidosa   (ruido 1800)  %7.1f dB\n",
               (double)ruidosa);

        /* CON EL SUELO UNICO: el suelo lo fija la tranquila y la ruidosa
         * sobresale de el. Esto TIENE que salir "pasaba", porque es el
         * fallo; si dejara de salir, esta prueba ya no prueba nada. */
        snprintf(t, sizeof t,
                 "con UN suelo para el banco se paraba aqui (%.1f dB)",
                 (double)(ruidosa - tranquila));
        pasa(t, (ruidosa - tranquila) >= SCAN_MARGEN_DB);

        /* CON EL SUELO POR MEMORIA: el de la ruidosa sale de la ruidosa.
         * Lo que se mide es su propio pico contra su propio valle. */
        (void)corre_pv(DEMOD_MODE_USB, 0.0, 1800.0, 23u, 0, &p, &v);
        printf("      la ruidosa contra SU suelo:  pico %.1f, valle %.1f\n",
               (double)p, (double)v);
        snprintf(t, sizeof t,
                 "con el suelo POR MEMORIA ya no para (%.1f dB)",
                 (double)(p - v));
        pasa(t, (p - v) < SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 8. EL SUELO DE UNA VISITA: RUIDO CONTRA ALGUIEN HABLANDO
     * --------------------------------------------------------------
     * Es lo que decide en la PRIMERA visita a una memoria, cuando todavia
     * no hay suelo aprendido: el suelo vale el valle de esa misma visita,
     * asi que la cuenta es pico menos valle. Tiene que separar el ruido
     * -que es estable- de una voz -que tiene pausas-.
     */
    printf("\n  8. EN UNA VISITA: EL RUIDO ES ESTABLE Y UNA VOZ NO\n");
    for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
        float ruido = corre_pv(k_modo[m], 0.0, 300.0, 11u, 0, 0, 0);
        float voz   = corre_pv(k_modo[m], 3000.0, 300.0, 11u, 1, 0, 0);

        snprintf(t, sizeof t, "%s: ruido %.1f dB, hablando %.1f dB",
                 k_nombre[m], (double)ruido, (double)voz);
        pasa(t, ruido < SCAN_MARGEN_DB && voz >= SCAN_MARGEN_DB);
    }

    /* --------------------------------------------------------------
     * 9. Y QUE FUERA DEL BARRIDO NO SE MIDE
     * --------------------------------------------------------------
     * Son 256 multiplicaciones y un log10f por bloque, 375 bloques por
     * segundo. Dejandolas corriendo siempre le quitaban a STANAG el
     * margen que le queda cuando esta enganchado a una señal, y la radio
     * se saturaba. Asi que la medida se enciende al arrancar el barrido y
     * se apaga al pararlo; esto comprueba que apagar APAGA de verdad y no
     * es solo un nombre.
     */
    printf("\n  9. FUERA DEL BARRIDO LA MEDIDA NO CORRE\n");
    {
        float con, sin_;
        unsigned b;

        con = corre(DEMOD_MODE_USB, 3000.0, 300.0, 200u, 11u);

        /* Lo mismo pero con la medida apagada desde el primer bloque. */
        demod_am_init();
        demod_am_set_active_rate(0u);
        demod_am_set_rf_doble(0u);
        demod_am_set_mode(DEMOD_MODE_USB);
        demod_am_set_mix_hz(24000.0f);
        demod_am_set_nivel_medir(0u);
        sem = 11u;
        cons_captura_tramas(SDR_RX_BLOCK_SAMPLES);
        {
            double fase = 0.0;
            double dw = 2.0 * M_PI * 25000.0 / 96000.0;
            unsigned k;

            for (b = 0; b < 200u; b++) {
                for (k = 0; k < SDR_RX_BLOCK_SAMPLES; k++) {
                    raw[2 * k]      = (int16_t)(3000.0 * cos(fase)
                                                + 300.0 * gauss());
                    raw[2 * k + 1u] = (int16_t)(3000.0 * sin(fase)
                                                + 300.0 * gauss());
                    fase += dw;
                }
                demod_am_process_raw(raw);
            }
        }
        sin_ = demod_am_get_nivel_db();

        printf("      encendida  %7.1f dB\n", (double)con);
        printf("      apagada    %7.1f dB\n", (double)sin_);
        pasa("apagada no se toca el numero", sin_ == 0.0f);
        pasa("y encendida si", con > 1.0f);
    }

    /* --------------------------------------------------------------
     * 10. EL SILENCIADOR DE AM/FM ESTRECHA VUELVE A ABRIR AL BAJARLO
     * --------------------------------------------------------------
     * Desde el 27/10 la metrica del silenciador NO se calcula con el
     * mando en su suelo: son 256 raices por bloque que no servian para
     * nada. Lo unico que eso puede romper es esto, y por eso se
     * comprueba: quien silencia el audio es la bandera, no el bucle, asi
     * que si el bucle deja de correr con la bandera CERRADA la radio se
     * queda muda para siempre.
     *
     * Subir, cerrar en una frecuencia vacia, bajar al suelo: tiene que
     * volver a oirse.
     * -------------------------------------------------------------- */
    printf("\n  10. BAJAR EL SILENCIADOR AL SUELO VUELVE A ABRIR\n");
    {
        uint32_t nn = SDR_RX_BLOCK_SAMPLES;
        unsigned b, k;

        demod_am_init();
        demod_am_set_active_rate(0u);
        demod_am_set_rf_doble(0u);
        demod_am_set_mode(DEMOD_MODE_AM);
        demod_am_set_mix_hz(24000.0f);
        sem = 7u;
        cons_captura_tramas(nn);

        /* Mando arriba y solo ruido flojo: tiene que cerrar. */
        demod_am_set_squelch_db(80.0f);
        for (b = 0; b < 100u; b++) {
            for (k = 0; k < nn; k++) {
                raw[2 * k]      = (int16_t)(3.0 * gauss());
                raw[2 * k + 1u] = (int16_t)(3.0 * gauss());
            }
            demod_am_process_raw(raw);
        }
        pasa("con el mando arriba y sin señal, cierra",
             demod_am_get_squelch_open() == 0u);

        /* Y al suelo: tiene que volver a abrir aunque el bucle ya no
         * corra. */
        demod_am_set_squelch_db(0.0f);
        for (b = 0; b < 10u; b++) {
            for (k = 0; k < nn; k++) {
                raw[2 * k]      = (int16_t)(3.0 * gauss());
                raw[2 * k + 1u] = (int16_t)(3.0 * gauss());
            }
            demod_am_process_raw(raw);
        }
        pasa("y al bajarlo al suelo vuelve a abrir",
             demod_am_get_squelch_open() == 1u);
    }

    /* --------------------------------------------------------------
     * 11. LO QUE TARDA EN ASENTARSE UN SALTO, MEDIDO
     * -------------------------------------------------------------- */
    printf("\n  11. CUANTO TARDA LA LECTURA EN ASENTARSE TRAS UN SALTO\n");
    printf("      (2,667 ms por bloque; tolerancia %.1f dB)\n",
           (double)ASIENTA_TOL_DB);
    {
        unsigned peor_re = 0u, peor_no = 0u;

        for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            unsigned re, no;
            float f1, f2;

            re = asienta(k_modo[m], 600.0, 300.0, 11u, 1, 300u, &f1);
            no = asienta(k_modo[m], 600.0, 300.0, 11u, 0, 300u, &f2);
            if (re > peor_re) { peor_re = re; }
            if (no > peor_no) { peor_no = no; }
            printf("      %-4s rehaciendo la cadena %3u bloques (%4.0f ms),"
                   " solo oscilador %3u (%4.0f ms)\n",
                   k_nombre[m], re, re * 2.667, no, no * 2.667);
        }
        /* Y con solo ruido, que es el caso del canal vacio: es el que
         * decide lo que cuesta una vuelta entera. */
        for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            unsigned re, no;

            re = asienta(k_modo[m], 0.0, 300.0, 11u, 1, 300u, 0);
            no = asienta(k_modo[m], 0.0, 300.0, 11u, 0, 300u, 0);
            if (re > peor_re) { peor_re = re; }
            if (no > peor_no) { peor_no = no; }
            printf("      %-4s canal vacio:          %3u bloques (%4.0f ms),"
                   " solo oscilador %3u (%4.0f ms)\n",
                   k_nombre[m], re, re * 2.667, no, no * 2.667);
        }

        printf("      lo peor: rehaciendo %u bloques (%.0f ms),"
               " solo oscilador %u (%.0f ms)\n",
               peor_re, peor_re * 2.667, peor_no, peor_no * 2.667);

        /*
         * Y ESTO ES LO QUE ATA LOS NUMEROS DE main.c A LA MEDIDA. Si
         * alguien acorta las esperas por debajo de lo que de verdad tarda
         * la lectura en asentarse, esta prueba se pone roja; y si las deja
         * con el triple de holgura que hacia falta, tambien, porque esa
         * holgura es lo que hace que el barrido se pierda las frases
         * cortas. Las dos cosas son fallos.
         */
        snprintf(t, sizeof t,
                 "SCAN_ESPERA_MODO_MS (%u ms) llega para rehacer la cadena",
                 SCAN_ESPERA_MODO_MS);
        pasa(t, (float)SCAN_ESPERA_MODO_MS >= peor_re * 2.667f);
        snprintf(t, sizeof t,
                 "SCAN_ESPERA_MS (%u ms) llega para el salto corriente",
                 SCAN_ESPERA_MS);
        pasa(t, (float)SCAN_ESPERA_MS >= peor_no * 2.667f);
        snprintf(t, sizeof t,
                 "y no sobra: menos del doble de lo medido (%.0f ms)",
                 peor_no * 2.667);
        pasa(t, (float)SCAN_ESPERA_MS < peor_no * 2.667f * 2.0f + 60.0f);
    }

    /* --------------------------------------------------------------
     * 12. Y CUANTO HAY QUE MIRAR: LA OTRA MITAD DE LA VUELTA
     * --------------------------------------------------------------
     * Mirar un segundo por memoria estaba puesto a ojo, con el argumento
     * de que "una voz tiene pausas". El argumento vale para APRENDER EL
     * SUELO -el valle de la visita- pero no para DECIDIR: con el suelo ya
     * aprendido, lo unico que hace falta es pillar al que habla encendido
     * una vez, y eso no tarda un segundo.
     *
     * Asi que se mide: con el suelo puesto, cuantas visitas de cada
     * duracion cazan a alguien que habla -silabas a dos y media por
     * segundo, que es lo normal- y cuantas se inventan una emisora donde
     * solo hay ruido. Las dos cosas, porque acortar a ciegas se paga en
     * la segunda.
     * -------------------------------------------------------------- */
    printf("\n  12. CUANTO HAY QUE MIRAR, CON EL SUELO YA APRENDIDO\n");
    {
        static const unsigned k_mira_ms[] = { 100u, 150u, 200u, 250u,
                                              400u, 600u, 1000u };
        unsigned d, v;
        unsigned peor_cazadas = 24u;
        unsigned cazadas = 0u, falsas = 0u;

        for (m = 0u; m < sizeof k_modo / sizeof k_modo[0]; m++) {
            printf("      %-4s", k_nombre[m]);
            for (d = 0u; d < sizeof k_mira_ms / sizeof k_mira_ms[0]; d++) {
                unsigned mira_bl = (k_mira_ms[d] * 375u) / 1000u;
                unsigned caza = 0u, falsa = 0u;
                float piso, p, vv;

                /* El suelo de esa memoria, aprendido con ella libre: es
                 * el valle de una visita con solo ruido. */
                (void)visita(k_modo[m], 0.0, 300.0, 11u, 0, 0.0,
                             ESPERA_BLOQUES, mira_bl, 0, &piso);

                for (v = 0u; v < 24u; v++) {
                    /* Cada visita cae en un sitio distinto de la frase:
                     * eso es lo que decide si se la pierde o no. */
                    double fs_ = (double)v / 24.0 * 0.4;

                    (void)visita(k_modo[m], 600.0, 300.0, 11u + v * 7u, 1,
                                 fs_, ESPERA_BLOQUES, mira_bl, &p, &vv);
                    if (p >= piso + SCAN_MARGEN_DB) { caza++; }

                    (void)visita(k_modo[m], 0.0, 300.0, 101u + v * 7u, 0,
                                 fs_, ESPERA_BLOQUES, mira_bl, &p, &vv);
                    if (p >= piso + SCAN_MARGEN_DB) { falsa++; }
                }
                printf("  %4u ms:%2u/24", k_mira_ms[d], caza);
                if (falsa) { printf("(%u falsas)", falsa); }
                /* El veredicto se deja para despues de la fila: si se
                 * suelta aqui dentro, parte la linea de numeros por la
                 * mitad y la tabla deja de leerse. */
                if (k_mira_ms[d] == SCAN_MIRA_MS) {
                    if (caza < peor_cazadas) { peor_cazadas = caza; }
                    cazadas = caza;
                    falsas  = falsa;
                }
            }
            printf("\n");
            snprintf(t, sizeof t,
                     "%s: a %u ms caza %u de 24 y se inventa %u",
                     k_nombre[m], SCAN_MIRA_MS, cazadas, falsas);
            pasa(t, cazadas >= 22u && falsas == 0u);
        }
        printf("      (a %u ms, lo peor que caza es %u de 24)\n",
               SCAN_MIRA_MS, peor_cazadas);
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

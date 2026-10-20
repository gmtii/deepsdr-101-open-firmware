/*
 * ¿QUE INDICE M1 ELIGE LA RADIO? - 14/10/2026
 *
 * 157 ráfagas de 178 dicen ser BPSK. Eso no es lo que hay en el aire:
 * es lo que elige el buscador de preámbulo. Y solo 14 de esas 157
 * pasan el CRC.
 *
 * El preámbulo acaba con M1: 127 símbolos que son UNA de las 8
 * rotaciones cíclicas de la misma secuencia base. La rotación que
 * acierta dice el giro (BPSK/4PSK/8PSK), el número de segmentos, el
 * ritmo del código y el desplazamiento del desentrelazador. Si se
 * elige mal la rotación, TODO lo que viene después está mal
 * configurado y el CRC no puede salir nunca. No es que se pierda un
 * bit: es que se decodifica otra trama distinta.
 *
 * hfdl_preamble_sync.c correlaba las 8 rotaciones y se quedaba con
 * LA PRIMERA que cruzara el umbral, no con la mejor. Y las correla en
 * orden 0,1,2,...,7, así que el índice 0 -BPSK- tiene derecho de
 * tanteo en cada símbolo. Peor: el correlador combina 8 bloques de 15
 * símbolos sumando VALORES ABSOLUTOS, y el comentario del propio
 * hfdl_framer.h ya avisa de que así "el ruido se sesga solo hacia
 * puntuaciones altas según encogen los bloques". Las 8 rotaciones
 * tienen entonces una probabilidad nada despreciable de cruzar 0,30
 * por ruido, y la que gana es la de índice más bajo, no la verdadera.
 *
 * Este banco transmite cada una de las 8 rotaciones y mira cuál se
 * elige. Tiene que FALLAR con el código de hoy: si sale verde a la
 * primera, el banco no vale.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "hfdl_preamble_seq.h"
#include "hfdl_preamble_sync.h"
#include "hfdl_ram.h"

static uint32_t s_sem = 1u;
static float ruido(void)
{
    /* Box-Muller con un generador propio: el banco tiene que ser
     * repetible entre máquinas, no puede depender del rand() de turno. */
    float u1, u2;
    s_sem = s_sem * 1664525u + 1013904223u;
    u1 = ((float)((s_sem >> 8) & 0xFFFFFFu) + 1.0f) / 16777217.0f;
    s_sem = s_sem * 1664525u + 1013904223u;
    u2 = (float)((s_sem >> 8) & 0xFFFFFFu) / 16777216.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318530718f * u2);
}

/*
 * Emite ruido, A, A, M1(giro) y devuelve el índice que la máquina de
 * estados ha enganchado, o -1 si no ha enganchado nada.
 */
/* Desplazamiento, en simbolos, entre donde engancho y donde debia. */
static int g_desp;

static int una_rafaga(uint32_t giro, float sigma)
{
    hfdl_preamble_sync_t p;
    float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    uint32_t i;
    uint32_t n = 0u;            /* simbolos metidos hasta ahora */
    int visto = -1;

    g_desp = 0;

    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(giro, m1);

    hfdl_preamble_sync_init(&p);

#define METE(v)                                                            \
    do {                                                                   \
        float _i = (v) + sigma * ruido();                                  \
        float _q = sigma * ruido();                                        \
        if (hfdl_preamble_sync_process_symbol(&p, _i, _q) == HFDL_PREAMBLE_LOCKED \
            && visto < 0) {                                                \
            visto = (int)hfdl_preamble_sync_get_m1_index(&p);              \
            /* El ultimo simbolo del M1 es el 300+127+127+127-1 = 680. */  \
            g_desp = (int)n - 680;                                         \
        }                                                                  \
        n++;                                                               \
    } while (0)

    /* Un rato de ruido delante: la radio no empieza a escuchar justo
     * en el primer símbolo de la ráfaga. */
    for (i = 0; i < 300u; i++) { METE(0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { METE(a[i]); }
    for (i = 0; i < HFDL_A_LEN; i++)  { METE(a[i]); }
    for (i = 0; i < HFDL_M1_LEN; i++) { METE(m1[i]); }
    /* Un poco de cola: el chequeo de M1 puede llegar con retraso. */
    for (i = 0; i < 40u; i++) { METE(0.0f); }
#undef METE

    return visto;
}

int main(void)
{
    static const float SIGMAS[] = { 0.0f, 0.5f, 0.9f, 1.3f, 1.8f };
    const uint32_t N = 40u;
    uint32_t s, g, t;
    uint32_t mal_total = 0u, bien_total = 0u, nada_total = 0u;
    uint32_t elegido_hist[HFDL_M_SHIFT_CNT];
    int fallos = 0;

    /* Las tablas de referencia viven en la RAM prestada de la cascada:
     * sin pedirla, el buscador se queda inerte a proposito. */
    hfdl_ram_coge();

    memset(elegido_hist, 0, sizeof(elegido_hist));

    printf("=== 1. ¿Se elige la rotación M1 que se ha transmitido? ===\n");
    printf("    (%u ráfagas por giro y por sigma)\n\n", N);

    for (s = 0; s < sizeof(SIGMAS) / sizeof(SIGMAS[0]); s++) {
        uint32_t bien_s = 0u, mal_s = 0u, nada_s = 0u;
        printf("  sigma %.2f\n", (double)SIGMAS[s]);
        printf("    giro  bien  mal  sin enganche   elegidos\n");
        for (g = 0; g < HFDL_M_SHIFT_CNT; g++) {
            uint32_t bien = 0u, mal = 0u, nada = 0u;
            uint32_t hist[HFDL_M_SHIFT_CNT];
            memset(hist, 0, sizeof(hist));
            s_sem = 1000u + 97u * s + 7u * g; /* semilla fija: repetible */
            for (t = 0; t < N; t++) {
                int v = una_rafaga(g, SIGMAS[s]);
                if (v < 0) {
                    nada++;
                } else if ((uint32_t)v == g) {
                    bien++;
                    hist[v]++;
                } else {
                    mal++;
                    hist[v]++;
                }
            }
            printf("    %4u  %4u  %3u  %12u   ", g, bien, mal, nada);
            for (t = 0; t < HFDL_M_SHIFT_CNT; t++) {
                printf("%u:%-3u", t, hist[t]);
                elegido_hist[t] += hist[t];
            }
            printf("\n");
            bien_s += bien; mal_s += mal; nada_s += nada;
        }
        printf("    --- sigma %.2f: bien %u, mal %u, sin enganche %u\n\n",
               (double)SIGMAS[s], bien_s, mal_s, nada_s);
        bien_total += bien_s; mal_total += mal_s; nada_total += nada_s;
    }

    printf("=== 2. ¿Hay sesgo hacia el índice bajo? ===\n");
    printf("    Se ha transmitido cada rotación el mismo número de veces,\n");
    printf("    así que un reparto honrado sería plano.\n    ");
    for (t = 0; t < HFDL_M_SHIFT_CNT; t++) {
        printf("%u:%-5u", t, elegido_hist[t]);
    }
    printf("\n\n");

    /*
     * ======================================================================
     * ¿CUANTAS DE LAS "157" SON RAFAGAS DE VERDAD?
     * ======================================================================
     *
     * La chapa de la radio pone "BPSK 14/157". Ese 157 no cuenta ráfagas
     * que haya en el aire: cuenta veces que el buscador de preámbulo dijo
     * "aquí hay una" y la cadena llegó hasta el CRC. Si el ruido engancha
     * solo, el denominador se infla y el 9% no es el 9% de nada.
     *
     * El embudo que la radio enseña -776>215>187- dice que de 776 A1 solo
     * 215 confirman A2. O sea que A1 engancha mucho con ruido, que es lo
     * que se espera de la puerta floja. La pregunta es cuántos llegan
     * hasta el final SIN que haya señal ninguna. Eso se mide aquí: ruido
     * y nada más que ruido, y se cuenta.
     */
    /*
     * ======================================================================
     * ¿POR QUE SE SIGUE ELIGIENDO MAL?
     * ======================================================================
     *
     * Decidir mejor no arreglo nada -la correlacion coherente de 127, que
     * distingue las ocho rotaciones muchisimo mejor que la de bloques, dio
     * 146 fallos contra 140-. Asi que no es un problema de parecido. La
     * otra explicacion posible es que la VENTANA no este donde tiene que
     * estar: corrida un simbolo, la rotacion buena correla tan mal como
     * las siete malas y da igual con que criterio se elija.
     *
     * Esto lo mide: el ultimo simbolo del M1 cae siempre en el mismo sitio
     * (el 680 de la ráfaga que monta este banco), asi que el
     * desplazamiento del enganche es un dato exacto, no una estimacion.
     */
    printf("=== 3. ¿Engancha donde debe? ===\n");
    {
        static const float SIG_D[] = { 0.5f, 0.9f, 1.3f };
        uint32_t sd, g2, t2;
        for (sd = 0; sd < 3u; sd++) {
            uint32_t bien_al_dia = 0u, bien_corrido = 0u;
            uint32_t mal_al_dia = 0u, mal_corrido = 0u;
            int dmin = 999, dmax = -999;
            for (g2 = 0; g2 < HFDL_M_SHIFT_CNT; g2++) {
                s_sem = 31000u + 97u * sd + 7u * g2;
                for (t2 = 0; t2 < 40u; t2++) {
                    int v = una_rafaga(g2, SIG_D[sd]);
                    if (v < 0) { continue; }
                    if (g_desp < dmin) { dmin = g_desp; }
                    if (g_desp > dmax) { dmax = g_desp; }
                    if ((uint32_t)v == g2) {
                        if (g_desp == 0) { bien_al_dia++; } else { bien_corrido++; }
                    } else {
                        if (g_desp == 0) { mal_al_dia++; } else { mal_corrido++; }
                    }
                }
            }
            printf("    sigma %.1f:  en su sitio -> %u bien / %u mal",
                   (double)SIG_D[sd], bien_al_dia, mal_al_dia);
            printf("   |   corrido -> %u bien / %u mal   (desp de %d a %d)\n",
                   bien_corrido, mal_corrido, dmin, dmax);
        }
    }
    printf("\n");

    printf("=== 3b. Enganches con RUIDO SOLO (ni un simbolo de señal) ===\n");
    {
        static const float SIG_R[] = { 0.3f, 0.7f, 1.0f };
        uint32_t r;
        for (r = 0; r < 3u; r++) {
            hfdl_preamble_sync_t p;
            uint32_t a1 = 0u, a2 = 0u, lock = 0u, i;
            hfdl_preamble_state_t prev;
            uint32_t elegido[HFDL_M_SHIFT_CNT];
            memset(elegido, 0, sizeof(elegido));
            s_sem = 4242u + r;
            hfdl_preamble_sync_init(&p);
            prev = HFDL_PREAMBLE_A1_SEARCH;
            /* 1800 simbolos por segundo: 180.000 son cien segundos de ruido. */
            for (i = 0; i < 180000u; i++) {
                hfdl_preamble_state_t st =
                    hfdl_preamble_sync_process_symbol(&p, SIG_R[r] * ruido(),
                                                      SIG_R[r] * ruido());
                if (st != prev) {
                    if (st == HFDL_PREAMBLE_A2_SEARCH) { a1++; }
                    else if (st == HFDL_PREAMBLE_M1_SEARCH) { a2++; }
                    else if (st == HFDL_PREAMBLE_LOCKED) {
                        int v = hfdl_preamble_sync_get_m1_index(&p);
                        lock++;
                        if (v >= 0) { elegido[v]++; }
                        hfdl_preamble_sync_reset(&p); /* como hace la radio al rematar */
                        st = HFDL_PREAMBLE_A1_SEARCH;
                    }
                }
                prev = st;
            }
            printf("    sigma %.1f, 100 s de ruido: embudo %u>%u>%u",
                   (double)SIG_R[r], a1, a2, lock);
            printf("   ->  %.1f enganches falsos por minuto\n",
                   (double)lock * 0.6);
            if (lock > 0u) {
                printf("        repartidos ");
                for (t = 0; t < HFDL_M_SHIFT_CNT; t++) { printf("%u:%-3u", t, elegido[t]); }
                printf("\n");
            }
        }
    }
    printf("\n");

    printf("=== 4. Veredicto ===\n");
    printf("    bien %u, mal %u, sin enganche %u\n",
           bien_total, mal_total, nada_total);

    /* Con ruido cero no hay excusa ninguna: tiene que acertar las 8. */
    {
        uint32_t g2;
        for (g2 = 0; g2 < HFDL_M_SHIFT_CNT; g2++) {
            int v;
            s_sem = 55u + g2;
            v = una_rafaga(g2, 0.0f);
            if (v != (int)g2) {
                printf("    FALLO: sin ruido, se elige %d habiendo transmitido %u\n", v, g2);
                fallos++;
            }
        }
    }

    /*
     * LO QUE ESTE BANCO EXIGE, Y LO QUE NO.
     *
     * Exige que CUANDO EL ENGANCHE CAE DONDE DEBE la rotacion elegida sea
     * la buena. Eso es lo unico que depende de hfdl_preamble_sync.c, y se
     * cumple: 130 de 130, 108 de 108 y 106 de 106 en el apartado 3.
     *
     * NO exige que no haya elecciones equivocadas en total, porque las que
     * quedan no son suyas: vienen de enganches corridos 300 simbolos, o
     * sea de la maquina A1->A2->M1 enganchando con el ruido de delante
     * ANTES de que empiece la señal. Eso no se arregla eligiendo mejor
     * entre ocho rotaciones -se probo, con una correlacion coherente que
     * distingue muchisimo mejor, y dio igual-: se arregla no abriendo una
     * ráfaga de dos segundos y medio detras de un enganche que no lleva
     * secuencia de entrenamiento. De eso se ocupa el portero de
     * hfdl_modo.c, y quien lo mide es sim/hfdl_extremo.c apartado 7,
     * porque hace falta la cadena entera y aqui solo esta el buscador.
     *
     * Un banco que exigiera aqui "cero equivocaciones" estaria pidiendole
     * a este modulo que arregle algo que pasa fuera de el, y la unica
     * forma de ponerlo verde seria falsear el umbral.
     */
    {
        uint32_t sd, g2, t2, al_dia_bien = 0u, al_dia_mal = 0u;
        static const float SIG_V[] = { 0.5f, 0.9f, 1.3f };
        for (sd = 0; sd < 3u; sd++) {
            for (g2 = 0; g2 < HFDL_M_SHIFT_CNT; g2++) {
                s_sem = 31000u + 97u * sd + 7u * g2;
                for (t2 = 0; t2 < 40u; t2++) {
                    int v = una_rafaga(g2, SIG_V[sd]);
                    if (v < 0 || g_desp != 0) { continue; }
                    if ((uint32_t)v == g2) { al_dia_bien++; } else { al_dia_mal++; }
                }
            }
        }
        printf("    enganches en su sitio: %u bien, %u mal\n", al_dia_bien, al_dia_mal);
        if (al_dia_mal != 0u) {
            printf("    FALLO: %u enganches caen en el simbolo bueno y aun asi\n", al_dia_mal);
            printf("           eligen la rotacion equivocada\n");
            fallos++;
        }
        if (al_dia_bien < 200u) {
            printf("    FALLO: solo %u enganches en su sitio, muy pocos para juzgar\n",
                   al_dia_bien);
            fallos++;
        }
    }

    if (fallos != 0) {
        printf("\n    *** %d FALLO(S) ***\n", fallos);
        return 1;
    }
    printf("\n    todo correcto\n");
    return 0;
}

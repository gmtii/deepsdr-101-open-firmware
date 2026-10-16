/*
 * LA BUSQUEDA AUTOMATICA DE VELOCIDAD Y ENTRELAZADO DEL 4285.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * La corazonada era buena. La velocidad y el entrelazado NO VAN EN LA
 * SEÑAL -el preambulo de 80 simbolos es identico a 75 y a 2400 bps- y
 * son once combinaciones que hay que acertar a mano con dos botones.
 * Diez de cada once veces se oye un 4285 perfectamente demodulado del
 * que no sale una letra, y desde fuera eso es indistinguible de un
 * demodulador roto.
 *
 * Este banco mide las tres cosas que tienen que ser verdad para que la
 * busqueda sirva de algo:
 *
 *   1. que ACIERTE. La grabacion es 300 bps con entrelazado largo, y es
 *      la unica de las once que saca "This is a Stanag4481 test
 *      message". No vale enganchar: tiene que salir la frase.
 *
 *   2. que acierte EN UN TIEMPO QUE UN HUMANO AGUANTE. Lo caro no es
 *      decidir, es LLENAR el desentrelazador antes de poder decidir:
 *      j*31 grupos, que con entrelazado largo son diez segundos. Si cada
 *      candidato pagara su propio relleno esto tardaria minutos. La
 *      lista esta ordenada para que las que comparten incremento vayan
 *      seguidas y compartan relleno; el techo de abajo es lo que vigila
 *      que ese orden siga siendo el que es.
 *
 *   3. que NO ENGANCHE CON RUIDO. Un Viterbi siempre devuelve bits con
 *      buena cara. Una busqueda que acepta el primer candidato que le
 *      pongan delante es peor que no tenerla: convierte "no sale nada"
 *      en "sale basura y parece texto".
 *
 * EL TECHO DE TIEMPO, Y DE DONDE SALE. Medido con el orden de hoy, la
 * frase aparece a los 21,6 s de audio. El techo esta en 30 s: deja
 * margen para que la señal tarde en empezar, y se pone rojo si alguien
 * reordena la lista de forma que un relleno largo se pague dos veces.
 */
#include "stanag_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-52s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* --- lector de wav, el mismo que los demas bancos del 4285 ------------- */
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out, uint16_t *can_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12];
    uint16_t canales = 1U, bits = 16U;
    uint32_t hz = 0UL;
    float *x = 0;

    if (!f) { printf("  no puedo abrir %s\n", ruta); return 0; }
    if (fread(cab, 1, 12, f) != 12U) { fclose(f); return 0; }
    for (;;) {
        uint8_t ch[8];
        uint32_t len;
        if (fread(ch, 1, 8, f) != 8U) { break; }
        len = le32(&ch[4]);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fm[16];
            uint32_t q = (len < 16U) ? len : 16U;
            if (fread(fm, 1, q, f) != q) { break; }
            canales = le16(&fm[2]); hz = le32(&fm[4]); bits = le16(&fm[14]);
            if (len > q) { fseek(f, (long)(len - q), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t ns = len / (uint32_t)(bits / 8U), i;
            int16_t *raw = (int16_t *)malloc(len);
            if (!raw) { break; }
            if (fread(raw, 1, len, f) != len) { free(raw); break; }
            x = (float *)malloc((size_t)ns * sizeof *x);
            for (i = 0U; i < ns; i++) { x[i] = (float)raw[i] / 32768.0f; }
            free(raw);
            *n_out = ns / canales; *hz_out = hz; *can_out = canales;
            fclose(f); return x;
        } else {
            fseek(f, (long)((len + 1U) & ~1U), SEEK_CUR);
        }
    }
    fclose(f);
    return x;
}

#define FS_RX     12000.0f
#define TECHO_S   30.0        /* ver la cabecera */
/*
 * EL MARGEN MINIMO. El ganador tiene que bajar de 4 % -que es el umbral
 * con el que la radio acepta- y el segundo tiene que estar al menos 8
 * puntos por encima. Medido: gana con 1 % y el segundo saca 13 %, o sea
 * doce puntos. Si alguien toca la ventana del Viterbi, el umbral o el
 * desentrelazador y el margen se encoge a la mitad, esto se pone rojo
 * ANTES de que la radio empiece a acertar por casualidad.
 */
#define AUTO_BIEN_BANCO  4U
#define MARGEN_MIN       8U

static float trabajo[STANAG_RX_FLOATS];
static char  todo[8192];

/* Una pasada con la busqueda encendida. Devuelve en que segundo de audio
 * aparecio la frase, o -1 si no aparecio. */
static double pasada(const float *y, uint32_t m, stanag_rx_est_t *fin, int traza)
{
    uint32_t i;
    double cuando = -1.0;
    uint8_t antes = 0U;

    stanag_rx_auto_pon(1U);
    stanag_rx_formato_pon(STANAG_RX_ASCII);
    if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) {
        printf("    no arranca: %s\n", stanag_rx_porque());
        return -1.0;
    }
    for (i = 0U; i + 64U <= m; i += 64U) {
        stanag_rx_est_t e;
        stanag_rx_mete(&y[i], 64U);
        (void)stanag_rx_paso();
        stanag_rx_estado(&e);
        if (traza && (e.busca_cual != antes)) {
            if (antes != 0U) {
                printf("      %5.1f s  prueba %2u/%u  %-4s %-5s\n",
                       (double)i / 12000.0, e.busca_cual, e.busca_de,
                       stanag_rx_velocidad_txt(stanag_rx_velocidad()),
                       stanag_rx_largo() ? "largo" : "corto");
            }
            antes = e.busca_cual;
        }
        if (cuando < 0.0) {
            uint32_t k;
            todo[0] = '\0';
            for (k = 0U; k < stanag_rx_nlineas(); k++) {
                strncat(todo, stanag_rx_linea((uint8_t)k), sizeof todo - strlen(todo) - 1U);
            }
            if (strstr(todo, "Stanag") || strstr(todo, "test message")) {
                cuando = (double)i / 12000.0;
            }
        }
    }
    todo[0] = '\0';
    for (i = 0U; i < stanag_rx_nlineas(); i++) {
        strncat(todo, stanag_rx_linea((uint8_t)i), sizeof todo - strlen(todo) - 1U);
    }
    stanag_rx_estado(fin);
    return cuando;
}

/* ruido blanco, para la tercera prueba */
static uint32_t sem = 12345UL;
static float ruido(void)
{
    sem = (sem * 1103515245UL) + 12345UL;
    return ((float)((sem >> 9) & 0xFFFFU) / 32768.0f) - 1.0f;
}

int main(int argc, char **argv)
{
    uint32_t n = 0UL, hz = 0UL, m, i;
    uint16_t canales = 1U;
    float *x, *y;
    double paso, cuando;
    stanag_rx_est_t e;
    char det[160];

    if (argc < 2) { printf("uso: %s fichero.wav\n", argv[0]); return 2; }
    printf("\nLA BUSQUEDA AUTOMATICA DE VELOCIDAD Y ENTRELAZADO\n");
    printf("=================================================\n\n");

    x = lee_wav(argv[1], &n, &hz, &canales);
    if (!x) { return 2; }
    m = (uint32_t)((double)n * (double)FS_RX / (double)hz);
    y = (float *)malloc((size_t)m * sizeof *y);
    paso = (double)hz / (double)FS_RX;
    for (i = 0U; i < m; i++) {
        double t = (double)i * paso;
        uint32_t k = (uint32_t)t;
        double fr = t - (double)k;
        if ((k + 1U) >= n) { k = n - 2U; fr = 1.0; }
        y[i] = (float)((1.0 - fr) * (double)x[k * canales] + fr * (double)x[(k + 1U) * canales]);
    }
    printf("  %u muestras a %u Hz -> %.1f s a 12 kHz\n", (unsigned)n, (unsigned)hz,
           (double)m / 12000.0);
    printf("  la grabacion es 300 bps con entrelazado largo, 1 de las 11\n\n");

    printf("  por donde va pasando:\n");
    cuando = pasada(y, m, &e, 1);
    printf("\n");

    snprintf(det, sizeof det, "acabo en %s / %s",
             stanag_rx_velocidad_txt(stanag_rx_velocidad()),
             stanag_rx_largo() ? "largo" : "corto");
    di("encuentra la combinacion el solo", e.busca_fijo
       && (stanag_rx_velocidad() == STANAG_RX_300) && stanag_rx_largo(), det);

    if (cuando >= 0.0) { snprintf(det, sizeof det, "la frase sale a los %.1f s", cuando); }
    else               { snprintf(det, sizeof det, "no sale la frase"); }
    di("y sale el texto, que es la unica prueba que vale",
       cuando >= 0.0, det);

    snprintf(det, sizeof det, "%.1f s de %.0f", (cuando >= 0.0) ? cuando : 999.0, TECHO_S);
    di("sin pasarse del techo de tiempo", (cuando >= 0.0) && (cuando <= TECHO_S), det);

    snprintf(det, sizeof det, "%u vuelta%s a la lista", e.busca_vueltas,
             (e.busca_vueltas == 1U) ? "" : "s");
    di("de una sola pasada por la lista", e.busca_vueltas == 0U, det);

    /* --- EL MARGEN, que es lo que dice si el acierto es casualidad ------ */
    printf("\n  el mejor ber de cada candidato en esa pasada:\n");
    {
        uint8_t k, n_c = stanag_rx_auto_cuantos();
        uint8_t gana = 100U, segundo = 100U;
        for (k = 0U; k < n_c; k++) {
            uint8_t b = stanag_rx_auto_ber(k);
            printf("      %-4s %-5s  %s%u%s\n",
                   stanag_rx_velocidad_txt(stanag_rx_auto_vel(k)),
                   stanag_rx_auto_largo(k) ? "largo" : "corto",
                   (b == 100U) ? "sin medir" : "", (b == 100U) ? 0U : b,
                   (b == 100U) ? "" : " %");
            if (b < gana) { segundo = gana; gana = b; }
            else if (b < segundo) { segundo = b; }
        }
        snprintf(det, sizeof det, "gana con %u %%, el siguiente %u %%", gana, segundo);
        di("y lo gana por goleada, no por los pelos",
           (gana < AUTO_BIEN_BANCO) && (segundo >= (gana + MARGEN_MIN)), det);
    }

    /* --- y que no se lo trague con ruido -------------------------------- */
    printf("\n  ahora lo mismo con ruido blanco, 30 s:\n");
    {
        uint32_t mr = 30U * 12000U;
        float *r = (float *)malloc((size_t)mr * sizeof *r);
        for (i = 0U; i < mr; i++) { r[i] = 0.3f * ruido(); }
        (void)pasada(r, mr, &e, 0);
        snprintf(det, sizeof det, "mejor ber %u%%, %u vuelta%s",
                 e.busca_mejor, e.busca_vueltas, (e.busca_vueltas == 1U) ? "" : "s");
        di("no engancha con ruido", !e.busca_fijo, det);
        free(r);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    free(x); free(y);
    return fallos ? 1 : 0;
}

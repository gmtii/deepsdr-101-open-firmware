/*
 * LA FASE, DESDE I/Q Y CON RUIDO: EL BANCO QUE FALTABA.
 * ====================================================
 *
 * Pedido por el usuario, 10/10/2026.
 *
 * POR QUE EXISTE. sim/als_real.c mide el decodificador contra la fase YA
 * EXTRAIDA de una grabacion. Y esa fase la extrajo tools/als_fase.py, que
 * hace esto:
 *
 *     bb, aa = sg.butter(4, 40 / (fs / 2))
 *     z = sg.filtfilt(bb, aa, z)
 *
 * O sea que al banco le estaban dando el trabajo YA HECHO: un filtro de
 * 40 Hz que la radio no daba. demod_am.c promediaba 64 muestras de I y de
 * Q a 96 kHz -un ancho de ruido equivalente de ~750 Hz- y llamaba a atan2
 * a pelo. Diecinueve veces mas potencia de ruido entrando en una medida
 * que luego se INTEGRA, o sea un paseo aleatorio.
 *
 * Un banco que mide una copia limpiada por un paso que el aparato no da no
 * mide nada, y este modo llevaba desde el 28/09 saliendo verde sin
 * decodificar una trama en antena.
 *
 * QUE MIDE ESTE. La cadena entera desde I y Q, que es por donde entra de
 * verdad: construye el par a partir de la fase conocida, le suma ruido
 * gaussiano, y se lo da a dcf77_fase_iq() -que es donde vive ahora el
 * filtro- y a dcf77_feed().
 *
 * Y comprueba las DOS cosas que hacen falta:
 *
 *   1. que con ruido siga decodificando, que es lo que antes no pasaba;
 *   2. que el numero de marca SEPARE "hay ALS162" de "solo hay ruido",
 *      que es la pregunta que no se podia contestar mirando la pantalla.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "dcf77.h"

static int fallos = 0;
static void ok(const char *q, int bien)
{
    printf("    %-54s %s\n", q, bien ? "OK" : "NO");
    if (!bien) { fallos++; }
}

/* Ruido repetible: dos tandas con la misma semilla dan lo mismo. */
static unsigned long sd;
static double gauss(void)
{
    double u1, u2;
    sd = sd * 1103515245UL + 12345UL; u1 = ((sd >> 16) & 0x7FFF) / 32768.0 + 1e-9;
    sd = sd * 1103515245UL + 12345UL; u2 = ((sd >> 16) & 0x7FFF) / 32768.0;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

static float x[400000];
static size_t n_x;

/*
 * `que` dice que hay debajo del ruido:
 *
 *   CON_ALS    la grabacion de verdad: portadora CON los elementos de fase.
 *   PORTADORA  portadora limpia y nada mas. ESTE es el caso que hay que
 *              distinguir, y no "no hay nada": cuando el usuario pregunta si
 *              esta recibiendo ALS162, su barra de Nivel esta al 75 %, o
 *              sea que portadora HAY. La pregunta es si ademas trae los
 *              elementos.
 *   NADA       ni portadora. Se mide pero no se exige: sin portadora el
 *              angulo es uniforme y el triangulo se pasea, asi que el
 *              numero sale ALTO. Es util saberlo -un numero alto con la
 *              barra de nivel vacia no significa señal- pero no es el caso
 *              que se vive delante de la radio.
 */
#define CON_ALS   2
#define PORTADORA 1
#define NADA      0

static void corre(double sigma, int que, dcf_info_t *out)
{
    size_t i;

    sd = 12345UL;
    dcf77_start(HORA_ALS162, 1500.0f);
    for (i = 0; i < n_x; i++) {
        double ph = (que == CON_ALS) ? (double)x[i] : 0.0;
        double a  = (que == NADA) ? 0.0 : 1.0;
        double I = a * cos(ph) + sigma * gauss();
        double Q = a * sin(ph) + sigma * gauss();
        dcf77_feed(dcf77_fase_iq((float)I, (float)Q));
    }
    dcf77_info(out);
}

int main(void)
{
    const char *ruta = "sim/muestras/als162_websdr_fase1500.f32";
    FILE *f = fopen(ruta, "rb");
    dcf_info_t a, b;
    double s;

    if (!f) {
        printf("  (no esta %s: me salto el banco)\n", ruta);
        return 0;
    }
    n_x = fread(x, sizeof(float), sizeof(x) / sizeof(x[0]), f);
    fclose(f);
    printf("ALS162 desde I/Q: %zu muestras de fase a 1500/s, con ruido inyectado\n\n", n_x);

    printf("  1. SIGUE DECODIFICANDO CON RUIDO\n");
    corre(0.0, CON_ALS, &a);
    printf("    sin ruido              marca %.2f  tramas buenas %u\n",
           (double)a.contraste, a.tramas_ok);
    ok("sin ruido decodifica", a.tramas_ok >= 2U);
    corre(0.4, CON_ALS, &b);
    printf("    sigma 0,40             marca %.2f  tramas buenas %u\n",
           (double)b.contraste, b.tramas_ok);
    ok("con sigma 0,40 sigue decodificando", b.tramas_ok >= 2U);

    printf("\n  2. EL NUMERO DE MARCA NO SE DESBOCA\n");
    printf("     (sin el filtro llegaba a 12 con sigma 0,8 y a 30 en antena)\n");
    for (s = 0.0; s < 1.7; s += 0.8) {
        corre(s, CON_ALS, &a);
        printf("    sigma %.2f              marca %.2f\n", s, (double)a.contraste);
        ok("se queda por debajo de 2", a.contraste < 2.0f);
    }

    printf("\n  3. Y SEPARA 'HAY ALS162' DE 'PORTADORA Y NADA MAS'\n");
    printf("     (la pregunta que no se podia contestar mirando la pantalla)\n");
    for (s = 0.4; s < 1.7; s += 0.4) {
        double con, pel;
        corre(s, CON_ALS,   &a); con = (double)a.contraste;
        corre(s, PORTADORA, &b); pel = (double)b.contraste;
        printf("    sigma %.2f   con ALS162 %.2f   portadora sola %.2f\n", s, con, pel);
        ok("con los elementos el numero es mayor", con > pel + 0.15);
    }

    printf("\n  4. Y SIN PORTADORA EL NUMERO SE VA ARRIBA, QUE TAMBIEN SE DICE\n");
    printf("     (no se exige: se deja medido para que nadie lea un numero\n");
    printf("      alto con la barra de nivel vacia y crea que hay señal)\n");
    corre(0.8, NADA, &a);
    printf("    sin portadora, sigma 0,80   marca %.2f\n", (double)a.contraste);

    printf("\n  %s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

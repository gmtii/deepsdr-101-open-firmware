/*
 * BANCO DE WWV (User/hora_wwv.c). 23/09/2026.
 *
 * QUE GENERA. La envolvente de una portadora AM de WWV tal y como la vera el
 * detector de la radio, con TODO lo que de verdad lleva encima y que puede
 * estorbar:
 *
 *   - La subportadora de 100 Hz que lleva la hora, al 18% de modulacion en
 *     el tramo fuerte y al 3% en el resto.
 *   - El tono de 500 Hz del audio, al 50%. Es dieciseis veces mas fuerte que
 *     lo que hay que medir, y esta ahi todo el rato: si el filtro de 100 Hz
 *     no lo rechaza, no se decodifica nada.
 *   - El tic de segundo: 5 ms de 1000 Hz al 100% al principio de cada
 *     segundo, justo en los 30 ms que preceden al flanco que hay que medir.
 *   - La voz del anuncio horario en los ultimos 7,5 segundos del minuto,
 *     como ruido de banda 80..300 Hz al 50%. Esa banda SI pisa los 100 Hz, y
 *     cae encima de los segundos 53 a 59, o sea encima de la marca P0 que
 *     cierra la trama.
 *   - Ruido blanco encima de todo.
 *
 * QUE COMPRUEBA:
 *   1. Que con señal limpia da la hora EXACTA, ya convertida a hora de pared
 *      española, y que respeta el convenio IRIG (la trama describe el minuto
 *      que empieza, no el siguiente): un error aqui son 60 segundos de
 *      desfase y no se nota mirando la pantalla.
 *   2. Hasta cuanto ruido aguanta.
 *   3. Que con SOLO ruido no se inventa una hora nunca. Esto es lo unico que
 *      de verdad importa: WWV no lleva ninguna paridad.
 *   4. Que una trama con un bit cambiado se rechaza.
 *   5. El barrido de las constantes del filtro y del umbral.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "dcf77.h"
#include "hora_wwv.h"
#include "hora_comun.h"   /* hora_huso_pon(): este banco pone el huso a mano */

#ifndef FS
#define FS 1500.0f
#endif

/* Las mismas que usa el modulo. Se repiten aqui SOLO para imprimirlas en la
 * cabecera del banco, y por eso se definen con los mismos -D que se le pasan
 * a el en el barrido: asi lo que se imprime es lo que se esta midiendo. */
#ifndef FILTRO_HZ
#define FILTRO_HZ 8.0f
#endif
#ifndef UMBRAL_BAJA
#define UMBRAL_BAJA 0.45f
#endif
#ifndef UMBRAL_SUBE
#define UMBRAL_SUBE 0.55f
#endif

static unsigned long s_rnd = 12345UL;
static float ruido(void)   /* uniforme en [-1,1), suficiente y reproducible */
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return ((float)((s_rnd >> 8) & 0xFFFFUL) / 32768.0f) - 1.0f;
}

/* ----------------------------------------------------------------------- */
/* La trama: 60 simbolos. 0 y 1 son bits; 2 es marca; 3 es "sin
 * subportadora" (solo el segundo 00).                                     */
static void trama_wwv(int minuto, int hora, int doy, int anno,
                      int dut1_signo, int dut1_dec, int dst1, int dst2, int lsw,
                      unsigned char *s)
{
    static const int mar[6] = { 9, 19, 29, 39, 49, 59 };
    int k;
    for (k = 0; k < 60; k++) { s[k] = 0; }
    s[0] = 3;
    for (k = 0; k < 6; k++) { s[mar[k]] = 2; }

    s[2] = (unsigned char)dst1;
    s[3] = (unsigned char)lsw;
    /* unidades del año */
    s[4] = (unsigned char)((anno % 10) & 1);
    s[5] = (unsigned char)(((anno % 10) >> 1) & 1);
    s[6] = (unsigned char)(((anno % 10) >> 2) & 1);
    s[7] = (unsigned char)(((anno % 10) >> 3) & 1);
    /* minutos */
    s[10] = (unsigned char)((minuto % 10) & 1);
    s[11] = (unsigned char)(((minuto % 10) >> 1) & 1);
    s[12] = (unsigned char)(((minuto % 10) >> 2) & 1);
    s[13] = (unsigned char)(((minuto % 10) >> 3) & 1);
    s[15] = (unsigned char)((minuto / 10) & 1);
    s[16] = (unsigned char)(((minuto / 10) >> 1) & 1);
    s[17] = (unsigned char)(((minuto / 10) >> 2) & 1);
    /* horas */
    s[20] = (unsigned char)((hora % 10) & 1);
    s[21] = (unsigned char)(((hora % 10) >> 1) & 1);
    s[22] = (unsigned char)(((hora % 10) >> 2) & 1);
    s[23] = (unsigned char)(((hora % 10) >> 3) & 1);
    s[25] = (unsigned char)((hora / 10) & 1);
    s[26] = (unsigned char)(((hora / 10) >> 1) & 1);
    /* dia del año */
    s[30] = (unsigned char)((doy % 10) & 1);
    s[31] = (unsigned char)(((doy % 10) >> 1) & 1);
    s[32] = (unsigned char)(((doy % 10) >> 2) & 1);
    s[33] = (unsigned char)(((doy % 10) >> 3) & 1);
    s[35] = (unsigned char)(((doy / 10) % 10) & 1);
    s[36] = (unsigned char)((((doy / 10) % 10) >> 1) & 1);
    s[37] = (unsigned char)((((doy / 10) % 10) >> 2) & 1);
    s[38] = (unsigned char)((((doy / 10) % 10) >> 3) & 1);
    s[40] = (unsigned char)((doy / 100) & 1);
    s[41] = (unsigned char)(((doy / 100) >> 1) & 1);
    /* DUT1 y decenas del año */
    s[50] = (unsigned char)dut1_signo;
    s[51] = (unsigned char)((anno / 10) & 1);
    s[52] = (unsigned char)(((anno / 10) >> 1) & 1);
    s[53] = (unsigned char)(((anno / 10) >> 2) & 1);
    s[54] = (unsigned char)(((anno / 10) >> 3) & 1);
    s[55] = (unsigned char)dst2;
    s[56] = (unsigned char)(dut1_dec & 1);
    s[57] = (unsigned char)((dut1_dec >> 1) & 1);
    s[58] = (unsigned char)((dut1_dec >> 2) & 1);
}

/* Emite un segundo entero de envolvente. sigma es el ruido, en fraccion de
 * la portadora. voz != 0 mete el anuncio hablado. */
static void emite_segundo(unsigned char simbolo, float sigma, int voz,
                          int con_tono_500)
{
    int n, N = (int)FS;
    static float t = 0.0f;       /* fase continua entre segundos */
    static float vz = 0.0f;      /* la "voz": ruido pasado por un polo */
    float alto_ms = (simbolo == 0) ? 170.0f
                  : (simbolo == 1) ? 470.0f
                  : (simbolo == 2) ? 770.0f : -1.0f;

    for (n = 0; n < N; n++) {
        float ms = (float)n * 1000.0f / FS;
        float m100 = 0.03f;
        float env;

        if (alto_ms > 0.0f && ms >= 30.0f && ms < (30.0f + alto_ms)) {
            m100 = 0.18f;
        }
        if (simbolo == 3) { m100 = 0.0f; }

        env = 1.0f + m100 * sinf(6.2831853f * 100.0f * t);
        if (con_tono_500) { env += 0.50f * sinf(6.2831853f * 500.0f * t); }
        if (ms < 5.0f)    { env += 1.00f * sinf(6.2831853f * 1000.0f * t); }
        if (voz) {
            /*
             * La voz del anuncio horario, modelada como voz masculina de
             * verdad y no como ruido: un fundamental de 120 Hz con sus
             * cuatro primeros armonicos y una envolvente de silabas a 4 Hz,
             * al 50% de modulacion. Importa que sea asi y no ruido blanco
             * porque lo que decide si estorba es cuanta energia cae en los
             * 100 Hz exactos, y un fundamental de 120 Hz cae a 20 Hz de la
             * subportadora - o sea, casi encima-. Es el peor caso realista.
             */
            float e = 0.5f + 0.5f * sinf(6.2831853f * 4.0f * t);
            vz = sinf(6.2831853f * 120.0f * t)
               + 0.6f * sinf(6.2831853f * 240.0f * t)
               + 0.4f * sinf(6.2831853f * 360.0f * t)
               + 0.3f * sinf(6.2831853f * 480.0f * t);
            env += 0.50f * e * vz * 0.45f;   /* 0,45 normaliza el pico a ~1 */
        }
        env += sigma * ruido();

        wwv_feed(env);
        t += 1.0f / FS;
        if (t > 100.0f) { t -= 100.0f; }   /* 100 s: entero para 100/500/1000 Hz */
    }
}

/* Emite minutos enteros a partir de una hora UTC. Devuelve el estado final. */
typedef struct { int ok, malas; dcf_estado_t estado; int h, m, d, mes, a; } res_t;

static res_t corre(int h0, int m0, int doy, int anno, int minutos,
                   float sigma, int con_voz, int con_tono, int romper_bit)
{
    unsigned char s[60];
    int mi, k;
    res_t r;
    dcf_info_t inf;

    for (mi = 0; mi < minutos; mi++) {
        int m = (m0 + mi) % 60;
        int h = (h0 + (m0 + mi) / 60) % 24;
        trama_wwv(m, h, doy, anno, 1, 3, 0, 0, 0, s);
        if (romper_bit >= 0 && mi == 1) { s[romper_bit] ^= 1; }
        for (k = 0; k < 60; k++) {
            int voz = (con_voz && k >= 53);
            emite_segundo(s[k], sigma, voz, con_tono);
        }
    }
    wwv_info(&inf);
    r.ok = inf.tramas_ok; r.malas = inf.tramas_malas; r.estado = inf.estado;
    r.h = inf.hora; r.m = inf.minuto; r.d = inf.dia; r.mes = inf.mes; r.a = inf.anno;
    return r;
}

int main(void)
{
    int fallos = 0;
    res_t r;

    printf("WWV - subportadora de 100 Hz, %d Hz de muestreo\n", (int)FS);
    printf("  filtro %.0f Hz, umbrales %.2f/%.2f\n",
           (double)FILTRO_HZ, (double)UMBRAL_BAJA, (double)UMBRAL_SUBE);

    /* --- 1. Señal limpia: la hora tiene que salir EXACTA ---------------- */
    /*
     * *** ESTE BANCO TAMBIEN MEDIA ESPAÑA - rehecho el 10/10/2026. ***
     *
     * Decia: "21:30 UTC en septiembre = verano en España = 23:30 de pared",
     * y comprobaba las 23. O sea que daba por bueno que una emisora de
     * Colorado diera hora de Madrid. Al quitar España del firmware salio en
     * rojo, que es exactamente lo que tenia que pasar.
     *
     * Ahora el huso se pone A MANO antes de medir y se prueban DOS, porque
     * con uno solo un "+2" cableado volveria a colarse.
     *
     * 23/09/2026 es el dia 266 del año. La trama del minuto 30 se cierra al
     * empezar el 31, asi que lo que vale es 21:31 UTC.
     */
    hora_huso_pon(120);                  /* Madrid en verano */
    s_rnd = 1UL;
    wwv_start(HORA_WWV10, FS);
    r = corre(21, 30, 266, 26, 4, 0.0f, 1, 1, -1);
    printf("  limpia (con tono de 500 Hz, tics y voz):\n");
    printf("    tramas buenas %d, malas %d, estado %d -> %02d:%02d del %02d/%02d/%02d\n",
           r.ok, r.malas, (int)r.estado, r.h, r.m, r.d, r.mes, r.a);
    if (r.estado != DCF_LISTO) { printf("    FALLO: no llego a LISTO\n"); fallos++; }
    if (r.h != 23 || r.d != 23 || r.mes != 9 || r.a != 26) {
        printf("    FALLO: con el huso en +2:00 esperaba las 23:3x del 23/09/26\n");
        fallos++;
    }

    /* EL MISMO MINUTO CON OTRO HUSO, que es lo que de verdad se comprueba:
     * que suma EL HUSO y no un numero fijo. Nepal esta a +5:45, asi que
     * ademas mueve los minutos -y eso un "+N horas" no puede imitarlo-. */
    hora_huso_pon(345);
    s_rnd = 1UL;
    wwv_start(HORA_WWV10, FS);
    r = corre(21, 30, 266, 26, 4, 0.0f, 1, 1, -1);
    printf("    con el huso en +5:45 (Nepal) -> %02d:%02d del %02d/%02d/%02d\n",
           r.h, r.m, r.d, r.mes, r.a);
    /* Con cuatro minutos de señal la ultima trama buena es la del 21:33 UTC
     * (es la que sale arriba como 23:33 con el huso en +2:00), y 21:33 mas
     * 5:45 son las 03:18 DEL DIA SIGUIENTE. Que el dia avance es la mitad de
     * lo que se comprueba aqui. */
    if (r.h != 3 || r.m != 18 || r.d != 24) {
        printf("    FALLO: esperaba las 03:18 del 24/09/26\n"); fallos++;
    }
    hora_huso_pon(120);

    /* --- 2. El convenio IRIG: +1 minuto -------------------------------- */
    /* Si la trama del minuto 30 se cerrara sin sumar uno, aqui saldria 30.
     * Se comprueba explicitamente porque es el fallo que no se ve. */
    s_rnd = 1UL;
    wwv_start(HORA_WWV10, FS);
    r = corre(21, 30, 266, 26, 3, 0.0f, 0, 1, -1);
    printf("  convenio IRIG: tras las tramas de los minutos 30 y 31 -> minuto %02d\n", r.m);
    if (r.m != 32) {
        printf("    FALLO: esperaba 32 (la trama del minuto 31 vale al empezar el 32)\n");
        fallos++;
    }

    /* --- 3. Barrido de ruido ------------------------------------------- */
    printf("  cuanto ruido aguanta (6 minutos cada uno, con tono y voz):\n");
    {
        float sig[] = { 0.02f, 0.05f, 0.10f, 0.20f, 0.35f, 0.50f };
        unsigned k;
        for (k = 0; k < sizeof(sig) / sizeof(sig[0]); k++) {
            s_rnd = 7UL;
            wwv_start(HORA_WWV10, FS);
            r = corre(21, 30, 266, 26, 6, sig[k], 1, 1, -1);
            printf("    ruido %4.0f%% de la portadora   %s   (buenas %d, malas %d)\n",
                   (double)(sig[k] * 100.0f),
                   (r.estado == DCF_LISTO) ? "sincroniza    " : "no sincroniza ",
                   r.ok, r.malas);
        }
    }

    /* --- 4. Solo ruido: NUNCA una hora -------------------------------- */
    printf("  solo ruido, 15 minutos (lo unico que no puede fallar):\n");
    {
        float sig[] = { 0.05f, 0.20f, 1.00f };
        unsigned k;
        for (k = 0; k < sizeof(sig) / sizeof(sig[0]); k++) {
            int mi, n;
            dcf_info_t inf;
            s_rnd = 99UL + k;
            wwv_start(HORA_WWV10, FS);
            for (mi = 0; mi < 15 * 60; mi++) {
                for (n = 0; n < (int)FS; n++) { wwv_feed(sig[k] * ruido()); }
            }
            wwv_info(&inf);
            printf("    sigma %4.2f -> %s (buenas %u, malas %u, contraste %.2f, estado %d)\n",
                   (double)sig[k],
                   (inf.estado == DCF_LISTO) ? "SINCRONIZA - FALLO GRAVE" : "no sincroniza",
                   inf.tramas_ok, inf.tramas_malas, (double)inf.contraste, (int)inf.estado);
            if (inf.estado == DCF_LISTO || inf.tramas_ok > 0U) { fallos++; }
            if (inf.contraste >= 0.25f) {
                printf("      FALLO: contraste %.2f con ruido puro (el limite es 0,25)\n",
                       (double)inf.contraste);
                fallos++;
            }
            /* La pantalla no puede decir "leyendo la trama" mirando ruido. */
            if (inf.estado != DCF_BUSCANDO && inf.estado != DCF_PERDIDO) {
                printf("      FALLO: estado %d con ruido puro\n", (int)inf.estado);
                fallos++;
            }
        }
    }

    /* --- 5. Un bit cambiado: se rechaza ------------------------------- */
    printf("  una trama con un bit cambiado:\n");
    {
        int bits[] = { 1, 14, 24, 34, 42, 48, 9, 29, 59, 12, 22, 31 };
        unsigned k;
        for (k = 0; k < sizeof(bits) / sizeof(bits[0]); k++) {
            s_rnd = 5UL;
            wwv_start(HORA_WWV10, FS);
            r = corre(21, 30, 266, 26, 3, 0.0f, 0, 1, bits[k]);
            printf("    bit %2d cambiado -> %s (buenas %d, malas %d)\n", bits[k],
                   (r.estado == DCF_LISTO) ? "ACEPTADA - FALLO" : "rechazada",
                   r.ok, r.malas);
            if (r.estado == DCF_LISTO) { fallos++; }
        }
    }

    if (fallos == 0) { printf("  TODO CORRECTO (0 fallos)\n"); }
    else             { printf("  %d FALLOS\n", fallos); }
    return fallos ? 1 : 0;
}

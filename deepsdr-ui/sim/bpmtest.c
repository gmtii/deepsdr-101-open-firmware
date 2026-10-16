/*
 * BANCO DE BPM (la otra mitad de User/hora_wwv.c). 23/09/2026.
 *
 * POR QUE VA APARTE DEL DE WWV. Comparten el frente entero -mezclador,
 * filtro, comparador y medida de anchos- pero son dos formatos distintos con
 * dos juegos de trampas distintos: los anchos son 200/500/800 en vez de
 * 170/470/770, el reparto de los bits no se parece, y BPM da el dia del mes
 * mientras que WWV da el dia del año. Que los dos bancos tengan que pasar es
 * lo que impide que un arreglo en el motor comun arregle uno y rompa el otro.
 *
 * Y HAY UNA COSA QUE SOLO SE PUEDE COMPROBAR AQUI: la subportadora. La
 * memoria tecnica de BPM dice 125 Hz y hay grabaciones recientes en 100, asi
 * que el decodificador busca las dos a la vez y usa la que encuentre. Este
 * banco emite la señal EN LAS DOS, por separado, y exige que sincronice
 * igual en los dos casos. Si algun dia se resuelve cual es, esto seguira
 * valiendo.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "dcf77.h"
#include "hora_wwv.h"
#include "hora_comun.h"

#ifndef FS
#define FS 1500.0f
#endif

static unsigned long s_rnd = 1UL;
static float ruido(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return ((float)((s_rnd >> 8) & 0xFFFFUL) / 32768.0f) - 1.0f;
}

/* ----------------------------------------------------------------------- */
/* La trama de BPM: 60 simbolos. 0 y 1 son bits, 2 es marca, 3 es "sin
 * subportadora" (solo el segundo 00).                                     */
static void trama_bpm(int minuto, int hora, int dia, int mes, int anno,
                      int dut_signo, int dut_dec, int leap, unsigned char *s)
{
    static const int mar[6] = { 9, 19, 29, 39, 49, 59 };
    static const int min_s[7] = { 1,2,3,4,6,7,8 },      min_p[7] = { 1,2,4,8,10,20,40 };
    static const int hor_s[6] = { 10,11,12,13,15,16 },  hor_p[6] = { 1,2,4,8,10,20 };
    static const int dia_s[6] = { 20,21,22,23,25,26 },  dia_p[6] = { 1,2,4,8,10,20 };
    static const int mes_s[5] = { 30,31,32,33,35 },     mes_p[5] = { 1,2,4,8,10 };
    static const int ann_s[8] = { 40,41,42,43,45,46,47,48 }, ann_p[8] = { 1,2,4,8,10,20,40,80 };
    int k;

    for (k = 0; k < 60; k++) { s[k] = 0; }
    s[0] = 3;
    for (k = 0; k < 6; k++) { s[mar[k]] = 2; }

    /* BCD: primero las unidades con sus cuatro pesos, luego las decenas. Se
     * reparte cifra a cifra y no restando pesos a lo bruto, porque en BCD
     * "18" no es 16+2 sino un 1 y un 8. */
#define PON(S,P,N,VAL) do {                                            \
        int _u = (VAL) % 10, _d = (VAL) / 10, _i;                      \
        for (_i = 0; _i < 4 && _i < (N); _i++) {                       \
            if (_u & (1 << _i)) { s[(S)[_i]] = 1; }                    \
        }                                                              \
        for (_i = 4; _i < (N); _i++) {                                 \
            if (_d & (1 << (_i - 4))) { s[(S)[_i]] = 1; }              \
        }                                                              \
        (void)(P);                                                     \
    } while (0)
    PON(min_s, min_p, 7, minuto);
    PON(hor_s, hor_p, 6, hora);
    PON(dia_s, dia_p, 6, dia);
    PON(mes_s, mes_p, 5, mes);
    PON(ann_s, ann_p, 8, anno);
#undef PON

    s[50] = (unsigned char)dut_signo;
    s[51] = (unsigned char)(dut_dec & 1);
    s[52] = (unsigned char)((dut_dec >> 1) & 1);
    s[53] = (unsigned char)((dut_dec >> 2) & 1);
    s[54] = (unsigned char)((dut_dec >> 3) & 1);
    s[57] = (unsigned char)leap;
}

static float s_sub_hz = 125.0f;   /* la que emite el banco */

/* Un segundo de envolvente. Lleva encima lo mismo que la emisora de verdad:
 * la subportadora al 18% / 3%, el tono de 1 kHz de los tics de segundo (que
 * en BPM son de 10 ms y de 300 ms en el minuto) y ruido. */
static void emite_segundo(unsigned char simbolo, float sigma, int tic_largo)
{
    int n, N = (int)FS;
    static float t = 0.0f;
    float alto_ms = (simbolo == 0) ? 200.0f
                  : (simbolo == 1) ? 500.0f
                  : (simbolo == 2) ? 800.0f : -1.0f;

    for (n = 0; n < N; n++) {
        float ms = (float)n * 1000.0f / FS;
        float msub = 0.03f;
        float env;

        /* El pulso de BPM empieza EN el segundo, no a los 30 ms como el de
         * WWV. Da igual para decodificar -se mide de subida a bajada- pero
         * se emite como es. */
        if (alto_ms > 0.0f && ms < alto_ms) { msub = 0.18f; }
        if (simbolo == 3) { msub = 0.0f; }

        env = 1.0f + msub * sinf(6.2831853f * s_sub_hz * t);
        if (ms < (tic_largo ? 300.0f : 10.0f)) {
            env += 0.80f * sinf(6.2831853f * 1000.0f * t);
        }
        env += sigma * ruido();

        wwv_feed(env);
        t += 1.0f / FS;
        if (t > 40.0f) { t -= 40.0f; }   /* entero para 100, 125 y 1000 Hz */
    }
}

typedef struct { int ok, malas, h, m, d, mes, a; dcf_estado_t estado; float contraste; } res_t;

static res_t corre(int h0, int m0, int dia, int mes, int anno,
                   int minutos, float sigma, int romper)
{
    unsigned char s[60];
    int mi, k;
    res_t r;
    dcf_info_t inf;

    for (mi = 0; mi < minutos; mi++) {
        /* La fecha tiene que avanzar CON la hora. La primera version de este
         * banco dejaba el dia fijo mientras los minutos daban la vuelta, o
         * sea que emitia "00:01 del 28 de febrero" justo despues de "23:59
         * del 28 de febrero". El decodificador hacia bien en no fiarse, y el
         * banco suspendia por culpa del banco. */
        int m = (m0 + mi) % 60;
        int h = h0 + (m0 + mi) / 60;
        uint8_t hh = (uint8_t)(h % 24), dd = (uint8_t)dia;
        uint8_t mm = (uint8_t)mes, aa = (uint8_t)anno, dsx = 0U;
        int dias = h / 24;
        while (dias-- > 0) { hora_suma_horas(24, &hh, &dd, &mm, &aa, &dsx); }
        trama_bpm(m, hh, dd, mm, aa, 1, 3, 0, s);
        if (romper >= 0) { s[romper] ^= 1; }
        for (k = 0; k < 60; k++) { emite_segundo(s[k], sigma, k == 0); }
    }
    wwv_info(&inf);
    r.ok = inf.tramas_ok; r.malas = inf.tramas_malas; r.estado = inf.estado;
    r.h = inf.hora; r.m = inf.minuto; r.d = inf.dia; r.mes = inf.mes; r.a = inf.anno;
    r.contraste = inf.contraste;
    return r;
}

int main(void)
{
    int fallos = 0;
    res_t r;

    printf("BPM - subportadora dentro del audio, %d Hz de muestreo\n", (int)FS);

    /*
     * *** EL HUSO SE PONE A MANO - 10/10/2026. ***
     * Este banco decia "septiembre es verano en España" y comprobaba las 00.
     * Lo era, pero solo si la radio estaba en España: la emisora es china y
     * da UTC. Desde que España salio del firmware (ver hora_comun.h) el huso
     * es un numero, y aqui se fija para que el banco mida la conversion y no
     * una regla de calendario que ya no existe.
     */
    hora_huso_pon(120);

    /* --- 1. Las DOS subportadoras ------------------------------------- */
    /* 22:30 UTC del 23/09/2026, con el huso en +2:00 -> 00:3x del 24. La
     * trama del minuto 30 vale al empezar el 31 (convenio IRIG). */
    {
        float subs[2] = { 125.0f, 100.0f };
        int k;
        for (k = 0; k < 2; k++) {
            s_sub_hz = subs[k];
            s_rnd = 1UL;
            wwv_start(HORA_BPM, FS);
            r = corre(22, 30, 23, 9, 26, 4, 0.02f, -1);
            printf("  emitiendo en %3.0f Hz: buenas %d, malas %d, estado %d"
                   " -> %02d:%02d del %02d/%02d/%02d\n",
                   (double)subs[k], r.ok, r.malas, (int)r.estado,
                   r.h, r.m, r.d, r.mes, r.a);
            if (r.estado != DCF_LISTO) {
                printf("    FALLO: no llego a LISTO con la subportadora en %.0f Hz\n",
                       (double)subs[k]);
                fallos++;
            }
            if (r.h != 0 || r.d != 24 || r.mes != 9 || r.a != 26) {
                printf("    FALLO: esperaba las 00:3x del 24/09/26\n");
                fallos++;
            }
        }
        s_sub_hz = 125.0f;
    }

    /* --- 2. Fechas con acarreo --------------------------------------- */
    printf("  fechas que cruzan la medianoche y el fin de mes:\n");
    {
        struct { int h, m, d, mes, a, eh, ed, emes; const char *q; } c[] = {
            { 21, 58, 23,  9, 26,  0, 24,  9, "fin de dia en verano (+2)" },
            { 22, 58, 31, 12, 26,  0,  1,  1, "fin de año" },
            { 23, 58, 28,  2, 24,  0, 29,  2, "29 de febrero" },
            { 10, 30, 15,  1, 26, 11, 15,  1, "invierno (+1), sin acarreo" }
        };
        unsigned k;
        for (k = 0; k < sizeof(c) / sizeof(c[0]); k++) {
            s_rnd = 3UL;
            wwv_start(HORA_BPM, FS);
            r = corre(c[k].h, c[k].m, c[k].d, c[k].mes, c[k].a, 4, 0.0f, -1);
            printf("    %-28s -> %02d:%02d del %02d/%02d/%02d  %s\n", c[k].q,
                   r.h, r.m, r.d, r.mes, r.a,
                   (r.estado == DCF_LISTO) ? "" : "(no sincronizo)");
            if (r.estado != DCF_LISTO) { fallos++; }
        }
    }

    /* --- 3. Ruido ----------------------------------------------------- */
    printf("  cuanto ruido aguanta (6 minutos cada uno):\n");
    {
        float sig[] = { 0.02f, 0.05f, 0.10f, 0.20f, 0.35f, 0.50f };
        unsigned k;
        for (k = 0; k < sizeof(sig) / sizeof(sig[0]); k++) {
            s_rnd = 7UL;
            wwv_start(HORA_BPM, FS);
            r = corre(22, 30, 23, 9, 26, 6, sig[k], -1);
            printf("    ruido %4.0f%% de la portadora   %s   (buenas %d, malas %d)\n",
                   (double)(sig[k] * 100.0f),
                   (r.estado == DCF_LISTO) ? "sincroniza    " : "no sincroniza ",
                   r.ok, r.malas);
        }
    }

    /* --- 4. Solo ruido ------------------------------------------------ */
    printf("  solo ruido, 15 minutos (lo unico que no puede fallar):\n");
    {
        float sig[] = { 0.05f, 0.20f, 1.00f };
        unsigned k;
        for (k = 0; k < sizeof(sig) / sizeof(sig[0]); k++) {
            int mi, n;
            dcf_info_t inf;
            s_rnd = 99UL + k;
            wwv_start(HORA_BPM, FS);
            for (mi = 0; mi < 15 * 60; mi++) {
                for (n = 0; n < (int)FS; n++) { wwv_feed(sig[k] * ruido()); }
            }
            wwv_info(&inf);
            printf("    sigma %4.2f -> %s (buenas %u, malas %u, contraste %.2f)\n",
                   (double)sig[k],
                   (inf.estado == DCF_LISTO) ? "SINCRONIZA - FALLO GRAVE" : "no sincroniza",
                   inf.tramas_ok, inf.tramas_malas, (double)inf.contraste);
            if (inf.estado == DCF_LISTO || inf.tramas_ok > 0U) { fallos++; }
            if (inf.contraste >= 0.25f) {
                printf("      FALLO: contraste %.2f con ruido puro\n", (double)inf.contraste);
                fallos++;
            }
        }
    }

    /* --- 5. Un bit cambiado ------------------------------------------- */
    /*
     * Hay que separar dos casos, y la diferencia NO es un detalle:
     *
     *  a) Un bit de RELLENO o una MARCA. Eso se rechaza siempre y en el acto,
     *     porque el decodificador sabe lo que tienen que valer.
     *
     *  b) Un bit de DATOS. BPM no lleva paridades -ni WWV tampoco-, asi que
     *     un bit de datos equivocado DE FORMA CONSISTENTE no se puede
     *     detectar mirando una trama: da una hora distinta pero legal, y la
     *     comprobacion de "un minuto mas que la anterior" tampoco lo caza,
     *     porque el error es el mismo en todas.
     *
     *     Lo unico que lo caza es que la cifra BCD se pase de nueve, y eso
     *     solo pasa en algunos valores. Por eso aqui no se exige que se
     *     rechace: se MIDE en cuantos minutos se acaba cazando. Es un limite
     *     real del formato y prefiero tenerlo escrito y medido que fingir
     *     que no existe.
     */
    printf("  bit de relleno o marca cambiado (se tiene que rechazar siempre):\n");
    {
        int bits[] = { 5, 14, 17, 18, 24, 27, 28, 34, 36, 38, 44, 55, 56, 58, 9, 29, 59 };
        unsigned k;
        for (k = 0; k < sizeof(bits) / sizeof(bits[0]); k++) {
            s_rnd = 5UL;
            wwv_start(HORA_BPM, FS);
            r = corre(22, 30, 23, 9, 26, 4, 0.0f, bits[k]);
            printf("    bit %2d -> %s (buenas %d, malas %d)\n", bits[k],
                   (r.estado == DCF_LISTO || r.ok > 0) ? "ACEPTADA - FALLO" : "rechazada",
                   r.ok, r.malas);
            if (r.estado == DCF_LISTO || r.ok > 0) { fallos++; }
        }
    }
    printf("  bit de DATOS cambiado: cuantos minutos tarda el BCD en cazarlo\n");
    printf("  (sin paridades no hay otra forma; ver el comentario del banco)\n");
    {
        int bits[] = { 3, 12, 22, 33, 46 };
        unsigned k;
        for (k = 0; k < sizeof(bits) / sizeof(bits[0]); k++) {
            int mm;
            int cazado = -1;
            for (mm = 2; mm <= 12 && cazado < 0; mm++) {
                s_rnd = 5UL;
                wwv_start(HORA_BPM, FS);
                r = corre(22, 30, 23, 9, 26, mm, 0.0f, bits[k]);
                if (r.malas > 0) { cazado = mm; }
            }
            if (cazado > 0) {
                printf("    bit %2d -> cazado a los %d minutos\n", bits[k], cazado);
            } else {
                printf("    bit %2d -> NO se caza en 12 minutos (da una hora legal pero falsa)\n",
                       bits[k]);
            }
        }
    }

    if (fallos == 0) { printf("  TODO CORRECTO (0 fallos)\n"); }
    else             { printf("  %d FALLOS\n", fallos); }
    return fallos ? 1 : 0;
}

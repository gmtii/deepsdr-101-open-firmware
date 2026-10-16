/*
 * Banco del decodificador MSF (la otra mitad de User/dcf77.c).
 *
 * Mismas razones que el de DCF77 y una mas: el formato de MSF es bastante
 * mas retorcido -dos bits por segundo, paridades IMPARES, BCD del mas
 * significativo al menos, y la hora en UTC que hay que pasar a española-, o
 * sea que hay cuatro sitios distintos donde equivocarse en silencio. Con una
 * emisora que a lo mejor ni se recibe desde aqui, un fallo de formato seria
 * indistinguible de la falta de señal para siempre.
 *
 * Lo que mas importa comprobar, ademas de lo de siempre:
 *   - que el desfase a hora española sea +1 en invierno y +2 en verano
 *   - que el patron fijo 01111110 de los segundos 52..59 rechace una trama
 *     corrida, que es como se rompen estas cosas en la practica
 */
#include "dcf77.h"
#include "hora_comun.h"   /* hora_huso_pon(): este banco pone el huso a mano */
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS      375.0f
#define ALTO   1000.0f
#define BAJO      0.0f   /* MSF apaga la portadora del todo, no la baja */

static unsigned long s_rnd = 24681357UL;
static double uni(void)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return (double)((s_rnd >> 16) & 0x7FFFUL) / 32767.0;
}
static double gauss(void)
{
    double u1 = uni(), u2 = uni();
    if (u1 < 1e-9) { u1 = 1e-9; }
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

/* BCD del MAS significativo al menos, como MSF. */
static void pon(uint8_t *a, int ini, int fin, int v)
{
    static const int peso[8] = { 80, 40, 20, 10, 8, 4, 2, 1 };
    int n = fin - ini + 1, off = 8 - n, k;
    for (k = 0; k < n; k++) {
        if (v >= peso[off + k]) { a[ini + k] = 1; v -= peso[off + k]; }
        else                    { a[ini + k] = 0; }
    }
}
/* Paridad IMPAR: el bit hace que el total (grupo + bit) sea impar. */
static uint8_t par_impar(const uint8_t *a, int ini, int fin)
{
    int k, n = 0;
    for (k = ini; k <= fin; k++) { n += a[k]; }
    return (uint8_t)((n & 1) ? 0 : 1);
}

/* Construye el minuto de MSF para la hora UTC dada. */
/*
 * dut1 en decimas de segundo, -8..+8. NO es un adorno: los bits de DUT1 van
 * en los segundos 01..16 con el bit A a CERO, o sea que son los unicos
 * segundos de la trama con A=0 y B=1 - el caso que apaga la portadora dos
 * veces dentro del mismo segundo, con un hueco encendido en medio.
 *
 * La primera version de este banco emitia DUT1 = 0 y por eso no producia
 * nunca ese caso, y por eso daba TODO CORRECTO mientras en la radio de
 * verdad MSF no pasaba de la primera trama. En el aire DUT1 casi nunca es
 * cero. Un banco que no genera el caso raro no es un banco, es una opinion.
 */
static void trama(uint8_t *a, uint8_t *b, int h_utc, int m, int d, int mes,
                  int anno, int ds, int verano, int dut1)
{
    int k;
    for (k = 0; k < 60; k++) { a[k] = 0; b[k] = 0; }
    /* DUT1 positivo: bits 01B..08B. Negativo: 09B..16B. */
    if (dut1 > 0) { for (k = 1; k <= dut1 && k <= 8; k++) { b[k] = 1; } }
    else if (dut1 < 0) { for (k = 1; k <= -dut1 && k <= 8; k++) { b[8 + k] = 1; } }
    pon(a, 17, 24, anno);
    pon(a, 25, 29, mes);
    pon(a, 30, 35, d);
    pon(a, 36, 38, ds);
    pon(a, 39, 44, h_utc);
    pon(a, 45, 51, m);
    a[52] = 0;
    for (k = 53; k <= 58; k++) { a[k] = 1; }
    a[59] = 0;
    b[54] = par_impar(a, 17, 24);
    b[55] = par_impar(a, 25, 35);
    b[56] = par_impar(a, 36, 38);
    b[57] = par_impar(a, 39, 51);
    b[58] = (uint8_t)(verano ? 1 : 0);
}

/*
 * Emite un minuto. El segundo 0 lleva la marca de minuto (500 ms apagada);
 * los demas, 100 ms siempre, +100 mas si A, +100 mas si B - y con A=0,B=1
 * son DOS apagones separados, que es el caso que obliga a muestrear en vez
 * de medir duraciones.
 */
static void emite_minuto(const uint8_t *a, const uint8_t *b, double ruido,
                         int corrido, int sin_senal)
{
    int seg, k;
    for (seg = 0; seg < 60; seg++) {
        int n = (int)(FS + 0.5f);
        for (k = 0; k < n; k++) {
            double ms = 1000.0 * (double)k / FS;
            int apagada;

            if (seg == 0 && !corrido) { apagada = (ms < 500.0); }
            else {
                apagada = (ms < 100.0)
                       || (a[seg] && ms >= 100.0 && ms < 200.0)
                       || (b[seg] && ms >= 200.0 && ms < 300.0);
            }
            {
                double v = sin_senal ? 0.0 : (apagada ? BAJO : ALTO);
                v += ruido * gauss();
                if (v < 0.0) { v = 0.0; }
                dcf77_feed((float)v);
            }
        }
    }
}

/* Ademas del resultado se guarda la ULTIMA hora emitida, para poder
 * comprobar que lo decodificado es exactamente eso mas el desfase. Con
 * ruido, el decodificador puede tardar mas en enganchar y sincronizar con
 * una trama posterior: eso NO es un fallo, es lo correcto. Lo que si seria
 * un fallo es que sacara una hora que nunca se emitio. Comparar contra un
 * minuto fijo escrito a mano confundia las dos cosas. */
typedef struct { int ok, minutos, h_emit, m_emit; dcf_info_t inf; } res_t;

static res_t corre(int h_utc, int m, int verano, double ruido, int corrido,
                   int sin_senal, int minutos, int dut1)
{
    uint8_t a[60], b[60];
    res_t r;
    int i, hh = h_utc, mm = m;
    /* La trama emitida en un minuto no se decodifica hasta que llega SU
     * marca de minuto, o sea durante el minuto siguiente. Asi que cuando el
     * decodificador dice LISTO, lo que ha decodificado es la trama ANTERIOR
     * a la que se acaba de emitir. Llevar la cuenta de la actual era un
     * desfase de un minuto en el banco, no en el decodificador. */
    int ant_h = -1, ant_m = -1;

    memset(&r, 0, sizeof r);
    s_rnd = 24681357UL;
    dcf77_start(HORA_MSF, FS);

    for (i = 0; i < minutos; i++) {
        int nh = hh, nm = mm + 1;          /* la trama describe el minuto siguiente */
        if (nm > 59) { nm = 0; nh = (nh + 1) % 24; }
        trama(a, b, nh, nm, 15, 6, 26, 1, verano, dut1);
        emite_minuto(a, b, ruido, corrido, sin_senal);
        r.h_emit = ant_h; r.m_emit = ant_m;
        ant_h = nh; ant_m = nm;
        hh = nh; mm = nm;

        dcf77_info(&r.inf);
        r.minutos = i + 1;
        if (r.inf.estado == DCF_LISTO) { r.ok = 1; break; }
    }
    dcf77_stop();
    return r;
}

static int fallos = 0;
static void comprueba(const char *que, int ok)
{
    printf("    %-54s %s\n", que, ok ? "OK" : "NO");
    if (!ok) { fallos++; }
}

int main(void)
{
    res_t r;

    printf("MSF: envolvente a %.0f muestras/s, portadora apagada del todo en las marcas\n\n", FS);

    /*
     * *** ESTE BANCO MEDIA ESPAÑA, Y POR ESO SALTO EN ROJO EL 10/10/2026. ***
     *
     * decodifica() de MSF sumaba "una hora en invierno y dos en verano"
     * escrito a mano, y aqui se comprobaba exactamente eso. Al quitar España
     * del firmware, los tres casos de conversion se pusieron en rojo: la
     * emisora ya no da hora de Madrid, da hora TUYA.
     *
     * Asi que ahora el banco pone el huso a mano antes de medir, que es lo
     * que de verdad hay que comprobar: que MSF suma EL HUSO y no un numero
     * fijo. Se mide con dos husos distintos -Madrid y Delhi- porque con uno
     * solo un "+60" cableado volveria a pasar sin que nadie se enterase.
     */
    printf("  1. SEÑAL LIMPIA, CON EL HUSO EN +1:00 (Madrid en invierno)\n");
    hora_huso_pon(60);
    r = corre(7, 30, 0, 0.0, 0, 0, 6, 3);
    printf("    sincroniza en %d min: %02u:%02u  %02u/%02u/%02u  %s\n",
           r.minutos, r.inf.hora, r.inf.minuto, r.inf.dia, r.inf.mes, r.inf.anno,
           r.inf.verano ? "verano" : "invierno");
    comprueba("sincroniza", r.ok);
    /* Sincroniza con la SEGUNDA y la TERCERA trama, no con la primera: al
     * arrancar, el seguidor de portadora no conoce aun el nivel y se pierde
     * la marca de minuto inicial. Pasa igual en la radio -nunca empiezas a
     * escuchar justo en un cambio de minuto- y es identico en DCF77. O sea
     * que la hora que sale es la tercera emitida: 07:33 UTC. */
    comprueba("07:33 UTC con el huso en +1:00 sale 08:33",
              r.inf.hora == 8U && r.inf.minuto == 33U);
    comprueba("la fecha es la correcta (15/06/26)",
              r.inf.dia == 15U && r.inf.mes == 6U && r.inf.anno == 26U);
    comprueba("y publica el UTC crudo, que es con lo que se mide el huso",
              r.inf.utc_hay == 1U && r.inf.utc_hora == 7U
              && r.inf.utc_minuto == 33U);

    printf("\n  1b. EL MISMO MINUTO CON EL HUSO EN +5:30 (Delhi)\n");
    hora_huso_pon(0);            /* el de antes no se pisa solo: ver hora_comun.c */
    hora_huso_pon(330);
    r = corre(7, 30, 0, 0.0, 0, 0, 6, 3);
    printf("    %02u:%02u\n", r.inf.hora, r.inf.minuto);
    comprueba("*** 07:33 UTC con el huso en +5:30 sale 13:03 ***",
              r.ok && r.inf.hora == 13U && r.inf.minuto == 3U);
    comprueba("o sea que suma EL HUSO y no una hora fija",
              r.inf.minuto == 3U);

    printf("\n  1c. HACIA ATRAS, QUE ES DONDE SE PIERDE UN DIA\n");
    hora_huso_pon(-300);         /* Nueva York en invierno */
    r = corre(1, 30, 0, 0.0, 0, 0, 6, 3);   /* la tercera trama es 01:33 UTC */
    printf("    01:33 UTC -> %02u:%02u  %02u/%02u/%02u\n",
           r.inf.hora, r.inf.minuto, r.inf.dia, r.inf.mes, r.inf.anno);
    comprueba("01:33 UTC con el huso en -5:00 sale 20:33", 
              r.ok && r.inf.hora == 20U && r.inf.minuto == 33U);
    /* Y LA FECHA RETROCEDE. La version anterior dejaba la fecha como venia
     * -lo decia su propio comentario- y por tanto aqui habria puesto el 15. */
    comprueba("*** y la fecha retrocede al 14, no se queda en el 15 ***",
              r.inf.dia == 14U && r.inf.mes == 6U && r.inf.anno == 26U);

    hora_huso_pon(60);

    printf("\n  2. CON EL BIT DE VERANO DEL REINO UNIDO PUESTO\n");
    r = corre(7, 30, 1, 0.0, 0, 0, 6, 3);
    printf("    %02u:%02u  %s\n", r.inf.hora, r.inf.minuto,
           r.inf.verano ? "verano" : "invierno");
    comprueba("sincroniza", r.ok);
    /*
     * *** Y AQUI LA HORA NO CAMBIA, QUE ES EL CAMBIO DE CRITERIO. ***
     *
     * El bit 58 de MSF dice si hay horario de verano EN EL REINO UNIDO. Eso
     * es la regla europea, y en Tokio o en Sidney no dice nada de donde
     * estas. La version anterior lo sumaba a la hora -"una en invierno y dos
     * en verano"- y por eso este mismo minuto salia 09:33. Ahora el huso ya
     * lleva dentro el verano del sitio donde estes, venga del RDS, de una
     * medida o de la tabla de ciudades, asi que este bit NO se suma.
     */
    comprueba("*** el bit de verano britanico YA NO mueve la hora ***",
              r.inf.hora == 8U && r.inf.minuto == 33U);
    comprueba("y tampoco se publica como 'verano' de aqui", r.inf.verano == 0U);

    printf("\n  3. LA MEDIANOCHE, QUE ES DONDE SE CRUZA EL DIA\n");
    r = corre(22, 45, 0, 0.0, 0, 0, 6, 3);   /* la tercera trama es 22:48 UTC */
    comprueba("22:48 UTC -> 23:48", r.ok && r.inf.hora == 23U && r.inf.minuto == 48U);
    r = corre(23, 45, 0, 0.0, 0, 0, 6, 3);   /* la tercera es 23:48 UTC */
    printf("    23:48 UTC -> %02u:%02u  %02u/%02u/%02u\n",
           r.inf.hora, r.inf.minuto, r.inf.dia, r.inf.mes, r.inf.anno);
    comprueba("23:48 UTC con el huso en +1:00 sale 00:48",
              r.ok && r.inf.hora == 0U && r.inf.minuto == 48U);
    comprueba("*** y el dia avanza al 16 ***", r.inf.dia == 16U);

    printf("\n  4. CON RUIDO\n");
    {
        double niv[] = { 60.0, 150.0, 300.0, 500.0 };
        int i, ultimo = -1;
        for (i = 0; i < 4; i++) {
            r = corre(12, 0, 0, niv[i], 0, 0, 8, 3);
            printf("    sigma %5.0f (%3.0f%% de la portadora)  %s\n", niv[i],
                   100.0 * niv[i] / ALTO,
                   r.ok ? "sincroniza" : "no sincroniza");
            if (r.ok) {
                int esperada = (r.h_emit + 1) % 24;   /* el huso esta en +1:00 */
                ultimo = i;
                printf("      decodificado %02u:%02u, emitido %02d:%02d UTC\n",
                       r.inf.hora, r.inf.minuto, r.h_emit, r.m_emit);
                comprueba("      la hora decodificada ES la que se emitio",
                          (int)r.inf.hora == esperada && (int)r.inf.minuto == r.m_emit);
            }
        }
        comprueba("aguanta al menos un 15% de ruido", ultimo >= 1);
    }

    printf("\n  5. TRAMA CORRIDA (sin marca de minuto)\n");
    r = corre(12, 0, 0, 0.0, 1, 0, 8, 3);
    printf("    %s (buenas %u, malas %u)\n",
           r.ok ? "*** SE HA COLADO ***" : "rechazada",
           r.inf.tramas_ok, r.inf.tramas_malas);
    comprueba("el patron fijo 01111110 la rechaza", !r.ok);

    printf("\n  6. SIN SEÑAL - LO IMPORTANTE\n");
    {
        double niv[] = { 50.0, 300.0, 1000.0 };
        int i;
        for (i = 0; i < 3; i++) {
            r = corre(0, 0, 0, niv[i], 0, 1, 12, 3);
            printf("    solo ruido sigma %6.0f -> %s (buenas %u, malas %u)\n", niv[i],
                   r.ok ? "*** SE INVENTA UNA HORA ***" : "no sincroniza",
                   r.inf.tramas_ok, r.inf.tramas_malas);
            comprueba("      NO se inventa una hora", !r.ok);
        }
    }

    printf("\n  %s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

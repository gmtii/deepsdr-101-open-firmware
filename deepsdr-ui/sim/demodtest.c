/*
 * EL BANCO DE demod_am.c, QUE NO TENIA NINGUNO. 10/10/2026.
 *
 * La deuda estaba escrita en el propio fichero, al lado de la union s_via,
 * el 10/10/2026 por la mañana:
 *
 *     "demod_am.c es el unico fichero grande del arbol que NINGUN banco
 *      compila -tiene 37 cabeceras propias- asi que esto no esta probado
 *      en marcha, solo comprobado estaticamente. Es la deuda que deja
 *      este cambio."
 *
 * Y eran 4.096 bytes de RAM apoyados en un argumento de lectura: que los
 * dos caminos de audio -el ancho de WFM a 192 kHz y el estrecho de
 * AM/SSB/NFM a 96 kHz- no pueden estar vivos a la vez, asi que pueden
 * compartir los mismos bytes. El argumento es bueno. Lo que no habia era
 * nadie que lo viese pasar.
 *
 * Ahora demod_am.c COMPILA Y CORRE aqui, con:
 *
 *   - sus modulos de verdad donde se puede: rapido.c, nco.c, nco_tabla.c,
 *     agc.c, nb.c, audiofil.c, anotch.c, nr_ss.c y sam.c. Es su codigo;
 *   - hostinc/arm_math.h para las cuatro familias del CMSIS que usa, con
 *     su propio banco (sim/armmath_test.c) porque un sucedaneo sin banco
 *     es una suposicion con sintaxis de C;
 *   - sim/demod_stubs.c para los veintitantos decodificadores, vacios y
 *     contando, porque lo que se mide aqui es EL REPARTO.
 *
 * LO QUE MIDE Y LO QUE NO:
 *
 *   SI: que la union solapa de verdad -medido desde FUERA, con las
 *       direcciones que el propio codigo le pasa al I2S, sin tocar
 *       demod_am.c y sin copiarse su declaracion-; que cada camino entrega
 *       su propia longitud; que pasar por el camino estrecho no deja nada
 *       que el ancho se encuentre despues; y que el audio va a parar a los
 *       decodificadores que estan encendidos y no a los otros.
 *   NO: que suene bien. Eso no se mide aqui ni se puede.
 *
 *   make demod FIRMWARE=...
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"
#include "gd32f4xx.h"   /* el contador de ciclos de mentira, para moverlo a mano */

static int fallos;

static void ok(const char *que, int bien, const char *porque)
{
    if (bien) {
        printf("    %-52s ok\n", que);
    } else {
        printf("    %-52s *** MAL ***  %s\n", que, porque ? porque : "");
        fallos++;
    }
}

/* --------------------------------------------------------------------
 * Una señal de entrada que no sea trivial: una portadora modulada en
 * amplitud mas ruido, entrelazada I,Q como la deja el DMA de captura.
 * No pretende parecerse a ninguna emision; pretende que ninguna etapa se
 * pueda saltar por tener la entrada a cero.
 * -------------------------------------------------------------------- */
static uint32_t semilla = 12345u;
static float azar(void)
{
    semilla = semilla * 1103515245u + 12345u;
    return (float)((semilla >> 16) & 0x7FFFu) / 16383.5f - 1.0f;
}

static void genera(int16_t *dst, uint32_t n, float f_hz, float fs_hz,
                   float ruido)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        float t = (float)k / fs_hz;
        float env = 0.6f + 0.4f * sinf(2.0f * 3.14159265f * 400.0f * t);
        float ph = 2.0f * 3.14159265f * f_hz * t;
        dst[2u * k]      = (int16_t)(8000.0f * env * cosf(ph) + ruido * 800.0f * azar());
        dst[2u * k + 1u] = (int16_t)(8000.0f * env * sinf(ph) + ruido * 800.0f * azar());
    }
}

static int16_t g_ancho[SDR_RX_BLOCK_SAMPLES_WFM * 2u];
static int16_t g_estre[SDR_RX_BLOCK_SAMPLES * 2u];

/* --------------------------------------------------------------------
 * 1. LA UNION, MEDIDA DESDE FUERA.
 *
 * Los dos caminos acaban llamando a gd32_i2s_stream_write_half() con su
 * propio audio_out. Esas dos direcciones son dos puntos del MISMO objeto
 * si la union es una union, y estan a distancias que se saben de
 * antemano: audio_out va detras de i_buf, q_buf y env, o sea
 *
 *     desplazamiento = 3 * MUESTRAS * sizeof(float32_t)
 *
 * que son 6.144 bytes en el camino ancho (512 muestras) y 3.072 en el
 * estrecho (256). Restando cada una su desplazamiento tiene que salir LA
 * MISMA direccion: el principio de la union.
 *
 * Esto NO se puede falsear leyendo el fuente: si alguien deshace la union
 * y vuelve a los dos juegos de buffers separados, las dos bases dejan de
 * coincidir y esto se pone rojo, aunque el codigo siga compilando y
 * sonando igual de bien. Que es exactamente el fallo del que no avisa
 * nadie: perder 4.096 bytes sin enterarse.
 * -------------------------------------------------------------------- */
static void prueba_union(void)
{
    uintptr_t p_ancho, p_estre, base_a, base_e;
    const size_t D_ANCHO = 3u * SDR_RX_BLOCK_SAMPLES_WFM * sizeof(float);
    const size_t D_ESTRE = 3u * SDR_RX_BLOCK_SAMPLES * sizeof(float);
    const size_t FIN_ANCHO = D_ANCHO + SDR_RX_BLOCK_SAMPLES_WFM * 2u * sizeof(int16_t);
    const size_t FIN_ESTRE = D_ESTRE + SDR_RX_BLOCK_SAMPLES * 2u * sizeof(int16_t);

    printf("\n  1. la union s_via, mirada por las direcciones que entrega\n");

    demod_am_init();
    cons_todos_apagados();
    cons_reinicia();

    demod_am_set_mode(DEMOD_MODE_WFM);
    genera(g_ancho, SDR_RX_BLOCK_SAMPLES_WFM, 30000.0f, 192000.0f, 0.2f);
    demod_wfm_process_raw(g_ancho);
    p_ancho = g_cons[CONS_I2S].ultimo;
    ok("el camino ancho ha entregado audio", g_cons[CONS_I2S].llamadas == 1u,
       "no ha llamado al I2S");

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_AM);
    cons_reinicia();
    genera(g_estre, SDR_RX_BLOCK_SAMPLES, 15000.0f, 96000.0f, 0.2f);
    demod_am_process_raw(g_estre);
    p_estre = g_cons[CONS_I2S].ultimo;
    ok("el camino estrecho ha entregado audio", g_cons[CONS_I2S].llamadas == 1u,
       "no ha llamado al I2S");

    if (!p_ancho || !p_estre) { return; }

    base_a = p_ancho - D_ANCHO;
    base_e = p_estre - D_ESTRE;

    {
        char d[96];
        snprintf(d, sizeof d, "ancho %p, estrecho %p",
                 (void *)base_a, (void *)base_e);
        ok("los dos caminos arrancan en la MISMA direccion", base_a == base_e, d);
    }
    ok("el estrecho cabe dentro del ancho", FIN_ESTRE <= FIN_ANCHO,
       "el camino estrecho se ha hecho mas grande");
    ok("y no son el mismo sitio (si no, no habria dos juegos)",
       p_ancho != p_estre, "audio_out en el mismo desplazamiento");

    printf("      base de la union      %p\n", (void *)base_a);
    printf("      audio_out ancho       %p  (+%u)\n", (void *)p_ancho,
           (unsigned)D_ANCHO);
    printf("      audio_out estrecho    %p  (+%u)\n", (void *)p_estre,
           (unsigned)D_ESTRE);
    printf("      la union mide         %u bytes, y asi se ahorran %u\n",
           (unsigned)FIN_ANCHO, (unsigned)FIN_ESTRE);
}

/* --------------------------------------------------------------------
 * 2. CADA CAMINO, SU LONGITUD.
 *
 * bip_mete() si lleva longitud, al contrario que la escritura del I2S, y
 * los dos caminos lo llaman con la suya justo antes de entregar. Es el
 * sitio donde se puede comprobar, en marcha, que el ancho mueve 512
 * tramas y el estrecho 256 - el invariante que se rompio el 04/08/2026,
 * cuando la cuenta del modulo del I2S no se movio con el buffer.
 * -------------------------------------------------------------------- */
static void prueba_longitudes(void)
{
    printf("\n  2. cada camino entrega su propia longitud\n");

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    cons_reinicia();
    demod_wfm_process_raw(g_ancho);
    ok("el ancho mueve 512 tramas",
       g_cons[CONS_BIP].ultimo_n == SDR_RX_BLOCK_SAMPLES_WFM, "no son 512");

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_AM);
    cons_reinicia();
    demod_am_process_raw(g_estre);
    ok("el estrecho mueve 256 tramas",
       g_cons[CONS_BIP].ultimo_n == SDR_RX_BLOCK_SAMPLES, "no son 256");

    ok("y el audio que se manda es el mismo buffer que el del bip",
       g_cons[CONS_BIP].ultimo == g_cons[CONS_I2S].ultimo,
       "el bip y el I2S miran a sitios distintos");
}

/* --------------------------------------------------------------------
 * 3. SIN RESIDUO: PASAR POR EL ESTRECHO NO DEJA NADA.
 *
 * Esta es la que de verdad pone a prueba el argumento de la union. Si
 * algun paso del camino ancho LEYERA un byte de la union antes de
 * escribirlo, lo que se encontraria seria lo que dejo el estrecho.
 *
 * Se compara el audio de una tanda de WFM contra la misma tanda de WFM
 * precedida de una tanda de AM que ensucia los mismos bytes a proposito.
 * demod_am_init() entre medias pone a cero todo el estado que NO esta en
 * la union -filtros, AGC, deenfasis-, asi que lo unico que puede quedar
 * del paso anterior son precisamente los bytes compartidos.
 *
 * Tiene que salir identico HASTA EL ULTIMO BIT. Un "parecido" aqui no
 * vale: cualquier diferencia es un byte leido antes de escribirse.
 * -------------------------------------------------------------------- */
static void prueba_sin_residuo(void)
{
    static int16_t ref[SDR_RX_BLOCK_SAMPLES_WFM * 2u];
    static int16_t dsp[SDR_RX_BLOCK_SAMPLES_WFM * 2u];
    const int BLOQUES = 24;
    int i, dif = 0;

    printf("\n  3. pasar por el camino estrecho no deja residuo en el ancho\n");

    cons_captura_tramas(SDR_RX_BLOCK_SAMPLES_WFM);

    /* Tanda limpia. */
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    semilla = 999u;
    genera(g_ancho, SDR_RX_BLOCK_SAMPLES_WFM, 30000.0f, 192000.0f, 0.3f);
    for (i = 0; i < BLOQUES; i++) { demod_wfm_process_raw(g_ancho); }
    memcpy(ref, cons_audio(), sizeof ref);

    /* Ahora se ensucia la union con el camino estrecho... */
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_AM);
    semilla = 4242u;
    genera(g_estre, SDR_RX_BLOCK_SAMPLES, 15000.0f, 96000.0f, 0.9f);
    for (i = 0; i < BLOQUES; i++) { demod_am_process_raw(g_estre); }

    /* ...y se repite exactamente la tanda limpia. */
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    semilla = 999u;
    genera(g_ancho, SDR_RX_BLOCK_SAMPLES_WFM, 30000.0f, 192000.0f, 0.3f);
    for (i = 0; i < BLOQUES; i++) { demod_wfm_process_raw(g_ancho); }
    memcpy(dsp, cons_audio(), sizeof dsp);

    for (i = 0; i < (int)(sizeof ref / sizeof ref[0]); i++) {
        if (ref[i] != dsp[i]) { dif++; }
    }
    {
        char d[64];
        snprintf(d, sizeof d, "%d muestras de %u distintas", dif,
                 (unsigned)(sizeof ref / sizeof ref[0]));
        ok("WFM da el MISMO audio, bit a bit, con y sin AM por medio",
           dif == 0, d);
    }

    /* Y al reves: el estrecho despues del ancho. */
    {
        static int16_t ref2[SDR_RX_BLOCK_SAMPLES * 2u];
        static int16_t dsp2[SDR_RX_BLOCK_SAMPLES * 2u];
        cons_captura_tramas(SDR_RX_BLOCK_SAMPLES);

        demod_am_init();
        demod_am_set_mode(DEMOD_MODE_USB);
        semilla = 777u;
        genera(g_estre, SDR_RX_BLOCK_SAMPLES, 12000.0f, 96000.0f, 0.3f);
        for (i = 0; i < BLOQUES; i++) { demod_am_process_raw(g_estre); }
        memcpy(ref2, cons_audio(), sizeof ref2);

        demod_am_init();
        demod_am_set_mode(DEMOD_MODE_WFM);
        semilla = 31337u;
        genera(g_ancho, SDR_RX_BLOCK_SAMPLES_WFM, 50000.0f, 192000.0f, 0.9f);
        for (i = 0; i < BLOQUES; i++) { demod_wfm_process_raw(g_ancho); }

        demod_am_init();
        demod_am_set_mode(DEMOD_MODE_USB);
        semilla = 777u;
        genera(g_estre, SDR_RX_BLOCK_SAMPLES, 12000.0f, 96000.0f, 0.3f);
        for (i = 0; i < BLOQUES; i++) { demod_am_process_raw(g_estre); }
        memcpy(dsp2, cons_audio(), sizeof dsp2);

        dif = 0;
        for (i = 0; i < (int)(sizeof ref2 / sizeof ref2[0]); i++) {
            if (ref2[i] != dsp2[i]) { dif++; }
        }
        {
            char d[64];
            snprintf(d, sizeof d, "%d muestras distintas", dif);
            ok("USB da el MISMO audio, bit a bit, con y sin WFM por medio",
               dif == 0, d);
        }
    }
    cons_captura_tramas(0u);
}

/* --------------------------------------------------------------------
 * 4. EL REPARTO VA DONDE DICE.
 *
 * El que esta encendido come y el que no, no. Dicho asi parece obvio; en
 * un fichero que reparte a veintitantos sitios con las condiciones
 * repartidas por 4.000 lineas, no lo es.
 *
 * *** Y AQUI EL BANCO ME CORRIGIO A MI, EN SU PRIMERA EJECUCION. ***
 *
 * La primera version exigia "nadie mas ha comido" y salia roja en cinco
 * de los seis casos, siempre con el mismo culpable: rtty_scope. Y el
 * firmware era cierto y yo no. El osciloscopio de sintonia NO es un
 * decodificador mas: es la ayuda para sintonizar que comparten ocho de
 * ellos, y demod_am.c lo alimenta UNA vez si alguno de esos ocho esta
 * encendido (linea 3697). Exigir silencio ahi era exigir que la radio
 * hiciera algo peor.
 *
 * Asi que el osciloscopio se saca de la cuenta de "sobra alguien" y se le
 * pone su propia prueba, la de abajo, que es la que de verdad vale: que
 * se enciende con los ocho de la lista y NO con los que no estan en ella.
 * -------------------------------------------------------------------- */
static void come(int quien, const char *nombre, demod_mode_t modo,
                 int bloques)
{
    int i;
    demod_am_init();
    demod_am_set_mode(modo);
    cons_todos_apagados();
    cons_reinicia();
    g_cons[quien].activo = 1u;
    for (i = 0; i < bloques; i++) {
        if (modo == DEMOD_MODE_WFM) { demod_wfm_process_raw(g_ancho); }
        else                        { demod_am_process_raw(g_estre); }
    }
    {
        char q[80], d[64];
        snprintf(q, sizeof q, "%s encendido recibe muestras", nombre);
        snprintf(d, sizeof d, "%u llamadas, %u muestras",
                 g_cons[quien].llamadas, g_cons[quien].muestras);
        ok(q, g_cons[quien].llamadas > 0u, d);
    }
    {
        int otro, sobra = 0;
        char q[80], d[160];
        size_t l = 0;
        d[0] = 0;
        for (otro = 0; otro < CONS_N; otro++) {
            if (otro == quien || otro == CONS_BIP || otro == CONS_I2S
                || otro == CONS_SCOPE) { continue; }
            if (g_cons[otro].llamadas) {
                sobra++;
                if (l < sizeof d - 20) {
                    l += (size_t)snprintf(d + l, sizeof d - l, "%s(%u) ",
                                          cons_nombre(otro),
                                          g_cons[otro].llamadas);
                }
            }
        }
        snprintf(q, sizeof q, "%s: ningun otro decodificador come", nombre);
        ok(q, sobra == 0, d);
    }
}

/* La puerta del osciloscopio: ocho si, los demas no. Un decodificador
 * que NO esta en esa lista no tiene por que gastar el acumulador de otro,
 * y si algun dia alguien mete una llamada sin puerta, esto lo dice. */
static void scope_con(int quien, const char *nombre, demod_mode_t modo,
                      int espera)
{
    int i;
    char q[88];
    demod_am_init();
    demod_am_set_mode(modo);
    cons_todos_apagados();
    cons_reinicia();
    g_cons[quien].activo = 1u;
    for (i = 0; i < 8; i++) {
        if (modo == DEMOD_MODE_WFM) { demod_wfm_process_raw(g_ancho); }
        else                        { demod_am_process_raw(g_estre); }
    }
    snprintf(q, sizeof q, "con %-12s el osciloscopio %s", nombre,
             espera ? "come" : "se queda callado");
    ok(q, (g_cons[CONS_SCOPE].llamadas > 0u) == (espera != 0),
       espera ? "no ha comido" : "ha comido sin estar en la lista");
}

static void prueba_reparto(void)
{
    printf("\n  4. el audio va a los decodificadores encendidos, y solo a ellos\n");
    semilla = 55u;
    genera(g_estre, SDR_RX_BLOCK_SAMPLES, 12000.0f, 96000.0f, 0.2f);
    genera(g_ancho, SDR_RX_BLOCK_SAMPLES_WFM, 30000.0f, 192000.0f, 0.2f);

    come(CONS_CW,     "CW   (USB)",  DEMOD_MODE_USB, 8);
    come(CONS_RTTY,   "RTTY (USB)",  DEMOD_MODE_USB, 8);
    come(CONS_NAVTEX, "NAVTEX(USB)", DEMOD_MODE_USB, 8);
    come(CONS_SSTV,   "SSTV (USB)",  DEMOD_MODE_USB, 8);
    come(CONS_HELL,   "HELL (USB)",  DEMOD_MODE_USB, 8);
    come(CONS_RDS,    "RDS  (WFM)",  DEMOD_MODE_WFM, 8);
    come(CONS_APRS,   "APRS (NFM)",  DEMOD_MODE_NFM, 8);
    /*
     * Y LA SSTV POR LOS DOS CAMINOS - 13/10/2026.
     *
     * Pedido por un usuario.
     *
     * El mismo decodificador y dos entradas, igual que el AX.25 con APRS y
     * PKT300. Que coma en NFM es la mitad de lo que hay que comprobar; la
     * otra mitad -y la que de verdad puede salir mal en silencio- es A QUE
     * TASA come, y esta justo debajo.
     */
    come(CONS_SSTV,   "SSTV (NFM)",  DEMOD_MODE_NFM, 8);

    /*
     * LAS DOS ENTRADAS DE LA SSTV TIENEN QUE DAR LA MISMA TASA.
     *
     * sstv_start() se llama con DEMOD_DEC_FS_HZ -12 kHz- venga el audio de
     * donde venga. Por banda lateral eso sale solo, porque ese camino ya
     * trabaja diezmado. Por FM el discriminador entrega a 48 o 96 kHz, asi
     * que demod_am.c tiene que diezmar antes de darselo.
     *
     * Si eso se olvidara, no reventaria nada: el decodificador mediria los
     * tiempos de linea con un reloj ocho veces mas rapido y la foto saldria
     * aplastada, sin que nada en la pantalla dijera por que. Por eso se
     * comparan las dos cuentas y no se mira solo que "coma".
     */
    {
        unsigned ssb, fm;
        int i;

        demod_am_init(); demod_am_set_mode(DEMOD_MODE_USB);
        cons_todos_apagados(); cons_reinicia();
        g_cons[CONS_SSTV].activo = 1u;
        for (i = 0; i < 8; i++) { demod_am_process_raw(g_estre); }
        ssb = g_cons[CONS_SSTV].muestras;

        demod_am_init(); demod_am_set_mode(DEMOD_MODE_NFM);
        cons_todos_apagados(); cons_reinicia();
        g_cons[CONS_SSTV].activo = 1u;
        for (i = 0; i < 8; i++) { demod_am_process_raw(g_estre); }
        fm = g_cons[CONS_SSTV].muestras;

        {
            char q[96], d[80];
            snprintf(d, sizeof d, "banda lateral %u muestras, FM %u", ssb, fm);
            snprintf(q, sizeof q,
                     "la SSTV recibe la MISMA tasa por los dos caminos");
            ok(q, ssb == fm && ssb > 0u, d);
        }
    }

    printf("\n     y la puerta del osciloscopio de sintonia\n");
    /* Los que SI estan en la lista de la linea 3697. */
    scope_con(CONS_CW,     "CW",     DEMOD_MODE_USB, 1);
    scope_con(CONS_RTTY,   "RTTY",   DEMOD_MODE_USB, 1);
    scope_con(CONS_NAVTEX, "NAVTEX", DEMOD_MODE_USB, 1);
    scope_con(CONS_SSTV,   "SSTV",   DEMOD_MODE_USB, 1);
    scope_con(CONS_PSK31,  "PSK31",  DEMOD_MODE_USB, 1);
    scope_con(CONS_DSC,    "DSC",    DEMOD_MODE_USB, 1);
    /* APRS va por su propio camino (aprs_scope_feed, en la rama de FM). */
    scope_con(CONS_APRS,   "APRS",   DEMOD_MODE_NFM, 1);
    /* Y los que no: ni Feld-Hell ni el Ident ni STANAG estan en la lista. */
    scope_con(CONS_HELL,   "HELL",   DEMOD_MODE_USB, 0);
    scope_con(CONS_IDENT,  "IDENT",  DEMOD_MODE_USB, 0);
    scope_con(CONS_STANAG, "STANAG", DEMOD_MODE_USB, 0);
    /* Y con nada encendido, nadie. */
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_USB);
    cons_todos_apagados();
    cons_reinicia();
    { int i; for (i = 0; i < 8; i++) { demod_am_process_raw(g_estre); } }
    ok("sin ningun modo digital, el osciloscopio no come",
       g_cons[CONS_SCOPE].llamadas == 0u, "ha comido con todo apagado");
}

/* --------------------------------------------------------------------
 * 5. LA CUENTA DE CICLOS RESTA, NO SUMA.
 *
 * demod_am.c publica cuanto le ha costado el bloque, y esa cifra sale en
 * la pantalla de Informacion. Aqui el contador de ciclos es de mentira y
 * lo mueve el banco una cantidad conocida justo antes de que lo lea por
 * segunda vez, asi que la resta tiene que dar exactamente eso.
 * -------------------------------------------------------------------- */
static void prueba_ciclos(void)
{
    printf("\n  5. los ciclos publicados son una resta y no otra cosa\n");

    int i;

    cons_paso_ciclos(1234u);

    /*
     * *** VARIOS BLOQUES, Y NO UNO. EL BANCO SE CAZO A SI MISMO. ***
     *
     * La primera version hacia UN bloque justo despues de init(), con el
     * contador a cero. Y con el contador a cero da igual restar que sumar:
     * 0 + x es lo mismo que x - 0. Al probar la mutacion -cambiar el menos
     * por un mas en demod_am.c- el banco siguio diciendo TODO CORRECTO.
     *
     * Una comprobacion que no distingue la suma de la resta no esta
     * comprobando la resta: esta comprobando que el numero no es cero.
     * Con tres bloques el contador ya vale algo al empezar el tercero, y
     * entonces las dos cuentas se separan.
     */
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_AM);
    dwt_falso.CYCCNT = 0u;
    for (i = 0; i < 3; i++) { demod_am_process_raw(g_estre); }
    ok("el camino estrecho publica el salto exacto",
       demod_am_get_last_cycles() == 1234u, "la resta no cuadra");
    ok("y el contador se ha movido de verdad entre bloques",
       dwt_falso.CYCCNT == 3u * 1234u, "el contador de mentira no avanza");

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    dwt_falso.CYCCNT = 0u;
    for (i = 0; i < 3; i++) { demod_wfm_process_raw(g_ancho); }
    ok("el camino ancho publica el suyo, por separado",
       demod_wfm_get_last_cycles() == 1234u, "la resta no cuadra");

    cons_paso_ciclos(0u);
}

/*
 * QUE LADO DE LA PORTADORA SE OYE, Y QUE EL ESPECTRO LO SEPA - 15/10/2026.
 *
 * Pedido por el usuario.
 *
 * El sombreado del espectro se decidia con una cadena de if sobre el MODO,
 * dentro de main.c. SAM-U y SAM-L son un AJUSTE de SAM, no modos propios,
 * asi que caian en el `else` de "doble banda lateral" y se pintaban a dos
 * bandas mientras la radio demodulaba una.
 *
 * Que el fallo durase tanto no es casualidad: la regla vivia en main.c y
 * EL SIMULADOR NO ENLAZA main.c, asi que no habia nada que la mirara. Esto
 * mide la regla ya mudada a demod_am.c, con los SEIS modos y las TRES
 * formas de SAM - que son justo las dos que faltaban.
 */
static void prueba_banda_lado(void)
{
    struct { demod_mode_t m; sam_banda_t b; int esperado; const char *que; } k[] = {
        { DEMOD_MODE_AM,  SAM_BANDA_AMBAS,  0, "AM: las dos bandas" },
        { DEMOD_MODE_USB, SAM_BANDA_AMBAS,  1, "USB: solo por encima" },
        { DEMOD_MODE_LSB, SAM_BANDA_AMBAS, -1, "LSB: solo por debajo" },
        { DEMOD_MODE_NFM, SAM_BANDA_AMBAS,  0, "NFM: simetrico" },
        { DEMOD_MODE_WFM, SAM_BANDA_AMBAS,  0, "WFM: simetrico" },
        { DEMOD_MODE_SAM, SAM_BANDA_AMBAS,  0, "SAM: las dos bandas" },
        { DEMOD_MODE_SAM, SAM_BANDA_USB,    1, "SAM-U: solo por encima" },
        { DEMOD_MODE_SAM, SAM_BANDA_LSB,   -1, "SAM-L: solo por debajo" },
    };
    unsigned i;

    printf("\n  EL LADO DE LA BANDA DE PASO\n");
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        int r;
        demod_am_set_mode(k[i].m);
        demod_am_set_sam_banda(k[i].b);
        r = (int)demod_am_banda_lado();
        printf("    %-28s sale %+d\n", k[i].que, r);
        ok(k[i].que, r == k[i].esperado,
           "el sombreado del espectro diria otra cosa que la radio");
    }
    /*
     * Y QUE EL AJUSTE DE SAM NO SE CUELE EN OTROS MODOS. Si la regla
     * mirara s_sam_banda sin mirar antes el modo, poner SAM-U y volver a
     * AM dejaria el sombreado torcido en AM. Es el fallo gemelo del que se
     * esta arreglando, en la otra direccion.
     */
    demod_am_set_sam_banda(SAM_BANDA_USB);
    demod_am_set_mode(DEMOD_MODE_AM);
    ok("y el ajuste de SAM no se cuela en AM",
       demod_am_banda_lado() == 0,
       "volver de SAM-U a AM dejaria el sombreado a un lado");
    demod_am_set_sam_banda(SAM_BANDA_AMBAS);
}

int main(void)
{
    printf("\nEL CAMINO DE AUDIO DE demod_am.c, CORRIENDO DE VERDAD\n");
    prueba_banda_lado();
    prueba_union();
    prueba_longitudes();
    prueba_sin_residuo();
    prueba_reparto();
    prueba_ciclos();

    printf("\n");
    if (fallos) { printf("HAY FALLOS (%d)\n\n", fallos); return 1; }
    printf("TODO CORRECTO (0 fallos)\n\n");
    return 0;
}

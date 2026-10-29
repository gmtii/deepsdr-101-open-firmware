/*
 * ======================================================================
 * FT8 DE VERDAD, DE PUNTA A PUNTA: CUANTO ENTRA Y CUANTO SALE
 * ======================================================================
 *
 * POR QUE EXISTE - 23/10/2026.
 *
 * *** Avisado: la radio decodifica menos estaciones de las que se oyen en
 * antena. ***
 *
 * Y no habia forma de contestar. Toda la cadena de FT8 -remuestreador,
 * FFT de 1024, adaptador de cascada y decodificador- no tenia NINGUN banco
 * que la recorriera entera: los cinco que hay miden trozos (la lista, el
 * SNR, las tablas de la FFT, el panel, el desvio) y ninguno mete audio por
 * un lado para contar mensajes por el otro. La cabecera de ft8_decoder.h
 * cita un banco de WAV que "ya ejercitaba este camino"... y ese banco
 * desaparecio con test/host/. O sea que la pregunta "¿cuanta banda se
 * escucha de verdad?" solo se podia contestar mirandolo en antena.
 *
 * ESTO MONTA UNA RANURA DE FT8 ENTERA EN MEMORIA -emisiones sinteticas
 * sobre ruido blanco, a 12 kHz, que es exactamente lo que demod_am.c le
 * entrega a ft8_modo_come()- y la pasa por EL CODIGO DEL FIRMWARE, sin
 * tocarlo: User/ft8_decimator.c, User/ft8_fft.c,
 * User/ft8_waterfall_adapter.c y User/ft8_decoder.c, mas Ft8Lib. Despues
 * cuenta los mensajes que salen.
 *
 * EL CODIFICADOR ES DE AQUI, y hace falta decirlo: Ft8Lib trae el
 * decodificador pero NO el codificador (no esta encode.c). Lo de abajo
 * -CRC, producto por la matriz generadora, codigo Gray y los tres grupos
 * de sincronismo- esta escrito aqui a partir de la estructura del
 * protocolo, usando las tablas que Ft8Lib ya publica. Se comprueba solo:
 * si el codificador estuviera mal, el decodificador DEL FIRMWARE no
 * sacaria el mensaje, y la primera prueba de abajo es justo esa.
 *
 * LO QUE MIDE, y por que cada una:
 *
 *   1. IDA Y VUELTA. Una emision limpia, en el centro de la ventana. Si
 *      esto falla no hay nada mas que mirar: o el codificador de aqui o la
 *      cadena de alla estan rotos.
 *
 *   2. LA BANDA QUE SE ESCUCHA. La misma emision barriendo de 100 a 3.000
 *      Hz. Dice DONDE se acaba la ventana, que es la pregunta del usuario.
 *
 *   3. LA REJILLA DE MEDIO BIN. La misma emision movida de 0 a 6,25 Hz en
 *      pasos pequenos, al borde de la sensibilidad. La ventana de analisis
 *      mide 1.024 muestras (dos simbolos) y de sus bins se guarda UNO DE
 *      CADA DOS, asi que una emision que caiga en un bin impar no tiene
 *      donde verse. Esto lo mide en vez de razonarlo.
 *
 *   4. LA SENSIBILIDAD. Barrido de relacion senal/ruido, varias semillas.
 *
 *   5. LA BANDA LLENA. Veinticinco emisiones a la vez, como una banda
 *      abierta de verdad: cuantas se decodifican y cuantas llegan a la
 *      cola de la pantalla, que son dos numeros distintos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft.h"
#include "ft8_decoder.h"
#include "ft8_shared_ram.h"
#include "ft8/constants.h"
#include "ft8/crc.h"
#include "ft8/message.h"
#include "rtc_hw.h"

/* --------------------------------------------------------------- tapones */
void rtc_hw_get(rtc_hw_datetime_t *o) { if (o) { memset(o, 0, sizeof *o); } }
const char *dxcc_pais(const char *ind) { (void)ind; return 0; }

#include "ft8_gen.h"

/* ---------------------------------------------- pasar la ranura por el fw */
#define MAX_SALIDA 64
static char g_salida[MAX_SALIDA][FT8_DECODER_LINE_LEN];
static int  g_n_salida;
static int  g_n_total;       /* lo que el decodificador dice haber sacado */
static int  g_n_cand;

static double g_ms_dec;   /* lo que tardo la ultima decodificacion */

static int decodifica_ranura(void)
{
    int n;
    ft8_decoded_msg_t m;

    ft8_fft_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();
    ft8_decoder_init();

    for (n = 0; n < RANURA_N && !ft8_waterfall_is_full(); n += 256) {
        int16_t trozo[256];
        int j;
        for (j = 0; j < 256 && (n + j) < RANURA_N; j++) {
            float v = g_audio[n + j];
            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            trozo[j] = (int16_t)v;
        }
        ft8_decimator_feed_block(trozo, (uint16_t)j);
        ft8_decimator_poll();
    }

    {
        /*
         * LO QUE TARDA LA DECODIFICACION, que desde el 28/10/2026 es un
         * numero con consecuencias: mientras dura, la captura de la ranura
         * siguiente se escribe en el desvio, que son
         * FT8_ADAPTER_LADO_BLOQUES bloques de 160 ms. Si tardara mas, lo
         * que no quepa se pierde (lo cuenta ft8_waterfall_desbordes()).
         *
         * Al pasar de 256 a 512 bins el buscador de candidatos mira el
         * doble de banda, asi que el numero importa mas que antes. Esto
         * es el anfitrion, no un Cortex-M4; sirve para ver la PROPORCION
         * y para que un dia que se dispare se vea aqui.
         */
        struct timespec t0, t1;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        ft8_decoder_process_slot();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        g_ms_dec = ((double)(t1.tv_sec - t0.tv_sec) * 1e3)
                 + ((double)(t1.tv_nsec - t0.tv_nsec) / 1e6);
    }

    g_n_salida = 0;
    while (ft8_decoder_get_message(&m)) {
        if (g_n_salida < MAX_SALIDA) {
            memcpy(g_salida[g_n_salida], m.line, FT8_DECODER_LINE_LEN);
        }
        g_n_salida++;
    }
    g_n_total = (int)ft8_decoder_get_total_count();
    g_n_cand  = ft8_decoder_get_last_num_candidates();
    return g_n_salida;
}

/* ¿Sale `texto` entre los mensajes decodificados? El renglon lleva columnas
 * separadas por tabulador y el mensaje es la quinta. */
static int salio(const char *texto)
{
    int i;
    for (i = 0; i < g_n_salida && i < MAX_SALIDA; i++) {
        const char *p = g_salida[i];
        int tabs = 0;
        while (*p != '\0' && tabs < 4) { if (*p == '\t') { tabs++; } p++; }
        if (tabs < 4) { continue; }
        if (strncmp(p, texto, strlen(texto)) == 0) { return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------- las pruebas */
#define TXT "CQ EA4XYZ IN80"

static int g_fallos;

static void prueba_1_ida_y_vuelta(void)
{
    uint8_t tonos[FT8_NN];

    printf("\n  1. IDA Y VUELTA\n");
    if (!ft8_tonos(TXT, tonos)) {
        printf("    el codificador no sabe hacer \"%s\"            MAL\n", TXT);
        g_fallos++;
        return;
    }
    ranura_vacia(1u);
    mete_emision(800.0f, 0.5f, amplitud_para(0.0f), tonos);
    decodifica_ranura();
    printf("    una emision limpia en 800 Hz, %d candidato(s)\n", g_n_cand);
    printf("    el firmware la lee                                 %s\n",
           salio(TXT) ? "ok" : "MAL");
    if (!salio(TXT)) { g_fallos++; }
}

static void prueba_2_banda(void)
{
    uint8_t tonos[FT8_NN];
    int hz;
    int ultimo_ok = 0;

    printf("\n  2. LA BANDA QUE SE ESCUCHA DE VERDAD\n");
    printf("     una emision a +5 dB, barriendo el audio\n");
    (void)ft8_tonos(TXT, tonos);

    for (hz = 100; hz <= 3000; hz += 100) {
        int ok;
        ranura_vacia(7u);
        mete_emision((float)hz, 0.5f, amplitud_para(5.0f), tonos);
        decodifica_ranura();
        ok = salio(TXT);
        if (ok) { ultimo_ok = hz; }
        {
            float lo, hi;
            ft8_waterfall_get_db_range(&lo, &hi);
            printf("      %4d Hz   %-6s  cand %3d  dec %2d  dB %.0f..%.0f\n",
                   hz, ok ? "se lee" : "- - -", g_n_cand, g_n_total,
                   (double)lo, (double)hi);
        }
    }
    printf("     la ultima que se lee esta en %d Hz\n", ultimo_ok);
    printf("     la ventana declarada son %.0f Hz\n",
           ((float)FT8_ADAPTER_SAMPLE_RATE_HZ / (float)FT8_ADAPTER_NFFT)
           * (float)(FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_RAW_BIN_STEP));
}

static void prueba_3_rejilla(void)
{
    uint8_t tonos[FT8_NN];
    int paso;

    printf("\n  3. LA REJILLA DE MEDIO BIN\n");
    printf("     la misma emision movida dentro de un bin de 6,25 Hz,\n");
    printf("     al borde de la sensibilidad (8 semillas por punto)\n");
    (void)ft8_tonos(TXT, tonos);

    for (paso = 0; paso < 8; paso++) {
        float off = (float)paso * (6.25f / 8.0f);
        int s, aciertos = 0;
        for (s = 0; s < 8; s++) {
            ranura_vacia((uint32_t)(100 + s));
            mete_emision(800.0f + off, 0.5f, amplitud_para(-15.0f), tonos);
            decodifica_ranura();
            aciertos += salio(TXT) ? 1 : 0;
        }
        printf("      +%4.2f Hz   %d/8\n", (double)off, aciertos);
    }
}

static void prueba_4_sensibilidad(void)
{
    uint8_t tonos[FT8_NN];
    int db;

    printf("\n  4. SENSIBILIDAD, en el centro de la ventana (8 semillas)\n");
    (void)ft8_tonos(TXT, tonos);

    for (db = 0; db >= -24; db -= 3) {
        int s, aciertos = 0;
        for (s = 0; s < 8; s++) {
            ranura_vacia((uint32_t)(200 + s));
            mete_emision(800.0f, 0.5f, amplitud_para((float)db), tonos);
            decodifica_ranura();
            aciertos += salio(TXT) ? 1 : 0;
        }
        printf("      %+3d dB   %d/8\n", db, aciertos);
    }
}

static void prueba_5_banda_llena(void)
{
    static const char *k_txt[25] = {
        "CQ EA4XYZ IN80", "CQ DL1ABC JO31", "CQ F5TGY JN38",
        "CQ I2ABC JN45",  "CQ G0XYZ IO91",  "CQ SM5ABC JO89",
        "CQ OH2ABC KP20", "CQ LA1ABC JP20", "CQ PA3ABC JO22",
        "CQ ON4ABC JO21", "CQ OK1ABC JO70", "CQ SP5ABC KO02",
        "CQ YO3ABC KN34", "CQ LZ1ABC KN12", "CQ TA1ABC KN41",
        "CQ UA3ABC KO85", "CQ 4X1ABC KM72", "CQ EA8ABC IL18",
        "CQ CT1ABC IM58", "CQ IS0ABC JM49", "CQ 9A1ABC JN85",
        "CQ S51ABC JN76", "CQ HA5ABC JN97", "CQ YU1ABC KN04",
        "CQ SV1ABC KM18"
    };
    uint8_t tonos[FT8_NN];
    int i, leidos = 0;

    printf("\n  5. LA BANDA LLENA: 25 emisiones de 300 a 2.700 Hz\n");

    /*
     * CADA UNA EN SU SITIO Y NINGUNA CLAVADA EN LA REJILLA.
     *
     * Poner las veinticinco en multiplos de 100 Hz las deja a todas en un
     * bin PAR, que es el caso bueno (ver la prueba 3). En antena nadie
     * sintoniza en multiplos de 6,25 Hz, asi que aqui cada una lleva su
     * propio desplazamiento dentro del bin: eso es lo que de verdad llega.
     */
    ranura_vacia(31u);
    for (i = 0; i < 25; i++) {
        float hz = 300.0f + (float)i * 100.0f
                 + (6.25f / 7.0f) * (float)(i % 7);   /* 0 .. 5,36 Hz */
        float db = -4.0f - (float)(i % 7) * 2.0f;   /* de -4 a -16 dB */
        if (!ft8_tonos(k_txt[i], tonos)) { continue; }
        mete_emision(hz, 0.4f + 0.02f * (float)i, amplitud_para(db), tonos);
    }
    decodifica_ranura();

    for (i = 0; i < 25; i++) {
        float hz = 300.0f + (float)i * 100.0f;
        int ok = salio(k_txt[i]);
        leidos += ok;
        printf("      %4.0f Hz  %-16s %s\n", (double)hz, k_txt[i],
               ok ? "se lee" : "- - -");
    }
    printf("\n      candidatos ................ %d (tope %d)\n",
           g_n_cand, FT8_DECODER_MAX_CANDIDATES);
    printf("      mensajes decodificados .... %d\n", g_n_total);
    printf("      llegan a la cola .......... %d (tope %d)\n",
           g_n_salida, FT8_DECODER_MSG_QUEUE_SIZE);
    printf("      de las 25 puestas se leen . %d\n", leidos);
    printf("      y tardo ................... %.0f ms (el desvio aguanta %d,"
           " o sea %.2f s)\n",
           g_ms_dec, FT8_ADAPTER_LADO_BLOQUES,
           (double)FT8_ADAPTER_LADO_BLOQUES * 0.160);
    if (g_n_total > g_n_salida) {
        printf("      *** %d mensajes decodificados NO llegan a la pantalla\n",
               g_n_total - g_n_salida);
    }

    /*
     * Y LA MISMA RANURA CON TODO DENTRO DE LA VENTANA.
     *
     * La de arriba mide la VENTANA, y por eso la mitad de las emisiones cae
     * fuera y solo se decodifican trece. Esta mide LA COLA: las veintidos
     * apretadas en los 1.450 Hz que de verdad se escuchan, que es lo que da
     * una banda abierta de verdad. Sin ella, la prueba 7 estaria mirando una
     * cola con trece mensajes y un tope de cuarenta, o sea nada.
     */
    printf("\n      y las mismas 22 apretadas DENTRO de la ventana:\n");
    ranura_vacia(41u);
    for (i = 0; i < 22; i++) {
        float hz = 250.0f + (float)i * 55.0f + (6.25f / 5.0f) * (float)(i % 5);
        float db = -3.0f - (float)(i % 6) * 2.0f;
        if (!ft8_tonos(k_txt[i], tonos)) { continue; }
        mete_emision(hz, 0.3f + 0.03f * (float)i, amplitud_para(db), tonos);
    }
    decodifica_ranura();
    leidos = 0;
    for (i = 0; i < 22; i++) { leidos += salio(k_txt[i]) ? 1 : 0; }
    printf("      de 250 a 1.405 Hz: %d candidatos, %d decodificados,\n",
           g_n_cand, g_n_total);
    printf("      %d en la cola (tope %d), %d de las 22 leidas\n",
           g_n_salida, FT8_DECODER_MSG_QUEUE_SIZE, leidos);
}

/*
 * DOS MENSAJES DISTINTOS CON EL MISMO CRC DE CATORCE BITS.
 *
 * Ft8Lib reaprovecha el CRC como "hash" del mensaje y lo dice en su propio
 * comentario ("TODO: 14 bits only"). ft8_decoder.c lo usaba para quitar
 * repetidos, asi que dos mensajes distintos que cayeran en el mismo valor
 * se veian como uno y el segundo se tiraba.
 *
 * Estos dos lo hacen. No son rebuscados -son dos CQ corrientes- y los
 * encontro un barrido de treinta mil mensajes: hay colisiones de sobra, que
 * es justo lo que dice la aritmetica de 16.384 valores.
 *
 * Desde el 23/10/2026 la huella sale de los 77 bits de la carga, asi que
 * los dos tienen que salir. Si alguien vuelve a mirar el CRC, esto se pone
 * rojo en vez de perderse una estacion de vez en cuando en antena.
 */
#define COL_A "CQ EA4ZZ KM72"
#define COL_B "CQ DL5AA KN41"

static void prueba_6_colision(void)
{
    uint8_t ta[FT8_NN], tb[FT8_NN];
    int a, b;

    printf("\n  6. DOS MENSAJES CON EL MISMO CRC DE 14 BITS\n");
    if (!ft8_tonos(COL_A, ta) || !ft8_tonos(COL_B, tb)) {
        printf("    no puedo codificar la pareja                     MAL\n");
        g_fallos++;
        return;
    }
    ranura_vacia(55u);
    mete_emision(600.0f, 0.5f, amplitud_para(0.0f), ta);
    mete_emision(1000.0f, 0.5f, amplitud_para(0.0f), tb);
    decodifica_ranura();
    a = salio(COL_A);
    b = salio(COL_B);
    printf("    \"%s\" en  600 Hz   %s\n", COL_A, a ? "ok" : "MAL");
    printf("    \"%s\" en 1000 Hz   %s\n", COL_B, b ? "ok" : "MAL");
    if (!a || !b) {
        printf("\n    los dos tienen el mismo CRC de 14 bits: si el quitado de\n");
        printf("    repetidos mira ESE numero en vez de los 77 bits del\n");
        printf("    mensaje, el segundo se tira creyendo que es el primero.\n");
        g_fallos++;
    }
}

/*
 * Y QUE NO SE QUEDE NINGUNO EN LA COLA. Lo que el decodificador cuenta
 * (ft8_decoder_get_total_count()) y lo que llega a la pantalla tienen que
 * ser el mismo numero: si la cola es mas corta que lo que una ranura
 * decodifica, queue_push() suma al contador y NO guarda el renglon, y eso
 * desde fuera se ve como "el total sube y la lista no".
 */
static void prueba_7_cola(void)
{
    printf("\n  7. NADA SE QUEDA EN LA COLA\n");
    printf("    decodificados %d, en la cola %d, tope %d            %s\n",
           g_n_total, g_n_salida, FT8_DECODER_MSG_QUEUE_SIZE,
           (g_n_total == g_n_salida) ? "ok" : "MAL");
    if (g_n_total != g_n_salida) { g_fallos++; }
}

/*
 * ======================================================================
 * EL ORDEN DE LOS SUBBLOQUES CUANDO EL BUCLE SE QUEDA ATRAS
 * ======================================================================
 *
 * El remuestreador entrega el audio en DOS buffers que la interrupcion va
 * llenando alternando, y ft8_decimator_poll() los vacia. Con uno lleno da
 * igual en que orden se miren; con los DOS llenos -que es el caso que ese
 * bucle dice cubrir- el orden es el de la emision, y mirarlos por indice
 * (0 y luego 1) mete el nuevo delante del viejo una vez de cada dos.
 *
 * COMO SE MIDE SIN MIRAR BANDERAS: se alimenta una escalera de tonos, uno
 * por subbloque y cada uno mas alto que el anterior, y se vacia solo cada
 * DOS subbloques para que siempre haya dos llenos. Despues se mira en que
 * bin tiene el maximo cada bloque de mag[]: si los subbloques han entrado
 * en orden, esos bins SUBEN. Si dos se cambian de sitio, bajan.
 *
 * El bloque 0 no cuenta: su ventana de analisis arranca con la mitad a
 * cero (ver ft8_waterfall_reset()), asi que su maximo no significa nada.
 */
static void prueba_8_orden(void)
{
    static int16_t trozo[1920];
    int k, mal = 0, ant = -1;

    printf("\n  8. EL ORDEN DE LOS SUBBLOQUES CON EL BUCLE ATRASADO\n");

    ft8_fft_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();

    for (k = 0; k < 12; k++) {
        double fase = 0.0;
        double f = 300.0 + 100.0 * (double)k;
        int j;

        for (j = 0; j < 1920; j++) {
            fase += 6.283185307179586 * f / (double)FS;
            trozo[j] = (int16_t)(8000.0 * sin(fase));
        }
        ft8_decimator_feed_block(trozo, 1920U);
        /*
         * SE VACIA EN LOS PARES, Y EL PRIMERO CUENTA.
         *
         * Vaciando en los impares (dos, dos, dos...) el buffer que se llena
         * antes es SIEMPRE el 0, que es el que un bucle por indice mira
         * primero: el orden sale bien por casualidad y la prueba no mide
         * nada. Con el primer vaciado en el subbloque 0, a partir de ahi la
         * pareja que queda llena es (1 viejo, 0 nuevo) - o sea justo al
         * reves que el indice.
         */
        if ((k & 1) == 0) { ft8_decimator_poll(); }
    }
    ft8_decimator_poll();   /* y el ultimo, que si no se queda sin volcar */

    for (k = 2; k < 12; k++) {
        /* Por celdas y no por bytes: desde el 28/10/2026 mag[] guarda dos
         * celdas en cada byte (ver FT8_ADAPTER_MAG_BITS). Leerlo a pelo
         * mezclaba dos bins en uno y esta prueba fallaba sola. */
        const ftx_waterfall_t *wf = ft8_waterfall_get();
        int fila = k * wf->block_stride;
        int b, mejor = 0;

        for (b = 1; b < FT8_ADAPTER_NUM_BINS; b++) {
            if (WF_MAG_INT_AT(wf, fila + b) > WF_MAG_INT_AT(wf, fila + mejor)) { mejor = b; }
        }
        printf("      bloque %2d  pico en el bin %3d (%4.0f Hz)%s\n",
               k, mejor, (double)mejor * 6.25,
               (mejor <= ant) ? "   <-- va hacia atras" : "");
        if (mejor <= ant) { mal++; }
        ant = mejor;
    }

    printf("    los subbloques entran en orden                     %s\n",
           mal ? "MAL" : "ok");
    if (mal) {
        printf("\n    con los dos buffers llenos hay que vaciar primero el que\n");
        printf("    lleva mas tiempo lleno, que es el que la interrupcion\n");
        printf("    esta escribiendo ahora - no el del indice 0.\n");
        g_fallos++;
    }
}

/*
 * ======================================================================
 * 9. UN GOLPE FUERTE EN MEDIO DE LA RANURA
 * ======================================================================
 *
 * El remuestreador lleva su propio automatico de ganancia delante de la
 * FFT -ver FT8_AGC_TARGET en ft8_decimator.c- y es de ATAQUE INSTANTANEO
 * con caida de 1,67 s: un solo pico fuerte le baja la ganancia de golpe y
 * tarda mas de un segundo y medio en volver.
 *
 * En onda corta eso no es raro: un chasquido atmosferico, una estacion
 * local que arranca, un motor. Si la ganancia se desploma en mitad de la
 * ranura, los diez segundos que quedan entran atenuados y las emisiones
 * flojas se van por debajo del suelo. Esto lo mide en vez de suponerlo:
 * la misma ranura, con y sin un golpe de 20 dB a mitad.
 */
static void prueba_9_golpe(void)
{
    static const char *k_txt[] = {
        "CQ EA1ABC IN52", "CQ EA2ABC IN83", "CQ EA3ABC JN01",
        "CQ EA4ABC IN80",
    };
    int i, sin_golpe, con_golpe;

    printf("\n  9. UN GOLPE FUERTE EN MEDIO DE LA RANURA\n");

    for (i = 0; i < 2; i++) {
        int k, salen = 0;

        ranura_vacia(77u);
        for (k = 0; k < 4; k++) {
            uint8_t tonos[FT8_NN];

            if (!ft8_tonos(k_txt[k], tonos)) { continue; }
            mete_emision(500.0f + 150.0f * (float)k, 0.5f,
                         amplitud_para(-6.0f), tonos);
        }
        if (i == 1) {
            /* El golpe: 20 ms a veinte decibelios por encima del ruido,
             * en el segundo 5 de la ranura. */
            int n0 = 5 * FS;

            for (k = 0; k < FS / 50; k++) {
                if ((n0 + k) < RANURA_N) {
                    g_audio[n0 + k] += 10.0f * RUIDO_SIGMA
                                       * sinf(2.0f * 3.14159265f * 900.0f
                                              * (float)k / (float)FS);
                }
            }
        }
        decodifica_ranura();
        for (k = 0; k < 4; k++) { if (salio(k_txt[k])) { salen++; } }
        if (i == 0) { sin_golpe = salen; } else { con_golpe = salen; }
    }

    printf("    sin golpe %d de 4,  con golpe %d de 4\n",
           sin_golpe, con_golpe);
    /*
     * Lo que se exige no es que no cueste NADA -un golpe de verdad tapa
     * audio de verdad- sino que no se lleve la ranura por delante.
     */
    if (con_golpe >= sin_golpe - 1) {
        printf("    el golpe no se lleva la ranura                     ok\n");
    } else {
        printf("    el golpe se lleva la ranura                        MAL\n");
        g_fallos++;
    }
}

int main(void)
{
    printf("\nFT8 DE PUNTA A PUNTA: CUANTO ENTRA Y CUANTO SALE\n");
    printf("  ventana %.0f Hz, %d bins, FREQ_OSR %d, NFFT %d, fs %d\n",
           ((float)FT8_ADAPTER_SAMPLE_RATE_HZ / (float)FT8_ADAPTER_NFFT)
           * (float)(FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_RAW_BIN_STEP),
           FT8_ADAPTER_NUM_BINS, FT8_ADAPTER_FREQ_OSR,
           FT8_ADAPTER_NFFT, FT8_ADAPTER_SAMPLE_RATE_HZ);

    prueba_1_ida_y_vuelta();
    if (g_fallos) {
        printf("\n  el codificador del banco no cuadra con el decodificador\n");
        printf("  del firmware: no tiene sentido seguir midiendo.\n\n");
        return 1;
    }
    prueba_2_banda();
    prueba_3_rejilla();
    prueba_4_sensibilidad();
    prueba_5_banda_llena();
    prueba_7_cola();      /* mira lo que dejo la 5, asi que va detras */
    prueba_6_colision();
    prueba_8_orden();
    prueba_9_golpe();

    printf("\n");
    if (g_fallos > 0) {
        printf("  %d FALLO(S)\n\n", g_fallos);
        return 1;
    }
    printf("  TODO CORRECTO\n\n");
    return 0;
}

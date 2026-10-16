/*
 * LA POLARIDAD GLOBAL, PROBADA CON UNA RAFAGA SINTETICA.
 *
 * POR QUE EXISTE (25/09/2026). Esta es la unica pieza de HFDL que NO viene
 * del proyecto padre: la escribi yo, porque en su codigo el
 * `polarity_bitmask` se consume en tres sitios y no lo calcula nadie - su
 * propia cabecera lo deja dicho ("needs its own design work, not a direct
 * port"). Ver el comentario de mide_polaridad() en hfdl_modo.c.
 *
 * Y justamente por eso hay que probarla mas que al resto: lo suyo lo
 * validan sus autopruebas contra dumphfdl; esto no lo valida nadie.
 *
 * COMO. Se fabrica una rafaga con las secuencias de verdad -las A, la M1 del
 * giro que se elija y las nueve ventanas de entrenamiento- y se mete por
 * hfdl_modo_simbolo(), que es POR DONDE PASA lo de la radio. Luego se
 * pregunta que polaridad salio. La rafaga se manda dos veces: tal cual, y
 * con TODO invertido, que es exactamente lo que hace una BPSK cogida por el
 * otro extremo. Tienen que salir bits distintos.
 *
 * LO QUE NO PRUEBA: que el mensaje decodificado sea correcto. Para eso hace
 * falta una captura real de HFDL, que no tenemos. Esto prueba que el bit se
 * decide, que se decide bien, y que distingue los dos casos.
 */
#include <stdio.h>
#include <stdint.h>
#include "hfdl_modo.h"
#include "hfdl_ram.h"
#include "ft8_shared_ram.h"
#include "hfdl_preamble_seq.h"
#include "hfdl_preamble_sync.h"
#include "hfdl_data_segment.h"

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-44s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

static uint32_t sem = 0xC0FFEEu;
static float ruido(void)
{ sem = sem*1103515245u + 12345u; return ((sem >> 16) & 1u) ? 1.0f : -1.0f; }

/*
 * Mete una rafaga entera. `signo` es +1 (tal cual) o -1 (todo invertido).
 * Devuelve la polaridad que decidio el firmware, o -1 si no llego a
 * decidirla.
 */
static int rafaga_n(float signo, uint32_t giro, uint32_t ventanas);

static int rafaga(float signo, uint32_t giro)
{
    return rafaga_n(signo, giro, 72u);
}

static int rafaga_n(float signo, uint32_t giro, uint32_t ventanas)
{
    float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    uint32_t i, v;

    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(giro, m1);

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { printf("    (no se pudo arrancar el modo)\n"); return -1; }

    /* Un poco de silencio delante, como en el aire. */
    for (i = 0; i < 40u; i++) { hfdl_modo_simbolo(signo * ruido(), 0.0f, signo * ruido(), 0.0f); }

    /* A1, A2, M1: el preambulo. */
    for (i = 0; i < HFDL_A_LEN; i++)  { hfdl_modo_simbolo(signo * a[i], 0.0f, signo * a[i], 0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { hfdl_modo_simbolo(signo * a[i], 0.0f, signo * a[i], 0.0f); }
    for (i = 0; i < HFDL_M1_LEN; i++) { hfdl_modo_simbolo(signo * m1[i], 0.0f, signo * m1[i], 0.0f); }

    /*
     * M2 (15 que se tiran), entrenamiento (9 x 15) y datos, todo seguido y
     * con la cuenta exacta - ver abajo. El entrenamiento lleva la secuencia
     * de verdad porque es lo que mira la correlacion de la polaridad; el
     * resto puede ser cualquier cosa.
     */
    for (i = 0; i < HFDL_M2_LEN; i++) {
        float r = ruido();
        hfdl_modo_simbolo(signo * r, 0.0f, signo * r, 0.0f);
    }
    /*
     * Entrenamiento: nueve ventanas de quince. Esto es lo que mira la
     * correlacion, y va con la secuencia de verdad.
     *
     * UNA VEZ Y NO DOS - 14/10/2026. Aqui habia este mismo bucle
     * DUPLICADO: 270 simbolos donde la maquina de estados espera 135. Los
     * 135 sobrantes se consumian como datos de las tres primeras tramas,
     * asi que noventa de los simbolos que el banco contaba como "datos"
     * eran secuencia de entrenamiento. La cuenta final salia bien por
     * casualidad y el banco salia verde midiendo una rafaga que no existe.
     *
     * Es exactamente el fallo que describe el comentario de aqui abajo, en
     * su segunda version: el banco no miente sobre el codigo, miente sobre
     * lo que esta midiendo.
     */
    for (v = 0; v < 9u; v++) {
        for (i = 0; i < HFDL_T_LEN; i++) {
            hfdl_modo_simbolo(signo * HFDL_T_SEQ[0][i], 0.0f,
                              signo * HFDL_T_SEQ[0][i], 0.0f);
        }
    }

    /*
     * Y la rafaga ENTERA, con su cuenta exacta: despues del preambulo van
     * 150 simbolos (15 de M2 mas 135 de entrenamiento) y luego 45 por cada
     * ventana de datos (30 de datos mas 15 de entrenamiento). Para el giro
     * 3, 150 + 72*45 = 3.390.
     *
     * EXACTOS Y NI UNO MAS, a proposito. La primera version metia 20.000 y
     * la cuenta salia 930 en vez de 2.160: al cerrarse la rafaga el
     * sincronismo vuelve a buscar preambulo, REENGANCHA con el ruido que
     * viene detras -que enganche en falso con ruido es cosa medida, un 44%
     * de las veces- y al reenganchar se pone el contador a cero. O sea que
     * yo estaba midiendo los simbolos de una segunda rafaga inventada.
     *
     * El banco no mentia sobre el codigo: mentia sobre lo que estaba
     * midiendo, que es la version mas dificil de ver.
     */
    for (i = 0; i < ventanas * 45u; i++) {
        float r = ruido();
        hfdl_modo_simbolo(signo * r, 0.0f, signo * r, 0.0f);
    }

    if (!hfdl_modo_polaridad_lista()) { return -1; }
    return (int)hfdl_modo_polaridad();
}

/*
 * CUANTOS SIMBOLOS LE LLEGAN AL DECODIFICADOR.
 *
 * POR QUE EXISTE (25/09/2026). Porque un desfase de UN simbolo tuvo la radio
 * sacando basura con el preambulo enganchado y las rafagas completas, y yo
 * no tenia forma de verlo: la pantalla decia "mal" igual con un simbolo de
 * desfase que con cien.
 *
 * La maquina de estados del segmento cambia de fase EN EL ULTIMO SIMBOLO de
 * cada ventana, asi que el simbolo recien metido pertenece al estado de
 * ANTES. Yo lo leia bien para el entrenamiento y mal para los datos: se
 * colaba el ultimo simbolo de entrenamiento y se perdia el ultimo de datos.
 *
 * Y LA CUENTA LO DICE SOLA, sin necesidad de entender nada: una rafaga tiene
 * data_segment_cnt ventanas de 30 simbolos de datos, ni uno mas ni uno
 * menos. Si el numero no es exacto, hay un desfase. Es el tipo de
 * comprobacion que vale mas que una hora de leer codigo.
 */
static void cuenta_simbolos(uint32_t esperados)
{
    uint32_t vistos = hfdl_modo_simbolos_datos();

    printf("\n  simbolos de datos que recibe el decodificador:\n");
    printf("    %u   (deberian ser %u)\n", (unsigned)vistos, (unsigned)esperados);
    exige(vistos == esperados, "ni un simbolo de mas ni de menos");
    if (vistos != esperados) {
        printf("    %+d de diferencia: hay un desfase en la clasificacion\n",
               (int)vistos - (int)esperados);
    }
}

int main(void)
{
    int p_directa, p_invertida;

    printf("  la polaridad global, con una rafaga sintetica:\n\n");

    p_directa   = rafaga(+1.0f, 3u);
    p_invertida = rafaga(-1.0f, 3u);

    printf("    rafaga tal cual  -> polaridad %d\n", p_directa);
    printf("    rafaga invertida -> polaridad %d\n", p_invertida);
    printf("\n");

    exige(p_directa >= 0,   "la rafaga tal cual llega a datos");
    exige(p_invertida >= 0, "la rafaga invertida llega a datos");

    /*
     * ESTA COMPROBACION HA CAMBIADO DE SENTIDO DOS VECES, Y LA SEGUNDA ES
     * LA BUENA. 14/10/2026.
     *
     * Version 1: "tal cual da 0, invertida da 1". Correcta.
     *
     * Version 2 (25/09): con el ecualizador puesto empezo a fallar, y
     * decidi que la equivocada era la comprobacion. El razonamiento que
     * escribi era, resumido, "el LMS empuja su salida HACIA la referencia,
     * asi que el ecualizador no conserva la ambiguedad: LA RESUELVE, y eso
     * es justo lo que hace falta". Y cambie el banco para EXIGIR que las
     * dos rafagas dieran 0.
     *
     * El diagnostico de la primera mitad era exacto -el LMS si tapa la
     * medida- y la conclusion, desastrosa. Porque si la correlacion se mide
     * a la salida del LMS, NO MIDE NADA: ni la polaridad ni, cuando hizo
     * falta, el giro de la constelacion. Y lo que hizo este banco fue
     * convertir ese cegamiento en un requisito, de modo que arreglarlo
     * habria salido ROJO.
     *
     * Lo que costo: con 192 tramas en antena, BPSK 58/147 y 4-PSK y 8-PSK
     * CERO de cuarenta y cinco. El giro absoluto que se entrego por la
     * mañana devolvia cero siempre y nadie podia verlo, porque el unico
     * banco que recorre este camino exigia precisamente eso.
     *
     * Version 3: la medida vuelve a tomarse sobre el simbolo CRUDO de
     * Costas -ver mide_polaridad() en hfdl_modo.c- y la exigencia vuelve a
     * ser la de la version 1, que era la correcta: las dos rafagas tienen
     * que DISTINGUIRSE.
     *
     * La leccion no es "me equivoque al cambiarlo". Es que cuando un banco
     * empieza a fallar, cambiarle la exigencia para que pase es la unica
     * salida que no deja rastro de que algo iba mal.
     */
    exige(p_directa == 0,
          "tal cual: la correlacion dice que viene del derecho");
    exige(p_invertida == 1,
          "invertida: la correlacion LO NOTA (antes salia 0: estaba ciega)");
    exige(p_directa != p_invertida,
          "y por tanto la medida distingue los dos casos");


    /* La rafaga del giro 3 - 1800 bps, una ranura - lleva 72 ventanas de
     * datos de 30 simbolos. El numero sale de HFDL_FRAME_PARAMS, no de
     * contar lo que salio: una cuenta que se compara consigo misma no
     * comprueba nada. */
    cuenta_simbolos(72u * 30u);

    /*
     * LA TRAMA QUE NO CABE, Y QUE SE CUENTE UNA SOLA VEZ.
     *
     * *** La radio marco "23>13>12>12 X336": trescientas treinta y seis
     * tramas sin sitio... de doce tramas. Un contador que se pasa de su
     * propio maximo esta mal, y no hacia falta medirlo para saberlo. ***
     *
     * AQUI SE USABA EL GIRO 6, Y DEJO DE VALER - 12/10/2026.
     *
     * Decia "el giro 6 pide 51.088 bytes y hay 49.824, asi que NO CABE".
     * Eso era verdad hasta el 11/10, cuando HFDL paso a pedir su sitio por
     * su nombre en la union y la union crecio de 54.432 a 60.304 bytes
     * justo para que el giro 6 entrara (ver ft8_shared_ram.h y
     * HFDL_RAM_GIRO6 en hfdl_ram.h: el margen sobre el giro 6 es CERO a
     * proposito). Desde entonces el giro 6 CABE, y este bloque estaba
     * exigiendo que no cupiera.
     *
     * Y lo peor no es que estuviera mal: es que seguia en verde. El
     * simulador tenia una copia vieja de la cabecera, asi que media un
     * mundo donde el giro 6 todavia no cabia. Un banco que mide una copia
     * miente, y este mintio durante un dia entero.
     *
     * Se cambia al GIRO 7, que es el que no cabe y no cabra: pide 76.288 +
     * 9.216 = 85.504, mas de lo que hay libre en TODA la radio. Ese no
     * depende de como se reparta la union, asi que esta prueba no se
     * volvera a quedar vieja por ensanchar nada.
     */
    {
        uint32_t antes = hfdl_modo_no_cupo(), d, d7;
        uint32_t antes7 = hfdl_modo_no_cupo_7();

        (void)rafaga_n(+1.0f, 7u, 168u);
        d  = hfdl_modo_no_cupo() - antes;
        d7 = hfdl_modo_no_cupo_7() - antes7;
        printf("\n  una trama que no cabe (giro 7, ranura doble):\n");
        printf("    contada %u vez/veces   (tiene que ser 1)\n", (unsigned)d);
        exige(d == 1u, "se cuenta UNA vez, no una por ventana");
        exige(d7 == 1u, "y se cuenta aparte, como el giro que no se arregla");
        exige(hfdl_modo_simbolos_datos() == 0u,
              "y no se le mete un solo simbolo al decodificador");
    }

    /*
     * Y EL GIRO 6 SI CABE, que es la otra mitad de lo mismo. Sin esto, el
     * dia que alguien estreche la union nadie se entera: el giro 7 seguiria
     * sin caber -como siempre- y el banco seguiria en verde mientras el 6
     * se cae.
     */
    {
        /* En ABSOLUTO, no por diferencia: rafaga_n() rearranca el modo y
         * eso pone el contador a cero (ver hfdl_modo_start). Restar contra
         * lo de antes daba -1 y se leia como 4.294.967.295. */
        uint32_t d;

        (void)rafaga_n(+1.0f, 6u, 168u);
        d = hfdl_modo_no_cupo();
        printf("\n  y el giro 6 CABE (margen cero, a proposito):\n");
        printf("    no cupo %u vez/veces   (tiene que ser 0)\n", (unsigned)d);
        exige(d == 0u, "el giro 6 entra en la RAM prestada");
        printf("    hay %u bytes y el giro 6 pide %u\n",
               (unsigned)(sizeof ((ft8_shared_ram_t *)0)->hfdl_buf)
                 - (unsigned)HFDL_RAM_CABECERA,
               (unsigned)HFDL_RAM_GIRO6);
        exige((uint32_t)sizeof ((ft8_shared_ram_t *)0)->hfdl_buf
                  - (uint32_t)HFDL_RAM_CABECERA >= (uint32_t)HFDL_RAM_GIRO6,
              "y el sitio reservado le llega, medido contra la union");
    }

    hfdl_modo_stop();
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

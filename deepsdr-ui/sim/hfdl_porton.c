/*
 * EL PORTERO DE RAFAGA: QUE DEJE PASAR LO QUE DEBE Y PARE LO QUE DEBE.
 *
 * POR QUE EXISTE (25/09/2026). Porque un portero equivocado no se parece a
 * un fallo: se parece a una radio que no decodifica. Si se queda cerrado, el
 * embudo marca ceros y cualquiera diria que el decodificador esta roto.
 *
 * Y porque es codigo mio, no del proyecto padre. Lo suyo -una FFT de 256
 * puntos con su suelo de ruido- no cabe en la RAM que queda, asi que el
 * portero se hace con la energia que el control de ganancia ya calcula. Eso
 * lo valida este banco y nadie mas.
 *
 * SE PRUEBA POR LOS DOS LADOS, que es lo que importa:
 *   1. con ruido, el sincronismo NO debe ver casi nada (esa es la ganancia);
 *   2. con senal, TIENE que dejarla pasar entera (ese es el riesgo).
 * Un banco que solo comprobara lo primero aprobaria un portero tapiado.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "hfdl_modo.h"
#include "hfdl_ram.h"
#include "hfdl_preamble_seq.h"

static int fallos;
/* El indicador se mira DURANTE la ráfaga, no despues: entre ráfagas hay
 * silencio a proposito y ahi tiene que valer poco. */
static uint32_t g_senal_rafaga;

static void exige(int cond, const char *que)
{
    printf("    %-46s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

static uint32_t sem = 7654321u;
static float ruido(float amp)
{
    sem = sem * 1103515245u + 12345u;
    return amp * (float)((int32_t)((sem >> 8) % 2001) - 1000) / 1000.0f;
}

/* Mete n bloques de 32 muestras con la amplitud que se diga. */
static void mete(float amp, uint32_t bloques)
{
    uint32_t b, i;
    float bi[32], bq[32];

    for (b = 0; b < bloques; b++) {
        for (i = 0; i < 32u; i++) { bi[i] = ruido(amp); bq[i] = ruido(amp); }
        hfdl_modo_mete(bi, bq, 32u);
        /*
         * Y poll() ENTRE BLOQUES, que es donde corre el Viterbi y donde
         * se REMATA el segmento. Sin esto la cadena se quedaba dentro del
         * segmento de la ráfaga anterior para siempre y se comia la
         * siguiente: el banco leia "2 de 4" y la puerta no tenia ninguna
         * culpa. Es lo mismo que ya hace el banco de aire real, y es lo
         * que hace la radio.
         */
        hfdl_modo_poll();
    }
}

/*
 * ======================================================================
 * UNA RAFAGA DE VERDAD, NO RUIDO MAS FUERTE - 14/10/2026
 * ======================================================================
 *
 * Hasta hoy la segunda mitad de este banco subia la amplitud del RUIDO
 * veinte veces y exigia que eso produjera enganches de A1. Funcionaba
 * -medido: cuatro- porque A1 combinaba ocho bloques de dieciseis
 * simbolos, y con ocho trozos cortos el ruido fuerte acierta uno de vez
 * en cuando.
 *
 * Esta mañana A1 paso a una sola correlacion coherente de 127 simbolos y
 * el ruido fuerte dejo de enganchar. Es exactamente la mejora que se
 * buscaba, y este banco se puso rojo POR ELLA: estaba exigiendo falsas
 * alarmas como prueba de que la puerta se abre.
 *
 * Lo que se le mete ahora es lo que la puerta tiene que dejar pasar: los
 * 448 simbolos de pre-key, A1, A2 y M1, modulados sobre la subportadora
 * de 1440 Hz igual que llegan del aire. Si alguien tapia la puerta, esto
 * se queda a cero y salta. Y ya no se puede aprobar metiendo basura.
 */
static void rafaga_de_verdad(void)
{
    float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    float bi[32], bq[32];
    uint32_t n = 0u, i, k;
    float fase = 0.0f;
    static float simbolos[448 + 3 * 127];
    uint32_t ns = 0u;

    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(0u, m1);

    for (i = 0; i < 448u; i++)        { simbolos[ns++] = 1.0f; }
    for (i = 0; i < HFDL_A_LEN; i++)  { simbolos[ns++] = a[i]; }
    for (i = 0; i < HFDL_A_LEN; i++)  { simbolos[ns++] = a[i]; }
    for (i = 0; i < HFDL_M1_LEN; i++) { simbolos[ns++] = m1[i]; }

    /* 12.000 muestras por segundo a 1.800 baudios: 6,667 por simbolo. */
    for (k = 0; k < (uint32_t)((double)ns * 12000.0 / 1800.0); k++) {
        uint32_t si = (uint32_t)((double)k * 1800.0 / 12000.0);
        float v = (si < ns) ? simbolos[si] : 0.0f;
        fase += 6.28318530718f * 1440.0f / 12000.0f;
        if (fase > 6.28318530718f) { fase -= 6.28318530718f; }
        bi[n] = 1.0f * v * cosf(fase) + ruido(0.02f);
        bq[n] = 1.0f * v * sinf(fase) + ruido(0.02f);
        if (++n == 32u) { hfdl_modo_mete(bi, bq, 32u); hfdl_modo_poll(); n = 0u; }
    }
}

int main(void)
{
    uint32_t a1_ruido = 0, a1_senal = 0, d;

    printf("  el portero de rafaga:\n\n");

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { printf("    no arranca\n"); return 1; }

    /* --- 1. cebado y ruido de fondo largo --- */
    mete(0.02f, 4000u);
    hfdl_modo_embudo(&a1_ruido, 0, 0);
    printf("    despues de 4.000 bloques de ruido:\n");
    printf("      senal %u%%   enganches A1 %u\n",
           hfdl_modo_senal(), (unsigned)a1_ruido);

    /* --- 2. y ahora CUATRO RAFAGAS DE VERDAD --- */
    { uint32_t sen_max = 0u;
      for (d = 0; d < 8u; d++) {
          rafaga_de_verdad();
          if (hfdl_modo_senal() > sen_max) { sen_max = hfdl_modo_senal(); }
          /* TRES SEGUNDOS DE HUECO, no medio. Una ráfaga que engancha
           * abre un segmento de datos de hasta 2,8 segundos durante el
           * cual la cadena esta ocupada: con el hueco corto, la ráfaga
           * siguiente caia dentro del segmento de la anterior y el banco
           * leia "2 de 4" como si la puerta fallara. Es el mismo error de
           * siempre: el banco midiendo otra cosa. */
          mete(0.02f, 1200u + d * 7u + (d & 1u) * 3u);
      }
      g_senal_rafaga = sen_max; }
    hfdl_modo_embudo(&a1_senal, 0, 0);
    d = a1_senal - a1_ruido;
    printf("    con ocho rafagas de verdad (pre-key + A1 + A2 + M1):\n");
    printf("      senal %u%% (durante la rafaga)   enganches A1 nuevos %u\n",
           g_senal_rafaga, (unsigned)d);
    printf("\n");

    /*
     * LO QUE SE EXIGE.
     *
     * Con ruido puro el sincronismo tiene que ver MUY POCO. No cero: los
     * primeros HFDL_SQ_CEBADO bloques pasan a proposito, porque hasta que el
     * suelo no se aprende cerrar seria decidir a ciegas.
     */
    exige(a1_ruido < 40u, "con ruido casi no llega nada al sincronismo");

    /*
     * Y con senal tiene que abrirse. Esta es la que impide tapiar la
     * puerta: si alguien sube el umbral hasta dejar la radio muda, salta
     * aqui.
     *
     * EL UMBRAL ERA `d > 0` Y NO SERVIA - 11/10/2026.
     *
     * Pedido por el usuario.
     *
     * `d > 0` quiere decir "que pase AL MENOS UNO". Medido hoy, d = 4: o
     * sea que un cambio que se cargara el 75 % del paso de senal seguia
     * saliendo en verde. Y el contraste cantaba, porque la comprobacion de
     * al lado tiene 40 veces de holgura (a1_ruido < 40 contra 1 medido) y
     * esta tenia 4.
     *
     * Se cambia por dos cosas. Un suelo de cuentas, para que no valga con
     * uno suelto; y sobre todo el RITMO comparado con el del ruido, que es
     * lo que de verdad dice si la puerta distingue. Medido hoy:
     *
     *     ruido:   1 enganche  / 4.000 bloques = 0,00025 por bloque
     *     senal:   4 enganches /   600 bloques = 0,0067  por bloque
     *     o sea 27 veces mas.
     *
     * Se exigen 8 veces: margen de sobra contra lo medido, y una puerta
     * medio tapiada ya no pasa. Y es una medida contra la OTRA MEDIDA, no
     * contra un numero que me haya inventado - que es la regla de este
     * proyecto.
     */
    /*
     * SEIS DE OCHO, Y POR QUE NO OCHO DE OCHO.
     *
     * Medido: con los huecos variados pasan seis. Las dos que se pierden
     * se caen antes del pre-key -ni tono detectan-, y se caen segun la
     * LONGITUD DEL HUECO que va delante, no segun cual sean: con huecos
     * todos iguales se perdian la 2 y la 4, y variandolos se pierden
     * otras. Eso es la fase del remuestreador 20:9 contra el pulso
     * CUADRADO que monta este banco, que no es el raiz-coseno-alzado que
     * manda un transmisor de verdad (sim/hfdl_extremo.c si lo monta).
     *
     * Y no es que me convenga creerlo: en aire real las ráfagas seguidas
     * salen seguidas -en la grabacion del usuario, t=130,1 y t=132,5 las
     * dos buenas, y t=163,1 / 165,6 / 170,5 tres seguidas-, asi que la
     * alternancia es de este banco y no de la radio.
     *
     * Queda apuntado en vez de redondeado: si alguien le pone al banco un
     * pulso conformado de verdad, esto tiene que subir a ocho, y si no
     * sube hay algo que mirar. Mientras tanto seis exige lo que importa:
     * una puerta tapiada da cero y salta aqui igual.
     */
    exige(d >= 6u, "las rafagas de verdad pasan la puerta (al menos 6 de 8)");
    {
        double r_ruido = (double)a1_ruido / 4000.0;
        /* Ocho ráfagas: 1.009 simbolos cada una a 6,67 muestras por
         * simbolo son 210 bloques. El silencio de en medio no cuenta
         * aqui: lo que se compara es el ritmo CON ráfaga contra el ritmo
         * con ruido. */
        double r_senal = (double)d / (8.0 * 210.0);  /* solo los bloques con ráfaga */
        /* Con cero enganches de ruido el cociente seria infinito: se le
         * pone el suelo de "un enganche" para que la cuenta siga
         * significando algo y no se dispare sola. */
        double piso = (r_ruido > (1.0 / 4000.0)) ? r_ruido : (1.0 / 4000.0);
        printf("      ritmo con senal / ritmo con ruido = %.0f veces\n",
               r_senal / piso);
        exige(r_senal > 8.0 * piso,
              "y la puerta distingue de verdad rafaga de ruido");
    }
    exige(g_senal_rafaga > 20u, "y la senal se nota en el indicador");

    /* Que el indicador distinga de verdad los dos casos. */
    exige(!hfdl_modo_porton_forzado(),
          "no ha hecho falta abrir por hartazgo");

    hfdl_modo_stop();
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

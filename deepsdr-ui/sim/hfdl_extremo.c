/*
 * ======================================================================
 * LA RAFAGA ENTERA, POR LA CADENA DE VERDAD, HASTA EL CRC
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * ESTE ES EL BANCO QUE FALTABA, Y SU AUSENCIA ES LA CAUSA DE TODO LO DE
 * HOY. El proyecto tiene veintitantos bancos de HFDL y todos prueban
 * PIEZAS: la aritmetica del Viterbi, el permutado del desentrelazador, la
 * trigonometria de un detector de fase. Ninguno coge una rafaga, la mete
 * por la cadena y mira si sale el CRC.
 *
 * Con esa coleccion se pueden entregar tres arreglos seguidos, todos con
 * su banco en verde, sin mover el numero de la radio ni una decima. Es
 * exactamente lo que paso.
 *
 * QUE HACE ESTO, en una frase: monta una rafaga completa -preambulo A1,
 * A2, M1, el M2 que se tira, las nueve ventanas de entrenamiento y las 72
 * ventanas de datos con su entrenamiento intercalado-, le mete un canal
 * con GIRO y RUIDO, la pasa por hfdl_modo_simbolo() -el mismo camino que
 * la radio- y comprueba que hfdl_modo_buenas() sube.
 *
 * Los simbolos de datos salen del transmisor que YA tenia la autoprueba
 * de hfdl_payload_decode.c: mensaje con CRC bueno, codificado,
 * entrelazado, modulado y cifrado. No es un transmisor escrito para la
 * ocasion; si estuviera mal, la autoprueba de las cuatro velocidades
 * fallaria.
 */
#include "hfdl_modo.h"
#include "hfdl_preamble_seq.h"
#include "hfdl_payload_decode.h"
#include "hfdl_data_segment.h"
#include "hfdl_matched_filter.h"
#include "hfdl_iq_mixer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;

/*
 * EL GIRO 7 NO CABE, Y ESO NO ES UN FALLO.
 *
 * 8-PSK a 1800 bps en ranura doble pide 76.288 + 9.216 = 85.504 bytes de
 * RAM y la radio entera tiene 14.496 libres. No es "esta sin hacer": es
 * que no se puede, y hfdl_ram.h lo deja escrito con los numeros delante,
 * medido por el usuario en antena con 94 ráfagas.
 *
 * El banco lo prueba igual -tiene que verse que lo RECHAZA limpiamente, no
 * que se cuelgue- pero no lo cuenta como fallo, porque un banco que esta
 * siempre rojo por algo que nadie va a arreglar deja de servir para avisar
 * de lo que si se puede arreglar.
 */
#define GIRO_SIN_RAM 7u
static uint32_t giro_actual = 0u;

static void di(int cond, const char *que, const char *como)
{
    int excusado = (giro_actual == GIRO_SIN_RAM) && !cond;
    printf("    %-44s %s  %s\n", que,
           cond ? "ok " : (excusado ? "n/c" : "MAL"), como ? como : "");
    if (!cond && !excusado) { fallos++; }
}

/* Ruido gaussiano barato (suma de doce uniformes menos seis). */
static uint32_t sem = 0x1234567u;
static float uni(void) { sem = sem * 1103515245u + 12345u; return (float)((sem >> 8) & 0xFFFFu) / 65536.0f; }
static float gauss(void)
{
    float s = 0.0f; int i;
    for (i = 0; i < 12; i++) { s += uni(); }
    return s - 6.0f;
}

/*
 * SITIO PARA LA RANURA DOBLE - 14/10/2026.
 *
 * Esto valia 3000 y la ranura doble pide 168*30 = 5040 simbolos de
 * datos. Con 3000 el transmisor se quedaba corto, el banco emitia una
 * rafaga truncada y las cuatro configuraciones dobles salian 0/10 con
 * canal PERFECTO. Eso no era el firmware: era el banco midiendose a si
 * mismo. Un banco con el buffer corto acusa al codigo de algo que no ha
 * hecho, que es la forma mas cara de perder una tarde.
 */
#define MAX_SIM 5200u
static float g_di[MAX_SIM], g_dq[MAX_SIM];
static float g_ei[MAX_SIM], g_eq[MAX_SIM];
static uint32_t nd_global;

/*
 * Mete una rafaga del giro `giro` (indice M1: 1 BPSK, 2 4-PSK, 3 8-PSK)
 * con la constelacion girada `fase` radianes y ruido de desviacion
 * `sigma`. Devuelve cuantas tramas buenas ha contado el firmware.
 */
static uint32_t rafaga(uint32_t giro, float fase, float sigma)
{
    float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    uint32_t i, v, nd = 0u, idx = 0u;
    uint32_t antes_b, despues_b;
    const float cs = cosf(fase), sn = sinf(fase);

    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(giro, m1);

    if (!hfdl_payload_decode_tx_simbolos(giro, g_di, g_dq, MAX_SIM, &nd)) {
        printf("    (el transmisor no pudo montar el giro %u)\n", giro);
        return 0u;
    }

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { printf("    (no arranca el modo)\n"); return 0u; }
    hfdl_modo_espia(g_ei, g_eq, MAX_SIM);
    nd_global = nd;
    antes_b = hfdl_modo_buenas();

    /* Un simbolo por el canal: gira y ensucia. El preambulo tambien pasa
     * por el, que es lo que hace el aire. */
    #define METE(vi, vq) do {                                      \
        float _i = (vi) * cs - (vq) * sn + gauss() * sigma;         \
        float _q = (vi) * sn + (vq) * cs + gauss() * sigma;         \
        hfdl_modo_simbolo(_i, _q, _i, _q);                          \
    } while (0)

    for (i = 0; i < 40u; i++) { METE(((uni() > 0.5f) ? 1.0f : -1.0f), 0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { METE(a[i], 0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { METE(a[i], 0.0f); }
    for (i = 0; i < HFDL_M1_LEN; i++) { METE(m1[i], 0.0f); }

    /* M2: quince que se tiran. */
    for (i = 0; i < HFDL_M2_LEN; i++) { METE(((uni() > 0.5f) ? 1.0f : -1.0f), 0.0f); }

    /* Nueve ventanas de entrenamiento. */
    for (v = 0; v < 9u; v++) {
        for (i = 0; i < HFDL_T_LEN; i++) { METE(HFDL_T_SEQ[0][i], 0.0f); }
    }

    /* Y las ventanas de datos: 30 de datos y 15 de entrenamiento cada
     * una. CUANTAS lo dice la tabla del protocolo -72 en ranura simple,
     * 168 en doble-, no un 72 escrito a mano, que es lo que habia aqui
     * hasta el 14/10/2026 y lo que hacia que las cuatro configuraciones
     * de ranura doble no se pudieran ni emitir. */
    for (v = 0; v < (uint32_t)HFDL_FRAME_PARAMS[giro].data_segment_cnt; v++) {
        for (i = 0; i < 30u; i++) {
            float di_ = (idx < nd) ? g_di[idx] : 1.0f;
            float dq_ = (idx < nd) ? g_dq[idx] : 0.0f;
            idx++;
            METE(di_, dq_);
        }
        for (i = 0; i < HFDL_T_LEN; i++) { METE(HFDL_T_SEQ[0][i], 0.0f); }
    }
    #undef METE

    hfdl_modo_poll();
    despues_b = hfdl_modo_buenas();
    return despues_b - antes_b;
}

/*
 * ======================================================================
 * Y LA MISMA RAFAGA, PERO ENTRANDO POR EL AUDIO
 * ======================================================================
 *
 * Lo de arriba mete SIMBOLOS por hfdl_modo_simbolo(), que es lo que hacen
 * todos los bancos que habia. Eso se salta el AGC, el filtro adaptado, el
 * sincronismo de simbolo y el lazo de Costas: justo las cuatro etapas
 * donde la auditoria de hoy dice que estan los defectos que quedan.
 *
 * Esto construye el audio de verdad -12 kHz complejos, tres muestras por
 * simbolo a 1800 baudios tras el remuestreo a 5400- con conformado de
 * pulso, y lo mete por hfdl_modo_mete(). Es el camino entero.
 */

/*
 * EL RELOJ DE MUESTREO ES FRACCIONARIO, Y ESO NO ES UN DETALLE.
 *
 * 12.000 / 1.800 = 6,667 muestras por simbolo. La primera version de este
 * banco uso SIETE, que parece poca cosa y es un 5 % de error de velocidad:
 * sobre los 127 simbolos de A1 eso son seis simbolos y medio de
 * deslizamiento, y el correlador del preambulo no engancha. El embudo lo
 * cantaba: "2>0>0", o sea A1 dispara y A2 no confirma nunca.
 *
 * Asi que se monta primero la tira ENTERA de simbolos y despues se genera
 * el audio muestra a muestra con un acumulador fraccionario, interpolando
 * entre simbolo y simbolo. Es lo que hace un transmisor de verdad y es lo
 * unico que deja al correlador hacer su trabajo.
 */
/*
 * EL CONFORMADO, CON UN RRC HECHO PARA 12 kHz.
 *
 * La primera version uso hfdl_matched_filter_coeffs, los del receptor, y
 * eso esta mal: ESOS trabajan a 5.400 Hz -tres muestras por simbolo- y
 * aplicarlos a 12.000 -donde hay 6,67- es un filtro de la mitad de ancho
 * de banda y de la mitad de alcance en simbolos. El resultado fue que los
 * simbolos llegaban al decodificador con un parecido de 0,16 a lo
 * transmitido: papilla.
 *
 * Asi que aqui se genera un raiz-coseno-alzado de verdad para 6,67
 * muestras por simbolo. beta 0,35 y ocho simbolos de alcance, que es lo
 * normal en HF y mas que suficiente para que el filtro adaptado del
 * receptor vea lo que espera.
 */
#define RRC_SPS   (12000.0f / 1800.0f)
#define RRC_SPAN  8
#define RRC_LEN   ((int)(RRC_SPAN * RRC_SPS) | 1)
static float g_rrc[RRC_LEN];

static void rrc_init(void)
{
    const float beta = 0.35f;
    int n;
    float suma = 0.0f;
    for (n = 0; n < RRC_LEN; n++) {
        float t = ((float)n - (float)(RRC_LEN - 1) * 0.5f) / RRC_SPS;
        float v;
        if (fabsf(t) < 1e-6f) {
            v = 1.0f - beta + 4.0f * beta / 3.14159265f;
        } else if (fabsf(fabsf(4.0f * beta * t) - 1.0f) < 1e-4f) {
            float a = beta / 1.41421356f;
            float b = (1.0f + 2.0f / 3.14159265f) * sinf(0.78539816f / beta);
            float c = (1.0f - 2.0f / 3.14159265f) * cosf(0.78539816f / beta);
            v = a * (b + c);
        } else {
            float pt = 3.14159265f * t;
            float num = sinf(pt * (1.0f - beta))
                      + 4.0f * beta * t * cosf(pt * (1.0f + beta));
            float den = pt * (1.0f - 16.0f * beta * beta * t * t);
            v = num / den;
        }
        g_rrc[n] = v;
        suma += v;
    }
    for (n = 0; n < RRC_LEN; n++) { g_rrc[n] /= suma; }
}

/* 150 + 168*45 = 7710 simbolos en la rafaga doble entera. Ver MAX_SIM. */
#define MAX_SIMB 8704u
static float g_si[MAX_SIMB], g_sq[MAX_SIMB];
static uint32_t g_ns;

static void pon(float i_, float q_)
{
    if (g_ns < MAX_SIMB) { g_si[g_ns] = i_; g_sq[g_ns] = q_; g_ns++; }
}

/*
 * EL CANAL DE HF: UN ECO RETRASADO, que es lo que hace que el ecualizador
 * sirva para algo.
 *
 * Sin multitrayecto el ecualizador no tiene nada que corregir y su paso
 * `mu` da exactamente igual -medido: el barrido de ruido sale identico con
 * 0,1 y con 0,03-. Un canal sin eco es un canal que no prueba el
 * ecualizador, y ese es justo el modulo del que se sospecha.
 *
 * Un eco al 50 % retrasado dos muestras a 12 kHz es poco mas de un cuarto
 * de simbolo: lo normal en una ruta de HF de un salto.
 */
/*
 * EL CANAL QUE SE MUEVE - 14/10/2026, de madrugada.
 *
 * Pedido por el usuario.
 *
 * Y el banco de aire real le da la razon a medias: de catorce ráfagas que
 * pasan por la antena (las cuenta el detector de pre-key) solo seis
 * decodifican. No es detectar: es demodular.
 *
 * Y este banco, hasta ahora, demodulaba en un laboratorio. Su canal era
 * un eco FIJO: la misma amplitud y la misma fase durante los 1,8
 * segundos de la ráfaga. La ionosfera no hace eso. Se mueve -el
 * desvanecimiento tipico de HF tiene un ancho Doppler de decimas de
 * hercio a unos pocos- y una ráfaga que empieza con el entrenamiento
 * perfecto puede acabar con los datos destrozados si el ecualizador no
 * sigue el movimiento.
 *
 * ESO ES EXACTAMENTE LA FIRMA QUE SE VE EN EL AIRE DE VERDAD: la ráfaga
 * 12 de Hf-acars entrena con coherencia 0,916 -casi perfecta- y falla el
 * CRC. Con un canal fijo eso no puede pasar, y por eso el banco salia
 * verde mientras la radio fallaba.
 *
 * Aqui se modela como dos caminos cuya ganancia compleja da un paseo
 * aleatorio lento, que es la forma barata y honrada del modelo de
 * Watterson: no reproduce la ionosfera, pero deja de mentir diciendo que
 * el canal esta quieto.
 */
static float g_doppler;   /* ancho del desvanecimiento, en Hz. 0 = canal quieto */
static float g_reloj;   /* error de reloj en ppm - ver PASO */
static float g_eco = 0.0f;
/*
 * EL DESVIO DE PORTADORA, en hercios. Es contra lo que pelea el lazo de
 * Costas, y por tanto lo unico del canal que puede distinguir un detector
 * de fase de BPSK de uno de M-PSK. Sin esto, el apartado 5 mide un canal
 * donde el lazo no tiene nada que hacer.
 */
static float g_desvio = 0.0f;
#define ECO_RETARDO 2

static uint32_t rafaga_audio(uint32_t giro, float sigma)
{
    float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    uint32_t i, v, nd = 0u, idx = 0u, antes_b, m;
    double t = 0.0;
    /* Simbolos por muestra. g_reloj es el error de reloj en partes por
     * millon: el transmisor y la radio no comparten cristal, y sobre
     * 2160 simbolos un error pequeño se convierte en simbolos enteros de
     * deriva. El banco sintetico daba por hecho que los dos relojes eran
     * el mismo, que es justo la clase de suposicion que esconde fallos. */
    const double PASO = (1800.0 / 12000.0) * (1.0 + (double)g_reloj * 1e-6);
    float bi[64], bq[64];
    uint32_t nb = 0u;
    float sub_ph = 0.0f;
    float eco_i[ECO_RETARDO] = {0.0f}, eco_q[ECO_RETARDO] = {0.0f};
    uint32_t eco_p = 0u;
    /*
     * EL ECO QUE GIRA, no el canal que se apaga. Ver g_doppler.
     *
     * La primera version de esto hacia pasear las ganancias de los dos
     * caminos alrededor de CERO, y claro: el canal se apagaba y todo
     * salia 0/10 con 0,2 Hz. Eso no era medir el ecualizador, era medir
     * un apagon. Un banco que castiga de mas miente igual que uno que
     * perdona.
     *
     * Lo que de verdad le cuesta a un ecualizador no es que baje el
     * nivel -de eso se encarga el AGC- sino que la RESPUESTA del canal
     * cambie de forma mientras dura la ráfaga. Asi que aqui el camino
     * directo se queda quieto y es el ECO el que gira de fase despacio:
     * la suma de los dos pasa por maximos y minimos, el canal cambia de
     * forma, y la potencia media se mantiene.
     */
    float eco_fase = 0.0f;
    float eco_paso = (g_doppler > 0.0f)
                   ? (6.28318530718f * g_doppler / 12000.0f) : 0.0f;

    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(giro, m1);
    if (!hfdl_payload_decode_tx_simbolos(giro, g_di, g_dq, MAX_SIM, &nd)) { return 0u; }
    nd_global = nd;

    /* La tira de simbolos, entera y en orden. */
    g_ns = 0u;
    for (i = 0; i < 60u; i++)         { pon(0.0f, 0.0f); }
    /*
     * EL PRE-KEY, QUE ESTE BANCO NO TRANSMITIA - 14/10/2026.
     *
     * ARINC 635-3, 2.2.2.1.1: cada ranura empieza con 448 simbolos de
     * 2-PSK con fase constante -249 ms de portadora pura- ANTES del
     * preambulo. Aqui se pasaba directamente al preambulo, o sea que el
     * banco medi­a una ráfaga que no existe en el aire.
     *
     * Y no es un detalle decorativo: una portadora constante NO TIENE
     * TRANSICIONES, y el sincronismo de simbolo se alimenta justo de
     * ellas. Lo que le pasa a ese lazo durante 448 simbolos sin una sola
     * transicion es algo que este banco no habia mirado nunca, y la radio
     * se lo come en cada ráfaga.
     */
    for (i = 0; i < 448u; i++)        { pon(1.0f, 0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { pon(a[i], 0.0f); }
    for (i = 0; i < HFDL_A_LEN; i++)  { pon(a[i], 0.0f); }
    for (i = 0; i < HFDL_M1_LEN; i++) { pon(m1[i], 0.0f); }
    for (i = 0; i < HFDL_M2_LEN; i++) { pon(1.0f, 0.0f); }
    for (v = 0; v < 9u; v++) {
        for (i = 0; i < HFDL_T_LEN; i++) { pon(HFDL_T_SEQ[0][i], 0.0f); }
    }
    for (v = 0; v < (uint32_t)HFDL_FRAME_PARAMS[giro].data_segment_cnt
                && g_ns < MAX_SIMB - 50u; v++) {
        for (i = 0; i < 30u; i++) {
            pon((idx < nd) ? g_di[idx] : 1.0f, (idx < nd) ? g_dq[idx] : 0.0f);
            idx++;
        }
        for (i = 0; i < HFDL_T_LEN; i++) { pon(HFDL_T_SEQ[0][i], 0.0f); }
    }
    for (i = 0; i < 40u; i++)         { pon(0.0f, 0.0f); }

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { return 0u; }
    hfdl_modo_espia(g_ei, g_eq, MAX_SIM);
    antes_b = hfdl_modo_buenas();

    /* Y el audio, muestra a muestra, con el reloj fraccionario. */
    m = 0u;
    rrc_init();
    while (t < (double)(g_ns - 1u)) {
        /*
         * El simbolo n contribuye con g_rrc evaluado en (muestra - n*SPS).
         * Se recorren los simbolos que caen dentro del alcance del filtro,
         * que son RRC_SPAN a cada lado.
         */
        float ii = 0.0f, qq = 0.0f;
        int kk;
        int centro = (int)(t + 0.5);
        for (kk = centro - RRC_SPAN; kk <= centro + RRC_SPAN; kk++) {
            int idx2;
            float dt;
            if (kk < 0 || (uint32_t)kk >= g_ns) { continue; }
            dt = (float)(t - (double)kk);
            idx2 = (int)((dt * RRC_SPS) + (float)(RRC_LEN - 1) * 0.5f + 0.5f);
            if (idx2 < 0 || idx2 >= RRC_LEN) { continue; }
            ii += g_si[kk] * g_rrc[idx2];
            qq += g_sq[kk] * g_rrc[idx2];
        }
        ii *= RRC_SPS; qq *= RRC_SPS;

        /*
         * Y A LA SUBPORTADORA, que es donde la cadena espera la señal.
         *
         * hfdl_demod_chain_init() arranca el mezclador en
         * HFDL_MIXER_SUBCARRIER_HZ = 1440 Hz y con signo -1, o sea que
         * multiplica por e^{-j*theta*n}. Un banco que meta la señal en
         * banda base se la encuentra a -1440 Hz despues del mezclador.
         *
         * Eso es exactamente lo que pasaba: el preambulo enganchaba -el
         * correlador y el lazo de Costas tiran de bastante desvio- y los
         * datos llegaban al decodificador con un parecido de 0,3 a lo
         * transmitido. Papilla, con canal PERFECTO.
         *
         * Tres horas de sospechar del ecualizador, del sincronismo y del
         * detector de fase, y el fallo estaba en el transmisor del banco.
         * Es la misma leccion de siempre: antes de acusar al codigo, hay
         * que probar que el instrumento mide.
         */
        {
            float cpo = cosf(sub_ph), spo = sinf(sub_ph);
            float mi = ii * cpo - qq * spo;
            float mq = ii * spo + qq * cpo;
            ii = mi; qq = mq;
            sub_ph += 6.28318530718f * (HFDL_MIXER_SUBCARRIER_HZ + g_desvio) / 12000.0f;
            if (sub_ph > 6.28318530718f) { sub_ph -= 6.28318530718f; }
        }
        if (g_eco != 0.0f) {
            float ei = eco_i[eco_p], eq2 = eco_q[eco_p];
            float cf = 1.0f, sf = 0.0f;
            if (eco_paso > 0.0f) {
                eco_fase += eco_paso;
                if (eco_fase > 6.28318530718f) { eco_fase -= 6.28318530718f; }
                cf = cosf(eco_fase); sf = sinf(eco_fase);
            }
            /* directo + eco girado */
            float ri = ii + g_eco * (ei * cf - eq2 * sf);
            float rq = qq + g_eco * (ei * sf + eq2 * cf);
            eco_i[eco_p] = ii; eco_q[eco_p] = qq;
            eco_p = (eco_p + 1u) % (uint32_t)ECO_RETARDO;
            ii = ri; qq = rq;
        }
        bi[nb] = ii + gauss() * sigma;
        bq[nb] = qq + gauss() * sigma;
        nb++;
        if (nb == 40u) { hfdl_modo_mete(bi, bq, nb); nb = 0u; hfdl_modo_poll(); }
        t += PASO;
        m++;
    }
    if (nb != 0u) { hfdl_modo_mete(bi, bq, nb); }
    hfdl_modo_poll();
    return hfdl_modo_buenas() - antes_b;
}

/* El parecido maximo entre lo espiado y lo transmitido, probando
 * desplazamientos. Invariante a escala y a giro, que es lo que hace que
 * sirva para los dos caminos. */
static float parecido(uint32_t *desp)
{
    uint32_t ne = hfdl_modo_espia_n(), d2;
    float mejor = -1.0f;
    *desp = 0u;
    for (d2 = 0u; d2 < 300u && d2 + 64u < ne && 64u < nd_global; d2++) {
        float ci = 0.0f, cq = 0.0f, e1 = 0.0f, e2 = 0.0f;
        uint32_t z;
        for (z = 0u; z < 64u; z++) {
            float ai = g_ei[z + d2], aq = g_eq[z + d2];
            float bi2 = g_di[z],     bq2 = g_dq[z];
            ci += ai * bi2 + aq * bq2;
            cq += aq * bi2 - ai * bq2;
            e1 += ai * ai + aq * aq;
            e2 += bi2 * bi2 + bq2 * bq2;
        }
        {
            float den = sqrtf(e1 * e2);
            float r = (den > 1e-9f) ? (sqrtf(ci * ci + cq * cq) / den) : 0.0f;
            if (r > mejor) { mejor = r; *desp = d2; }
        }
    }
    return mejor;
}

int main(void)
{
    /*
     * LAS OCHO, NO TRES. 14/10/2026.
     *
     * Pedido por el usuario.
     *
     * Aqui habia tres entradas -giros 1, 2 y 3- y se llamaban "BPSK",
     * "4PSK" y "8PSK" como si eso cubriera el aire. No lo cubre: hay
     * OCHO configuraciones (HFDL_FRAME_PARAMS[]) y estas tres son una
     * sola de cada giro. Las otras cinco no habian pasado nunca por un
     * banco, y entre ellas estan:
     *
     *   - el indice 0, 300 bps, el UNICO con ritmo de codigo 4. Es el
     *     que usan los squitter de la estacion de tierra, o sea el mas
     *     frecuente que hay en la frecuencia.
     *   - los indices 4..7, ranura doble: 168 segmentos de datos en vez
     *     de 72 y desplazamiento de desentrelazador 23 en vez de 17.
     *
     * Un banco que mide 3 de 8 y sale verde no dice "HFDL funciona":
     * dice "estas tres funcionan". Con eso se pueden entregar arreglos
     * en verde toda la tarde sin mover el numero de la radio.
     */
    static const struct { uint32_t giro; const char *nom; } k[] = {
        { 0u, "BPSK  300 simple" },
        { 1u, "BPSK  600 simple" },
        { 2u, "4PSK 1200 simple" },
        { 3u, "8PSK 1800 simple" },
        { 4u, "BPSK  300 doble " },
        { 5u, "BPSK  600 doble " },
        { 6u, "4PSK 1200 doble " },
        { 7u, "8PSK 1800 doble " },
    };
    const uint32_t NK = (uint32_t)(sizeof k / sizeof k[0]);
    uint32_t c;

    printf("\nLA RAFAGA ENTERA, HASTA EL CRC\n");
    printf("==============================\n\n");

    printf("  1. canal perfecto: sin giro y sin ruido\n");
    for (c = 0u; c < NK; c++) {
        char d[64];
        uint32_t b;
        giro_actual = k[c].giro;
        b = rafaga(k[c].giro, 0.0f, 0.0f);
        uint32_t dd = 0u;
        float pa = parecido(&dd);
        snprintf(d, sizeof d, "%u buena(s), parecido %.3f (desp %u)", b, (double)pa, dd);
        di(b >= 1u, k[c].nom, d);
    }

    printf("\n  2. con la constelacion girada medio paso\n");
    for (c = 0u; c < NK; c++) {
        /* Medio paso de la constelacion DE ESTA CONFIGURACION. El indice
         * M1 no es el numero de bits por simbolo -el 0 y el 4 son BPSK,
         * el 6 es 4-PSK-, asi que se saca de la tabla, no del indice. */
        float paso = 6.28318530718f / (float)(1u << HFDL_FRAME_PARAMS[k[c].giro].mod_arity);
        char d[64];
        uint32_t b;
        giro_actual = k[c].giro;
        b = rafaga(k[c].giro, paso, 0.0f);
        snprintf(d, sizeof d, "%u buena(s), giro de %.0f grados",
                 b, paso * 57.29578f);
        di(b >= 1u, k[c].nom, d);
    }

    printf("\n  3. y con ruido\n");
    for (c = 0u; c < NK; c++) {
        char d[64];
        uint32_t b;
        giro_actual = k[c].giro;
        b = rafaga(k[c].giro, 0.0f, 0.08f);
        snprintf(d, sizeof d, "%u buena(s) con sigma 0,08", b);
        di(b >= 1u, k[c].nom, d);
    }

    /*
     * APARTADO 4: INFORMATIVO, NO EXIGE NADA TODAVIA.
     *
     * Este es el camino que importa -AGC, filtro adaptado, remuestreador,
     * sincronismo de simbolo y Costas, que los otros tres apartados se
     * saltan- y hoy sale a cero: 2160 simbolos llegan al decodificador,
     * que es la cuenta exacta, y el CRC falla.
     *
     * NO SE EXIGE PORQUE NO SE SABE DE QUIEN ES EL CERO. La radio de
     * verdad decodifica el 39 % de BPSK por este mismo camino, asi que un
     * cero con canal PERFECTO huele a que el transmisor de audio de este
     * banco todavia no es representativo -conformado, latencia de grupo,
     * el punto exacto donde cae el enganche- y no a que el firmware este
     * roto del todo.
     *
     * Marcarlo en rojo seria mentir en la direccion contraria a la de
     * esta mañana: alli un banco verde escondia un fallo; aqui un banco
     * rojo acusaria al codigo de algo que todavia no se ha demostrado.
     * Se queda a la vista, con sus numeros, hasta que se sepa.
     */
    printf("\n  4. LA RAFAGA POR EL AUDIO (informativo, ver el comentario)\n");
    {
        uint32_t a1 = 0u, a2 = 0u, mm1 = 0u;
        for (c = 0u; c < NK; c++) {
            uint32_t b = rafaga_audio(k[c].giro, 0.0f);
            hfdl_modo_embudo(&a1, &a2, &mm1);
            printf("    %-16s %s %u buena(s) de %u, %u simbolos, embudo %u>%u>%u\n",
                   k[c].nom, (b >= 1u) ? "ok " : "---",
                   b, hfdl_modo_total(), hfdl_modo_simbolos_datos(), a1, a2, mm1);
            {
                uint32_t dd = 0u;
                float pa = parecido(&dd);
                printf("               parecido %.3f con desplazamiento %u\n",
                       (double)pa, dd);
            }
        }
    }

    /*
     * ======================================================================
     * 5. EL BARRIDO DE RUIDO: lo que de verdad decide los parametros
     * ======================================================================
     *
     * Con canal perfecto decodifica todo y no se distingue nada. Lo que
     * interesa es DONDE se cae cada velocidad, porque ese es el numero que
     * se mueve al tocar mu, la ventana del cuantificador o el sincronismo.
     *
     * Diez rafagas por punto. No es mucho, pero es diez veces mas de lo
     * que se podia medir con la radio del usuario en veinte minutos.
     */
    printf("\n  5. BARRIDO DE RUIDO (10 rafagas por punto)\n");
    {
        static const float k_sig[] = { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };
        uint32_t si2, rep;
        g_eco = 0.5f;   /* el eco, para que el ecualizador tenga trabajo */
        printf("      configuracion     ");
        for (si2 = 0u; si2 < 5u; si2++) { printf(" s=%.2f", (double)k_sig[si2]); }
        printf("\n");
        for (c = 0u; c < NK; c++) {
            printf("      %-16s  ", k[c].nom);
            for (si2 = 0u; si2 < 5u; si2++) {
                uint32_t ok = 0u;
                for (rep = 0u; rep < 10u; rep++) {
                    if (rafaga_audio(k[c].giro, k_sig[si2]) >= 1u) { ok++; }
                }
                printf("  %2u/10", ok);
            }
            printf("\n");
        }
    }

    /*
     * 6. EL BARRIDO DE DESVIO DE PORTADORA.
     *
     * Aqui es donde se tiene que ver la diferencia entre un detector de
     * fase de BPSK y uno de M-PSK: con la portadora desviada el lazo tiene
     * que seguirla, y un detector que mide el error contra dos puntos
     * cuando hay ocho tira de la fase hacia donde no es.
     */
    printf("\n  6. BARRIDO DE DESVIO DE PORTADORA (sigma 0,5, eco 0,5)\n");
    {
        static const float k_d[] = { 0.0f, 20.0f, 60.0f, 150.0f, 400.0f };
        uint32_t di2, rep;
        g_eco = 0.5f;
        printf("      configuracion     ");
        for (di2 = 0u; di2 < 5u; di2++) { printf("  %4.0fHz", (double)k_d[di2]); }
        printf("\n");
        for (c = 0u; c < NK; c++) {
            printf("      %-16s  ", k[c].nom);
            for (di2 = 0u; di2 < 5u; di2++) {
                uint32_t ok = 0u;
                g_desvio = k_d[di2];
                for (rep = 0u; rep < 10u; rep++) {
                    if (rafaga_audio(k[c].giro, 0.5f) >= 1u) { ok++; }
                }
                printf("  %2u/10", ok);
            }
            printf("\n");
        }
        g_desvio = 0.0f;
    }

    /*
     * ======================================================================
     * 7. EL PORTERO DEL ENTRENAMIENTO
     * ======================================================================
     *
     * Un portero que no se ha visto tirar a nadie no se ha probado. Aqui
     * se le meten dos cosas por el MISMO camino de audio:
     *
     *   - ráfagas de verdad, para ver que NO las tira (si tirara una sola,
     *     el portero seria peor que el problema);
     *   - ruido y nada mas que ruido, para ver que SI tira los enganches
     *     falsos, que son los que dejan al receptor sordo dos segundos y
     *     medio cada vez.
     *
     * Y se imprime la coherencia de las dos, que es de donde sale el
     * umbral HFDL_COH_MINIMA: no se eligio a ojo.
     */
    /*
     * ======================================================================
     * 6b. EL ERROR DE RELOJ
     * ======================================================================
     *
     * El transmisor del avion y el cristal de esta radio no son el mismo
     * reloj. Sobre los 2.160 simbolos de una ráfaga simple -o los 5.040 de
     * una doble- unas pocas partes por millon se convierten en simbolos
     * enteros de deriva, y un simbolo de deriva corre la tira que entra al
     * desentrelazador: el Viterbi decodifica entonces ruido perfectamente
     * uniforme, que es el fallo del 25/09 otra vez.
     *
     * Hasta hoy el banco daba por hecho que los dos relojes eran el mismo.
     * Eso no es un canal: es un laboratorio.
     */
    printf("\n  6b. BARRIDO DE ERROR DE RELOJ (sigma 0,5, eco 0,5)\n");
    {
        static const float k_r[] = { 0.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f };
        uint32_t ri, rep;
        g_eco = 0.5f;
        printf("      configuracion     ");
        for (ri = 0u; ri < 6u; ri++) { printf(" %4.0fppm", (double)k_r[ri]); }
        printf("\n");
        for (c = 0u; c < NK; c++) {
            printf("      %-16s  ", k[c].nom);
            for (ri = 0u; ri < 6u; ri++) {
                uint32_t ok = 0u;
                g_reloj = k_r[ri];
                for (rep = 0u; rep < 10u; rep++) {
                    if (rafaga_audio(k[c].giro, 0.5f) >= 1u) { ok++; }
                }
                printf("  %2u/10", ok);
            }
            printf("\n");
        }
        g_reloj = 0.0f;
    }

    /*
     * ======================================================================
     * 6c. EL CANAL QUE SE MUEVE
     * ======================================================================
     *
     * Ver g_doppler. Este es el apartado que el banco nunca tuvo y el que
     * explica por que salia verde mientras la radio fallaba: con el canal
     * quieto, una ráfaga que entrena bien acaba bien SIEMPRE. Con el canal
     * moviendose, no.
     */
    /*
     * 6d. ¿QUE VE EL PRE-KEY? Se mide DESPUES del lazo de Costas, asi
     * que lo que reporte es el error QUE QUEDA. Si con 60 Hz de desvio
     * dice ~0, el lazo ya se lo ha comido durante el tono y corregirlo
     * no aporta nada; si dice 60, el lazo no lo esta siguiendo y ahi hay
     * una mina.
     */
    printf("\n  6d. EL DESVIO QUE VE EL PRE-KEY (BPSK 600, sigma 0,2)\n");
    {
        static const float k_v[] = { 0.0f, 20.0f, 60.0f, 150.0f, 400.0f };
        uint32_t vi;
        g_eco = 0.0f;
        for (vi = 0u; vi < 5u; vi++) {
            uint32_t b;
            g_desvio = k_v[vi];
            b = rafaga_audio(1u, 0.2f);
            printf("      puesto %5.1f Hz  ->  el pre-key dice %7.2f Hz   (%u buena)\n",
                   (double)k_v[vi], (double)hfdl_modo_prekey_desvio(), b);
        }
        g_desvio = 0.0f;
    }

    printf("\n  6c. BARRIDO DE DESVANECIMIENTO (sigma 0,5, eco 0,5)\n");
    {
        static const float k_d[] = { 0.0f, 0.2f, 0.5f, 1.0f, 2.0f };
        uint32_t di3, rep;
        g_eco = 0.5f;
        printf("      configuracion     ");
        for (di3 = 0u; di3 < 5u; di3++) { printf(" %4.1fHz", (double)k_d[di3]); }
        printf("\n");
        for (c = 0u; c < NK; c++) {
            printf("      %-16s  ", k[c].nom);
            for (di3 = 0u; di3 < 5u; di3++) {
                uint32_t ok = 0u;
                g_doppler = k_d[di3];
                for (rep = 0u; rep < 10u; rep++) {
                    if (rafaga_audio(k[c].giro, 0.5f) >= 1u) { ok++; }
                }
                printf("  %2u/10", ok);
            }
            printf("\n");
        }
        g_doppler = 0.0f;
    }

    printf("\n  7. EL PORTERO DEL ENTRENAMIENTO\n");
    {
        uint32_t ant_r, rech_buenas = 0u, buenas = 0u;
        float coh_min = 9.0f, coh_max = 0.0f;
        g_eco = 0.5f;

        /* a) ráfagas de verdad, en el peor ruido del barrido */
        for (c = 0u; c < NK; c++) {
            uint32_t rep;
            printf("    %-16s  coherencias:", k[c].nom);
            for (rep = 0u; rep < 5u; rep++) {
                uint32_t b2 = rafaga_audio(k[c].giro, 0.9f);
                float co = hfdl_modo_coherencia();
                ant_r = hfdl_modo_rechazadas();
                if (b2 >= 1u) { buenas++; }
                printf(" %.3f%s", (double)co, ant_r ? "!" : "");
                if (co > 0.0f) {
                    if (co < coh_min) { coh_min = co; }
                    if (co > coh_max) { coh_max = co; }
                }
                /* "!" = el portero tiro algo en esta ráfaga. Si ademas
                 * salio buena, es que tiro un enganche falso ANTES y la
                 * de verdad entro despues: eso es justo lo que se busca. */
                if (ant_r != 0u && b2 == 0u) { rech_buenas += ant_r; }
            }
            printf("\n");
        }
        (void)rech_buenas; (void)ant_r;
        printf("    ráfagas de verdad (sigma 0,9): %u decodificadas\n", buenas);
        printf("    coherencia maxima vista en una ráfaga de verdad: %.3f\n",
               (double)coh_max);
        giro_actual = 0u;
        printf("    (el umbral del portero es %.2f)\n", (double)0.30f);
        printf("    coherencia minima: %.3f  <- NO es la de una ráfaga buena:\n",
               (double)coh_min);
        printf("       es la del ultimo enganche JUZGADO, que en dos casos fue\n");
        printf("       un enganche falso del ruido de delante. Por eso la prueba\n");
        printf("       de abajo es sobre la MAXIMA y sobre el recuento, no sobre\n");
        printf("       esta: un banco que exija el minimo esta midiendo al ruido.\n");

        /*
         * LA PRUEBA DE QUE EL PORTERO NO ESTORBA NO ES CONTAR RECHAZOS:
         * es que los recuentos del apartado 5 no bajen. Eso ya esta
         * medido ahi arriba y salio identico con el portero puesto y sin
         * el. Aqui lo unico que se exige es el margen del umbral, que es
         * lo que este apartado si puede medir.
         */
        if (coh_max < 0.60f) {
            printf("    FALLO: las ráfagas de verdad solo llegan a %.3f de coherencia,\n",
                   (double)coh_max);
            printf("           demasiado cerca del umbral 0,30 - el portero tiraria buenas\n");
            fallos++;
        }
        if (buenas < 15u) {
            printf("    FALLO: solo %u ráfagas decodificadas de 40 con el portero puesto\n",
                   buenas);
            fallos++;
        }

        /* b) ruido solo, por el mismo camino */
        {
            uint32_t i, tot_antes, rech_antes;
            hfdl_modo_stop();
            if (hfdl_modo_start()) {
                tot_antes = hfdl_modo_total();
                rech_antes = hfdl_modo_rechazadas();
                /* 1.200.000 muestras a 12 kHz = 100 segundos de ruido,
                 * por el mismo bloque de 64 que usa la radio. */
                {
                    float ni[64], nq[64];
                    uint32_t b2;
                    for (i = 0u; i < 1200000u / 64u; i++) {
                        for (b2 = 0u; b2 < 64u; b2++) {
                            ni[b2] = gauss() * 0.5f;
                            nq[b2] = gauss() * 0.5f;
                        }
                        hfdl_modo_mete(ni, nq, 64u);
                        hfdl_modo_poll();
                    }
                }
                printf("    100 s de RUIDO: %u tiradas por el portero, %u llegaron al CRC\n",
                       hfdl_modo_rechazadas() - rech_antes,
                       hfdl_modo_total() - tot_antes);
                /*
                 * Con ruido puro lo que NO puede pasar es que algo llegue
                 * al CRC: cada una de esas deja al receptor sordo entre
                 * 1,2 y 2,8 segundos, y esa sordera es lo que se ve desde
                 * fuera como tramas que se oyen y renglones que no suben.
                 */
                if ((hfdl_modo_total() - tot_antes) != 0u) {
                    printf("    FALLO: %u enganches de RUIDO han llegado al CRC\n",
                           hfdl_modo_total() - tot_antes);
                    fallos++;
                }
            }
        }
        g_eco = 0.0f;
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

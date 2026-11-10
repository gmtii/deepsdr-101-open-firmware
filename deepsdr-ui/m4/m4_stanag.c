/*
 * ======================================================================
 * LO QUE CUESTAN LAS DOS CADENAS MAS GORDAS: STANAG Y HFDL
 * ======================================================================
 *
 * POR QUE HACE FALTA ESTE NUMERO. Los otros bancos del M4 miden dibujo
 * y audio, que son lo que se ve. Estas dos cadenas son lo que NO se ve:
 * corren enteras desde la interrupcion de audio y desde el bucle
 * principal, y cuando se pasan no sale un fotograma feo, sale un modo
 * que no engancha y nadie sabe por que.
 *
 * Y LO MAS IMPORTANTE ES EL CASO SIN SENAL. Una radio con un modo
 * puesto y la banda vacia es el estado mas comun que hay: el usuario
 * sintoniza, mira, y no hay nada. Si ese caso cuesta lo mismo que el
 * caso con senal -o mas, porque el portero esta abierto y el buscador
 * de preambulo muele ruido- la radio va lenta precisamente cuando no
 * esta haciendo nada util. Por eso aqui todo se mide DOS VECES.
 *
 * COMO SE LEEN LOS NUMEROS. Igual que en los demas bancos del M4 (ver
 * m4/LEEME.md): un tic del SysTick son 40 instrucciones, el GD32 va a
 * 200 MHz y la radio de verdad tarda alrededor de 1,7 veces esto. El
 * tanto por ciento del segundo sale de la tasa a la que DE VERDAD le
 * entra el audio a cada pieza, que aqui son:
 *
 *   HFDL     12 kHz COMPLEJOS, en bloques de 32 muestras (96/192 kHz de
 *            ADC) o de 64 (48 kHz). Ver DEC_BLOCK_SAMPLES en
 *            demod_am.c y la llamada a hfdl_modo_mete() de su linea
 *            6228. Los simbolos salen a 1800 baudios.
 *   STANAG   12 kHz REALES, el mismo audio de banda lateral que comen
 *            FT8, ALE y JTTY. Ver stanag_modo_mete(). Los simbolos van
 *            a 2400 baudios, cinco muestras cada uno.
 *
 * LO QUE ESTE BANCO NO DICE, que es lo de siempre: QEMU no modela la
 * espera de la flash ni la cache ni el bus, asi que lo de aqui es un
 * SUELO. Y el reparto real entre interrupcion y bucle principal lo
 * decide main.c, que aqui no esta: esto mide las piezas, no el reparto.
 *
 * NO HAY TOPES A PROPOSITO. Es una medida, no un guardian: ninguna fila
 * de aqui puede ponerse roja. Si un dia hace falta vigilar que algo no
 * engorde, el tope se pone en un banco aparte y con el numero de hoy
 * delante.
 */
#include "semi.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "hfdl_modo.h"
#include "hfdl_ram.h"
#include "hfdl_demod_chain.h"
#include "hfdl_iq_mixer.h"
#include "hfdl_rational_resampler.h"
#include "hfdl_agc.h"
#include "hfdl_matched_filter.h"
#include "hfdl_symsync.h"
#include "hfdl_costas.h"
#include "hfdl_equalizer.h"
#include "hfdl_viterbi.h"
#include "hfdl_deinterleaver.h"
#include "hfdl_descrambler.h"
#include "hfdl_soft_demod.h"
#include "hfdl_preamble_seq.h"
#include "hfdl_preamble_sync.h"
#include "hfdl_data_segment.h"
#include "hfdl_payload_decode.h"
#include "hfdl_pdu.h"
#include "hfdl_crc.h"

#include "stanag_det.h"
#include "stanag_rx.h"
#include "s4415.h"
#include "stanag_modo.h"

void pila_di(unsigned long maximo);

int g_fallos;

#define SCB_CCR (*(volatile uint32_t*)0xE000ED14UL)

/* ====================================================================
 * LOS TAPONES
 * ====================================================================
 *
 * Nada de esto se mide: son las costuras de la radio que estos modulos
 * tocan de refilon y que aqui no existen. Van DENTRO de este fichero a
 * proposito: tocar User/ para poder medirlo seria medir otra cosa.
 */

/* Aqui no hay NVIC y el banco es de un solo hilo. */
void __disable_irq(void) { }
void __enable_irq(void)  { }

/*
 * LA RAM PRESTADA. En la radio la presta la cascada y vale NULL fuera
 * del modo (ver hfdl_ram.h); aqui es un bloque fijo que esta siempre.
 * Devolver siempre la region, y no NULL, es lo unico que cambia, y hace
 * falta para poder medir las piezas sueltas -el buscador de preambulo,
 * por ejemplo- sin arrancar el modo entero.
 */
static uint8_t g_hram[HFDL_RAM_TOTAL] __attribute__((aligned(8)));
static uint8_t g_prestada;

void hfdl_ram_coge(void)   { g_prestada = 1U; }
void hfdl_ram_suelta(void) { g_prestada = 0U; }
uint8_t *hfdl_ram_cabecera(void) { return g_hram; }
uint8_t *hfdl_ram_todo(uint32_t *cap)
{
    if (cap) { *cap = (uint32_t)sizeof g_hram; }
    return g_hram;
}
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *cap)
{
    if (cap) { *cap = (uint32_t)sizeof g_hram - HFDL_RAM_CABECERA; }
    return g_hram + HFDL_RAM_CABECERA;
}
uint8_t waterfall_prestada(void) { return 0U; }

/* La hora, la base de paises y el idioma: ni se miden ni hacen falta. */
struct rtc_hw_datetime_tapon { uint8_t x[16]; };
void rtc_hw_get(void *o) { if (o) { memset(o, 0, 16); } }
int  icao24_db_lookup(uint32_t icao, char *out, uint32_t n)
{ (void)icao; if (out && n) { out[0] = '\0'; } return 0; }
const char *tr(const char *es, const char *en) { (void)en; return es; }

/* Los relojes del panel, que stanag_modo.c mira para su chapa. */
volatile uint32_t g_msticks;
uint32_t ui_bucle_peor_us(void)  { return 0U; }
uint32_t ui_polls_peor_us(void)  { return 0U; }
uint32_t ui_dibujo_peor_us(void) { return 0U; }

/*
 * EL MONTON, Y HACE FALTA DE VERDAD.
 *
 * El transmisor de rafagas de hfdl_payload_decode.c -el mismo que usa su
 * autoprueba- pide memoria con malloc() a lo grande: el mensaje, el
 * entrelazado, la tabla de permutacion y la region del decodificador. Son
 * unos 60 kB en el giro 2. Sin monton, malloc() devuelve NULL, el
 * transmisor escribe en la direccion cero y el banco se queda colgado sin
 * decir nada, que es exactamente lo que paso la primera vez.
 *
 * Vive en .bss, que el enlazador declara NOLOAD, asi que no cuesta ni un
 * byte de imagen. En la radio no hay monton y nadie llama a esto: el
 * transmisor es de banco, no de firmware.
 */
static uint8_t g_mont[256U * 1024U];
void *_sbrk(int inc)
{
    static uint32_t p;
    uint8_t *r = &g_mont[p];

    if (inc < 0) { return (void *)-1; }
    if ((uint32_t)inc > (uint32_t)sizeof g_mont - p) { return (void *)-1; }
    p += (uint32_t)inc;
    return r;
}

/* ====================================================================
 * EL CUENTAHILOS
 * ==================================================================== */

/* Tres decimales no hacen falta y dos si: el 0,4 % del detector y el
 * 0,04 % del mezclador son cosas muy distintas y en enteros los dos son
 * cero. */
/*
 * Y DONDE SE GUARDA LO QUE SE MIDE, para el resumen del final.
 *
 * El resumen llevaba los tres numeros ESCRITOS A MANO -"~39 ms", "~19 ms",
 * "4,8 / 18,6 ms"- de cuando se midieron por primera vez. El 08/11/2026 se
 * acelero realce() y la ventana del identificador paso de 39 a 18 ms: el
 * resumen seguia diciendo 39. Es el fallo que este proyecto lleva meses
 * persiguiendo -algo escrito una vez que deja de ser verdad- dentro del
 * banco que existe precisamente para no equivocarse con los numeros. Asi
 * que ahora el resumen imprime lo que acaba de medir.
 */
static unsigned long g_peor_det, g_peor_barrido, g_peor_vit0, g_peor_vit2;

static void prd2(unsigned long cent)
{
    prn(cent / 100UL); pr(",");
    if ((cent % 100UL) < 10UL) { pr("0"); }
    prn(cent % 100UL);
}

static unsigned long ins_de(unsigned long a, unsigned long b)
{
    return ((a - b) & 0xFFFFFFUL) * 40UL;
}

/*
 * UNA FILA DEL INFORME.
 *
 *   que      el nombre
 *   ins      instrucciones por unidad (muestra, simbolo, bloque, suceso)
 *   por_seg  cuantas unidades de esas pasan en UN SEGUNDO de audio
 *
 * De ahi salen los microsegundos por segundo y el tanto por ciento, que
 * es el unico numero con el que se puede decidir algo.
 */
static unsigned long g_ult_us;      /* lo ultimo que dijo fila(), en us/s */

static unsigned long fila(const char *que, unsigned long ins, unsigned long por_seg)
{
    unsigned long us_s = (ins * por_seg) / 200UL;   /* us por segundo */

    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" ins");
    if (ins >= 200UL) { pr("  ("); prn(ins / 200UL); pr(" us)"); }
    pr("   ");
    prn(us_s); pr(" us/s   ");
    prd2(us_s / 100UL); pr(" % del segundo\n");
    g_ult_us = us_s;
    return us_s;
}

/*
 * LA TABLA DEL RESUMEN.
 *
 * Solo entran las filas de PRIMER NIVEL -lo que la radio llama de
 * verdad, con su modo puesto- y no las piezas sueltas: las piezas van
 * DENTRO de esas filas y ponerlas juntas seria contarlas dos veces.
 */
#define N_RES 24
static const char   *g_res_q[N_RES];
static unsigned long g_res_us[N_RES];
static uint32_t      g_res_n;

static void anota(const char *que, unsigned long us_s)
{
    if (g_res_n < (uint32_t)N_RES) {
        g_res_q[g_res_n] = que; g_res_us[g_res_n] = us_s; g_res_n++;
    }
}

static void resumen(void)
{
    uint32_t i, j, m;

    pr("\n======================================================================\n");
    pr("RESUMEN: lo que se come cada cosa de UN SEGUNDO de audio de 12 kHz\n");
    pr("======================================================================\n");
    pr("  De mayor a menor. Son instrucciones entre 200 MHz: en la radio\n");
    pr("  de verdad hay que multiplicar por 1,7 (ver m4/LEEME.md).\n\n");
    for (i = 0u; i < g_res_n; i++) {
        const char *tq; unsigned long tu;
        m = i;
        for (j = i + 1u; j < g_res_n; j++) {
            if (g_res_us[j] > g_res_us[m]) { m = j; }
        }
        tq = g_res_q[i]; tu = g_res_us[i];
        g_res_q[i] = g_res_q[m]; g_res_us[i] = g_res_us[m];
        g_res_q[m] = tq; g_res_us[m] = tu;
        pr("    "); pr(g_res_q[i]); pr("  ");
        prn(g_res_us[i]); pr(" us/s   ");
        prd2(g_res_us[i] / 100UL); pr(" %\n");
    }
    pr("\n  Y LO QUE NO ES POR SEGUNDO, que son sucesos y van aparte.\n");
    pr("  Son los numeros que acaba de medir este banco, no los de cuando\n");
    pr("  se escribio esto: ver g_peor_det en este mismo fichero.\n");
    pr("    una ventana del identificador de STANAG   ");
    prd2(g_peor_det / 2000UL); pr(" ms, y salen ~12 por segundo\n");
    pr("    un barrido de portadora del 4285          ");
    prd2(g_peor_barrido / 2000UL); pr(" ms, uno por enganche\n");
    pr("    el remate de una rafaga de HFDL (Viterbi) ");
    prd2(g_peor_vit0 / 2000UL); pr(" ms en giro 0,\n");
    pr("                                              ");
    prd2(g_peor_vit2 / 2000UL); pr(" ms en giro 2\n");
    pr("    Los tres corren en el BUCLE PRINCIPAL, de un tiron y sin partir:\n");
    pr("    son el hueco mas largo que el audio se queda sin que nadie lo mire.\n");
}

/*
 * EL PEOR DE UNA TANDA, que para un bucle no es lo mismo que la media.
 * El bucle principal se mide por el peor hueco que deja, no por el
 * promedio: una sola llamada de 39 ms deja al audio sin servir aunque la
 * media salga bonita.
 */
static unsigned long g_peor;
static void peor_cero(void) { g_peor = 0UL; }
static void peor_mira(unsigned long a, unsigned long b)
{
    unsigned long v = ins_de(a, b);
    if (v > g_peor) { g_peor = v; }
}
static void peor_di(const char *que)
{
    pr("      el peor "); pr(que); pr(": ");
    prn(g_peor); pr(" ins = "); prn(g_peor / 200UL); pr(" us\n");
}

/* Lo mismo pero partiendo de un total y un numero de unidades. */
static unsigned long tramo(const char *que, unsigned long a, unsigned long b,
                           unsigned long n, unsigned long por_seg)
{
    unsigned long ins = ins_de(a, b) / ((n != 0UL) ? n : 1UL);
    (void)fila(que, ins, por_seg);
    return ins;
}

/* Y para los sucesos que no son periodicos: solo el coste, sin reparto. */
static unsigned long suelto(const char *que, unsigned long a, unsigned long b,
                            unsigned long n)
{
    unsigned long ins = ins_de(a, b) / ((n != 0UL) ? n : 1UL);

    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" ins   ");
    prn(ins / 200UL); pr(" us a 200 MHz\n");
    return ins;
}

/* ====================================================================
 * RUIDO REPETIBLE
 * ====================================================================
 * Suma de doce uniformes menos seis, igual que los bancos de sim/. Un
 * banco que da un numero distinto cada vez no sirve para comparar dos
 * versiones, que es para lo que existe.
 */
static unsigned long g_sem = 1234567UL;
static float uni(void)
{
    g_sem = g_sem * 1103515245UL + 12345UL;
    return (float)((g_sem >> 16) & 0x7FFFUL) / 32767.0f;
}
static float gauss(void)
{
    int i; float s = 0.0f;
    for (i = 0; i < 12; i++) { s += uni(); }
    return s - 6.0f;
}

/* ====================================================================
 * LAS TASAS DE VERDAD
 * ==================================================================== */

#define FS_HZ        12000UL   /* las dos cadenas comen a 12 kHz */
#define BLOQUE       32UL      /* DEC_BLOCK_SAMPLES_96K, el caso normal */
#define BLOQUES_S    (FS_HZ / BLOQUE)      /* 375 bloques por segundo */
#define SIMB_HFDL    1800UL    /* baudios de HFDL */
#define SIMB_4285    2400UL    /* baudios del 4285 */

/* ====================================================================
 * EL AUDIO
 * ==================================================================== */

#define N_AUD   36000u         /* 3 segundos a 12 kHz */
static float g_ai[N_AUD];
static float g_aq[N_AUD];

/* La tira de simbolos de una rafaga de HFDL. 150 + 72*45 = 3390 en
 * ranura sencilla; se deja sitio para la doble por si algun dia se
 * mide. */
#define N_SIM   8704u
static float g_si[N_SIM];
static float g_sq[N_SIM];
static uint32_t g_ns;

/* --- el conformador, copiado de la idea de sim/hfdl_extremo.c ------- */
#define RRC_SPS   (12000.0f / 1800.0f)
#define RRC_SPAN  8
/* 8 simbolos a cada lado por 6,667 muestras por simbolo, impar. Va de
 * literal y no de cuenta con float porque un tamano de array a nivel de
 * fichero no puede depender de una expresion en coma flotante. */
#define RRC_LEN   53
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

static void pon_sim(float i_, float q_)
{
    if (g_ns < N_SIM) { g_si[g_ns] = i_; g_sq[g_ns] = q_; g_ns++; }
}

/*
 * De la tira de simbolos al audio de 12 kHz complejos con la
 * subportadora de 1440 Hz, que es donde la cadena espera encontrarla
 * (ver HFDL_MIXER_SUBCARRIER_HZ y el comentario grande de
 * sim/hfdl_extremo.c sobre este mismo asunto: meter la senal en banda
 * base deja al mezclador con la senal a -1440 Hz y el banco mide
 * papilla).
 *
 * Devuelve cuantas muestras ha puesto.
 */
static uint32_t hfdl_audio(float sigma, uint32_t max)
{
    double t = 0.0;
    const double PASO = 1800.0 / 12000.0;
    float sub = 0.0f;
    uint32_t m = 0u;

    while (t < (double)(g_ns - 1u) && m < max) {
        float ii = 0.0f, qq = 0.0f;
        int kk, centro = (int)(t + 0.5);

        for (kk = centro - RRC_SPAN; kk <= centro + RRC_SPAN; kk++) {
            int idx;
            float dt;
            if (kk < 0 || (uint32_t)kk >= g_ns) { continue; }
            dt = (float)(t - (double)kk);
            idx = (int)((dt * RRC_SPS) + (float)(RRC_LEN - 1) * 0.5f + 0.5f);
            if (idx < 0 || idx >= RRC_LEN) { continue; }
            ii += g_si[kk] * g_rrc[idx];
            qq += g_sq[kk] * g_rrc[idx];
        }
        ii *= RRC_SPS; qq *= RRC_SPS;
        {
            float c = cosf(sub), s = sinf(sub);
            float mi = ii * c - qq * s;
            float mq = ii * s + qq * c;
            ii = mi; qq = mq;
            sub += 6.28318530718f * 1440.0f / 12000.0f;
            if (sub > 6.28318530718f) { sub -= 6.28318530718f; }
        }
        g_ai[m] = ii + gauss() * sigma;
        g_aq[m] = qq + gauss() * sigma;
        m++;
        t += PASO;
    }
    return m;
}

/* Ruido y nada mas, para el caso que de verdad importa. */
static void ruido_iq(uint32_t n, float sigma)
{
    uint32_t k;
    for (k = 0u; k < n; k++) {
        g_ai[k] = gauss() * sigma;
        g_aq[k] = gauss() * sigma;
    }
}

/* Monta la tira de simbolos de una rafaga entera del giro `giro`:
 * pre-key, A1, A2, M1, el M2 que se tira, nueve ventanas de
 * entrenamiento y las de datos con su entrenamiento intercalado. Misma
 * forma que sim/hfdl_extremo.c, que es la que el firmware espera. */
/*
 * `cola` son simbolos de relleno DETRAS de la rafaga. Hacen falta cuando
 * la rafaga entra por el audio y no por los simbolos: el filtro adaptado,
 * el sincronizador y el conformador se comen entre todos unas decenas de
 * simbolos de retardo de grupo, asi que sin cola el segmento de datos se
 * queda a medias y hfdl_modo_poll() no tiene nada que rematar. Entrando
 * por simbolos no hacen falta y se pide cola 0.
 */
static uint32_t rafaga_simbolos(uint32_t giro, uint32_t cola)
{
    static float a[HFDL_A_LEN], m1[HFDL_M1_LEN];
    static float di[N_SIM], dq[N_SIM];
    uint32_t i, v, nd = 0u, idx = 0u;

    g_ns = 0u;
    hfdl_preamble_seq_build_a(a);
    hfdl_preamble_seq_build_m1_variant(giro, m1);
    if (!hfdl_payload_decode_tx_simbolos(giro, di, dq, N_SIM, &nd)) {
        return 0u;
    }

    for (i = 0u; i < 40u; i++) { pon_sim((uni() > 0.5f) ? 1.0f : -1.0f, 0.0f); }
    for (i = 0u; i < HFDL_A_LEN; i++)  { pon_sim(a[i], 0.0f); }
    for (i = 0u; i < HFDL_A_LEN; i++)  { pon_sim(a[i], 0.0f); }
    for (i = 0u; i < HFDL_M1_LEN; i++) { pon_sim(m1[i], 0.0f); }
    for (i = 0u; i < HFDL_M2_LEN; i++) { pon_sim((uni() > 0.5f) ? 1.0f : -1.0f, 0.0f); }
    for (v = 0u; v < 9u; v++) {
        for (i = 0u; i < HFDL_T_LEN; i++) { pon_sim(HFDL_T_SEQ[0][i], 0.0f); }
    }
    for (v = 0u; v < (uint32_t)HFDL_FRAME_PARAMS[giro].data_segment_cnt; v++) {
        for (i = 0u; i < 30u; i++) {
            pon_sim((idx < nd) ? di[idx] : 1.0f, (idx < nd) ? dq[idx] : 0.0f);
            idx++;
        }
        for (i = 0u; i < HFDL_T_LEN; i++) { pon_sim(HFDL_T_SEQ[0][i], 0.0f); }
    }
    for (i = 0u; i < cola; i++) { pon_sim((uni() > 0.5f) ? 1.0f : -1.0f, 0.0f); }
    return g_ns;
}

/* ====================================================================
 * 1. HFDL: LAS PIEZAS DE LA CADENA
 * ====================================================================
 *
 * Cada una por separado, medida con el mismo tamano de lote que le toca
 * dentro de hfdl_demod_chain_process(): el mezclador y el remuestreador
 * van por muestra de 12 kHz, el AGC y el filtro adaptado por las nueve
 * fases que suelta el remuestreador, y el sincronizador y el lazo por
 * simbolo. Asi las filas se pueden sumar y comparar con la cadena
 * entera, que es lo que hace el resumen.
 */
static hfdl_demod_chain_t g_cad;
/* Lo que cuesta la cadena por muestra de 12 kHz, medido mas abajo. Lo usa
 * el apartado de la rafaga entera para poder sumar las dos mitades. */
static unsigned long g_cad_ins;

static void hfdl_piezas(void)
{
    static hfdl_iq_mixer_t mx;
    static hfdl_rational_resampler_t rs;
    static hfdl_agc_t ag;
    static hfdl_matched_filter_t mf;
    static hfdl_symsync_t ss;
    static hfdl_costas_t co;
    float bi[HFDL_RESAMP_NUM_PHASES], bq[HFDL_RESAMP_NUM_PHASES];
    float ci[HFDL_RESAMP_NUM_PHASES], cq[HFDL_RESAMP_NUM_PHASES];
    float oi[HFDL_SYMSYNC_M], oq[HFDL_SYMSYNC_M];
    float hi[HFDL_SYMSYNC_M], hq[HFDL_SYMSYNC_M];
    uint8_t hc[HFDL_SYMSYNC_M];
    unsigned long a, b;
    uint32_t k, n;

    pr("\n  LAS PIEZAS DE LA CADENA DE HFDL, UNA A UNA\n");
    pr("    (el % es sobre 12.000 muestras/s complejas, o 1.800 simbolos/s)\n\n");

    hfdl_iq_mixer_init(&mx, 1440.0f, 12000.0f, HFDL_MIXER_DEFAULT_SIGN);
    a = tic();
    for (k = 0u; k < 6000u; k++) {
        float mi, mq;
        hfdl_iq_mixer_process(&mx, &g_ai[k], &g_aq[k], &mi, &mq, 1u);
    }
    b = tic();
    (void)tramo("mezclador a banda base, por muestra  ", a, b, 6000UL, FS_HZ);

    hfdl_rational_resampler_init(&rs);
    a = tic();
    for (k = 0u; k + 20u < 6000u; k += 20u) {
        hfdl_rational_resampler_process(&rs, &g_ai[k], &g_aq[k], bi, bq);
    }
    b = tic();
    (void)tramo("remuestreador 9/20, por muestra      ", a, b, 6000UL, FS_HZ);

    hfdl_agc_init(&ag);
    a = tic();
    for (k = 0u; k < 300u; k++) {
        hfdl_agc_process(&ag, bi, bq, ci, cq, HFDL_RESAMP_NUM_PHASES);
    }
    b = tic();
    /* 9 salidas por cada 20 muestras de 12 kHz: 5.400 por segundo. */
    (void)tramo("AGC, por muestra de 5.400 Hz         ", a, b,
                300UL * HFDL_RESAMP_NUM_PHASES, 5400UL);

    hfdl_matched_filter_init(&mf);
    a = tic();
    for (k = 0u; k < 300u; k++) {
        hfdl_matched_filter_process(&mf, ci, cq, bi, bq, HFDL_RESAMP_NUM_PHASES);
    }
    b = tic();
    (void)tramo("filtro adaptado 19 tomas, por muestra", a, b,
                300UL * HFDL_RESAMP_NUM_PHASES, 5400UL);

    hfdl_symsync_reset(&ss);
    hfdl_symsync_unlock(&ss);
    a = tic();
    for (k = 0u; k < 5400u; k++) {
        hfdl_symsync_step(&ss, g_ai[k], g_aq[k], oi, oq, hi, hq, hc, &n);
    }
    b = tic();
    (void)tramo("sincronizador de simbolo, por muestra", a, b, 5400UL, 5400UL);

    hfdl_costas_init(&co, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
    a = tic();
    for (k = 0u; k < 3600u; k++) {
        float ri, rq;
        hfdl_costas_step_mpsk(&co, g_ai[k], g_aq[k], 1u, &ri, &rq);
    }
    b = tic();
    (void)tramo("lazo de Costas BPSK, por simbolo     ", a, b, 3600UL, SIMB_HFDL);

    a = tic();
    for (k = 0u; k < 3600u; k++) {
        float ri, rq;
        hfdl_costas_step_mpsk(&co, g_ai[k], g_aq[k], 3u, &ri, &rq);
    }
    b = tic();
    (void)tramo("lazo de Costas 8-PSK, por simbolo    ", a, b, 3600UL, SIMB_HFDL);

    a = tic();
    for (k = 0u; k < 3600u; k++) {
        float ri, rq;
        hfdl_costas_derotate_only(&co, g_ai[k], g_aq[k], &ri, &rq);
    }
    b = tic();
    (void)tramo("derotar la muestra T/2, por simbolo  ", a, b, 3600UL, SIMB_HFDL);
}

/* ====================================================================
 * 2. HFDL: LA CADENA ENTERA, POR BLOQUE
 * ==================================================================== */
static void hfdl_cadena(uint32_t n_sen)
{
    float si[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT], sq[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    float hi[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT], hq[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    unsigned long a, b, v;
    uint32_t k, ns, tot = 0u, nb = 0u;

    pr("\n  LA CADENA DE DEMODULACION ENTERA, hfdl_demod_chain_process()\n\n");

    hfdl_demod_chain_init(&g_cad);
    a = tic();
    for (k = 0u; k + BLOQUE <= n_sen; k += BLOQUE) {
        hfdl_demod_chain_process(&g_cad, &g_ai[k], &g_aq[k], (uint32_t)BLOQUE,
                                 si, sq, hi, hq, &ns);
        tot += ns; nb++;
    }
    b = tic();
    v = tramo("CON SENAL, por bloque de 32 muestras ", a, b, nb, BLOQUES_S);
    g_cad_ins = v / BLOQUE;
    anota("HFDL  la cadena sola (va DENTRO de _mete) ",
          fila("CON SENAL, por muestra de 12 kHz     ", g_cad_ins, FS_HZ));
    pr("      "); prn((unsigned long)tot); pr(" simbolos de ");
    prn((unsigned long)nb); pr(" bloques\n");

    ruido_iq(n_sen, 0.3f);
    hfdl_demod_chain_init(&g_cad);
    tot = 0u; nb = 0u;
    a = tic();
    for (k = 0u; k + BLOQUE <= n_sen; k += BLOQUE) {
        hfdl_demod_chain_process(&g_cad, &g_ai[k], &g_aq[k], (uint32_t)BLOQUE,
                                 si, sq, hi, hq, &ns);
        tot += ns; nb++;
    }
    b = tic();
    v = tramo("SIN SENAL (solo ruido), por bloque   ", a, b, nb, BLOQUES_S);
    (void)fila("SIN SENAL, por muestra de 12 kHz     ", v / BLOQUE, FS_HZ);
    pr("      ");
    pr("la cadena no mira lo que lleva dentro: cuesta lo mismo con y sin\n");
}

/* ====================================================================
 * 3. HFDL: EL DESPUES DE LA CADENA
 * ====================================================================
 * El buscador de preambulo, el ecualizador, el Viterbi, el
 * desentrelazado, el descrambler y el formato de PDU.
 */
static hfdl_preamble_sync_t g_pre;
static hfdl_equalizer_t     g_eq;
static hfdl_viterbi_t       g_vit;
static hfdl_deinterleaver_t g_dei;
static hfdl_descrambler_t   g_scr;

static void hfdl_despues(void)
{
    unsigned long a, b, v;
    uint32_t k;

    pr("\n  LO QUE VA DETRAS DE LA CADENA\n\n");

    /* --- el buscador de preambulo, que corre en CADA simbolo ------- */
    hfdl_preamble_sync_init(&g_pre);
    hfdl_preamble_sync_reset(&g_pre);
    a = tic();
    for (k = 0u; k < 3600u; k++) {
        (void)hfdl_preamble_sync_process_symbol(&g_pre, g_ai[k], g_aq[k]);
    }
    b = tic();
    (void)tramo("preambulo con RUIDO, por simbolo     ", a, b, 3600UL, SIMB_HFDL);

    /* Y con el preambulo de verdad delante, que es cuando dispara la
     * confirmacion de A2 y la prueba de los ocho M1: el caso caro. */
    {
        static float a1[HFDL_A_LEN], m1[HFDL_M1_LEN];
        uint32_t rep;
        hfdl_preamble_seq_build_a(a1);
        hfdl_preamble_seq_build_m1_variant(0u, m1);
        hfdl_preamble_sync_reset(&g_pre);
        a = tic();
        for (rep = 0u; rep < 8u; rep++) {
            for (k = 0u; k < HFDL_A_LEN; k++) {
                (void)hfdl_preamble_sync_process_symbol(&g_pre, a1[k], 0.0f);
            }
            for (k = 0u; k < HFDL_A_LEN; k++) {
                (void)hfdl_preamble_sync_process_symbol(&g_pre, a1[k], 0.0f);
            }
            for (k = 0u; k < HFDL_M1_LEN; k++) {
                (void)hfdl_preamble_sync_process_symbol(&g_pre, m1[k], 0.0f);
            }
            hfdl_preamble_sync_reset(&g_pre);
        }
        b = tic();
        (void)tramo("preambulo CON SENAL, por simbolo     ", a, b,
                    8UL * (2UL * HFDL_A_LEN + HFDL_M1_LEN), SIMB_HFDL);
    }

    /* --- el ecualizador, T/2, 15 tomas ---------------------------- */
    hfdl_equalizer_init(&g_eq, HFDL_EQ_ACTIVE_MU);
    a = tic();
    for (k = 0u; k < 3600u; k++) {
        float yi, yq;
        hfdl_equalizer_push(&g_eq, g_ai[k], g_aq[k]);
        hfdl_equalizer_push(&g_eq, g_aq[k], g_ai[k]);
        hfdl_equalizer_execute(&g_eq, &yi, &yq);
        hfdl_equalizer_step(&g_eq, (yi > 0.0f) ? 1.0f : -1.0f, 0.0f, yi, yq);
    }
    b = tic();
    (void)tramo("ecualizador (2 push+exec+step)/simb  ", a, b, 3600UL, SIMB_HFDL);

    /* --- el demodulador blando ------------------------------------ */
    a = tic();
    for (k = 0u; k < 3600u; k++) {
        uint8_t sb[HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL];
        hfdl_soft_demod(3u, g_ai[k], g_aq[k], sb);
    }
    b = tic();
    (void)tramo("bits blandos 8-PSK, por simbolo      ", a, b, 3600UL, SIMB_HFDL);

    /* --- el descrambler ------------------------------------------- */
    hfdl_descrambler_init(&g_scr);
    a = tic();
    for (k = 0u; k < 20000u; k++) { (void)hfdl_descrambler_advance(&g_scr); }
    b = tic();
    v = ins_de(a, b) / 20000UL;
    pr("    descrambler, por simbolo             "); prn(v); pr(" ins\n");

    /* --- el desentrelazador --------------------------------------- */
    {
        const hfdl_rate_params_t *p = &hfdl_rate_params[2];   /* 1200 bps sencilla */
        uint32_t ncol = (uint32_t)hfdl_deinterleaver_column_cnt(p);
        uint32_t nbits = ncol * HFDL_DEINTERLEAVER_ROW_CNT;

        hfdl_deinterleaver_configure(&g_dei, g_hram + HFDL_RAM_CABECERA, p);
        hfdl_deinterleaver_reset(&g_dei);
        a = tic();
        for (k = 0u; k < nbits; k++) { hfdl_deinterleaver_push(&g_dei, (uint8_t)(k & 0xFFu)); }
        b = tic();
        v = suelto("desentrelazar: meter la rafaga entera", a, b, 1UL);
        pr("      "); prn((unsigned long)nbits); pr(" bits, ");
        prn(ins_de(a, b) / nbits); pr(" ins por bit\n");
        a = tic();
        for (k = 0u; k < nbits; k++) { (void)hfdl_deinterleaver_pop(&g_dei); }
        b = tic();
        (void)suelto("desentrelazar: sacarla entera        ", a, b, 1UL);
    }

    /* --- el Viterbi, que es lo mas caro de HFDL ------------------- */
    {
        static uint8_t simb[2u * 4096u];
        static uint8_t salida[4096u / 8u + 8u];
        static hfdl_viterbi_metric_t ma, mb;
        uint32_t nb;
        /* Tres tamanos: lo que suelta cada giro depende del numero de
         * simbolos de datos, y el coste es LINEAL con los bits. */
        static const uint32_t k_bits[3] = { 540u, 1080u, 2160u };
        static const char *const k_nom[3] = {
            "Viterbi K=7 1/2,  540 bits (giro 0) ",
            "Viterbi K=7 1/2, 1080 bits (giro 1) ",
            "Viterbi K=7 1/2, 2160 bits (giro 2) "
        };
        uint32_t t;

        for (k = 0u; k < sizeof simb; k++) { simb[k] = (uint8_t)(uni() * 255.0f); }
        for (t = 0u; t < 3u; t++) {
            nb = k_bits[t];
            a = tic();
            (void)hfdl_viterbi_init(&g_vit, &ma, &mb,
                                    (hfdl_viterbi_decision_t *)(g_hram + HFDL_RAM_CABECERA),
                                    nb + 8u, 0u);
            (void)hfdl_viterbi_update_block(&g_vit, simb, nb);
            hfdl_viterbi_chainback(&g_vit, salida, nb, 0u);
            b = tic();
            (void)suelto(k_nom[t], a, b, 1UL);
            pr("      "); prn(ins_de(a, b) / nb); pr(" ins por bit decodificado\n");
            if (t == 0u) { g_peor_vit0 = ins_de(a, b); }
            if (t == 2u) { g_peor_vit2 = ins_de(a, b); }
        }
    }

    /* --- el formato de PDU ---------------------------------------- */
    {
        static uint8_t mp[256];
        hfdl_mpdu_header_t h;
        hfdl_spdu_t sp;
        uint32_t nl = 0u;
        static hfdl_lpdu_result_t lp[HFDL_MAX_LPDUS_TOTAL];

        for (k = 0u; k < sizeof mp; k++) { mp[k] = (uint8_t)(uni() * 255.0f); }
        a = tic();
        for (k = 0u; k < 2000u; k++) { (void)hfdl_mpdu_parse_header(mp, sizeof mp, &h); }
        b = tic();
        (void)suelto("PDU: cabecera MPDU                   ", a, b, 2000UL);
        a = tic();
        for (k = 0u; k < 2000u; k++) {
            nl = hfdl_mpdu_extract_lpdus(mp, sizeof mp, &h, lp, HFDL_MAX_LPDUS_TOTAL);
        }
        b = tic();
        (void)suelto("PDU: sacar los LPDU                  ", a, b, 2000UL);
        (void)nl;
        a = tic();
        for (k = 0u; k < 2000u; k++) { (void)hfdl_spdu_parse(mp, sizeof mp, &sp); }
        b = tic();
        (void)suelto("PDU: el SPDU de la estacion          ", a, b, 2000UL);
        a = tic();
        for (k = 0u; k < 2000u; k++) { (void)hfdl_crc16_x25(mp, sizeof mp); }
        b = tic();
        (void)suelto("CRC-16 X.25 de 256 bytes             ", a, b, 2000UL);
    }
}

/* ====================================================================
 * 4. HFDL: EL CAMINO DE LA RADIO
 * ====================================================================
 * hfdl_modo_mete() es lo que corre EN LA INTERRUPCION por cada bloque de
 * audio, y hfdl_modo_poll() lo que corre en el bucle principal al
 * rematar una rafaga. Las dos cosas, con senal y sin ella.
 */
static void hfdl_modo_coste(uint32_t n_sen)
{
    unsigned long a, b, v;
    uint32_t k, nb;

    pr("\n  EL CAMINO DE LA RADIO: hfdl_modo_mete() y hfdl_modo_poll()\n\n");

    /* --- SIN SENAL, el estado mas comun de todos ------------------ */
    hfdl_modo_stop();
    if (!hfdl_modo_start()) { pr("    (no arranca el modo)\n"); return; }

    /*
     * EL PORTERO HAY QUE CEBARLO ANTES DE MEDIR. Los primeros 200
     * bloques pasan TODO porque todavia no sabe donde esta el suelo de
     * ruido (ver porton() en hfdl_modo.c), asi que medir ahi daria el
     * caso abierto haciendolo pasar por el caso cerrado. Se le dan
     * primero 400 bloques de ruido y luego se mide.
     */
    ruido_iq(n_sen, 0.3f);
    for (k = 0u; k + BLOQUE <= n_sen && k < 400u * BLOQUE; k += BLOQUE) {
        hfdl_modo_mete(&g_ai[k], &g_aq[k], (uint32_t)BLOQUE);
    }
    nb = 0u;
    a = tic();
    for (k = 0u; k + BLOQUE <= n_sen; k += BLOQUE) {
        hfdl_modo_mete(&g_ai[k], &g_aq[k], (uint32_t)BLOQUE);
        nb++;
    }
    b = tic();
    v = tramo("SIN SENAL, porton cerrado, por bloque", a, b, nb, BLOQUES_S);
    anota("HFDL  hfdl_modo_mete() SIN SENAL, cerrado",
          fila("SIN SENAL, porton cerrado, por muestra", v / BLOQUE, FS_HZ));

    /*
     * Y EL MISMO RUIDO CON EL PORTERO ABIERTO, que es lo que pasa en el
     * medio segundo de cebado y cada vez que el portero se abre "de
     * puro hartazgo" (ver HFDL_SQ_HARTO_MS). Ahi el ruido entra entero
     * al buscador de preambulo, y se nota.
     */
    hfdl_modo_stop();
    (void)hfdl_modo_start();
    nb = 0u;
    a = tic();
    for (k = 0u; k + BLOQUE <= n_sen && nb < 180u; k += BLOQUE) {
        hfdl_modo_mete(&g_ai[k], &g_aq[k], (uint32_t)BLOQUE);
        nb++;
    }
    b = tic();
    v = tramo("SIN SENAL, porton ABIERTO, por bloque", a, b, nb, BLOQUES_S);
    anota("HFDL  hfdl_modo_mete() SIN SENAL, abierto",
          fila("SIN SENAL, porton abierto, por muestra", v / BLOQUE, FS_HZ));

    /* --- CON SENAL ------------------------------------------------ */
    hfdl_modo_stop();
    (void)hfdl_modo_start();
    (void)rafaga_simbolos(0u, 400u);
    rrc_init();
    {
        uint32_t n = hfdl_audio(0.05f, N_AUD);
        nb = 0u;
        peor_cero();
        a = tic();
        for (k = 0u; k + BLOQUE <= n; k += BLOQUE) {
            unsigned long p1 = tic();
            hfdl_modo_mete(&g_ai[k], &g_aq[k], (uint32_t)BLOQUE);
            peor_mira(p1, tic());
            nb++;
        }
        b = tic();
        v = tramo("CON SENAL, por bloque de 32 muestras ", a, b, nb, BLOQUES_S);
        anota("HFDL  hfdl_modo_mete() CON SENAL         ",
              fila("CON SENAL, por muestra de 12 kHz     ", v / BLOQUE, FS_HZ));
        peor_di("bloque de 32");
        pr("      "); prn((unsigned long)n); pr(" muestras de audio, ");
        prn((unsigned long)g_ns); pr(" simbolos emitidos\n");
    }

    /* El remate: el Viterbi y el formato, fuera de la interrupcion. */
    a = tic();
    hfdl_modo_poll();
    b = tic();
    (void)suelto("hfdl_modo_poll() que remata la rafaga", a, b, 1UL);
    pr("      embudo A1/A2/M1 ");
    {
        uint32_t a1 = 0u, a2 = 0u, m1 = 0u;
        hfdl_modo_embudo(&a1, &a2, &m1);
        prn(a1); pr("/"); prn(a2); pr("/"); prn(m1);
        pr("   rafagas buenas "); prn(hfdl_modo_buenas());
        pr(" de "); prn(hfdl_modo_total()); pr("\n");
    }
}

/*
 * LA RAFAGA ENTERA, DE PRINCIPIO A FIN, metiendo SIMBOLOS.
 *
 * Esto se salta la cadena -que ya se ha medido sola- y mide lo otro: el
 * preambulo, el ecualizador, el segmento de datos y el remate con el
 * Viterbi. Sumado a la cadena da la rafaga completa, y asi se ve cual
 * de las dos mitades manda.
 */
static void hfdl_rafaga_entera(uint32_t giro)
{
    unsigned long a, b, cad, sim, rem;
    uint32_t k, n;

    n = rafaga_simbolos(giro, 0u);
    if (n == 0u) { pr("    (el transmisor no monta el giro)\n"); return; }

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { pr("    (no arranca el modo)\n"); return; }

    a = tic();
    for (k = 0u; k < n; k++) {
        hfdl_modo_simbolo(g_si[k], g_sq[k], g_si[k], g_sq[k]);
    }
    b = tic();
    sim = ins_de(a, b);
    a = tic();
    hfdl_modo_poll();
    b = tic();
    rem = ins_de(a, b);

    /* Y lo que cuesta la cadena para esos mismos simbolos: a 1800
     * baudios, cada simbolo trae 12000/1800 = 6,67 muestras de 12 kHz. */
    cad = (unsigned long)n * (FS_HZ / SIMB_HFDL) * g_cad_ins
        + ((unsigned long)n * (FS_HZ % SIMB_HFDL) * g_cad_ins) / SIMB_HFDL;

    pr("    giro "); prn(giro); pr(" ("); prn(hfdl_rate_params[giro].bitrate_bps);
    pr(" bps, "); prn((unsigned long)n); pr(" simbolos)\n");
    pr("      simbolos (preambulo+ecualizador+segmento)  ");
    prn(sim); pr(" ins = "); prn(sim / 200UL); pr(" us\n");
    pr("      remate (Viterbi, CRC, PDU)                 ");
    prn(rem); pr(" ins = "); prn(rem / 200UL); pr(" us\n");
    {
        unsigned long ms_aire = (n * 1000UL) / SIMB_HFDL;
        unsigned long us_cpu  = (sim + rem) / 200UL;

        pr("      la rafaga dura "); prn(ms_aire);
        pr(" ms de aire y cuesta "); prn(us_cpu); pr(" us de CPU\n");
        /* us de CPU sobre us de aire, en centesimas de tanto por ciento:
         * us_cpu*100*100 / (ms_aire*1000) = us_cpu*10/ms_aire. */
        pr("      o sea el ");
        prd2((us_cpu * 10UL) / ((ms_aire != 0UL) ? ms_aire : 1UL));
        pr(" % del tiempo que dura\n");

        /* Y DE PRINCIPIO A FIN DE VERDAD, o sea con la cadena delante,
         * que es por donde entra el audio en la radio. */
        us_cpu = (sim + rem + cad) / 200UL;
        pr("      con la cadena delante son "); prn(us_cpu);
        pr(" us, el ");
        prd2((us_cpu * 10UL) / ((ms_aire != 0UL) ? ms_aire : 1UL));
        pr(" % del tiempo que dura\n");
    }
}

/* ====================================================================
 * 5. STANAG: EL DETECTOR
 * ==================================================================== */
static float g_trab[12288];

/* El transmisor de 4285 de sim/, que es el inverso exacto del receptor.
 * Se incluye tal cual: no se toca ni una linea de sim/. */
#include "stanag_tx.h"
static stx_t g_tx;
static uint8_t g_bits[4096];

/* Genera `max` muestras de 4285 a `vel` con entrelazado `largo`. */
static uint32_t stanag_audio(int vel, int largo, uint32_t max, float sigma)
{
    uint32_t nb, n, k;

    stx_init(&g_tx, vel, largo);
    g_tx.sin_marcas = 0U;
    nb = stx_mensaje(&g_tx, "DEEPSDR 4285 TEST ", g_bits, sizeof g_bits);
    n = stx_audio(&g_tx, g_bits, nb, g_ai, max);
    if (sigma > 0.0f) {
        for (k = 0u; k < n; k++) { g_ai[k] += gauss() * sigma; }
    }
    return n;
}

static void stanag_detector(uint32_t n_sen)
{
    unsigned long a, b, v;
    uint32_t k, pasos, vale;
    stanag_det_t d;

    pr("\n  STANAG: EL DETECTOR DE TRAMA, stanag_det.c\n");
    pr("    (el % es sobre 12.000 muestras/s reales)\n\n");

    /* --- con senal ------------------------------------------------ */
    if (!stanag_det_init(g_trab, STANAG_DET_FLOATS)) {
        pr("    (no arranca el detector)\n"); return;
    }
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) { stanag_det_mete(&g_ai[k], 64u); }
    b = tic();
    v = tramo("CON SENAL, _mete() por muestra       ", a, b, (n_sen / 64u) * 64UL, FS_HZ);
    (void)v;

    pasos = 0u; vale = 0u;
    peor_cero();
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        unsigned long p1;
        stanag_det_mete(&g_ai[k], 64u);
        p1 = tic();
        if (stanag_det_paso(&d)) { pasos++; if (d.vale) { vale++; } }
        peor_mira(p1, tic());
    }
    b = tic();
    v = tramo("CON SENAL, _mete()+_paso() por muestra", a, b,
              (n_sen / 64u) * 64UL, FS_HZ);
    peor_di("stanag_det_paso()");
    g_peor_det = g_peor;
    pr("      "); prn((unsigned long)pasos); pr(" ventanas, ");
    prn((unsigned long)vale); pr(" con medida;  periodo ");
    prn(d.periodo_us); pr(" decenas de us (10667 = 4285), calidad ");
    prn(d.calidad); pr("\n");
    if (pasos != 0u) {
        /* Las ventanas no son periodicas con el audio: el detector
         * remata una cada tercio de anillo. Se dice lo que vale UNA. */
        unsigned long por_v = ins_de(a, b) / pasos;
        pr("      una ventana de las caras: ");
        prn(por_v); pr(" ins = "); prn(por_v / 200UL); pr(" us\n");
        fila("  y al ritmo al que salen           ", por_v,
             (FS_HZ * (unsigned long)pasos) / ((n_sen / 64u) * 64UL));
    }
    stanag_det_fin();

    /* --- sin senal ------------------------------------------------ */
    ruido_iq(n_sen, 0.3f);
    (void)stanag_det_init(g_trab, STANAG_DET_FLOATS);
    pasos = 0u;
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        stanag_det_mete(&g_ai[k], 64u);
        if (stanag_det_paso(&d)) { pasos++; }
    }
    b = tic();
    (void)tramo("SIN SENAL, _mete()+_paso() por muestra", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);
    pr("      "); prn((unsigned long)pasos);
    pr(" ventanas: las dos transformadas se hacen IGUAL sin senal,\n");
    pr("      porque la autocorrelacion no depende de lo que haya dentro\n");
    stanag_det_fin();
}

/* ====================================================================
 * 6. STANAG: EL RECEPTOR DEL 4285
 * ==================================================================== */
static void stanag_receptor(uint32_t n_sen)
{
    unsigned long a, b, v;
    uint32_t k, pasos;
    stanag_rx_est_t e;

    pr("\n  STANAG: EL RECEPTOR, stanag_rx.c (300 bps, entrelazado corto)\n\n");

    stanag_rx_auto_pon(0U);
    stanag_rx_velocidad_pon(STANAG_RX_300);
    stanag_rx_largo_pon(0U);
    stanag_rx_formato_pon(STANAG_RX_ASCII);
    if (!stanag_rx_init(g_trab, STANAG_DET_FLOATS)) {
        pr("    (no arranca: "); pr(stanag_rx_porque()); pr(")\n");
        return;
    }

    /* _mete() solo: lo unico que corre en la interrupcion. */
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) { stanag_rx_mete(&g_ai[k], 64u); }
    b = tic();
    (void)tramo("CON SENAL, _mete() por muestra       ", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);

    /* Y el molino entero, que es lo que corre en el bucle principal.
     * La primera pasada es la cara: mientras no engancha, barre la
     * portadora por 256 posiciones y once desvios. */
    stanag_rx_fin();
    (void)stanag_rx_init(g_trab, STANAG_DET_FLOATS);
    pasos = 0u;
    peor_cero();
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        unsigned long p1;
        stanag_rx_mete(&g_ai[k], 64u);
        p1 = tic();
        (void)stanag_rx_paso();
        peor_mira(p1, tic());
        pasos++;
    }
    b = tic();
    v = tramo("CON SENAL, de cero (busca y engancha)", a, b,
              (n_sen / 64u) * 64UL, FS_HZ);
    peor_di("stanag_rx_paso() (el barrido de portadora)");
    g_peor_barrido = g_peor;
    (void)v;
    stanag_rx_estado(&e);
    pr("      engancha "); prn(e.engancha);
    pr("  tramas "); prn(e.tramas);
    pr("  grupos "); prn(e.grupos);
    pr("  busquedas "); prn(e.busquedas);
    pr("  corr "); prn(e.corr); pr("\n");
    if (e.busquedas != 0u) {
        pr("      ");
        prn((unsigned long)e.busquedas);
        pr(" barridos de portadora en "); prn(n_sen / 12UL);
        pr(" ms de audio\n");
    }

    /* El mismo molino, ya enganchado: ahora no barre. */
    {
        uint32_t ant = e.busquedas;
        a = tic();
        for (k = 0u; k + 64u <= n_sen; k += 64u) {
            stanag_rx_mete(&g_ai[k], 64u);
            (void)stanag_rx_paso();
        }
        b = tic();
        (void)tramo("CON SENAL, ya enganchado, por muestra", a, b,
                    (n_sen / 64u) * 64UL, FS_HZ);
        stanag_rx_estado(&e);
        pr("      barridos nuevos "); prn((unsigned long)(e.busquedas - ant));
        pr("   tramas "); prn(e.tramas); pr("\n");
    }
    stanag_rx_fin();

    /* --- y sin senal, que es el caso que de verdad manda ---------- */
    ruido_iq(n_sen, 0.3f);
    (void)stanag_rx_init(g_trab, STANAG_DET_FLOATS);
    peor_cero();
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        unsigned long p1;
        stanag_rx_mete(&g_ai[k], 64u);
        p1 = tic();
        (void)stanag_rx_paso();
        peor_mira(p1, tic());
    }
    b = tic();
    (void)tramo("SIN SENAL, _mete()+_paso() por muestra", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);
    peor_di("stanag_rx_paso()");
    stanag_rx_estado(&e);
    pr("      busquedas "); prn(e.busquedas);
    pr("  engancha "); prn(e.engancha);
    pr("  corr "); prn(e.corr); pr("\n");
    pr("      Sin senal no engancha, asi que el molino no para nunca:\n");
    pr("      cuesta MAS que con senal buena, y ese es el caso comun\n");
    stanag_rx_fin();
}

/* ====================================================================
 * 7. STANAG: EL 4415
 * ==================================================================== */
/*
 * LO QUE AQUI NO SE PUEDE MEDIR, Y QUE LO BLOQUEA.
 *
 * El 4415 se mide SIN SENAL y con un 4285 delante, no con un 4415 de
 * verdad enganchado. El motivo es que no hay generador de 4415 que se
 * pueda usar: el unico que existe en el arbol esta DENTRO de
 * sim/s4415test.c, que es un programa con su propio main(), y sim/ no se
 * toca. sim/stanag_tx.h si es una cabecera y por eso el 4285 si se mide
 * con senal de verdad.
 *
 * Lo que si dice el numero de abajo: lo que cuesta el 4415 MIENTRAS BUSCA,
 * que es lo que hace el 99 % del tiempo que esta puesto. El caso
 * enganchado costara algo mas -hay que desensanchar y decodificar- pero
 * el correlador de 288 muestras de plantilla, que es el grueso, corre
 * igual en los dos casos.
 */
static void stanag_4415(uint32_t n_sen)
{
    unsigned long a, b;
    uint32_t k;
    s4415_est_t e;

    pr("\n  STANAG 4415: 75 bps, cada dibit ensanchado en 32 simbolos\n\n");

    if (!s4415_init(g_trab, S4415_FLOATS)) {
        pr("    (no arranca: pide "); prn(s4415_ram_bytes()); pr(" bytes)\n");
        return;
    }
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) { s4415_mete(&g_ai[k], 64u); }
    b = tic();
    (void)tramo("CON UN 4285 delante, _mete() x muestra", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);
    peor_cero();
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        unsigned long p1;
        s4415_mete(&g_ai[k], 64u);
        p1 = tic();
        (void)s4415_paso();
        peor_mira(p1, tic());
    }
    b = tic();
    (void)tramo("CON UN 4285 delante, _mete()+_paso()  ", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);
    peor_di("s4415_paso()");
    s4415_estado(&e);
    pr("      tiradas "); prn(s4415_tiradas());
    pr("   perdidas "); prn(s4415_perdidas()); pr("\n");
    s4415_fin();

    ruido_iq(n_sen, 0.3f);
    (void)s4415_init(g_trab, S4415_FLOATS);
    peor_cero();
    a = tic();
    for (k = 0u; k + 64u <= n_sen; k += 64u) {
        unsigned long p1;
        s4415_mete(&g_ai[k], 64u);
        p1 = tic();
        (void)s4415_paso();
        peor_mira(p1, tic());
    }
    b = tic();
    (void)tramo("SIN SENAL, _mete()+_paso() por muestra", a, b,
                (n_sen / 64u) * 64UL, FS_HZ);
    peor_di("s4415_paso()");
    s4415_fin();
}

/* ====================================================================
 * 8. STANAG: EL CAMINO DE LA RADIO
 * ====================================================================
 *
 * Esto es lo que de verdad llama la radio: demod_am.c:6442 mete el audio
 * de banda lateral por stanag_modo_mete() en bloques de
 * s_dec_block_samples (32 o 64 muestras a 12 kHz) y main.c:3464 llama a
 * stanag_modo_poll() en cada vuelta del bucle principal. El boton de
 * forma decide cual de los tres oficios corre, y los tres se reparten la
 * misma RAM prestada, asi que nunca hay dos a la vez.
 */
static void stanag_modo_coste(const char *nombre, uint8_t forma,
                              uint32_t n_sen, const char *resu)
{
    unsigned long a, b;
    uint32_t k;

    stanag_modo_stop();
    stanag_modo_forma_pon(forma);
    if (!stanag_modo_start()) {
        pr("    "); pr(nombre); pr(": no arranca\n"); return;
    }
    peor_cero();
    a = tic();
    for (k = 0u; k + BLOQUE <= n_sen; k += BLOQUE) {
        unsigned long p1;
        stanag_modo_mete(&g_ai[k], (uint16_t)BLOQUE);
        p1 = tic();
        stanag_modo_poll();
        peor_mira(p1, tic());
    }
    b = tic();
    (void)tramo(nombre, a, b, (n_sen / BLOQUE) * BLOQUE, FS_HZ);
    anota(resu, g_ult_us);
    peor_di("stanag_modo_poll()");
    pr("      "); pr(stanag_modo_estado_txt()); pr("\n");
    stanag_modo_stop();
}

/* ==================================================================== */

int main(void)
{
    uint32_t n;

    /*
     * SIN LA TRAMPA DE ACCESOS DESALINEADOS, y hay que decir por que.
     *
     * start.c pone UNALIGN_TRP para cazar nuestros fallos, y aqui salta
     * en el memcpy/memmove de newlib que usan estos modulos: la rutina
     * de la biblioteca hace lecturas de palabra sin alinear A PROPOSITO,
     * porque el Cortex-M4 las permite. Es legal y no es un fallo
     * nuestro.
     *
     * La radio NO pone ese bit -lo pone este banco-, asi que apagarlo
     * aqui no tapa nada que pase en la radio. Es lo mismo que hace
     * m4_ft8.c y por el mismo motivo.
     */
    SCB_CCR &= ~(1UL << 3);

    reloj_on();
    pr("\n");
    pr("======================================================================\n");
    pr("LO QUE CUESTAN STANAG Y HFDL EN UN CORTEX-M4 DE VERDAD\n");
    pr("======================================================================\n");
    pr("  1 tic = 40 instrucciones.  200 MHz.  La radio tarda ~1,7 veces esto.\n");
    pr("  Audio: 12 kHz para las dos cadenas (complejo en HFDL, real en 4285).\n");

    /* ---------------- HFDL ---------------- */
    pr("\n======================  H F D L  ======================\n");
    rrc_init();
    (void)rafaga_simbolos(0u, 0u);
    n = hfdl_audio(0.05f, N_AUD);

    hfdl_piezas();
    hfdl_cadena(n);
    hfdl_despues();
    hfdl_modo_coste(n);

    pr("\n  LA RAFAGA ENTERA, DE PRINCIPIO A FIN (por simbolos)\n\n");
    hfdl_rafaga_entera(0u);
    hfdl_rafaga_entera(2u);
    hfdl_modo_stop();

    /* ---------------- STANAG ---------------- */
    pr("\n=====================  S T A N A G  =====================\n");
    n = stanag_audio(2, 0, N_AUD, 0.05f);
    pr("\n  (");
    prn((unsigned long)n); pr(" muestras de 4285 a 300 bps = ");
    prn(n / 12UL); pr(" ms de audio)\n");

    stanag_detector(n);
    /* El audio se ha machacado con ruido dentro: se vuelve a generar. */
    n = stanag_audio(2, 0, N_AUD, 0.05f);
    stanag_receptor(n);
    n = stanag_audio(2, 0, N_AUD, 0.05f);
    stanag_4415(n);

    pr("\n  EL CAMINO DE LA RADIO: stanag_modo_mete() + stanag_modo_poll()\n");
    pr("    (bloques de 32 muestras, un poll por bloque, como el bucle real)\n\n");
    n = stanag_audio(2, 0, N_AUD, 0.05f);
    stanag_modo_coste("AUTO (identificar) CON SENAL         ", STANAG_AUTO, n,
                      "4285  stanag_modo AUTO    CON SENAL      ");
    stanag_modo_coste("S4285 (demodular)  CON SENAL         ", STANAG_4285, n,
                      "4285  stanag_modo S4285   CON SENAL      ");
    stanag_modo_coste("S4415              CON UN 4285       ", STANAG_4415, n,
                      "4415  stanag_modo S4415   CON UN 4285    ");
    ruido_iq(n, 0.3f);
    stanag_modo_coste("AUTO (identificar) SIN SENAL         ", STANAG_AUTO, n,
                      "4285  stanag_modo AUTO    SIN SENAL      ");
    stanag_modo_coste("S4285 (demodular)  SIN SENAL         ", STANAG_4285, n,
                      "4285  stanag_modo S4285   SIN SENAL      ");
    stanag_modo_coste("S4415              SIN SENAL         ", STANAG_4415, n,
                      "4415  stanag_modo S4415   SIN SENAL      ");

    resumen();

    pr("\n");
    pila_di(0UL);
    pr("\n  (este banco no tiene topes: es una medida, no un guardian)\n\n");
    salir();
    return 0;
}

/*
 * LO QUE CUESTA EL CAMINO DE AUDIO Y LOS DEMODULADORES, EN INSTRUCCIONES.
 *
 * POR QUE ESTE Y NO EL QUE YA HABIA. m4_audio.c mide tres piezas sueltas
 * -el reductor de ruido, el mezclador del NCO y el corrector de espejo- y
 * se para ahi. Lo que de verdad se lleva el segundo no son esas tres: es
 * demod_am_process_raw() entera, que corre una vez por bloque dentro de la
 * interrupcion del DMA y arrastra detras el filtro de canal, el Hilbert,
 * el diezmador, el lazo del SAM, el discriminador y el automatico de
 * ganancia. Y no cuesta lo mismo en AM que en banda lateral: son caminos
 * distintos, con etapas distintas, y hasta ahora nadie los habia pesado
 * uno al lado del otro en la CPU de verdad.
 *
 * COMO SE LEE. El reloj es el SysTick del M4 emulado con -icount shift=0:
 * un tic son 40 instrucciones (ver m4/LEEME.md). Los bloques de audio
 * llegan a 375 por segundo -96.000/256 en AM, banda lateral y FM
 * estrecha; 192.000/512 en FM ancha, que da el mismo 375-, asi que el
 * tanto por ciento de cada segundo es instrucciones x 375 / 200.000.000.
 * En la radio de verdad lo medido se multiplica por ~1,7, que es el factor
 * calibrado del LEEME.
 *
 * COMO SE MIDE. Entrada de verdad: una portadora en la frecuencia
 * intermedia baja con envolvente al 40 %, modulacion de fase para que la
 * rama de FM tenga desviacion que discriminar, ruido encima y dos
 * chasquidos para el quitapulsos. Nada de ceros: un demodulador en
 * silencio no hace el mismo trabajo -el lazo del SAM no se mueve, el
 * automatico de ganancia no relaja y el silenciador corta-. Y antes de
 * cada medida van unas pasadas en vacio, para que los lazos y los
 * estimadores esten donde van a estar los 375 bloques del segundo
 * siguiente y no arrancando.
 *
 * LO QUE AQUI NO SE MIDE, dicho claro:
 *
 *   - El reductor de ruido (nr_ss.c). Ya lo pesa m4_audio.c y repetirlo
 *     seria dar el mismo numero dos veces. Por eso los modos se miden con
 *     el reductor APAGADO: lo que sale es el camino, y lo que cuesta
 *     encenderlo esta en el otro banco.
 *   - Los veintitantos decodificadores a los que demod_am.c va dejando
 *     copias. Aqui estan todos apagados y en su sitio hay tapones vacios,
 *     abajo del todo. Cada uno tiene su propio banco.
 *   - El bus del I2S. El tapon se queda con el puntero y vuelve.
 *
 * Esto no es un vigilante: no pone topes ni se pone rojo. Es una MEDIDA.
 */
#include "semi.h"

#include <math.h>

#include "arm_math.h"

#include "demod_am.h"
#include "sdr_rx.h"
#include "sam.h"
#include "disc_fm.h"
#include "semibanda.h"
#include "agc.h"
#include "audiofil.h"
#include "anotch.h"
#include "nb.h"
#include "rapido.h"
#include "nr_ss.h"
#include "dcf77.h"

int g_fallos;

void pila_di(unsigned long maximo);

/* ------------------------------------------------------------------ */
/* LA ENTRADA, QUE TIENE QUE PARECERSE A UNA SEÑAL                     */

#define N96   ((uint32_t)SDR_RX_BLOCK_SAMPLES)      /* 256 a 96 kHz  */
#define N192  ((uint32_t)SDR_RX_BLOCK_SAMPLES_WFM)  /* 512 a 192 kHz */
#define NDEC  32U                                   /* el bloque diezmado a 12 kHz */

static int16_t s_raw[2U * N192];

static float s_fi[N192];
static float s_fq[N192];
static float s_env[N192];
static int16_t s_est[2U * N192];
static float s_cuad[N192];
static float s_dec[NDEC];

static unsigned long s_sem = 0x1234567UL;

static float ruidito(void)
{
    s_sem = s_sem * 1103515245UL + 12345UL;
    return (float)((long)((s_sem >> 16) & 0xFFFFUL) - 32768) * (1.0f / 32768.0f);
}

/*
 * Un bloque que se puede repetir sin costura: la portadora y la modulacion
 * llevan un numero entero de vueltas dentro del bloque, asi que al volver
 * a empezar no hay un salto de fase que le de al lazo del SAM un trabajo
 * que en la radio no tendria.
 */
static void genera(uint32_t n, float fs_hz, float port_hz)
{
    uint32_t k;
    float dw = 6.28318531f * port_hz / fs_hz;
    float wm = 6.28318531f / (float)n;   /* una vuelta de modulacion por bloque */

    for (k = 0U; k < n; k++) {
        float m   = sinf(wm * (float)k);
        float env = 6000.0f * (0.6f + 0.4f * m);
        float ph  = dw * (float)k + 3.0f * m;   /* la desviacion para la rama de FM */
        float i   = env * cosf(ph) + 250.0f * ruidito();
        float q   = env * sinf(ph) + 250.0f * ruidito();

        s_raw[2U * k]      = (int16_t)i;
        s_raw[2U * k + 1U] = (int16_t)q;
    }
    /* Y dos chasquidos, que es lo unico que el quitapulsos viene a cazar. */
    s_raw[2U * 37U]        = (int16_t)(s_raw[2U * 37U] + 14000);
    s_raw[2U * 37U + 1U]   = (int16_t)(s_raw[2U * 37U + 1U] - 12000);
    s_raw[2U * 150U]       = (int16_t)(s_raw[2U * 150U] - 13000);
    s_raw[2U * 150U + 1U]  = (int16_t)(s_raw[2U * 150U + 1U] + 14000);
}

/* Lo mismo pero ya en flotante, para las piezas que comen float. */
static void genera_f(uint32_t n)
{
    uint32_t k;
    float wm = 6.28318531f / (float)n;

    for (k = 0U; k < n; k++) {
        float m  = sinf(wm * (float)k);
        float ph = 1.5707963f * (float)k + 3.0f * m;

        s_fi[k]  = (0.6f + 0.4f * m) * cosf(ph) + 0.02f * ruidito();
        s_fq[k]  = (0.6f + 0.4f * m) * sinf(ph) + 0.02f * ruidito();
        s_env[k] = 8000.0f * (0.6f + 0.4f * m) + 300.0f * ruidito();
    }
    s_fi[37] += 4.0f;
    s_fq[150] -= 4.0f;
}

/* ------------------------------------------------------------------ */

/* El tanto por ciento de un segundo que se lleva algo que corre una vez
 * por bloque de audio. 375 bloques por segundo a 200 MHz. */
static void porciento(unsigned long ins)
{
    unsigned long milesimas = (ins * 375UL) / 200000UL;   /* por mil */

    pr("      se lleva ");
    prn(milesimas / 10UL); pr(",");
    prn(milesimas % 10UL);
    pr(" % de cada segundo\n");
}

/* ------------------------------------------------------------------ */
/* LOS MODOS, DE PRINCIPIO A FIN                                       */

#define CALIENTA 20
#define VUELTAS  60

static void un_modo(const char *nombre, demod_mode_t modo)
{
    unsigned long a, b, v;
    int r;

    demod_am_init();
    demod_am_set_active_rate(0U);      /* 96 kHz */
    demod_am_set_rf_doble(0U);
    demod_am_set_mode(modo);
    demod_am_set_mix_hz((float)DEMOD_IF_OFFSET_HZ);

    /* El reductor, el notch y el quitapulsos, apagados: lo que se pesa
     * aqui es el camino, y cada uno de esos tres se pesa aparte. */
    nr_ss_set_enabled(0U);
    anotch_set_activo(0U);
    nb_set_nivel(0U);

    genera(N96, 96000.0f, (float)DEMOD_IF_OFFSET_HZ);

    for (r = 0; r < CALIENTA; r++) { demod_am_process_raw(s_raw); }
    a = tic();
    for (r = 0; r < VUELTAS; r++) { demod_am_process_raw(s_raw); }
    b = tic();
    v = mide(nombre, a, b, (unsigned long)VUELTAS);
    porciento(v);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    unsigned long a, b, v;
    int r;
    uint32_t k;

    reloj_on();

    pr("\nEL CAMINO DE AUDIO Y LOS DEMODULADORES, EN UN CORTEX-M4 DE VERDAD\n");
    pr("\n  LOS MODOS ENTEROS, bloque de 256 muestras a 96 kHz\n");
    pr("  (demod_am_process_raw: del I/Q crudo al bloque de audio)\n\n");

    un_modo("AM                             ", DEMOD_MODE_AM);
    un_modo("SAM                            ", DEMOD_MODE_SAM);
    un_modo("USB                            ", DEMOD_MODE_USB);
    un_modo("LSB                            ", DEMOD_MODE_LSB);
    un_modo("NFM                            ", DEMOD_MODE_NFM);

    /*
     * LA FM ANCHA VA POR SU PROPIA PUERTA Y A SU PROPIA TASA: 512 muestras
     * a 192 kHz, que son los mismos 375 bloques por segundo, asi que el
     * tanto por ciento se compara con los de arriba tal cual.
     */
    pr("\n  LA FM ANCHA, bloque de 512 muestras a 192 kHz\n");
    pr("  (demod_wfm_process_raw: camino aparte, mismos 375 bloques/s)\n\n");
    {
        demod_am_init();
        demod_am_set_active_rate(0U);
        demod_am_set_rf_doble(1U);
        demod_am_set_mode(DEMOD_MODE_WFM);
        demod_am_set_mix_hz(0.0f);
        nr_ss_set_enabled(0U);
        anotch_set_activo(0U);
        nb_set_nivel(0U);

        genera(N192, 192000.0f, 0.0f);

        for (r = 0; r < CALIENTA; r++) { demod_wfm_process_raw(s_raw); }
        a = tic();
        for (r = 0; r < VUELTAS; r++) { demod_wfm_process_raw(s_raw); }
        b = tic();
        v = mide("WFM                            ", a, b, (unsigned long)VUELTAS);
        porciento(v);
    }

    /* ---------------------------------------------------------------- */
    pr("\n  LAS PIEZAS, UNA A UNA\n\n");

    genera_f(N192);

    /*
     * EL QUITAPULSOS. Va delante del diezmador y ve el bloque crudo
     * entero: 256 muestras a 96 kHz, 512 a 192. Se mide apagado y en
     * medio, que es lo que cuesta de verdad la diferencia de encenderlo.
     */
    {
        nb_set_nivel(0U);
        nb_reset();
        for (r = 0; r < 5; r++) { (void)nb_process(s_fi, s_fq, N96); }
        a = tic();
        for (r = 0; r < 120; r++) { (void)nb_process(s_fi, s_fq, N96); }
        b = tic();
        v = mide("nb apagado, 256 muestras       ", a, b, 120UL);
        porciento(v);

        nb_set_nivel(2U);
        nb_reset();
        for (r = 0; r < 20; r++) { (void)nb_process(s_fi, s_fq, N96); }
        a = tic();
        for (r = 0; r < 120; r++) { (void)nb_process(s_fi, s_fq, N96); }
        b = tic();
        v = mide("nb nivel 2, 256 muestras       ", a, b, 120UL);
        porciento(v);

        nb_reset();
        for (r = 0; r < 20; r++) { (void)nb_process(s_fi, s_fq, N192); }
        a = tic();
        for (r = 0; r < 120; r++) { (void)nb_process(s_fi, s_fq, N192); }
        b = tic();
        v = mide("nb nivel 2, 512 muestras       ", a, b, 120UL);
        porciento(v);
        nb_set_nivel(0U);
    }

    /*
     * EL DIEZMADOR DE MEDIO BANDA Y EL INTERPOLADOR. Solo corren con la
     * radiofrecuencia a 192 kHz: el diezmador deja 512 en 256 antes de
     * demodular, y el interpolador devuelve las 256 de audio a 512 para
     * el conversor.
     */
    {
        semibanda_reset();
        for (r = 0; r < 5; r++) { semibanda_diezma(s_fi, s_fq, N96); }
        a = tic();
        for (r = 0; r < 120; r++) { semibanda_diezma(s_fi, s_fq, N96); }
        b = tic();
        v = mide("semibanda_diezma, 512 -> 256   ", a, b, 120UL);
        porciento(v);

        for (k = 0U; k < 2U * N96; k++) { s_est[k] = (int16_t)(1000.0f * ruidito()); }
        semibanda_reset();
        for (r = 0; r < 5; r++) { semibanda_interpola(s_est, N96); }
        a = tic();
        for (r = 0; r < 120; r++) { semibanda_interpola(s_est, N96); }
        b = tic();
        v = mide("semibanda_interpola, 256 -> 512", a, b, 120UL);
        porciento(v);
    }

    /*
     * EL LAZO DEL SAM. Dos formas: la de siempre -las dos bandas, que es
     * sam_bloque()- y la de una sola banda, que saca ademas la cuadratura
     * para el metodo de fases. 256 muestras a 96 kHz las dos.
     */
    {
        static sam_t s;

        genera_f(N96);
        sam_init(&s, 96000.0f, 250.0f, 100.0f, 65.0f / 75.0f);
        for (r = 0; r < 20; r++) {
            sam_bloque(&s, s_fi, s_fq, s_env, N96, 96000.0f);
        }
        a = tic();
        for (r = 0; r < 120; r++) {
            sam_bloque(&s, s_fi, s_fq, s_env, N96, 96000.0f);
        }
        b = tic();
        v = mide("sam_bloque, 256 muestras       ", a, b, 120UL);
        porciento(v);

        sam_init(&s, 96000.0f, 250.0f, 100.0f, 65.0f / 75.0f);
        for (r = 0; r < 20; r++) {
            sam_bloque_cuad_mez(&s, s_fi, s_fq, s_env, s_cuad, N96,
                                96000.0f, 1.0f);
        }
        a = tic();
        for (r = 0; r < 120; r++) {
            sam_bloque_cuad_mez(&s, s_fi, s_fq, s_env, s_cuad, N96,
                                96000.0f, 1.0f);
        }
        b = tic();
        v = mide("sam_bloque_cuad_mez, 256       ", a, b, 120UL);
        porciento(v);
    }

    /*
     * EL AUTOMATICO DE GANANCIA, que es lo ultimo del camino: coge el
     * audio en flotante y lo deja en estereo de 16 bits para el I2S.
     */
    {
        for (r = 0; r < 5; r++) {
            (void)agc_bloque(s_env, s_est, N96, 18000.0f, 0.99994048f, 40.0f,
                             DEMOD_AM_GAIN);
        }
        a = tic();
        for (r = 0; r < 120; r++) {
            (void)agc_bloque(s_env, s_est, N96, 18000.0f, 0.99994048f, 40.0f,
                             DEMOD_AM_GAIN);
        }
        b = tic();
        v = mide("agc_bloque, 256 muestras       ", a, b, 120UL);
        porciento(v);

        a = tic();
        for (r = 0; r < 120; r++) { agc_manual(s_env, s_est, N96, DEMOD_AM_GAIN); }
        b = tic();
        v = mide("agc_manual, 256 muestras       ", a, b, 120UL);
        porciento(v);
    }

    /*
     * EL NOTCH AUTOMATICO. No corre a 96 kHz sino en el camino diezmado de
     * 12, o sea 32 muestras por bloque, que es lo que hay que comparar con
     * todo lo de arriba.
     */
    {
        for (k = 0U; k < NDEC; k++) {
            s_dec[k] = 0.4f * sinf(0.9f * (float)k) + 0.1f * ruidito();
        }
        anotch_init(DEMOD_DEC_FS_HZ);
        anotch_set_activo(0U);
        a = tic();
        for (r = 0; r < 120; r++) { anotch_process(s_dec, NDEC); }
        b = tic();
        v = mide("anotch apagado, 32 muestras    ", a, b, 120UL);
        porciento(v);

        anotch_set_activo(1U);
        anotch_set_fuerza(2U);
        for (r = 0; r < 120; r++) { anotch_process(s_dec, NDEC); }
        a = tic();
        for (r = 0; r < 120; r++) { anotch_process(s_dec, NDEC); }
        b = tic();
        v = mide("anotch fuerza 2, 32 muestras   ", a, b, 120UL);
        porciento(v);

        anotch_set_fuerza(3U);
        for (r = 0; r < 120; r++) { anotch_process(s_dec, NDEC); }
        a = tic();
        for (r = 0; r < 120; r++) { anotch_process(s_dec, NDEC); }
        b = tic();
        v = mide("anotch fuerza 3, 32 muestras   ", a, b, 120UL);
        porciento(v);
        anotch_set_activo(0U);
    }

    /*
     * EL FILTRO DE AUDIO. Y aqui hay que leer con cuidado: audiofil.c NO
     * corre por bloque. Lo que hace es DISEÑAR los coeficientes cuando se
     * mueve un corte, y de ahi en adelante quien filtra es el biquad del
     * CMSIS. O sea que este numero no se multiplica por 375: se paga una
     * vez por cada toque del mando.
     */
    {
        float coef[AUDIOFIL_COEFS];
        float lo, hi;

        a = tic();
        for (r = 0; r < 20; r++) {
            audiofil_disena(150.0f + (float)r, 2800.0f, 12000.0f, coef, &lo, &hi);
        }
        b = tic();
        (void)mide("audiofil_disena (NO por bloque)", a, b, 20UL);
        pr("      una vez por toque del mando, no 375 veces por segundo\n");

        /*
         * Lo que SI corre por bloque es la cascada que sale de ahi: tres
         * biquads del CMSIS sobre las 32 muestras del camino diezmado.
         * Ese es el coste de verdad del filtro de audio.
         */
        {
            static arm_biquad_casd_df1_inst_f32 inst;
            static float32_t est[4 * AUDIOFIL_ETAPAS];

            audiofil_disena(200.0f, 2800.0f, DEMOD_DEC_FS_HZ, coef, &lo, &hi);
            arm_biquad_cascade_df1_init_f32(&inst, (uint8_t)AUDIOFIL_ETAPAS,
                                            coef, est);
            for (r = 0; r < 10; r++) {
                arm_biquad_cascade_df1_f32(&inst, s_dec, s_env, NDEC);
            }
            a = tic();
            for (r = 0; r < 120; r++) {
                arm_biquad_cascade_df1_f32(&inst, s_dec, s_env, NDEC);
            }
            b = tic();
            v = mide("filtro de audio, 3 biquads x32 ", a, b, 120UL);
            porciento(v);
        }

        /*
         * Y EL FILTRO DE CANAL, que es la otra cascada y la cara: dos
         * biquads, pero sobre las 256 muestras crudas Y sobre los dos
         * railes, I y Q. Ocho veces mas muestras que el de audio.
         */
        {
            static arm_biquad_casd_df1_inst_f32 ii, qq;
            static float32_t ei[8], eq[8];
            static const float32_t chf[10] = {
                0.00094469f, 0.00188938f, 0.00094469f, 1.91119707f, -0.91497583f,
                1.00000000f, 2.00000000f, 1.00000000f, 1.97917474f, -0.98295375f
            };

            arm_biquad_cascade_df1_init_f32(&ii, 2U, (float32_t *)chf, ei);
            arm_biquad_cascade_df1_init_f32(&qq, 2U, (float32_t *)chf, eq);
            for (r = 0; r < 10; r++) {
                arm_biquad_cascade_df1_f32(&ii, s_fi, s_env, N96);
                arm_biquad_cascade_df1_f32(&qq, s_fq, s_cuad, N96);
            }
            a = tic();
            for (r = 0; r < 120; r++) {
                arm_biquad_cascade_df1_f32(&ii, s_fi, s_env, N96);
                arm_biquad_cascade_df1_f32(&qq, s_fq, s_cuad, N96);
            }
            b = tic();
            v = mide("filtro de canal, 2 biquads I/Q ", a, b, 120UL);
            porciento(v);
        }
    }

    /*
     * EL DISCRIMINADOR DE FM DE SSTV Y WEFAX. Tampoco esta en el camino de
     * audio de siempre: lo llaman sstv.c y wefax.c sobre el audio ya
     * diezmado a 12 kHz, muestra a muestra, y solo con esos modos puestos.
     * Se pesa por bloque diezmado -32 muestras- para poder compararlo.
     */
    {
        static disc_fm_t d;

        disc_fm_init(&d, 1900.0f, DEMOD_DEC_FS_HZ);
        for (r = 0; r < 10; r++) {
            for (k = 0U; k < NDEC; k++) { (void)disc_fm_paso(&d, s_dec[k]); }
        }
        a = tic();
        for (r = 0; r < 120; r++) {
            for (k = 0U; k < NDEC; k++) { (void)disc_fm_paso(&d, s_dec[k]); }
        }
        b = tic();
        v = mide("disc_fm_paso x32 (SSTV/WEFAX)  ", a, b, 120UL);
        porciento(v);
    }

    /*
     * Y EL ARCOTANGENTE RAPIDO, que es la pieza que hace barato todo lo
     * que lleva fase: el discriminador de FM estrecha lo llama una vez por
     * muestra. Se mide en tandas de 256 llamadas, que es lo que se paga en
     * un bloque de FM.
     */
    {
        volatile float acc = 0.0f;

        a = tic();
        for (r = 0; r < 120; r++) {
            float s = 0.0f;

            for (k = 0U; k < N96; k++) { s += rapido_atan2(s_fq[k], s_fi[k]); }
            acc = s;
        }
        b = tic();
        v = mide("rapido_atan2 x256              ", a, b, 120UL);
        porciento(v);
        (void)acc;
    }

    pr("\n");
    pila_di(0UL);
    pr("\n  (medida, no vigilante: aqui no hay topes)\n\n");
    salir();
    return 0;
}

/* ====================================================================== */
/* LOS TAPONES                                                            */
/*                                                                        */
/* demod_am.c reparte: va dejando copias del audio y de la I/Q a los       */
/* veintitantos decodificadores del firmware, y al final empuja el bloque  */
/* al I2S. Enlazarlos de verdad seria arrastrar medio arbol -pantallas,    */
/* ajustes, la flash- y ademas mediria OTRA cosa: cada decodificador tiene */
/* su propio banco. Aqui estan todos apagados y vacios, que es lo que deja */
/* a la vista el coste del camino y nada mas.                             */
/*                                                                        */
/* Todos contestan que NO estan activos, asi que demod_am.c ni siquiera    */
/* llega a llamar a las funciones que les dan muestras. Estan definidas    */
/* porque el enlazador las pide, no porque se usen.                       */
/* ====================================================================== */

#define TAPON32(ACT, MET) \
    uint8_t ACT(void) { return 0U; } \
    void MET(const float *a, uint32_t n) { (void)a; (void)n; }
#define TAPON16(ACT, MET) \
    uint8_t ACT(void) { return 0U; } \
    void MET(const float *a, uint16_t n) { (void)a; (void)n; }

TAPON16(ais_modo_activo,    ais_modo_mete)
TAPON16(ale_modo_activo,    ale_modo_mete)
TAPON32(analiz_activo,      analiz_feed)
TAPON32(ax25_activo,        ax25_process)
TAPON32(dsc_activo,         dsc_process)
TAPON16(ft8_modo_activo,    ft8_modo_come)
TAPON32(hell_activo,        hell_process)
TAPON16(ident_modo_activo,  ident_modo_mete)
TAPON16(jtty_modo_activo,   jtty_modo_mete)
TAPON32(navtex_activo,      navtex_process)
TAPON32(psk31_activo,       psk31_process)
TAPON32(rds_activo,         rds_feed)
TAPON32(sstv_activo,        sstv_process)
TAPON16(stanag_modo_activo, stanag_modo_mete)
TAPON32(wefax_activo,       wefax_process)
TAPON16(wspr_modo_activo,   wspr_modo_mete)

uint8_t cw_get_enabled(void) { return 0U; }
void    cw_init(void) { }
void    cw_process(const float *a, uint32_t n) { (void)a; (void)n; }
void    cw_set_pitch_hz(float hz) { (void)hz; }
void    cw_set_wpm_hint(float w) { (void)w; }

uint8_t rtty_get_enabled(void) { return 0U; }
void    rtty_init(void) { }
void    rtty_process(const float *a, uint32_t n) { (void)a; (void)n; }
void    rtty_scope_init(void) { }
void    rtty_scope_feed(const float *a, uint32_t n) { (void)a; (void)n; }

uint8_t hfdl_modo_activo(void) { return 0U; }
void    hfdl_modo_mete(const float *i_in, const float *q_in, uint32_t n)
{ (void)i_in; (void)q_in; (void)n; }

uint8_t dcf77_activo(void) { return 0U; }
void    dcf77_feed(float m) { (void)m; }
float   dcf77_fase_iq(float i, float q) { (void)i; (void)q; return 0.0f; }
uint8_t dcf77_usa_fase(hora_emisora_t e) { (void)e; return 0U; }
uint8_t dcf77_submuestras(hora_emisora_t e) { (void)e; return 1U; }
hora_emisora_t dcf77_emisora(void) { return (hora_emisora_t)0; }

/*
 * EL SUMIDERO DE AUDIO. En la radio esto copia el bloque a la mitad que
 * toque del buffer del DMA del I2S; aqui se queda con el puntero y vuelve.
 * Copiarlo seria medir un memcpy que en la radio hace el mismo trabajo
 * pero a otra memoria, y no es lo que se viene a pesar.
 */
const int16_t *g_ultimo_audio;
void gd32_i2s_stream_write_half(const int16_t *stereo_frames, uint32_t n_frames)
{ g_ultimo_audio = stereo_frames; (void)n_frames; }

void bip_mete(int16_t *estereo, uint32_t n, float fs_hz)
{ (void)estereo; (void)n; (void)fs_hz; }

/* Lo decide sdr_rx.c mirando el DMA; aqui nunca hay bloques rotos. */
uint8_t sdr_rx_last_block_corrupted(void) { return 0U; }

/* El contador de ciclos de mentira, declarado en hostinc/gd32f4xx.h: aqui
 * no hay DWT. demod_am.c lo lee al entrar y al salir del bloque para
 * publicar demod_am_get_last_cycles(), que este banco no usa -lo que vale
 * es el SysTick, que SI avanza con las instrucciones-. */
typedef struct { uint32_t CYCCNT; uint32_t CTRL; uint32_t LAR; } dwt_falso_t;
dwt_falso_t dwt_falso;

/* Las patillas y el reloj de los perifericos: aqui no hay ni lo uno ni lo
 * otro. demod_am.c mueve una patilla de medida al entrar y salir. */
void gpio_bit_set(uint32_t port, uint32_t pin) { (void)port; (void)pin; }
void gpio_bit_reset(uint32_t port, uint32_t pin) { (void)port; (void)pin; }
void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pin)
{ (void)port; (void)mode; (void)pupd; (void)pin; }
void gpio_output_options_set(uint32_t port, uint32_t otype, uint32_t speed,
                             uint32_t pin)
{ (void)port; (void)otype; (void)speed; (void)pin; }
void rcu_periph_clock_enable(uint32_t periph) { (void)periph; }

/* Y las dos del nucleo que el CMSIS trae en la radio y aqui no hay:
 * demod_am.c cierra las interrupciones un momento para leer la continua
 * del camino ancho sin que se la cambien a medias. */
void     __disable_irq(void) { }
void     __enable_irq(void) { }
uint32_t __get_PRIMASK(void) { return 0UL; }
void     __set_PRIMASK(uint32_t v) { (void)v; }

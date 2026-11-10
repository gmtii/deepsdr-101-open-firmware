/*
 * ======================================================================
 * LAS PIEZAS TRANSVERSALES, EN INSTRUCCIONES DE CORTEX-M4
 * ======================================================================
 *
 * POR QUE ESTE BANCO. Los que ya habia miden cosas que se ven: el
 * espectro, la cabecera, el camino de audio, el FT8. Lo que corre POR
 * DEBAJO de todo eso -la subportadora de RDS metida en la interrupcion de
 * FM ancha, la transformada del panadaptador, el oscilador que llaman
 * todos, los dos correctores de espejo- no lo media nadie entero, y es
 * justo lo que se paga en TODOS los modos a la vez.
 *
 * Lo que ya estaba medido antes de esto, para no contarlo dos veces:
 *
 *   m4/m4_audio.c    nco_mezcla() a 256 y a 512 muestras, e
 *                    iqbal_aplica() / iqbal_mide() a 256.
 *   m4/m4_espectro.c el DIBUJO de la traza y la cascada, no la FFT.
 *   m4/m4_numeros.c  aritmetica suelta, nada de esto.
 *
 * Lo que anade este fichero: rds_feed() en sus cuatro estados, rds_resto()
 * y rds_corrige(), las cuatro entradas de fft.c a los tamanos que la radio
 * usa de verdad, nco_paso() y las dos formas de sacar seno y coseno de la
 * tabla, iqbal_bloque() -que es lo que de verdad corre en la interrupcion,
 * no iqbal_aplica() suelto-, iqa_bloque() y spec_agc_step().
 *
 * LA CUENTA. El camino de audio corre a 375 bloques por segundo, y a 375
 * corre TODO lo que esta dentro de la interrupcion del DMA, porque el
 * bloque crece con la tasa: 256 muestras a 96 kHz y 512 a 192 kHz dan
 * 375 bloques por segundo en los dos casos. Asi que
 *
 *     % de cada segundo = instrucciones x veces_por_segundo / 2.000.000
 *
 * con veces_por_segundo = 375 para lo que corre una vez por bloque, y la
 * tasa de muestreo para lo que se mide por muestra suelta.
 *
 * Y lo de siempre: 1 tic = 40 instrucciones, 200 MHz, y en la radio de
 * verdad es ~1,7 veces esto (ver m4/LEEME.md).
 */
#include "semi.h"
void pila_di(unsigned long maximo);

#include "rds.h"
#include "rds_pi.h"
#include "fft.h"
#include "nco.h"
#include "iqbal.h"
#include "iqancho.h"
#include "spec_agc.h"

int g_fallos;

/*
 * EL BIT DE LECTURAS DESALINEADAS, QUITADO A PROPOSITO.
 *
 * m4/start.c pone UNALIGN_TRP para que un acceso desalineado se pare en
 * seco con el registro delante. Esta bien para el codigo que toca el bus
 * del panel, y aqui tumba el banco antes de medir nada. El sitio exacto,
 * localizado con addr2line sobre el .elf:
 *
 *     rds_start()  ->  limpia_cadena(s_info.ps, 8U)
 *     str.w r2, [r5, #21]   y   str.w r2, [r5, #25]
 *
 * O sea: el campo `ps` de rds_info_t cae en el desplazamiento 21, que es
 * impar, y el -O2 junta los ocho bytes de blancos en DOS escrituras de
 * palabra sin alinear. En el GD32 eso es legal y el nucleo lo hace sin
 * rechistar -por eso la radio lleva meses arrancando el RDS sin un solo
 * fallo-; aqui seria un UsageFault que no significa nada.
 *
 * Asi que se quita SOLO ese bit, SOLO desde este fichero, y DIV_0_TRP y
 * los tres manejadores con nombre propio se quedan puestos: lo unico a lo
 * que se renuncia es a cazar lo que en esta maquina no es un defecto.
 */
#define SCB_CCR (*(volatile uint32_t *)0xE000ED14UL)
static void desalineado_vale(void)
{
    SCB_CCR &= ~(1UL << 3);
    __asm__ volatile("dsb");
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/* COMO SE DICEN LOS NUMEROS                                           */

/*
 * Un tanto por ciento del segundo, con una cifra decimal. `veces` es
 * cuantas veces por segundo corre eso: 375 para lo que va una vez por
 * bloque de la interrupcion, la tasa de muestreo para lo que se mide por
 * muestra suelta.
 */
static void porc(unsigned long ins, unsigned long veces)
{
    unsigned long long milesimas =
        ((unsigned long long)ins * (unsigned long long)veces) / 200000ULL;

    pr("      se lleva ");
    prn((unsigned long)(milesimas / 10ULL)); pr(",");
    prn((unsigned long)(milesimas % 10ULL));
    pr(" % de cada segundo (");
    prn(veces); pr(" veces por segundo)\n");
}

/* Lo mismo que mide(), pero con los tics ya sumados a mano: los bucles de
 * RDS tienen que generar la senal ENTRE medida y medida, y eso no puede
 * contar. */
static unsigned long di_ins(const char *que, unsigned long tics,
                            unsigned long n)
{
    unsigned long ins = (tics * 40UL) / n;

    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" instrucciones  = ");
    prn(ins / 200UL); pr(" us a 200 MHz\n");
    return ins;
}

/* Lo que cuesta cada muestra de un bloque, que es como se compara el RDS
 * con el resto del camino. Con dos decimales, que a este tamano uno solo
 * no distingue nada. */
static void por_muestra(unsigned long ins, unsigned long n)
{
    unsigned long c = (ins * 100UL) / n;

    pr("      por muestra ");
    prn(c / 100UL); pr(",");
    if ((c % 100UL) < 10UL) { pr("0"); }
    prn(c % 100UL);
    pr(" instrucciones\n");
}

/* Para que el compilador no se lleve por delante lo que se esta midiendo. */
static volatile float s_sumidero;

/* ------------------------------------------------------------------ */
/* EL GENERADOR DE MULTIPLEX DE FM, CON RDS DE VERDAD                  */

/*
 * La idea y los numeros son los de sim/rdstest.c y sim/rds_coste.c: la
 * subportadora a 57 kHz modulada en bifase con bloques de RDS de verdad
 * -con su CRC sacado de rds_resto(), no inventado aqui-, el piloto de 19
 * kHz, el audio y la diferencia de estereo.
 *
 * LA DIFERENCIA ES COMO SE GENERA, y no cambia la senal: alli es un sinf()
 * por portadora y por muestra, que en un M4 emulado con icount serian
 * varios millones de instrucciones por cada segundo de senal, mas que lo
 * que se quiere medir. Aqui se usa que a 192 kHz TODAS las portadoras
 * caen en fracciones exactas:
 *
 *     57 kHz  = 19/64 de la tasa        19 kHz = 19/192
 *      1 kHz  =  1/192                  14 kHz = 14/192
 *
 * o sea que la senal entera se repite cada 192 muestras y las tablas son
 * de 192 valores. Lo unico que no se repite es el bifase, que lleva los
 * datos, y ese se calcula igual que alli.
 */
#define FS_MPX     192000.0f
#define NBLK       512U            /* SDR_RX_BLOCK_SAMPLES_WFM */
#define PERIODO    192U

static float s_sub[PERIODO], s_pil[PERIODO], s_pil2[PERIODO];
static float s_aud[PERIODO], s_lr[PERIODO];
static uint32_t s_grupos[16];

static float sen2pi(float x);   /* x en vueltas, no en radianes */

static uint32_t blq(uint16_t datos, uint16_t off)
{
    uint32_t m = (uint32_t)datos << 10;
    uint16_t crc = rds_resto(m);
    return m | (uint32_t)(crc ^ off);
}

static void grupo_0a(uint16_t pi, uint8_t seg, char c1, char c2, uint32_t *out)
{
    uint16_t b = (uint16_t)((0U << 12) | (0U << 11) | (5U << 5) | seg);

    out[0] = blq(pi, RDS_OFF_A);
    out[1] = blq(b, RDS_OFF_B);
    out[2] = blq((uint16_t)0x1234, RDS_OFF_C);
    out[3] = blq((uint16_t)(((uint8_t)c1 << 8) | (uint8_t)c2), RDS_OFF_D);
}

/*
 * Seno de 2*pi*x sin libm: tabla de un cuarto de vuelta por series no, que
 * esto se llama 960 veces en todo el banco y se quiere leer. Se usa el
 * seno que ya tiene la radio, que ademas es el mismo que luego se mide.
 */
static float sen2pi(float x)
{
    float s, c;

    while (x >= 1.0f) { x -= 1.0f; }
    while (x < 0.0f)  { x += 1.0f; }
    nco_sen_cos_fase((uint32_t)(x * 4294967296.0f), &s, &c);
    return s;
}

static void tablas_mpx(void)
{
    unsigned n;

    for (n = 0U; n < PERIODO; n++) {
        float t = (float)n;
        /* cos(2*pi*57k*n/fs) = sen de un cuarto de vuelta mas */
        s_sub[n]  = sen2pi(t * (19.0f / 64.0f) + 0.25f);
        s_pil[n]  = sen2pi(t * (19.0f / 192.0f));
        s_pil2[n] = sen2pi(t * (38.0f / 192.0f));
        s_aud[n]  = sen2pi(t * (1.0f / 192.0f));
        s_lr[n]   = sen2pi(t * (14.0f / 192.0f));
    }
    grupo_0a(0x3141U, 0U, 'R', 'A', &s_grupos[0]);
    grupo_0a(0x3141U, 1U, 'D', 'I', &s_grupos[4]);
    grupo_0a(0x3141U, 2U, 'O', ' ', &s_grupos[8]);
    grupo_0a(0x3141U, 3U, 'E', 'S', &s_grupos[12]);
}

/* El transmisor, con el mismo estado que el de sim/rdstest.c. */
typedef struct {
    unsigned n;            /* indice en la tabla de 192 */
    float    fase_bit;
    int      bit_ant, valor, arrancado;
    int      gi, bi;
} tx_t;

static tx_t s_tx;
static unsigned long s_rnd = 12345UL;

static void tx_init(void)
{
    s_tx.n = 0U; s_tx.fase_bit = 0.0f;
    s_tx.bit_ant = 0; s_tx.valor = 0; s_tx.arrancado = 0;
    s_tx.gi = 0; s_tx.bi = 0;
}

static int tx_bit(void)
{
    int b, k, pos;
    uint32_t b26;

    pos = s_tx.bi;
    k = pos / 26;
    b26 = s_grupos[s_tx.gi * 4 + k];
    b = (int)((b26 >> (25 - (pos % 26))) & 1U);
    s_tx.bi++;
    if (s_tx.bi >= 104) { s_tx.bi = 0; s_tx.gi = (s_tx.gi + 1) % 4; }
    s_tx.bit_ant ^= b;        /* codificacion diferencial */
    return s_tx.bit_ant;
}

/* Ruido: el mismo congruencial de los bancos del simulador, dos tiradas
 * sumadas para que no sea plano del todo. */
static float ruido(void)
{
    float a, b;

    s_rnd = s_rnd * 1103515245UL + 12345UL;
    a = (float)((s_rnd >> 16) & 0x7FFFUL) * (1.0f / 32767.0f) - 0.5f;
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    b = (float)((s_rnd >> 16) & 0x7FFFUL) * (1.0f / 32767.0f) - 0.5f;
    return a + b;
}

/* Un bloque de 512 muestras de multiplex. `amp` 0 = emisora SIN datos:
 * el piloto, el audio y el estereo siguen estando, que es lo que de verdad
 * llega entre emisoras con RDS. */
static void bloque_mpx(float *dst, float amp, float sigma)
{
    unsigned k;
    const float paso = 1187.5f / FS_MPX;

    for (k = 0U; k < NBLK; k++) {
        float bif, y;
        unsigned n = s_tx.n;

        if (!s_tx.arrancado) {
            s_tx.valor = tx_bit() ? 1 : -1;
            s_tx.arrancado = 1;
        }
        s_tx.fase_bit += paso;
        if (s_tx.fase_bit >= 1.0f) {
            s_tx.fase_bit -= 1.0f;
            s_tx.valor = tx_bit() ? 1 : -1;
        }
        bif = (s_tx.fase_bit < 0.5f) ? (float)s_tx.valor : (float)(-s_tx.valor);

        y  = amp * bif * s_sub[n];
        y += 0.08f * s_pil[n];
        y += 0.35f * s_aud[n];
        y += 0.30f * s_lr[n] * s_pil2[n];
        if (sigma != 0.0f) { y += sigma * ruido(); }
        dst[k] = y;

        s_tx.n = (n + 1U >= PERIODO) ? 0U : n + 1U;
    }
}

static float s_mpx[NBLK];

/*
 * Corre `bloques` de senal SIN medir -para que los dos lazos enganchen- y
 * despues mide `cuantos` bloques de rds_feed(), generando cada bloque
 * fuera del reloj.
 */
static unsigned long rds_corre(const char *que, float amp, float sigma,
                               unsigned calienta, unsigned cuantos)
{
    unsigned long a, b, tics = 0UL;
    unsigned i;
    unsigned long ins;

    for (i = 0U; i < calienta; i++) {
        bloque_mpx(s_mpx, amp, sigma);
        rds_feed(s_mpx, NBLK);
    }
    for (i = 0U; i < cuantos; i++) {
        bloque_mpx(s_mpx, amp, sigma);
        a = tic();
        rds_feed(s_mpx, NBLK);
        b = tic();
        tics += (a - b) & 0xFFFFFFUL;
    }
    ins = di_ins(que, tics, cuantos);
    porc(ins, 375UL);
    por_muestra(ins, NBLK);
    return ins;
}

static void di_estado(void)
{
    rds_info_t in;

    rds_info(&in);
    pr("      enganchado "); prn((unsigned long)in.enganchado);
    pr(", bloques ok "); prn((unsigned long)in.bloques_ok);
    pr(", mal "); prn((unsigned long)in.bloques_mal);
    pr(", PS \""); pr(in.ps); pr("\"\n");
}

/* ------------------------------------------------------------------ */

static void seccion_rds(void)
{
    unsigned long a, b, v;
    int r;

    pr("\n  --- RDS: LA SUBPORTADORA DENTRO DE LA INTERRUPCION DE FM ANCHA ---\n");
    pr("      512 muestras a 192 kHz por bloque, 375 bloques por segundo.\n");
    pr("      El FIR es de 64 tomas y diezma por 16: 32 salidas por bloque.\n");
    /*
     * Y ESTO SE PUEDE COMPARAR CON ALGO, que es lo que le da valor.
     * sim/rds_coste.c no mide tiempo: cuenta SUCESOS y los multiplica por
     * instrucciones sacadas del desensamblado del firmware (I_MUESTRA 59
     * por muestra, I_FIR 448 e I_BANDABASE 150 por salida del diezmado).
     * Para un bloque de 512 eso predice 512*59 + 32*(448+150) = 49.344.
     * Lo de abajo es la misma cuenta pero CORRIENDO, en un M4.
     */
    pr("      sim/rds_coste.c predice 49.344 por bloque contando el\n");
    pr("      desensamblado; aqui se mide corriendo.\n\n");

    tablas_mpx();

    /* APAGADO: lo que cuesta no hacer nada, para restarlo de todo lo demas.
     * Es el caso de los nueve modos que no son FM ancha. */
    rds_stop();
    tx_init();
    v = 0UL;
    {
        unsigned long tics = 0UL;
        unsigned i;
        for (i = 0U; i < 50U; i++) {
            bloque_mpx(s_mpx, 0.03f, 0.0f);
            a = tic(); rds_feed(s_mpx, NBLK); b = tic();
            tics += (a - b) & 0xFFFFFFUL;
        }
        v = di_ins("rds_feed APAGADO               ", tics, 50U);
        porc(v, 375UL);
    }

    /* A: emisora con RDS limpio. 2,7 s de calentamiento, que es lo que los
     * dos lazos necesitan para enganchar; luego 200 bloques medidos. */
    rds_start(FS_MPX);
    tx_init();
    (void)rds_corre("rds_feed, RDS LIMPIO           ", 0.03f, 0.000f, 1000U, 200U);
    di_estado();

    /* B: emisora SIN datos, musica y estereo. El decodificador nunca
     * engancha, asi que mete_bit() calcula el resto en CADA bit en vez de
     * uno de cada 26. */
    rds_stop();
    rds_start(FS_MPX);
    tx_init();
    (void)rds_corre("rds_feed, emisora SIN RDS      ", 0.000f, 0.010f, 200U, 200U);
    di_estado();

    /* C: solo ruido, entre emisoras. */
    rds_stop();
    rds_start(FS_MPX);
    tx_init();
    (void)rds_corre("rds_feed, SOLO RUIDO           ", 0.000f, 0.300f, 200U, 200U);
    di_estado();

    /* D: el peor caso real. Engancha, pero falla bloques y entonces se pone
     * a buscar rafagas en corrige_en_pos(), que es el trozo caro. */
    rds_stop();
    rds_start(FS_MPX);
    tx_init();
    (void)rds_corre("rds_feed, RDS AL BORDE (ruido) ", 0.03f, 0.300f, 1000U, 200U);
    di_estado();

    rds_stop();

    /* Y las dos piezas que se pueden aislar, para saber donde esta el
     * dinero cuando la senal se pone fea. */
    {
        uint32_t x = s_grupos[0];
        volatile uint16_t sink = 0U;

        a = tic();
        for (r = 0; r < 200; r++) { sink = rds_resto(x + (uint32_t)r); }
        b = tic();
        (void)sink;
        v = mide("rds_resto, un bloque de 26 bits", a, b, 200);
        pr("      una por BIT mientras busca sincronismo, una por bloque con el\n");
        porc(v, 1187UL);
    }
    {
        uint32_t x;
        volatile uint8_t sink = 0U;

        /* El caso que se paga: un bloque que NO se puede arreglar, o sea la
         * busqueda ENTERA de rafagas. El que se arregla a la primera sale
         * mas barato y no es el que decide. */
        a = tic();
        for (r = 0; r < 20; r++) {
            x = s_grupos[0] ^ 0x02040801UL;
            sink = rds_corrige(&x, RDS_OFF_A, 2U);
        }
        b = tic();
        (void)sink;
        v = mide("rds_corrige que NO encuentra   ", a, b, 20);
        pr("      rafaga de hasta 2 bits: 26 posiciones x 2 patrones\n");

        a = tic();
        for (r = 0; r < 20; r++) {
            x = s_grupos[0] ^ 0x00000002UL;
            sink = rds_corrige(&x, RDS_OFF_A, 2U);
        }
        b = tic();
        (void)sink;
        (void)mide("rds_corrige que SI encuentra   ", a, b, 20);
    }

    /*
     * rds_pi.c NO TIENE TRABAJO POR MUESTRA. Es una tabla de codigos de
     * cadena y el renglon de la cabecera: corre cuando se repinta la
     * cabecera de FM, no dentro de la interrupcion. Se mide una vez para
     * dejarlo escrito con un numero al lado en vez de con una opinion.
     */
    {
        char rot[40];
        volatile uint8_t sink = 0U;

        a = tic();
        for (r = 0; r < 100; r++) { sink = rds_rotulo(rot, sizeof rot, 1U, 0x3141U, 1U, "RADIO ES"); }
        b = tic();
        (void)sink;
        (void)mide("rds_rotulo (cabecera, NO ISR)  ", a, b, 100);
    }
}

/* ------------------------------------------------------------------ */
/* LA FFT, A LOS TAMANOS QUE LA RADIO USA DE VERDAD                    */

/*
 * QUIEN LA LLAMA Y CON QUE. Mirado en User/main.c y User/rtty_scope.c:
 *
 *   fft_compute_db_iq()    main.c, el panadaptador, con `n` igual al
 *                          bloque de la captura: 512 a 192 kHz (todos los
 *                          modos desde el 23/10) y 256 a 96 y 48 kHz.
 *   fft_compute_db_iq_ex() main.c, el camino del zoom, con crudo = 0.
 *   fft_potencias()        rtty_scope.c, el osciloscopio de los modos
 *                          digitales, SIEMPRE de FFT_SIZE (512).
 *   fft_compute_db()       no la llama NADIE en el firmware de hoy: el FT8
 *                          tiene la suya en User/ft8_fft.c. Se mide igual,
 *                          que es la entrada publica de la cabecera.
 *
 * Y NO CORRE EN LA INTERRUPCION, asi que su cadencia no es 375. Va en el
 * bucle principal y main.c la limita a SPECTRUM_MAX_FFT_PER_FRAME = 6 por
 * cuadro dibujado; con la cascada a unos 30 cuadros por segundo salen 180
 * por segundo, y el techo absoluto -una por bloque de captura- son 375.
 * El % se da a esas 180, que es el regimen, y al lado el techo.
 */
static int16_t s_fi[512], s_fq[512];
static float   s_db[512], s_re[512];

static void datos_con_forma(void)
{
    unsigned k;
    unsigned long v = 0x1234567UL;

    /* Dos portadoras y suelo de ruido: una FFT sobre ceros no mide la FFT,
     * mide los denormales. */
    for (k = 0U; k < 512U; k++) {
        float t = (float)k;
        float s = 8000.0f * sen2pi(t * (37.0f / 512.0f))
                + 2500.0f * sen2pi(t * (113.0f / 512.0f) + 0.1f);
        float c = 8000.0f * sen2pi(t * (37.0f / 512.0f) + 0.25f)
                + 2500.0f * sen2pi(t * (113.0f / 512.0f) + 0.35f);

        v = v * 1103515245UL + 12345UL;
        s += (float)((long)((v >> 16) & 0x3FFUL) - 512);
        v = v * 1103515245UL + 12345UL;
        c += (float)((long)((v >> 16) & 0x3FFUL) - 512);
        s_fi[k] = (int16_t)s;
        s_fq[k] = (int16_t)c;
        s_re[k] = s * (1.0f / 32768.0f);
    }
}

static void seccion_fft(void)
{
    unsigned long a, b, v;
    int r;

    pr("\n  --- LA FFT DEL PANADAPTADOR ---\n");
    pr("      FFT_SIZE compilado = "); prn((unsigned long)FFT_SIZE); pr("\n\n");

    fft_init();
    datos_con_forma();

    fft_tam_iq(512U);
    a = tic(); for (r = 0; r < 20; r++) { fft_compute_db_iq(s_fi, s_fq, s_db); } b = tic();
    v = mide("fft_compute_db_iq, 512 (192 kHz)", a, b, 20);
    porc(v, 180UL);
    pr("      y el techo, una por bloque de captura:\n");
    porc(v, 375UL);

    fft_tam_iq(256U);
    a = tic(); for (r = 0; r < 20; r++) { fft_compute_db_iq(s_fi, s_fq, s_db); } b = tic();
    v = mide("fft_compute_db_iq, 256 (96 kHz) ", a, b, 20);
    porc(v, 180UL);

    fft_tam_iq(512U);
    a = tic(); for (r = 0; r < 20; r++) { fft_compute_db_iq_ex(s_fi, s_fq, s_db, 0U); } b = tic();
    (void)mide("fft_compute_db_iq_ex, 512, zoom", a, b, 20);

    a = tic(); for (r = 0; r < 20; r++) { fft_compute_db(s_fi, s_db); } b = tic();
    (void)mide("fft_compute_db, real, 512      ", a, b, 20);

    a = tic(); for (r = 0; r < 20; r++) { fft_potencias(s_re, s_db); } b = tic();
    (void)mide("fft_potencias, real, 512 (RTTY) ", a, b, 20);
    pr("      una por VENTANA de 512 muestras de audio, en el bucle principal,\n");
    pr("      y solo en los modos digitales: la cadencia la pone rtty_scope.c\n");

    a = tic(); fft_init(); b = tic();
    (void)mide("fft_init (una vez, al arrancar)", a, b, 1);
}

/* ------------------------------------------------------------------ */
/* EL OSCILADOR                                                        */

static void seccion_nco(void)
{
    static float bi[512], bq[512];
    unsigned long a, b, v;
    nco_t o;
    int r;
    unsigned k;

    pr("\n  --- EL NCO: LO MAS LLAMADO DEL ARBOL ---\n");
    pr("      nco_mezcla() a 256 y a 512 ya lo mide m4/m4_audio.c; aqui esta\n");
    pr("      lo de por muestra suelta, que es lo que usan el RDS y los lazos.\n\n");

    nco_init();
    for (k = 0U; k < 512U; k++) { bi[k] = 0.5f; bq[k] = 0.125f; }

    nco_freq(&o, 3000.0f, 96000.0f);
    nco_fase_cero(&o);
    a = tic(); for (r = 0; r < 20; r++) { nco_mezcla(&o, bi, bq, 512U); } b = tic();
    v = di_ins("nco_mezcla, bloque de 512      ", (a - b) & 0xFFFFFFUL, 20U);
    porc(v, 375UL);
    por_muestra(v, 512U);

    /* nco_paso(): la que usa rds_feed(), una por muestra del multiplex. */
    nco_freq(&o, 57000.0f, 192000.0f);
    nco_fase_cero(&o);
    {
        float sn, cs;
        a = tic();
        for (r = 0; r < 20; r++) {
            for (k = 0U; k < 512U; k++) {
                nco_paso(&o, &sn, &cs);
                s_sumidero = sn + cs;
            }
        }
        b = tic();
        v = di_ins("nco_paso, una llamada          ", (a - b) & 0xFFFFFFUL, 20U * 512U);
        pr("      con el sumidero que impide que el -O2 se la lleve\n");
        porc(v, 192000UL);
    }

    /* nco_sen_cos_rad(): la de los lazos cuya fase NO avanza a paso fijo
     * -el SAM, el Costas del RDS-, que si no llamarian a sinf()/cosf(). */
    {
        float sn, cs;
        a = tic();
        for (r = 0; r < 20; r++) {
            for (k = 0U; k < 512U; k++) {
                nco_sen_cos_rad((float)k * 0.0123f, &sn, &cs);
                s_sumidero = sn + cs;
            }
        }
        b = tic();
        v = di_ins("nco_sen_cos_rad, una llamada   ", (a - b) & 0xFFFFFFUL, 20U * 512U);
        porc(v, 192000UL);
    }

    /* nco_sen_cos_fase(): la misma cuenta pero en linea y con la fase ya
     * en la rejilla de 32 bits. La diferencia con la de arriba ES el peaje
     * de recibir radianes, que es justo lo que dice su comentario. */
    {
        float sn, cs;
        uint32_t f = 0UL;

        a = tic();
        for (r = 0; r < 20; r++) {
            for (k = 0U; k < 512U; k++) {
                nco_sen_cos_fase(f, &sn, &cs);
                f += 0x02000000UL;
                s_sumidero = sn + cs;
            }
        }
        b = tic();
        v = di_ins("nco_sen_cos_fase, EN LINEA     ", (a - b) & 0xFFFFFFUL, 20U * 512U);
        porc(v, 192000UL);
    }

    a = tic(); for (r = 0; r < 100; r++) { nco_freq(&o, 3000.0f + (float)r, 96000.0f); } b = tic();
    (void)mide("nco_freq (al girar el dial)    ", a, b, 100);
}

/* ------------------------------------------------------------------ */
/* LOS DOS CORRECTORES DE ESPEJO                                       */

/*
 * iqbal_bloque() es lo que de verdad corre dentro de la interrupcion del
 * DMA (User/sdr_rx.c), sobre el bloque CRUDO ENTRELAZADO: par = I, impar =
 * Q. m4/m4_audio.c mide iqbal_aplica() y iqbal_mide() por separado y a
 * 256; aqui se mide la funcion entera, a 512, y partida en sus dos casos,
 * que no cuestan lo mismo:
 *
 *   - siete bloques de cada ocho solo CORRIGEN;
 *   - el octavo ademas MIDE, que son dos recorridos mas del bloque.
 *
 * iqa_bloque() -el corrector de banda ancha, seis tomas complejas por
 * muestra- salio del camino el 08/10/2026 justo por lo que cuesta. Se mide
 * para que el numero este, no porque corra hoy.
 *
 * LO QUE AQUI NO SE MIDE, Y QUE LO BLOQUEA. iqbal_bloque() tiene una
 * tercera rama: si s_fir_hay, se va por fir_aplica(), que son cuatro tomas
 * complejas por muestra en vez de dos multiplicaciones. Esa rama solo se
 * enciende despues de una CALIBRACION completa -varias ranuras de
 * frecuencia, cada una con su portadora y su bin espejo entrando por
 * fft_set_bins_hook() y iqbal_calibra(), y luego iqbal_resuelve()-, o sea
 * montar un generador de barrido entero dentro del banco. Queda sin medir
 * a proposito; el que la quiera tiene que escribir esa calibracion.
 */
static int16_t s_crudo[2U * 512U];

static void llena_crudo(void)
{
    unsigned k;
    unsigned long v = 0x76543210UL;

    for (k = 0U; k < 512U; k++) {
        float t = (float)k;
        float i = 6000.0f * sen2pi(t * (29.0f / 512.0f));
        float q = 5400.0f * sen2pi(t * (29.0f / 512.0f) + 0.27f);

        v = v * 1103515245UL + 12345UL;
        i += (float)((long)((v >> 16) & 0x1FFUL) - 256);
        v = v * 1103515245UL + 12345UL;
        q += (float)((long)((v >> 16) & 0x1FFUL) - 256);
        s_crudo[2U * k]      = (int16_t)i;
        s_crudo[2U * k + 1U] = (int16_t)q;
    }
}

static void seccion_espejo(void)
{
    unsigned long a, b, v, tmin, tmax, tsum, t1;
    unsigned i;

    pr("\n  --- LOS CORRECTORES DE ESPEJO, DENTRO DE LA INTERRUPCION ---\n");
    pr("      bloque CRUDO entrelazado de 512 muestras (192 kHz).\n\n");

    iqbal_init();
    iqbal_set_on(1U);
    iqbal_aprende(1U);

    /* Unos cuantos bloques antes de medir: el estimador tiene que tener ya
     * theta y g, porque con s_hay a cero la correccion no se ejecuta y el
     * bloque saldria barato de mentira. */
    for (i = 0U; i < 40U; i++) {
        llena_crudo();
        iqbal_bloque(s_crudo, 512U);
    }

    /* Ocho bloques seguidos: uno de ellos es el que mide. Se quedan el mas
     * barato y el mas caro, que son los dos casos de verdad. */
    tmin = 0xFFFFFFFFUL; tmax = 0UL; tsum = 0UL;
    for (i = 0U; i < 8U; i++) {
        llena_crudo();
        a = tic(); iqbal_bloque(s_crudo, 512U); b = tic();
        t1 = (a - b) & 0xFFFFFFUL;
        tsum += t1;
        if (t1 < tmin) { tmin = t1; }
        if (t1 > tmax) { tmax = t1; }
    }
    v = di_ins("iqbal_bloque, solo CORRIGIENDO ", tmin, 1U);
    por_muestra(v, 512U);
    v = di_ins("iqbal_bloque, el que MIDE (1/8)", tmax, 1U);
    por_muestra(v, 512U);
    /* Y lo que se paga de verdad por segundo, que es la media de la vuelta
     * de ocho: el caro no corre 375 veces. */
    v = di_ins("iqbal_bloque, MEDIA de los 8   ", tsum, 8U);
    porc(v, 375UL);

    /* Y apagado, que es lo que paga quien tiene el corrector quitado. */
    iqbal_set_on(0U);
    iqbal_aprende(0U);
    llena_crudo();
    a = tic(); iqbal_bloque(s_crudo, 512U); b = tic();
    v = di_ins("iqbal_bloque APAGADO           ", (a - b) & 0xFFFFFFUL, 1U);
    porc(v, 375UL);

    /* El de banda ancha. Hoy no lo llama nadie; ver la cabecera de arriba. */
    iqa_init();
    iqa_set_on(1U);
    for (i = 0U; i < 20U; i++) {
        llena_crudo();
        iqa_bloque(s_crudo, 512U);
    }
    tmin = 0UL;
    for (i = 0U; i < 8U; i++) {
        llena_crudo();
        a = tic(); iqa_bloque(s_crudo, 512U); b = tic();
        tmin += (a - b) & 0xFFFFFFUL;
    }
    v = di_ins("iqa_bloque, el de BANDA ANCHA  ", tmin, 8U);
    porc(v, 375UL);
    por_muestra(v, 512U);
    pr("      fuera del camino desde el 08/10/2026; el numero esta por si vuelve\n");
}

/* ------------------------------------------------------------------ */
/* LA AUTOESCALA DEL ESPECTRO                                          */

static void seccion_spec_agc(void)
{
    unsigned long a, b, v;
    unsigned k;
    float mn = 0.0f, mx = 100.0f;
    int r;

    pr("\n  --- LA AUTOESCALA DEL ESPECTRO ---\n");
    pr("      una vez por CUADRO, no por bloque. No la media ningun otro banco.\n\n");

    /* Un cuadro con forma: suelo de ruido, una senal y el pico de continua
     * del centro, que es justo lo que los percentiles tienen que ignorar. */
    for (k = 0U; k < 512U; k++) {
        float d = 20.0f + 6.0f * sen2pi((float)k * (3.0f / 512.0f));
        if (k > 200U && k < 230U) { d += 45.0f; }
        if (k == 256U) { d = 130.0f; }
        s_db[k] = d;
    }

    a = tic(); for (r = 0; r < 50; r++) { spec_agc_step(s_db, 512U, &mn, &mx); } b = tic();
    v = mide("spec_agc_step, 512 bins        ", a, b, 50);
    porc(v, 30UL);
    pr("      a 30 cuadros por segundo, que es su cadencia de verdad\n");

    a = tic(); for (r = 0; r < 50; r++) { spec_agc_target(s_db, 512U, &mn, &mx); } b = tic();
    (void)mide("spec_agc_target, 512 bins      ", a, b, 50);

    a = tic(); for (r = 0; r < 50; r++) { spec_agc_step(s_db, 256U, &mn, &mx); } b = tic();
    (void)mide("spec_agc_step, 256 bins        ", a, b, 50);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    desalineado_vale();
    reloj_on();

    pr("\nLAS PIEZAS TRANSVERSALES, EN UN CORTEX-M4 DE VERDAD\n");
    pr("  1 tic = 40 instrucciones. 200 MHz. En la radio, x1,7.\n");

    seccion_rds();
    seccion_fft();
    seccion_nco();
    seccion_espejo();
    seccion_spec_agc();

    pr("\n");
    pila_di(0UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}

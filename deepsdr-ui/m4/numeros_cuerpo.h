/*
 * LOS MISMOS NUMEROS, EN EL PC Y EN EL CORTEX-M4.
 *
 * Este fichero lo incluyen DOS programas: el banco del M4 (m4_numeros.c,
 * que corre bajo qemu con la FPU de simple precision del Cortex-M4) y su
 * gemelo del PC (sim/numeros_pc.c, que corre con la del anfitrion). Los dos
 * compilan EL MISMO codigo del firmware y escriben el MISMO formato, bit a
 * bit. Si los dos ficheros de salida no son identicos, hay un sitio donde
 * el simulador dice una cosa y la radio dice otra -y es justo donde los
 * bancos del PC pueden estar dando por bueno algo que en la placa no lo
 * es-.
 *
 * SE IMPRIMEN LOS BITS, NO EL NUMERO. "0,7071" y "0,7071" pueden ser dos
 * floats distintos; 0x3F3504F3 y 0x3F3504F2 no. Lo que se busca aqui son
 * diferencias de un ulp que luego se acumulan en un lazo realimentado -el
 * PLL del SAM, el corrector de espejo, el seguidor de fase de un
 * decodificador-, y esas no se ven en cuatro decimales.
 *
 * Quien escribe de verdad lo pone cada lado: pr() y prh() en el M4,
 * printf() en el PC. Aqui solo se decide QUE se mide.
 */
#include <stdint.h>
#include <string.h>
#include "fft.h"
#include "nco.h"
#include "iqbal.h"
#include "sam.h"
#include "ldpc.h"
#include "crc.h"
#include "hfdl_costas.h"
#include "ft8_shared_ram.h"

/* La RAM compartida de FT8 vive en waterfall.c, que aqui arrastraria la
 * pantalla entera. Como lo que se mide es el LDPC y no el reparto de
 * memoria, se define aqui: es el MISMO tipo y los dos lados -PC y M4-
 * usan este, asi que la comparacion sigue siendo contra lo mismo. */
ft8_shared_ram_t g_ft8_shared_ram;

/* El ruido de siempre, el mismo en los dos lados: un congruencial entero,
 * sin coma flotante, para que la ENTRADA no pueda diferir nunca. */
static uint32_t s_vai;
static void vai_cero(void) { s_vai = 0x12345678UL; }
static int16_t vai16(void)
{
    s_vai = s_vai * 1103515245UL + 12345UL;
    return (int16_t)((int32_t)((s_vai >> 12) & 0xFFFFUL) - 32768);
}

/* Un float, por sus bits. memcpy y no un puntero con cast: el cast es un
 * acceso con un tipo que no es el del objeto, y con -O2 el compilador tiene
 * permiso para no creerselo. */
static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* Y de un vector, una firma que depende de TODOS los bits de TODOS los
 * elementos. Sin coma flotante: la firma no puede tener el mismo problema
 * que lo que vigila. */
static uint32_t firma(const float *v, uint32_t n)
{
    uint32_t f = 2166136261UL, i;
    for (i = 0U; i < n; i++) {
        uint32_t u = bits(v[i]);
        f = (f ^ (u & 0xFFU)) * 16777619UL;
        f = (f ^ ((u >> 8) & 0xFFU)) * 16777619UL;
        f = (f ^ ((u >> 16) & 0xFFU)) * 16777619UL;
        f = (f ^ (u >> 24)) * 16777619UL;
    }
    return f;
}

/* ------------------------------------------------------------------ */

static int16_t s_i[1024], s_q[1024];
static float   s_db[1024];

/*
 * LA PILA DE CADA TROZO, medida corriendo.
 *
 * Solo tiene sentido en el M4 -en el PC la pila son megas y no dice nada de
 * la radio-, asi que el lado del PC define pila_marca() como nada. Y se
 * imprime DESPUES de la linea en blanco que cierra la comparacion, para
 * que el diff de los dos lados no la vea.
 */
void numeros_corre(void)
{
    uint32_t i;

    di("== NUMEROS, BIT A BIT ==");

    /* --- 1. El seno y el coseno del NCO, vuelta entera --------------- */
    {
        float acc_s[64], acc_c[64];
        for (i = 0U; i < 64U; i++) {
            nco_sen_cos_rad((float)i * 0.09817477f, &acc_s[i], &acc_c[i]);
        }
        dih("nco_sen_cos_rad sen", firma(acc_s, 64U));
        dih("nco_sen_cos_rad cos", firma(acc_c, 64U));
    }

    /* --- 2. El incremento del NCO, que se calcula en double ---------- */
    {
        nco_t o;
        uint32_t acc = 2166136261UL;
        static const float k_hz[8] = { 1.0f, 1000.0f, 12345.6f, -7000.0f,
                                       48000.0f, 0.125f, 95999.5f, -0.0f };
        for (i = 0U; i < 8U; i++) {
            nco_freq(&o, k_hz[i], 192000.0f);
            acc = (acc ^ o.inc) * 16777619UL;
            acc = (acc ^ bits(nco_freq_real(&o, 192000.0f))) * 16777619UL;
        }
        dih("nco_freq inc + real", acc);
    }

    /* --- 3. La mezcla del NCO sobre un bloque ------------------------ */
    {
        nco_t o;
        static float bi[256], bq[256];
        nco_freq(&o, 3000.0f, 48000.0f);
        nco_fase_cero(&o);
        vai_cero();
        for (i = 0U; i < 256U; i++) {
            bi[i] = (float)vai16() * (1.0f / 32768.0f);
            bq[i] = (float)vai16() * (1.0f / 32768.0f);
        }
        nco_mezcla(&o, bi, bq, 256U);
        dih("nco_mezcla I", firma(bi, 256U));
        dih("nco_mezcla Q", firma(bq, 256U));
    }

    /* --- 4. La FFT y los dB, que es lo que se ve en el espectro ------ */
    {
        fft_init();
        vai_cero();
        for (i = 0U; i < 1024U; i++) { s_i[i] = vai16(); s_q[i] = vai16(); }
        fft_compute_db_iq(s_i, s_q, s_db);
        dih("fft_compute_db_iq", firma(s_db, 1024U));
        dih("fft db[0]", bits(s_db[0]));
        dih("fft db[512]", bits(s_db[512]));
        dih("fft db[1023]", bits(s_db[1023]));
    }

    /* --- 5. Una senoide limpia: el bin y su nivel -------------------- */
    {
        nco_t o;
        static float bi[1024], bq[1024];
        nco_freq(&o, 12000.0f, 192000.0f);
        nco_fase_cero(&o);
        for (i = 0U; i < 1024U; i++) { bi[i] = 1.0f; bq[i] = 0.0f; }
        nco_mezcla(&o, bi, bq, 1024U);
        for (i = 0U; i < 1024U; i++) {
            s_i[i] = (int16_t)(bi[i] * 16000.0f);
            s_q[i] = (int16_t)(bq[i] * 16000.0f);
        }
        fft_compute_db_iq(s_i, s_q, s_db);
        {
            uint32_t mejor = 0U;
            for (i = 1U; i < 1024U; i++) {
                if (s_db[i] > s_db[mejor]) { mejor = i; }
            }
            din("tono de 12 kHz, bin", mejor);
            dih("y su nivel", bits(s_db[mejor]));
            dih("firma entera", firma(s_db, 1024U));
        }
    }

    pila_marca("el NCO, la FFT y el tono ");

    /* --- 6. EL CORRECTOR DE ESPEJO, QUE ES UN LAZO -----------------
     *
     * Aqui es donde un ulp importa. Los cinco de arriba son cuentas de
     * una pasada: la diferencia entra y sale. Este ARRASTRA estado de un
     * bloque al siguiente -theta y g se corrigen poco a poco- asi que una
     * diferencia de un bit en el primer bloque puede ser otra cosa al
     * cabo de cien. Por eso se miden los dos: lo que vale al principio y
     * lo que vale despues de cien bloques.
     *
     * La señal de entrada lleva un desequilibrio PUESTO A MANO -Q con un
     * 6 % mas de ganancia y 3 grados de inclinacion- para que el
     * corrector tenga de verdad algo que corregir y el lazo se mueva.
     */
    {
        static int16_t bi[512], bq[512];
        uint32_t b;
        nco_t o;

        iqbal_init();
        iqbal_set_on(1U);
        iqbal_aprende(1U);
        nco_freq(&o, 7000.0f, 192000.0f);
        nco_fase_cero(&o);

        for (b = 0U; b < 100U; b++) {
            static float fi[512], fq[512];
            for (i = 0U; i < 512U; i++) { fi[i] = 1.0f; fq[i] = 0.0f; }
            nco_mezcla(&o, fi, fq, 512U);
            for (i = 0U; i < 512U; i++) {
                /* el desequilibrio: ganancia en Q y un poco de inclinacion */
                float ii = fi[i];
                float qq = fq[i] * 1.06f + fi[i] * 0.05234f;
                bi[i] = (int16_t)(ii * 12000.0f);
                bq[i] = (int16_t)(qq * 12000.0f);
            }
            iqbal_mide(bi, bq, 512U);
            iqbal_aplica(bi, bq, 512U);
            if (b == 0U) {
                dih("espejo theta al 1er bloque", bits(iqbal_theta()));
                dih("espejo g     al 1er bloque", bits(iqbal_g()));
            }
        }
        dih("espejo theta tras 100", bits(iqbal_theta()));
        dih("espejo g     tras 100", bits(iqbal_g()));
        dih("espejo grados", bits(iqbal_fase_grados()));
        dih("espejo dB", bits(iqbal_gan_db()));
    }

    pila_marca("y el corrector de espejo  ");

    /* --- 7. EL PLL DEL SAM, QUE ES EL LAZO MAS LARGO ----------------
     *
     * El de arriba corrige de bloque en bloque; este de MUESTRA en
     * MUESTRA, y lleva un integrador. Si un ulp se amplifica en algun
     * sitio de esta radio, es aqui. Se le da una portadora con un error
     * de frecuencia puesto -300 Hz- y se mide el enganche a los 1.000
     * muestras y a los 48.000, que es un segundo entero.
     */
    {
        sam_t sm;
        static float fi[512], fq[512], sal[512];
        nco_t o;
        uint32_t b;

        sam_init(&sm, 48000.0f, 1000.0f, 100.0f, 0.707f);
        sam_reset(&sm);
        /* Las ocho primeras muestras, una a una: donde empieza a diferir
         * dice que operacion es la que no cuadra. */
        {
            sam_t s8;
            sam_init(&s8, 48000.0f, 1000.0f, 100.0f, 0.707f);
            sam_reset(&s8);
            for (i = 0U; i < 8U; i++) {
                float y = sam_step(&s8, 0.5f, 0.0f, 48000.0f);
                dih("sam_step", bits(y));
                dih("  enganche", bits(sam_enganche(&s8)));
            }
            /* Y lo mismo por bloques de uno: si esto SI difiere y lo de
             * arriba no, lo que no cuadra esta en sam_nucleo()/sam_lectura()
             * y no en sam_step(). */
            {
                sam_t sb;
                float ii = 0.5f, qq = 0.0f, yy;
                sam_init(&sb, 48000.0f, 1000.0f, 100.0f, 0.707f);
                sam_reset(&sb);
                for (i = 0U; i < 8U; i++) {
                    sam_bloque(&sb, &ii, &qq, &yy, 1U, 48000.0f);
                    dih("sam_bloque n=1", bits(yy));
                }
                dih("sam_bloque n=1 enganche", bits(sam_enganche(&sb)));
                dih("sam_bloque n=1 omega2", bits(sb.omega2));
            }
        }
        nco_freq(&o, 300.0f, 48000.0f);
        nco_fase_cero(&o);

        for (b = 0U; b < 94U; b++) {       /* 94 x 512 = 48.128 muestras */
            for (i = 0U; i < 512U; i++) { fi[i] = 0.5f; fq[i] = 0.0f; }
            nco_mezcla(&o, fi, fq, 512U);
            sam_bloque(&sm, fi, fq, sal, 512U, 48000.0f);
            if (b == 1U) {
                dih("sam enganche a 1.024 muestras", bits(sam_enganche(&sm)));
                dih("sam salida a 1.024 muestras", firma(sal, 512U));
            }
        }
        /* Y LA GUARDA DE LOS CUATRO NUMEROS. Ver sam_init(): con zeta
         * por encima de uno, g2 salia NaN y el lazo entero se moria en
         * silencio. Esto lo vigila: si alguien quita la guarda, aqui sale
         * NaN y el banco se pone rojo. */
        {
            sam_t mal;
            float y, ii = 0.5f, qq = 0.0f;
            sam_init(&mal, 48000.0f, 1000.0f, 100.0f, 20.0f);  /* zeta imposible */
            sam_reset(&mal);
            for (i = 0U; i < 64U; i++) { y = sam_step(&mal, ii, qq, 48000.0f); }
            if (!(y == y) || !(mal.omega2 == mal.omega2)) {
                di("  *** MAL *** zeta>1 deja el SAM en NaN");
                g_fallos++;
            } else {
                di("zeta imposible: el lazo sigue vivo  ok");
            }
            sam_init(&mal, 0.0f, -1.0f, -1.0f, -1.0f);         /* todo imposible */
            sam_reset(&mal);
            for (i = 0U; i < 64U; i++) { y = sam_step(&mal, ii, qq, 48000.0f); }
            if (!(y == y) || !(mal.omega2 == mal.omega2)) {
                di("  *** MAL *** los cuatro numeros a cero dejan NaN");
                g_fallos++;
            } else {
                di("cuatro numeros imposibles: sigue vivo  ok");
            }
        }

        dih("sam enganche a 48.128", bits(sam_enganche(&sm)));
        din("sam enganchado", (unsigned long)sam_enganchado(&sm));
        dih("sam salida al final", firma(sal, 512U));
    }

    pila_marca("y el PLL del SAM          ");

    /* --- 8. EL LDPC DE FT8, QUE ES QUIEN DECIDE LOS BITS -----------
     *
     * Aqui un ulp SI puede cambiar el resultado, y no un poco: el
     * decodificador va acumulando verosimilitudes y al final decide CERO
     * o UNO. Dos cuentas que difieran en el ultimo bit pueden caer a
     * lados distintos del cero y sacar un mensaje distinto -o ninguno-.
     * Por eso se le dan unas verosimilitudes DEBILES, cerca del empate,
     * que es donde se nota; con una señal fuerte decidiria igual aunque
     * las cuentas bailaran.
     */
    {
        static float llr[174];
        static uint8_t plano[174];   /* 174, uno por BIT: lo dice ldpc.h */
        static uint8_t paq[12];
        int ok = 0;
        uint32_t f = 2166136261UL;

        vai_cero();
        for (i = 0U; i < 174U; i++) {
            /* +-0,8 y no +-5: cerca del empate es donde se decide de
             * verdad. El signo sale del mismo congruencial entero de
             * siempre, asi que la entrada es identica en los dos lados. */
            llr[i] = ((vai16() >= 0) ? 0.8f : -0.8f)
                     + (float)(vai16() / 1024) * 0.0009765625f;
        }
        bp_decode(llr, 20, plano, &ok);   /* el que corre de verdad en la radio */
        din("ldpc fallos que quedan", (unsigned long)ok);
        for (i = 0U; i < 174U; i++) { f = (f ^ plano[i]) * 16777619UL; }
        dih("ldpc bits decididos", f);

        /* Y el CRC, que es quien dice si el mensaje vale. Entero puro:
         * tiene que salir igual SIEMPRE, en el PC, en la radio y en
         * cualquier compilador. Si esto difiere, no es coma flotante. */
        for (i = 0U; i < 12U; i++) { paq[i] = 0U; }
        for (i = 0U; i < 91U; i++) {
            if (plano[i]) { paq[i >> 3] |= (uint8_t)(0x80U >> (i & 7U)); }
        }
        din("crc del resultado", (unsigned long)ftx_compute_crc(paq, 82));
    }

    pila_marca("y el LDPC de FT8         ");

    /* --- 9. EL LAZO DE COSTAS DE HFDL ------------------------------
     *
     * El tercer lazo realimentado de la radio, y el que decide los bits
     * de los paquetes de los aviones. Se le da una portadora con un error
     * de frecuencia y se mira a donde ha llegado la fase y la frecuencia
     * tras 10.000 muestras.
     */
    {
        hfdl_costas_t c;
        nco_t o;
        static float bi[500], bq[500];
        uint32_t b;
        float oi = 0.0f, oq = 0.0f;

        hfdl_costas_init(&c, 0.01f, 0.0001f);
        hfdl_costas_reset(&c);
        nco_freq(&o, 40.0f, 2500.0f);
        nco_fase_cero(&o);

        for (b = 0U; b < 20U; b++) {
            for (i = 0U; i < 500U; i++) { bi[i] = 1.0f; bq[i] = 0.0f; }
            nco_mezcla(&o, bi, bq, 500U);
            for (i = 0U; i < 500U; i++) {
                hfdl_costas_step_bpsk(&c, bi[i], bq[i], &oi, &oq);
            }
            if (b == 0U) { dih("costas freq tras 500", bits(c.freq)); }
        }
        dih("costas freq tras 10.000", bits(c.freq));
        dih("costas fase tras 10.000", bits(c.phase));
        dih("costas salida I", bits(oi));
        dih("costas salida Q", bits(oq));
    }

    pila_marca("y el Costas de HFDL      ");

    di("");

    /* Y ahora si, fuera de lo que se compara. Ver pila_marca(). */
    pila_tabla();
}

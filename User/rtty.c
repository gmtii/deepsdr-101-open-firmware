#include "rtty.h"
#include <string.h>
#include "rtty_auto.h"
#include "config.h"
#include "debug_uart.h"
#include <math.h>

/*
 * RTTY decoder core - see rtty.h for the module contract and the
 * *** FIRST DRAFT, NOT YET VALIDATED *** warning.
 *
 * Pipeline, per RTTY_BLOCK_SAMPLES(32)-sample block @ 12kHz
 * (~2.67ms/block, ~8.25 blocks per bit period at 45.45 baud):
 *
 *   1. Two block Goertzel filters (goertzel_mag()), one centered on
 *      CONFIG_RTTY_MARK_HZ, one on CONFIG_RTTY_SPACE_HZ - cheap
 *      single-bin DFT magnitude, exactly the right tool when you only
 *      care about two specific frequencies rather than the whole
 *      spectrum (no FFT needed).
 *   2. Whichever has more energy wins - mark=1, space=0 - one bit
 *      DECISION per block (rtty_decide_bit()). No hysteresis yet
 *      (*** likely first thing to add once real logs show chatter
 *      right at the mark/space crossover on a weak/noisy signal ***).
 *   3. A per-character state machine (rtty_state_machine()) that
 *      idles on mark, treats a mark->space transition as a candidate
 *      START bit, then samples the block decision nearest the center
 *      of each of the 5 Baudot data bits and the stop bit, entirely
 *      by SAMPLE COUNT since the start edge (no continuous bit PLL -
 *      timing re-syncs fresh at every start bit, which is the simple,
 *      standard approach for a first-draft embedded RTTY decoder: it
 *      tolerates transmit/receive clock drift because it never
 *      accumulates error across more than one character, at the cost
 *      of being less robust to noise landing exactly on a start-bit
 *      edge than a real bit-PLL would be).
 *   4. A valid frame (mark seen at the stop-bit position) gets
 *      Baudot-decoded (rtty_baudot_decode()) through k_baudot_letters/
 *      k_baudot_figs, tracking FIGS/LTRS shift state, and pushed to
 *      the output ring buffer.
 *
 * *** THINGS MOST LIKELY TO NEED TUNING FROM REAL LOGS ***, roughly
 * in the order to check them:
 *   - CONFIG_RTTY_MARK_HZ/SPACE_HZ (config.h): must actually match
 *     where the two tones land in YOUR receive convention (LSB vs
 *     USB, radio's audio passband, station's shift polarity). Use
 *     rtty_get_last_mark_mag()/rtty_get_last_space_mag() tuned to a
 *     known RTTY signal to see if either ever shows a strong peak at
 *     all, and where.
 *   - No hysteresis on the bit decision (see step 2) - if
 *     rtty_get_last_bit() chatters rapidly with no signal present
 *     (noise), that's expected and harmless (rtty_state_machine()
 *     just won't get a stable enough start edge to sync on); if it
 *     chatters WITH a real signal present, the two Goertzel bins are
 *     probably too close to the actual tones' bandwidth/each other
 *     and need hysteresis or narrower/better-placed bins.
 *   - The Baudot table itself (k_baudot_letters/k_baudot_figs below)
 *     - the letters are solid, but a handful of figures-shift
 *       punctuation positions have known US-vs-CCITT/European
 *       regional variance (flagged inline below) - if a decoded
 *       message's LETTERS look right but punctuation looks wrong,
 *       start here.
 */

#define GOERTZEL_N RTTY_BLOCK_SAMPLES /* block length the Goertzel filters run over each call */
#define RTTY_FS_HZ 12000.0f           /* s_ssb_dec's rate - see demod_am.c's RTTY INTEGRATION comment */

#define BAUDOT_FIGS 0x1BU /* 11011 - shift to figures */
#define BAUDOT_LTRS 0x1FU /* 11111 - shift to letters */

/*
 * ITA2/CCITT-2 ("European") Baudot table - the international
 * convention amateur RTTY follows, NOT the older US TTY table (which
 * differs at a few positions, notably code 5 = BEL instead of ' and
 * code 9 = WRU instead of this table's placeholder). '\0' entries are
 * either genuinely blank/unused codes or control characters with no
 * simple ASCII equivalent (BEL, WRU/ENQ) - rtty_baudot_decode() skips
 * pushing '\0' to the output rather than guessing wrong.
 *
 * *** Positions flagged "(check)" below have known regional table
 * variance in different published references - verify against a real
 * decoded QSO before trusting the exact punctuation. ***
 */
static const char k_baudot_letters[32] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',
    '\r', 'D', 'R',  'J', 'N', 'F', 'C', 'K',
    'T',  'Z', 'L',  'W', 'H', 'Y', 'P', 'Q',
    'O',  'B', 'G',  '\0'/*FIGS*/, 'M', 'X', 'V', '\0'/*LTRS*/
};

static const char k_baudot_figs[32] = {
    '\0', '3', '\n', '-', ' ', '\'', '8', '7',
    '\r', '\0'/*WRU, check*/, '4', '\0'/*BEL, check*/, ',', '!'/*check*/, ':', '(',
    '5',  '"'/*check*/, ')', '2', '#'/*GBP sign, check*/, '6', '0', '1',
    '9',  '?', '&', '\0'/*FIGS*/, '.', '/', ';', '\0'/*LTRS*/
};

/*
 * --- output ring buffer ---
 *
 * OJO CON EL CONTEXTO, que esto lo decia al reves hasta el 30/09/2026.
 *
 * Ponia "both only ever called from the main loop/poll context in this
 * project's design, never the ISR". FALSO: rtty_process() se llama desde
 * demod_am_process_raw(), que es el gancho de bloque que sdr_rx.c invoca
 * DESDE DMA0_Channel3_IRQHandler(). O sea que ring_push() corre en la
 * interrupcion del audio y rtty_get_char() en el bucle principal.
 *
 * Hoy funciona, pero por como esta escrito y no por el razonamiento de ese
 * comentario: es un productor-consumidor con UN indice de cada lado, los dos
 * uint16_t alineados, y cada lado escribe solo el suyo. Eso es seguro en este
 * nucleo sin reordenar nada.
 *
 * Lo que NO se puede hacer es fiarse de la frase vieja y meter aqui estado
 * compartido que no sea atomico -un contador que los dos incrementen, una
 * longitud, un segundo indice- porque eso si se rompe. Y lo mismo vale para
 * rtty_set_enabled()/rtty_set_baud()/rtty_recompute_coeffs(), que reescriben
 * seis floats de coeficientes y la maquina de estados desde el bucle
 * principal mientras la interrupcion los esta usando: hoy se tolera porque lo
 * peor que sale es un caracter malo justo al cambiar el ajuste, no porque
 * este protegido.
 */
#define RTTY_RINGBUF_SIZE 128U
static char    s_ring[RTTY_RINGBUF_SIZE];
static uint16_t s_ring_head; /* next write position */
static uint16_t s_ring_tail; /* next read position */

/* --- Goertzel coefficients, precomputed once in rtty_init() --- */
static float s_mark_coeff;
static float s_mark_cos;
static float s_mark_sin;
static float s_space_coeff;
static float s_space_cos;
static float s_space_sin;
static float s_mark_hz;  /* live, adjustable - see rtty_set_mark_space_hz() */
static float s_space_hz; /* live, adjustable - see rtty_set_mark_space_hz() */

/* --- bit-sync state machine --- */
typedef enum {
    RTTY_ST_IDLE = 0,   /* waiting for a mark->space edge (candidate start bit) */
    RTTY_ST_RECEIVING   /* mid-character, sampling data/stop bits by elapsed sample count */
} rtty_state_t;

static rtty_state_t s_state;
static uint32_t s_samples_since_edge;
static uint8_t  s_bits_captured;
static uint8_t  s_shift_reg;
static uint8_t  s_prev_bit;     /* last block's bit decision, for edge detection */
static uint8_t  s_figs_shift;   /* 0=letters, 1=figures - current Baudot shift state */

/*
 * LA INVERSION, SOLA - 06/10/2026.
 *
 * *** El dueño, despues de pasarse media mañana con 3712 sacando basura
 * porque el desplazamiento estaba del reves: "lo de la inversion no lo
 * puede detectar automaticamente?". ***
 *
 * Puede, y sale casi gratis: los dos Goertzels YA estan calculados y la
 * polaridad contraria es el mismo bit con el signo cambiado. O sea que no
 * hay que medir nada nuevo - solo hay que mirar que pasaria.
 *
 * COMO SE SABE CUAL ES LA BUENA, sin leer el texto. Un caracter de ITA2
 * lleva arranque a espacio y parada a marca, y la parada se comprueba 6,5
 * periodos despues del flanco. Con la polaridad CORRECTA esa parada esta
 * donde tiene que estar casi siempre; con la contraria, el "flanco de
 * arranque" que engancha la maquina es ruido y la parada cae donde cae.
 * Asi que no hace falta entender lo que pone: basta contar cuantos marcos
 * cierran bien por un lado y por el otro.
 *
 * La SOMBRA es esa cuenta. Lleva la misma maquina de estados pero no
 * decodifica nada: ni registro de desplazamiento, ni letras/cifras, ni
 * cola. Cuatro variables y un contador.
 *
 * Y SOLO SE MUEVE CUANDO LA ACTIVA VA MAL. La condicion no es "la sombra
 * va mejor", es "la activa va mal Y la sombra la triplica". Sin esa
 * primera mitad, una radio que esta decodificando perfectamente podria
 * darse la vuelta sola por una racha de ruido, y eso es peor que no tener
 * la deteccion: el dueño ya ha elegido, y lo que elige el dueño no se le
 * cambia por detras mientras le este funcionando.
 *
 * COMPROBADO EN ANTENA - 06/10/2026, sobre otro RTTY distinto del que
 * sirvio para ajustar los numeros. *** El dueño: "lo ha hecho, primero ha
 * sacado 2 lineas de basura y luego ya a decodificado bien". ***
 *
 * Dos lineas de basura son unos cuarenta caracteres, o sea una ventana de
 * 48 marcos sin llegar a cerrar: justo lo que las cuentas dicen que tiene
 * que tardar. Y no se toco ni un umbral despues de medirlos en la
 * grabacion de 3712 - son los mismos numeros contra una señal distinta.
 */
typedef struct {
    rtty_state_t est;
    uint32_t     desde;
    uint8_t      nbits;
    uint8_t      ant;
    uint16_t     bien;
    uint16_t     marcos;
} rtty_sombra_t;

/*
 * LOS NUMEROS, MEDIDOS SOBRE LA GRABACION DE 3712 DEL DUEÑO y no puestos a
 * ojo. La misma grabacion, metida por aqui con las dos polaridades, da
 * esto por ventana de marcos:
 *
 *                    del reves          bien puesta
 *     activa        77 / 52 / 69 %     94 / 100 /  98 %
 *     sombra        94 / 100 / 99 %    77 /  54 /  67 %
 *
 * Entre 77 y 94 cabe una puerta de sobra. Las tres condiciones de abajo
 * -la activa floja, la sombra buena de verdad, y quince puntos de
 * diferencia- separan las dos columnas sin rozar ninguna.
 */
#define RTTY_INV_VENTANA  48U   /* marcos mirados antes de decidir: ~7 s a 50 Bd */
#define RTTY_INV_FLOJA    85U   /* la activa por debajo de esto es "va mal" */
#define RTTY_INV_BUENA    90U   /* y la sombra por encima de esto es "va bien" */
#define RTTY_INV_MARGEN   15U   /* puntos de diferencia que hay que sacar */

static rtty_sombra_t s_sombra;
static uint16_t s_marcos;       /* marcos cerrados por la activa, buenos o malos */
static uint16_t s_bien;         /* y cuantos de ellos cerraron bien */
static uint16_t s_auto_inv_n;   /* veces que se ha dado la vuelta sola */
static uint8_t  s_salud_pc = 100U;
static uint32_t s_marcos_total;
static uint16_t s_salud_n;    /* ventanas de salud cerradas */  /* % de marcos que cerraron bien en la ultima ventana */
static uint32_t s_bit_period_samples; /* Fs/baud, precomputed in rtty_init() */

static uint8_t s_enabled;
static uint8_t s_last_bit;
static float   s_last_mark_mag;
static float   s_last_space_mag;

/*
 * El nivel de cada tono CUANDO SUENA. Ver el comentario largo de
 * rtty_process(). goertzel_mag() devuelve magnitud AL CUADRADO, asi que
 * estos son potencias y el suelo tiene que ser una potencia tambien.
 */
#define RTTY_REF_ALFA   (1.0f / 512.0f)   /* ~1,4 s a 32 muestras y 12 kHz */
#define RTTY_REF_SUELO  (1e-9f)
static float   s_mark_ref;
static float   s_space_ref;

/*
 * LA VENTANA DE MEDIDA, QUE NO TIENE POR QUE SER EL BLOQUE - 06/10/2026.
 *
 * El Goertzel corria sobre RTTY_BLOCK_SAMPLES, o sea 32 muestras. A 12 kHz
 * eso son 2,67 ms, y la resolucion de un Goertzel es 1/ventana:
 *
 *     1 / 2,67 ms  =  375 Hz
 *
 * Con un desplazamiento de 450 Hz, los dos tonos estan a UNA RESOLUCION de
 * distancia. O sea que el filtro de la marca no ve solo la marca: se le
 * cuela media raya del espacio, y al reves. Y peor: entran 375 Hz de ruido
 * en cada medida cuando la señal ocupa 50.
 *
 * Con una señal fuerte -el 3382 del dueño- sobra margen y sale igual. Con
 * una floja -el 3712, la misma emision por otro camino- no sale.
 *
 * La ventana buena es MEDIO BIT: a 50 baudios, 10 ms y 100 Hz de
 * resolucion, que separa los 450 de sobra y mete cuatro veces menos ruido
 * -6 dB-. Medio y no un bit entero porque la decision se sigue tomando
 * cada 32 muestras, y una ventana de un bit entero se comeria el bit de al
 * lado en cada transicion.
 *
 * El bloque SIGUE siendo de 32: lo que cambia es que se mira hacia atras.
 * Por eso hace falta el historial - en la pila no, que esto corre en la
 * cadena del audio.
 */
/*
 * 128 Y NO 256: la ventana mas larga que se llega a pedir es medio bit a
 * 45,45 baudios, o sea 132 muestras, y redondeada hacia abajo a bloques
 * enteros son 128. Con 256 sobraba medio kilobyte de RAM, y de RAM libre
 * quedan menos de dos - el enlazador lo dijo sin rodeos: "region RAM
 * overflowed by 80 bytes".
 */
#define RTTY_VENT_MAX   128U
#define RTTY_VENT_MIN    32U
static float   s_hist[RTTY_VENT_MAX];
static uint16_t s_hist_n;        /* cuantas muestras validas hay */
static uint16_t s_vent;          /* la ventana de ahora, en muestras */

/* --- baud rate (bit period), see rtty_set_baud()'s comment in rtty.h --- */
static float s_baud;

/* --- station NORMAL/REVERSE convention, see rtty_set_station_inverted()'s
 * comment in rtty.h - purely a flag + a live swap-on-change, independent
 * of s_mark_hz/s_space_hz's absolute values (which the USB/LSB mode
 * mirror and the live encoder nudge both still own). --- */
static uint8_t s_station_inverted;

/*
 * Block Goertzel magnitude at one frequency, given its precomputed
 * coeff/cos/sin (see rtty_init()) - standard single-bin DFT, O(n)
 * per call, n=RTTY_BLOCK_SAMPLES. Returns magnitude-SQUARED (no
 * sqrtf() - only the mark-vs-space COMPARISON matters for the bit
 * decision, so the extra sqrtf() would be wasted cycles).
 */
static float goertzel_mag(const float *samples, uint32_t n, float coeff, float c, float s)
{
    float s0, s1 = 0.0f, s2 = 0.0f;
    uint32_t i;
    float real, imag;

    for (i = 0; i < n; i++) {
        s0 = samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    real = s1 - s2 * c;
    imag = s2 * s;
    return real * real + imag * imag;
}

static void ring_push(char c)
{
    uint16_t next = (uint16_t)((s_ring_head + 1U) % RTTY_RINGBUF_SIZE);

    if (next == s_ring_tail) {
        return; /* full - drop the character rather than overwrite unread ones */
    }
    s_ring[s_ring_head] = c;
    s_ring_head = next;
}

/*
 * Baudot 5-bit code -> character, tracking FIGS/LTRS shift state.
 * Shift codes update s_figs_shift and push nothing. '\0' table
 * entries (genuinely blank codes, or the handful of control
 * characters this table doesn't map to ASCII - see k_baudot_figs'
 * comment) also push nothing, rather than a wrong/misleading glyph.
 */
static void rtty_baudot_decode(uint8_t code)
{
    char c;

    if (code == BAUDOT_FIGS) {
        s_figs_shift = 1U;
        return;
    }
    if (code == BAUDOT_LTRS) {
        s_figs_shift = 0U;
        return;
    }

    c = s_figs_shift ? k_baudot_figs[code] : k_baudot_letters[code];
    if (c != '\0') {
        ring_push(c);
    }
}

/*
 * La misma maquina de estados, pero SIN decodificar: solo mira si el marco
 * cierra con su bit de parada. Ver el comentario de rtty_sombra_t.
 */
static void sombra_tick(rtty_sombra_t *m, uint8_t bit)
{
    if (m->est == RTTY_ST_IDLE) {
        if (m->ant == 1U && bit == 0U) {
            m->est = RTTY_ST_RECEIVING;
            m->desde = RTTY_BLOCK_SAMPLES;
            m->nbits = 0U;
        }
        m->ant = bit;
        return;
    }
    m->desde += RTTY_BLOCK_SAMPLES;
    while (m->nbits < 5U
           && m->desde >= (uint32_t)((float)s_bit_period_samples * ((float)m->nbits + 1.5f))) {
        m->nbits++;
    }
    if (m->nbits == 5U
        && m->desde >= (uint32_t)((float)s_bit_period_samples * 6.5f)) {
        m->marcos++;
        if (bit == 1U) { m->bien++; }
        m->est = RTTY_ST_IDLE;
        m->ant = bit;
    }
}

/*
 * Per-block bit-sync/framing state machine - see this file's
 * top-of-file pipeline comment (step 3) for the design. Called once
 * per rtty_process() with that block's bit decision.
 */
static void rtty_state_machine(uint8_t bit)
{
    if (s_state == RTTY_ST_IDLE) {
        if (s_prev_bit == 1U && bit == 0U) {
            /* mark->space edge - candidate start bit, sync here */
            s_state = RTTY_ST_RECEIVING;
            s_samples_since_edge = RTTY_BLOCK_SAMPLES; /* this block IS the start bit */
            s_bits_captured = 0U;
            s_shift_reg = 0U;
        }
        s_prev_bit = bit;
        return;
    }

    /* RTTY_ST_RECEIVING */
    s_samples_since_edge += RTTY_BLOCK_SAMPLES;

    /* Data bit k (0..4) center target: (k + 1.5) bit periods after the
     * edge (edge=start of the start bit, which occupies the first
     * bit period). A while loop (not if) in case one block somehow
     * spans more than one target - shouldn't happen at 32
     * samples/block vs ~264 samples/bit, but cheap to be safe. */
    while (s_bits_captured < 5U
           && s_samples_since_edge >= (uint32_t)((float)s_bit_period_samples * ((float)s_bits_captured + 1.5f))) {
        s_shift_reg = (uint8_t)(s_shift_reg | (uint8_t)(bit << s_bits_captured));
        s_bits_captured++;
    }

    /* Stop bit check: center target 6.5 bit periods after the edge
     * (1 start + 5 data periods = 6, +0.5 to land mid-stop-bit).
     * Works whether the far end sends 1 or 1.5 stop bits - either
     * way mark should be present at the 6.5-period mark. */
    if (s_bits_captured == 5U
        && s_samples_since_edge >= (uint32_t)((float)s_bit_period_samples * 6.5f)) {
#if CONFIG_RTTY_DIAG_ENABLED
        /* Raw bit-level dump - see CONFIG_RTTY_DIAG_ENABLED's comment
         * in config.h. s_shift_reg is LSB-first (bit 0 = first bit
         * received after the start bit) - the SAME numbering the
         * k_baudot_letters/k_baudot_figs tables are indexed by, so
         * this value can be checked directly against a Baudot/ITA2
         * table by hand if a decoded message looks systematically
         * wrong (e.g. every code off by a fixed amount would point at
         * a bit-order bug here rather than bit-sync timing). */
        debug_print_dec("rtty: raw 5-bit code (LSB-first)", (uint32_t)s_shift_reg);
        debug_print(bit ? "rtty: stop bit OK\n" : "rtty: *** framing error, no stop bit ***\n");
#endif
        s_marcos++;
        s_marcos_total++;
        if (bit == 1U) {
            s_bien++;
            rtty_baudot_decode(s_shift_reg);
        }
        /* else: framing error (no stop bit where expected) - drop
         * the character. */
        s_state = RTTY_ST_IDLE;
        s_prev_bit = bit;
    }
}

/*
 * Recomputes the Goertzel coefficients from s_mark_hz/s_space_hz -
 * shared by rtty_init() (from config.h's CONFIG_RTTY_MARK_HZ/SPACE_HZ
 * defaults) and rtty_set_mark_space_hz() (live adjustment - see its
 * own comment for why this needs to exist at all).
 */
static void rtty_recompute_coeffs(void)
{
    float w_mark  = 2.0f * 3.14159265358979f * s_mark_hz  / RTTY_FS_HZ;
    float w_space = 2.0f * 3.14159265358979f * s_space_hz / RTTY_FS_HZ;

    s_mark_cos   = cosf(w_mark);
    s_mark_sin   = sinf(w_mark);
    s_mark_coeff = 2.0f * s_mark_cos;
    s_space_cos   = cosf(w_space);
    s_space_sin   = sinf(w_space);
    s_space_coeff = 2.0f * s_space_cos;
}

void rtty_init(void)
{
    s_mark_hz  = CONFIG_RTTY_MARK_HZ;
    s_space_hz = CONFIG_RTTY_SPACE_HZ;
    rtty_recompute_coeffs();

    s_baud = CONFIG_RTTY_BAUD;
    s_bit_period_samples = (uint32_t)(RTTY_FS_HZ / s_baud + 0.5f);

    s_state = RTTY_ST_IDLE;
    s_samples_since_edge = 0U;
    s_bits_captured = 0U;
    s_shift_reg = 0U;
    s_prev_bit = 1U; /* assume idle (mark) at startup */
    s_figs_shift = 0U;

    s_ring_head = 0U;
    s_ring_tail = 0U;

    s_enabled = CONFIG_RTTY_ENABLED_DEFAULT;
    s_last_bit = 1U;
    s_last_mark_mag = 0.0f;
    s_last_space_mag = 0.0f;
    s_mark_ref = 0.0f;
    s_space_ref = 0.0f;
    s_hist_n = 0U;
    memset(s_hist, 0, sizeof s_hist);
    memset(&s_sombra, 0, sizeof s_sombra);
    s_sombra.ant = 1U;
    s_marcos = 0U;
    s_bien = 0U;
    s_station_inverted = 0U; /* NORMAL by default - matches CONFIG_RTTY_MARK_HZ/SPACE_HZ's own polarity */

    debug_print_dec("rtty: init, bit period (samples @ 12kHz)", s_bit_period_samples);
}

void rtty_process(const float *audio, uint32_t n)
{
    float mark_mag, space_mag;
    uint8_t bit;

    if (!s_enabled) {
        return;
    }
    if (n != RTTY_BLOCK_SAMPLES) {
        return; /* contract violation - see rtty.h, silently ignore rather than read OOB */
    }

    /*
     * El bloque nuevo al final del historial, lo viejo se corre hacia
     * atras. Son 256 flotantes como mucho y un bloque son 32: ocho
     * memmove al segundo de 1 kB. Un anillo seria mas fino pero obligaria
     * a partir el Goertzel en dos tramos, y el Goertzel es recursivo: hay
     * que darle las muestras SEGUIDAS y en orden.
     */
    if (s_vent < RTTY_VENT_MIN) { s_vent = RTTY_VENT_MIN; }
    memmove(&s_hist[0], &s_hist[RTTY_BLOCK_SAMPLES],
            (RTTY_VENT_MAX - RTTY_BLOCK_SAMPLES) * sizeof s_hist[0]);
    memcpy(&s_hist[RTTY_VENT_MAX - RTTY_BLOCK_SAMPLES], audio,
           RTTY_BLOCK_SAMPLES * sizeof s_hist[0]);
    if (s_hist_n < RTTY_VENT_MAX) { s_hist_n = (uint16_t)(s_hist_n + RTTY_BLOCK_SAMPLES); }

    {
        uint16_t v = (s_hist_n < s_vent) ? s_hist_n : s_vent;
        const float *p = &s_hist[RTTY_VENT_MAX - v];
        mark_mag  = goertzel_mag(p, v, s_mark_coeff,  s_mark_cos,  s_mark_sin);
        space_mag = goertzel_mag(p, v, s_space_coeff, s_space_cos, s_space_sin);
    }

    /*
     * CADA TONO CONTRA SI MISMO, NO CONTRA EL OTRO - 06/10/2026.
     *
     * *** El dueño, con la misma emision -el boletin del DWD- en dos
     * frecuencias: en 3382 el texto entero, en 3712 "solo me decodifca
     * mierda". Y despues: "quiero que se pueda decodificar el 3712". ***
     *
     * Las dos estaban bien sintonizadas. Medido sobre sus dos fotos, las
     * rayas del oscilador caen ENCIMA de los tonos en las dos:
     *
     *     3712   rayas  918 y 1385 Hz    tonos  927 y 1374 Hz
     *     3382   rayas 2438 y 2904 Hz    tonos 2438 y 2904 Hz
     *
     * Lo que las separa es el desvanecimiento, y lo que no lo aguantaba
     * era esta linea:
     *
     *     bit = (mark_mag >= space_mag) ? 1U : 0U;
     *
     * Un tono contra EL OTRO. Es la MISMA piedra que tenia stanag_det.c
     * hasta esa misma mañana, y por la misma razon fisica: 850 -o 450- Hz
     * de separacion son dos caminos distintos por la ionosfera, y se
     * desvanecen por separado. Cuando la marca llega 10 dB mas floja que
     * el espacio, esa comparacion no decide que bit se ha transmitido:
     * decide cual de los dos tonos se esta desvaneciendo menos, y dice
     * "espacio" durante todo el rato que dure el desvanecimiento.
     *
     * LO QUE SE MIDE AHORA. De cada tono se lleva el nivel QUE TIENE
     * CUANDO LE TOCA SONAR -su referencia- y se compara cada uno con la
     * suya:
     *
     *     mark_mag / s_mark_ref   contra   space_mag / s_space_ref
     *
     * que sin dividir es el producto cruzado de abajo. Si la marca llega
     * la mitad de fuerte que el espacio pero AHORA esta en su nivel y el
     * espacio no, gana la marca, que es lo correcto.
     *
     * La referencia se actualiza SOLO con el tono que va ganando, nunca
     * con los dos a la vez: lo que se quiere estimar es cuanto vale ese
     * tono encendido, no su media con el apagado. Por eso un idle largo
     * -todo marca- no arrastra la referencia del espacio hacia el ruido;
     * se queda en el ultimo valor bueno que se le vio.
     *
     * La constante es 1/512 de bloque, y un bloque son 32 muestras a 12
     * kHz: 1,4 s de memoria. Tiene que ser MUCHO mas larga que un
     * caracter -150 ms a 50 baudios- para que no la mueva el propio
     * texto, y mucho mas corta que un desvanecimiento, que dura decenas
     * de segundos.
     *
     * Y MIENTRAS NO HAYA VISTO LOS DOS TONOS, la regla de siempre. Con
     * una referencia a cero el producto cruzado daria cero contra algo y
     * decidiria siempre lo mismo: justo el fallo que se viene a quitar.
     */
    if (mark_mag >= space_mag) {
        s_mark_ref  += (mark_mag  - s_mark_ref)  * RTTY_REF_ALFA;
    } else {
        s_space_ref += (space_mag - s_space_ref) * RTTY_REF_ALFA;
    }

    if (s_mark_ref > RTTY_REF_SUELO && s_space_ref > RTTY_REF_SUELO) {
        bit = (mark_mag * s_space_ref >= space_mag * s_mark_ref) ? 1U : 0U;
    } else {
        bit = (mark_mag >= space_mag) ? 1U : 0U;
    }

    s_last_mark_mag  = mark_mag;
    s_last_space_mag = space_mag;
    s_last_bit = bit;

    rtty_state_machine(bit);

    /*
     * Y la sombra: la misma trama con la polaridad contraria. Ver
     * rtty_sombra_t. Cuesta cuatro variables y ninguna medida nueva.
     */
    sombra_tick(&s_sombra, (uint8_t)(bit ^ 1U));
    rtty_auto_bit(bit);

    if (s_marcos >= RTTY_INV_VENTANA) {
        /*
         * La activa va mal Y la sombra la triplica: es que el
         * desplazamiento esta del reves. Se da la vuelta sola.
         *
         * rtty_set_station_inverted() intercambia marca y espacio y
         * recalcula los coeficientes, que es exactamente lo que hace el
         * boton de Ajustes - asi el ajuste guardado y lo que se ve en
         * pantalla siguen diciendo la verdad despues de darse la vuelta.
         */
        uint32_t act = (uint32_t)s_bien * 100UL;              /* por s_marcos */
        uint32_t som = (uint32_t)s_sombra.bien * 100UL;       /* por s_sombra.marcos */
        uint8_t  vale = 0U;

        if (s_sombra.marcos >= (RTTY_INV_VENTANA / 2U)) {
            vale = (uint8_t)(
                  act < (uint32_t)s_marcos * RTTY_INV_FLOJA
               && som >= (uint32_t)s_sombra.marcos * RTTY_INV_BUENA
               /* y la sombra saca RTTY_INV_MARGEN puntos a la activa,
                * multiplicado en cruz para no dividir */
               && som * (uint32_t)s_marcos
                  >= (act + (uint32_t)RTTY_INV_MARGEN * (uint32_t)s_marcos)
                     * (uint32_t)s_sombra.marcos);
        }
        if (vale) {
            rtty_set_station_inverted((uint8_t)(s_station_inverted ? 0U : 1U));
            s_auto_inv_n++;
            debug_print("rtty: desplazamiento del reves, me doy la vuelta\n");
            /* Y a empezar de cero: la maquina llevaba medio caracter con
             * la polaridad vieja, y las cuentas son de otra medida. */
            s_state = RTTY_ST_IDLE;
            s_prev_bit = 1U;
            s_bits_captured = 0U;
        }
        s_salud_pc = (uint8_t)(((uint32_t)s_bien * 100UL) / (uint32_t)s_marcos);
        s_salud_n++;
        s_marcos = 0U;
        s_bien = 0U;
        s_sombra.bien = 0U;
        s_sombra.marcos = 0U;
    }
}

uint8_t rtty_get_char(char *out)
{
    if (s_ring_tail == s_ring_head) {
        return 0U;
    }
    *out = s_ring[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1U) % RTTY_RINGBUF_SIZE);
    return 1U;
}

void rtty_set_enabled(uint8_t on)
{
    s_enabled = on ? 1U : 0U;
    /* Re-sync from a clean slate every time this gets toggled, same
     * "don't trust stale mid-character state across an on/off" idea
     * as the WFM/AM settle-mute resets on mode entry. */
    s_state = RTTY_ST_IDLE;
    s_prev_bit = 1U;
    s_bits_captured = 0U;
    debug_print(s_enabled ? "rtty: on\n" : "rtty: off\n");
}

uint8_t rtty_get_enabled(void)
{
    return s_enabled;
}

uint8_t rtty_get_last_bit(void)      { return s_last_bit; }
float   rtty_get_last_mark_mag(void) { return s_last_mark_mag; }
float   rtty_get_last_space_mag(void){ return s_last_space_mag; }

/*
 * Live mark/space Hz adjustment - added 08/08/2026, per the project
 * owner, after the first real test: with the RTTY tuning scope
 * (rtty_scope.c) showing the two live peaks, config.h's CONFIG_RTTY_
 * MARK_HZ/SPACE_HZ defaults didn't line up with them (expected - see
 * config.h's own "no universal correct value" warning on those
 * defines). Recompiling for every 100Hz nudge to find the right
 * values would be painfully slow to iterate with, so this makes
 * mark/space runtime-adjustable instead (see main.c's
 * tune_encoder_poll() - the encoder temporarily repurposes to this,
 * 100Hz/detent, while the scope is showing) - once a good pair of
 * values is found this way, the right move is to update config.h's
 * CONFIG_RTTY_MARK_HZ/SPACE_HZ to match, so a normal (non-scope)
 * session starts correctly tuned without needing to redo this dance.
 */
void rtty_set_mark_space_hz(float mark_hz, float space_hz)
{
    s_mark_hz = mark_hz;
    s_space_hz = space_hz;
    rtty_recompute_coeffs();
}

float rtty_get_mark_hz(void)  { return s_mark_hz; }
float rtty_get_space_hz(void) { return s_space_hz; }

/*
 * Shift (the Hz separation between mark/space) as its own adjustable
 * parameter - added 08/08/2026, per the project owner, alongside a
 * RADIO-page menu tile (see main.c's ENCODER_TARGET_RTTY_SHIFT).
 * Complements rtty_set_mark_space_hz()'s encoder-driven nudge (which
 * moves mark AND space together, preserving shift) with the other
 * axis: changing shift WITHOUT moving the center frequency, for
 * switching between standard shifts (170Hz is the near-universal HF
 * default this file already uses, but 200/425/850Hz all see real use
 * depending on band/mode/region) without needing to re-tune the
 * center by hand afterward.
 *
 * Preserves both the center frequency ((mark+space)/2) and the
 * current polarity (whether mark is above or below space, i.e.
 * whichever of RTTY_VARIANT_NORMAL/INVERTED is currently active - see
 * k_demod_modes[]'s comment) - only the DISTANCE between them changes.
 */
void rtty_set_shift_hz(float shift_hz)
{
    float center = (s_mark_hz + s_space_hz) * 0.5f;
    float half = shift_hz * 0.5f;
    uint8_t mark_is_higher = (s_mark_hz > s_space_hz) ? 1U : 0U;

    if (mark_is_higher) {
        s_mark_hz  = center + half;
        s_space_hz = center - half;
    } else {
        s_mark_hz  = center - half;
        s_space_hz = center + half;
    }
    rtty_recompute_coeffs();
}

float rtty_get_shift_hz(void)
{
    float diff = s_mark_hz - s_space_hz;
    return (diff < 0.0f) ? -diff : diff;
}

/*
 * Baud rate - see rtty.h's comment. Resyncs the bit-sync state machine
 * from a clean slate, same reasoning as rtty_set_enabled()'s toggle:
 * s_bit_period_samples changing mid-character would otherwise corrupt
 * whatever frame was in flight (s_samples_since_edge was accumulated
 * against the OLD period).
 */
void rtty_set_baud(float baud)
{
    s_baud = baud;
    s_bit_period_samples = (uint32_t)(RTTY_FS_HZ / s_baud + 0.5f);
    /* medio bit, redondeado a bloques enteros y dentro de los topes */
    {
        uint32_t v = s_bit_period_samples / 2U;
        v = (v / RTTY_BLOCK_SAMPLES) * RTTY_BLOCK_SAMPLES;
        if (v < RTTY_VENT_MIN) { v = RTTY_VENT_MIN; }
        if (v > RTTY_VENT_MAX) { v = RTTY_VENT_MAX; }
        s_vent = (uint16_t)v;
    }
    s_state = RTTY_ST_IDLE;
    s_prev_bit = 1U;
    s_bits_captured = 0U;
    debug_print_dec("rtty: bit period (samples @ 12kHz) now", s_bit_period_samples);
}

float rtty_get_baud(void) { return s_baud; }

/*
 * Station NORMAL/REVERSE convention - see rtty.h's comment. Only swaps
 * mark_hz/space_hz when the flag actually CHANGES (tapping the tile
 * twice in a row without an intervening mode switch must not swap
 * twice back to the original) - same "compare before acting" shape as
 * menu_tile_rfagc_callback()'s OFF-transition handling in main.c.
 */
void rtty_set_station_inverted(uint8_t inverted)
{
    uint8_t new_inv = inverted ? 1U : 0U;

    if (new_inv != s_station_inverted) {
        float tmp = s_mark_hz;
        s_mark_hz = s_space_hz;
        s_space_hz = tmp;
        rtty_recompute_coeffs();
    }
    s_station_inverted = new_inv;
    debug_print(s_station_inverted ? "rtty: station convention now REVERSE\n" : "rtty: station convention now NORMAL\n");
}

uint8_t rtty_get_station_inverted(void) { return s_station_inverted; }

void rtty_reapply_station_inversion(void)
{
    if (s_station_inverted) {
        float tmp = s_mark_hz;
        s_mark_hz = s_space_hz;
        s_space_hz = tmp;
        rtty_recompute_coeffs();
    }
}

/* Cuantas veces se ha dado la vuelta sola. Ver rtty_sombra_t. */
uint16_t rtty_auto_inv_veces(void) { return s_auto_inv_n; }

/*
 * Que parte de los caracteres de la ultima ventana cerraron con su bit de
 * parada. Es la medida que usan la deteccion de inversion de aqui y el
 * ajuste automatico de rtty_auto.c para saber si hay que tocar algo: por
 * encima del 85% esto esta decodificando y no se le toca nada.
 */
uint8_t rtty_salud_pc(void) { return s_salud_pc; }

/* Marcos cerrados desde que arranco el modo: el reloj con el que
 * rtty_auto.c le da a cada velocidad candidata su turno. */
uint16_t rtty_marcos_n(void) { return (uint16_t)s_marcos_total; }

/*
 * Cuantas ventanas de salud se han cerrado. rtty_auto.c lo usa para saber
 * si el numero que esta leyendo es NUEVO: al cambiar la velocidad, la
 * ventana en curso todavia lleva caracteres de la velocidad anterior, y
 * puntuar una candidata con la mitad de los marcos de otra es como medir
 * dos cosas y apuntar una.
 */
uint16_t rtty_salud_n(void) { return s_salud_n; }

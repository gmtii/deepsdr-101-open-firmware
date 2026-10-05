#ifndef SAM_H
#define SAM_H

#include "arm_math.h"

/*
 * SAM - synchronous AM demodulation (both sidebands), 21/08/2026.
 *
 * Ported from Jorge's own sam.c/sam.h, itself derived from Warren
 * Pratt's WDSP SAM demodulator (2016, GPL, used in PowerSDR/Thetis
 * and many other open SDR projects). Two real, deliberate departures
 * from that source, both documented here rather than silently:
 *
 * 1. NO Hilbert-transform carrier-selective path (SAML/SAMU - single-
 *    sideband synchronous demod that rejects the OTHER sideband using
 *    a 7-stage allpass Hilbert transformer, sam_variables_t's c0[]/
 *    c1[]/a[]/b[]/c[]/d[] arrays in the source Jorge provided). That
 *    source declares those arrays but never initializes them anywhere
 *    - the actual WDSP-standard 7-stage allpass pole coefficients
 *    aren't present in what was handed over, and this project would
 *    rather ship a plain, VERIFIED-correct both-sideband SAM now than
 *    guess at coefficients for a carrier-selective mode and risk
 *    shipping something subtly wrong. DEMOD_SAM (both sidebands) never
 *    touches those arrays in the original source either - it uses
 *    corr[0] directly - so this omission costs nothing for the mode
 *    actually implemented here. Add SAML/SAMU later once real,
 *    verified Hilbert coefficients are sourced.
 *
 * 2. Fixed a real typo in the original source's own (commented-out)
 *    frequency-offset calculation - a stray unmatched '(' before
 *    `omega2` left that line unable to compile as written:
 *      SAM_carrier = 0.08 * (omega2 * SAMPLERATE / (DF * TPI);
 *    sam_step()'s carrier_freq_offset_hz output has the corrected,
 *    complete version - see that function's own comment.
 *
 * 3. 05/10/2026: EL DETECTOR Y EL FILTRO DE LAZO NO CORREN POR MUESTRA,
 *    corren una de cada SAM_DR (4). El original tenia justo eso -su
 *    termino DF/DR- y esta version lo habia puesto a 1 "para no apartarse
 *    de la fuente". Apartarse de la fuente salia caro: el arcotangente del
 *    detector a 96 kHz era la mitad del coste del modo, y el modo entero
 *    estaba comiendose la CPU que le hace falta al resto de la radio. El
 *    porque de que no cambie el lazo, y como se escalan g1 y g2 para que
 *    no cambie, esta en sam.c, encima de sam_nucleo(). Lo comprueba
 *    sim/samtest.c contra el PLL exacto por muestra.
 *
 * Everything else - the PLL (phase detector/loop filter/NCO) and the
 * fade leveler (DC-tracking amplitude smoother for fading AM
 * carriers) - is ported as-is, same structure and constants as the
 * original.
 *
 * Runs on s_i_buf/s_q_buf (96kHz, post channel-filter, same tap AM's
 * own arm_cmplx_mag_f32() envelope detector already uses) - see
 * demod_am.c's DEMOD_MODE_SAM branch.
 */

/*
 * CADA CUANTAS MUESTRAS SE MIDE LA FASE Y SE MUEVE EL LAZO.
 *
 * 4 y no 8 por una razon medida, no por gusto: entre medida y medida las
 * muestras se suman en un fasor, y si la portadora esta en el tope del lazo
 * -4000 Hz- ese fasor gira 15 grados por muestra a 96 kHz y 30 a 48 kHz. Con
 * 4 muestras eso son 45 y 90 grados de recorrido, que todavia se promedian
 * bien; con 8 serian 105 y 210, y a 210 grados la suma se cancela y el
 * detector apunta al reves. El tope del lazo manda sobre el ahorro.
 */
#define SAM_DR  4U

typedef struct {
    /* PLL */
    float32_t pll_fmax;
    float32_t zeta;
    float32_t omegaN;
    float32_t omega_min;
    float32_t omega_max;
    float32_t g1;
    float32_t g2;
    float32_t det;
    float32_t fil_out;
    float32_t del_out;
    float32_t omega2;

    /*
     * LA FASE DEL OSCILADOR, EN UN ACUMULADOR DE 32 BITS Y NO EN RADIANES.
     *
     * Era un float al que se le sumaba del_out y al que despues habia que
     * devolver a [0, 2pi) con dos bucles. Ahora la vuelta entera son 2^32 y
     * la vuelta a rango la da sola la aritmetica sin signo: no hay bucles,
     * no hay comparaciones en coma flotante -que en este Cortex-M4 se pagan
     * pasando por el registro de estado del coprocesador- y, de propina, la
     * fase deja de acumular error de redondeo. `inc` es del_out ya pasado a
     * esa rejilla, y se recalcula solo cuando el lazo se mueve, o sea una de
     * cada SAM_DR muestras.
     */
    uint32_t  fase;
    int32_t   inc;

    /* El fasor sumado entre dos medidas, y lo que falta para la proxima. */
    float32_t acum0;
    float32_t acum1;
    uint8_t   cuenta;

    /*
     * ENGANCHE. Media corrida de |det|, que es lo unico que distingue un
     * numero que significa algo de un numero inventado.
     *
     * Sin portadora el detector da angulos repartidos por toda la vuelta y
     * la media de su valor absoluto tiende a pi/2; enganchado se queda cerca
     * de cero, y cuanto peor la relacion señal/ruido mas sube. O sea que es
     * una medida continua y no un si/no con un umbral escondido: ver
     * sam_enganche().
     */
    float32_t det_med;
    float32_t a_det;             /* constante de la media de arriba */

    /* fade leveler */
    float32_t tauR;
    float32_t tauI;
    float32_t dc;
    float32_t dc_insert;
    float32_t mtauR;
    float32_t onem_mtauR;
    float32_t mtauI;
    float32_t onem_mtauI;
    uint8_t   fade_leveler; /* on/off - matches sam_variables_t's own flag, default on */

    /* outputs, updated every sam_init()/sam_step() call */
    float32_t corr0; /* = det's own numerator - real part of the carrier-relative signal, this mode's actual audio */
    float32_t corr1; /* imaginary part - phase-detector input, not audio */
    float32_t audio;
    float32_t carrier_hz_raw;    /* omega2 pasado a Hz, al instante */
    /*
     * LA LECTURA PROMEDIADA DE VERDAD - 05/10/2026.
     *
     * *** El dueño: "en radio china en 7265 me pone +13ppm" despues de haber
     * leido +23,7 en la misma emisora. ***
     *
     * Y no era el cristal: era que esto NO PROMEDIABA. Ponia
     * 0,08*nuevo + 0,92*viejo POR MUESTRA, y 0,92 elevado a las 256 muestras
     * de un bloque es 5e-10 - o sea que de lo anterior no quedaba nada y lo
     * que se leia era la frecuencia instantanea del lazo al final del bloque,
     * saltando con cada desvanecimiento. Un suavizado que no suaviza es peor
     * que ninguno, porque parece que si.
     *
     * Ahora la constante de tiempo es SAM_TAU_S segundos de verdad, y se
     * calcula con las muestras que han pasado - asi vale igual llamando por
     * bloques que muestra a muestra.
     */
    float32_t carrier_hz;
} sam_t;

/* Lo que tarda la lectura de arriba en seguir un cambio, en segundos. Un
 * segundo: lo justo para que el numero se quede quieto mientras se lee y no
 * tanto como para que haya que esperar mirando la pantalla. */
#define SAM_TAU_S  1.0f

/* sample_rate_hz: the rate sam_step() is actually called at (this
 * project's s_i_buf/s_q_buf, 96000.0f).
 * pll_fmax_hz/omega_n_hz/zeta: same meaning and same WDSP defaults as
 * the original (4000.0f / 200.0f / 65.0f/75.0f). */
void sam_init(sam_t *s, float32_t sample_rate_hz, float32_t pll_fmax_hz,
        float32_t omega_n_hz, float32_t zeta);

/* Resets the PLL's running state (phzerror/det/fil_out/del_out/omega2
 * and the carrier_hz readings) without recomputing g1/g2/omega_min/
 * omega_max - call when retuning to a different station so the loop
 * re-acquires cleanly instead of carrying over the previous station's
 * (irrelevant) frequency estimate. Does not touch the fade leveler's
 * own dc/dc_insert state - a brief fade-leveler transient after
 * retuning is harmless and self-corrects within tauR/tauI, unlike a
 * stale PLL frequency estimate which would actively fight the new
 * carrier. */
void sam_reset(sam_t *s);

/* Feeds one (i,q) sample through the demodulator. Returns the audio
 * sample (same value as s->audio afterward). sample_rate_hz must be
 * the SAME value passed to sam_init() - passed again here rather than
 * stored, matching this project's own established RAM-saving
 * convention elsewhere (see hfdl_project's sam_freq_offset.c, same
 * author, same session, same reasoning). */
float32_t sam_step(sam_t *s, float32_t i_in, float32_t q_in, float32_t sample_rate_hz);

/*
 * EL BLOQUE ENTERO DE UNA VEZ, que es como lo llama la radio - 05/10/2026.
 *
 * *** El dueño, tres veces: "al elegir modo sam la radio se ralentiza un
 * monton" / "el modo sam se sigue ralientizando" / "sam se sigue
 * ralentizando". ***
 *
 * No es azucar: es la mitad del arreglo. Llamando a sam_step() 256 veces, el
 * compilador tiene que suponer que el puntero `s` puede haber cambiado entre
 * una llamada y la siguiente, asi que recarga y reescribe en memoria las
 * doce variables del lazo EN CADA MUESTRA -veinte accesos- y paga ademas el
 * prologo y el epilogo de la llamada. Con el bucle dentro, todo eso se queda
 * en registros del coprocesador y solo se escribe una vez, al final.
 *
 * Las dos entradas comparten CUERPO -sam_nucleo(), en sam.c-, no hay dos
 * copias del PLL. sim/samtest.c comprueba que dan exactamente lo mismo
 * muestra a muestra, que es lo unico que hace que esa afirmacion valga algo.
 *
 * `audio` puede ser el mismo buffer que `i_in` o que `q_in`: se escribe una
 * muestra despues de leer las dos.
 */
void sam_bloque(sam_t *s, const float32_t *i_in, const float32_t *q_in,
                float32_t *audio, uint32_t n, float32_t sample_rate_hz);

/*
 * CUANTO SE FIA UNO DE carrier_hz: 0 nada, 1 del todo.
 *
 * Sale de la media corrida de |det| (ver det_med en el struct). Sin
 * portadora el detector apunta a cualquier sitio y esto se va a cero; con
 * una portadora limpia se acerca a uno. NO es un umbral: el que quiera un
 * si/no que use sam_enganchado(), que lo pone en un solo sitio.
 *
 * Para que sirve, y es la razon de que exista: la pantalla enseñaba el error
 * de PLL en PPM SIEMPRE, hubiera portadora o no, y un numero en una pantalla
 * se cree. Con esto se puede decir "no hay enganche" en vez de inventarse
 * -269 PPM.
 */
float32_t sam_enganche(const sam_t *s);

/* El si/no, con el umbral en UN solo sitio. Ver sim/samtest.c, que lo mide
 * con portadora, con portadora debil y con ruido solo. */
uint8_t sam_enganchado(const sam_t *s);

#endif /* SAM_H */

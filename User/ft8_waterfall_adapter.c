#include "ft8_waterfall_adapter.h"
#include "ft8_fft1024.h"
#include "ft8_shared_ram.h"

#if FT8_ADAPTER_NFFT != FT8_FFT_SIZE
#error "FT8_ADAPTER_NFFT must equal ft8_fft1024.h's FT8_FFT_SIZE - see ft8_waterfall_adapter.h's derivation comment"
#endif

#if (FT8_ADAPTER_MIN_RAW_BIN + (FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_RAW_BIN_STEP)) > FT8_FFT_BINS_USEFUL
#error "FT8_ADAPTER_NUM_BINS/MIN_RAW_BIN window exceeds ft8_fft1024_compute_db()'s available output bins"
#endif

/* Real memory union with the main waterfall's own pixel buffer - see
 * ft8_shared_ram.h for the full "why". Storage is defined once, in
 * waterfall.c - these are typed views into it via macros, so every
 * line below that already said s_window/s_dbout/s_mag
 * keeps working unchanged. */
#define s_window    (g_ft8_shared_ram.ft8.window)    /* sliding analysis window, oldest sample first - see ft8_waterfall_feed_subblock() */
#define s_dbout     (g_ft8_shared_ram.ft8.dbout)
#define s_lado      (g_ft8_shared_ram.ft8.lado)      /* ver su comentario en ft8_shared_ram.h */     /* scratch, reused every subblock */
#define s_mag       (g_ft8_shared_ram.ft8.mag)       /* ~46.5KB with NUM_BINS=256 - see header comment */

#define FT8_ADAPTER_MAG_SIZE (FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS)

static ftx_waterfall_t s_wf;
static int s_time_sub; /* 0..FT8_ADAPTER_TIME_OSR-1: which subblock within the CURRENT symbol block this feed will land in */

/* Calibration diagnostics - see FT8_ADAPTER_DB_OFFSET's comment in
 * the header. Tracks the min/max raw (pre-offset, pre-quantization)
 * dB value seen across the whole current slot, so FT8_ADAPTER_DB_OFFSET
 * can be tuned from a real capture instead of guessed. Reset alongside
 * everything else in ft8_waterfall_reset(). */
/* El desvio: mientras vale 1, mag[] esta congelado para el decodificador y
 * la captura escribe en s_lado. Ver ft8_waterfall_cierra_ranura(). */
static uint8_t  s_desviada;
static int      s_lado_n;      /* bloques ya escritos en el desvio */
static uint16_t s_desbordes;   /* los que no cupieron: audio perdido */

static float s_db_min;
static float s_db_max;

/*
 * AQUI VIVIAN LA CASCADA PROPIA DE FT8 Y SU ESPECTRO EN VIVO. 25/09/2026.
 *
 * Eran un historial de 16 filas y una tira de barras, pensados para que FT8
 * ensenara su propia ventana en pantalla. En ESTA radio no se pintan nunca:
 * el panel de FT8 es texto y barra de ranura, por decision del dueno del
 * proyecto ("para ft8 no me hace falta ver el espectro"). O sea que se
 * calculaban, se desplazaban fila a fila en cada subbloque, y nadie los
 * miraba.
 *
 * Se van porque costaban 20 bytes POR BIN -16 del historial, dentro de la
 * union, y 4 del espectro en vivo, fuera de ella- y los bins son justo lo
 * que hacia falta para ensanchar la ventana de busqueda. Medido compilando:
 * con ellos dentro el tope eran 160 bins (1.000 Hz); sin ellos, 192 (1.200).
 * Cambiar una vista que nadie mira por un 20% mas de banda escuchada no
 * tiene mucha discusion.
 *
 * Si algun dia se quiere esa vista: lo que habia esta en el historial de
 * git, y el espectro en vivo no hace falta guardarlo aparte -es la fila de
 * mag[] que se acaba de escribir-.
 */

void ft8_waterfall_reset(void)
{
    int i;

    for (i = 0; i < FT8_ADAPTER_NFFT; i++)
    {
        s_window[i] = 0; /* silence - the first ~NFFT/SUBBLOCK_SIZE feeds of a fresh slot will see some of this until the window fills with real audio, same transient any STFT has */
    }
    s_time_sub = 0;
    s_db_min = 1e9f;
    s_db_max = -1e9f;

    /*
     * OJO CON ESTA RAMA. Con la captura desviada, poner num_blocks a 0
     * haria que el siguiente subbloque escribiera en mag[0]... que es justo
     * lo que el decodificador esta leyendo. Ese era el fallo que la copia
     * entera de mag[] evitaba a base de memoria.
     *
     * Desviada, lo que se reinicia es el desvio: la ranura nueva vuelve a
     * empezar, pero en el buffer de al lado. mag[] no se toca.
     */
    if (s_desviada) {
        s_lado_n = 0;
    } else {
        s_wf.num_blocks = 0;
    }

    s_wf.max_blocks = FT8_ADAPTER_MAX_BLOCKS;
    s_wf.num_bins = FT8_ADAPTER_NUM_BINS;
    s_wf.time_osr = FT8_ADAPTER_TIME_OSR;
    s_wf.freq_osr = FT8_ADAPTER_FREQ_OSR;
    s_wf.mag = s_mag;
    s_wf.block_stride = FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS;
    s_wf.protocol = FTX_PROTOCOL_FT8;
}

bool ft8_waterfall_is_full(void)
{
    /* Desviada no puede estar llena: lo que se esta capturando son los
     * primeros bloques de la ranura siguiente, no una ranura entera. */
    if (s_desviada) { return false; }
    return s_wf.num_blocks >= FT8_ADAPTER_MAX_BLOCKS;
}

void ft8_waterfall_cierra_ranura(void)
{
    s_desviada = 1U;
    s_lado_n = 0;
    s_time_sub = 0;
    /* num_blocks se queda en 93: mag[] contiene la ranura entera y eso es
     * lo que el decodificador tiene que leer. */
}

void ft8_waterfall_reintegra(void)
{
    int i;
    int n;

    if (!s_desviada) { return; }

    n = s_lado_n * s_wf.block_stride;
    for (i = 0; i < n; i++) { s_mag[i] = s_lado[i]; }

    s_wf.num_blocks = s_lado_n;
    s_desviada = 0U;
    s_lado_n = 0;
}

uint8_t  ft8_waterfall_desviada(void)  { return s_desviada; }
uint16_t ft8_waterfall_desbordes(void) { return s_desbordes; }

const ftx_waterfall_t *ft8_waterfall_get(void)
{
    return &s_wf;
}

void ft8_waterfall_get_db_range(float *out_min, float *out_max)
{
    *out_min = s_db_min;
    *out_max = s_db_max;
}


void ft8_waterfall_feed_subblock(const int16_t *audio_samples)
{
    int i;
    int freq_sub;
    int bin;
    int offset = 0;
    /* Decoupled on purpose (09/2026, per the project owner): the
     * ventana deslizante y la FFT siguen corriendo aunque la ranura ya
     * este llena; lo unico que se detiene es la escritura en mag[], para
     * no pasarse del final del buffer mientras se espera al borde de
     * ranura que marca el reloj. (Antes esto ademas mantenia viva la
     * cascada propia de FT8, que ya no existe - ver la nota de arriba.) */
    /*
     * DONDE SE ESCRIBE ESTE SUBBLOQUE. Tres casos:
     *   - normal:   en mag[], en el bloque que toque.
     *   - desviada: en s_lado[], hasta que se llene.
     *   - llena:    en ningun sitio, esperando el borde de ranura.
     */
    uint8_t *destino = s_mag;
    bool     escribe = true;

    if (s_desviada) {
        if (s_lado_n >= FT8_ADAPTER_LADO_BLOQUES) {
            /* La decodificacion esta tardando mas que el desvio entero.
             * Se pierde audio, y se dice. */
            s_desbordes++;
            escribe = false;
        } else {
            destino = s_lado;
        }
    } else if (ft8_waterfall_is_full()) {
        escribe = false;
    }

    /* Slide the analysis window: drop the oldest SUBBLOCK_SIZE
     * samples, append the new ones at the end - same overlap
     * technique as ft8_lib's own monitor_process() ("shift
     * last_frame, add new subblock"), just against our own int16
     * buffer instead of a float one (fft_compute_db() applies the
     * Hann window internally, same as it does for the main spectrum -
     * no pre-windowing needed here). ALWAYS runs, same reasoning. */
    for (i = 0; i < FT8_ADAPTER_NFFT - FT8_ADAPTER_SUBBLOCK_SIZE; i++)
    {
        s_window[i] = s_window[i + FT8_ADAPTER_SUBBLOCK_SIZE];
    }
    for (i = 0; i < FT8_ADAPTER_SUBBLOCK_SIZE; i++)
    {
        s_window[FT8_ADAPTER_NFFT - FT8_ADAPTER_SUBBLOCK_SIZE + i] = audio_samples[i];
    }

    ft8_fft1024_compute_db(s_window, s_dbout);

    if (escribe)
    {
        int bloque = s_desviada ? s_lado_n : s_wf.num_blocks;
        offset = (bloque * s_wf.block_stride) + (s_time_sub * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS);
    }

    /* src_bin = bin*RAW_BIN_STEP + freq_sub*(RAW_BIN_STEP/FREQ_OSR) -
     * see FT8_ADAPTER_RAW_BIN_STEP's comment in the header for why
     * this is no longer simply "bin*freq_osr + freq_sub" (that only
     * matched common/monitor.c's own mapping back when FREQ_OSR
     * happened to equal the raw-bin-step by coincidence, before
     * FREQ_OSR was reduced to 1 for the wider-window memory budget -
     * see the header's MEMORY BUDGET CONTEXT comment). The "2*db+240,
     * clamp 0-255" encoding itself (WF_ELEM_MAG in decode.h decodes it
     * back as x*0.5-120) is unchanged. Solo se escribe s_mag[], que es lo
     * que decodifica ft8_lib, y solo mientras la ranura no este llena: las
     * ramas de cascada y espectro en vivo que habia aqui se fueron el
     * 25/09/2026 (ver la nota de mas arriba). */
    for (freq_sub = 0; freq_sub < FT8_ADAPTER_FREQ_OSR; freq_sub++)
    {
        for (bin = 0; bin < FT8_ADAPTER_NUM_BINS; bin++)
        {
            int src_bin = FT8_ADAPTER_MIN_RAW_BIN + (bin * FT8_ADAPTER_RAW_BIN_STEP) + (freq_sub * (FT8_ADAPTER_RAW_BIN_STEP / FT8_ADAPTER_FREQ_OSR));
            float db = s_dbout[src_bin] + FT8_ADAPTER_DB_OFFSET;
            int scaled = (int)(2.0f * db + 240.0f);

            if (db < s_db_min) { s_db_min = db; }
            if (db > s_db_max) { s_db_max = db; }

            if (scaled < 0) { scaled = 0; }
            if (scaled > 255) { scaled = 255; }

            if (escribe)
            {
                destino[offset] = (WF_ELEM_T)scaled;
                offset++;
            }
        }
    }

    if (escribe)
    {
        s_time_sub++;
        if (s_time_sub >= FT8_ADAPTER_TIME_OSR)
        {
            s_time_sub = 0;
            if (s_desviada) { s_lado_n++; } else { s_wf.num_blocks++; }
        }
    }
}

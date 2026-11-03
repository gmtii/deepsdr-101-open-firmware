#ifndef _INCLUDE_DECODE_H_
#define _INCLUDE_DECODE_H_

#include <stdint.h>
#include <stdbool.h>

#include "constants.h"
#include "message.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    float mag;
    float phase;
} waterfall_cpx_t;

// #define WATERFALL_USE_PHASE

#ifdef WATERFALL_USE_PHASE
#define WF_ELEM_T          waterfall_cpx_t
#define WF_ELEM_MAG(x)     ((x).mag)
#define WF_ELEM_MAG_INT(x) (int)(2 * ((x).mag + 120.0f))
#else
#define WF_ELEM_T          uint8_t
#define WF_ELEM_MAG(x)     ((float)(x)*0.5f - 120.0f)
#define WF_ELEM_MAG_INT(x) (int)(x)
#endif

/*
 * ======================================================================
 * *** DOS BINS POR BYTE - 28/10/2026, cambio de ESTE proyecto ***
 * ======================================================================
 *
 * ESTO NO ES DE ft8_lib: es una modificacion nuestra sobre su copia, y
 * esta anotada en PROCEDENCIA.md. Quien venga de arriba tiene que saberlo.
 *
 * POR QUE. mag[] es lo que decide cuanta banda se puede mirar: son
 * bloques x bins bytes, y en esta radio la memoria sale de una union
 * compartida con la cascada. Con un byte por bin, cubrir los 3.200 Hz de
 * audio pedia 47.616 bytes y no habia de donde sacarlos; con MEDIO byte
 * son los mismos 23.808 que ya costaban 1.600 Hz.
 *
 * Y NO CUESTA SENSIBILIDAD, medido antes de hacerlo en
 * deepsdr-ui/sim/ft8_banda.c: la rejilla de medio bin sale bin a bin
 * igual y el barrido de relacion senal/ruido se queda en los mismos
 * -18 dB. El motivo es que el decodificador no mira el valor absoluto de
 * un bin: compara los ocho tonos de cada simbolo entre si, y dieciseis
 * niveles repartidos sobre el margen de cada bloque le bastan.
 *
 * LA ESCALA. Se guardan 0..15 y se leen como 4 dB por nivel, centrados
 * donde estaba la escala de antes, para que **el numero que sale de
 * WF_MAG_INT_AT siga siendo el de siempre**: medios decibelios. Eso es lo
 * que permite no tocar FTX_MIN_SCORE ni ninguno de los umbrales que
 * cuelgan de el. Un nivel vale ocho unidades de las de antes.
 *
 * EL INDICE VA EN BINS, NO EN BYTES, y por eso las funciones de abajo
 * pasaron de llevar un puntero a llevar un entero: a medio byte no se
 * puede apuntar. Es el unico cambio de forma; la aritmetica es la misma.
 */
/*
 * ======================================================================
 * *** Y TRES BITS POR CELDA - 02/11/2026, tambien de ESTE proyecto ***
 * ======================================================================
 *
 * EL PORQUE, que es el mismo de arriba un paso mas alla. Con la ventana
 * en 3.200 Hz y FREQ_OSR=1 el decodificador pierde las emisoras que caen
 * a medio bin: medido en deepsdr-ui/sim/ft8_sensible.c, 216 de 216 en el
 * centro del bin contra 13 de 216 a medio bin. Subir FREQ_OSR a 2 lo
 * cierra -216 de 216- pero dobla mag[], y los 23.808 bytes que eso pide
 * no estaban.
 *
 * CON TRES BITS mag[] mide 35.712 en vez de 47.616, y entonces SI cabe.
 *
 * LO QUE CUESTA, medido y no estimado, barriendo de -11 a -20 dB con la
 * emision cayendo donde quiere en frecuencia y en tiempo:
 *
 *     hoy      FREQ_OSR 1, 4 bits ....  97 aciertos de 240
 *              FREQ_OSR 2, 4 bits .... 129   (no cabe en RAM)
 *     esto     FREQ_OSR 2, 3 bits .... 113
 *
 * O sea que de las 32 que da el submuestreo se quedan 16. La mitad del
 * premio por la mitad de la memoria, y el agujero del medio bin cerrado
 * igual. El umbral del 50 % se mueve 0,3 dB: -16,0 contra -15,7.
 *
 * LA ESCALA ES LA MISMA RECTA. El tramo de 51 dB no cambia; lo que
 * cambia es que se reparte en ocho escalones en vez de dieciseis, o sea
 * que el paso dobla. Por eso FTX_MAG_PASO_DB se calcula a partir de
 * WF_MAG_NIVELES y no es un numero suelto: cambiar FTX_MAG_BITS no puede
 * dejar la escalera descuadrada.
 *
 * EL EMPAQUETADO YA NO CAE EN BYTE. Tres bits no dividen a ocho, asi que
 * una celda puede partirse entre dos bytes. Las dos funciones de abajo
 * leen y escriben SIEMPRE dos bytes, que es mas barato que preguntar si
 * hace falta el segundo. A cambio, mag[] y el desvio llevan UN BYTE DE
 * RELLENO al final -FT8_ADAPTER_MAG_RELLENO-, porque la ultima celda
 * toca el byte siguiente. Sin ese byte se sale del array.
 */
#ifndef FTX_MAG_BITS
#define FTX_MAG_BITS 3
#endif

#if FTX_MAG_BITS == 3 || FTX_MAG_BITS == 4
/* La escalera: WF_MAG_NIVELES escalones a partir de FTX_MAG_BASE_DB. Se
 * pueden fijar desde fuera (-D) para barrerlos en el banco sin tocar el
 * fichero; los valores de abajo son los medidos. */
#ifndef FTX_MAG_BASE_DB
#define FTX_MAG_BASE_DB (-35.0f)
#endif
#define WF_MAG_NIVELES   (1 << FTX_MAG_BITS)
#ifndef FTX_MAG_PASO_DB
/* 54,4 son los 16 escalones de 3,4 dB con los que se midio la escalera a
 * cuatro bits: el MISMO tramo, repartido entre los escalones que haya. */
#define FTX_MAG_PASO_DB  (54.4f / (float)WF_MAG_NIVELES)
#endif

#if FTX_MAG_BITS == 4
#define WF_MAG_RAW(wf, i)  ((int)((((const uint8_t *)(wf)->mag)[(i) >> 1]   \
                                   >> (((i) & 1) ? 4 : 0)) & 0x0F))
/* Lo que hay que escribir en el byte `i>>1` para dejar el nivel `q`
 * (0..15) en el bin `i`. Lo usa el adaptador al guardar. */
#define WF_MAG_PON(base, i, q)                                             \
    do {                                                                   \
        uint8_t *b_ = &((uint8_t *)(base))[(i) >> 1];                      \
        if ((i) & 1) { *b_ = (uint8_t)((*b_ & 0x0FU) | ((q) << 4)); }      \
        else         { *b_ = (uint8_t)((*b_ & 0xF0U) | (q)); }             \
    } while (0)
#else
static inline int ftx_mag3_lee(const uint8_t *b, int i)
{
    unsigned bit = (unsigned)i * 3U;
    const uint8_t *p = &b[bit >> 3];
    unsigned v = (unsigned)p[0] | ((unsigned)p[1] << 8);
    return (int)((v >> (bit & 7U)) & 7U);
}

static inline void ftx_mag3_pon(uint8_t *b, int i, uint8_t q)
{
    unsigned bit = (unsigned)i * 3U;
    uint8_t *p = &b[bit >> 3];
    unsigned s = bit & 7U;
    unsigned v = (unsigned)p[0] | ((unsigned)p[1] << 8);

    v = (v & ~(7U << s)) | (((unsigned)q & 7U) << s);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
#define WF_MAG_RAW(wf, i)      ftx_mag3_lee((const uint8_t *)(wf)->mag, (i))
#define WF_MAG_PON(base, i, q) ftx_mag3_pon((uint8_t *)(base), (i), (uint8_t)(q))
#endif

#define WF_MAG_AT(wf, i)     ((float)WF_MAG_RAW(wf, i) * FTX_MAG_PASO_DB + FTX_MAG_BASE_DB)

/*
 * ======================================================================
 * *** LA ESCALERA, YA EN ENTEROS - 03/11/2026, cambio de ESTE proyecto.
 * Anotado en PROCEDENCIA.md. ***
 * ======================================================================
 *
 * EL PORQUE. WF_MAG_INT_AT era
 *
 *     (int)(2.0f * ((float)raw * FTX_MAG_PASO_DB + FTX_MAG_BASE_DB) + 240.0f)
 *
 * o sea: desempaquetar la celda, convertir a flotante, multiplicar,
 * sumar, multiplicar, sumar y convertir a entero. Y eso corre DENTRO del
 * bucle mas caliente de FT8: ft8_sync_score() lo llama hasta ocho veces
 * por simbolo de sincronismo, y la busqueda de candidatos llama a
 * ft8_sync_score() treinta mil veces por ranura. Son millones de
 * conversiones por ranura para leer una celda que solo puede valer
 * WF_MAG_NIVELES cosas distintas.
 *
 * LO QUE SE HACE. La tabla. Son ocho enteros -dieciseis si se compila con
 * cuatro bits- calculados por el compilador con la MISMA expresion, asi
 * que el valor que sale es el mismo bit a bit, truncado incluido. Lo que
 * desaparece es la aritmetica, no el resultado.
 *
 * Y EL TRUNCADO IMPORTA, por eso la tabla y no una resta escalada: los
 * niveles truncan a 170, 183, 197, 210, 224, 238, 251 y 265, o sea que la
 * diferencia entre vecinos vale unas veces 13 y otras 14. Calcular la
 * puntuacion sobre niveles crudos y escalar al final habria dado otros
 * numeros -parecidos, pero otros- y con ellos otro orden de candidatos.
 * Aqui no cambia ninguno.
 */
#define FTX_MAG_INT(q) \
    ((int16_t)(2.0f * ((float)(q) * FTX_MAG_PASO_DB + FTX_MAG_BASE_DB) + 240.0f))
extern const int16_t ftx_mag_int[16];
#define WF_MAG_INT_AT(wf, i) ((int)ftx_mag_int[WF_MAG_RAW(wf, i)])
/*
 * EL ESCALON CRUDO, Y CUANTOS HAY.
 *
 * Lo usa ft8_snr.c para su histograma de ruido. Lo hacia con
 * WF_MAG_INT_AT, que son unidades de medio dB, y eso ATA EL TAMAÑO DEL
 * HISTOGRAMA A LA ESCALERA: con base -35 y paso 3 el escalon 15 da 260 y
 * se sale de un histo[256]. Pasó de verdad, barriendo la escalera el
 * 28/10/2026: "stack smashing detected".
 *
 * Indexando por el escalon crudo no puede pasar: hay WF_MAG_NIVELES y
 * punto, los diga la escalera que los diga.
 */
#define WF_MAG_NIVEL(wf, i)  WF_MAG_RAW(wf, i)
#define WF_MAG_NIVEL_DB(x)   ((float)(x) * FTX_MAG_PASO_DB + FTX_MAG_BASE_DB)
#else
#define WF_MAG_INT_AT(wf, i) WF_ELEM_MAG_INT((wf)->mag[i])
#define WF_MAG_AT(wf, i)     WF_ELEM_MAG((wf)->mag[i])
#define WF_MAG_NIVELES       256
#define WF_MAG_NIVEL(wf, i)  ((int)((wf)->mag[i]))
#define WF_MAG_NIVEL_DB(x)   ((float)(x) * 0.5f - 120.0f)
#define WF_MAG_PON(base, i, q) (((uint8_t *)(base))[i] = (uint8_t)(q))
#endif

/// Input structure to ftx_find_sync() function. This structure describes stored waterfall data over the whole message slot.
/// Fields time_osr and freq_osr specify additional oversampling rate for time and frequency resolution.
/// If time_osr=1, FFT magnitude data is collected once for every symbol transmitted, i.e. every 1/6.25 = 0.16 seconds.
/// Values time_osr > 1 mean each symbol is further subdivided in time.
/// If freq_osr=1, each bin in the FFT magnitude data corresponds to 6.25 Hz, which is the tone spacing.
/// Values freq_osr > 1 mean the tone spacing is further subdivided by FFT analysis.
typedef struct
{
    int max_blocks;          ///< number of blocks (symbols) allocated in the mag array
    int num_blocks;          ///< number of blocks (symbols) stored in the mag array
    int num_bins;            ///< number of FFT bins in terms of 6.25 Hz
    int time_osr;            ///< number of time subdivisions
    int freq_osr;            ///< number of frequency subdivisions
    WF_ELEM_T* mag;          ///< FFT magnitudes stored as uint8_t[blocks][time_osr][freq_osr][num_bins]
    int block_stride;        ///< Helper value = time_osr * freq_osr * num_bins
    ftx_protocol_t protocol; ///< Indicate if using FT4 or FT8
} ftx_waterfall_t;

/// Output structure of ftx_find_sync() and input structure of ftx_decode().
/// Holds the position of potential start of a message in time and frequency.
typedef struct
{
    int16_t score;       ///< Candidate score (non-negative number; higher score means higher likelihood)
    int16_t time_offset; ///< Index of the time block
    int16_t freq_offset; ///< Index of the frequency bin
    uint8_t time_sub;    ///< Index of the time subdivision used
    uint8_t freq_sub;    ///< Index of the frequency subdivision used
} ftx_candidate_t;

/// Structure that contains the status of various steps during decoding of a message
typedef struct
{
    float freq;
    float time;
    int ldpc_errors;         ///< Number of LDPC errors during decoding
    uint16_t crc_extracted;  ///< CRC value recovered from the message
    uint16_t crc_calculated; ///< CRC value calculated over the payload
    // int unpack_status;       ///< Return value of the unpack routine
} ftx_decode_status_t;

/// Localize top N candidates in frequency and time according to their sync strength (looking at Costas symbols)
/// We treat and organize the candidate list as a min-heap (empty initially).
/// @param[in] power Waterfall data collected during message slot
/// @param[in] sync_pattern Synchronization pattern
/// @param[in] num_candidates Number of maximum candidates (size of heap array)
/// @param[in,out] heap Array of ftx_candidate_t type entries (with num_candidates allocated entries)
/// @param[in] min_score Minimal score allowed for pruning unlikely candidates (can be zero for no effect)
/// @return Number of candidates filled in the heap
int ftx_find_candidates(const ftx_waterfall_t* power, int num_candidates, ftx_candidate_t heap[], int min_score);

/* La misma, con un gancho que se llama cada pocos milisegundos para que
 * quien llama pueda atender lo suyo -en esta radio, vaciar el
 * remuestreador-. Modificacion de este proyecto: ver el comentario en el
 * .c. Con `respira` a cero hace exactamente lo de siempre. */
int ftx_find_candidates_respira(const ftx_waterfall_t* power, int num_candidates, ftx_candidate_t heap[], int min_score, void (*respira)(void));

/// Attempt to decode a message candidate. Extracts the bit probabilities, runs LDPC decoder, checks CRC and unpacks the message in plain text.
/// @param[in] power Waterfall data collected during message slot
/// @param[in] cand Candidate to decode
/// @param[in] max_iterations Maximum allowed LDPC iterations (lower number means faster decode, but less precise)
/// @param[out] message ftx_message_t structure that will receive the decoded message
/// @param[out] status ftx_decode_status_t structure that will be filled with the status of various decoding steps
/// @return True if the decoding was successful, false otherwise (check status for details)
bool ftx_decode_candidate(const ftx_waterfall_t* power, const ftx_candidate_t* cand, int max_iterations, ftx_message_t* message, ftx_decode_status_t* status);

#ifdef __cplusplus
}
#endif

#endif // _INCLUDE_DECODE_H_

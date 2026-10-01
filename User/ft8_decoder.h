#ifndef FT8_DECODER_H
#define FT8_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* size_t, para ft8_decoder_grid_coord_txt() */

/*
 * Orchestrates a real decode pass over a captured slot: calls
 * ft8_lib's ftx_find_candidates()/ftx_decode_candidate()/
 * ftx_message_decode() directly (the pieces the host WAV test
 * (test/host/ft8_host_test.c) already exercised against real off-air
 * recordings), and queues the results as ready-to-display text lines.
 *
 * Deliberately NOT slot-timing aware (same scope limit as
 * ft8_waterfall_adapter.h/ft8_decimator.h) - call
 * ft8_decoder_process_slot() whenever ft8_waterfall_is_full() says a
 * capture is ready; arming/rearming the next capture is the caller's
 * job (see main.c's minimal bring-up hook).
 */

#define FT8_DECODER_MAX_CANDIDATES 140 /* matches ft8_lib's own demo default (kMax_candidates) */
#define FT8_DECODER_MIN_SCORE      10  /* matches ft8_lib's own demo default (kMin_score) */
#define FT8_DECODER_LDPC_ITERATIONS 25 /* matches ft8_lib's own demo default (kLDPC_iterations) */
#define FT8_DECODER_MSG_QUEUE_SIZE 20  /* generous for our 400Hz search window - a busy full-band capture might see more, ours won't */
#define FT8_DECODER_LINE_LEN       88   /* + la columna de pais, 25/09/2026 */  /* "HH:MM " (6) + "0169Hz -05dB ~ " (15) + message (up to 35) + " 1234km" (7) + NUL, rounded up (09/2026 #2: was 56, before the distance-to-grid suffix) */

/* One decoded message, already formatted as a display-ready line
 * ("freq snr ~ message") - see ft8_decoder_process_slot()'s comment
 * for the exact format. */
typedef struct
{
    char line[FT8_DECODER_LINE_LEN];
} ft8_decoded_msg_t;

/* Call once, at startup (alongside ft8_decimator_reset()/
 * ft8_waterfall_reset()) - clears the message queue. */
void ft8_decoder_init(void);

/* Sets/reads the QTH grid used for the distance-to-grid field on
 * decoded CQ lines Y para el aspa de "donde estamos" en los mapas de
 * FT8, WSPR, AIS y JTTY - see their own comments in ft8_decoder.c.
 * ft8_decoder_init() above seeds this from the compiled-in
 * FT8_GRID_POR_DEFECTO default, que a dia de hoy es el UNICO sitio de donde
 * sale.
 *
 * HISTORIA, porque explica por que este fichero esta escrito asi.
 *
 * Hasta el 29/09/2026 la cabecera afirmaba que settings.c leia una clave
 * "grid" de CONFIG.CSV y que build_csv() la guardaba. NO EXISTIA NADA DE
 * ESO: ni la clave, ni la llamada, ni el guardado. El unico sitio que
 * ponia el localizador era ft8_decoder_init(), con el valor compilado, y
 * el aspa del mapa llevaba meses clavada en Canarias.
 *
 * El fallo no fue el codigo: fue el comentario. Describia una
 * funcionalidad que nadie construyo, y al leerlo se daba por hecho que el
 * localizador se configuraba y ya nadie lo comprobaba.
 *
 * HOY YA NO ES ASI, y conviene decirlo aqui mismo porque el aviso que
 * habia en este hueco se quedo describiendo el mundo de aquella tarde y
 * paso a ser el la mentira: desde el 30/09 settings.c SI tiene la clave
 * "grid" (settings.c, en settings_load()), SI llama a esta funcion y
 * build_csv() SI la guarda, y la pantalla de Ajustes -> Equipo -> QTH es
 * quien la escribe. Lo vigila tools/qth_check.py. */
void ft8_decoder_set_own_grid(const char *grid, int len);
const char *ft8_decoder_get_own_grid(void);


/*
 * UN LOCALIZADOR A COORDENADAS, en DIEZMILESIMAS DE GRADO (40,6458 sale
 * como 406458; al sur y al oeste, negativo). Devuelve false si el
 * localizador no vale, y entonces no toca las salidas.
 *
 * POR QUE ES PUBLICA - 30/09/2026. *** El dueño, con la foto de la
 * pantalla del QTH delante: "creo que las coordenadas no estan bien". ***
 * Y no lo estaban: la pantalla del QTH de main.c se habia escrito SU
 * PROPIA conversion, en enteros, y la subcuadricula iba multiplicada por
 * diez (8333 donde tocaba 833). "IN80fp" -Madrid, 40,6458 N 3,5417 W-
 * salia como 46,4573 N 0,5831 E, o sea en Francia.
 *
 * Lo grave no es la cifra, es que hubiera dos conversiones. La de aqui
 * -la que mueve el aspa de los mapas y mide las distancias de los CQ-
 * estaba bien, asi que el fallo no se veia por ningun lado salvo en esa
 * pantalla, que es justo la que existe para DARTE CUENTA de que te has
 * equivocado de letra. Ahora hay una sola, esta, y main.c solo formatea.
 */
/*
 * Pone el localizador POR DEFECTO (IN80dk, Madrid capital). Se llama UNA vez
 * en el arranque, antes de settings_load(), y desde ningun otro sitio: es
 * para que una radio recien flasheada tenga algo valido, no para reponer
 * nada. ft8_decoder_init() NO lo hace, a proposito - ver su comentario.
 */
void ft8_decoder_grid_por_defecto(void);

bool ft8_decoder_grid_e4(const char *grid, int len,
                         int32_t *lat_e4, int32_t *lon_e4);

/*
 * Y EL RENGLON YA HECHO: "40.6458, -3.5417". Devuelve false -y deja `out`
 * vacio- si el localizador no vale o si no cabe.
 *
 * El FORMATO es parte de la respuesta y por eso vive aqui y no en quien
 * pinta: punto decimal, coma como separador y menos para sur y oeste. *** Por
 * el dueño, 30/09/2026: "te falta un negativo". *** Este renglon existe para
 * pegarlo en un mapa y comprobar que el localizador teclado es el tuyo, y con
 * coma decimal eso no se puede - un mapa lee la coma como el separador entre
 * las dos cifras-. Lo mide sim/qthcoord.c, cadena por cadena.
 */
bool ft8_decoder_grid_coord_txt(const char *grid, int len,
                                char *out, size_t n);

/* Runs ftx_find_candidates() + ftx_decode_candidate() +
 * ftx_message_decode() over whatever ft8_waterfall_get() currently
 * holds, and pushes every successfully decoded, non-duplicate message
 * into the output queue. Call this ONCE right after
 * ft8_waterfall_is_full() becomes true (NOT from an ISR - this walks
 * up to FT8_DECODER_MAX_CANDIDATES candidates, each running a
 * bp_decode() LDPC pass costing ~4.4KB of stack momentarily - see this
 * session's stack_watermark.h measurement for why that's fine from
 * the main loop but would NOT be fine from inside an ISR). Duplicate
 * suppression is a simple exact-match-within-this-slot check, not
 * ft8_lib's own hash table (this deliberately skips the
 * ftx_callsign_hash_interface_t plumbing for now - compound/hashed
 * callsigns may decode less completely than a full WSJT-X-style
 * multi-pass would give you, but ordinary messages - the large
 * majority in practice - decode in full without it, same tradeoff
 * already accepted in test/host/ft8_host_test.c's own hash stub). */
void ft8_decoder_process_slot(void);

/*
 * LA MISMA DECODIFICACION, PERO A TROZOS - 25/09/2026.
 *
 * ft8_decoder_process_slot() bloquea el bucle principal mientras dura, y
 * mientras dura nadie vacia el remuestreador, que solo tiene 320 ms de
 * colchon. Con la ventana en 1.600 Hz eso costaba SEGUNDOS de audio de la
 * ranura siguiente, que entonces no llegaba a sus 93 bloques y no
 * decodificaba: se veia como "salen cinco lineas y luego nada en varias
 * tandas".
 *
 * Asi que la radio usa estas dos: empezar una vez, y luego un candidato por
 * vuelta del bucle principal, vaciando el remuestreador entre medias.
 *
 *     ft8_decoder_slot_empieza();
 *     while (ft8_decoder_slot_paso()) { ft8_decimator_poll(); }
 *
 * El trabajo total es el mismo. Lo que cambia es que ya no se hace seguido.
 */
void    ft8_decoder_slot_empieza(void);
uint8_t ft8_decoder_slot_paso(void);

/* Pulls one queued decoded-message line. Returns true and fills *out
 * if one was pending, false (leaves *out untouched) if the queue is
 * empty. Call from the main loop to drain into the UI - same
 * "background sets state, main loop drains/acts on it" split as every
 * other cross-context field in this codebase (e.g. rtty_get_char()). */
bool ft8_decoder_get_message(ft8_decoded_msg_t *out);

/* Running total of real decoded messages since ft8_decoder_init()
 * (i.e. since boot) - counts every successful, non-duplicate decode
 * even if the display queue was full and that particular message got
 * dropped before ever reaching ft8_decoder_get_message(). Added
 * (09/2026) per the project owner's request: a persistent counter,
 * shown continuously in the UI, to help tell at a glance how
 * productive a given band/time/tuning is while experimenting, since
 * the scrolling text panel alone doesn't make totals easy to track. */
uint32_t ft8_decoder_get_total_count(void);

/*
 * EL DT MEDIO DE LA ULTIMA RANURA, en milisegundos, y de cuantos mensajes
 * sale.
 *
 * El DT de UNA estacion no dice nada: cada operador tiene su reloj y el suyo
 * puede estar corrido. Lo que habla es que TODAS coincidan - quince
 * estaciones de nueve paises no se ponen de acuerdo para ir todas 0,8 s
 * tarde. Ese sesgo comun es nuestro, y es lo que ft8_modo corrige solo.
 */
int ft8_decoder_dt_medio_ms(void);
int ft8_decoder_dt_cuantos(void);

/* Candidate count from the last ftx_find_candidates() call - see its
 * own declaration comment in ft8_decoder.c for why. */
int ft8_decoder_get_last_num_candidates(void);

#endif /* FT8_DECODER_H */

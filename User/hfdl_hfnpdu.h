#ifndef HFDL_HFNPDU_H
#define HFDL_HFNPDU_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Minimal decoder for the HFDL HFNPDU "Performance data" message
 * (type 0xD1) - the position-report format carried inside a
 * HFDL_LPDU_KIND_USER_DATA LPDU (see hfdl_pdu.h). Ported 16/09/2026
 * from the real, open-source reference implementation (dumphfdl's
 * hfnpdu.c performance_data_parse()/parse_coordinate(), itself using
 * libacars) - field offsets and the coordinate formula are copied
 * exactly from that source, not reverse-engineered from scratch.
 * Verified byte-for-byte against a real decoded HFDL message from
 * this project's own hardware (src_id=250/dst_gs_id=4, 11387kHz New
 * York/Riverhead capture): bytes 10-11 of the LPDU user data were
 * 0xFF 0xD1 (the envelope marker + this type), and the latitude
 * field computed to 42.117 deg, matching dumphfdl's own 42.1120493
 * for the identical message (small residual difference is this
 * comment's hand-calculation rounding, not a formula difference).
 *
 * Deliberately NOT a full HFNPDU decoder: only PERFORMANCE_DATA
 * (0xD1) is handled here. SYSTEM_TABLE (0xD0), SYSTEM_TABLE_REQUEST
 * (0xD2), FREQUENCY_DATA (0xD5), DELAYED_ECHO (0xDE) and
 * ENVELOPED_DATA (0xFF, which is actually ACARS - needs a real ACARS
 * decoder, out of scope here) are all left unhandled - see
 * hfdl_hfnpdu_parse_performance_data()'s own comment on how to tell
 * them apart if extending this later.
 *
 * Only the fields useful for a position report are extracted
 * (flight ID, position, UTC time, flight leg, ground station ID) -
 * the MPDU/SPDU traffic-statistics fields further into the same PDU
 * (mpdus_rx, spdus_rx, etc. in dumphfdl's struct) are skipped, not
 * because they're hard, just not useful for "where is the aircraft".
 */

struct hfdl_hfnpdu_location {
    double lat; /* degrees, positive = North */
    double lon; /* degrees, positive = East */
};

struct hfdl_hfnpdu_time {
    uint8_t hour, min, sec; /* UTC */
};

typedef struct {
    char flight_id[7];               /* 6 chars + NUL - often all-zero/blank in practice, same as real captures */
    struct hfdl_hfnpdu_location location;
    struct hfdl_hfnpdu_time utc_time;
    uint8_t version;
    uint8_t flight_leg;
    uint8_t gs_id;                    /* ground station the aircraft is currently working */
} hfdl_hfnpdu_perf_data_t;

/*
 * `buf`/`len` are the LPDU's user-data bytes exactly as reported by
 * hfdl_mpdu_extract_lpdus() (hfdl_lpdu_result_t doesn't hand back a
 * pointer/length pair directly - the caller already has `decoded` +
 * the LPDU's byte range from the same buffer it fed to
 * hfdl_mpdu_extract_lpdus(); see demod_am.c's call site for exactly
 * where this offset comes from).
 *
 * ESTE BLOQUE DECIA OTRA COSA HASTA EL 10/10/2026, y lo que decia dejo
 * de ser verdad ESA MISMA MAÑANA, cuando el .c paso de exigir el octeto
 * de tipo en un sitio fijo a buscarlo. Se corrige entero en vez de
 * retocarlo, porque las tres condiciones que listaba eran falsas y la
 * cuarta -la peligrosa- tambien.
 *
 * LO QUE HACE DE VERDAD (ver hfdl_hfnpdu.c):
 *
 *   - BUSCA el octeto de tipo 0xD1 en los primeros 24 bytes, no lo exige
 *     en el 0 ni en el 1. En la unica trama real verificada estaba en el
 *     11, detras de un sobre 0xFF en el 10.
 *   - Fuera del byte 0, exige que delante vaya el sobre 0xFF.
 *   - La comprobacion de longitud es RELATIVA al sitio donde lo
 *     encuentra, no "len < 47".
 *   - Valida latitud, longitud, hora y estacion de tierra, y se queda
 *     con el primer candidato que pasa las cuatro.
 *
 * *** Y LO QUE HAY QUE SABER PARA USARLA BIEN: ***
 *
 *   `*out` SE ESCRIBE ENTERO ANTES DE VALIDAR, asi que al devolver false
 *   NO se queda como estaba: lleva dentro los campos del ultimo
 *   candidato probado, que son bytes leidos donde no era.
 *
 * Quien use *out sin mirar el retorno pinta un avion en una posicion
 * inventada. Y una posicion inventada se cree, porque es un punto en un
 * mapa y no dice de donde salio. MIRA EL RETORNO SIEMPRE.
 */
bool hfdl_hfnpdu_parse_performance_data(const uint8_t *buf, uint32_t len, hfdl_hfnpdu_perf_data_t *out);

/*
 * EL OCTETO DE TIPO DEL HFNPDU, Y DONDE EMPIEZA SU CONTENIDO.
 * 14/10/2026, con bytes de verdad por fin.
 *
 * El volcado en pantalla de la radio dio dos tramas del mismo
 * minuto en 17.967 kHz:
 *
 *   FF D1 51 54 52 38 37 51 ...   -> sobre, tipo 0xD1, "QTR87Q"
 *   FF FF 01 32 AE C1 B6 AD ...   -> sobre, tipo 0xFF, y detras un ACARS
 *
 * O sea: el sobre 0xFF va en el byte 0 y EL TIPO EN EL 1. La cabecera de
 * este fichero decia, de una trama que nadie guardo, "bytes 10-11 of the
 * LPDU user data were 0xFF 0xD1", y de ahi salio una busqueda del tipo por
 * los primeros 24 bytes. Era un recuerdo mal apuntado.
 *
 * Devuelve el tipo (0xD1 datos de rendimiento, 0xFF ACARS envuelto, 0xD0
 * tabla del sistema...) y, en `contenido`, el indice del primer byte de
 * detras. 0 si no hay de donde sacarlo.
 */
uint8_t hfdl_hfnpdu_tipo(const uint8_t *buf, uint32_t len, uint32_t *contenido);

#endif /* HFDL_HFNPDU_H */

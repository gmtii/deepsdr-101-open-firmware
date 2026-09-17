#ifndef HFDL_PDU_H
#define HFDL_PDU_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HFDL MPDU/LPDU header parsing - step 2 of Phase 2's staged plan
 * (see hfdl_crc.h for step 1 and the project's HFDL_fase2_arquitectura
 * document for the full plan). Like hfdl_crc.c, this module has ZERO
 * dependency on the rest of the Phase 2 pipeline (no symbol sync, no
 * Viterbi, no framer) - it operates on an already-decoded octet
 * buffer (post-Viterbi, post-REVERSE_BYTE - see note below) and is
 * built/tested standalone against synthetic buffers first.
 *
 * Byte layout confirmed directly against dumphfdl's src/mpdu.c and
 * src/lpdu.c (szpajder/dumphfdl, GPL-3.0-or-later) - see that
 * project's mpdu_parse()/parse_lpdu_list()/lpdu_parse() for the
 * reference implementation this was checked against.
 *
 * SCOPE, per the project owner's explicit decision: full parsing of
 * the 5 "small" LPDU types (logon request/confirm/resume, logoff,
 * logon denied - all of which carry a plain ICAO address and are
 * cheap to decode). UNNUMBERED_DATA / UNNUMBERED_ACKED_DATA (the
 * types that carry real ACARS/HFNPDU payload) are detected but NOT
 * decoded - that would pull in dumphfdl's hfnpdu.c/acars.c and a
 * libacars-equivalent, explicitly deferred out of this phase.
 *
 * IMPORTANT bit-order note: dumphfdl bit-reverses the ENTIRE
 * post-Viterbi PDU buffer once (REVERSE_BYTE on every octet, see
 * hfdl.c's decode_user_data()) before mpdu_parse()/lpdu_parse() ever
 * see it - so by the time code in THIS module runs, the buffer is
 * expected to already be in that same corrected, MSB-first byte
 * order. Callers (the eventual hfdl_demod.c, in a later phase) are
 * responsible for applying that same per-byte reversal to the raw
 * Viterbi chainback output before calling into this module - it is
 * NOT done here, since this module has no Viterbi/DSP dependency by
 * design.
 *
 * A SEPARATE, second bit-reversal is applied specifically to the 3
 * raw ICAO address bytes inside parse_icao_hex() in dumphfdl's
 * util.c - i.e. those 3 bytes get reversed AGAIN on top of the
 * buffer-wide reversal above. This looked like a possible mistake at
 * first glance, but it's deliberate and confirmed straight from
 * dumphfdl's own source, not a guess - see hfdl_icao_parse() below,
 * which replicates it exactly for the same reason dumphfdl does.
 */

typedef enum {
	HFDL_PDU_DIR_DOWNLINK = 0,  /* aircraft -> ground */
	HFDL_PDU_DIR_UPLINK   = 1,  /* ground -> aircraft */
} hfdl_pdu_direction_t;

/* Exact bit-width-derived maximum, NOT an estimate: aircraft_cnt in
 * an uplink MPDU header is a 3-bit field (((byte0 & 0x70) >> 4) + 1),
 * so 8 is the true maximum, guaranteed by the wire format. */
#define HFDL_MAX_AIRCRAFT_PER_UPLINK 8

/* Exact bit-width-derived maximum, NOT an estimate: lpdu_cnt (per
 * MPDU for downlink, per aircraft segment for uplink) is a 4-bit
 * nibble, so 15 is the true maximum per segment. */
#define HFDL_MAX_LPDU_CNT_PER_SEGMENT 15

/* Practical engineering safety cap on the TOTAL number of LPDUs this
 * module will extract from one MPDU across all segments - unlike the
 * two constants above, this is NOT derived from the wire format (the
 * theoretical worst case, 8 aircraft x 15 LPDUs each, is 120 and
 * would need a much larger fixed buffer than any real HFDL frame's
 * byte budget could actually fill). 32 is a deliberately generous
 * margin over what a real frame's length limits make possible, kept
 * fixed-size to avoid dynamic allocation - if hfdl_mpdu_extract_lpdus()
 * hits this cap, it stops and returns what it found rather than
 * overflowing the caller's array.
 */
#define HFDL_MAX_LPDUS_TOTAL 32

typedef struct {
	bool crc_ok;
	hfdl_pdu_direction_t direction;
	uint32_t hdr_len;      /* header length covered by the MPDU-level FCS,
	                         * NOT counting the 2 FCS bytes themselves */

	/* --- valid when direction == HFDL_PDU_DIR_DOWNLINK --- */
	uint8_t src_id;        /* source aircraft ID */
	uint8_t dst_id;        /* destination ground station ID */

	/* --- valid when direction == HFDL_PDU_DIR_UPLINK --- */
	uint8_t src_gs_id;     /* source ground station ID */
	uint8_t aircraft_cnt;  /* number of entries valid in aircraft[] below */
	struct {
		uint8_t dst_id;    /* destination aircraft ID */
		uint8_t lpdu_cnt;  /* number of LPDUs addressed to this aircraft */
	} aircraft[HFDL_MAX_AIRCRAFT_PER_UPLINK];
} hfdl_mpdu_header_t;

typedef enum {
	HFDL_LPDU_KIND_INVALID = 0,     /* bounds/CRC error - other fields undefined */
	HFDL_LPDU_KIND_LOGON_REQUEST,   /* LOGON_REQUEST_NORMAL, LOGON_REQUEST_DLS, LOGON_RESUME */
	HFDL_LPDU_KIND_LOGON_CONFIRM,   /* LOGON_CONFIRM, LOGON_RESUME_CONFIRM */
	HFDL_LPDU_KIND_LOGOFF_OR_DENIED,/* LOGOFF_REQUEST, LOGON_DENIED */
	HFDL_LPDU_KIND_USER_DATA,       /* UNNUMBERED_DATA, UNNUMBERED_ACKED_DATA - NOT decoded */
	HFDL_LPDU_KIND_UNKNOWN_TYPE,    /* any type octet not in the 9 known values */
} hfdl_lpdu_kind_t;

typedef struct {
	hfdl_lpdu_kind_t kind;
	bool crc_ok;
	uint8_t type_octet;      /* raw LPDU type byte, valid whenever kind != INVALID */

	/* --- valid when kind == LOGON_REQUEST, LOGON_CONFIRM or LOGOFF_OR_DENIED --- */
	uint32_t icao_address;   /* 24-bit ICAO address, in the low 24 bits */

	/* --- valid when kind == LOGON_CONFIRM only --- */
	uint8_t ac_id;

	/* --- valid when kind == LOGOFF_OR_DENIED only --- */
	uint8_t reason_code;

	/* --- valid when kind == USER_DATA only --- */
	uint32_t user_data_len;  /* bytes of undecoded HFNPDU/ACARS payload following the type octet */
} hfdl_lpdu_result_t;

/* Parses just the MPDU header (direction, src/dst IDs, per-aircraft
 * LPDU counts for uplink) and checks its FCS. `buf`/`len` are the
 * full MPDU buffer (bit-order already corrected - see module header
 * comment above). Returns false on a structurally too-short buffer
 * (before even reaching the FCS check) - a bad FCS is NOT a false
 * return, it's reported via hdr_out->crc_ok so the caller can still
 * inspect header fields if it wants (mirrors dumphfdl's own
 * Config.output_corrupted_pdus behaviour). */
bool hfdl_mpdu_parse_header(const uint8_t *buf, uint32_t len, hfdl_mpdu_header_t *hdr_out);

/* Walks every LPDU referenced by an already-parsed MPDU header (both
 * downlink's single flat list and uplink's per-aircraft segments) and
 * decodes each one into `results_out[0 .. return value - 1]`.
 * `results_cap` is the capacity of `results_out` - stops early
 * (without overflowing) if reached, see HFDL_MAX_LPDUS_TOTAL above
 * for the module's own recommended cap. Does nothing and returns 0 if
 * `hdr->crc_ok` is false, since LPDU offsets can't be trusted once
 * the header itself failed its own check. */
uint32_t hfdl_mpdu_extract_lpdus(const uint8_t *buf, uint32_t len,
		const hfdl_mpdu_header_t *hdr,
		hfdl_lpdu_result_t *results_out, uint32_t results_cap);

/* Runs the module's standalone self-test against synthetic MPDU/LPDU
 * buffers (hand-built, with correct FCS bytes appended via
 * hfdl_crc_append() from hfdl_crc.c) - covers a downlink MPDU with a
 * mix of LPDU kinds, an uplink MPDU with 2 aircraft, and a few
 * malformed-input edge cases. Returns true if every case matches its
 * expected outcome. Depends on hfdl_crc.c (already self-tested
 * separately) but nothing else. */
bool hfdl_pdu_selftest(void);

#endif /* HFDL_PDU_H */

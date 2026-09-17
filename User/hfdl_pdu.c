#include <stddef.h>
#include <string.h>
#include "hfdl_pdu.h"
#include "hfdl_crc.h"

/* Same bit-reversal trick dumphfdl's util.h REVERSE_BYTE macro uses -
 * see hfdl_pdu.h's module header comment for WHY this gets applied a
 * second time on top of the buffer-wide reversal the caller already
 * did. Kept as a local static helper (not exposed) since it's an
 * internal implementation detail of ICAO field parsing only. */
static uint8_t reverse_byte(uint8_t x)
{
	x = (uint8_t)((x & 0xF0u) >> 4 | (x & 0x0Fu) << 4);
	x = (uint8_t)((x & 0xCCu) >> 2 | (x & 0x33u) << 2);
	x = (uint8_t)((x & 0xAAu) >> 1 | (x & 0x55u) << 1);
	return x;
}

/* Mirrors dumphfdl's util.c parse_icao_hex() exactly: reverses each
 * of the 3 input bytes, then packs them big-endian into the low 24
 * bits of the result. */
static uint32_t hfdl_icao_parse(const uint8_t *buf3)
{
	uint32_t result = 0u;
	for (int32_t i = 0; i < 3; i++) {
		result |= (uint32_t)reverse_byte(buf3[i]) << (8u * (2u - (uint32_t)i));
	}
	return result;
}

/* LPDU type octets - see dumphfdl's src/lpdu.c for the reference list. */
#define LPDU_TYPE_UNNUMBERED_DATA        0x0Du
#define LPDU_TYPE_UNNUMBERED_ACKED_DATA  0x1Du
#define LPDU_TYPE_LOGON_DENIED           0x2Fu
#define LPDU_TYPE_LOGOFF_REQUEST         0x3Fu
#define LPDU_TYPE_LOGON_RESUME           0x4Fu
#define LPDU_TYPE_LOGON_RESUME_CONFIRM   0x5Fu
#define LPDU_TYPE_LOGON_REQUEST_NORMAL   0x8Fu
#define LPDU_TYPE_LOGON_CONFIRM          0x9Fu
#define LPDU_TYPE_LOGON_REQUEST_DLS      0xBFu

/* Minimum LPDU PAYLOAD lengths (i.e. NOT counting the 2 trailing FCS
 * bytes), matching dumphfdl's *_LPDU_LEN constants exactly. */
#define LOGON_REQUEST_LPDU_LEN  4u  /* type + 3-byte ICAO */
#define LOGOFF_REQUEST_LPDU_LEN 5u  /* type + 3-byte ICAO + reason code */
#define LOGON_CONFIRM_LPDU_LEN  8u  /* type + 3-byte ICAO + ac_id + 3 reserved bytes */

bool hfdl_mpdu_parse_header(const uint8_t *buf, uint32_t len, hfdl_mpdu_header_t *hdr_out)
{
	if (buf == NULL || hdr_out == NULL || len < 2u) {
		return false;
	}

	hfdl_mpdu_header_t hdr = {0};
	uint32_t hdr_len;

	if (buf[0] & 0x02u) {
		/* Downlink: aircraft -> ground */
		hdr.direction = HFDL_PDU_DIR_DOWNLINK;
		uint32_t lpdu_cnt = (uint32_t)(buf[0] >> 2) & 0x0Fu;
		hdr_len = 6u + lpdu_cnt;

		if (len < hdr_len + 2u) {
			return false; /* too short to even contain header + FCS */
		}

		hdr.dst_id = buf[1] & 0x7Fu;
		hdr.src_id = buf[2];
	} else {
		/* Uplink: ground -> aircraft, possibly multiple destination aircraft */
		hdr.direction = HFDL_PDU_DIR_UPLINK;
		uint32_t aircraft_cnt = (uint32_t)((buf[0] & 0x70u) >> 4) + 1u;
		hdr_len = 2u; /* P/NAC/T + UTC/GS ID */

		if (len < hdr_len + 1u) {
			return false;
		}
		hdr.src_gs_id = buf[1] & 0x7Fu;

		if (aircraft_cnt > HFDL_MAX_AIRCRAFT_PER_UPLINK) {
			/* Cannot happen per the wire format's 3-bit field, but
			 * guard anyway rather than trust a possibly-corrupt buffer. */
			return false;
		}

		for (uint32_t i = 0; i < aircraft_cnt; i++) {
			if (len < hdr_len + 2u) {
				return false; /* too short for this aircraft's 2-byte sub-header */
			}
			uint8_t dst_id   = buf[hdr_len];
			uint8_t lpdu_cnt = (buf[hdr_len + 1u] >> 4) & 0x0Fu;
			hdr.aircraft[i].dst_id   = dst_id;
			hdr.aircraft[i].lpdu_cnt = lpdu_cnt;
			hdr_len += 2u + lpdu_cnt;
		}
		hdr.aircraft_cnt = (uint8_t)aircraft_cnt;

		if (len < hdr_len + 2u) {
			return false; /* too short for header + FCS once full size is known */
		}
	}

	hdr.hdr_len = hdr_len;
	hdr.crc_ok = hfdl_crc_check(buf, hdr_len);
	*hdr_out = hdr;
	return true;
}

/* Decodes a single LPDU's payload (buf/payload_len already excludes
 * the trailing 2 FCS bytes - the caller has already run the CRC check
 * separately and passes its result in via `crc_ok`). Mirrors
 * dumphfdl's lpdu_parse()'s type switch. */
static hfdl_lpdu_result_t lpdu_parse_one(const uint8_t *buf, uint32_t payload_len, bool crc_ok)
{
	hfdl_lpdu_result_t r = {0};

	if (payload_len < 1u) {
		r.kind = HFDL_LPDU_KIND_INVALID;
		return r;
	}

	r.crc_ok = crc_ok;
	r.type_octet = buf[0];

	if (!crc_ok) {
		r.kind = HFDL_LPDU_KIND_INVALID;
		return r;
	}

	switch (r.type_octet) {
	case LPDU_TYPE_UNNUMBERED_DATA:
	case LPDU_TYPE_UNNUMBERED_ACKED_DATA:
		r.kind = HFDL_LPDU_KIND_USER_DATA;
		r.user_data_len = payload_len - 1u; /* everything after the type octet, undecoded */
		break;

	case LPDU_TYPE_LOGON_DENIED:
	case LPDU_TYPE_LOGOFF_REQUEST:
		if (payload_len < LOGOFF_REQUEST_LPDU_LEN) {
			r.kind = HFDL_LPDU_KIND_INVALID;
			break;
		}
		r.kind = HFDL_LPDU_KIND_LOGOFF_OR_DENIED;
		r.icao_address = hfdl_icao_parse(buf + 1);
		r.reason_code = buf[4];
		break;

	case LPDU_TYPE_LOGON_CONFIRM:
	case LPDU_TYPE_LOGON_RESUME_CONFIRM:
		if (payload_len < LOGON_CONFIRM_LPDU_LEN) {
			r.kind = HFDL_LPDU_KIND_INVALID;
			break;
		}
		r.kind = HFDL_LPDU_KIND_LOGON_CONFIRM;
		r.icao_address = hfdl_icao_parse(buf + 1);
		r.ac_id = buf[4];
		break;

	case LPDU_TYPE_LOGON_RESUME:
	case LPDU_TYPE_LOGON_REQUEST_NORMAL:
	case LPDU_TYPE_LOGON_REQUEST_DLS:
		if (payload_len < LOGON_REQUEST_LPDU_LEN) {
			r.kind = HFDL_LPDU_KIND_INVALID;
			break;
		}
		r.kind = HFDL_LPDU_KIND_LOGON_REQUEST;
		r.icao_address = hfdl_icao_parse(buf + 1);
		break;

	default:
		r.kind = HFDL_LPDU_KIND_UNKNOWN_TYPE;
		break;
	}

	return r;
}

/* Extracts and decodes every LPDU in `[lens_ptr, lens_ptr+lpdu_cnt)`
 * (the size-octet array) starting at `data_ptr`, matching dumphfdl's
 * parse_lpdu_list(). Returns the number of octets consumed from
 * data_ptr on success, or -1 on a truncated/malformed LPDU list.
 * Writes into `*results_out`/`*results_used` (which the caller
 * pre-initializes and shares across all segments so the
 * HFDL_MAX_LPDUS_TOTAL cap applies globally, not per-segment). */
static int32_t extract_lpdu_list(const uint8_t *lens_ptr, const uint8_t *data_ptr,
		const uint8_t *endptr, uint32_t lpdu_cnt,
		hfdl_lpdu_result_t *results_out, uint32_t results_cap, uint32_t *results_used)
{
	int32_t consumed = 0;

	for (uint32_t j = 0; j < lpdu_cnt; j++) {
		uint32_t lpdu_len = (uint32_t)lens_ptr[j] + 1u; /* size octet is length-1 */
		if (data_ptr + lpdu_len > endptr) {
			return -1; /* truncated - can't trust anything past this point */
		}
		if (lpdu_len < 3u) {
			/* Need at least type + 2 FCS bytes, same floor dumphfdl uses */
			data_ptr += lpdu_len;
			consumed += (int32_t)lpdu_len;
			continue;
		}

		uint32_t payload_len = lpdu_len - 2u; /* strip trailing FCS */
		bool crc_ok = hfdl_crc_check(data_ptr, payload_len);

		if (*results_used < results_cap) {
			results_out[*results_used] = lpdu_parse_one(data_ptr, payload_len, crc_ok);
			(*results_used)++;
		}
		/* If the cap is already hit, we still walk past this LPDU
		 * (below) so remaining offsets stay correct - we just stop
		 * recording new results. */

		data_ptr += lpdu_len;
		consumed += (int32_t)lpdu_len;

		if (*results_used >= HFDL_MAX_LPDUS_TOTAL) {
			break; /* module-wide safety cap reached, stop entirely */
		}
	}

	return consumed;
}

uint32_t hfdl_mpdu_extract_lpdus(const uint8_t *buf, uint32_t len,
		const hfdl_mpdu_header_t *hdr,
		hfdl_lpdu_result_t *results_out, uint32_t results_cap)
{
	if (buf == NULL || hdr == NULL || results_out == NULL || results_cap == 0u) {
		return 0u;
	}
	if (!hdr->crc_ok) {
		return 0u; /* header offsets aren't trustworthy once its own FCS failed */
	}

	uint32_t results_used = 0u;
	const uint8_t *endptr = buf + len;

	if (hdr->direction == HFDL_PDU_DIR_DOWNLINK) {
		uint32_t lpdu_cnt = hdr->hdr_len - 6u; /* hdr_len = 6 + lpdu_cnt, see parse_header above */
		const uint8_t *lens_ptr = buf + 6u;
		const uint8_t *data_ptr = buf + hdr->hdr_len + 2u; /* past header + FCS */
		extract_lpdu_list(lens_ptr, data_ptr, endptr, lpdu_cnt,
				results_out, results_cap, &results_used);
	} else {
		const uint8_t *hdrptr = buf + 2u;   /* first aircraft's 2-byte sub-header */
		const uint8_t *dataptr = buf + hdr->hdr_len + 2u; /* past full header + FCS */
		for (uint32_t i = 0; i < hdr->aircraft_cnt && results_used < HFDL_MAX_LPDUS_TOTAL; i++) {
			const uint8_t *lens_ptr = hdrptr + 2u; /* size octets follow this aircraft's 2-byte sub-header */
			int32_t consumed = extract_lpdu_list(lens_ptr, dataptr, endptr,
					hdr->aircraft[i].lpdu_cnt, results_out, results_cap, &results_used);
			if (consumed < 0) {
				break; /* truncated - stop, keep whatever was already decoded */
			}
			hdrptr += 2u + hdr->aircraft[i].lpdu_cnt;
			dataptr += (uint32_t)consumed;
		}
	}

	return results_used;
}

/* ---- self-test ---- */

bool hfdl_pdu_selftest(void)
{
	/* --- Case 1: downlink MPDU, 2 LPDUs: a LOGON_REQUEST_NORMAL and
	 * an UNNUMBERED_DATA carrying 3 bytes of (undecoded) payload.
	 * Built LPDU-first, since the header's own FCS covers the 2 size
	 * octets - their real values must be known BEFORE that FCS is
	 * computed (unlike a real transmitter, which knows the whole
	 * frame upfront, this test has to construct bottom-up). --- */
	{
		uint8_t lpdu1[8];
		uint32_t lpdu1_len = 0;
		lpdu1[lpdu1_len++] = (uint8_t)LPDU_TYPE_LOGON_REQUEST_NORMAL;
		/* raw ICAO bytes as they'd appear on the wire - picked so their
		 * reverse_byte() is easy to predict: 0x01->0x80, 0x02->0x40, 0x04->0x20 */
		lpdu1[lpdu1_len++] = 0x01u;
		lpdu1[lpdu1_len++] = 0x02u;
		lpdu1[lpdu1_len++] = 0x04u;
		hfdl_crc_append(lpdu1, lpdu1_len);
		lpdu1_len += 2u;

		uint8_t lpdu2[8];
		uint32_t lpdu2_len = 0;
		lpdu2[lpdu2_len++] = (uint8_t)LPDU_TYPE_UNNUMBERED_DATA;
		lpdu2[lpdu2_len++] = 0xAAu;
		lpdu2[lpdu2_len++] = 0xBBu;
		lpdu2[lpdu2_len++] = 0xCCu;
		hfdl_crc_append(lpdu2, lpdu2_len);
		lpdu2_len += 2u;

		uint8_t buf[64];
		uint32_t pos = 0;

		/* MPDU header: downlink (bit1=1), lpdu_cnt=2 -> hdr_len = 6+2 = 8 */
		buf[pos++] = 0x02u | (2u << 2); /* direction=downlink, lpdu_cnt=2 */
		buf[pos++] = 0x15u;             /* dst_id (GS), top bit ignored */
		buf[pos++] = 0x22u;             /* src_id (AC) */
		buf[pos++] = 0x00u;             /* reserved/unused by this parse */
		buf[pos++] = 0x00u;
		buf[pos++] = 0x00u;
		buf[pos++] = (uint8_t)(lpdu1_len - 1u); /* size octet 1 */
		buf[pos++] = (uint8_t)(lpdu2_len - 1u); /* size octet 2 */
		uint32_t hdr_len = 8u;
		hfdl_crc_append(buf, hdr_len);
		pos = hdr_len + 2u;

		memcpy(buf + pos, lpdu1, lpdu1_len);
		pos += lpdu1_len;
		memcpy(buf + pos, lpdu2, lpdu2_len);
		pos += lpdu2_len;

		hfdl_mpdu_header_t hdr;
		if (!hfdl_mpdu_parse_header(buf, pos, &hdr)) return false;
		if (!hdr.crc_ok) return false;
		if (hdr.direction != HFDL_PDU_DIR_DOWNLINK) return false;
		if (hdr.dst_id != 0x15u || hdr.src_id != 0x22u) return false;

		hfdl_lpdu_result_t results[8];
		uint32_t n = hfdl_mpdu_extract_lpdus(buf, pos, &hdr, results, 8);
		if (n != 2u) return false;

		if (results[0].kind != HFDL_LPDU_KIND_LOGON_REQUEST || !results[0].crc_ok) return false;
		/* reverse_byte(0x01)=0x80, reverse_byte(0x02)=0x40, reverse_byte(0x04)=0x20
		 * -> icao = 0x80<<16 | 0x40<<8 | 0x20 = 0x804020 */
		if (results[0].icao_address != 0x804020u) return false;

		if (results[1].kind != HFDL_LPDU_KIND_USER_DATA || !results[1].crc_ok) return false;
		if (results[1].user_data_len != 3u) return false;
	}

	/* --- Case 2: uplink MPDU, 2 aircraft, 1 LPDU each (both LOGOFF_REQUEST) --- */
	{
		/* Same LPDU-first ordering as Case 1, for the same reason. */
		uint8_t lpdu_a1[8];
		uint32_t lpdu_a1_len = 0;
		lpdu_a1[lpdu_a1_len++] = (uint8_t)LPDU_TYPE_LOGOFF_REQUEST;
		lpdu_a1[lpdu_a1_len++] = 0x01u; lpdu_a1[lpdu_a1_len++] = 0x02u; lpdu_a1[lpdu_a1_len++] = 0x04u;
		lpdu_a1[lpdu_a1_len++] = 0x03u; /* reason_code */
		hfdl_crc_append(lpdu_a1, lpdu_a1_len);
		lpdu_a1_len += 2u;

		uint8_t lpdu_a2[8];
		uint32_t lpdu_a2_len = 0;
		lpdu_a2[lpdu_a2_len++] = (uint8_t)LPDU_TYPE_LOGOFF_REQUEST;
		lpdu_a2[lpdu_a2_len++] = 0x01u; lpdu_a2[lpdu_a2_len++] = 0x02u; lpdu_a2[lpdu_a2_len++] = 0x04u;
		lpdu_a2[lpdu_a2_len++] = 0x06u;
		hfdl_crc_append(lpdu_a2, lpdu_a2_len);
		lpdu_a2_len += 2u;

		uint8_t buf[64];
		uint32_t pos = 0;

		buf[pos++] = (uint8_t)((1u << 4) | 0x00u); /* direction=uplink (bit1=0), aircraft_cnt = 1+1 = 2 */
		buf[pos++] = 0x30u; /* src_gs_id */

		/* Aircraft 1 sub-header: dst_id, lpdu_cnt=1 in top nibble, then its size octet(s) */
		buf[pos++] = 0x41u;         /* dst_id */
		buf[pos++] = (1u << 4);     /* lpdu_cnt = 1 */
		buf[pos++] = (uint8_t)(lpdu_a1_len - 1u);

		/* Aircraft 2 sub-header */
		buf[pos++] = 0x42u;
		buf[pos++] = (1u << 4);
		buf[pos++] = (uint8_t)(lpdu_a2_len - 1u);

		uint32_t hdr_len = pos;
		hfdl_crc_append(buf, hdr_len);
		pos = hdr_len + 2u;

		memcpy(buf + pos, lpdu_a1, lpdu_a1_len);
		pos += lpdu_a1_len;
		memcpy(buf + pos, lpdu_a2, lpdu_a2_len);
		pos += lpdu_a2_len;

		hfdl_mpdu_header_t hdr;
		if (!hfdl_mpdu_parse_header(buf, pos, &hdr)) return false;
		if (!hdr.crc_ok) return false;
		if (hdr.direction != HFDL_PDU_DIR_UPLINK) return false;
		if (hdr.aircraft_cnt != 2u) return false;
		if (hdr.aircraft[0].dst_id != 0x41u || hdr.aircraft[1].dst_id != 0x42u) return false;

		hfdl_lpdu_result_t results[8];
		uint32_t n = hfdl_mpdu_extract_lpdus(buf, pos, &hdr, results, 8);
		if (n != 2u) return false;
		if (results[0].kind != HFDL_LPDU_KIND_LOGOFF_OR_DENIED || results[0].reason_code != 0x03u) return false;
		if (results[1].kind != HFDL_LPDU_KIND_LOGOFF_OR_DENIED || results[1].reason_code != 0x06u) return false;
	}

	/* --- Case 3: malformed input - MPDU header claims a length that
	 * doesn't fit in the buffer. Must fail cleanly, not overread. --- */
	{
		uint8_t buf[4] = {0x02u | (5u << 2), 0x00u, 0x00u, 0x00u}; /* lpdu_cnt=5 -> hdr_len=11, but buf is only 4 bytes */
		hfdl_mpdu_header_t hdr;
		if (hfdl_mpdu_parse_header(buf, sizeof(buf), &hdr)) return false; /* must return false */
	}

	/* --- Case 4: downlink MPDU header with a deliberately corrupted
	 * FCS - crc_ok must come back false, and extract_lpdus must then
	 * refuse to walk it (return 0) rather than trust bogus offsets. --- */
	{
		uint8_t buf[16] = {0};
		buf[0] = 0x02u; /* downlink, lpdu_cnt=0 -> hdr_len=6 */
		hfdl_crc_append(buf, 6u);
		buf[6] ^= 0xFFu; /* corrupt the FCS */

		hfdl_mpdu_header_t hdr;
		if (!hfdl_mpdu_parse_header(buf, 8u, &hdr)) return false; /* parses structurally fine */
		if (hdr.crc_ok) return false; /* but FCS must be reported bad */

		hfdl_lpdu_result_t results[4];
		uint32_t n = hfdl_mpdu_extract_lpdus(buf, 8u, &hdr, results, 4);
		if (n != 0u) return false;
	}

	return true;
}

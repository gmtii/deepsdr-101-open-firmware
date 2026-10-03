#ifndef HFDL_CRC_H
#define HFDL_CRC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HFDL frame check sequence - CRC-16/X-25 (a.k.a. CRC-16/IBM-SDLC),
 * added as the first, self-contained building block of Phase 2
 * (HFDL demodulation/decoding), per the project owner's staged plan.
 *
 * This is NOT an HFDL-specific algorithm - it's the same FCS used by
 * HDLC, X.25 and PPP: reflected CRC-16, poly 0x1021 (bit-reversed in
 * the lookup table so the byte-at-a-time loop needs no runtime bit
 * reversal), init 0xFFFF, and the final CRC XORed with 0xFFFF before
 * being compared against the two FCS bytes transmitted in the frame
 * (little-endian, i.e. buf[len] | (buf[len+1] << 8)).
 *
 * Confirmed against dumphfdl's src/pdu.c (szpajder/dumphfdl, GPL-3.0-
 * or-later) - see hfdl_pdu_fcs_check() there: the FCS covers every
 * byte of the frame EXCEPT the trailing 2 FCS bytes themselves.
 *
 * This module has zero dependency on the rest of the HFDL Phase 2
 * pipeline (no symbol sync, no Viterbi, no framer) - it's built and
 * tested standalone first, against a known test vector, before being
 * wired into hfdl_pdu_parse() later in the staged plan.
 */

/* Computes the raw CRC-16/X-25 running value over `len` bytes of
 * `data`, starting from `crc_init`. To check/generate an HFDL FCS,
 * call with crc_init = 0xFFFF, then XOR the result with 0xFFFF - see
 * hfdl_crc_check() below, which already does this. This lower-level
 * entry point is exposed mainly for the standalone self-test. */
uint16_t hfdl_crc16_x25(const uint8_t *data, uint32_t len);

/* Checks a complete HFDL frame's FCS. `buf` points at the start of
 * the frame, `payload_len` is the number of bytes covered by the FCS
 * (i.e. NOT counting the trailing 2 FCS bytes themselves - this
 * matches dumphfdl's hdr_len convention). The 2 FCS bytes are read
 * from buf[payload_len] / buf[payload_len + 1] (little-endian).
 * Returns true if the frame's FCS matches the computed value. */
bool hfdl_crc_check(const uint8_t *buf, uint32_t payload_len);

/* Appends a correct little-endian FCS to buf[payload_len] and
 * buf[payload_len + 1] - buf must have room for payload_len + 2
 * bytes. Mainly useful for building self-test vectors and for the
 * PC-side dumphfdl cross-check mentioned in hfdl_crc_selftest(). */
void hfdl_crc_append(uint8_t *buf, uint32_t payload_len);

/* Runs the module's standalone self-test against a known-good test
 * vector (see hfdl_crc.c for where it comes from). Returns true if
 * the implementation matches the expected result. Call once at boot
 * (e.g. from a debug/init path) - cheap, no side effects. */
bool hfdl_crc_selftest(void);

#endif /* HFDL_CRC_H */

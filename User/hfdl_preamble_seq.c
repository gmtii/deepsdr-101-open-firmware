#include "hfdl_preamble_seq.h"

/* Literal copy of dumphfdl's hfdl_init_globals() A_octets[] (src/hfdl.c,
 * szpajder/dumphfdl, GPL-3.0-or-later). */
const uint8_t HFDL_A_OCTETS[16] = {
	0x5B, /* 0b01011011 */
	0xBC, /* 0b10111100 */
	0x74, /* 0b01110100 */
	0x57, /* 0b01010111 */
	0x03, /* 0b00000011 */
	0xD9, /* 0b11011001 */
	0x89, /* 0b10001001 */
	0x39, /* 0b00111001 */
	0xF2, /* 0b11110010 */
	0x08, /* 0b00001000 */
	0xD5, /* 0b11010101 */
	0x36, /* 0b00110110 */
	0x94, /* 0b10010100 */
	0x2C, /* 0b00101100 */
	0x32, /* 0b00110010 */
	0xFE  /* 0b11111110 */
};

/* Literal copy of dumphfdl's hfdl_init_globals() M1_bits[M1_LEN]
 * (src/hfdl.c). Kept as plain 0/1 uint8_t here, same as dumphfdl
 * declares it (uint32_t there, uint8_t here - values are only 0/1
 * either way, no information lost). */
const uint8_t HFDL_M1_BASE_BITS[HFDL_M1_LEN] = {
	0,1,1,1,0,1,1,0,1,1,1,1,0,1,0,0,0,1,0,1,1,0,0,
	1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,
	0,0,0,1,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,0,0,1,0,0,1,1,
	0,0,0,0,0,1,0,1,0,1,0,1,1,0,1,0,0,1,0,0,1,0,1,0,0,1,
	1,1,1,0,0,1,0,0,0,1,1,0,1,0,1,0,0,0,0,1,1,1,1,1,1,1
};

/* Literal copy of dumphfdl's hfdl_init_globals() M_shifts[M_SHIFT_CNT]
 * (src/hfdl.c). */
const uint16_t HFDL_M_SHIFTS[HFDL_M_SHIFT_CNT] = { 72, 82, 113, 123, 61, 103, 93, 9 };

/* Literal copy of dumphfdl's hfdl_frame_params[M_SHIFT_CNT]
 * (src/hfdl.c) - comments there label these by bitrate/slot count,
 * kept here for the same reason. */
const hfdl_frame_params_t HFDL_FRAME_PARAMS[HFDL_M_SHIFT_CNT] = {
	/* [0] 300 bps, single slot */
	{ .mod_arity = HFDL_MOD_BPSK, .data_segment_cnt = 72,  .code_rate = 4, .deinterleaver_push_column_shift = 17 },
	/* [1] 600 bps, single slot */
	{ .mod_arity = HFDL_MOD_BPSK, .data_segment_cnt = 72,  .code_rate = 2, .deinterleaver_push_column_shift = 17 },
	/* [2] 1200 bps, single slot */
	{ .mod_arity = HFDL_MOD_PSK4, .data_segment_cnt = 72,  .code_rate = 2, .deinterleaver_push_column_shift = 17 },
	/* [3] 1800 bps, single slot */
	{ .mod_arity = HFDL_MOD_PSK8, .data_segment_cnt = 72,  .code_rate = 2, .deinterleaver_push_column_shift = 17 },
	/* [4] 300 bps, double slot */
	{ .mod_arity = HFDL_MOD_BPSK, .data_segment_cnt = 168, .code_rate = 4, .deinterleaver_push_column_shift = 23 },
	/* [5] 600 bps, double slot */
	{ .mod_arity = HFDL_MOD_BPSK, .data_segment_cnt = 168, .code_rate = 2, .deinterleaver_push_column_shift = 23 },
	/* [6] 1200 bps, double slot */
	{ .mod_arity = HFDL_MOD_PSK4, .data_segment_cnt = 168, .code_rate = 2, .deinterleaver_push_column_shift = 23 },
	/* [7] 1800 bps, double slot */
	{ .mod_arity = HFDL_MOD_PSK8, .data_segment_cnt = 168, .code_rate = 2, .deinterleaver_push_column_shift = 23 },
};

void hfdl_preamble_seq_build_a(float32_t out[HFDL_A_LEN])
{
	/* Mirrors liquid-dsp's bsequence_init() exactly (see this
	 * file's header): MSB-first per byte, in array order = oldest-
	 * to-newest / chronological order, which is exactly what
	 * hfdl_framer_t's ref_bits[] expects (see hfdl_framer.h). */
	uint32_t bit_idx = 0;
	for (uint32_t byte_idx = 0; byte_idx < 16u && bit_idx < HFDL_A_LEN; byte_idx++) {
		uint8_t byte = HFDL_A_OCTETS[byte_idx];
		for (int b = 7; b >= 0 && bit_idx < HFDL_A_LEN; b--) {
			uint8_t bit = (byte >> b) & 0x01u;
			out[bit_idx++] = bit ? 1.0f : -1.0f;
		}
	}
}

void hfdl_preamble_seq_build_m1_variant(uint32_t shift_idx, float32_t out[HFDL_M1_LEN])
{
	if (shift_idx >= HFDL_M_SHIFT_CNT) {
		shift_idx = 0u; /* defensive - caller bug, avoid an out-of-bounds HFDL_M_SHIFTS[] read */
	}
	uint16_t shift = HFDL_M_SHIFTS[shift_idx];
	for (uint32_t j = 0; j < HFDL_M1_LEN; j++) {
		uint8_t bit = HFDL_M1_BASE_BITS[(shift + j) % HFDL_M1_LEN];
		out[j] = bit ? 1.0f : -1.0f;
	}
}

/* T_seq - literal transcription of dumphfdl's src/hfdl.c
 * "static float complex T_seq[2][T_LEN]" table - see hfdl_preamble_seq.h's
 * declaration comment. Real part only (every imaginary part is 0.0f
 * in dumphfdl's own table). */
const float32_t HFDL_T_SEQ[2][15] = {
	{  1.f,  1.f,  1.f, -1.f,  1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f, -1.f, -1.f, -1.f, -1.f },
	{ -1.f, -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,  1.f, -1.f,  1.f, -1.f,  1.f,  1.f,  1.f,  1.f }
};

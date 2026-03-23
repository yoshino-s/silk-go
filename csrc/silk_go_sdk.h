/* Buffer-based SILK v3 encode/decode for Go (ccgo). PCM = little-endian int16 mono. */
#ifndef SILK_GO_SDK_H
#define SILK_GO_SDK_H

#include <stddef.h>
#include <stdint.h>

enum {
	SILK_SDK_OK = 0,
	SILK_SDK_ERR_HEADER = -1000,
	SILK_SDK_ERR_SHORT_READ = -1001,
	SILK_SDK_ERR_ALLOC = -1002,
	SILK_SDK_ERR_CORRUPT = -1003,
	SILK_SDK_ERR_DECODE = -1004,
	SILK_SDK_ERR_ENCODE = -1005,
	SILK_SDK_ERR_BAD_ARG = -1006,
};

void silk_sdk_free(void *p);

/* api_sample_rate_hz: 0 -> 24000. packet_loss_percent: 0..100 (decoder simulation). */
int32_t silk_decode_pcm(
	const uint8_t *bitstream,
	size_t bitstream_len,
	uint8_t **out_pcm,
	size_t *out_pcm_len,
	int32_t api_sample_rate_hz,
	float packet_loss_percent);

/*
 * Encode PCM -> SILK container (#!SILK_V3). pcm_len must be even.
 * max_internal_fs_hz: 0 -> auto. target_rate_bps: 0 -> encoder default. packet_size_ms: 0 -> 20.
 * tencent_compat: 0/1 prepends STX before header.
 * complexity: 0 or negative (except -2) -> kn007 default (2). -2 -> explicit SKP low (0). 1..2 -> medium/high.
 */
int32_t silk_encode_silk(
	const uint8_t *pcm_s16le,
	size_t pcm_byte_len,
	uint8_t **out_bitstream,
	size_t *out_bitstream_len,
	int32_t api_fs_hz,
	int32_t max_internal_fs_hz,
	int32_t target_rate_bps,
	int32_t packet_size_ms,
	int32_t packet_loss_perc,
	int32_t complexity,
	int32_t use_inband_fec,
	int32_t use_dtx,
	int32_t tencent_compat);

#endif

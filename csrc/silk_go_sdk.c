/*
 * Buffer I/O SILK v3 encoder/decoder derived from kn007/silk-v3-decoder test/Decoder.c and Encoder.c (Skype SILK SDK).
 */
#include "silk_go_sdk.h"
#include <stdlib.h>
#include <string.h>
#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_SigProc_FIX.h"

#ifdef _SYSTEM_IS_BIG_ENDIAN
static void swap_endian_s16(SKP_int16 *vec, SKP_int len) {
	SKP_int i;
	for (i = 0; i < len; i++) {
		SKP_int16 tmp = vec[i];
		SKP_uint8 *p1 = (SKP_uint8 *)&vec[i];
		SKP_uint8 *p2 = (SKP_uint8 *)&tmp;
		p1[0] = p2[1];
		p1[1] = p2[0];
	}
}
#endif

#define DEC_MAX_BYTES_PER_FRAME 1024
#define DEC_MAX_INPUT_FRAMES 5
#define DEC_FRAME_LENGTH_MS 20
#define DEC_MAX_API_FS_KHZ 48
#define DEC_MAX_LBRR_DELAY 2

#define ENC_MAX_BYTES_PER_FRAME 250
#define ENC_MAX_INPUT_FRAMES 5
#define ENC_FRAME_LENGTH_MS 20
#define ENC_MAX_API_FS_KHZ 48

typedef struct {
	uint8_t *d;
	size_t len;
	size_t cap;
} grow_buf;

static void gb_free(grow_buf *b) {
	free(b->d);
	b->d = NULL;
	b->len = b->cap = 0;
}

static int32_t gb_reserve(grow_buf *b, size_t add) {
	size_t need = b->len + add;
	if (need <= b->cap) {
		return SILK_SDK_OK;
	}
	size_t ncap = b->cap ? b->cap : 4096U;
	while (ncap < need) {
		if (ncap > SIZE_MAX / 2U) {
			return SILK_SDK_ERR_ALLOC;
		}
		ncap *= 2U;
	}
	uint8_t *p = (uint8_t *)realloc(b->d, ncap);
	if (!p) {
		return SILK_SDK_ERR_ALLOC;
	}
	b->d = p;
	b->cap = ncap;
	return SILK_SDK_OK;
}

static int32_t gb_append(grow_buf *b, const void *src, size_t n) {
	int32_t e = gb_reserve(b, n);
	if (e) {
		return e;
	}
	memcpy(b->d + b->len, src, n);
	b->len += n;
	return SILK_SDK_OK;
}

static int rd_raw(const uint8_t **pp, size_t *rem, void *dst, size_t n) {
	if (*rem < n) {
		return -1;
	}
	memcpy(dst, *pp, n);
	*pp += n;
	*rem -= n;
	return 0;
}

static int rd_i16le(const uint8_t **pp, size_t *rem, SKP_int16 *out) {
	if (rd_raw(pp, rem, out, sizeof(SKP_int16))) {
		return -1;
	}
#ifdef _SYSTEM_IS_BIG_ENDIAN
	{
		SKP_uint8 *p1 = (SKP_uint8 *)out;
		SKP_uint8 t = p1[0];
		p1[0] = p1[1];
		p1[1] = t;
	}
#endif
	return 0;
}

static int32_t append_i16le(grow_buf *b, SKP_int16 v) {
#ifdef _SYSTEM_IS_BIG_ENDIAN
	{
		SKP_uint8 *p = (SKP_uint8 *)&v;
		SKP_uint8 t = p[0];
		p[0] = p[1];
		p[1] = t;
	}
#endif
	return gb_append(b, &v, sizeof(v));
}

static int32_t append_pcm_frame(grow_buf *b, const SKP_int16 *s, SKP_int n) {
#ifdef _SYSTEM_IS_BIG_ENDIAN
	/* rare: expand to LE */
	size_t i;
	for (i = 0; i < (size_t)n; i++) {
		SKP_int16 v = s[i];
		SKP_uint8 le[2];
		le[0] = (SKP_uint8)(v & 0xff);
		le[1] = (SKP_uint8)((v >> 8) & 0xff);
		int32_t e = gb_append(b, le, 2);
		if (e) {
			return e;
		}
	}
	return SILK_SDK_OK;
#else
	return gb_append(b, s, (size_t)n * sizeof(SKP_int16));
#endif
}

static int32_t parse_bitstream_header(const uint8_t **pp, size_t *rem) {
	if (*rem < 1U) {
		return SILK_SDK_ERR_HEADER;
	}
	uint8_t b0 = **pp;
	(*pp)++;
	(*rem)--;
	if (b0 == 0x02) {
		static const char p9[9] = "#!SILK_V3";
		if (*rem < 9U) {
			return SILK_SDK_ERR_HEADER;
		}
		if (memcmp(*pp, p9, 9) != 0) {
			return SILK_SDK_ERR_HEADER;
		}
		*pp += 9;
		*rem -= 9;
		return SILK_SDK_OK;
	}
	if (b0 != '#') {
		return SILK_SDK_ERR_HEADER;
	}
	static const char p8[8] = "!SILK_V3";
	if (*rem < 8U) {
		return SILK_SDK_ERR_HEADER;
	}
	if (memcmp(*pp, p8, 8) != 0) {
		return SILK_SDK_ERR_HEADER;
	}
	*pp += 8;
	*rem -= 8;
	return SILK_SDK_OK;
}

void silk_sdk_free(void *p) {
	free(p);
}

typedef struct {
	int32_t API_sampleRate_Hz;
	float packetLoss_percent;
} SilkDecodeParams;

typedef struct {
	int32_t API_fs_Hz;
	int32_t max_internal_fs_Hz;
	int32_t targetRate_bps;
	int32_t packetSize_ms;
	int32_t packetLoss_perc;
	int32_t complexity;
	int32_t useInbandFEC;
	int32_t useDTX;
	int32_t tencent_compat;
} SilkEncodeParams;

static int32_t silk_decode_pcm_impl(
	const uint8_t *bitstream,
	size_t bitstream_len,
	uint8_t **out_pcm,
	size_t *out_pcm_len,
	const SilkDecodeParams *parg) {
	if (!out_pcm || !out_pcm_len || !bitstream || !parg) {
		return SILK_SDK_ERR_BAD_ARG;
	}
	*out_pcm = NULL;
	*out_pcm_len = 0;

	const uint8_t *rd = bitstream;
	size_t rem = bitstream_len;
	int32_t eh = parse_bitstream_header(&rd, &rem);
	if (eh) {
		return eh;
	}

	SKP_uint8 payload[DEC_MAX_BYTES_PER_FRAME * DEC_MAX_INPUT_FRAMES * (DEC_MAX_LBRR_DELAY + 1)];
	SKP_uint8 FECpayload[DEC_MAX_BYTES_PER_FRAME * DEC_MAX_INPUT_FRAMES];
	SKP_int16 nBytesPerPacket[DEC_MAX_LBRR_DELAY + 1];
	SKP_int16 out[(DEC_FRAME_LENGTH_MS * DEC_MAX_API_FS_KHZ) * DEC_MAX_INPUT_FRAMES * 2];
	SKP_int32 decSizeBytes;
	void *psDec = NULL;
	SKP_int16 ret, len, tot_len;
	SKP_int16 nBytes, nBytesFEC;
	SKP_uint8 *payloadEnd = NULL;
	SKP_uint8 *payloadToDec = NULL;
	SKP_uint8 *payloadPtr;
	SKP_int32 frames, lost;
	SKP_int32 i, k;
	SKP_int16 totBytes;
	SKP_int16 *outPtr;
	SKP_float loss_prob = parg->packetLoss_percent;
	static SKP_int32 rand_seed = 1;
	SKP_SILK_SDK_DecControlStruct DecControl;
	grow_buf pcm = {0};
	int32_t ge;

	memset(nBytesPerPacket, 0, sizeof(nBytesPerPacket));

	if (parg->API_sampleRate_Hz == 0) {
		DecControl.API_sampleRate = 24000;
	} else {
		DecControl.API_sampleRate = parg->API_sampleRate_Hz;
	}
	DecControl.framesPerPacket = 1;

	ret = (SKP_int16)SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
	if (ret) {
		return SILK_SDK_ERR_DECODE;
	}
	psDec = malloc((size_t)decSizeBytes);
	if (!psDec) {
		return SILK_SDK_ERR_ALLOC;
	}
	ret = (SKP_int16)SKP_Silk_SDK_InitDecoder(psDec);
	if (ret) {
		free(psDec);
		return SILK_SDK_ERR_DECODE;
	}

	payloadEnd = payload;

	/* Prime jitter buffer (same as Decoder.c) */
	for (i = 0; i < DEC_MAX_LBRR_DELAY; i++) {
		if (rd_i16le(&rd, &rem, &nBytes)) {
			if (i == 0) {
				goto ok_out;
			}
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}
		if (nBytes < 0) {
			if (i == 0) {
				goto ok_out;
			}
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}
		if ((size_t)nBytes > rem) {
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}
		if (rd_raw(&rd, &rem, payloadEnd, (size_t)nBytes)) {
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}
		nBytesPerPacket[i] = nBytes;
		payloadEnd += nBytes;
	}

	for (;;) {
		if (rd_i16le(&rd, &rem, &nBytes)) {
			break;
		}
		if (nBytes < 0) {
			break;
		}
		if ((size_t)nBytes > rem) {
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}
		if (rd_raw(&rd, &rem, payloadEnd, (size_t)nBytes)) {
			ge = SILK_SDK_ERR_SHORT_READ;
			goto fail;
		}

		rand_seed = SKP_RAND(rand_seed);
		if ((((SKP_float)((rand_seed >> 16) + (1 << 15))) / 65535.0f >= (loss_prob / 100.0f)) && (nBytes > 0)) {
			nBytesPerPacket[DEC_MAX_LBRR_DELAY] = nBytes;
			payloadEnd += nBytes;
		} else {
			nBytesPerPacket[DEC_MAX_LBRR_DELAY] = 0;
		}

		if (nBytesPerPacket[0] == 0) {
			lost = 1;
			payloadPtr = payload;
			for (i = 0; i < DEC_MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], (i + 1), FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		} else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}

		outPtr = out;
		tot_len = 0;
		if (lost == 0) {
			frames = 0;
			do {
				len = (SKP_int16)(sizeof(out) / sizeof(SKP_int16));
				ret = (SKP_int16)SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					ge = SILK_SDK_ERR_DECODE;
					goto fail;
				}
				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > DEC_MAX_INPUT_FRAMES) {
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
			} while (DecControl.moreInternalDecoderFrames);
		} else {
			for (i = 0; i < DecControl.framesPerPacket; i++) {
				len = (SKP_int16)(sizeof(out) / sizeof(SKP_int16));
				ret = (SKP_int16)SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					ge = SILK_SDK_ERR_DECODE;
					goto fail;
				}
				outPtr += len;
				tot_len += len;
			}
		}
		ge = append_pcm_frame(&pcm, out, tot_len);
		if (ge) {
			goto fail;
		}

		totBytes = 0;
		for (i = 0; i < DEC_MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}
		if (totBytes < 0 || (size_t)totBytes > sizeof(payload)) {
			ge = SILK_SDK_ERR_CORRUPT;
			goto fail;
		}
		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], DEC_MAX_LBRR_DELAY * sizeof(SKP_int16));
	}

	/* Drain jitter buffer */
	for (k = 0; k < DEC_MAX_LBRR_DELAY; k++) {
		if (nBytesPerPacket[0] == 0) {
			lost = 1;
			payloadPtr = payload;
			for (i = 0; i < DEC_MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], (i + 1), FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		} else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}

		outPtr = out;
		tot_len = 0;
		if (lost == 0) {
			frames = 0;
			do {
				len = (SKP_int16)(sizeof(out) / sizeof(SKP_int16));
				ret = (SKP_int16)SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					ge = SILK_SDK_ERR_DECODE;
					goto fail;
				}
				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > DEC_MAX_INPUT_FRAMES) {
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
			} while (DecControl.moreInternalDecoderFrames);
		} else {
			for (i = 0; i < DecControl.framesPerPacket; i++) {
				len = (SKP_int16)(sizeof(out) / sizeof(SKP_int16));
				ret = (SKP_int16)SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					ge = SILK_SDK_ERR_DECODE;
					goto fail;
				}
				outPtr += len;
				tot_len += len;
			}
		}
		ge = append_pcm_frame(&pcm, out, tot_len);
		if (ge) {
			goto fail;
		}

		totBytes = 0;
		for (i = 0; i < DEC_MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}
		if (totBytes < 0 || (size_t)totBytes > sizeof(payload)) {
			ge = SILK_SDK_ERR_CORRUPT;
			goto fail;
		}
		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], DEC_MAX_LBRR_DELAY * sizeof(SKP_int16));
	}

ok_out:
	free(psDec);
	*out_pcm = pcm.d;
	*out_pcm_len = pcm.len;
	return SILK_SDK_OK;

fail:
	free(psDec);
	gb_free(&pcm);
	return ge;
}

int32_t silk_decode_pcm(
	const uint8_t *bitstream,
	size_t bitstream_len,
	uint8_t **out_pcm,
	size_t *out_pcm_len,
	int32_t api_sample_rate_hz,
	float packet_loss_percent) {
	SilkDecodeParams p;
	memset(&p, 0, sizeof(p));
	p.API_sampleRate_Hz = api_sample_rate_hz;
	p.packetLoss_percent = packet_loss_percent;
	return silk_decode_pcm_impl(bitstream, bitstream_len, out_pcm, out_pcm_len, &p);
}

static int32_t silk_encode_silk_impl(
	const uint8_t *pcm_s16le,
	size_t pcm_byte_len,
	uint8_t **out_bitstream,
	size_t *out_bitstream_len,
	const SilkEncodeParams *params) {
	SKP_uint8 payload[ENC_MAX_BYTES_PER_FRAME * ENC_MAX_INPUT_FRAMES];
	SKP_int16 in[ENC_FRAME_LENGTH_MS * ENC_MAX_API_FS_KHZ * ENC_MAX_INPUT_FRAMES];
	SKP_int32 encSizeBytes;
	void *psEnc = NULL;
	SKP_int ret;
	SKP_int16 nBytes;
	SKP_int32 API_fs_Hz;
	SKP_int32 max_internal_fs_Hz;
	SKP_int32 targetRate_bps;
	SKP_int32 smplsSinceLastPacket;
	SKP_int32 packetSize_ms;
	SKP_int32 frameSizeReadFromFile_ms = ENC_FRAME_LENGTH_MS;
	SKP_int32 packetLoss_perc;
	SKP_int32 complexity_mode;
	SKP_int32 DTX_enabled;
	SKP_int32 INBandFEC_enabled;
	SKP_int32 tencent;
	SKP_SILK_SDK_EncControlStruct encControl;
	SKP_SILK_SDK_EncControlStruct encStatus;
	grow_buf bits = {0};
	int32_t ge;
	const uint8_t *pcm_rd = pcm_s16le;
	size_t pcm_rem = pcm_byte_len;

	if (!pcm_s16le || !out_bitstream || !out_bitstream_len || !params) {
		return SILK_SDK_ERR_BAD_ARG;
	}
	*out_bitstream = NULL;
	*out_bitstream_len = 0;
	if (pcm_byte_len % 2U != 0) {
		return SILK_SDK_ERR_BAD_ARG;
	}

	API_fs_Hz = params->API_fs_Hz;
	if (API_fs_Hz <= 0) {
		API_fs_Hz = 24000;
	}
	max_internal_fs_Hz = params->max_internal_fs_Hz;
	targetRate_bps = params->targetRate_bps;
	packetSize_ms = params->packetSize_ms;
	if (packetSize_ms <= 0) {
		packetSize_ms = 20;
	}
	packetLoss_perc = params->packetLoss_perc;
	if (packetLoss_perc < 0) {
		packetLoss_perc = 0;
	}
	complexity_mode = params->complexity;
#if LOW_COMPLEXITY_ONLY
	complexity_mode = 0;
#else
	/* kn007 Encoder.c default is 2 when -complexity is not passed; Go zero EncodeOptions uses 0 for that.
	   Pass complexity == -2 for explicit SKP low (0), e.g. CLI -complexity 0. */
	if (complexity_mode == -2) {
		complexity_mode = 0;
	} else if (complexity_mode <= 0) {
		complexity_mode = 2;
	} else if (complexity_mode > 2) {
		complexity_mode = 2;
	}
#endif
	INBandFEC_enabled = params->useInbandFEC ? 1 : 0;
	DTX_enabled = params->useDTX ? 1 : 0;
	tencent = params->tencent_compat ? 1 : 0;

	if (max_internal_fs_Hz == 0) {
		max_internal_fs_Hz = 24000;
		if (API_fs_Hz < max_internal_fs_Hz) {
			max_internal_fs_Hz = API_fs_Hz;
		}
	}

	if (API_fs_Hz > ENC_MAX_API_FS_KHZ * 1000 || API_fs_Hz < 0) {
		return SILK_SDK_ERR_BAD_ARG;
	}

	if (tencent) {
		static const char brk[] = "\x02";
		ge = gb_append(&bits, brk, 1);
		if (ge) {
			return ge;
		}
	}
	{
		static const char hdr[] = "#!SILK_V3";
		ge = gb_append(&bits, hdr, 9);
		if (ge) {
			gb_free(&bits);
			return ge;
		}
	}

	ret = SKP_Silk_SDK_Get_Encoder_Size(&encSizeBytes);
	if (ret) {
		gb_free(&bits);
		return SILK_SDK_ERR_ENCODE;
	}
	psEnc = malloc((size_t)encSizeBytes);
	if (!psEnc) {
		gb_free(&bits);
		return SILK_SDK_ERR_ALLOC;
	}
	ret = SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);
	if (ret) {
		free(psEnc);
		gb_free(&bits);
		return SILK_SDK_ERR_ENCODE;
	}

	memset(&encControl, 0, sizeof(encControl));
	encControl.API_sampleRate = API_fs_Hz;
	encControl.maxInternalSampleRate = max_internal_fs_Hz;
	encControl.packetSize = (packetSize_ms * API_fs_Hz) / 1000;
	encControl.packetLossPercentage = packetLoss_perc;
	encControl.useInBandFEC = INBandFEC_enabled;
	encControl.useDTX = DTX_enabled;
	encControl.complexity = complexity_mode;
	encControl.bitRate = (targetRate_bps > 0 ? targetRate_bps : 0);

	smplsSinceLastPacket = 0;

	for (;;) {
		SKP_int need = (frameSizeReadFromFile_ms * API_fs_Hz) / 1000;
		size_t need_bytes = (size_t)need * sizeof(SKP_int16);
		if (pcm_rem < need_bytes) {
			break;
		}
		memcpy(in, pcm_rd, need_bytes);
#ifdef _SYSTEM_IS_BIG_ENDIAN
		swap_endian_s16(in, need);
#endif
		pcm_rd += need_bytes;
		pcm_rem -= need_bytes;

		nBytes = ENC_MAX_BYTES_PER_FRAME * ENC_MAX_INPUT_FRAMES;
		ret = SKP_Silk_SDK_Encode(psEnc, &encControl, in, (SKP_int16)need, payload, &nBytes);
		if (ret) {
			free(psEnc);
			gb_free(&bits);
			return SILK_SDK_ERR_ENCODE;
		}

		packetSize_ms = (SKP_int)((1000 * (SKP_int32)encControl.packetSize) / encControl.API_sampleRate);
		smplsSinceLastPacket += need;

		if (((1000 * smplsSinceLastPacket) / API_fs_Hz) == packetSize_ms) {
			ge = append_i16le(&bits, nBytes);
			if (ge) {
				free(psEnc);
				gb_free(&bits);
				return ge;
			}
			ge = gb_append(&bits, payload, (size_t)nBytes);
			if (ge) {
				free(psEnc);
				gb_free(&bits);
				return ge;
			}
			smplsSinceLastPacket = 0;
		}
	}

	nBytes = -1;
	if (!tencent) {
		ge = append_i16le(&bits, nBytes);
		if (ge) {
			free(psEnc);
			gb_free(&bits);
			return ge;
		}
	}

	free(psEnc);
	*out_bitstream = bits.d;
	*out_bitstream_len = bits.len;
	return SILK_SDK_OK;
}

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
	int32_t tencent_compat) {
	SilkEncodeParams p;
	memset(&p, 0, sizeof(p));
	p.API_fs_Hz = api_fs_hz;
	p.max_internal_fs_Hz = max_internal_fs_hz;
	p.targetRate_bps = target_rate_bps;
	p.packetSize_ms = packet_size_ms;
	p.packetLoss_perc = packet_loss_perc;
	p.complexity = complexity;
	p.useInbandFEC = use_inband_fec;
	p.useDTX = use_dtx;
	p.tencent_compat = tencent_compat;
	return silk_encode_silk_impl(pcm_s16le, pcm_byte_len, out_bitstream, out_bitstream_len, &p);
}

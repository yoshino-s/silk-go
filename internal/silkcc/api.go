//go:build (linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64))

package silkcc

import (
	"errors"
	"fmt"
	"runtime"
	"unsafe"

	"modernc.org/libc"
)

// SDK status codes (mirror csrc/silk_go_sdk.h).
const (
	ErrHeader    = -1000
	ErrShortRead = -1001
	ErrAlloc     = -1002
	ErrCorrupt   = -1003
	ErrDecode    = -1004
	ErrEncode    = -1005
	ErrBadArg    = -1006
)

// DecodePCM decodes a SILK v3 container to little-endian int16 mono PCM.
// apiSampleRateHz 0 selects 24000. packetLossPercent is decoder-side loss simulation (0–100).
func DecodePCM(bitstream []byte, apiSampleRateHz int32, packetLossPercent float32) ([]byte, error) {
	if len(bitstream) == 0 {
		return nil, fmt.Errorf("silkcc: %w", ErrEmptyInput)
	}
	tls := libc.NewTLS()
	var outPtr uintptr
	var outLen size_t
	pIn := uintptr(unsafe.Pointer(&bitstream[0]))
	rc := silk_decode_pcm(tls, pIn, size_t(len(bitstream)),
		uintptr(unsafe.Pointer(&outPtr)), uintptr(unsafe.Pointer(&outLen)),
		apiSampleRateHz, packetLossPercent)
	runtime.KeepAlive(bitstream)
	if rc != 0 {
		return nil, statusError(rc)
	}
	if outPtr == 0 || outLen == 0 {
		return nil, nil
	}
	out := make([]byte, int(outLen))
	copy(out, unsafe.Slice((*byte)(unsafe.Pointer(outPtr)), int(outLen)))
	silk_sdk_free(tls, outPtr)
	return out, nil
}

// EncodeSILK encodes little-endian int16 mono PCM into a SILK v3 bitstream (#!SILK_V3 wire format).
// pcm must have even length. Use 0 for “encoder default” where noted.
func EncodeSILK(
	pcm []byte,
	apiFsHz int32,
	maxInternalFsHz int32,
	targetRateBps int32,
	packetSizeMs int32,
	packetLossPerc int32,
	complexity int32,
	useInbandFEC int32,
	useDTX int32,
	tencentCompat int32,
) ([]byte, error) {
	if len(pcm) == 0 {
		return nil, fmt.Errorf("silkcc: %w", ErrEmptyInput)
	}
	if len(pcm)%2 != 0 {
		return nil, fmt.Errorf("silkcc: %w", ErrOddPCM)
	}
	tls := libc.NewTLS()
	var outPtr uintptr
	var outLen size_t
	rc := silk_encode_silk(tls,
		uintptr(unsafe.Pointer(&pcm[0])), size_t(len(pcm)),
		uintptr(unsafe.Pointer(&outPtr)), uintptr(unsafe.Pointer(&outLen)),
		apiFsHz, maxInternalFsHz, targetRateBps, packetSizeMs, packetLossPerc,
		complexity, useInbandFEC, useDTX, tencentCompat,
	)
	runtime.KeepAlive(pcm)
	if rc != 0 {
		return nil, statusError(rc)
	}
	if outPtr == 0 {
		return nil, nil
	}
	out := make([]byte, int(outLen))
	copy(out, unsafe.Slice((*byte)(unsafe.Pointer(outPtr)), int(outLen)))
	silk_sdk_free(tls, outPtr)
	return out, nil
}

var (
	ErrEmptyInput = errors.New("empty input buffer")
	ErrOddPCM     = errors.New("pcm byte length must be even")
)

func statusError(rc int32) error {
	switch rc {
	case ErrHeader:
		return fmt.Errorf("silkcc: invalid silk header (%d)", rc)
	case ErrShortRead:
		return fmt.Errorf("silkcc: truncated bitstream (%d)", rc)
	case ErrAlloc:
		return fmt.Errorf("silkcc: allocation failed (%d)", rc)
	case ErrCorrupt:
		return fmt.Errorf("silkcc: corrupt stream (%d)", rc)
	case ErrDecode:
		return fmt.Errorf("silkcc: decode failed (%d)", rc)
	case ErrEncode:
		return fmt.Errorf("silkcc: encode failed (%d)", rc)
	case ErrBadArg:
		return fmt.Errorf("silkcc: invalid argument (%d)", rc)
	default:
		return fmt.Errorf("silkcc: error %d", rc)
	}
}

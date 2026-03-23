//go:build !((linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64)))

package silk

import (
	"errors"
	"runtime"
)

type DecodeOptions struct {
	SampleRateHz      int32
	PacketLossPercent float32
}

type EncodeOptions struct {
	SampleRateHz            int32
	MaxInternalSampleRateHz int32
	TargetBitrateBps        int32
	PacketSizeMs            int32
	PacketLossPercent       int32
	Complexity              int32
	UseInbandFEC            int32
	UseDTX                  int32
	TencentCompat           int32
}

const ComplexityLow = -2

func DecodePCM(bitstream []byte, opt DecodeOptions) ([]byte, error) {
	return nil, errors.New("silk-go: no ccgo bundle for " + runtime.GOOS + "/" + runtime.GOARCH)
}

func EncodePCM(pcm []byte, opt EncodeOptions) ([]byte, error) {
	return nil, errors.New("silk-go: no ccgo bundle for " + runtime.GOOS + "/" + runtime.GOARCH)
}

//go:build (linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64))

package silk

import (
	"github.com/yoshino-s/silk-go/internal/silkcc"
)

// DecodeOptions controls buffer decode (no CLI parsing).
type DecodeOptions struct {
	// Output sample rate in Hz; 0 means 24000.
	SampleRateHz int32
	// Simulated packet loss percentage (0–100), same as kn007 decoder -loss.
	PacketLossPercent float32
}

// EncodeOptions controls buffer encode (no CLI parsing).
type EncodeOptions struct {
	// API sample rate in Hz; 0 means 24000.
	SampleRateHz int32
	// 0 means auto (min(API rate, 24 kHz)).
	MaxInternalSampleRateHz int32
	// 0 lets encoder pick default target rate.
	TargetBitrateBps int32
	// Packet interval in ms; 0 means 20.
	PacketSizeMs      int32
	PacketLossPercent int32
	// 0 = kn007 default (high / SKP complexity 2). 1 or 2 = SKP medium or high. Use ComplexityLow for SKP low (0).
	Complexity    int32
	UseInbandFEC  int32 // 0/1
	UseDTX        int32 // 0/1
	TencentCompat int32 // 0/1 — prepend STX for WeChat/QQ style header
}

// ComplexityLow selects SKP complexity 0 (low). EncodeOptions.Complexity 0 means the same default as kn007 Encoder without -complexity (2).
const ComplexityLow = -2

// DecodePCM decodes SILK v3 bytes to little-endian int16 mono PCM.
func DecodePCM(bitstream []byte, opt DecodeOptions) ([]byte, error) {
	return silkcc.DecodePCM(bitstream, opt.SampleRateHz, opt.PacketLossPercent)
}

// EncodePCM encodes little-endian int16 mono PCM to SILK v3 bytes.
func EncodePCM(pcm []byte, opt EncodeOptions) ([]byte, error) {
	return silkcc.EncodeSILK(pcm,
		opt.SampleRateHz,
		opt.MaxInternalSampleRateHz,
		opt.TargetBitrateBps,
		opt.PacketSizeMs,
		opt.PacketLossPercent,
		opt.Complexity,
		opt.UseInbandFEC,
		opt.UseDTX,
		opt.TencentCompat,
	)
}

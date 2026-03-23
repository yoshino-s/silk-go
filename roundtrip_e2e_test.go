//go:build (linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64))

//go:generate go run testdata/gen_e2e_pcm.go -o testdata/e2e_24k_mono.pcm

package silk_test

import (
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/yoshino-s/silk-go"
)

func TestRoundTripSyntheticPCM(t *testing.T) {
	const sampleRate = 24000
	const seconds = 3
	pcm := syntheticSinePCM(sampleRate, seconds, 440, 8000)
	silkOut, err := silk.EncodePCM(pcm, silk.EncodeOptions{SampleRateHz: sampleRate})
	if err != nil {
		t.Fatal(err)
	}
	if len(silkOut) == 0 {
		t.Fatal("empty silk output")
	}
	back, err := silk.DecodePCM(silkOut, silk.DecodeOptions{SampleRateHz: sampleRate})
	if err != nil {
		t.Fatal(err)
	}
	if len(back) < len(pcm)/4 {
		t.Fatalf("decoded unexpectedly short: got %d bytes, input %d", len(back), len(pcm))
	}
}

func TestRoundTripTestdataFixture(t *testing.T) {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	root := filepath.Dir(thisFile)
	pcmPath := filepath.Join(root, "testdata", "e2e_24k_mono.pcm")
	pcm, err := os.ReadFile(pcmPath)
	if err != nil {
		t.Fatalf("read fixture %s: %v (run: go run testdata/gen_e2e_pcm.go -o testdata/e2e_24k_mono.pcm)", pcmPath, err)
	}
	const sampleRate = 24000
	silkOut, err := silk.EncodePCM(pcm, silk.EncodeOptions{SampleRateHz: sampleRate})
	if err != nil {
		t.Fatal(err)
	}
	back, err := silk.DecodePCM(silkOut, silk.DecodeOptions{SampleRateHz: sampleRate})
	if err != nil {
		t.Fatal(err)
	}
	if len(back) == 0 {
		t.Fatal("empty decoded PCM")
	}
}

func syntheticSinePCM(sampleRateHz, seconds int, freqHz float64, amp int16) []byte {
	n := sampleRateHz * seconds
	out := make([]byte, n*2)
	for i := 0; i < n; i++ {
		v := int16(float64(amp) * math.Sin(2*math.Pi*freqHz*float64(i)/float64(sampleRateHz)))
		binary.LittleEndian.PutUint16(out[i*2:], uint16(v))
	}
	return out
}

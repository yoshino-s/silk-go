//go:build ignore

// Regenerate fixture: go run testdata/gen_e2e_pcm.go -o testdata/e2e_24k_mono.pcm
package main

import (
	"encoding/binary"
	"flag"
	"math"
	"os"
)

func main() {
	out := flag.String("o", "testdata/e2e_24k_mono.pcm", "output path")
	flag.Parse()
	const sampleRate = 24000
	const seconds = 2
	n := sampleRate * seconds
	pcm := make([]byte, n*2)
	for i := 0; i < n; i++ {
		v := int16(8000 * math.Sin(2*math.Pi*440*float64(i)/float64(sampleRate)))
		binary.LittleEndian.PutUint16(pcm[i*2:], uint16(v))
	}
	if err := os.WriteFile(*out, pcm, 0o644); err != nil {
		panic(err)
	}
}

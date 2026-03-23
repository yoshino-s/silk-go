//go:build (linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64))

package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/yoshino-s/silk-go"
)

var (
	fsAPI int32
	loss  float32
)

func main() {
	root := &cobra.Command{
		Use:          "silk-decoder INPUT.silk OUTPUT.pcm",
		Short:        "Decode SILK v3 to little-endian int16 mono PCM",
		Args:         cobra.ExactArgs(2),
		SilenceUsage: true,
		RunE:         runDecode,
	}
	root.Flags().Int32VarP(&fsAPI, "fs-api", "F", 0, "Output sample rate in Hz (0 = 24000); kn007: -Fs_API")
	root.Flags().Float32Var(&loss, "loss", 0, "Simulated packet loss percent (0–100); kn007: -loss")
	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func runDecode(_ *cobra.Command, args []string) error {
	in, err := os.ReadFile(args[0])
	if err != nil {
		return err
	}
	opt := silk.DecodeOptions{
		SampleRateHz:      fsAPI,
		PacketLossPercent: loss,
	}
	pcm, err := silk.DecodePCM(in, opt)
	if err != nil {
		return err
	}
	return os.WriteFile(args[1], pcm, 0o644)
}

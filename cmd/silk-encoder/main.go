//go:build (linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64))

package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/yoshino-s/silk-go"
)

var (
	fsAPI          int32
	packetMs       int32
	targetBitrate  int32
	loss           int32
	complexity     int32
	inbandFEC      int32
	dtx            int32
	tencent        bool
)

func main() {
	root := &cobra.Command{
		Use:          "silk-encoder INPUT.pcm OUTPUT.silk",
		Short:        "Encode little-endian int16 mono PCM to SILK v3",
		Args:         cobra.ExactArgs(2),
		SilenceUsage: true,
		RunE:         runEncode,
	}
	root.Flags().Int32VarP(&fsAPI, "fs-api", "F", 0, "API sample rate in Hz (0 = 24000); kn007: -Fs_API")
	root.Flags().Int32Var(&packetMs, "packet-ms", 0, "Packet length in ms (0 = 20); kn007: -packetlength")
	root.Flags().Int32Var(&targetBitrate, "bitrate", 0, "Target bitrate in bps (0 = encoder default); kn007: -rate")
	root.Flags().Int32Var(&loss, "loss", 0, "Packet loss percentage for encoder tuning; kn007: -loss")
	root.Flags().Int32Var(&complexity, "complexity", 0, "0–2; omit flag for kn007 default (high). If set, 0 selects low (SKP 0); kn007 -complexity 0")
	root.Flags().Int32Var(&inbandFEC, "inband-fec", 0, "0 or 1; kn007: -inbandFEC")
	root.Flags().Int32Var(&dtx, "dtx", 0, "0 or 1; kn007: -DTX")
	root.Flags().BoolVar(&tencent, "tencent", false, "Prepend Tencent/STX-style header")
	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func runEncode(cmd *cobra.Command, args []string) error {
	pcm, err := os.ReadFile(args[0])
	if err != nil {
		return err
	}
	opt := silk.EncodeOptions{
		SampleRateHz:            fsAPI,
		MaxInternalSampleRateHz: 0,
		TargetBitrateBps:        targetBitrate,
		PacketSizeMs:            packetMs,
		PacketLossPercent:       loss,
		UseInbandFEC:            inbandFEC,
		UseDTX:                  dtx,
	}
	if cmd.Flags().Changed("complexity") {
		if complexity == 0 {
			opt.Complexity = silk.ComplexityLow
		} else {
			opt.Complexity = complexity
		}
	}
	if tencent {
		opt.TencentCompat = 1
	}
	out, err := silk.EncodePCM(pcm, opt)
	if err != nil {
		return err
	}
	return os.WriteFile(args[1], out, 0o644)
}

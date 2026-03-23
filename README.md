# silk-go

Pure Go bindings for **SILK** audio (used inside Opus / common in VoIP), built from [kn007/silk-v3-decoder](https://github.com/kn007/silk-v3-decoder) with **[modernc.org/ccgo](https://modernc.org/ccgo)** so the codec runs **without cgo**. The translated runtime uses **[modernc.org/libc](https://modernc.org/libc)**.

## Features

- **No cgo** — single toolchain `go build`; no host C compiler required for consumers.
- **Buffer API** — encode/decode `[]byte` (little-endian **int16 mono** PCM) instead of upstream CLI `argv`.
- **Tooling** — `silk-encoder` and `silk-decoder` CLIs ([Cobra](https://github.com/spf13/cobra)).
- **Supported platforms** (same as bundled `z_silk_*.go` files):

  | OS      | Architectures        |
  |---------|----------------------|
  | Linux   | 386, amd64, arm, arm64 |
  | macOS   | amd64, arm64         |
  | Windows | 386, amd64, arm64    |

  **Windows/arm** is omitted because `modernc.org/libc` has no port. Windows `z_silk_*.go` files are produced by adapting Linux codegen (see `scripts/regen-silkcc-all.sh`).

## Repository layout

| Path | Purpose |
|------|---------|
| `_upstream/silk-v3-decoder` | **Git submodule** — upstream SILK tree (do not edit in place for long-lived forks; fork upstream separately). |
| `csrc/` | Thin C SDK (`silk_go_sdk.c`) wrapping encoder/decoder for ccgo. |
| `internal/silkcc/` | Generated Go (`z_silk_<goos>_<goarch>.go`) + small `api.go` wrappers. |
| `decode.go` | Public package `silk`: `EncodePCM` / `DecodePCM`. |
| `testdata/e2e_24k_mono.pcm` | 24 kHz mono sine fixture; regenerate with `go generate` (see tests). |

## Clone with submodule

```bash
git clone --recurse-submodules https://github.com/yoshino-s/silk-go.git
# or after a normal clone:
git submodule update --init --recursive
```

## Build and test

```bash
go build ./...
go test ./...
```

End-to-end tests (encode → decode) run only on supported OS/arch combinations (see build tags on `decode.go`).

Regenerate the PCM fixture:

```bash
go generate ./...
# equivalent:
go run testdata/gen_e2e_pcm.go -o testdata/e2e_24k_mono.pcm
```

## Regenerating `internal/silkcc` (maintainers)

After changing `csrc/silk_go_sdk.c` / `silk_go_sdk.h` or ccgo options, rebuild all platform files:

```bash
go install modernc.org/ccgo/v4@v4.32.2
./scripts/regen-silkcc-all.sh
go build ./...
```

Requires a working `ccgo` and cross-target clang setup as described in the script (macOS can generate Linux/Darwin targets; Windows files are shims from Linux).

## Library API

```go
import "github.com/yoshino-s/silk-go"

pcmOut, err := silk.DecodePCM(bitstream, silk.DecodeOptions{
    SampleRateHz:      24000, // 0 means 24000
    PacketLossPercent: 0,     // decoder-side loss simulation, 0–100
})

silkBytes, err := silk.EncodePCM(pcm, silk.EncodeOptions{
    SampleRateHz: 24000, // 0 means 24000
    // other fields: 0 means “encoder default” unless documented otherwise
})
```

### Encoder complexity (aligned with kn007 `Encoder.c`)

| `EncodeOptions.Complexity` | Behaviour |
|----------------------------|-----------|
| `0` (field zero value, flag omitted in CLI) | Same as upstream when **`-complexity` is not passed**: internal **SKP complexity 2**. |
| `silk.ComplexityLow` (`-2`) | Explicit **low** (SKP 0), e.g. when you want the old CLI “`-complexity 0`” meaning. |
| `1`, `2` | Passed through (clamped inside C to valid range). |

Builds with **`LOW_COMPLEXITY_ONLY`** in upstream force low complexity regardless.

## Command-line tools

### silk-encoder

```text
silk-encoder INPUT.pcm OUTPUT.silk [flags]
```

| Flag | kn007 analogue | Description |
|------|----------------|-------------|
| `--fs-api`, `-F` | `-Fs_API` | API sample rate Hz (0 = 24000). |
| `--packet-ms` | `-packetlength` | Packet length ms (0 = 20). |
| `--bitrate` | `-rate` | Target bitrate bps (0 = default). |
| `--loss` | `-loss` | Packet loss % for encoder tuning. |
| `--complexity` | `-complexity` | Omit flag for kn007 default (high). If set, **`0` = low** (SKP 0). |
| `--inband-fec` | `-inbandFEC` | 0 or 1. |
| `--dtx` | `-DTX` | 0 or 1. |
| `--tencent` | (Tencent header) | Prepend STX-style header for WeChat/QQ-style payloads. |

Example:

```bash
go run ./cmd/silk-encoder ./testdata/e2e_24k_mono.pcm /tmp/out.silk --fs-api 24000
```

### silk-decoder

```text
silk-decoder INPUT.silk OUTPUT.pcm [flags]
```

| Flag | kn007 analogue | Description |
|------|----------------|-------------|
| `--fs-api`, `-F` | `-Fs_API` | Output sample rate Hz (0 = 24000). |
| `--loss` | `-loss` | Simulated loss % (0–100). |

## Known limitations

- The decoder implementation may **read ahead** multiple packets. **Very short** bitstreams can fail with **`SILK_SDK_ERR_SHORT_READ` (-1001)**. Use enough PCM / silk data for several frames when testing (the bundled fixture and e2e tests are long enough).

## License

Follow the licenses of upstream SILK (see submodule) and this repository’s own `LICENSE` if present. The ccgo-generated files are derived from upstream C sources.

## 中文文档

See [README_ZH.md](README_ZH.md).

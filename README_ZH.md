# silk-go（中文说明）

基于 [kn007/silk-v3-decoder](https://github.com/kn007/silk-v3-decoder)，用 **[modernc.org/ccgo](https://modernc.org/ccgo)** 将 SILK 编解码翻译成 **纯 Go（无 cgo）** 的库，运行时依赖 **[modernc.org/libc](https://modernc.org/libc)**。

## 特性

- **无 cgo**：使用者只需 `go build`，不必安装 C 编译器。
- **缓冲区 API**：对 `[]byte` 编解码；PCM 为 **小端 int16 单声道**。
- **命令行**：`silk-encoder` / `silk-decoder` 使用 [Cobra](https://github.com/spf13/cobra) 解析参数。
- **支持平台**（与 `internal/silkcc/z_silk_*.go` 一致）：

  | 系统   | 架构 |
  |--------|------|
  | Linux  | 386、amd64、arm、arm64 |
  | macOS  | amd64、arm64 |
  | Windows | 386、amd64、arm64 |

  **Windows/arm** 未支持（`modernc.org/libc` 无该端口）。Windows 的生成文件由 Linux 同架构产物改写 `//go:build` 得到（见 `scripts/regen-silkcc-all.sh`）。

## 目录结构

| 路径 | 说明 |
|------|------|
| `_upstream/silk-v3-decoder` | **Git 子模块**，上游 SILK 源码树。 |
| `csrc/` | C 侧 SDK（`silk_go_sdk.c`），供 ccgo 与 `src/*.c` 一起翻译。 |
| `internal/silkcc/` | 生成的 `z_silk_<goos>_<goarch>.go` 与 `api.go`。 |
| `decode.go` | 对外包名 `silk`：`EncodePCM`、`DecodePCM`。 |
| `testdata/e2e_24k_mono.pcm` | 24 kHz 单声道正弦测试 PCM；可用 `go generate` 重新生成。 |

## 克隆（含子模块）

```bash
git clone --recurse-submodules https://github.com/yoshino-s/silk-go.git
# 若已克隆但未拉子模块：
git submodule update --init --recursive
```

## 构建与测试

```bash
go build ./...
go test ./...
```

端到端测试仅在带 ccgo 生成体的 `GOOS`/`GOARCH` 上编译运行（与 `decode.go` 的 build tag 一致）。

重新生成测试用 PCM：

```bash
go generate ./...
# 或：
go run testdata/gen_e2e_pcm.go -o testdata/e2e_24k_mono.pcm
```

## 维护者：重新生成 `internal/silkcc`

修改 `csrc/silk_go_sdk.c` / `silk_go_sdk.h` 或 ccgo 参数后：

```bash
go install modernc.org/ccgo/v4@v4.32.2
./scripts/regen-silkcc-all.sh
go build ./...
```

需要可用的 `ccgo` 与脚本中所述的交叉 clang；在 macOS 上通常可生成 Linux/Darwin；Windows 文件由 Linux  shim 得到。

## 库 API 概要

```go
import "github.com/yoshino-s/silk-go"

pcmOut, err := silk.DecodePCM(bitstream, silk.DecodeOptions{
    SampleRateHz:      24000, // 0 表示 24000
    PacketLossPercent: 0,     // 解码端模拟丢包 0–100
})

silkBytes, err := silk.EncodePCM(pcm, silk.EncodeOptions{
    SampleRateHz: 24000,
    // 其余字段 0 一般表示“编码器默认”，见英文 README 表格
})
```

### 编码复杂度（与 kn007 `Encoder.c` 一致）

| `EncodeOptions.Complexity` | 含义 |
|----------------------------|------|
| `0`（结构体零值 / 命令行**不传** `--complexity`） | 与上游**未传 `-complexity`** 相同：内部 **SKP complexity 2**。 |
| `silk.ComplexityLow`（`-2`） | 显式**低档**（SKP 0），对应旧命令行里 “`-complexity 0`” 的语义。 |
| `1`、`2` | 传入 C 侧（内部会限制在合法范围）。 |

若上游以 **`LOW_COMPLEXITY_ONLY`** 编译，则始终强制低档。

## 命令行工具

### silk-encoder

```text
silk-encoder INPUT.pcm OUTPUT.silk [flags]
```

常用标志与 kn007 对应关系见下表：

| 标志 | kn007 | 说明 |
|------|-------|------|
| `--fs-api`、`-F` | `-Fs_API` | API 采样率 Hz（0 = 24000）。 |
| `--packet-ms` | `-packetlength` | 包长 ms（0 = 20）。 |
| `--bitrate` | `-rate` | 目标码率 bps（0 = 默认）。 |
| `--loss` | `-loss` | 丢包率（编码器侧调参）。 |
| `--complexity` | `-complexity` | **不传**则与 kn007 默认（高）一致；**若传入且值为 0** 表示低档（SKP 0）。 |
| `--inband-fec` | `-inbandFEC` | 0 或 1。 |
| `--dtx` | `-DTX` | 0 或 1。 |
| `--tencent` | 腾讯头 | 为微信/QQ 风格负载加 STX 等头。 |

示例：

```bash
go run ./cmd/silk-encoder ./testdata/e2e_24k_mono.pcm /tmp/out.silk --fs-api 24000
```

### silk-decoder

```text
silk-decoder INPUT.silk OUTPUT.pcm [flags]
```

| 标志 | kn007 | 说明 |
|------|-------|------|
| `--fs-api`、`-F` | `-Fs_API` | 输出 PCM 采样率 Hz（0 = 24000）。 |
| `--loss` | `-loss` | 模拟丢包百分比（0–100）。 |

## 已知限制

- 解码实现可能**预读多个包**，**极短**的码流可能返回 **`SILK_SDK_ERR_SHORT_READ`（-1001）**。自测时请使用足够长的 PCM/码流（本仓库的 `testdata` 与 e2e 测试已满足）。

## 许可证

请同时遵守上游 SILK（见子模块）与本仓库 `LICENSE`（若有）的许可条款。ccgo 生成文件衍生自上游 C 源码。

## English

See [README.md](README.md).

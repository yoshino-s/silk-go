#!/usr/bin/env bash
# Regenerate internal/silkcc/z_silk_<GOOS>_<GOARCH>.go for each supported platform.
# Requires: go install modernc.org/ccgo/v4@v4.32.2, Apple clang (or clang with cross targets).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="${ROOT}/_upstream/silk-v3-decoder/silk"
OUTDIR="${ROOT}/internal/silkcc"
WRAPPER="${ROOT}/scripts/ccgo-clang-cross.sh"
CCGO="$(command -v ccgo || true)"
if [[ -z "${CCGO}" ]]; then
	CCGO="$(go env GOPATH)/bin/ccgo"
fi
if [[ ! -x "${CCGO}" && ! -f "${CCGO}" ]]; then
	echo "install ccgo: go install modernc.org/ccgo/v4@v4.32.2" >&2
	exit 1
fi

chmod +x "${WRAPPER}" 2>/dev/null || true

mkdir -p "${OUTDIR}"
rm -f "${OUTDIR}"/z_silk_*.go

# goos goarch clang_target
PAIRS=(
	linux 386 i686-linux-gnu
	linux amd64 x86_64-linux-gnu
	linux arm arm-linux-gnueabihf
	linux arm64 aarch64-linux-gnu
	darwin amd64 x86_64-apple-darwin
	darwin arm64 aarch64-apple-darwin
)
# Windows variants are produced below by cloning linux/<arch> (MinGW headers rarely available on macOS).

regen_one() {
	local goos="$1" goarch="$2" target="$3"
	local out="${OUTDIR}/z_silk_${goos}_${goarch}.go"
	echo "==> ${goos}/${goarch} (${target}) -> ${out##*/}"
	export TARGET_GOOS="${goos}"
	export TARGET_GOARCH="${goarch}"
	export CCGO_CPP="${WRAPPER}"
	export CCGO_CLANG_TARGET="${target}"
	(
		cd "${UPSTREAM}"
		"${CCGO}" -ignore-unsupported-alignment \
			-o "${out}" \
			-I "${ROOT}/csrc" -I interface -I src -I test \
			--package-name silkcc \
			"${ROOT}/csrc/silk_go_sdk.c" \
			$(ls src/*.c)
	) || {
		echo "FAILED ${goos}/${goarch}" >&2
		rm -f "${out}"
		return 0
	}
}

i=0
len=${#PAIRS[@]}
while ((i < len)); do
	regen_one "${PAIRS[i]}" "${PAIRS[i + 1]}" "${PAIRS[i + 2]}"
	i=$((i + 3))
done

# Windows: MinGW sysroots are often missing on macOS; reuse linux/<same arch> codegen.
# modernc.org/libc abstracts OS for stdio/file paths used here; replace //go:build only.
clone_windows_from_linux() {
	local arch="$1"
	local src="${OUTDIR}/z_silk_linux_${arch}.go"
	local dst="${OUTDIR}/z_silk_windows_${arch}.go"
	if [[ ! -f "${src}" ]]; then
		return 0
	fi
	echo "==> windows/${arch} (shim from linux/${arch}) -> ${dst##*/}"
	sed '0,/^\/\/go:build linux &&/s/^\/\/go:build linux &&/\/\/go:build windows \&\&/' "${src}" >"${dst}"
}

clone_windows_from_linux 386
clone_windows_from_linux amd64
clone_windows_from_linux arm64
# windows/arm: modernc.org/libc has no port; omit.

count="$(ls -1 "${OUTDIR}"/z_silk_*.go 2>/dev/null | wc -l | tr -d ' ')"
echo "done: ${count} z_silk_*.go file(s) in ${OUTDIR}"

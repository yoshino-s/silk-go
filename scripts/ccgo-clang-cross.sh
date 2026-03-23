#!/usr/bin/env bash
# Used as CCGO_CPP: clang with a fixed --target for ccgo's preprocessor probe.
set -euo pipefail
exec clang --target="${CCGO_CLANG_TARGET:?set CCGO_CLANG_TARGET}" "$@"

#!/usr/bin/env bash
# Thin alias: full multi-arch regeneration lives in regen-silkcc-all.sh.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/regen-silkcc-all.sh"

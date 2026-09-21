#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BIRTHS="${1:-10000000}"
SEED="${2:-1}"
OUT="${3:-out/cambrian01-seed-${SEED}-$(date +%Y%m%d-%H%M%S)-$$}"
if [[ -e "$OUT" ]]; then echo "Output already exists: $OUT" >&2; exit 1; fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/driftvm --births "$BIRTHS" --population 256 --seed "$SEED" --report-every 100000 --out "$OUT"
cat "$OUT/summary.txt"

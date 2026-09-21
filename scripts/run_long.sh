#!/usr/bin/env bash
set -euo pipefail

BIRTHS="${1:-10000000}"
SEED="${2:-1}"
OUT="${3:-out/cambrian0-seed-${SEED}}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo "Running DriftVM: births=${BIRTHS} seed=${SEED} out=${OUT}"
./build/driftvm \
  --births "${BIRTHS}" \
  --population 256 \
  --seed "${SEED}" \
  --report-every 100000 \
  --out "${OUT}"

echo
cat "${OUT}/summary.txt"

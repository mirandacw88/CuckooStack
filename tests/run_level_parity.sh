#!/usr/bin/env bash
# Verifies the C++ level generator reproduces the web build's daily course exactly.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${TMPDIR:-/tmp}/cs_level_parity"
clang++ -std=c++20 -O1 -Ithird_party/glm tests/level_parity.cpp src/core/Level.cpp src/core/Difficulty.cpp -o "$BIN"
fail=0
for d in 2026-10-06 2026-01-01 2025-12-31 2027-02-28 2030-07-15; do
  if diff <(node tests/level_parity.mjs "$d" 5000) <("$BIN" "$d" 5000) >/dev/null; then echo "ok   $d ($(node tests/level_parity.mjs "$d" 5000 | wc -l | tr -d ' ') lines)"; else echo "FAIL $d"; fail=1; fi
done
exit $fail

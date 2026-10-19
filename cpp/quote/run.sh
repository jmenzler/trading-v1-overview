#!/usr/bin/env bash
# Build level N with sanitizers and run it against its golden vectors: ./run.sh 1
set -euo pipefail
cd "$(dirname "$0")"
level="${1:?usage: ./run.sh <level>}"
mkdir -p build
# -ffp-contract=off: no fused multiply-add, Numba doesn't fuse either (see notes/level1-swap-step.md).
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Wall -Wextra -Wpedantic -Wconversion -ffp-contract=off \
  -Iinclude src/*.cpp "tests/level${level}_test.cpp" -o "build/level${level}"
"./build/level${level}"

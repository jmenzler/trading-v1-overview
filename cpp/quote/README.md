# C++ quote kernel

C++ port of trading-v1's Numba CLMM/DLMM quote kernels, verified bit-exact against the Numba reference and benchmarked against it.

## Layout

```
include/quote/   public headers (kernel contracts)
src/             kernel implementations
tests/           level tests + golden-vector loader
vectors/         golden vectors exported from trading-v1 (hex-float CSV)
notes/           math note per level
run.sh           ./run.sh <level>: build with ASan/UBSan, run against vectors
```

## Levels

| Level | Kernel | Status |
|---|---|---|
| 1 | CLMM swap step in one range | 995/995 bit-exact; parity with Numba ([bench](bench/RESULTS.md)) |
| 2 | CLMM quote with tick crossing | bit-exact on 50,688 recorded-state quotes |
| 3 | Orca adaptive-fee CLMM quote | bit-exact on 6,600 fuzzed quotes covering all four fee-decay branches |
| 4 | Meteora DLMM quote across bins | bit-exact on 6,600 fuzzed quotes; 128-bit fee math where Numba int64 overflows |
| 5 | nanobind binding, `QUOTE_BACKEND=cpp`, native size search | 12,672 searches bit-exact; search 58.6 → 34.7 µs |

## Design

TODO

## Correctness method

TODO

## Benchmarks

TODO

## Why the result came out this way

TODO

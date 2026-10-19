# Level 1 benchmark: compute_swap_step

Apple M3 Pro · Apple clang 21 · Numba 0.65 / llvmlite 0.47 · Python 3.14 · 2026-10-19

## Method

- **Inputs:** the 995 Level 1 golden vectors, same order on both sides.
- **Correctness first:** each side checks every output bit for bit against the vectors and refuses to benchmark on any mismatch.
- **Kernel-only timing:** a compiled loop (C++, or `@njit` for Numba) calls the step over all 995 inputs 20 times per repetition and stores the outputs to arrays.
- **Repetitions:** 500 warm-up, then 5,000 timed. Each figure is ns per call, as the median and p99 over repetitions. Each row is the median of 3 process runs.
- **C++ flags:** `-O3 -mcpu=native -ffp-contract=off`. Numba also compiles for the host CPU without fastmath.

## Numbers

| Implementation | median ns/call | p99 ns/call |
|---|---|---|
| Numba (`@njit` loop) | 1.61 | 2.07 |
| C++, kernel in its own translation unit | 2.37 | 2.87 |
| C++, `-flto` (kernel inlined into loop) | 1.54 | 1.97 |
| Numba dispatcher called from Python, per call | 131 | 149 |

## Reading

- **C++ matches Numba; it doesn't beat it.** Both are about 1.6 ns, which is ten-odd floating-point ops including three divides. Numba and clang both lower the code through LLVM to essentially the same machine code, so a hand port gains nothing at this level.
- **Inlining is the only visible lever:** without LTO the C++ call costs about 0.8 ns more, because the call can't be inlined into the loop.
- **The real cost is the boundary:** calling the kernel from Python costs about 130 ns, roughly 80× the kernel itself. Any speedup has to come from moving the loop (the tick-crossing quote, then the size search) into native code with one boundary crossing, not from a faster step.

## Reproduce

```bash
# C++ (from cpp/quote)
clang++ -std=c++20 -O3 -mcpu=native -ffp-contract=off -flto -Iinclude src/clmm_step.cpp bench/bench_step.cpp -o build/bench_lto
./build/bench_lto
# Numba (from the private trading-v1 repo)
uv run python scripts/bench_swap_step_numba.py <path>/vectors/level1_swap_step.csv
```

---

# Full quote and size search, called from trading-v1

Same machine. Inputs: 60 distinct recorded TSLA CLMM pool states (median 420 ticks each).

- **Quote:** each state in both directions at 8 sizes from 1 to 10,000 UI, so 960 calls per repetition; 20 warm-up and 300 timed repetitions.
- **Search:** `fast_find_optimal_size` with edge 50 bps, min size 1 and cap 10,000, in both directions, so 120 searches per repetition; 100 timed repetitions.
- **Reporting:** median µs per call over repetitions, then the median of 3 process runs.
- **Correctness first:** all three configurations produce the same SHA-256 digest over their search results. The suite-level parity checks (50,688 quotes and 12,672 searches over all 1,056 recorded states) are bit-identical.

| Configuration | Quote call | Size search |
|---|---|---|
| Numba (production today): Python golden-section loop, Numba kernel | 3.83 µs | 58.6 µs |
| Python loop, C++ kernel (kernel swap only) | 3.98 µs | 60.6 µs |
| **C++ search: whole loop in one native call** | — | **34.7 µs (1.69× faster)** |

## Reading

- **The kernel ties.** Called from inside compiled code with no binding, one 1,000-UI quote takes 9.1 µs in both C++ and Numba. Both go through LLVM to near-identical machine code; I compared the assembly of the tick-scan loop.
- **Swapping only the kernel gains nothing.** The Python loop pays about 290 ns per Numba call and about 390 ns per nanobind call (four NumPy arrays are converted on each call), for 12 calls per search.
- **The win comes from moving the loop.** One native call per search removes the Python control flow and 11 of the 12 boundary crossings: 58.6 µs → 34.7 µs.
- **What's left is algorithmic.** `find_next_tick` scans the tick array linearly on every crossing, so a quote costs O(crossings × ticks). A cursor would make the remaining ~35 µs much smaller, with identical outputs, and Numba could take the same change.

## Pitfalls hit along the way

- **`nanobind_add_module` builds with `-Os` by default.** It made the C++ quote 1.7× slower than Numba (17 µs against 10 µs) until `NOMINSIZE` was set. A stale scikit-build directory kept the old flags even after the fix; only a clean rebuild showed it.
- **A pure function in a fixed-input loop gets hoisted.** With LTO, clang computed the quote once and reported 7 ns. Reading the input through a `volatile` fixed that; the real figure is 9.1 µs.
- **On arm64, clang fuses multiply-add by default.** That breaks bit-exactness against Numba, hence `-ffp-contract=off`, pinned by 20 FMA-sensitive vectors in Level 1.

---

# Full trading-v1 suite on the C++ backend

`QUOTE_BACKEND=cpp uv run pytest` fails exactly the same 73 tests as the Numba baseline (73 already fail on `develop`) and passes 8,253: the 8,248 that pass on Numba plus 5 new parity tests. No test fails on only one backend.

## A latent int64 overflow in the Numba DLMM fee

Numba computes Meteora's variable fee, `variable_fee_control * (volatility_accumulator * bin_step)^2`, in int64; on chain it is u128. Above 2^63, LLVM treats signed overflow as undefined, so the Numba result isn't even a predictable wraparound. In one fuzz case it produced a negative fee rate in some bins. The C++ port computes the fee in 128 bits. It matches Numba bit for bit wherever Numba is defined (6,600 fuzzed quotes), and matches an exact Python reference with unbounded ints in the overflow regime, where Numba differs in 772 of 6,600 quotes.

Every DLMM pool trading-v1 tracks is capped below the threshold by its `max_volatility_accumulator`: the worst case is 0.73 × int64 max, on bin_step-200 pools. So production quotes aren't affected today.

## Parity on recorded pool states from TimescaleDB

I sampled 2,133 snapshots, evenly spaced from January to October 2026, across all 149 pools that trading-v1 records tick or bin arrays for: 52 Raydium CLMM, 19 Byreal, 3 Fusion, 39 Orca Whirlpool and 36 Meteora DLMM.

- **Real from the snapshot:** ticks and bins, price, liquidity, active id.
- **Real from the on-chain pool account:** token decimals and DLMM static fee parameters.
- **Swept, because they aren't recorded:** the static CLMM fee rate (0.01%, 0.25% and 1%), and the volatility state, with one setting per fee-decay branch.

| Kernel | Quotes | Non-zero output | Bit mismatches |
|---|---|---|---|
| CLMM static fee | 106,062 | 106,062 | 0 |
| CLMM adaptive fee (Orca pools) | 49,368 | 49,368 | 0 |
| DLMM | 44,528 | 41,216 | 0 |

## Pure Python vs Numba vs C++

"Pure Python" is the same kernel code with Numba's JIT turned off (`NUMBA_DISABLE_JIT=1`), so the CPython interpreter runs it. Inputs and method are as above, except pure Python uses 20 timed repetitions instead of 300 and 100. Figures are the median of 3 process runs in µs, with p99 in parentheses. All three produce the same search-result digest.

| Implementation | CLMM quote call | Size search | Search speed-up vs pure Python |
|---|---|---|---|
| Pure Python (interpreted) | 585 (686) | 5,236 (5,468) | 1× |
| Numba (production) | 4.21 (5.06) | 63.6 (68.8) | 82× |
| Python search loop + C++ kernel | 4.35 (4.69) | 62.7 (75.0) | 84× |
| **C++ search (one native call)** | — | **36.8 (39.8)** | **142×** |

Numba is where almost all of the gain over Python comes from (×139 on a quote, ×82 on a search). C++ adds ×1.73 on the search, and only by moving the whole loop out of Python.

## DLMM: pure Python vs Numba vs C++

Inputs are 170 real Meteora DLMM states across 36 pools: bins and active id from TimescaleDB snapshots, static and volatility parameters from the on-chain LbPair account. That gives 2,720 quote calls per repetition (8 sizes from 0.1 to 10,000 UI, both directions) and 1,020 searches (3 edge and cap settings, both directions). The replay clock is pinned to 200 s after the latest pool update. Figures are the median of 3 process runs in µs, with p99 in parentheses.

| Implementation | DLMM quote call | Size search | Search speed-up vs pure Python |
|---|---|---|---|
| Pure Python (interpreted) | 87.0 (91.8) | 1,044 (1,068) | 1× |
| Numba (production) | 1.28 (1.41) | 34.0 (38.6) | 31× |
| Python search loop + C++ kernel | 2.04 (2.45) | 41.7 (52.3) | 25× |
| **C++ search (one native call)** | — | **20.4 (22.7)** | **51×** |

- **Numba and C++ give bit-identical search results** on all 1,020 searches: 617 empty, 332 unclipped, 71 clipped. A fuzz test in trading-v1 covers 600 more.
- **Pure Python does not match bit for bit.** CPython computes `float ** int` with libm's `pow`, while Numba (and the C++ port) use binary exponentiation, so `(1 + bin_step/1e4) ** bin_id` differs in the last bits.
- **Calling the C++ kernel from Python is slower than Numba for DLMM.** A DLMM quote is cheap (a binary search plus a few bins), so the per-call cost dominates, and nanobind converting 21 scalars and 3 arrays costs about 0.75 µs more than Numba's dispatcher. Only the native search, with one crossing instead of about 12, comes out ahead: 1.67× faster than Numba. Passing the fee parameters as one pre-built object instead of 11 scalars would cut that per-call overhead.

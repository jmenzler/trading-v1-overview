# Level 1 — one CLMM swap step

**Your job:** implement `quote::compute_swap_step` in `src/clmm_step.cpp` until `./run.sh 1` prints `995/995 passed`.

## The model

Inside one tick range a concentrated-liquidity pool has constant liquidity `L`. Write `s = sqrt(price)`, with price = token B per token A in raw units. The range then behaves like a constant-product pool with *virtual* reserves:

```
x = L / s        (token A)
y = L * s        (token B)
```

When `s` moves from `s0` to `s1`, the reserves change by:

```
Δx = L * (1/s1 - 1/s0)
Δy = L * (s1 - s0)
```

Everything in this level follows from those two lines (Uniswap v3 whitepaper §6.2, eq. 6.13–6.16).

## The two directions

| | a_to_b (sell A) | b_to_a (sell B) |
|---|---|---|
| price | goes **down**, `target <= current` | goes **up**, `target >= current` |
| input token | A, so input = `Δx` | B, so input = `Δy` |
| output token | B, so output = `L * (s0 - s1)` | A, so output = `L * (1/s0 - 1/s1)` |

## What a step does

1. Work out `max_amount_in`, the input needed to push `s` all the way from current to target.
2. If `amount_remaining >= max_amount_in`, the step consumes exactly that and lands on the target (`reached_target = true`).
3. Otherwise it consumes all of `amount_remaining` and stops inside the range. In that case you need the new `s`:
   - **b_to_a:** invert `Δy = L * (s1 - s0)` for `s1`. One line.
   - **a_to_b:** invert `Δx = L * (1/s1 - 1/s0)` for `s1`. Do the algebra on paper. The reference writes the result as a single fraction with `L * s0` in the numerator, so put yours in that form too (see "Bit-exact" below).
4. Compute the output from the old and new `s`.

**Guards the reference has.** Your port must have the same ones, because the edge-case vectors test each of them:
- `L == 0` returns `{0, 0, current, false}`.
- a_to_b: `max_amount_in` is `0` unless both `target > 0` and `current > 0`.
- b_to_a: the output is `0` unless both `current > 0` and the new `s > 0`.
- `max_amount_in`, and both returned amounts, go through `abs`.

## Bit-exact: why it's possible, and what breaks it

IEEE 754 makes `+ - * /` and `sqrt` *correctly rounded*: each result is the exact answer rounded to the nearest double. Numba compiles with strict IEEE semantics (no `fastmath`). So two programs that perform **the same operations in the same order** give identical bits, whatever the language. The test compares raw bits (`ulp_distance == 0`). That is stricter than `==`, because it also tells `+0.0` and `-0.0` apart.

Things that break it:
- **Reordering.** `(L * s) / (L + a * s)` and `L / (L/s + a)` are equal in algebra but give different bits. Follow the reference's grouping.
- **FMA contraction.** Clang may fuse `x + y * z` into one fused multiply-add, which rounds once instead of twice. `run.sh` passes `-ffp-contract=off` to stop that. The 20 `edge_fma_sensitive_*` vectors were chosen so that they fail if fusion happens. **Exercise:** once you pass, delete the flag, rerun, and explain which line of your code those cases hit.
- **`-ffast-math`.** It allows reordering everywhere. Never use it here.

## How this differs from on-chain

The on-chain program (Raydium CLMM `swap_math.rs`, Orca `swap_manager.rs`) does this in integers. `s` is a u128 Q64.64 fixed-point number, and some products go through 256-bit intermediates. It rounds `amount_in` **up** and `amount_out` **down**, so rounding always favours the pool. trading-v1 uses float64 instead. Its quotes drift by less than 1e-9 relative from an exact reference and are checked against `simulateTransaction` in an external test. This port matches the float kernel, not the chain, because it has to be a drop-in replacement for Numba.

## Where the vectors come from

`vectors/level1_swap_step.csv` (995 rows). Expected outputs come from trading-v1's Numba `_compute_swap_step_f64`. The exporter also checks that the JIT and pure-Python versions agree on every row.
- **960 `real_*`:** 60 distinct pool states recorded from the TSLA fixture of 2026-07-06. Each runs in both directions up to the next initialized tick, at 8 fractions of `max_amount_in`: 1e-12, 1e-6, 0.01, 0.5, 0.999999, exactly 1, 1.000001 and 10.
- **15 `edge_*`:** zero liquidity, target equal to current, zero amount, denormal amount, `1e300` amount, zero target, min/max sqrt-price targets, and liquidity at the u128 maximum.
- **20 `edge_fma_sensitive_*`:** described above.

Doubles are stored as hex floats (`0x1.ff12b3537f3f1p+0`), which round-trip exactly.

## C++ you'll touch

Returning a struct with brace init (`return {a, b, c, d};`), `std::fabs` from `<cmath>`, the ternary operator, and `const` locals. Delete the `(void)` lines once you use the parameters.

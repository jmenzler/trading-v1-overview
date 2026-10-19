#pragma once

namespace quote {

struct SwapStep {
    double amount_in;        // input consumed by this step, raw units, >= 0
    double amount_out;       // output produced by this step, raw units, >= 0
    double sqrt_price_next;  // sqrt(price) after the step, price = token_b / token_a in raw units
    bool reached_target;     // true iff the whole range up to sqrt_price_target was consumed
};

// One CLMM swap step inside a single liquidity range (constant liquidity, no tick crossed).
//
// sqrt_price_current, sqrt_price_target: sqrt(price) as float, i.e. sqrt_price_x64 / 2^64.
//   a_to_b moves price down (target <= current); b_to_a moves price up (target >= current).
// liquidity: active liquidity L of the range, u128 on chain, here as double.
// amount_remaining: input still to be swapped, raw units, fee already deducted.
//
// Must reproduce trading-v1's Numba `_compute_swap_step_f64` bit for bit (see notes/level1-swap-step.md).
SwapStep compute_swap_step(double sqrt_price_current, double sqrt_price_target, double liquidity,
                           double amount_remaining, bool a_to_b);

}  // namespace quote

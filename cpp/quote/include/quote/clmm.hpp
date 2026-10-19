#pragma once

#include <cstdint>
#include <span>

namespace quote {

// Flattened tick data, sorted by tick index ascending. All four spans have the same length.
struct TickArrays {
    std::span<const std::int64_t> indices;
    std::span<const double> liquidity_net;  // signed liquidity change when crossing upward
    std::span<const double> initialized;    // 1.0 / 0.0
    std::span<const double> sqrt_prices;    // sqrt_price_x64 / 2^64 at each tick
};

struct ClmmQuote {
    double output_ui;         // output in UI units (decimal-adjusted)
    double sqrt_price_final;  // sqrt(price) after the swap
};

// Orca Whirlpool adaptive-fee state, as read from the pool account. Integer units as on chain.
struct AdaptiveFeeState {
    std::int64_t static_fee_rate_units;
    std::int64_t filter_period;
    std::int64_t decay_period;
    std::int64_t reduction_factor;
    std::int64_t control_factor;
    std::int64_t max_volatility_accumulator;
    std::int64_t tick_group_size;
    std::int64_t last_ref_ts;
    std::int64_t last_major_ts;
    std::int64_t vol_reference;
    std::int64_t tgi_reference;
    std::int64_t vol_accumulator;
};

inline constexpr std::int64_t kNoTickFound = -999999;

// Array position of the next initialized tick in swap direction, or kNoTickFound.
// a_to_b: greatest index with tick <= current_tick; b_to_a: smallest index with tick > current_tick.
std::int64_t find_next_tick(std::int64_t current_tick, const TickArrays& ticks, bool a_to_b);

// Static-fee CLMM quote with tick crossing (Raydium CLMM, Byreal, Fusion, static Orca).
// amount_in in UI units; fee_rate as a fraction, deducted from the input up front.
// Mirrors trading-v1's Numba `_clmm_quote_f64` bit for bit.
ClmmQuote clmm_quote(double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
                     const TickArrays& ticks, bool a_to_b, double fee_rate, int token_a_decimals,
                     int token_b_decimals);

// Adaptive-fee CLMM quote (Orca Whirlpool V3). Mirrors Numba `_clmm_quote_adaptive_f64`.
ClmmQuote clmm_quote_adaptive(double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
                              const TickArrays& ticks, bool a_to_b, int token_a_decimals, int token_b_decimals,
                              const AdaptiveFeeState& fee, std::int64_t now_ts);

}  // namespace quote

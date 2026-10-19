#pragma once

#include <cstdint>
#include <span>

namespace quote {

// Flattened bins, sorted by bin id ascending. All spans have the same length.
struct BinArrays {
    std::span<const std::int64_t> ids;
    std::span<const double> amounts_x;  // UI-denominated reserve of token X
    std::span<const double> amounts_y;  // UI-denominated reserve of token Y
};

// Meteora DLMM dynamic-fee parameters and volatility state, as read from the pool account.
struct DlmmFeeState {
    std::int64_t base_factor;
    std::int64_t base_fee_power_factor;
    std::int64_t filter_period;
    std::int64_t decay_period;
    std::int64_t reduction_factor;
    std::int64_t variable_fee_control;
    std::int64_t max_volatility_accumulator;
    std::int64_t volatility_accumulator;
    std::int64_t volatility_reference;
    std::int64_t index_reference;
    std::int64_t last_update_timestamp;
};

struct DlmmQuote {
    double output_ui;
    std::int64_t last_bin_id;
    double fee_ui;
};

// DLMM bin-traversal quote with dynamic fee. Mirrors trading-v1's Numba `_dlmm_quote_core_f64`.
DlmmQuote dlmm_quote(double amount_in, std::int64_t active_id, std::int64_t bin_step, const BinArrays& bins,
                     bool swap_for_y, int token_a_decimals, int token_b_decimals, const DlmmFeeState& fee,
                     std::int64_t current_timestamp);

}  // namespace quote

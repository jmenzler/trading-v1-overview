#include "quote/dlmm.hpp"

#include <cmath>

#include "quote/py_semantics.hpp"

namespace quote {

namespace {

__extension__ using u128 = unsigned __int128;

constexpr int kMaxBinCrossings = 200;
constexpr std::int64_t kBpsDenominator = 10'000;
constexpr std::int64_t kVolatilityStep = 10'000;
constexpr std::int64_t kVariableFeeDenominator = 100'000'000'000;
constexpr std::int64_t kMaxFeeRate = 100'000'000;  // 10% in 1e-9 units
constexpr double kFeePrecision = 1e9;

// Position of bin_id in the sorted id array, or -1 when that bin isn't loaded.
std::int64_t find_bin(const BinArrays& bins, std::int64_t bin_id) {
    std::size_t lo = 0;
    std::size_t hi = bins.ids.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) >> 1U;
        if (bins.ids[mid] < bin_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= bins.ids.size() || bins.ids[lo] != bin_id) return -1;
    return static_cast<std::int64_t>(lo);
}

// Base + variable fee for a swap in bin current_bin_id, in 1e-9 units; ceil-rounded like the program.
std::int64_t total_fee_rate(std::int64_t base_fee, std::int64_t bin_step, std::int64_t index_ref,
                            std::int64_t vol_ref, std::int64_t current_bin_id, const DlmmFeeState& fee) {
    std::int64_t delta_id = index_ref - current_bin_id;
    if (delta_id < 0) delta_id = -delta_id;
    std::int64_t va = vol_ref + pysem::wrapping_mul(delta_id, kVolatilityStep);
    if (va > fee.max_volatility_accumulator) va = fee.max_volatility_accumulator;
    std::int64_t variable_fee = 0;
    if (fee.variable_fee_control > 0 && va > 0) {
        // u128 as on chain: vfc * (va * bin_step)^2 can exceed i64, where Numba's int64 result is undefined.
        const u128 vxb = static_cast<u128>(va) * static_cast<u128>(bin_step);
        const u128 v_fee_num = static_cast<u128>(fee.variable_fee_control) * vxb * vxb;
        const u128 ceil_fee = (v_fee_num + (kVariableFeeDenominator - 1)) / kVariableFeeDenominator;
        variable_fee = ceil_fee > static_cast<u128>(kMaxFeeRate) ? kMaxFeeRate : static_cast<std::int64_t>(ceil_fee);
    }
    std::int64_t total = base_fee + variable_fee;
    if (total > kMaxFeeRate) total = kMaxFeeRate;
    return total;
}

}  // namespace

DlmmQuote dlmm_quote(double amount_in, std::int64_t active_id, std::int64_t bin_step, const BinArrays& bins,
                     bool swap_for_y, int token_a_decimals, int token_b_decimals, const DlmmFeeState& fee,
                     std::int64_t current_timestamp) {
    // Decay the volatility reference to current_timestamp, as the program does at the start of a swap.
    const std::int64_t elapsed = current_timestamp - fee.last_update_timestamp;
    std::int64_t index_ref = fee.index_reference;
    std::int64_t vol_ref = fee.volatility_reference;
    if (elapsed >= fee.filter_period) {
        index_ref = active_id;
        vol_ref = elapsed < fee.decay_period
                      ? pysem::floor_div(pysem::wrapping_mul(fee.volatility_accumulator, fee.reduction_factor),
                                         kBpsDenominator)
                      : 0;
    }

    std::int64_t pow10 = 1;
    for (std::int64_t i = 0; i < fee.base_fee_power_factor; ++i) pow10 = pysem::wrapping_mul(pow10, 10);
    const std::int64_t base_fee =
        pysem::wrapping_mul(pysem::wrapping_mul(pysem::wrapping_mul(fee.base_factor, bin_step), 10), pow10);

    double total_output = 0.0;
    double total_fee_ui = 0.0;
    double remaining = amount_in;
    std::int64_t current_bin_id = active_id;

    const double bin_step_rate = 1.0 + static_cast<double>(bin_step) / 10'000.0;
    const double decimal_adj = pysem::int_power(10.0, token_a_decimals - token_b_decimals);
    const double out_scale = pysem::int_power(10.0, swap_for_y ? token_b_decimals : token_a_decimals);

    for (int crossing = 0; crossing < kMaxBinCrossings; ++crossing) {
        if (remaining <= 0.0) break;

        const std::int64_t found = find_bin(bins, current_bin_id);
        if (found < 0) break;
        const auto bin_idx = static_cast<std::size_t>(found);

        const double fee_rate =
            static_cast<double>(total_fee_rate(base_fee, bin_step, index_ref, vol_ref, current_bin_id, fee)) /
            kFeePrecision;
        const double one_minus_fee = 1.0 - fee_rate;
        if (one_minus_fee <= 0.0) break;

        const double bin_price = pysem::int_power(bin_step_rate, current_bin_id) * decimal_adj;

        double to_swap;
        double output;
        if (swap_for_y) {
            const double available_y = bins.amounts_y[bin_idx];
            if (available_y <= 0.0) {
                --current_bin_id;
                continue;
            }
            const double max_x_net = bin_price > 0.0 ? available_y / bin_price : 0.0;
            const double max_x_gross = max_x_net / one_minus_fee;
            to_swap = remaining < max_x_gross ? remaining : max_x_gross;
            output = (to_swap * one_minus_fee) * bin_price;
        } else {
            const double available_x = bins.amounts_x[bin_idx];
            if (available_x <= 0.0) {
                ++current_bin_id;
                continue;
            }
            const double max_y_net = available_x * bin_price;
            const double max_y_gross = max_y_net / one_minus_fee;
            to_swap = remaining < max_y_gross ? remaining : max_y_gross;
            output = bin_price > 0.0 ? (to_swap * one_minus_fee) / bin_price : 0.0;
        }
        // Output is floored to whole raw units, as the program pays out integers.
        output = std::floor(output * out_scale) / out_scale;

        total_output += output;
        total_fee_ui += to_swap * fee_rate;
        remaining -= to_swap;

        if (remaining > 0.0) current_bin_id += swap_for_y ? -1 : 1;
    }

    return {total_output, current_bin_id, total_fee_ui};
}

}  // namespace quote

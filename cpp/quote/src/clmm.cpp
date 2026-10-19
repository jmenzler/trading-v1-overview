#include "quote/clmm.hpp"

#include <cmath>

#include "quote/clmm_step.hpp"
#include "quote/py_semantics.hpp"

namespace quote {

namespace {

constexpr double kMinSqrtPrice = 2.3283064e-10;
constexpr double kMaxSqrtPrice = 4.2949672e09;
constexpr int kMaxTickCrossings = 100;

}  // namespace

std::int64_t find_next_tick(std::int64_t current_tick, const TickArrays& ticks, bool a_to_b) {
    const auto n = static_cast<std::int64_t>(ticks.indices.size());
    if (a_to_b) {
        for (std::int64_t i = n - 1; i >= 0; --i) {
            const auto k = static_cast<std::size_t>(i);
            if (ticks.indices[k] <= current_tick && ticks.initialized[k] == 1.0) return i;
        }
    } else {
        for (std::int64_t i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            if (ticks.indices[k] > current_tick && ticks.initialized[k] == 1.0) return i;
        }
    }
    return kNoTickFound;
}

ClmmQuote clmm_quote(double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
                     const TickArrays& ticks, bool a_to_b, double fee_rate, int token_a_decimals,
                     int token_b_decimals) {
    double amount_remaining = amount_in * pysem::int_power(10.0, a_to_b ? token_a_decimals : token_b_decimals);
    amount_remaining *= 1.0 - fee_rate;

    double current_liquidity = liquidity;
    std::int64_t current_tick = tick_current;
    double total_output = 0.0;

    for (int crossing = 0; crossing < kMaxTickCrossings; ++crossing) {
        if (amount_remaining <= 0.0 || current_liquidity <= 0.0) break;

        const std::int64_t next_idx = find_next_tick(current_tick, ticks, a_to_b);
        const bool found = next_idx != kNoTickFound;
        const auto k = static_cast<std::size_t>(found ? next_idx : 0);
        const double target_sqrt_price =
            found ? ticks.sqrt_prices[k] : (a_to_b ? kMinSqrtPrice : kMaxSqrtPrice);

        const SwapStep step =
            compute_swap_step(sqrt_price, target_sqrt_price, current_liquidity, amount_remaining, a_to_b);
        total_output += step.amount_out;
        amount_remaining -= step.amount_in;
        sqrt_price = step.sqrt_price_next;

        if (!(step.reached_target && found)) break;
        if (a_to_b) {
            current_liquidity -= ticks.liquidity_net[k];
            current_tick = ticks.indices[k] - 1;
        } else {
            current_liquidity += ticks.liquidity_net[k];
            current_tick = ticks.indices[k];
        }
        if (current_liquidity < 0.0) current_liquidity = 0.0;
    }

    const double output_ui = total_output / pysem::int_power(10.0, a_to_b ? token_b_decimals : token_a_decimals);
    return {output_ui, sqrt_price};
}

namespace {

constexpr std::int64_t kRefMaxAgeSeconds = 3600;
constexpr std::int64_t kBpsDenominator = 10'000;
constexpr std::int64_t kVolatilityStep = 10'000;
constexpr std::int64_t kFeeControlDenominator = 10'000'000'000'000;  // control denom * volatility scale^2
constexpr std::int64_t kMaxFeeUnits = 100'000;
constexpr double kFeeUnitsPerOne = 1'000'000.0;
constexpr int kMaxOuterSteps = 200;
constexpr int kMaxInnerSteps = 5000;

// Tick group the price is in; at an exact group boundary an a_to_b swap is already in the group below.
std::int64_t tick_group_index(std::int64_t tick, std::int64_t tick_group_size, bool a_to_b) {
    std::int64_t tgi = pysem::floor_div(tick, tick_group_size);
    if (a_to_b && tick == tgi * tick_group_size) --tgi;
    return tgi;
}

// Adaptive fee in units of 1e-6, ceil-rounded like the on-chain program, split into high/low halves to stay in i64.
std::int64_t adaptive_fee_units(std::int64_t tgi_reference, std::int64_t current_tgi, std::int64_t vol_reference,
                                const AdaptiveFeeState& fee) {
    std::int64_t delta = tgi_reference - current_tgi;
    if (delta < 0) delta = -delta;
    std::int64_t raw_va = vol_reference + pysem::wrapping_mul(delta, kVolatilityStep);
    if (raw_va > fee.max_volatility_accumulator) raw_va = fee.max_volatility_accumulator;
    const std::int64_t crossed = pysem::wrapping_mul(raw_va, fee.tick_group_size);
    const std::int64_t csq = pysem::wrapping_mul(crossed, crossed);
    const std::int64_t xh = pysem::floor_div(csq, kFeeControlDenominator);
    const std::int64_t xl = csq - xh * kFeeControlDenominator;
    const std::int64_t cx_lo = pysem::wrapping_mul(fee.control_factor, xl);
    std::int64_t q_lo = pysem::floor_div(cx_lo, kFeeControlDenominator);
    if (cx_lo - q_lo * kFeeControlDenominator > 0) ++q_lo;
    std::int64_t fee_units = pysem::wrapping_mul(fee.control_factor, xh) + q_lo;
    if (fee_units > kMaxFeeUnits) fee_units = kMaxFeeUnits;
    std::int64_t total_units = fee.static_fee_rate_units + fee_units;
    if (total_units > kMaxFeeUnits) total_units = kMaxFeeUnits;
    return total_units;
}

}  // namespace

ClmmQuote clmm_quote_adaptive(double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
                              const TickArrays& ticks, bool a_to_b, int token_a_decimals, int token_b_decimals,
                              const AdaptiveFeeState& fee, std::int64_t now_ts) {
    double amount_remaining = amount_in * pysem::int_power(10.0, a_to_b ? token_a_decimals : token_b_decimals);
    const std::int64_t group = fee.tick_group_size;
    std::int64_t current_tgi = tick_group_index(tick_current, group, a_to_b);

    // Decay the volatility reference to now_ts, as the program does at the start of a swap.
    std::int64_t tgi_reference = fee.tgi_reference;
    std::int64_t vol_reference = fee.vol_reference;
    const std::int64_t max_ts = fee.last_ref_ts > fee.last_major_ts ? fee.last_ref_ts : fee.last_major_ts;
    const std::int64_t ref_age = now_ts - fee.last_ref_ts;
    const std::int64_t elapsed = now_ts - max_ts;
    if (ref_age > kRefMaxAgeSeconds) {
        tgi_reference = pysem::floor_div(tick_current, group);
        vol_reference = 0;
    } else if (elapsed < fee.filter_period) {
    } else if (elapsed < fee.decay_period) {
        tgi_reference = pysem::floor_div(tick_current, group);
        vol_reference = pysem::floor_div(pysem::wrapping_mul(fee.vol_accumulator, fee.reduction_factor), kBpsDenominator);
    } else {
        tgi_reference = pysem::floor_div(tick_current, group);
        vol_reference = 0;
    }

    double current_liquidity = liquidity;
    double total_output = 0.0;

    for (int outer = 0; outer < kMaxOuterSteps; ++outer) {
        if (amount_remaining <= 0.0 || current_liquidity <= 0.0) break;

        const std::int64_t next_idx = find_next_tick(tick_current, ticks, a_to_b);
        const bool found = next_idx != kNoTickFound;
        const auto k = static_cast<std::size_t>(found ? next_idx : 0);
        const std::int64_t next_tick_val = found ? ticks.indices[k] : kNoTickFound;
        const double outer_target_sqrt = found ? ticks.sqrt_prices[k] : (a_to_b ? kMinSqrtPrice : kMaxSqrtPrice);

        bool crossed_initialized_tick = false;

        // Inner steps stop at each tick-group boundary, where the fee is recomputed.
        for (int inner = 0; inner < kMaxInnerSteps; ++inner) {
            if (amount_remaining <= 0.0) break;

            const double fee_rate =
                static_cast<double>(adaptive_fee_units(tgi_reference, current_tgi, vol_reference, fee)) / kFeeUnitsPerOne;

            const std::int64_t boundary_tick = a_to_b ? current_tgi * group : (current_tgi + 1) * group;
            const double boundary_sqrt = std::pow(1.0001, static_cast<double>(boundary_tick) * 0.5);

            double bounded_target;
            if (a_to_b) {
                bounded_target = boundary_sqrt > outer_target_sqrt ? boundary_sqrt : outer_target_sqrt;
            } else {
                bounded_target = boundary_sqrt < outer_target_sqrt ? boundary_sqrt : outer_target_sqrt;
            }
            const bool hit_outer = bounded_target == outer_target_sqrt;

            const double one_minus_fee = 1.0 - fee_rate;
            const double net_remaining = amount_remaining * one_minus_fee;
            const SwapStep step =
                compute_swap_step(sqrt_price, bounded_target, current_liquidity, net_remaining, a_to_b);

            double gross_consumed = amount_remaining;
            if (step.reached_target && one_minus_fee > 0.0) gross_consumed = step.amount_in / one_minus_fee;

            total_output += step.amount_out;
            amount_remaining -= gross_consumed;
            sqrt_price = step.sqrt_price_next;

            if (!step.reached_target) {
                amount_remaining = 0.0;
                break;
            }
            if (hit_outer && found) {
                if (a_to_b) {
                    current_liquidity -= ticks.liquidity_net[k];
                    tick_current = next_tick_val - 1;
                } else {
                    current_liquidity += ticks.liquidity_net[k];
                    tick_current = next_tick_val;
                }
                if (current_liquidity < 0.0) current_liquidity = 0.0;
                current_tgi = tick_group_index(tick_current, group, a_to_b);
                crossed_initialized_tick = true;
                break;
            }
            if (a_to_b) {
                --current_tgi;
                tick_current = boundary_tick - 1;
            } else {
                ++current_tgi;
                tick_current = boundary_tick;
            }
        }

        if (amount_remaining <= 0.0) break;
        if (!found && !crossed_initialized_tick) break;
    }

    const double output_ui = total_output / pysem::int_power(10.0, a_to_b ? token_b_decimals : token_a_decimals);
    return {output_ui, sqrt_price};
}

}  // namespace quote

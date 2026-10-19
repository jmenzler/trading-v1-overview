#include "quote/search.hpp"

#include <cmath>

namespace quote {

double clmm_impact_bps(const ClmmPool& pool, double amount_in, bool a_to_b) {
    if (pool.sqrt_price == 0.0 || pool.liquidity == 0.0 || pool.ticks.indices.empty()) return kUnquotableBps;

    // std::pow, not Numba's int_power: trading-v1 computes this in CPython, whose float ** int calls libm pow.
    const double decimal_adj = std::pow(10.0, pool.token_a_decimals - pool.token_b_decimals);
    const double mid_price_before = (pool.sqrt_price * pool.sqrt_price) * decimal_adj;
    const double output =
        pool.adaptive_fee != nullptr
            ? clmm_quote_adaptive(amount_in, pool.sqrt_price, pool.liquidity, pool.tick_current, pool.ticks, a_to_b,
                                  pool.token_a_decimals, pool.token_b_decimals, *pool.adaptive_fee, pool.now_ts)
                  .output_ui
            : clmm_quote(amount_in, pool.sqrt_price, pool.liquidity, pool.tick_current, pool.ticks, a_to_b,
                         pool.fee_rate, pool.token_a_decimals, pool.token_b_decimals)
                  .output_ui;
    if (output <= 0.0 || amount_in <= 0.0 || mid_price_before <= 0.0) return kUnquotableBps;

    double slip_bps;
    if (a_to_b) {
        const double expected = amount_in * (1.0 - pool.fee_rate) * mid_price_before;
        if (expected <= 0.0) return kUnquotableBps;
        slip_bps = (expected - output) / expected * 10000.0;
    } else {
        const double effective = amount_in / output;
        const double fair = mid_price_before / (1.0 - pool.fee_rate);
        if (fair <= 0.0) return kUnquotableBps;
        slip_bps = (effective - fair) / fair * 10000.0;
    }
    if (slip_bps < 0.0) slip_bps = 0.0;
    return slip_bps + pool.fee_rate * 10000.0;
}

double dlmm_impact_bps(const DlmmPool& pool, double amount_in, bool a_to_b) {
    if (pool.bin_step == 0 || pool.bins.ids.empty() || pool.raw_mid_before <= 0.0) return kUnquotableBps;

    const double output = dlmm_quote(amount_in, pool.active_id, pool.bin_step, pool.bins, a_to_b,
                                     pool.token_a_decimals, pool.token_b_decimals, pool.fee, pool.now_ts)
                              .output_ui;
    if (output <= 0.0 || amount_in <= 0.0) return kUnquotableBps;

    double impact_bps;
    if (a_to_b) {
        const double amount_after_fee = amount_in * (1.0 - pool.fee_rate);
        const double expected = amount_after_fee * pool.raw_mid_before;
        if (expected <= 0.0) return kUnquotableBps;
        impact_bps = ((expected - output) / expected) * 10000.0;
    } else {
        const double effective = amount_in / output;
        const double fair = pool.raw_mid_before / (1.0 - pool.fee_rate);
        if (fair <= 0.0) return kUnquotableBps;
        impact_bps = ((effective - fair) / fair) * 10000.0;
    }
    if (impact_bps < 0.0) impact_bps = 0.0;
    return impact_bps + pool.fee_rate * 10000.0;
}

}  // namespace quote

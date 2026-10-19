#pragma once

#include <cmath>
#include <cstdint>

#include "quote/clmm.hpp"
#include "quote/dlmm.hpp"

namespace quote {

// CLMM pool (static or Orca adaptive fee), already converted to float the way trading-v1's `_clmm_quote_core` does it.
struct ClmmPool {
    double sqrt_price;  // sqrt_price_x64 / 2^64
    double liquidity;
    std::int64_t tick_current;
    TickArrays ticks;
    double fee_rate;  // fee_bps / 10000; also the impact formula's fee for adaptive pools
    int token_a_decimals;
    int token_b_decimals;
    const AdaptiveFeeState* adaptive_fee;  // null for static-fee pools
    std::int64_t now_ts;                   // adaptive fee decay clock; unused for static-fee pools
};

// DLMM pool plus the values trading-v1 computes in Python before quoting.
struct DlmmPool {
    std::int64_t active_id;
    std::int64_t bin_step;
    BinArrays bins;
    int token_a_decimals;
    int token_b_decimals;
    DlmmFeeState fee;
    double fee_rate;        // pool fee_bps / 10000, used by the impact formula
    double raw_mid_before;  // (1 + bin_step/1e4) ** active_id * 10 ** (dec_a - dec_b), via CPython's pow
    std::int64_t now_ts;
};

struct SizeSearch {
    double clipped_size;    // min(unclipped, alloc_cap), or 0 when trading at the cap is unprofitable
    double unclipped_size;  // profit-maximizing size over [min_size, pool_search_cap]
    double unclipped_net_edge_bps;
};

inline constexpr double kUnquotableBps = 1e9;
inline constexpr double kUnquotableThresholdBps = 1e8;

// Slippage + fee impact in bps for swapping amount_in (UI units); 1e9 when unquotable.
// Mirror trading-v1's `clmm_quote_impact_bps` / `dlmm_quote_impact_bps`.
double clmm_impact_bps(const ClmmPool& pool, double amount_in, bool a_to_b);
double dlmm_impact_bps(const DlmmPool& pool, double amount_in, bool a_to_b);

// Maximizes (edge_bps - impact_bps(size)) * size by log-space golden-section search; mirrors trading-v1's
// `fast_find_optimal_size`. Callers validate min_size > 0, pool_search_cap >= alloc_cap, min_size < pool_search_cap.
template <typename ImpactBps>
SizeSearch golden_section_size_search(const ImpactBps& impact_bps, double edge_bps, double min_size, double alloc_cap,
                                      double pool_search_cap, int iterations) {
    struct Probe {
        double profit;
        double net_edge;
    };
    constexpr double kNoProfit = -1e18;
    auto probe = [&](double size) -> Probe {
        if (size <= 0.0) return {kNoProfit, 0.0};
        const double impact = impact_bps(size);
        if (impact >= kUnquotableThresholdBps) return {kNoProfit, 0.0};
        const double net_edge = edge_bps - impact;
        return {net_edge * size, net_edge};
    };

    double best_size = 0.0;
    double best_profit = 0.0;
    double best_net_edge = 0.0;
    bool saw_profitable_below_cap = false;
    auto consider = [&](double size, Probe p) {
        if (p.profit > best_profit) {
            best_size = size;
            best_profit = p.profit;
            best_net_edge = p.net_edge;
        }
        if (size <= alloc_cap && p.profit > 0.0) saw_profitable_below_cap = true;
    };

    consider(min_size, probe(min_size));
    consider(pool_search_cap, probe(pool_search_cap));

    // u = log(size): u_lo < c < d < u_hi, one new quote per iteration.
    const double inv_phi = 2.0 / (1.0 + std::sqrt(5.0));
    double u_lo = std::log(min_size);
    double u_hi = std::log(pool_search_cap);
    double span = u_hi - u_lo;
    double c = u_hi - inv_phi * span;
    double d = u_lo + inv_phi * span;
    double size_c = std::exp(c);
    double size_d = std::exp(d);
    Probe pc = probe(size_c);
    Probe pd = probe(size_d);
    consider(size_c, pc);
    consider(size_d, pd);

    for (int i = 0; i < iterations; ++i) {
        if (pc.profit > pd.profit) {
            u_hi = d;
            d = c;
            pd = pc;
            span = u_hi - u_lo;
            c = u_hi - inv_phi * span;
            size_c = std::exp(c);
            pc = probe(size_c);
            consider(size_c, pc);
        } else {
            u_lo = c;
            c = d;
            pc = pd;
            span = u_hi - u_lo;
            d = u_lo + inv_phi * span;
            size_d = std::exp(d);
            pd = probe(size_d);
            consider(size_d, pd);
        }
    }

    const double converged = std::exp((u_lo + u_hi) / 2.0);
    consider(converged, probe(converged));

    if (best_profit <= 0.0) return {0.0, 0.0, 0.0};

    double clipped_size;
    if (best_size <= alloc_cap) {
        clipped_size = best_size;
    } else if (saw_profitable_below_cap) {
        clipped_size = alloc_cap;  // unimodal profit: f(alloc_cap) >= best below-cap profit > 0
    } else {
        const double impact_at_cap = impact_bps(alloc_cap);
        clipped_size = (impact_at_cap < kUnquotableThresholdBps && (edge_bps - impact_at_cap) > 0.0) ? alloc_cap : 0.0;
    }
    return {clipped_size, best_size, best_net_edge};
}

inline SizeSearch find_optimal_size(const ClmmPool& pool, bool a_to_b, double edge_bps, double min_size,
                                    double alloc_cap, double pool_search_cap, int iterations) {
    return golden_section_size_search([&](double size) { return clmm_impact_bps(pool, size, a_to_b); }, edge_bps,
                                      min_size, alloc_cap, pool_search_cap, iterations);
}

inline SizeSearch find_optimal_size(const DlmmPool& pool, bool a_to_b, double edge_bps, double min_size,
                                    double alloc_cap, double pool_search_cap, int iterations) {
    return golden_section_size_search([&](double size) { return dlmm_impact_bps(pool, size, a_to_b); }, edge_bps,
                                      min_size, alloc_cap, pool_search_cap, iterations);
}

}  // namespace quote

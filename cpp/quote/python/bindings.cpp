// Python module `quote_cpp`: drop-in replacements for trading-v1's Numba quote kernels.
// Argument order and tuple returns match the Numba functions exactly.

#include <cstdint>
#include <span>
#include <tuple>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/tuple.h>

#include "quote/clmm.hpp"
#include "quote/clmm_step.hpp"
#include "quote/dlmm.hpp"
#include "quote/search.hpp"

namespace nb = nanobind;

namespace {

template <typename T>
using Array1D = nb::ndarray<const T, nb::ndim<1>, nb::c_contig, nb::device::cpu>;

template <typename T>
std::span<const T> as_span(const Array1D<T>& a) {
    return {a.data(), a.shape(0)};
}

quote::TickArrays tick_arrays(const Array1D<std::int64_t>& indices, const Array1D<double>& liq_net,
                              const Array1D<double>& initialized, const Array1D<double>& sqrt_prices) {
    if (liq_net.shape(0) != indices.shape(0) || initialized.shape(0) != indices.shape(0) ||
        sqrt_prices.shape(0) != indices.shape(0)) {
        throw nb::value_error("tick arrays must have equal length");
    }
    return {as_span(indices), as_span(liq_net), as_span(initialized), as_span(sqrt_prices)};
}

std::tuple<double, double, double, bool> compute_swap_step(double sqrt_price_current, double sqrt_price_target,
                                                           double liquidity, double amount_remaining, bool a_to_b) {
    const auto s = quote::compute_swap_step(sqrt_price_current, sqrt_price_target, liquidity, amount_remaining, a_to_b);
    return {s.amount_in, s.amount_out, s.sqrt_price_next, s.reached_target};
}

std::tuple<double, double> clmm_quote(double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
                                      Array1D<std::int64_t> tick_indices, Array1D<double> tick_liq_net,
                                      Array1D<double> tick_initialized, Array1D<double> tick_sqrt_prices, bool a_to_b,
                                      double fee_rate, int token_a_decimals, int token_b_decimals) {
    const auto q = quote::clmm_quote(amount_in, sqrt_price, liquidity, tick_current,
                                     tick_arrays(tick_indices, tick_liq_net, tick_initialized, tick_sqrt_prices),
                                     a_to_b, fee_rate, token_a_decimals, token_b_decimals);
    return {q.output_ui, q.sqrt_price_final};
}

std::tuple<double, double> clmm_quote_adaptive(
    double amount_in, double sqrt_price, double liquidity, std::int64_t tick_current,
    Array1D<std::int64_t> tick_indices, Array1D<double> tick_liq_net, Array1D<double> tick_initialized,
    Array1D<double> tick_sqrt_prices, bool a_to_b, int token_a_decimals, int token_b_decimals,
    std::int64_t static_fee_rate_units, std::int64_t filter_period, std::int64_t decay_period,
    std::int64_t reduction_factor, std::int64_t control_factor, std::int64_t max_volatility_accumulator,
    std::int64_t tick_group_size, std::int64_t last_ref_ts, std::int64_t last_major_ts, std::int64_t vol_reference,
    std::int64_t tgi_reference, std::int64_t vol_accumulator, std::int64_t now_ts) {
    const quote::AdaptiveFeeState fee{static_fee_rate_units, filter_period,    decay_period,
                                      reduction_factor,      control_factor,   max_volatility_accumulator,
                                      tick_group_size,       last_ref_ts,      last_major_ts,
                                      vol_reference,         tgi_reference,    vol_accumulator};
    const auto q = quote::clmm_quote_adaptive(
        amount_in, sqrt_price, liquidity, tick_current,
        tick_arrays(tick_indices, tick_liq_net, tick_initialized, tick_sqrt_prices), a_to_b, token_a_decimals,
        token_b_decimals, fee, now_ts);
    return {q.output_ui, q.sqrt_price_final};
}

std::tuple<double, std::int64_t, double> dlmm_quote(
    double amount_in, std::int64_t active_id, std::int64_t bin_step, Array1D<std::int64_t> bin_ids,
    Array1D<double> amounts_x, Array1D<double> amounts_y, bool swap_for_y, int token_a_decimals,
    int token_b_decimals, std::int64_t base_factor, std::int64_t base_fee_power_factor, std::int64_t filter_period,
    std::int64_t decay_period, std::int64_t reduction_factor, std::int64_t variable_fee_control,
    std::int64_t max_volatility_accumulator, std::int64_t volatility_accumulator, std::int64_t volatility_reference,
    std::int64_t index_reference, std::int64_t last_update_timestamp, std::int64_t current_timestamp) {
    if (amounts_x.shape(0) != bin_ids.shape(0) || amounts_y.shape(0) != bin_ids.shape(0)) {
        throw nb::value_error("bin arrays must have equal length");
    }
    const quote::DlmmFeeState fee{base_factor,          base_fee_power_factor, filter_period,
                                  decay_period,         reduction_factor,      variable_fee_control,
                                  max_volatility_accumulator, volatility_accumulator, volatility_reference,
                                  index_reference,      last_update_timestamp};
    const auto q = quote::dlmm_quote(amount_in, active_id, bin_step,
                                     {as_span(bin_ids), as_span(amounts_x), as_span(amounts_y)}, swap_for_y,
                                     token_a_decimals, token_b_decimals, fee, current_timestamp);
    return {q.output_ui, q.last_bin_id, q.fee_ui};
}

quote::ClmmPool clmm_pool(double sqrt_price, double liquidity, std::int64_t tick_current,
                          const Array1D<std::int64_t>& tick_indices, const Array1D<double>& tick_liq_net,
                          const Array1D<double>& tick_initialized, const Array1D<double>& tick_sqrt_prices,
                          double fee_rate, int token_a_decimals, int token_b_decimals) {
    return {sqrt_price, liquidity, tick_current,
            tick_arrays(tick_indices, tick_liq_net, tick_initialized, tick_sqrt_prices),
            fee_rate, token_a_decimals, token_b_decimals, nullptr, 0};
}

double clmm_impact_bps(double sqrt_price, double liquidity, std::int64_t tick_current,
                       Array1D<std::int64_t> tick_indices, Array1D<double> tick_liq_net,
                       Array1D<double> tick_initialized, Array1D<double> tick_sqrt_prices, double fee_rate,
                       int token_a_decimals, int token_b_decimals, double amount_in, bool a_to_b) {
    const auto pool = clmm_pool(sqrt_price, liquidity, tick_current, tick_indices, tick_liq_net, tick_initialized,
                                tick_sqrt_prices, fee_rate, token_a_decimals, token_b_decimals);
    return quote::clmm_impact_bps(pool, amount_in, a_to_b);
}

std::tuple<double, double, double> clmm_find_optimal_size(
    double sqrt_price, double liquidity, std::int64_t tick_current, Array1D<std::int64_t> tick_indices,
    Array1D<double> tick_liq_net, Array1D<double> tick_initialized, Array1D<double> tick_sqrt_prices,
    double fee_rate, int token_a_decimals, int token_b_decimals, bool a_to_b, double edge_bps, double min_size,
    double alloc_cap, double pool_search_cap, int iterations) {
    const auto pool = clmm_pool(sqrt_price, liquidity, tick_current, tick_indices, tick_liq_net, tick_initialized,
                                tick_sqrt_prices, fee_rate, token_a_decimals, token_b_decimals);
    const auto r = quote::find_optimal_size(pool, a_to_b, edge_bps, min_size, alloc_cap, pool_search_cap, iterations);
    return {r.clipped_size, r.unclipped_size, r.unclipped_net_edge_bps};
}

std::tuple<double, double, double> clmm_adaptive_find_optimal_size(
    double sqrt_price, double liquidity, std::int64_t tick_current, Array1D<std::int64_t> tick_indices,
    Array1D<double> tick_liq_net, Array1D<double> tick_initialized, Array1D<double> tick_sqrt_prices,
    double fee_rate, int token_a_decimals, int token_b_decimals, std::int64_t static_fee_rate_units,
    std::int64_t filter_period, std::int64_t decay_period, std::int64_t reduction_factor, std::int64_t control_factor,
    std::int64_t max_volatility_accumulator, std::int64_t tick_group_size, std::int64_t last_ref_ts,
    std::int64_t last_major_ts, std::int64_t vol_reference, std::int64_t tgi_reference, std::int64_t vol_accumulator,
    std::int64_t now_ts, bool a_to_b, double edge_bps, double min_size, double alloc_cap, double pool_search_cap,
    int iterations) {
    const quote::AdaptiveFeeState fee{static_fee_rate_units, filter_period,    decay_period,
                                      reduction_factor,      control_factor,   max_volatility_accumulator,
                                      tick_group_size,       last_ref_ts,      last_major_ts,
                                      vol_reference,         tgi_reference,    vol_accumulator};
    auto pool = clmm_pool(sqrt_price, liquidity, tick_current, tick_indices, tick_liq_net, tick_initialized,
                          tick_sqrt_prices, fee_rate, token_a_decimals, token_b_decimals);
    pool.adaptive_fee = &fee;
    pool.now_ts = now_ts;
    const auto r = quote::find_optimal_size(pool, a_to_b, edge_bps, min_size, alloc_cap, pool_search_cap, iterations);
    return {r.clipped_size, r.unclipped_size, r.unclipped_net_edge_bps};
}

std::tuple<double, double, double> dlmm_find_optimal_size(
    std::int64_t active_id, std::int64_t bin_step, Array1D<std::int64_t> bin_ids, Array1D<double> amounts_x,
    Array1D<double> amounts_y, int token_a_decimals, int token_b_decimals, std::int64_t base_factor,
    std::int64_t base_fee_power_factor, std::int64_t filter_period, std::int64_t decay_period,
    std::int64_t reduction_factor, std::int64_t variable_fee_control, std::int64_t max_volatility_accumulator,
    std::int64_t volatility_accumulator, std::int64_t volatility_reference, std::int64_t index_reference,
    std::int64_t last_update_timestamp, double fee_rate, double raw_mid_before, std::int64_t now_ts, bool a_to_b,
    double edge_bps, double min_size, double alloc_cap, double pool_search_cap, int iterations) {
    if (amounts_x.shape(0) != bin_ids.shape(0) || amounts_y.shape(0) != bin_ids.shape(0)) {
        throw nb::value_error("bin arrays must have equal length");
    }
    const quote::DlmmPool pool{
        active_id, bin_step, {as_span(bin_ids), as_span(amounts_x), as_span(amounts_y)}, token_a_decimals,
        token_b_decimals,
        {base_factor, base_fee_power_factor, filter_period, decay_period, reduction_factor, variable_fee_control,
         max_volatility_accumulator, volatility_accumulator, volatility_reference, index_reference,
         last_update_timestamp},
        fee_rate, raw_mid_before, now_ts};
    const auto r = quote::find_optimal_size(pool, a_to_b, edge_bps, min_size, alloc_cap, pool_search_cap, iterations);
    return {r.clipped_size, r.unclipped_size, r.unclipped_net_edge_bps};
}

}  // namespace

NB_MODULE(quote_cpp, m) {
    m.doc() = "C++ ports of trading-v1's Numba quote kernels";
    m.def("compute_swap_step", &compute_swap_step);
    m.def("clmm_quote", &clmm_quote);
    m.def("clmm_quote_adaptive", &clmm_quote_adaptive);
    m.def("dlmm_quote", &dlmm_quote);
    m.def("clmm_impact_bps", &clmm_impact_bps);
    m.def("clmm_find_optimal_size", &clmm_find_optimal_size);
    m.def("clmm_adaptive_find_optimal_size", &clmm_adaptive_find_optimal_size);
    m.def("dlmm_find_optimal_size", &dlmm_find_optimal_size);
}

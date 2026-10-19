#include "quote/clmm_step.hpp"

#include <cmath>

namespace quote {

// Operation order mirrors the Numba kernel exactly; reordering changes the rounded bits.
SwapStep compute_swap_step(double sqrt_price_current, double sqrt_price_target, double liquidity,
                           double amount_remaining, bool a_to_b) {
    if (liquidity == 0.0) return {0.0, 0.0, sqrt_price_current, false};

    double amount_in;
    double amount_out;
    double sqrt_price_next;
    bool reached;

    if (a_to_b) {
        const double max_amount_in =
            (sqrt_price_target > 0.0 && sqrt_price_current > 0.0)
                ? std::fabs(liquidity * (1.0 / sqrt_price_target - 1.0 / sqrt_price_current))
                : 0.0;
        if (amount_remaining >= max_amount_in) {
            amount_in = max_amount_in;
            sqrt_price_next = sqrt_price_target;
            reached = true;
        } else {
            amount_in = amount_remaining;
            sqrt_price_next = (liquidity * sqrt_price_current) / (liquidity + amount_in * sqrt_price_current);
            reached = false;
        }
        amount_out = liquidity * (sqrt_price_current - sqrt_price_next);
    } else {
        const double max_amount_in = std::fabs(liquidity * (sqrt_price_target - sqrt_price_current));
        if (amount_remaining >= max_amount_in) {
            amount_in = max_amount_in;
            sqrt_price_next = sqrt_price_target;
            reached = true;
        } else {
            amount_in = amount_remaining;
            sqrt_price_next = sqrt_price_current + amount_in / liquidity;
            reached = false;
        }
        amount_out = (sqrt_price_current > 0.0 && sqrt_price_next > 0.0)
                         ? liquidity * (1.0 / sqrt_price_current - 1.0 / sqrt_price_next)
                         : 0.0;
    }

    return {std::fabs(amount_in), std::fabs(amount_out), sqrt_price_next, reached};
}

}  // namespace quote

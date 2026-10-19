// Kernel-only benchmark for compute_swap_step over the Level 1 golden vectors.
// Verifies every output bit-exact first, then times repetitions of the full input set.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "quote/clmm_step.hpp"
#include "../tests/vectors.hpp"

namespace {

constexpr int kWarmupReps = 500;
constexpr int kTimedReps = 5000;
constexpr int kInnerLoops = 20;  // amortizes clock-read cost over 20 passes of the input set

struct Inputs {
    std::vector<double> sqrt_cur, sqrt_tgt, liquidity, amount;
    std::vector<unsigned char> a_to_b;
};

struct Outputs {
    std::vector<double> amount_in, amount_out, sqrt_next;
    std::vector<unsigned char> reached;
};

void run_once(const Inputs& in, Outputs& out) {
    const std::size_t n = in.amount.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto s = quote::compute_swap_step(in.sqrt_cur[i], in.sqrt_tgt[i], in.liquidity[i], in.amount[i],
                                                in.a_to_b[i] != 0);
        out.amount_in[i] = s.amount_in;
        out.amount_out[i] = s.amount_out;
        out.sqrt_next[i] = s.sqrt_price_next;
        out.reached[i] = s.reached_target;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "vectors/level1_swap_step.csv";
    const auto rows = vectors::load_csv(path);
    const std::size_t n = rows.size();

    Inputs in;
    Outputs out{std::vector<double>(n), std::vector<double>(n), std::vector<double>(n),
                std::vector<unsigned char>(n)};
    for (const auto& r : rows) {
        in.sqrt_cur.push_back(r.f64("sqrt_price_current"));
        in.sqrt_tgt.push_back(r.f64("sqrt_price_target"));
        in.liquidity.push_back(r.f64("liquidity"));
        in.amount.push_back(r.f64("amount_remaining"));
        in.a_to_b.push_back(r.flag("a_to_b"));
    }

    run_once(in, out);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = rows[i];
        if (vectors::ulp_distance(out.amount_in[i], r.f64("amount_in")) ||
            vectors::ulp_distance(out.amount_out[i], r.f64("amount_out")) ||
            vectors::ulp_distance(out.sqrt_next[i], r.f64("sqrt_price_next")) ||
            (out.reached[i] != 0) != r.flag("reached")) {
            std::fprintf(stderr, "mismatch on %s, refusing to benchmark\n", r.str("case").c_str());
            return 1;
        }
    }

    for (int rep = 0; rep < kWarmupReps; ++rep) run_once(in, out);

    std::vector<double> ns_per_call(kTimedReps);
    for (int rep = 0; rep < kTimedReps; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < kInnerLoops; ++k) run_once(in, out);
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        ns_per_call[static_cast<std::size_t>(rep)] = ns / static_cast<double>(n * kInnerLoops);
    }

    std::sort(ns_per_call.begin(), ns_per_call.end());
    const double median = ns_per_call[ns_per_call.size() / 2];
    const double p99 = ns_per_call[ns_per_call.size() * 99 / 100];
    std::printf("{\"impl\": \"cpp\", \"calls_per_rep\": %zu, \"median_ns\": %.3f, \"p99_ns\": %.3f}\n",
                n * kInnerLoops, median, p99);
    return 0;
}

// Level 1: compute_swap_step vs Numba golden vectors, bit-exact.

#include <cstdio>
#include <stdexcept>
#include <string>

#include "quote/clmm_step.hpp"
#include "vectors.hpp"

namespace {

constexpr int kMaxFailuresShown = 10;

bool check_field(const std::string& name, double got, double want, std::string& report) {
    const auto ulps = vectors::ulp_distance(got, want);
    if (ulps == 0) return true;
    char buf[200];
    std::snprintf(buf, sizeof buf, "    %-16s got %a  want %a  (%llu ulp)\n", name.c_str(), got, want,
                  static_cast<unsigned long long>(ulps));
    report += buf;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "vectors/level1_swap_step.csv";
    const auto rows = vectors::load_csv(path);

    int pass = 0, fail = 0, todo = 0;
    for (const auto& row : rows) {
        quote::SwapStep got{};
        try {
            got = quote::compute_swap_step(row.f64("sqrt_price_current"), row.f64("sqrt_price_target"),
                                           row.f64("liquidity"), row.f64("amount_remaining"), row.flag("a_to_b"));
        } catch (const std::logic_error&) {
            ++todo;
            continue;
        }

        std::string report;
        bool ok = check_field("amount_in", got.amount_in, row.f64("amount_in"), report);
        ok &= check_field("amount_out", got.amount_out, row.f64("amount_out"), report);
        ok &= check_field("sqrt_price_next", got.sqrt_price_next, row.f64("sqrt_price_next"), report);
        if (got.reached_target != row.flag("reached")) {
            ok = false;
            report += "    reached_target   got " + std::to_string(got.reached_target) + "  want " +
                      row.str("reached") + "\n";
        }

        if (ok) {
            ++pass;
        } else if (++fail <= kMaxFailuresShown) {
            std::printf("FAIL %s\n%s", row.str("case").c_str(), report.c_str());
        }
    }

    if (todo) std::printf("TODO %d cases (compute_swap_step not implemented)\n", todo);
    std::printf("%d/%zu passed\n", pass, rows.size());
    return pass == static_cast<int>(rows.size()) ? 0 : 1;
}

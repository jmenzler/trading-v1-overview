#pragma once

// Python integer and power semantics as Numba compiles them, reproduced exactly in plain C++.

#include <cmath>
#include <cstdint>

namespace quote::pysem {

// Python `a // b`: rounds toward negative infinity (C++ `/` truncates toward zero).
inline std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Numba int64 `*` wraps on overflow; signed overflow is UB in C++, so multiply as unsigned.
inline std::int64_t wrapping_mul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}

// Numba `float ** int`: binary exponentiation, reciprocal for negative exponents, libm pow past 2^16.
// Multiplication order matters for inexact bases such as 1.0001.
inline double int_power(double base, std::int64_t exponent) {
    const bool invert = exponent < 0;
    std::uint64_t exp = invert ? static_cast<std::uint64_t>(-exponent) : static_cast<std::uint64_t>(exponent);
    if (exp > 0x10000) return std::pow(base, static_cast<double>(exponent));
    double result = 1.0;
    while (exp != 0) {
        if (exp & 1U) result *= base;
        exp >>= 1U;
        base *= base;
    }
    return invert ? 1.0 / result : result;
}

}  // namespace quote::pysem

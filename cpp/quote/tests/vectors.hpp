#pragma once

// Test-only helpers: load golden-vector CSVs and compare doubles bit for bit.

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vectors {

struct Row {
    std::unordered_map<std::string, std::string> cells;

    const std::string& str(const std::string& col) const {
        auto it = cells.find(col);
        if (it == cells.end()) throw std::runtime_error("missing column: " + col);
        return it->second;
    }
    // Hex floats ("0x1.8p+3") round-trip exactly through strtod.
    double f64(const std::string& col) const { return std::strtod(str(col).c_str(), nullptr); }
    bool flag(const std::string& col) const { return str(col) == "1"; }
};

inline std::vector<std::string> split(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

inline std::vector<Row> load_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::string line;
    std::getline(in, line);
    const auto header = split(line);
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        const auto cells = split(line);
        if (cells.size() != header.size()) throw std::runtime_error("bad row in " + path + ": " + line);
        Row row;
        for (std::size_t i = 0; i < header.size(); ++i) row.cells[header[i]] = cells[i];
        rows.push_back(std::move(row));
    }
    return rows;
}

// Distance in units-in-the-last-place; 0 means identical bits (so +0.0 vs -0.0 counts as a mismatch).
inline std::uint64_t ulp_distance(double a, double b) {
    auto key = [](double x) {
        auto u = std::bit_cast<std::uint64_t>(x);
        return (u >> 63) ? ~u : (u | (std::uint64_t{1} << 63));
    };
    const auto ka = key(a), kb = key(b);
    return ka > kb ? ka - kb : kb - ka;
}

}  // namespace vectors

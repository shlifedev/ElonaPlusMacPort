#pragma once
// Deterministic data patterns shared by the native tests and the x86 fixtures
// (tests/plugins/fixtures/plugin_fixtures.py patfill/rndfill).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace elona::plugin_tests {

// byte i = (i*7 + i/251) & 0xff
inline std::uint8_t pattern_byte(std::size_t i) { return static_cast<std::uint8_t>((i * 7 + i / 251) & 0xff); }

inline std::vector<std::uint8_t> pattern(std::size_t size, std::size_t base = 0) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = pattern_byte(base + i);
    return data;
}

// HSP int32 arithmetic: h = i*1103515245+12345; h ^= h>>15 (arithmetic); byte = (h>>7)&255
inline std::int32_t asr(std::int32_t v, int n) {
    return static_cast<std::int32_t>(v < 0 ? ~(~static_cast<std::uint32_t>(v) >> n) : static_cast<std::uint32_t>(v) >> n);
}

inline std::uint8_t rnd_byte(std::size_t k) {
    const auto i = static_cast<std::uint32_t>(k);
    std::int32_t h = static_cast<std::int32_t>(i * 1103515245u + 12345u);
    h = h ^ asr(h, 15);
    return static_cast<std::uint8_t>(asr(h, 7) & 255);
}

inline std::vector<std::uint8_t> rnd_pattern(std::size_t size, std::size_t base = 0) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = rnd_byte(base + i);
    return data;
}

}  // namespace elona::plugin_tests

#include "exrand.h"

namespace elona::plugins {
namespace {
constexpr int kN = 624;
constexpr int kM = 397;
constexpr std::uint32_t kMatrixA = 0x9908b0dfu;
constexpr std::uint32_t kUpper = 0x80000000u;
constexpr std::uint32_t kLower = 0x7fffffffu;
constexpr std::uint32_t kDefaultSeed = 5489u;  // `push 0x1571` before the lazy init
constexpr std::int32_t kRawInvalidArgument = 3;

std::uint32_t twist(std::uint32_t u, std::uint32_t v) noexcept {
    const std::uint32_t y = (u & kUpper) | (v & kLower);
    return (y >> 1) ^ ((v & 1u) != 0 ? kMatrixA : 0u);
}
}  // namespace

ExRand::ExRand() noexcept = default;

void ExRand::seed_state(std::uint32_t seed) noexcept {
    state_[0] = seed;
    for (int i = 1; i < kN; ++i) {
        const std::uint32_t prev = state_[i - 1];
        state_[i] = 1812433253u * (prev ^ (prev >> 30)) + static_cast<std::uint32_t>(i);
    }
    // The original sets its countdown to 1 so the next draw refills.
    index_ = kN;
    seeded_ = true;
}

void ExRand::refill() noexcept {
    if (!seeded_) seed_state(kDefaultSeed);
    // Same recurrence as the original next_state (0xe3 + 0x18c + 1 words).
    // Note: the original computes the low bit from the *next* word, matching
    // mt19937ar-cok's MIXBITS/TWIST definition used here.
    int k = 0;
    for (; k < kN - kM; ++k) state_[k] = state_[k + kM] ^ twist(state_[k], state_[k + 1]);
    for (; k < kN - 1; ++k) state_[k] = state_[k + kM - kN] ^ twist(state_[k], state_[k + 1]);
    state_[kN - 1] = state_[kM - 1] ^ twist(state_[kN - 1], state_[0]);
    index_ = 0;
}

std::uint32_t ExRand::next_u32() noexcept {
    if (index_ >= kN) refill();
    std::uint32_t y = state_[index_++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

std::int32_t ExRand::scale(std::uint32_t draw, std::int32_t range) noexcept {
    // fild (exact) -> fmul 2^-32 (exact, power of two) -> fimul range (one
    // rounding to the FPU precision) -> _ftol (truncate toward zero).
    // IEEE double multiplication equals x87 with 53-bit precision control.
    double value = static_cast<double>(draw) * (1.0 / 4294967296.0);
    value = value * static_cast<double>(range);
    return static_cast<std::int32_t>(value);  // 0 <= value < 2^31
}

PluginResult ExRand::randomize(std::int32_t seed) noexcept {
    if (seed < 0) return legacy_dll_error(kRawInvalidArgument, "exrand_randomize: negative seed (original EAX 3)");
    seed_state(static_cast<std::uint32_t>(seed));
    return legacy_ok(0);
}

PluginResult ExRand::rnd(std::int32_t& out, std::int32_t range) noexcept {
    if (range < 0) return legacy_dll_error(kRawInvalidArgument, "exrand_rnd: negative range (original EAX 3)");
    out = scale(next_u32(), range);
    return legacy_ok(0);
}

PluginResult ExRand::rnd2(std::int32_t& out, std::int32_t base, std::int32_t range) noexcept {
    if (range < 0) return legacy_dll_error(kRawInvalidArgument, "exrand_rnd2: negative range (original EAX 3)");
    // x86 `add eax, ecx` wraps modulo 2^32.
    out = static_cast<std::int32_t>(static_cast<std::uint32_t>(scale(next_u32(), range)) +
                                    static_cast<std::uint32_t>(base));
    return legacy_ok(0);
}

}  // namespace elona::plugins

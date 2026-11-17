#pragma once
// exrand.dll replacement (exrand.dll SHA-256 fb99c144...0b23).
//
// Static evidence (x86 disassembly of the shipped DLL):
//   * Generator: MT19937, init_genrand(seed) (1812433253 multiplier) and the
//     mt19937ar-cok "next_state" refill. An unseeded first draw seeds 5489.
//   * exrand_randomize(seed,_,_,_): seed < 0 -> EAX 3 (HSP DLL error, state
//     unchanged); otherwise init_genrand(seed), EAX 0.
//   * exrand_rnd(&out, range,_,_): range < 0 -> EAX 3 (DLL error, out unchanged).
//     Otherwise out = (int)trunc((double)u32 * 2^-32 * range) via x87
//     fild/fmul/fimul/_ftol, EAX 0. range 0 -> 0 (draw still consumed).
//   * exrand_rnd2(&out, base, range,_): same draw with + base. Exported but not
//     declared by 2.335; provided for completeness.
//   * exrand_randomize_by_time is exported but not declared by 2.335 and is not
//     provided (time seeding is non-deterministic and would hide divergence).
// The x87 precision used by the original process is verified by fixtures (see
// docs/PLUGIN_IMPLEMENTATION.md); this port computes in IEEE double.
#include <array>
#include <cstdint>

#include "plugin_result.h"

namespace elona::plugins {

class ExRand {
public:
    ExRand() noexcept;

    // Raw-return contract: result.raw_return is the original EAX.
    PluginResult randomize(std::int32_t seed) noexcept;
    PluginResult rnd(std::int32_t& out, std::int32_t range) noexcept;
    PluginResult rnd2(std::int32_t& out, std::int32_t base, std::int32_t range) noexcept;

    // Next tempered MT19937 output (exposed for tests).
    std::uint32_t next_u32() noexcept;

    // Pure scaling step used by rnd/rnd2 (exposed for tests).
    static std::int32_t scale(std::uint32_t draw, std::int32_t range) noexcept;

private:
    void seed_state(std::uint32_t seed) noexcept;
    void refill() noexcept;

    std::array<std::uint32_t, 624> state_{};
    int index_ = 624;
    bool seeded_ = false;
};

}  // namespace elona::plugins

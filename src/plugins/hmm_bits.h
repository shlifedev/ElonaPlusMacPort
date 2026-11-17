#pragma once
// hmm.dll HMMBITON / HMMBITOFF / HMMBITCHECK replacement.
//
// Original (hmm.dll SHA-256 5eed0341...0565, exports 1000d6e0/1000d700/1000d720):
//   ON:    *p |=  (1 << (bit & 31)); EAX = -1          -> legacy stat 1
//   OFF:   *p &= ~(1 << (bit & 31)); EAX = -1          -> legacy stat 1
//   CHECK: (value & (1 << (bit & 31))) ? EAX = -1 : 0   -> legacy stat 1 / 0
// x86 `shl reg, cl` masks the count to 5 bits; there is no range check.
// The 2.335 declarations are `$1` (var ptr, int, int, int) for ON/OFF and `$0`
// (int x4) for CHECK. The last two parameters are unused.
//
// Sign convention (avoid double negation):
//   *_raw functions return the original export's EAX. A DllHandler that
//   returns raw EAX to a host which applies `stat = -raw` centrally must use
//   these. hmm_stat_from_raw() exists only for tests/documentation.
//
// These are hot (946 HMMBITON call sites) so they are header-only and never
// fail. The dispatcher resolves `target` to the int32 element the game passed
// (array element addressing) and refuses a non-int variable before calling.
#include <cstdint>

namespace elona::plugins {

constexpr std::uint32_t hmm_bit_mask(std::int32_t bit) noexcept {
    return std::uint32_t{1} << (static_cast<std::uint32_t>(bit) & 31u);
}

// Raw EAX: always -1 (legacy stat 1).
inline std::int32_t hmm_bit_on_raw(std::int32_t& target, std::int32_t bit) noexcept {
    target = static_cast<std::int32_t>(static_cast<std::uint32_t>(target) | hmm_bit_mask(bit));
    return -1;
}

// Raw EAX: always -1 (legacy stat 1).
inline std::int32_t hmm_bit_off_raw(std::int32_t& target, std::int32_t bit) noexcept {
    target = static_cast<std::int32_t>(static_cast<std::uint32_t>(target) & ~hmm_bit_mask(bit));
    return -1;
}

// Raw EAX: -1 when set (legacy stat 1), 0 when clear (legacy stat 0).
constexpr std::int32_t hmm_bit_check_raw(std::int32_t value, std::int32_t bit) noexcept {
    return (static_cast<std::uint32_t>(value) & hmm_bit_mask(bit)) != 0 ? -1 : 0;
}

// Legacy (#func $N, STRUCTPRM_SUBID_OLDDLLINIT) stat for a non-positive raw value.
constexpr std::int32_t hmm_stat_from_raw(std::int32_t raw) noexcept {
    return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(raw));
}

}  // namespace elona::plugins

#pragma once
// Typed result shared by the portable ElonaPlus 2.335 plugin replacements.
//
// These modules replace the *observed contract* of the original x86 plugins.
// They never load or imitate the x86 DLL ABI. The HSP dispatcher (owned by the
// game host) converts VM arguments into the typed parameters below and maps a
// PluginResult back into HSP `stat` or an HSP error.
#include <cstdint>

namespace elona::plugins {

enum class PluginOutcome : std::uint8_t {
    // The call completed. `stat` is the value the original legacy (#func $N)
    // call stored in HSP `stat` (already negated from the raw export return).
    ok,
    // The original export returned a positive value without the wait bits
    // (0x10000/0x20000). OpenHSP/HSP 3.x raise HSPERR_DLL_ERROR for that in the
    // legacy calling convention. The dispatcher must raise the HSP error and
    // must not update `stat`.
    hsp_dll_error,
    // The original would have dereferenced a wild/stale pointer, overrun a VM
    // buffer or reached CRT behaviour that was not verified. The native module
    // refuses instead of guessing. The dispatcher must stop with a diagnostic
    // naming the call site; continuing would hide a divergence.
    unsafe_input,
    // Input that the original accepts but whose result this port has not
    // reproduced or verified. Never treat as success.
    unsupported,
    // Host failure (allocation, zlib init) that has no original equivalent.
    host_error,
};

struct PluginResult {
    PluginOutcome outcome = PluginOutcome::ok;
    // Valid only when outcome == ok.
    std::int32_t stat = 0;
    // What the original 32-bit export returned in EAX (informational; lets
    // tests and diagnostics show the legacy sign convention). For ok results
    // stat == -raw_return.
    std::int32_t raw_return = 0;
    // Static string; never owned. Empty for ok.
    const char* message = "";

    constexpr bool is_ok() const noexcept { return outcome == PluginOutcome::ok; }
};

constexpr PluginResult legacy_ok(std::int32_t raw_return) noexcept {
    // Legacy convention: stat = -raw for raw <= 0. Callers must not use this
    // for positive raw values; use legacy_dll_error instead.
    return PluginResult{PluginOutcome::ok, static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(raw_return)),
                        raw_return, ""};
}

constexpr PluginResult legacy_dll_error(std::int32_t raw_return, const char* message) noexcept {
    return PluginResult{PluginOutcome::hsp_dll_error, 0, raw_return, message};
}

constexpr PluginResult refused(PluginOutcome outcome, const char* message) noexcept {
    return PluginResult{outcome, 0, 0, message};
}

const char* outcome_name(PluginOutcome outcome) noexcept;

}  // namespace elona::plugins

#include "plugin_result.h"

namespace elona::plugins {

const char* outcome_name(PluginOutcome outcome) noexcept {
    switch (outcome) {
    case PluginOutcome::ok: return "ok";
    case PluginOutcome::hsp_dll_error: return "hsp_dll_error";
    case PluginOutcome::unsafe_input: return "unsafe_input";
    case PluginOutcome::unsupported: return "unsupported";
    case PluginOutcome::host_error: return "host_error";
    }
    return "invalid";
}

}  // namespace elona::plugins

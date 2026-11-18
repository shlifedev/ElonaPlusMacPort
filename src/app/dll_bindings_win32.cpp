// Narrow replacements for the Win32 APIs that ElonaPlus 2.335 declares with
// #uselib/#func. Only the behaviour the game relies on is reproduced; every
// other API stays unregistered and stops at first use with a diagnostic.
// Handles returned here are opaque 32-bit tokens, never host pointers.
#include <string>
#include <cstring>
#include <limits>
#include "platform/encoding.h"

#include "native_dll.h"

namespace elona::app {
namespace {
std::int32_t g_last_error = 0;
std::int32_t g_next_handle = 0x00e20001;
int g_open_mutexes = 0;
}  // namespace

void register_win32_bindings() {
    // winmm: the host clock already has 1 ms resolution; TIMERR_NOERROR = 0.
    register_dll_handler("winmm.dll", "timeBeginPeriod", [](DllCall& c) {
        if (c.args.size() != 1 || c.args[0].i < 1) throw DllRefusal("timeBeginPeriod period < 1 (TIMERR_NOCANDO path unverified)");
        return 0;
    });
    register_dll_handler("winmm.dll", "timeEndPeriod", [](DllCall& c) {
        if (c.args.size() != 1 || c.args[0].i < 1) throw DllRefusal("timeEndPeriod period < 1");
        return 0;
    });
    register_dll_handler("winmm.dll", "timeGetTime", [](DllCall& c) { return c.host->tick_ms(); });

    // The game asks for the locale only to map the width of CP932 text. Use
    // the Japanese locale on both platforms, independent of the desktop locale.
    register_dll_handler("kernel32.dll", "GetUserDefaultLCID", [](DllCall&) { return 0x411; });
    register_dll_handler("kernel32.dll", "LCMapStringA", [](DllCall& c) -> std::int32_t {
        const auto& a = c.args;
        if (a.size() != 6 || a[0].kind != DllArgKind::Int || a[0].i != 0x411 ||
            a[1].kind != DllArgKind::Int || (a[1].i != 0x400000 && a[1].i != 0x800000) ||
            a[2].kind != DllArgKind::String || a[3].kind != DllArgKind::Int || a[3].i != -1 ||
            a[4].kind != DllArgKind::VarRef || a[5].kind != DllArgKind::Int || a[5].i < 0)
            throw DllRefusal("LCMapStringA requires a terminated Japanese string and a width mapping");
        std::string mapped;
        if (!platform::map_cp932_width(a[2].str, a[1].i == 0x800000, mapped))
            throw DllRefusal("LCMapStringA invalid CP932 input");
        const auto bytes = mapped.size() + 1;
        if (bytes > std::size_t(std::numeric_limits<std::int32_t>::max()))
            throw DllRefusal("LCMapStringA result exceeds a 32-bit byte count");
        if (a[5].i == 0) return static_cast<std::int32_t>(bytes);
        if (std::size_t(a[5].i) < bytes) {
            g_last_error = 122; // ERROR_INSUFFICIENT_BUFFER
            return 0;
        }
        if (!a[4].data || bytes > a[4].bytes_remaining)
            throw DllRefusal("LCMapStringA result exceeds the HSP destination variable");
        std::memcpy(a[4].data, mapped.c_str(), bytes);
        return static_cast<std::int32_t>(bytes);
    });

    // kernel32: single-instance mutex. The original checks GetLastError() ==
    // ERROR_ALREADY_EXISTS (183) after CreateMutexA. Cross-process detection is
    // not implemented yet: this host reports "no other instance" and says so.
    register_dll_handler("kernel32.dll", "CreateMutexA", [](DllCall& c) {
        ++g_open_mutexes;
        g_last_error = 0;
        c.host->note("CreateMutexA: single-instance check is per-process only (no cross-process mutex yet)");
        return g_next_handle++;
    });
    register_dll_handler("kernel32.dll", "CloseHandle", [](DllCall& c) {
        if (c.args.empty() || c.args[0].kind != DllArgKind::Int || (c.args[0].i & 0xffff0000) != 0x00e20000)
            throw DllRefusal("CloseHandle on a handle this host did not create");
        if (g_open_mutexes > 0) --g_open_mutexes;
        g_last_error = 0;
        return 1;
    });
    register_dll_handler("kernel32.dll", "GetLastError", [](DllCall&) { return g_last_error; });
}

}  // namespace elona::app

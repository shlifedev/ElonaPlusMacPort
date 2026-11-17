// Narrow replacements for the Win32 APIs that ElonaPlus 2.335 declares with
// #uselib/#func. Only the behaviour the game relies on is reproduced; every
// other API stays unregistered and stops at first use with a diagnostic.
// Handles returned here are opaque 32-bit tokens, never host pointers.
#include <string>

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

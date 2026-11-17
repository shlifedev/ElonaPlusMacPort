// VM-host side boundary for native replacements of the original x86 DLLs.
//
// Contract (agreed with the coordinator / plugin worker):
//  * A handler returns the raw 32-bit value the x86 export would have left in
//    EAX. The host alone applies the HSP calling convention afterwards:
//    legacy (#func ... $flags, STRUCTDAT subid -5/-6) -> stat = -raw, positive
//    raw -> await (0x10000 / 0x20000 forms) or HSPERR_DLL_ERROR; modern -> stat = raw.
//  * Arguments are decoded by the host from STRUCTPRM mptypes. Game integers are
//    never turned into host pointers; variable references carry the PVal, APTR,
//    element type and the number of bytes remaining in that variable's storage.
//  * A handler that cannot honour a call throws DllRefusal (unsafe input,
//    unsupported variant, host error). The host prints the call site and stops
//    the program; nothing is reported as success.
//  * Unregistered functions stop at their first call with a diagnostic.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "hsp3struct.h"

namespace elona::app {

enum class DllArgKind { Int, Double, Float, String, VarRef, Surface, RefStr, Null, ExInfo };

struct DllArg {
    DllArgKind kind = DllArgKind::Int;
    std::int32_t i = 0;          // Int (also the value of a FLEX int)
    double d = 0.0;              // Double/Float
    std::string str;             // String: CP932 bytes exactly as in the VM
    PVal* pval = nullptr;        // VarRef
    APTR aptr = 0;               // VarRef element index
    int var_type = 0;            // HSPVAR_FLAG_* of the referenced variable
    std::uint8_t* data = nullptr;  // VarRef: element address inside VM storage
    std::size_t bytes_remaining = 0;  // VarRef: bytes from `data` to the end of the variable
    int surface_id = -1;         // Surface (BMSCR*) : active screen id
    char* refstr = nullptr;      // RefStr: HSPCTX refstr buffer (1024 bytes)

    // Bounds-checked 32-bit view of a VarRef (int arrays / memory buffers).
    std::int32_t* i32(std::size_t index = 0) const;
};

class HostServices;

struct DllCall {
    std::string_view library;  // lower-case, as declared (e.g. "hmm.dll")
    std::string_view symbol;   // export name (e.g. "_HMMBITON@16")
    bool legacy = false;
    std::vector<DllArg> args;
    HostServices* host = nullptr;
};

struct DllRefusal : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using DllHandler = std::function<std::int32_t(DllCall&)>;

// Host facilities a handler may use (screens, files, diagnostics).
class HostServices {
public:
    virtual ~HostServices() = default;
    // 24-bit bottom-up DIB of a screen (same layout as BMSCR.pBit).
    virtual bool surface(int id, std::uint8_t*& bits, int& width, int& height, int& stride) = 0;
    virtual void mark_surface_dirty(int id) = 0;
    // Current pos (BMSCR.cx/cy) of a screen.
    virtual bool surface_cursor(int id, int& cx, int& cy) = 0;
    // gx/gy/gmode of a screen and the active (gsel) screen id.
    virtual bool surface_state(int id, int& gx, int& gy, int& gmode) = 0;
    virtual int active_surface() = 0;
    virtual std::int32_t tick_ms() = 0;
    virtual void note(const std::string& message) = 0;
    // CP932 game path -> native path through the game filesystem policy.
    // not_found is an ordinary missing file (callers reproduce the original
    // open failure); rejected is a policy/encoding/IO error that must stop.
    enum class Resolve { ok, not_found, rejected };
    virtual Resolve resolve_path(std::string_view cp932, bool for_write, std::filesystem::path& out, std::string& error) = 0;
    // Non-atomic stream writes (zOpen mode 1): materialises parent directories in
    // the user root. end_stream_write must follow a successful close.
    virtual bool begin_stream_write(std::string_view cp932, std::filesystem::path& out, std::string& error) = 0;
    virtual bool end_stream_write(std::string_view cp932, std::string& error) = 0;
};

void register_dll_handler(std::string_view library, std::string_view symbol, DllHandler handler);
const DllHandler* find_dll_handler(std::string_view library, std::string_view symbol);
std::vector<std::string> registered_dll_handlers();

}  // namespace elona::app

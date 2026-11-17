// Bindings from the original DLL exports to the portable plugin modules
// (src/plugins, plugin worker). Each handler returns the x86 EAX value; the host
// (gui_host.cpp exec_dll) applies the legacy stat/await/error convention.
// PluginResult outcomes other than ok/hsp_dll_error become DllRefusal, which the
// host reports with the call site and stops on.
#include <cstring>
#include <map>
#include <memory>

#include "hsp3code.h"
#include "hspvar_core.h"
#include "native_dll.h"

#ifdef ELONA_HAVE_PLUGINS
#include "plugins/exrand.h"
#include "plugins/hmm_bits.h"
#include "plugins/hspda.h"
#include "plugins/zgzip.h"
#endif

namespace elona::app {

#ifdef ELONA_HAVE_PLUGINS
namespace {
using namespace elona::plugins;

struct PluginState {
    ExRand exrand;
    GzipRegistry gzip;
    HspdaXNote xnote;
    PVal* xnote_var = nullptr;
    std::map<std::int32_t, std::string> write_paths;  // zOpen write handle -> CP932 path
};
PluginState& state() {
    static PluginState s;
    return s;
}

std::int32_t finish(const PluginResult& r, const DllCall& call) {
    switch (r.outcome) {
    case PluginOutcome::ok:
    case PluginOutcome::hsp_dll_error:
        return r.raw_return;
    case PluginOutcome::unsafe_input:
        throw DllRefusal(std::string("unsafe input: ") + r.message);
    case PluginOutcome::unsupported:
        throw DllRefusal(std::string("unsupported variant: ") + r.message);
    case PluginOutcome::host_error:
        throw DllRefusal(std::string("host error: ") + r.message);
    }
    throw DllRefusal("unknown plugin outcome in " + std::string(call.symbol));
}

const DllArg& arg(const DllCall& c, std::size_t i, DllArgKind kind) {
    if (i >= c.args.size() || c.args[i].kind != kind) throw DllRefusal("unexpected argument layout");
    return c.args[i];
}

std::int32_t& int_ref(const DllArg& a) {
    if (a.var_type != HSPVAR_FLAG_INT) throw DllRefusal("expected an int variable");
    return *a.i32(0);
}

HspVarType var_type(int flag) { return static_cast<HspVarType>(flag); }

// HSP2-style exinfo functions read their own parameters from the VM.
PVal* getva(APTR& aptr) {
    PVal* pval = nullptr;
    aptr = code_getva(&pval);
    return pval;
}

}  // namespace

void register_plugin_bindings() {
    // ---- exrand.dll
    register_dll_handler("exrand.dll", "_exrand_randomize@16", [](DllCall& c) {
        return finish(state().exrand.randomize(arg(c, 0, DllArgKind::Int).i), c);
    });
    register_dll_handler("exrand.dll", "_exrand_rnd@16", [](DllCall& c) {
        std::int32_t& out = int_ref(arg(c, 0, DllArgKind::VarRef));
        std::int32_t v = out;
        auto r = state().exrand.rnd(v, arg(c, 1, DllArgKind::Int).i);
        if (r.is_ok()) out = v;
        return finish(r, c);
    });
    // ---- hmm.dll status bits (no audio/input)
    register_dll_handler("hmm.dll", "_HMMBITON@16", [](DllCall& c) {
        return hmm_bit_on_raw(int_ref(arg(c, 0, DllArgKind::VarRef)), arg(c, 1, DllArgKind::Int).i);
    });
    register_dll_handler("hmm.dll", "_HMMBITOFF@16", [](DllCall& c) {
        return hmm_bit_off_raw(int_ref(arg(c, 0, DllArgKind::VarRef)), arg(c, 1, DllArgKind::Int).i);
    });
    register_dll_handler("hmm.dll", "_HMMBITCHECK@16", [](DllCall& c) {
        return hmm_bit_check_raw(arg(c, 0, DllArgKind::Int).i, arg(c, 1, DllArgKind::Int).i);
    });
    // ---- z.hpi gzip streams
    register_dll_handler("z.hpi", "_zOpen@16", [](DllCall& c) {
        const DllArg& h = arg(c, 0, DllArgKind::VarRef);
        std::int32_t& out = int_ref(h);
        const std::string& path = arg(c, 1, DllArgKind::String).str;
        std::int32_t mode = arg(c, 2, DllArgKind::Int).i, level = arg(c, 3, DllArgKind::Int).i;
        std::filesystem::path resolved;
        std::string err;
        if (mode != 0) {
            if (!c.host->begin_stream_write(path, resolved, err)) throw DllRefusal("zOpen write path rejected by file policy: " + err);
        } else {
            auto rr = c.host->resolve_path(path, false, resolved, err);
            if (rr == HostServices::Resolve::rejected) throw DllRefusal("zOpen path rejected by file policy: " + err);
            if (rr == HostServices::Resolve::not_found) resolved.clear();  // original gzopen failure: stat 1, handle 0
        }
        std::int32_t handle = out;
        auto r = state().gzip.open(handle, resolved, mode, level);
        if (r.outcome == PluginOutcome::ok) {
            out = handle;
            if (mode != 0 && handle != 0) state().write_paths[handle] = path;
        }
        return finish(r, c);
    });
    register_dll_handler("z.hpi", "_zRead@16", [](DllCall& c) {
        const DllArg& b = arg(c, 0, DllArgKind::VarRef);
        return finish(state().gzip.read(arg(c, 1, DllArgKind::Int).i, b.data, b.bytes_remaining, arg(c, 2, DllArgKind::Int).i), c);
    });
    register_dll_handler("z.hpi", "_zWrite@16", [](DllCall& c) {
        const DllArg& b = arg(c, 0, DllArgKind::VarRef);
        return finish(state().gzip.write(arg(c, 1, DllArgKind::Int).i, b.data, b.bytes_remaining, arg(c, 2, DllArgKind::Int).i), c);
    });
    register_dll_handler("z.hpi", "_zClose@16", [](DllCall& c) {
        std::int32_t handle = arg(c, 0, DllArgKind::Int).i;
        auto r = state().gzip.close(handle);
        auto it = state().write_paths.find(handle);
        if (it != state().write_paths.end()) {
            std::string path = it->second, err;
            state().write_paths.erase(it);
            if (r.is_ok() && r.stat == 0 && !c.host->end_stream_write(path, err))
                throw DllRefusal("zClose: finishing stream write failed: " + err);
        }
        return finish(r, c);
    });
    // ---- hspda.dll
    register_dll_handler("hspda.dll", "_sortval@16", [](DllCall& c) {
        arg(c, 0, DllArgKind::ExInfo);
        APTR aptr;
        PVal* pval = getva(aptr);
        std::int32_t order = code_getdi(0);
        return finish(hspda_sortval(var_type(pval->flag), pval->pt, std::size_t(pval->size), pval->len[1], order), c);
    });
    register_dll_handler("hspda.dll", "_sortnote@16", [](DllCall& c) {
        arg(c, 0, DllArgKind::ExInfo);
        APTR aptr;
        PVal* pval = getva(aptr);
        std::int32_t order = code_getdi(0);
        if (pval->flag != HSPVAR_FLAG_STR) throw DllRefusal("sortnote on a non-string variable");
        auto* data = reinterpret_cast<char*>(HspVarCorePtrAPTR(pval, aptr));
        int size = 0;
        HspVarCoreGetBlockSize(pval, reinterpret_cast<PDAT*>(data), &size);
        std::string out;
        auto r = hspda_sortnote(std::string_view(data, strnlen(data, std::size_t(size))), std::size_t(size), order, out);
        if (r.is_ok()) code_setva(pval, aptr, HSPVAR_FLAG_STR, out.c_str());
        return finish(r, c);
    });
    register_dll_handler("hspda.dll", "_xnotesel@16", [](DllCall& c) {
        const DllArg& v = arg(c, 0, DllArgKind::VarRef);
        state().xnote_var = v.pval;
        return finish(state().xnote.select(var_type(v.var_type), v.data, v.bytes_remaining, arg(c, 1, DllArgKind::Int).i), c);
    });
    register_dll_handler("hspda.dll", "_xnoteadd@16", [](DllCall& c) {
        PVal* pval = state().xnote_var;
        if (!pval) throw DllRefusal("xnoteadd before xnotesel");
        auto* data = reinterpret_cast<std::uint8_t*>(HspVarCorePtrAPTR(pval, 0));
        int size = 0;
        HspVarCoreGetBlockSize(pval, reinterpret_cast<PDAT*>(data), &size);
        return finish(state().xnote.add(data, std::size_t(size), arg(c, 1, DllArgKind::String).str), c);
    });
}
#else
void register_plugin_bindings() {}
#endif

}  // namespace elona::app

// End-to-end test host: runs a locally compiled, trusted HSP fixture on the real
// HSP VM with the platform file-event adapter installed. Not a game launcher.
// usage: platform_hsp_file_host AX GAME_ROOT USER_ROOT [PACKED_NAME PACKED_FILE]
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "hsp3config.h"
#include "hsp3.h"
#include "platform/game_files_hsp.h"

namespace {
HSPCTX* context = nullptr;
std::unique_ptr<elona::platform::GameFileSystem> files;
std::string returned;
[[noreturn]] void unsupported(const char* kind, int command) {
    std::fprintf(stderr, "unsupported %s command=%d\n", kind, command);
    throw HSPERR_UNSUPPORTED_FUNCTION;
}
int external_command(int command) {
    if (command != 0x0f) unsupported("external", command);   // mes only
    code_next();
    const char* text = code_getdsi(const_cast<char*>(""));
    std::fputs(text, stdout);
    if (code_getdi(0) == 0) std::fputc('\n', stdout);
    return RUNMODE_RUN;
}
int unavailable_command(int command) { unsupported("DLL/plugin", command); }
void* unavailable_function(int*, int command) { unsupported("DLL/plugin function", command); }
// dirinfo(id) is TYPE_EXTSYSVAR (10) code 0x102 (hspcmd.cpp "$102 10 dirinfo";
// hsp3gr_linux.cpp hsp3typeinit_cl_extfunc installs reffunc_function there).
// The GUI host must answer it the same way: id 0/1 -> virtual_root().
int* token_type = nullptr;
int* token_value = nullptr;
void* external_function(int* type_res, int arg) {
    if (*token_type != TYPE_MARK || *token_value != '(') throw HSPERR_INVALID_FUNCPARAM;
    code_next();
    if ((arg & 0xff) != 0x002) unsupported("external function", arg);
    const int id = code_geti();
    char* result = hsp3ext_getdir(id);
    if (*token_type != TYPE_MARK || *token_value != ')') throw HSPERR_INVALID_FUNCPARAM;
    code_next();
    *type_res = HSPVAR_FLAG_STR;
    return result;
}
void message_callback(HSPCTX* ctx) {
    if (ctx->runmode == RUNMODE_END || ctx->runmode == RUNMODE_STOP) throw HSPERR_NONE;
    unsupported("run mode", ctx->runmode);
}
}  // namespace
char* hsp3ext_getdir(int id);

// dirinfo(0)=dir_cur and dirinfo(1)=dir_exe both report the virtual root.
// Reset() also queries id 5 (dir_tv); answer a path under the virtual root.
char* hsp3ext_getdir(int id) {
    if (id == 0 || id == 1) returned = files->virtual_root();
    else if (id == 5) returned = files->virtual_root() + "\\hsptv";
    else unsupported("directory information", id);
    return returned.data();
}
char* hsp3ext_sysinfo(int id, int*, char*) { unsupported("system information", id); }
void hsp3ext_execfile(char*, char*, int mode) { unsupported("process execution", mode); }
void* ex_getbmscr(int id) { unsupported("screen reference", id); }
void ex_mref(PVal*, int id) { unsupported("memory reference", id); }

// Native paths must not pass through the ANSI code page: on Windows the narrow
// argv is converted with the system ACP (949 on the reference PC) and loses
// characters such as U+30FC. Hosts must take paths from the wide command line.
#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::filesystem::path> args(wargv, wargv + argc);
#else
int main(int argc, char** argv) {
    std::vector<std::filesystem::path> args(argv, argv + argc);
#endif
    if (argc != 4 && argc != 6) { std::fprintf(stderr, "usage: AX GAME USER [PACKED_NAME PACKED_FILE]\n"); return 2; }
    try {
        elona::platform::FileSystemConfig cfg;
        cfg.game_root = args[2];
        cfg.user_root = args[3];
        if (argc == 6) {
            std::ifstream in(args[5], std::ios::binary);
            cfg.packed_members.push_back({args[4].string(), std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {})});
        }
        elona::platform::FileDiagnostic d;
        if (elona::platform::GameFileSystem::open(cfg, files, &d) != elona::platform::FileError::ok)
            throw std::runtime_error("file system: " + d.detail);
        Hsp3 vm;
        // The AX path is ASCII in the tests; HSP's loader takes a narrow string.
        std::string ax = args[1].string();
        vm.SetFileName(ax.data());
        if (vm.Reset(0)) throw std::runtime_error("VM failed to load fixture");
        context = &vm.hspctx;
        context->msgfunc = message_callback;
        auto* ext = code_gettypeinfo(TYPE_EXTCMD);
        ext->cmdfunc = external_command;
        code_gettypeinfo(TYPE_EXTSYSVAR)->reffunc = external_function;
        token_type = ext->hspexinfo->nptype;
        token_value = ext->hspexinfo->npval;
        for (int type : {TYPE_DLLFUNC, TYPE_DLLCTRL}) {
            auto* info = code_gettypeinfo(type);
            info->cmdfunc = unavailable_command;
            info->reffunc = unavailable_function;
        }
        // Fresh user type id after Reset() (the fixture has no #regcmd plugins).
        if (!elona::platform::hsp::install_file_events(code_gettypeinfo(-1), context, files.get()))
            throw std::runtime_error("cannot install file events");
        const int result = code_execcmd();
        std::fflush(stdout);
        if (result == RUNMODE_ERROR) {
            std::fprintf(stderr, "HSP error=%d line=%d detail=%s\n", int(code_geterror()), code_getdebug_line(),
                         elona::platform::hsp::last_diagnostic().detail.c_str());
            return 1;
        }
        if (result != RUNMODE_END) { std::fprintf(stderr, "completion mode=%d\n", result); return 1; }
        return context->endcode;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    } catch (HSPERROR error) {
        std::fprintf(stderr, "HSP initialization error=%d\n", int(error));
        return 1;
    }
}

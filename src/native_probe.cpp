// Diagnostic host for trusted, locally compiled HSP fixtures. Not a game runtime
// or a sandbox for untrusted bytecode. Do not execute the original game here.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "hsp3config.h"
#include "hsp3.h"

namespace {
HSPCTX* context = nullptr;
std::string host_directory;
std::string fixture_directory;
std::string returned_directory;
[[noreturn]] void unsupported(const char* kind, int command) {
    std::fprintf(stderr, "unsupported %s command=%d\n", kind, command);
    throw HSPERR_UNSUPPORTED_FUNCTION;
}
int external_command(int command) {
    if (command != 0x0f) unsupported("external", command);
    code_next();
    const char* text = code_getdsi(const_cast<char*>(""));
    std::fputs(text, stdout);
    if (code_getdi(0) == 0) std::fputc('\n', stdout);
    return RUNMODE_RUN;
}
int unavailable_command(int command) { unsupported("DLL/plugin", command); }
void* unavailable_function(int*, int command) { unsupported("DLL/plugin function", command); }
void message_callback(HSPCTX* ctx) {
    if (ctx->runmode == RUNMODE_END || ctx->runmode == RUNMODE_STOP) throw HSPERR_NONE;
    unsupported("run mode", ctx->runmode);
}
std::uint32_t le32(const std::vector<unsigned char>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("truncated AX header");
    return std::uint32_t(data[offset]) | std::uint32_t(data[offset + 1]) << 8 |
           std::uint32_t(data[offset + 2]) << 16 | std::uint32_t(data[offset + 3]) << 24;
}
void validate_probe_input(const char* filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read fixture");
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
    if (data.size() < sizeof(HSPHED) || std::memcmp(data.data(), "HSP3", 4) != 0)
        throw std::runtime_error("not a supported current HSP3 object");
    if (le32(data, 12) != data.size()) throw std::runtime_error("AX size mismatch");
    if (le32(data, 16) < sizeof(HSPHED))
        throw std::runtime_error("legacy AX header requires an explicit compatibility loader; not executed");
    for (std::size_t offset = 16; offset < 80; offset += 8) {
        auto begin = le32(data, offset), size = le32(data, offset + 4);
        if (begin > data.size() || size > data.size() - begin)
            throw std::runtime_error("AX section exceeds file bounds");
    }
    if (le32(data, 52) != 0 || (le32(data, 84) & 0xffffu) != 0)
        throw std::runtime_error("DLL and plugin declarations are not permitted in diagnostic fixtures");
}
} // namespace

char* hsp3ext_getdir(int id) {
    switch (id) {
    case 0: returned_directory = std::filesystem::current_path().string(); break;
    case 1: returned_directory = host_directory; break;
    case 5: returned_directory = fixture_directory + "/hsptv"; break;
    default: unsupported("directory information", id);
    }
    return returned_directory.data();
}
char* hsp3ext_sysinfo(int id, int*, char*) { unsupported("system information", id); }
void hsp3ext_execfile(char*, char*, int mode) { unsupported("process execution", mode); }
void* ex_getbmscr(int id) { unsupported("screen reference", id); }
void ex_mref(PVal*, int id) { unsupported("memory reference", id); }

int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "--trusted-fixture") != 0) {
        std::fprintf(stderr, "Usage: elona_hsp_probe --trusted-fixture FILE.ax\nOnly use locally reviewed test fixtures. This is not a sandbox or game launcher.\n");
        return 2;
    }
    try {
        validate_probe_input(argv[2]);
        host_directory = std::filesystem::absolute(argv[0]).parent_path().string();
        fixture_directory = std::filesystem::absolute(argv[2]).parent_path().string();
        Hsp3 vm;
        vm.SetFileName(argv[2]);
        if (vm.Reset(0)) throw std::runtime_error("VM failed to load fixture");
        context = &vm.hspctx;
        context->msgfunc = message_callback;
        auto* commands = code_gettypeinfo(TYPE_EXTCMD);
        commands->cmdfunc = external_command;
        for (int type : {TYPE_DLLFUNC, TYPE_DLLCTRL}) {
            auto* info = code_gettypeinfo(type);
            info->cmdfunc = unavailable_command;
            info->reffunc = unavailable_function;
        }
        int result = code_execcmd();
        if (result == RUNMODE_ERROR) {
            std::fprintf(stderr, "HSP error=%d line=%d\n", int(code_geterror()), code_getdebug_line());
            return 1;
        }
        if (result != RUNMODE_END) {
            std::fprintf(stderr, "Unsupported VM completion mode=%d\n", result);
            return 1;
        }
        return context->endcode;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    } catch (HSPERROR error) {
        std::fprintf(stderr, "HSP initialization error=%d\n", int(error));
        return 1;
    }
}

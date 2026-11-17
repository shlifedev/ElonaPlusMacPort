#include "platform/game_files_hsp.h"

#include <cstring>
#include <string>
#include <vector>

#include "hsp3struct.h"
#include "strbuf.h"

namespace elona::platform::hsp {
namespace {

struct State {
    HSPCTX* ctx = nullptr;
    GameFileSystem* files = nullptr;
    char* dirlist_buffer = nullptr;   // owned between FDIRLIST1 and FDIRLIST2
    FileDiagnostic last;
    int counts[HSPEVENT_MAX] = {};
};
State state;

[[noreturn]] void fail(FileError error, const char* command, const std::string& detail) {
    state.last.error = error;
    state.last.detail = std::string(command) + ": " + to_string(error) + (detail.empty() ? "" : " - " + detail);
    throw HSPERR_FILE_IO;
}

void check(FileError error, const char* command, const FileDiagnostic& d) {
    if (error != FileError::ok) fail(error, command, d.detail);
}

std::string current_name() {
    return state.ctx->fnbuffer ? std::string(state.ctx->fnbuffer) : std::string();
}

}  // namespace

bool install_file_events(HSP3TYPEINFO* info, HSPCTX* ctx, GameFileSystem* files) {
    if (!info || !ctx || !files || info->eventfunc != nullptr) return false;
    state = State{};
    state.ctx = ctx;
    state.files = files;
    info->option |= HSPEVENT_ENABLE_FILE;
    info->eventfunc = &file_event;
    return true;
}

void uninstall() {
    if (state.dirlist_buffer) sbFree(state.dirlist_buffer);
    state = State{};
}

const FileDiagnostic& last_diagnostic() { return state.last; }

const int* event_counts(int* size) {
    if (size) *size = HSPEVENT_MAX;
    return state.counts;
}

int file_event(int event, int prm1, int prm2, void* prm3) {
    if (event < HSPEVENT_FNAME || event > HSPEVENT_FDIRLIST2) return 0;
    ++state.counts[event];
    if (!state.ctx || !state.files) {
        // Never fall through to the core's OS file code when not bound.
        state.last = {FileError::invalid_config, "file event before install_file_events"};
        throw HSPERR_FILE_IO;
    }
    HSPCTX* ctx = state.ctx;
    GameFileSystem& files = *state.files;
    FileDiagnostic d;
    switch (event) {
    case HSPEVENT_FNAME:
        // (n/a, n/a, name): identical to the core: keep it in ctx->fnbuffer.
        sbStrCopy(&ctx->fnbuffer, static_cast<char*>(prm3));
        return 1;
    case HSPEVENT_FREAD: {
        // (seek offset, size, dest): dpm_read semantics; strsize = bytes read.
        std::vector<std::uint8_t> bytes;
        const std::uint64_t offset = prm1 > 0 ? static_cast<std::uint64_t>(prm1) : 0;
        const std::size_t max = prm2 > 0 ? static_cast<std::size_t>(prm2) : 0;
        check(files.read_file(current_name(), bytes, offset, max, &d), "bload/noteload", d);
        if (!bytes.empty()) std::memcpy(prm3, bytes.data(), bytes.size());
        ctx->strsize = static_cast<int>(bytes.size());
        return 1;
    }
    case HSPEVENT_FWRITE: {
        // (seek offset or -1 = new file, size, source); strsize = bytes written.
        const std::size_t size = prm2 > 0 ? static_cast<std::size_t>(prm2) : 0;
        check(files.write_file(current_name(), prm3, size, prm1 < 0 ? -1 : prm1, &d), "bsave/notesave", d);
        ctx->strsize = static_cast<int>(size);
        return 1;
    }
    case HSPEVENT_FEXIST: {
        std::int64_t size = -1;
        const FileError e = files.file_size(current_name(), size);
        if (e == FileError::outside_root) fail(e, "exist", current_name());   // escaping the game namespace is a bug: stop
        if (e != FileError::ok) {
            // Malformed names (wildcards, device names, bad CP932): the original
            // fopen fails and exist yields -1. Keep that, but record why.
            state.last = {e, std::string("exist: ") + to_string(e) + " - " + current_name()};
            size = -1;
        }
        ctx->strsize = size > 0x7fffffff ? 0x7fffffff : static_cast<int>(size);
        return 1;
    }
    case HSPEVENT_FDELETE:
        check(files.remove_file(current_name(), &d), "delete", d);
        return 1;
    case HSPEVENT_FMKDIR:
        check(files.make_directory(current_name(), &d), "mkdir", d);
        return 1;
    case HSPEVENT_FCHDIR:
        // 2.335 has no chdir; the VM cwd stays the virtual root. Never touch the OS cwd.
        fail(FileError::not_writable, "chdir", "chdir is not supported by the ElonaPlus file layer: " + current_name());
    case HSPEVENT_FCOPY:
        // (n/a, n/a, destination name); source is fnbuffer.
        check(files.copy_file(current_name(), static_cast<char*>(prm3), &d), "bcopy", d);
        return 1;
    case HSPEVENT_FDIRLIST1: {
        // (mode, n/a, char** result): CRLF-separated names, stat = count.
        std::vector<DirectoryEntry> entries;
        check(files.list_directory(current_name(), prm1, entries, nullptr, &d), "dirlist", d);
        if (state.dirlist_buffer) { sbFree(state.dirlist_buffer); state.dirlist_buffer = nullptr; }
        char* buffer = sbAlloc(0x1000);
        buffer[0] = 0;
        for (const auto& entry : entries) {
            std::string line = entry.name_cp932;
            if (line.find('\0') != std::string::npos) continue;
            line += "\r\n";
            sbStrAdd(&buffer, const_cast<char*>(line.c_str()));
        }
        state.dirlist_buffer = buffer;
        ctx->stat = static_cast<int>(entries.size());
        *static_cast<char**>(prm3) = buffer;
        return 1;
    }
    case HSPEVENT_FDIRLIST2:
        if (state.dirlist_buffer) { sbFree(state.dirlist_buffer); state.dirlist_buffer = nullptr; }
        return 1;
    default:
        return 0;
    }
}

}  // namespace elona::platform::hsp

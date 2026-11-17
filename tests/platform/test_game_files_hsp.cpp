// Drives the HSP file-event adapter exactly as hsp3code.cpp code_event() would,
// with a zero-initialised HSPCTX. Links the real HSP strbuf.cpp.
#include "platform/game_files_hsp.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "hsp3struct.h"
#include "strbuf.h"

using namespace elona::platform;
namespace fs = std::filesystem;

// strbuf.cpp's debug helper references Alertf (supio); not used on these paths.
void Alertf(const char*, ...) {}

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static HSPCTX ctx;

static void fname(const std::string& n) { CHECK(hsp::file_event(HSPEVENT_FNAME, 0, 0, const_cast<char*>(n.c_str())) == 1); }
static bool throws(int event, int p1, int p2, void* p3) {
    try { hsp::file_event(event, p1, p2, p3); } catch (HSPERROR e) { return e == HSPERR_FILE_IO; }
    return false;
}
static int exist(const std::string& n) { fname(n); CHECK(hsp::file_event(HSPEVENT_FEXIST, 0, 0, nullptr) == 1); return ctx.strsize; }

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const fs::path scratch = fs::absolute(argv[1]);
    fs::remove_all(scratch);
    fs::create_directories(scratch / "game" / "user");
    fs::create_directories(scratch / "game" / "data");
    std::ofstream(scratch / "game" / "user" / "a.npc", std::ios::binary) << "npc";
    std::ofstream(scratch / "game" / "data" / "d.txt", std::ios::binary) << "data";
    sbInit();
    std::memset(&ctx, 0, sizeof ctx);
    ctx.fnbuffer = sbAlloc(64);

    // Before install: never falls through to OS code.
    CHECK(throws(HSPEVENT_FEXIST, 0, 0, nullptr));
    CHECK(hsp::file_event(HSPEVENT_GETKEY, 0, 0, nullptr) == 0);

    FileSystemConfig cfg;
    cfg.game_root = scratch / "game";
    cfg.user_root = scratch / "user";
    cfg.packed_members.push_back({"scene1.hsp", {'h', 'i'}});
    std::unique_ptr<GameFileSystem> files;
    CHECK(GameFileSystem::open(cfg, files) == FileError::ok);
    HSP3TYPEINFO info;
    std::memset(&info, 0, sizeof info);
    CHECK(hsp::install_file_events(&info, &ctx, files.get()));
    CHECK((info.option & HSPEVENT_ENABLE_FILE) && info.eventfunc == &hsp::file_event);
    CHECK(!hsp::install_file_events(&info, &ctx, files.get()));       // occupied type refused
    CHECK(info.eventfunc(HSPEVENT_COMMAND, 0, 0, nullptr) == 0);       // non-file events untouched

    const std::string root = files->virtual_root();
    CHECK(exist(root + "\\user\\a.npc") == 3);
    CHECK(std::string(ctx.fnbuffer) == root + "\\user\\a.npc");
    CHECK(exist(root + "\\user\\none.npc") == -1);
    CHECK(exist(root + "\\user") == -1);
    CHECK(exist(root + "\\user\\*.npc") == -1);                        // malformed: -1 like fopen, diagnosed
    CHECK(hsp::last_diagnostic().error == FileError::invalid_path);
    fname("D:\\x.txt");
    CHECK(throws(HSPEVENT_FEXIST, 0, 0, nullptr));                     // escaping: stop
    // noteload of a DPM member: FEXIST then FREAD(0,size).
    CHECK(exist("scene1.hsp") == 2);
    char buf[16] = {};
    CHECK(hsp::file_event(HSPEVENT_FREAD, 0, ctx.strsize, buf) == 1);
    CHECK(ctx.strsize == 2 && std::string(buf, 2) == "hi");
    // bsave (new file) and bload with offset/size.
    fname(root + "\\user\\b.npc");
    const char payload[] = "0123456789";
    CHECK(hsp::file_event(HSPEVENT_FWRITE, -1, 10, const_cast<char*>(payload)) == 1);
    CHECK(ctx.strsize == 10);
    std::memset(buf, 0, sizeof buf);
    CHECK(hsp::file_event(HSPEVENT_FREAD, 3, 4, buf) == 1);
    CHECK(ctx.strsize == 4 && std::string(buf, 4) == "3456");
    CHECK(hsp::file_event(HSPEVENT_FWRITE, 8, 3, const_cast<char*>("XYZ")) == 1);   // bsave offset
    CHECK(exist(root + "\\user\\b.npc") == 11);
    // Write outside the writable set -> HSPERR_FILE_IO with diagnostic, original untouched.
    fname(root + "\\data\\d.txt");
    CHECK(throws(HSPEVENT_FWRITE, -1, 1, const_cast<char*>("z")));
    CHECK(hsp::last_diagnostic().error == FileError::not_writable);
    CHECK(hsp::last_diagnostic().detail.find("bsave") != std::string::npos);
    // bload of a missing file -> error (dpm_read < 0).
    fname(root + "\\user\\none.npc");
    CHECK(throws(HSPEVENT_FREAD, 0, 4, buf));
    // bcopy.
    fname(root + "\\user\\a.npc");
    CHECK(hsp::file_event(HSPEVENT_FCOPY, 0, 0, const_cast<char*>((root + "\\user\\c.npc").c_str())) == 1);
    CHECK(exist(root + "\\user\\c.npc") == 3);
    // dirlist: CRLF names, stat = count; FDIRLIST2 frees.
    fname(root + "\\user\\*.npc");
    char* list = nullptr;
    CHECK(hsp::file_event(HSPEVENT_FDIRLIST1, 0, 0, &list) == 1);
    CHECK(ctx.stat == 3 && list && std::string(list) == "a.npc\r\nb.npc\r\nc.npc\r\n");
    CHECK(hsp::file_event(HSPEVENT_FDIRLIST2, 0, 0, nullptr) == 1);
    fname(root + "\\nothing\\*");
    CHECK(hsp::file_event(HSPEVENT_FDIRLIST1, 0, 0, &list) == 1);
    CHECK(ctx.stat == 0 && std::string(list).empty());
    CHECK(hsp::file_event(HSPEVENT_FDIRLIST2, 0, 0, nullptr) == 1);
    // delete original-only file: logical delete.
    fname(root + "\\user\\a.npc");
    CHECK(hsp::file_event(HSPEVENT_FDELETE, 0, 0, nullptr) == 1);
    CHECK(exist(root + "\\user\\a.npc") == -1);
    CHECK(fs::exists(scratch / "game" / "user" / "a.npc"));
    fname(root + "\\user\\a.npc");
    CHECK(throws(HSPEVENT_FDELETE, 0, 0, nullptr));                    // delfile()==0 -> error
    // mkdir / chdir.
    fname(root + "\\user\\sub");
    CHECK(hsp::file_event(HSPEVENT_FMKDIR, 0, 0, nullptr) == 1);
    CHECK(throws(HSPEVENT_FMKDIR, 0, 0, nullptr));
    const fs::path cwd = fs::current_path();
    fname(root + "\\user");
    CHECK(throws(HSPEVENT_FCHDIR, 0, 0, nullptr));
    CHECK(fs::current_path() == cwd);
    int n = 0;
    const int* counts = hsp::event_counts(&n);
    CHECK(n == HSPEVENT_MAX && counts[HSPEVENT_FNAME] > 10 && counts[HSPEVENT_FCHDIR] == 1);
    hsp::uninstall();
    std::printf("%s: %d/%d checks failed\n", failures ? "FAILED" : "OK", failures, checks);
    return failures ? 1 : 0;
}

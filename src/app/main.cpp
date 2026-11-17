// elona_gui_host: native HSP GUI host (diagnostic stage; not yet a playable game).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gui_host.h"

namespace {
void usage() {
    std::fprintf(stderr,
                 "Usage: elona_gui_host (--trusted-fixture | --game) FILE.ax [options]\n"
                 "  --headless              no window; CPU pixel model only\n"
                 "  --hidden-window         real SDL windows, kept hidden\n"
                 "  --auxiliary-close-hide  closing operation-help window 20 hides it; game continues\n"
                 "  --deterministic         virtual clock; ignore real keyboard/mouse\n"
                 "  --script-realtime       scripted input/dialogs with real clock and audio (requires --script)\n"
                 "  --fail-on-hsp-error     diagnostic: stop before the game's onerror handler\n"
                 "  --translation FILE.tsv display-only UTF-8 translation catalog\n"
                 "  --translation-misses F private audit of untranslated displayed strings\n"
                 "  --script FILE           deterministic input: '<frame> key|down|up VK', 'mouse X Y', 'click X Y',\n"
                 "                          'screenshot PATH', 'capture PATH', 'exit [code]'\n"
                 "  --max-waits N           stop after N wait/await/stop frames (exit 124)\n"
                 "  --timeout-ms N          wall-clock bound (exit 124)\n"
                 "  --work-dir DIR          VM working directory (scratch dir or staged private game copy)\n"
                 "  --exe-dir DIR           value of dirinfo(1)/dir_exe\n"
                 "  --screenshot FILE.bmp   save screen 0 pixels at exit\n"
                 "  --window-capture FILE   save the presented window read back from the renderer\n"
                 "  --report FILE.json      run report (exit reason, first unsupported call)\n"
                 "  --font FILE             font file (repeatable)\n"
                 "  --no-text-aa            threshold glyph alpha\n"
                 "  --trace-commands        count GUI commands in the report\n"
                 "  --game-root DIR         original 2.335 install, read only (with --game)\n"
                 "  --user-root DIR         private writable save/config root (with --game)\n"
                 "  --packed-dir DIR        extracted DPM members (scene1.hsp, scene2.hsp)\n"
                 "  --soundfont FILE.sf2    SoundFont for MIDI (default: pinned GeneralUser GS)\n"
                 "  --list-dll              print DLL declarations and native handler status, then exit\n"
                 "Never point --work-dir or --user-root at the original game folder.\n");
}
// OS start arguments are native paths: Windows passes UTF-16 (wmain), POSIX
// passes the native byte string. Paths stay std::filesystem::path end to end;
// only option names, numbers and SDL font names are converted to UTF-8. They
// are never the VM's CP932 strings (those come from GameFileSystem).
std::string arg_utf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

int host_main(const std::vector<std::filesystem::path>& argv) {
    const int argc = static_cast<int>(argv.size());
    elona::app::HostOptions o;
    for (int i = 1; i < argc; ++i) {
        std::string a = arg_utf8(argv[i]);
        auto next_path = [&]() -> std::filesystem::path {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        auto next = [&]() -> std::string { return arg_utf8(next_path()); };
        if (a == "--trusted-fixture") { o.trusted_fixture = true; o.ax = next_path(); }
        else if (a == "--game") { o.game = true; o.ax = next_path(); }
        else if (a == "--headless") o.headless = true;
        else if (a == "--hidden-window") o.hidden_window = true;
        else if (a == "--auxiliary-close-hide") o.auxiliary_close_hide = true;
        else if (a == "--deterministic") o.deterministic = true;
        else if (a == "--script-realtime") o.scripted_realtime = true;
        else if (a == "--fail-on-hsp-error") o.fail_on_hsp_error = true;
        else if (a == "--translation") o.translation_catalog = next_path();
        else if (a == "--translation-misses") o.translation_misses = next_path();
        else if (a == "--script") o.script = next_path();
        else if (a == "--max-waits") o.max_waits = std::atol(next().c_str());
        else if (a == "--timeout-ms") o.timeout_ms = std::atol(next().c_str());
        else if (a == "--work-dir") o.work_dir = next_path();
        else if (a == "--exe-dir") o.virtual_exe_dir = next_path();
        else if (a == "--screenshot") o.screenshot = next_path();
        else if (a == "--window-capture") o.window_capture = next_path();
        else if (a == "--report") o.report = next_path();
        else if (a == "--font") o.fonts.push_back(next());
        else if (a == "--no-text-aa") o.text_antialias = false;
        else if (a == "--trace-commands") o.trace_commands = true;
        else if (a == "--game-root") o.game_root = next_path();
        else if (a == "--user-root") o.user_root = next_path();
        else if (a == "--list-dll") o.list_dll = true;
        else if (a == "--packed-dir") o.packed_dir = next_path();
        else if (a == "--soundfont") o.soundfont = next_path();
        else if (a == "--seed-tick") o.seed_tick = std::atoll(next().c_str());
        else if (a == "--virtual-epoch") o.virtual_epoch = std::atoll(next().c_str());
        else { usage(); return 2; }
    }
    if (o.ax.empty() || (o.trusted_fixture == o.game)) { usage(); return 2; }
    if (o.scripted_realtime && (o.script.empty() || o.deterministic)) {
        std::fprintf(stderr, "--script-realtime requires --script and cannot be combined with --deterministic\n");
        return 2;
    }
    if (o.game && !o.list_dll && (o.game_root.empty() || o.user_root.empty() || o.work_dir.empty())) {
        std::fprintf(stderr, "--game requires --game-root, --user-root and --work-dir\n");
        return 2;
    }
    // Make output paths independent of --work-dir.
    for (auto* p : {&o.script, &o.screenshot, &o.window_capture, &o.report, &o.game_root, &o.user_root, &o.work_dir, &o.packed_dir, &o.soundfont, &o.translation_catalog, &o.translation_misses})
        if (!p->empty()) *p = std::filesystem::absolute(*p);
    return elona::app::run_host(o);
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    return host_main(std::vector<std::filesystem::path>(argv, argv + argc));
}
#else
int main(int argc, char** argv) {
    return host_main(std::vector<std::filesystem::path>(argv, argv + argc));
}
#endif

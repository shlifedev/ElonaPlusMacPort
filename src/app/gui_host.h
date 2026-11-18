#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace elona::app {

struct HostOptions {
    std::filesystem::path ax;            // current-format (112-byte header) AX
    bool trusted_fixture = false;        // reviewed fixture: reject DLL declarations
    bool game = false;                   // staged game: DLL calls dispatched to native handlers
    bool headless = false;               // no window (pixel model + text only)
    bool deterministic = false;          // virtual clock; input only from --script
    bool scripted_realtime = false;      // scripted input/dialogs with real clock/audio
    bool hidden_window = false;          // create real SDL windows but keep them hidden
    bool auxiliary_close_hide = false;   // opt-in: dismiss operation-help screen 20 without ending game
    std::filesystem::path work_dir;      // working directory for the VM (scratch / staged copy)
    std::filesystem::path script;        // deterministic input script
    std::filesystem::path screenshot;    // BMP of screen 0 when the program stops/ends
    std::filesystem::path window_capture;  // BMP read back from the presented window at exit
    std::filesystem::path report;        // JSON run report
    std::filesystem::path virtual_exe_dir;  // dirinfo(1)/dir_exe (defaults to work_dir)
    long max_waits = -1;                 // stop after N wait/await/stop iterations
    long timeout_ms = 0;                 // wall-clock bound (0 = none)
    std::vector<std::string> fonts;
    bool text_antialias = true;
    bool trace_commands = false;
    bool fail_on_hsp_error = false;      // diagnostic: stop before script onerror recovery
    // --game: read-only original install + writable private user root, routed
    // through elona_platform's GameFileSystem (never writes the game root).
    std::filesystem::path game_root;
    std::filesystem::path user_root;
    std::filesystem::path packed_dir;     // extracted DPM members (scene1.hsp, scene2.hsp)
    bool list_dll = false;
    std::filesystem::path soundfont;     // SF2 for MIDI (default: build-time GeneralUser GS)
    std::filesystem::path translation_catalog;  // display-only UTF-8 translations
    std::filesystem::path translation_fallback; // optional draft, hashed display keys
    std::filesystem::path translation_misses;   // optional private display audit
    // Deterministic clock sources: randomize default seed = seed_tick + virtual ms
    // (GetTickCount analogue); gettime = virtual_epoch (UTC seconds) + virtual ms.
    long long seed_tick = 1000000;
    long long virtual_epoch = 1767225600;  // 2026-01-01 00:00:00 UTC               // print DLL declarations (library!symbol, mptypes) and exit
};

int run_host(const HostOptions& options);

}  // namespace elona::app

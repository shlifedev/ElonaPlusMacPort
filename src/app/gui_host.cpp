// Native HSP3 GUI host for ElonaPlus.
//
// Registers its own handlers for TYPE_EXTCMD (GUI commands), TYPE_EXTSYSVAR
// (ginfo/mousex/...), and TYPE_DLLFUNC on top of the tested OpenHSP core.
// Command semantics follow OpenHSP src/hsp3/win32gui/hsp3gr_wingui.cpp (the
// Win32 runtime the original game used); pixel work is delegated to the CPU
// DIB model in src/graphics. Any command, option or DLL function that is not
// implemented produces an "ELONA-UNSUPPORTED" diagnostic with the source line
// and stops the program with an HSP error; nothing is silently accepted.
#include "gui_host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "graphics/hsp_screen.h"
#include "graphics/image_io.h"
#include "graphics/poly_raster.h"
#include "graphics/ui_text_codec.h"
#ifdef ELONA_HAVE_TTF
#include "graphics/text_renderer_ttf.h"
#endif
#include "hsp3.h"
#include "hsp3code.h"
#include "hsp3config.h"
#include "hsp3debug.h"
#include "stack.h"
#include "dpmread.h"
#include "native_dll.h"
#ifdef ELONA_LEGACY_RNG
#include "legacy_rng.h"
#endif
#include <ctime>
#include "platform/keymap_vk.h"
#ifdef ELONA_HAVE_GAME_FILES
#include "platform/game_files.h"
#include "platform/game_files_hsp.h"
#include "platform/encoding.h"
#endif
#include "platform/window_display.h"
#include "platform/audio_out.h"
#ifdef ELONA_HAVE_MUSIC
#include "audio/music.h"
#endif

namespace elona::app {
void register_plugin_bindings();  // dll_bindings_plugins.cpp
void register_win32_bindings();   // dll_bindings_win32.cpp
void register_media_bindings();   // dll_bindings_media.cpp
void register_gfx_bindings();     // dll_bindings_gfx.cpp
void register_water_bindings();   // dll_bindings_gfx.cpp
void register_elona_bindings();   // dll_bindings_gfx.cpp
namespace {

using gfx::Screen;

// A compile stamp identifies which host produced a private crash/run report.
// It is a compiler timestamp (not a source revision or a UTC timestamp).
constexpr const char* kHostBuildStamp = __DATE__ " " __TIME__;
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr const char* kHostArchitecture = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
constexpr const char* kHostArchitecture = "x86_64";
#else
constexpr const char* kHostArchitecture = "other";
#endif

const char* const kExtCmdNames[] = {
    "button", "chgdisp", "exec", "dialog", "(04)", "(05)", "(06)", "(07)", "mmload", "mmplay", "mmstop", "mci",
    "pset", "pget", "syscolor", "mes", "title", "pos", "circle", "cls", "font", "sysfont", "objsize", "picload",
    "color", "palcolor", "palette", "redraw", "width", "gsel", "gcopy", "gzoom", "gmode", "bmpsave", "hsvcolor",
    "getkey", "listbox", "chkbox", "combox", "input", "mesbox", "buffer", "screen", "bgscr", "mouse", "objsel",
    "groll", "line", "clrobj", "boxf", "objprm", "objmode", "stick", "grect", "grotate", "gsquare", "gradf",
    "objimage", "objskip", "objenable", "celload", "celdiv", "celput", "gfilter", "setreq", "getreq", "mmvol",
    "mmpan", "mmstat", "mtlist", "mtinfo", "devinfo", "devinfoi", "devprm", "devcontrol", "httpload", "httpinfo",
    "objcolor", "rgbcolor", "viewcalc", "layerobj"};

std::string extcmd_name(int cmd) {
    if (cmd >= 0 && cmd < int(sizeof(kExtCmdNames) / sizeof(kExtCmdNames[0]))) return kExtCmdNames[cmd];
    char b[32];
    std::snprintf(b, sizeof b, "extcmd 0x%02x", cmd);
    return b;
}

struct ScriptAction {
    std::string op;
    std::vector<std::string> args;
    int line = 0;
};

// Portable model of the Win32 child controls created by HSP input/mesbox.
// Text is kept as CP932 bytes (the ANSI edit text HSP exchanges with the
// variable); EN_UPDATE semantics copy the whole text into the variable after
// every change, as Object_StrInput/Object_MesboxInput do.
struct EditObject {
    int kind = 0;           // 0 deleted, 1 input (single line), 2 mesbox (multi line)
    PVal* pval = nullptr;
    APTR aptr = 0;
    int vartype = HSPVAR_FLAG_STR;
    std::string text;       // CP932 bytes
    std::size_t caret = 0;  // byte offset, always on a character boundary
    bool select_all = false;
    int limit = 0;          // CP932 byte budget; never split a double-byte character
    std::string preedit;    // CP932 display only; never copied into the HSP variable
    bool composing = false;
    std::size_t preedit_start = 0, preedit_end = 0;  // CP932 offsets of IME selection
    std::string input_error;  // visible rejection, also recorded in the run report
    bool readonly = false;
    int x = 0, y = 0, w = 0, h = 0;
    int exinfo1 = 0;        // mesbox: selection start after last update
    gfx::FontSpec font;     // objmode 2: current font; otherwise DEFAULT_GUI_FONT approximation
};

struct Host : HostServices {
    ~Host() override { audio.reset(); }  // stop callbacks before MusicPlayer is destroyed
    HostOptions opt;
    HSPCTX* ctx = nullptr;
    int* type = nullptr;
    int* val = nullptr;
    std::unique_ptr<gfx::TextRenderer> text;
    std::unique_ptr<gfx::ScreenTable> screens;
    Screen* cur = nullptr;
    int cur_window = 0;
    std::unique_ptr<platform::Display> display;
    int fullscreen_width = 0, fullscreen_height = 0;  // logical desktop requested by chgdisp
    gfx::PolyRaster poly;

    // input
    bool key[256] = {};
    bool script_key_release[256] = {};
    int cklast = 0;
    int mouse_screen_x = 0, mouse_screen_y = 0;
    bool focus = true;
    int cursor_shown = 1;

    // time
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::int64_t virtual_ms = 0;

    // run bookkeeping
    long frame = 0;  // number of wait/await/stop entries processed
    std::multimap<long, ScriptAction> script;
    std::string exit_reason = "end";
    int forced_exit_code = -1;
    std::string first_unsupported;
    struct ObservedError { int code; int line; long pc_words; long frame; };
    std::vector<ObservedError> observed_errors;
    unsigned observed_error_count = 0;
    struct DialogEvent {
        int mode = 0, result = -1;
        long frame = 0, pc_words = -1;
        bool interactive = false, completed = false;
        std::string title, text, error;
    };
    std::vector<DialogEvent> dialogs;  // last 64; full game text, private report
    unsigned dialog_count = 0;
    int vm_return_mode = -1;
    std::vector<std::string> recent_input_events;
    long end_pc_words = -1;
    unsigned close_requests = 0;
    unsigned hidden_help_close_requests = 0;
    std::vector<std::string> notes;
    std::map<std::string, long> command_counts;
    std::string last_error;
    std::string dir_buffer;
    int reffunc_value = 0;
    bool quit_requested = false;
    std::unique_ptr<platform::AudioOut> audio;
    struct SoundBank { int flag = 0; int opt = 0; std::vector<std::uint8_t> wav; std::string fname; };
    std::map<int, SoundBank> banks;  // MMMan banks: flag 1 = in-memory WAV, 2 = MCI voice (MIDI/MP3/large WAV)
    int curmus = -1;
#ifdef ELONA_HAVE_MUSIC
    std::shared_ptr<const audio::SoundFont> soundfont;
    audio::MusicPlayer music;
    bool music_output = false;  // a real device renders the player
#endif
    bool numlock_on = true;
    std::vector<int> dialog_answers;  // deterministic yes/no answers (6/7)  // Windows toggle state emulated for GetKeyboardState/keybd_event
    bool midi_noted = false;
    std::map<int, std::vector<EditObject>> objects;  // per screen id
    std::map<int, int> focus_obj;                            // per screen id, -1 none
#ifdef ELONA_HAVE_GAME_FILES
    std::unique_ptr<platform::GameFileSystem> files;
#endif

    // ---- HostServices
    bool surface(int id, std::uint8_t*& bits, int& w, int& h, int& stride) override {
        Screen* s = screens->get(id);
        if (!s) return false;
        bits = s->dib.data();
        w = s->width();
        h = s->height();
        stride = s->dib.stride();
        return true;
    }
    bool surface_cursor(int id, int& x, int& y) override {
        Screen* s = screens->get(id);
        if (!s) return false;
        x = s->cx;
        y = s->cy;
        return true;
    }
    bool surface_state(int id, int& gx, int& gy, int& gmode) override {
        Screen* s = screens->get(id);
        if (!s) return false;
        gx = s->gx;
        gy = s->gy;
        gmode = s->gmode;
        return true;
    }
    int active_surface() override { return cur_window; }
    void mark_surface_dirty(int id) override {
        if (Screen* s = screens->get(id)) s->mark_dirty();
    }
    std::int32_t tick_ms() override {
        if (opt.deterministic) return std::int32_t(virtual_ms);
        return std::int32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }
    Resolve resolve_path(std::string_view cp932, bool for_write, std::filesystem::path& out, std::string& error) override;
    bool begin_stream_write(std::string_view cp932, std::filesystem::path& out, std::string& error) override;
    bool end_stream_write(std::string_view cp932, std::string& error) override;
    void note(const std::string& m) override {
        notes.push_back(m);
        std::fprintf(stderr, "elona-host: %s\n", m.c_str());
    }
};

Host* H = nullptr;
bool safe_relative(const std::string& name);

HostServices::Resolve Host::resolve_path(std::string_view cp932, bool for_write, std::filesystem::path& out, std::string& error) {
#ifdef ELONA_HAVE_GAME_FILES
    if (files) {
        platform::FileDiagnostic d;
        auto e = for_write ? files->resolve_write(cp932, out, &d) : files->resolve_read(cp932, out, &d);
        if (e == platform::FileError::ok) return Resolve::ok;
        error = d.detail.empty() ? "file policy rejected path" : d.detail;
        return e == platform::FileError::not_found ? Resolve::not_found : Resolve::rejected;
    }
#endif
    (void)for_write;
    std::string s(cp932);
    if (!safe_relative(s)) { error = "fixture resolver accepts ASCII relative paths only"; return Resolve::rejected; }
    std::replace(s.begin(), s.end(), '\\', '/');
    out = std::filesystem::current_path() / s;
    return Resolve::ok;
}

bool Host::begin_stream_write(std::string_view cp932, std::filesystem::path& out, std::string& error) {
#ifdef ELONA_HAVE_GAME_FILES
    if (files) {
        platform::FileDiagnostic d;
        auto e = files->begin_stream_write(cp932, out, &d);
        if (e == platform::FileError::ok) return true;
        error = d.detail;
        return false;
    }
#endif
    return resolve_path(cp932, true, out, error) == Resolve::ok;
}

bool Host::end_stream_write(std::string_view cp932, std::string& error) {
#ifdef ELONA_HAVE_GAME_FILES
    if (files) {
        platform::FileDiagnostic d;
        if (files->end_stream_write(cp932, &d) == platform::FileError::ok) return true;
        error = d.detail;
        return false;
    }
#endif
    (void)cp932;
    (void)error;
    return true;
}

std::string where() {
    if (!H || !H->ctx) return "line=? (before VM start)";
    char b[512];
    int line = code_getdebug_line();  // also updates the debug file name
    const char* name = code_getdebug_name();
    std::snprintf(b, sizeof b, "line=%d file=%s", line, name ? name : "?");
    return b;
}

[[noreturn]] void unsupported(const std::string& kind, const std::string& detail) {
    std::string msg = kind + ": " + detail + " " + where();
    if (H->first_unsupported.empty()) H->first_unsupported = msg;
    std::fprintf(stderr, "ELONA-UNSUPPORTED %s\n", msg.c_str());
    // Terminate instead of raising an HSP error: the game's own `onerror`
    // handler would otherwise swallow the failure and continue on a diverged
    // path (observed: error handler re-entering the same unimplemented DLL).
    H->exit_reason = "unsupported";
    throw HSPERR_NONE;
}

Screen& screen_safe(int id) {
    Screen* s = H->screens->get(id);
    if (!s) throw HSPERR_ILLEGAL_FUNCTION;  // HspWnd::GetBmscrSafe
    return *s;
}

std::string to_utf8(std::string_view cp932) {
    std::string out;
    gfx::ui_cp932_to_utf8(cp932, out);
    return out;
}

// ---------------------------------------------------------------- presentation

bool is_window(const Screen& s) { return s.type == gfx::kTypeMain || s.type == gfx::kTypeBgscr; }

void draw_edit_overlay(gfx::Dib& d, const struct EditObject& o, bool focused, int wid);
bool has_visible_objects(int wid);

void present(Screen& s) {
    if (!is_window(s) || s.hidden) return;
    if (!has_visible_objects(s.wid)) {
        H->display->present(s.wid, s.dib, s.viewx, s.viewy, s.wx, s.wy);
    } else {
        // Win32 child controls are separate windows drawn over the client area,
        // not part of the screen bitmap (bmpsave/pget never see them).
        gfx::Dib composed = s.dib;
        auto& v = H->objects[s.wid];
        int focus = H->focus_obj.count(s.wid) ? H->focus_obj[s.wid] : -1;
        for (int i = 0; i < int(v.size()); ++i)
            if (v[i].kind) draw_edit_overlay(composed, v[i], i == focus && H->focus, s.wid);
        H->display->present(s.wid, composed, s.viewx, s.viewy, s.wx, s.wy);
    }
    s.dirty = false;
}

void present_dirty() {
    for (int i = 0; i < H->screens->size(); ++i) {
        Screen* s = H->screens->get(i);
        if (s && s->dirty && is_window(*s) && s->fl_udraw) present(*s);
    }
}

void configure_window(Screen& s, bool show) {
    platform::WindowSpec spec;
    spec.wid = s.wid;
    spec.width = s.wx;
    spec.height = s.wy;
    spec.x = s.window_x;
    spec.y = s.window_y;
    spec.borderless = s.type == gfx::kTypeBgscr;
    spec.visible = show && !H->opt.hidden_window;
    spec.title_utf8 = s.title_cp932.empty() ? "ElonaPlus Native" : to_utf8(s.title_cp932);
    std::string err;
    if (!H->display->configure_window(spec, err)) {
        H->note("window configuration failed: " + err);
        unsupported("window", err);
    }
    s.hidden = !show;
}

// ---------------------------------------------------------------- resources

bool safe_relative(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    for (unsigned char c : name)
        if (c >= 0x80) return false;  // non-ASCII CP932 paths need the platform file resolver
    std::string norm = name;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::stringstream ss(norm);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (part == "..") return false;
    return true;
}

std::filesystem::path resolve_in_workdir(const std::string& cp932, const char* what, bool for_write = false) {
#ifdef ELONA_HAVE_GAME_FILES
    if (H->files) {
        std::filesystem::path out;
        std::string err;
        auto r = H->resolve_path(cp932, for_write, out, err);
        if (r == HostServices::Resolve::rejected) unsupported("file policy", std::string(what) + " '" + to_utf8(cp932) + "': " + err);
        if (r == HostServices::Resolve::not_found) return {};
        return out;
    }
#endif
    // Fixture resolver: ASCII-only relative paths inside the VM working directory.
    if (!safe_relative(cp932)) unsupported("file path", std::string(what) + " path not resolvable without platform file resolver: '" + to_utf8(cp932) + "'");
    std::string norm = cp932;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    return std::filesystem::current_path() / norm;
}

bool read_bytes(const std::filesystem::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

std::string lower_ext(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = name.substr(dot);
    for (auto& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

int picload(int wid, const std::string& fname, int mode) {
    // HspWnd::Picload. mode 0: resize to image, 1: draw at pos, 2: resize + black clear
    Screen* bm = H->screens->get(wid);
    if (!bm) return 1;
    std::vector<std::uint8_t> bytes;
    auto path = resolve_in_workdir(fname, "picload");
    if (path.empty() || !read_bytes(path, bytes) || bytes.empty()) return 1;
    gfx::DecodedImage img;
    std::string err;
    if (!gfx::decode_image(bytes, img, err)) unsupported("picload decode", fname + ": " + err);
    std::string ext = lower_ext(fname);
    bool stb_alpha = ext == ".png" || ext == ".psd" || ext == ".tga";
    if (mode == 0 || mode == 2) {
        int type = bm->type;
        std::string title = bm->title_cp932;
        bm = &H->screens->make(wid, type, img.width, img.height, 0);
        bm->title_cp932 = title;
        if (mode == 2) bm->cls(4);
        if (wid == H->cur_window) H->cur = bm;
        if (is_window(*bm)) configure_window(*bm, true);
    }
    // OleLoadPicture path (BMP/JPEG/GIF) renders opaque pixels; stb path blends alpha.
    bm->render_rgba(img.width, img.height, stb_alpha ? img.components : 3, img.rgba.data());
    bm->resname = fname;
    return 0;
}


// ---------------------------------------------------------------- objects

void clear_objects(int wid) {
    H->objects[wid].clear();
    H->focus_obj[wid] = -1;
    H->display->set_text_input(wid, false);
}

EditObject& object_safe(Screen& b, int id) {
    auto& v = H->objects[b.wid];
    if (id < 0 || id >= int(v.size()) || v[id].kind == 0) throw HSPERR_ILLEGAL_FUNCTION;
    return v[id];
}

void edit_dirty(EditObject& o) {
    for (auto& [wid, v] : H->objects)
        for (auto& e : v)
            if (&e == &o) if (Screen* sc = H->screens->get(wid)) sc->mark_dirty();
}

void edit_cancel_composition(EditObject& o) {
    o.preedit.clear();
    o.composing = false;
    o.preedit_start = o.preedit_end = 0;
    edit_dirty(o);
}

void edit_sync_var(EditObject& o) {
    edit_dirty(o);
    // EN_UPDATE: GetWindowText -> variable (empty text is stored via int 0, i.e. "")
    if (o.vartype == HSPVAR_FLAG_STR) {
        code_setva(o.pval, o.aptr, HSPVAR_FLAG_STR, o.text.c_str());
    } else if (o.vartype == HSPVAR_FLAG_INT) {
        int v = std::atoi(o.text.c_str());
        code_setva(o.pval, o.aptr, HSPVAR_FLAG_INT, &v);
    } else if (o.vartype == HSPVAR_FLAG_DOUBLE) {
        double v = std::atof(o.text.c_str());
        code_setva(o.pval, o.aptr, HSPVAR_FLAG_DOUBLE, &v);
    }
    if (o.kind == 2) o.exinfo1 = int(o.caret);
}

std::size_t prev_char_start(const std::string& t, std::size_t pos) {
    std::size_t i = 0, last = 0;
    while (i < pos) {
        last = i;
        unsigned char c = static_cast<unsigned char>(t[i]);
        bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
        if (c == '\r' && i + 1 < t.size() && t[i + 1] == '\n') i += 2;
        else i += (lead && i + 1 < t.size()) ? 2 : 1;
    }
    return last;
}

std::size_t next_char_end(const std::string& t, std::size_t pos) {
    if (pos >= t.size()) return t.size();
    unsigned char c = static_cast<unsigned char>(t[pos]);
    bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
    if (c == '\r' && pos + 1 < t.size() && t[pos + 1] == '\n') return pos + 2;
    return std::min(t.size(), pos + ((lead && pos + 1 < t.size()) ? 2 : 1));
}

bool edit_insert(EditObject& o, const std::string& bytes) {
    if (o.readonly) return false;
    std::string base = o.text;
    std::size_t caret = o.caret;
    if (o.select_all) { base.clear(); caret = 0; }
    if (o.limit > 0 && base.size() + bytes.size() > std::size_t(o.limit)) return false;  // EN_MAXTEXT, no update
    base.insert(caret, bytes);
    o.text = base;
    o.caret = caret + bytes.size();
    o.select_all = false;
    o.input_error.clear();
    edit_sync_var(o);
    return true;
}

// WM_CHAR delivered to the focused edit control
void edit_char(EditObject& o, int ch) {
    switch (ch) {
    case 8:  // backspace
        if (o.readonly) return;
        if (o.select_all) { o.text.clear(); o.caret = 0; o.select_all = false; edit_sync_var(o); return; }
        if (o.caret == 0) return;
        { std::size_t start = prev_char_start(o.text, o.caret); o.text.erase(start, o.caret - start); o.caret = start; }
        edit_sync_var(o);
        return;
    case 13:  // single-line edits (MyEditProc) swallow CR; ES_WANTRETURN inserts CRLF
        if (o.kind == 2) edit_insert(o, "\r\n");
        return;
    case 9:
        if (o.kind == 2) edit_insert(o, "\t");
        return;
    case 1:  // Ctrl+A in mesbox (MyEditProc2): select all
        if (o.kind == 2) { o.select_all = true; edit_dirty(o); }
        return;
    case 27:
        return;
    default:
        if (ch >= 32 && ch < 256) edit_insert(o, std::string(1, char(ch)));
    }
}

void edit_key(EditObject& o, int vk) {
    switch (vk) {
    case 0x25: o.caret = prev_char_start(o.text, o.caret); o.select_all = false; break;  // LEFT
    case 0x27: o.caret = next_char_end(o.text, o.caret); o.select_all = false; break;    // RIGHT
    case 0x24: o.caret = 0; o.select_all = false; break;                                  // HOME
    case 0x23: o.caret = o.text.size(); o.select_all = false; break;                      // END
    case 0x2e:                                                                            // DELETE
        if (o.readonly) break;
        if (o.select_all) { o.text.clear(); o.caret = 0; o.select_all = false; }
        else if (o.caret < o.text.size()) { std::size_t e = next_char_end(o.text, o.caret); o.text.erase(o.caret, e - o.caret); }
        else break;
        edit_sync_var(o);
        break;
    default: break;
    }
    edit_dirty(o);
}

EditObject* focused_edit(int wid) {
    auto it = H->focus_obj.find(wid);
    if (it == H->focus_obj.end() || it->second < 0) return nullptr;
    auto& v = H->objects[wid];
    if (it->second < int(v.size()) && v[it->second].kind) return &v[it->second];
    return nullptr;
}

bool edit_encode(const std::string& utf8, std::string& cp932) {
#ifdef ELONA_HAVE_GAME_FILES
    // macOS may deliver decomposed kana, which must be NFC before the strict
    // Windows CP932 conversion. This changes only the UI boundary, not VM text.
    std::string nfc;
    return platform::utf8_to_nfc(utf8, nfc) && platform::utf8_to_cp932(nfc, cp932);
#else
    for (unsigned char c : utf8) if (c >= 0x80) return false;
    cp932 = utf8;
    return true;
#endif
}

void edit_reject(EditObject& o, const char* why) {
    o.input_error = why;
    H->note(std::string("text input rejected: ") + why);
    edit_dirty(o);
}

// SDL composition start/length count Unicode characters, not UTF-8 bytes.
// Convert the prefix independently after NFC so decomposed kana remain whole.
std::size_t preedit_offset(const std::string& utf8, int chars) {
    std::size_t end = 0;
    for (int n = 0; n < std::max(0, chars) && end < utf8.size(); ++n) {
        ++end;
        while (end < utf8.size() && (static_cast<unsigned char>(utf8[end]) & 0xc0) == 0x80) ++end;
    }
    std::string cp932;
    return edit_encode(utf8.substr(0, end), cp932) ? cp932.size() : 0;
}

void edit_preedit(int wid, const platform::InputEvent& e) {
    EditObject* o = focused_edit(wid);
    if (!o || o->readonly) return;
    edit_cancel_composition(*o);
    o->input_error.clear();
    if (e.text_utf8.empty()) return;  // cancel/finished: leave committed text untouched
    o->composing = true;
    if (!edit_encode(e.text_utf8, o->preedit)) {
        o->input_error = "Cannot enter this character (CP932)";
        return;
    }
    int start = std::min<int>(e.text_utf8.size(), std::max(0, e.edit_start));
    int len = std::min<int>(e.text_utf8.size() - start, std::max(0, e.edit_length));
    o->preedit_start = std::min(o->preedit.size(), preedit_offset(e.text_utf8, start));
    o->preedit_end = std::max(o->preedit_start, std::min(o->preedit.size(), preedit_offset(e.text_utf8, start + len)));
}

void edit_text_utf8(int wid, const std::string& utf8) {
    EditObject* o = focused_edit(wid);
    if (!o || o->readonly) return;
    edit_cancel_composition(*o);
    std::string cp932;
    if (!edit_encode(utf8, cp932)) {
        edit_reject(*o, "Cannot enter this character (CP932)");
        return;
    }
    // A text event is atomic: a rejected candidate must not leave a
    // truncated Japanese name or erase the selection it would have replaced.
    std::size_t base_size = o->select_all ? 0 : o->text.size();
    if (o->limit > 0 && base_size + cp932.size() > std::size_t(o->limit)) {
        edit_reject(*o, "Text limit reached");
        return;
    }
    // WM_CHAR per byte pair: insert character by character so limits apply per char.
    for (std::size_t i = 0; i < cp932.size();) {
        unsigned char c = static_cast<unsigned char>(cp932[i]);
        bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
        std::size_t n = (lead && i + 1 < cp932.size()) ? 2 : 1;
        if (n == 1) edit_char(*o, c);
        else if (!edit_insert(*o, cp932.substr(i, 2))) edit_reject(*o, "Text limit reached");
        i += n;
    }
}


bool has_visible_objects(int wid) {
    auto it = H->objects.find(wid);
    if (it == H->objects.end()) return false;
    for (auto& o : it->second)
        if (o.kind && o.w > 0 && o.h > 0) return true;
    return false;
}

// Classic Win32 edit appearance: WS_EX_CLIENTEDGE sunken 2px border, white
// client, black text, caret when focused. Glyph metrics come from the same
// text renderer as mes; this is a presentation approximation.
void draw_edit_overlay(gfx::Dib& d, const EditObject& o, bool focused, int wid) {
    if (o.w <= 0 || o.h <= 0) return;
    auto fill = [&](int x0, int y0, int x1, int y1, gfx::colorref c) {
        for (int y = std::max(0, y0); y < std::min(d.height(), y1); ++y)
            for (int x = std::max(0, x0); x < std::min(d.width(), x1); ++x) d.set(x, y, c);
    };
    int x = o.x, y = o.y, w = o.w, h = o.h;
    fill(x, y, x + w, y + h, gfx::rgb(255, 255, 255));
    fill(x, y, x + w, y + 1, gfx::rgb(160, 160, 160));
    fill(x, y, x + 1, y + h, gfx::rgb(160, 160, 160));
    fill(x + 1, y + 1, x + w - 1, y + 2, gfx::rgb(105, 105, 105));
    fill(x + 1, y + 1, x + 2, y + h - 1, gfx::rgb(105, 105, 105));
    fill(x, y + h - 1, x + w, y + h, gfx::rgb(255, 255, 255));
    fill(x + w - 1, y, x + w, y + h, gfx::rgb(255, 255, 255));
    gfx::TextRenderer* tr = H->text.get();
    if (!tr) return;
    // Clip text to the client area by drawing into a scratch DIB.
    int cw = std::max(1, w - 4), ch = std::max(1, h - 4);
    gfx::Dib client(cw, ch);
    client.fill(gfx::rgb(255, 255, 255));
    std::string shown = o.text;
    std::size_t caret = o.caret, comp_begin = 0, comp_end = 0;
    std::size_t sel_begin = o.select_all ? 0 : caret;
    std::size_t sel_end = o.select_all ? shown.size() : caret;
    if (focused && o.composing) {
        if (o.select_all) { shown.clear(); caret = 0; }
        const std::string& preedit = o.preedit.empty() && !o.input_error.empty() ? std::string("[CP932]") : o.preedit;
        shown.insert(caret, preedit);
        comp_begin = caret;
        comp_end = caret + preedit.size();
        sel_begin = caret + o.preedit_start;
        sel_end = caret + o.preedit_end;
        caret = sel_begin;
    }
    if (focused && !o.input_error.empty() && !o.composing) shown += " [" + o.input_error + "]";
    const gfx::colorref text_color = o.input_error.empty() ? gfx::rgb(0, 0, 0) : gfx::rgb(176, 0, 0);
    int ty = 1, caret_x = 1, caret_y = 1, line_h = 12;
    std::size_t line_start = 0;
    while (true) {
        std::size_t eol = shown.find("\r\n", line_start);
        std::size_t line_end = eol == std::string::npos ? shown.size() : eol;
        std::string line = shown.substr(line_start, line_end - line_start);
        gfx::TextExtent ext = line.empty() ? tr->measure_line(o.font, " ") : tr->draw_line(o.font, client, 1, ty, line, text_color);
        line_h = std::max(1, ext.cy);
        auto offset_x = [&](std::size_t p) { return 1 + (p == line_start ? 0 : tr->measure_line(o.font, shown.substr(line_start, p - line_start)).cx); };
        if (caret >= line_start && caret <= line_end) {
            caret_x = offset_x(caret);
            caret_y = ty;
        }
        if (focused && sel_end > line_start && sel_begin < line_end) {
            std::size_t a = std::max(sel_begin, line_start), b = std::min(sel_end, line_end);
            int ax = offset_x(a), bx = offset_x(b);
            for (int yy = ty; yy < ty + line_h; ++yy)
                for (int xx = ax; xx < bx; ++xx)
                    if (client.contains(xx, yy)) client.set(xx, yy, gfx::rgb(0, 90, 190));
            tr->draw_line(o.font, client, ax, ty, shown.substr(a, b - a), gfx::rgb(255, 255, 255));
        }
        if (focused && comp_end > line_start && comp_begin < line_end) {
            int ax = offset_x(std::max(comp_begin, line_start)), bx = offset_x(std::min(comp_end, line_end));
            for (int xx = ax; xx < bx; ++xx)
                if (client.contains(xx, ty + line_h - 1)) client.set(xx, ty + line_h - 1, gfx::rgb(0, 90, 190));
        }
        if (eol == std::string::npos) break;
        line_start = eol + 2;
        ty += line_h;
    }
    if (focused) {
        for (int yy = caret_y; yy < caret_y + line_h; ++yy)
            if (client.contains(caret_x, yy)) client.set(caret_x, yy, 0);
        Screen* s = H->screens->get(wid);
        H->display->set_text_input_area(wid, x + 2 - (s ? s->viewx : 0), y + 2 + caret_y - (s ? s->viewy : 0),
                                       cw, line_h, std::min(cw, caret_x));
    }
    for (int yy = 0; yy < ch; ++yy)
        for (int xx = 0; xx < cw; ++xx)
            if (d.contains(x + 2 + xx, y + 2 + yy)) d.set(x + 2 + xx, y + 2 + yy, client.get(xx, yy));
}

int add_input_object(Screen& b, PVal* pval, APTR aptr, int sx, int sy, const std::string& defval, int limit, int vartype, int mode) {
    auto& v = H->objects[b.wid];
    if (!H->focus_obj.count(b.wid)) H->focus_obj[b.wid] = -1;
    int id = int(v.size());
    for (int i = 0; i < int(v.size()); ++i) if (v[i].kind == 0) { id = i; break; }
    if (id == int(v.size())) v.emplace_back();
    EditObject& o = v[id];
    o = EditObject{};
    o.kind = (mode & 0x100) ? 2 : 1;
    o.pval = pval;
    o.aptr = aptr;
    o.vartype = vartype;
    o.limit = limit;
    o.readonly = (mode & 0x200) != 0;
    o.x = b.cx; o.y = b.cy; o.w = sx; o.h = sy;
    o.font = (b.objmode & 3) == 2 ? b.font : gfx::FontSpec{"", 12, 0, 17};
    b.posinc(sy);
    o.text = defval;  // SetWindowText -> EN_UPDATE
    o.caret = o.text.size();
    edit_sync_var(o);
    // focflg: the first input object of the window receives focus
    bool first = true;
    for (int i = 0; i < int(v.size()); ++i) if (i != id && v[i].kind) first = false;
    if (first) { H->focus_obj[b.wid] = id; H->display->set_text_input(b.wid, true); }
    return id;
}


// ---------------------------------------------------------------- multimedia (MMMan)

int mm_load(const std::string& fname, int num, int opt) {
    std::string ext = lower_ext(fname);
    if (fname.size() >= 3 && std::tolower((unsigned char)fname[0]) == 'c' && std::tolower((unsigned char)fname[1]) == 'd' && fname[2] == ':')
        unsupported("mmload", "CD audio");
    if (ext == ".avi" || ext == ".wmv" || ext == ".mpg") unsupported("mmload", "video " + ext);
    Host::SoundBank bank;
    bank.opt = opt;
    bank.fname = fname;
    bank.flag = 2;
    if (ext == ".wav") {
        auto path = resolve_in_workdir(fname, "mmload");
        std::vector<std::uint8_t> bytes;
        if (path.empty() || !read_bytes(path, bytes)) return 1;  // dpm_exist == -1
        if (bytes.size() < 2000000) { bank.flag = 1; bank.wav = std::move(bytes); }
        else { bank.flag = 2; bank.wav = std::move(bytes); }
    }
    H->banks[num] = std::move(bank);
    return 0;
}

void mm_stop_all() {
    H->audio->stop();
#ifdef ELONA_HAVE_MUSIC
    H->music.stop();
#endif
    H->curmus = -1;
}

void mm_play(int num) {
    auto it = H->banks.find(num);
    if (it == H->banks.end()) return;  // Play() returns 1, ignored by mmplay
    auto& b = it->second;
    if (b.flag > 1 && H->curmus != -1) mm_stop_all();
    std::string ext = lower_ext(b.fname);
    if (!b.wav.empty()) {
        std::string err;
        int a = b.opt & 15;
        if (!H->audio->play_wav(b.wav, a == 1, err)) {
            // sndPlaySound/MCI failures are silent in the original; record them.
            H->note("mmplay " + b.fname + ": " + err);
            return;
        }
        if (b.flag > 1) H->curmus = num;
        if (a == 2) {  // SND_SYNC / MCI wait: block for the sound's duration
            int ms = H->audio->duration_ms(b.wav);
            if (ms > 0) {
                if (H->opt.deterministic) H->virtual_ms += ms;
                else std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
        }
        return;
    }
#ifdef ELONA_HAVE_MUSIC
    // MCI sequencer / MPEG audio: rendered by src/audio (SoundFont MIDI, MP3).
    // An MCI open/play failure is silent in the original (mmplay ignores the
    // result); keep that, but record the reason.
    {
        std::vector<std::uint8_t> bytes;
        auto path = resolve_in_workdir(b.fname, "mmplay");
        audio::LoadError lerr;
        std::unique_ptr<audio::MusicStream> stream;
        if (path.empty() || !read_bytes(path, bytes)) {
            lerr.message = "file not found";
        } else {
            stream = audio::open_music(std::move(bytes), H->soundfont, b.fname, lerr);
        }
        if (!stream) {
            H->note("mmplay " + to_utf8(b.fname) + ": MCI open failed (" + lerr.message + "); silent like the original");
            H->curmus = num;
            return;
        }
        int a = b.opt & 15;
        if (a == 2) unsupported("mmplay", "MCI wait mode (opt 2) for music");
        H->music.play(std::move(stream), a == 1);
        if (!H->music_output && !H->midi_noted) {
            H->note("mmplay " + to_utf8(b.fname) + ": music decoded and sequenced, but this run has no audio device (null output)");
            H->midi_noted = true;
        }
    }
#else
    if (!H->midi_noted) {
        H->note("mmplay " + b.fname + ": built without src/audio; MCI play ignored like a failed MCI device (music silent)");
        H->midi_noted = true;
    }
#endif
    H->curmus = num;
}

// ---------------------------------------------------------------- input

int vk_to_char(int vk) {
    // MapVirtualKey(vk, MAPVK_VK_TO_CHAR) for the US/JIS-common keys
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return vk;
    switch (vk) {
    case 0x08: case 0x09: case 0x0d: case 0x1b: case 0x20: return vk;
    case 0x6a: return '*'; case 0x6b: return '+'; case 0x6d: return '-'; case 0x6e: return '.'; case 0x6f: return '/';
    case 0xbc: return ','; case 0xbd: return '-'; case 0xbe: return '.'; case 0xbf: return '/';
    default: return (vk >= 0x60 && vk <= 0x69) ? '0' + (vk - 0x60) : 0;
    }
}

void handle_event(const platform::InputEvent& e) {
    using K = platform::InputEvent;
    Screen* s = H->screens->get(e.window);
    if (H->opt.trace_commands && e.kind != K::MouseMove) {
        // Bounded input metadata only: do not retain typed names or IME text.
        if (H->recent_input_events.size() == 64) H->recent_input_events.erase(H->recent_input_events.begin());
        static const char* kinds[] = {"KeyDown", "KeyUp", "MouseMove", "MouseDown", "MouseUp", "Wheel", "Text", "TextEditing", "CloseRequest", "FocusGained", "FocusLost"};
        H->recent_input_events.push_back("frame=" + std::to_string(H->frame) + " kind=" + kinds[int(e.kind)] +
            " window=" + std::to_string(e.window) + " vk=" + std::to_string(e.vk) +
            " repeat=" + std::to_string(e.repeat) + " text_bytes=" + std::to_string(e.text_utf8.size()));
    }
    switch (e.kind) {
    case K::KeyDown:
        if (EditObject* o = focused_edit(e.window); o && o->composing) {
            // Conversion keys belong to the IME, not to the game. In
            // particular RETURN must not both commit a name and accept it.
            if (e.vk == 0x1b) {
                edit_cancel_composition(*o);
                H->display->clear_composition(e.window);
            }
            break;
        }
        H->key[e.vk & 255] = true;
        if (EditObject* o = focused_edit(e.window)) {
            // TranslateMessage generates WM_CHAR for these keys; printable
            // characters arrive separately as text input.
            if (e.vk == 'A' && H->key[0x11]) edit_char(*o, 1);
            else if (e.vk == 0x08 || e.vk == 0x0d || e.vk == 0x09 || e.vk == 0x1b) edit_char(*o, e.vk);
            else edit_key(*o, e.vk);
        }
        if (code_isirq(HSPIRQ_ONKEY))
            code_sendirq(HSPIRQ_ONKEY, vk_to_char(e.vk), e.vk, 1 | (e.repeat ? 0x40000000 : 0));
        break;
    case K::KeyUp:
        H->key[e.vk & 255] = false;
        break;
    case K::MouseMove:
        if (s) { s->savepos[0] = short(e.x); s->savepos[1] = short(e.y); }
        H->mouse_screen_x = e.x + (s && !(H->fullscreen_width && s->wid == 0) ? std::max(0, s->window_x) : 0);
        H->mouse_screen_y = e.y + (s && !(H->fullscreen_width && s->wid == 0) ? std::max(0, s->window_y) : 0);
        break;
    case K::MouseDown:
    case K::MouseUp: {
        static const int vks[] = {0x01, 0x02, 0x04};
        H->key[vks[e.button % 3]] = e.kind == K::MouseDown;
        if (s) { s->savepos[0] = short(e.x); s->savepos[1] = short(e.y); }
        if (e.kind == K::MouseDown && code_isirq(HSPIRQ_ONCLICK))
            code_sendirq(HSPIRQ_ONCLICK, e.button, e.button == 0 ? 1 : e.button == 1 ? 2 : 16, (e.x & 0xffff) | (e.y << 16));
        break;
    }
    case K::Wheel:
        if (s) s->savepos[3] = short(e.wheel);
        break;
    case K::FocusGained:
        H->focus = true;
        if (EditObject* o = focused_edit(e.window)) edit_dirty(*o);
        break;
    case K::FocusLost:
        H->focus = false;
        std::fill(std::begin(H->key), std::end(H->key), false);
        if (EditObject* o = focused_edit(e.window)) edit_cancel_composition(*o);
        H->display->clear_composition(e.window);
        break;
    case K::CloseRequest: {
        static const char* sources[] = {"unknown", "application-quit", "window-close", "script"};
        ++H->close_requests;
        H->note("close request: window=" + std::to_string(e.window) + " frame=" + std::to_string(H->frame) +
                " onexit=" + std::to_string(code_isirq(HSPIRQ_ONEXIT)) + " source=" + sources[int(e.close_source)]);
        // 2.335's beginner-controls panel uses screen 20. The original global
        // onexit ends the game even when only this panel is dismissed. Native
        // launcher policy is opt-in and does not alter main-window/app quit.
        if (H->opt.auxiliary_close_hide && e.window == 20 &&
            e.close_source != K::CloseSource::application_quit && s && is_window(*s)) {
            H->display->hide_window(20);
            s->hidden = true;
            ++H->hidden_help_close_requests;
            H->note("operation-help window 20 hidden; game continues (--auxiliary-close-hide)");
            break;
        }
        if (code_isirq(HSPIRQ_ONEXIT)) {
            code_sendirq(HSPIRQ_ONEXIT, 0, e.window, 0);
        } else {
            H->exit_reason = "window-closed";
            throw HSPERR_NONE;
        }
        break;
    }
    case K::Text:
        edit_text_utf8(e.window, e.text_utf8);
        break;
    case K::TextEditing:
        edit_preedit(e.window, e);
        break;
    }
}

void pump_real_events(int timeout_ms) {
    std::vector<platform::InputEvent> events;
    H->display->poll(events, timeout_ms);
    if (H->opt.deterministic || H->opt.scripted_realtime) {
        // Deterministic runs never take the user's keyboard/mouse; only window
        // close requests are honoured.
        for (auto& e : events)
            if (e.kind == platform::InputEvent::CloseRequest) handle_event(e);
        return;
    }
    for (auto& e : events) {
        if ((e.kind == platform::InputEvent::KeyDown || e.kind == platform::InputEvent::KeyUp) && !H->numlock_on) {
            // NumLock off: the keypad produces navigation keys, as on Windows.
            static const int nav[10] = {0x2d, 0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21};
            if (e.vk >= 0x60 && e.vk <= 0x69) e.vk = nav[e.vk - 0x60];
            else if (e.vk == 0x6e) e.vk = 0x2e;
        }
        handle_event(e);
    }
}

void save_screenshot(const std::filesystem::path& path) {
    Screen* s = H->screens->get(0);
    if (!s) { H->note("screenshot skipped: screen 0 missing"); return; }
    std::string err;
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    if (!gfx::save_bmp(s->dib, path, &err)) H->note("screenshot failed: " + err);
}

bool capture_window(const std::filesystem::path& path) {
    std::vector<std::uint8_t> rgb;
    int w = 0, h = 0;
    std::string err;
    if (Screen* s = H->screens->get(0)) present(*s);
    if (!H->display->capture(0, rgb, w, h, err)) { H->note("window capture failed: " + err); return false; }
    gfx::Dib dib(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = &rgb[(std::size_t(y) * w + x) * 3];
            dib.set(x, y, gfx::rgb(p[0], p[1], p[2]));
        }
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    if (!gfx::save_bmp(dib, path, &err)) { H->note("window capture save failed: " + err); return false; }
    return true;
}

void apply_script(long frame) {
    auto range = H->script.equal_range(frame);
    // release keys pressed with `key` on the previous frame
    for (int i = 0; i < 256; ++i)
        if (H->script_key_release[i]) { H->key[i] = false; H->script_key_release[i] = false; }
    std::vector<ScriptAction> actions;
    for (auto it = range.first; it != range.second; ++it) actions.push_back(it->second);
    for (auto& a : actions) {
        auto arg = [&](std::size_t i) { return i < a.args.size() ? a.args[i] : std::string(); };
        if (a.op == "key" || a.op == "down" || a.op == "up") {
            int vk = platform::vk_from_name(arg(0));
            if (!vk) { H->note("script line " + std::to_string(a.line) + ": unknown key " + arg(0)); continue; }
            platform::InputEvent e{a.op == "up" ? platform::InputEvent::KeyUp : platform::InputEvent::KeyDown};
            e.vk = vk;
            handle_event(e);
            if (a.op != "up") {
                int ch = vk_to_char(vk);
                if (ch >= 'A' && ch <= 'Z' && !H->key[0x10]) ch = ch - 'A' + 'a';
                if (ch >= 32 && ch != 0x7f && !H->key[0x11] && !(focused_edit(e.window) && focused_edit(e.window)->composing)) {
                    platform::InputEvent t{platform::InputEvent::Text};
                    t.text_utf8 = std::string(1, char(ch));
                    handle_event(t);
                }
            }
            if (a.op == "key") H->script_key_release[vk & 255] = true;
        } else if (a.op == "dialog") {
            H->dialog_answers.push_back(arg(0) == "yes" ? 6 : 7);
        } else if (a.op == "text") {
            platform::InputEvent t{platform::InputEvent::Text};
            for (std::size_t i = 0; i < a.args.size(); ++i) t.text_utf8 += (i ? " " : "") + a.args[i];
            handle_event(t);
        } else if (a.op == "compose") {
            platform::InputEvent t{platform::InputEvent::TextEditing};
            t.edit_start = std::atoi(arg(0).c_str());
            t.edit_length = std::atoi(arg(1).c_str());
            for (std::size_t i = 2; i < a.args.size(); ++i) t.text_utf8 += (i > 2 ? " " : "") + a.args[i];
            handle_event(t);
        } else if (a.op == "focus") {
            platform::InputEvent t{arg(0) == "lost" ? platform::InputEvent::FocusLost : platform::InputEvent::FocusGained};
            handle_event(t);
        } else if (a.op == "close") {
            // Exercise the same onexit path as an OS window-close request;
            // unlike `exit`, the script may handle and cancel this request.
            platform::InputEvent t{platform::InputEvent::CloseRequest};
            t.window = a.args.empty() ? 0 : std::atoi(arg(0).c_str());
            t.close_source = platform::InputEvent::CloseSource::script;
            handle_event(t);
        } else if (a.op == "mouse" || a.op == "click") {
            platform::InputEvent e{platform::InputEvent::MouseMove};
            e.x = std::atoi(arg(0).c_str());
            e.y = std::atoi(arg(1).c_str());
            handle_event(e);
            if (a.op == "click") {
                e.kind = platform::InputEvent::MouseDown;
                handle_event(e);
                H->script_key_release[1] = true;
            }
        } else if (a.op == "screenshot") {
            present_dirty();
            save_screenshot(arg(0));
            H->note("screenshot frame " + std::to_string(frame) + " -> " + arg(0));
        } else if (a.op == "capture") {
            present_dirty();
            capture_window(arg(0));
        } else if (a.op == "exit") {
            H->exit_reason = "script-exit";
            H->forced_exit_code = a.args.empty() ? 0 : std::atoi(arg(0).c_str());
            throw HSPERR_NONE;
        } else {
            H->note("script line " + std::to_string(a.line) + ": unknown op " + a.op);
        }
    }
}

bool script_pending_after(long frame) { return H->script.upper_bound(frame) != H->script.end(); }

void check_bounds() {
    if (H->opt.max_waits >= 0 && H->frame > H->opt.max_waits) {
        H->exit_reason = "max-waits";
        throw HSPERR_NONE;
    }
    if (H->opt.timeout_ms > 0) {
        auto el = std::chrono::steady_clock::now() - H->start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(el).count() > H->opt.timeout_ms) {
            H->exit_reason = "timeout";
            throw HSPERR_NONE;
        }
    }
}

void begin_frame() {
    ++H->frame;
    if (H->audio) H->audio->update();
    present_dirty();
    check_bounds();
    if (H->opt.deterministic || H->opt.scripted_realtime) apply_script(H->frame);
}

void msgfunc(HSPCTX* hctx) {
    bool counted = false;
    while (true) {
        if (hctx->runmode == RUNMODE_LOGMES) {
            hctx->runmode = RUNMODE_RUN;
            return;
        }
        if ((hctx->runmode == RUNMODE_WAIT || hctx->runmode == RUNMODE_AWAIT || hctx->runmode == RUNMODE_STOP) && !counted) {
            counted = true;
            begin_frame();
            if (hctx->runmode == RUNMODE_RUN) return;  // an interrupt handler resumed execution
        }
        pump_real_events(0);
        switch (hctx->runmode) {
        case RUNMODE_STOP:
            if (H->opt.deterministic) {
                if (!script_pending_after(H->frame)) {
                    H->exit_reason = "stop-idle";
                    throw HSPERR_NONE;
                }
                H->virtual_ms += 16;
                counted = false;  // each idle step is a frame for the script
            } else {
                present_dirty();
                pump_real_events(50);
                check_bounds();
                if (H->opt.scripted_realtime) counted = false;
            }
            break;
        case RUNMODE_WAIT:
            hctx->runmode = code_exec_wait(H->tick_ms());
            [[fallthrough]];
        case RUNMODE_AWAIT: {
            int tick = H->tick_ms();
            if (code_exec_await(tick) != RUNMODE_RUN) {
                if (H->opt.deterministic) {
                    H->virtual_ms = std::max<std::int64_t>(H->virtual_ms, hctx->waittick);
                } else {
                    int remain = std::max(1, hctx->waittick - tick);
                    pump_real_events(std::min(remain, 50));
                }
            }
            break;
        }
        case RUNMODE_RETURN:
            throw HSPERR_RETURN_WITHOUT_GOSUB;
        case RUNMODE_INTJUMP:
            throw HSPERR_INTJUMP;
        case RUNMODE_ASSERT:
            hctx->runmode = RUNMODE_STOP;
            break;
        case RUNMODE_END:
            throw HSPERR_NONE;
        default:
            return;
        }
    }
}


void register_host_win32_bindings() {
    // user32 GetKeyboardState(var buf[256]): bit7 = key down, bit0 = toggle
    // (only NumLock/CapsLock/ScrollLock toggles are modelled; NumLock starts on).
    register_dll_handler("user32.dll", "GetKeyboardState", [](DllCall& c) -> std::int32_t {
        if (c.args.size() != 1 || c.args[0].kind != DllArgKind::VarRef) throw DllRefusal("unexpected argument layout");
        if (c.args[0].bytes_remaining < 256) throw DllRefusal("GetKeyboardState buffer smaller than 256 bytes");
        std::uint8_t* p = c.args[0].data;
        for (int i = 0; i < 256; ++i) p[i] = std::uint8_t((H->key[i] ? 0x80 : 0) | (i == 0x90 && H->numlock_on ? 1 : 0));
        return 1;
    });
    // user32 keybd_event(vk, scan, flags): injected key press/release.
    register_dll_handler("user32.dll", "keybd_event", [](DllCall& c) -> std::int32_t {
        if (c.args.size() != 3) throw DllRefusal("unexpected argument layout");
        int vk = c.args[0].i & 0xff, flags = c.args[2].i;
        if (flags & ~2) throw DllRefusal("keybd_event flags " + std::to_string(flags));
        platform::InputEvent e{(flags & 2) ? platform::InputEvent::KeyUp : platform::InputEvent::KeyDown};
        e.vk = vk;
        e.window = H->cur_window;
        if (!(flags & 2) && vk == 0x90) {
            H->numlock_on = !H->numlock_on;
            H->note(std::string("keybd_event: NumLock toggled ") + (H->numlock_on ? "on" : "off") + " (host keypad mapping follows)");
        }
        handle_event(e);
        return 0;  // void function
    });
}

// ---------------------------------------------------------------- commands

int get_ginfo(int arg) {
    Screen& b = *H->cur;
    int wxp = 0, wyp = 0, ww = b.wx, wh = b.wy;
    if (arg >= 4 && arg <= 11) {
        if (H->fullscreen_width && b.wid == 0) {
            // SDL input is mapped into logical client pixels at desktop origin (0,0).
            ww = b.wx;
            wh = b.wy;
        } else if (!H->display->window_rect(b.wid, wxp, wyp, ww, wh)) {
            wxp = std::max(b.window_x, 0);
            wyp = std::max(b.window_y, 0);
        }
    }
    switch (arg) {
    case 0: return H->mouse_screen_x;
    case 1: return H->mouse_screen_y;
    case 2: return H->focus ? 0 : -1;
    case 3: return H->cur_window;
    case 4: return wxp;
    case 5: return wyp;
    case 6: return wxp + ww;
    case 7: return wyp + wh;
    case 8: return b.viewx;
    case 9: return b.viewy;
    case 10: return ww;
    case 11: return wh;
    case 12: if (b.type != gfx::kTypeBuffer) return b.wx; [[fallthrough]];
    case 26: return b.width();
    case 13: if (b.type != gfx::kTypeBuffer) return b.wy; [[fallthrough]];
    case 27: return b.height();
    case 14: return b.printsize.cx;
    case 15: return b.printsize.cy;
    case 16: return gfx::red_of(b.color);
    case 17: return gfx::green_of(b.color);
    case 18: return gfx::blue_of(b.color);
    case 19: return 0;
    case 20: case 21: {
        if (H->fullscreen_width) return arg == 20 ? H->fullscreen_width : H->fullscreen_height;
        int w = 1920, h = 1080;
        if (!H->opt.deterministic) H->display->desktop_size(w, h);
        return arg == 20 ? w : h;
    }
    case 22: return b.cx;
    case 23: return b.cy;
    case 24: return H->ctx->intwnd_id;
    case 25: return H->screens->empty_id();
    default: unsupported("ginfo", std::to_string(arg));
    }
}

int get_alpha_operation(const Screen& b) {
    int alpha = b.gfrate;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) { alpha = 256; if (b.gmode >= 4) alpha = 255; }
    switch (b.gmode) {
    case 3: case 4: break;
    case 5: alpha |= 0x200; break;
    case 6: alpha |= 0x300; break;
    default: alpha = 0x100; break;
    }
    return alpha;
}

int cnv_rgb(gfx::colorref c) { return int((c & 0x00ff00) | ((c >> 16) & 0xff) | ((c & 0xff) << 16)); }

void grotate_sub(const Screen& src, int x, int y, int sx, int sy, int sizex, int sizey, double rot, int opx, int opy) {
    Screen& b = *H->cur;
    int tx0 = std::clamp(x, 0, src.width()), ty0 = std::clamp(y, 0, src.height());
    int tx1 = std::clamp(tx0 + sx - 1, 0, src.width()), ty1 = std::clamp(ty0 + sy - 1, 0, src.height());
    ty0 = src.height() - 1 - ty0;
    ty1 = src.height() - 1 - ty1;
    int attr = 0;
    if (b.gmode == 2) attr = gfx::kPolyAttrColorKey;
    if (b.gmode == 4) { attr = gfx::kPolyAttrColorKey; H->poly.set_color_key(b.color); }
    auto p = gfx::make_sprite_poly(attr, get_alpha_operation(b), b.cx, b.cy, sizex, sizey, rot, tx0, ty1, tx1, ty0, opx, opy);
    const gfx::Dib* tex = &src.dib;
    gfx::Dib copy;
    if (&src == &b) { copy = src.dib; tex = &copy; }  // rasteriser reads texture while writing
    auto r = H->poly.draw_tex(b.dib, *tex, p);
    if (r.skipped_out_of_range) H->note("grotate: " + std::to_string(r.skipped_out_of_range) + " texels outside texture skipped " + where());
    b.mark_dirty();
}

int cmd_screen(int cmd) {
    int p1 = code_getdi(0), p2 = code_getdi(640), p3 = code_getdi(480), p4 = code_getdi(0);
    int p5 = code_getdi(-1), p6 = code_getdi(-1), p7 = code_getdi(p2), p8 = code_getdi(p3);
    int t = cmd == 0x29 ? gfx::kTypeBuffer : cmd == 0x2b ? gfx::kTypeBgscr : gfx::kTypeMain;
    if (p4 & ~(2 | 4 | 8 | 16 | 32)) unsupported(extcmd_name(cmd), "mode flags " + std::to_string(p4));
    if (p4 & 1) unsupported(extcmd_name(cmd), "palette mode");
    Screen& b = H->screens->make(p1, t, p2, p3, p4);
    clear_objects(p1);
    b.width_request(p7, p8, p5, p6, 0);
    if (t == gfx::kTypeBuffer || b.type == gfx::kTypeBuffer) {
        H->cur = &b;
        H->cur_window = p1;
        return RUNMODE_RUN;
    }
    if (p5 >= 0) b.window_x = p5;
    if (p6 >= 0) b.window_y = p6;
    configure_window(b, !(p4 & 2));
    H->cur = &b;
    H->cur_window = p1;
    H->ctx->waitcount = 0;
    H->ctx->waittick = -1;
    H->ctx->runmode = RUNMODE_AWAIT;
    return RUNMODE_AWAIT;
}

// The original and managed gmes both render unmatched paragraphs one glyph at a time.
// Resolve the immediate HSP routine so ordinary one-character labels still translate.
bool drawing_in_routine(std::string_view name) {
    if (H->opt.translation_catalog.empty()) return false;
    for (auto* frame = stm_cur; frame != mem_stm;) {
        --frame;
        if (frame->type == TYPE_EX_SUBROUTINE) return false;
        if (frame->type == TYPE_EX_CUSTOMFUNC) {
            const auto* routine = reinterpret_cast<const HSPROUTINE*>(STM_GETPTR(frame));
            const int offset = routine->param->nameidx;
            return offset >= 0 && offset <= H->ctx->hsphed->max_ds - int(name.size()+1) &&
                   std::memcmp(H->ctx->mem_mds + offset, name.data(), name.size()) == 0 &&
                   H->ctx->mem_mds[offset + name.size()] == 0;
        }
    }
    return false;
}

std::string localize_dialog(std::string_view source) {
    if (!H->text || H->opt.translation_catalog.empty()) return std::string(source);
    std::string result;
    while (!source.empty()) {
        const auto end = source.find_first_of("\r\n");
        const auto line = source.substr(0, end);
        const auto first = line.find_first_not_of(" \t");
        const auto last = line.find_last_not_of(" \t");
        const std::string* translated = nullptr;
        if (first != std::string_view::npos) {
            translated = H->text->lookup_translation("@dialog/" + std::string(line.substr(first, last - first + 1)));
        }
        if (translated) result += *translated;
        else result.append(line);
        if (end == std::string_view::npos) break;
        const auto separators = source[end] == '\r' && end + 1 < source.size() && source[end + 1] == '\n' ? 2 : 1;
        result.append(source.substr(end, separators));
        source.remove_prefix(end + separators);
    }
    return result;
}

int cmdfunc_extcmd_body(int cmd) {
    Screen* b = H->cur;
    int p1, p2, p3, p4, p5, p6;
    switch (cmd) {
    case 0x01: {  // chgdisp operates on the main window even when an auxiliary screen is selected
        const int mode = code_getdi(0);
        const int width = code_getdi(640), height = code_getdi(480);
        H->ctx->stat = 1;
        if (mode == 0 && !H->fullscreen_width) break;
        if (mode != 0 && (mode != 1 || width <= 0 || height <= 0)) {
            H->note("chgdisp: unavailable mode " + std::to_string(mode) + " at " +
                    std::to_string(width) + "x" + std::to_string(height));
            break;
        }
        std::string error;
        // Screen 0 is configured before the VM starts. bgscr reuses its SDL window.
        if (H->display->has_windows() && !H->display->set_fullscreen(0, mode == 1, error)) {
            H->note("chgdisp: " + error);
            break;
        }
        H->fullscreen_width = mode ? width : 0;
        H->fullscreen_height = mode ? height : 0;
        H->ctx->stat = 0;
        break;
    }
    case 0x08: {  // mmload
        std::string fname = code_gets();
        p1 = code_getdi(0);
        p2 = code_getdi(0);
        if (mm_load(fname, p1, p2)) throw HSPERR_FILE_IO;
        break;
    }
    case 0x09:  // mmplay
        mm_play(code_getdi(0));
        break;
    case 0x0a: {  // mmstop
        p1 = code_getdi(-1);
        if (p1 < 0) { mm_stop_all(); break; }
        auto it = H->banks.find(p1);
        if (it == H->banks.end()) break;
        if (it->second.flag == 1) H->audio->stop();
        else if (H->curmus != -1) {
#ifdef ELONA_HAVE_MUSIC
            H->music.stop();  // StopBank(MCI): stop/close myid only, PCM keeps playing
#endif
            H->curmus = -1;
        }
        break;
    }
    case 0x03: {  // dialog
        const auto* dialog_pc = code_getpcbak();
        const long dialog_pc_words = dialog_pc ? long(dialog_pc - H->ctx->mem_mcs) : -1;
        std::string text = code_getdsi("");
        p1 = code_getdi(0);
        std::string title = code_getds("");
        p2 = code_getdi(0);
        if (p1 >= 64) break;  // Win32 runtime ignores these
        if (p1 & 16) unsupported("dialog", "file selection dialog (mode " + std::to_string(p1) + ")");
        if (p1 & 32) unsupported("dialog", "colour selection dialog");
        if (H->dialogs.size() == 64) H->dialogs.erase(H->dialogs.begin());
        Host::DialogEvent entry;
        entry.mode = p1;
        entry.frame = H->frame;
        entry.pc_words = dialog_pc_words;
        entry.interactive = !H->opt.deterministic && !H->opt.scripted_realtime;
        entry.title = localize_dialog(to_utf8(title));
        entry.text = localize_dialog(to_utf8(text));
        H->dialogs.push_back(std::move(entry));
        auto& dialog = H->dialogs.back();
        ++H->dialog_count;
        H->note("dialog[" + std::to_string(p1) + "] '" + dialog.title + "': " + dialog.text);
        H->note("dialog begin: frame=" + std::to_string(dialog.frame) + " pc_words=" + std::to_string(dialog.pc_words) +
                " source=" + (dialog.interactive ? "interactive" : "scripted"));
        bool yesno = (p1 & 2) != 0;
        int result = -1;
        if (H->opt.deterministic || H->opt.scripted_realtime) {
            if (!yesno) result = 1;  // IDOK: the only possible answer
            else if (!H->dialog_answers.empty()) { result = H->dialog_answers.front(); H->dialog_answers.erase(H->dialog_answers.begin()); }
            else {
                dialog.error = "yes/no dialog without a scripted answer ('dialog yes|no')";
                unsupported("dialog", dialog.error + ": " + dialog.text);
            }
        } else {
            platform::DialogLabels labels{localize_dialog("OK"), localize_dialog("Yes"), localize_dialog("No")};
            result = H->display->message_box(b->wid, dialog.text, dialog.title, (p1 & 1) != 0, yesno ? 1 : 0, labels);
            if (result < 0) {
                dialog.error = H->display->message_box_error();
                if (dialog.error.empty()) dialog.error = "message box returned without a selected button";
                H->note("dialog failure: " + dialog.error);
                unsupported("dialog", dialog.error);
            }
        }
        dialog.result = result;
        dialog.completed = true;
        H->note("dialog end: result=" + std::to_string(result) + " frame=" + std::to_string(H->frame) +
                " pc_words=" + std::to_string(dialog.pc_words));
        H->ctx->stat = result;
        break;
    }
    case 0x0c:  // pset
        p1 = code_getdi(b->cx);
        p2 = code_getdi(b->cy);
        b->pset(p1, p2);
        break;
    case 0x0d:  // pget
        p1 = code_getdi(b->cx);
        p2 = code_getdi(b->cy);
        b->pget(p1, p2);
        break;
    case 0x0f: {  // mes/print
        char* ptr = code_getdsi("");
        code_estmppush(ptr);
        int sw = code_getdi(0);
        ptr = code_estmppop();
        if (sw & ~(1 | 2 | 4 | 8)) unsupported("mes", "option " + std::to_string(sw));
        std::string help_notice;
        if (H->opt.auxiliary_close_hide && b->wid == 20) {
            // Presentation only: retain the VM variable and CP932 game data.
            const auto line = to_utf8(ptr);
            if (line == "*Closing this window will also close the game. You can disable this window in the settings.")
                help_notice = "*Closing this help window keeps the game running. You can disable it in the settings.";
            else if (line == "※このウインドウを閉じると連動してゲームも閉じます。設定からこのウインドウが出ないようにもできます。") {
                if (!edit_encode("この案内ウインドウを閉じてもゲームは続行します。設定で非表示にできます。", help_notice))
                    unsupported("help notice", "cannot encode Japanese display notice");
            }
        }
        if (b->text) b->text->set_log_word_draw(drawing_in_routine("msg_write"));
        b->print(help_notice.empty() ? std::string_view(ptr) : std::string_view(help_notice), sw, !drawing_in_routine("gmes"));
        if (b->text) b->text->set_log_word_draw(false);
        if (b->fl_udraw && is_window(*b)) present(*b);
        break;
    }
    case 0x10: {  // title
        char* p = code_gets();
        if (b->type >= gfx::kTypeMain) {
            b->title_cp932 = p;
            H->display->set_title(b->wid, to_utf8(p));
        }
        break;
    }
    case 0x11:  // pos
        b->cx = code_getdi(b->cx);
        b->cy = code_getdi(b->cy);
        break;
    case 0x12:  // circle
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(b->width()); p4 = code_getdi(b->height()); p5 = code_getdi(1);
        b->circle(p1, p2, p3, p4, p5);
        break;
    case 0x13:  // cls
        b->cls(code_getdi(0));
        clear_objects(b->wid);
        break;
    case 0x14: {  // font
        std::string name = code_gets();
        p1 = code_getdi(12);
        p2 = code_getdi(0);
        p3 = code_getdi(1);
        gfx::FontSpec spec{name, p1, p2, 0};
        if (b->text && b->text->prepare(spec)) {
            b->font = spec;
            H->ctx->stat = 0;
        } else {
            H->ctx->stat = -1;
        }
        b->fonteff_size = p3;
        break;
    }
    case 0x15:  // sysfont
        p1 = code_getdi(0);
        if (p1 == 0) b->set_font_default();
        else b->font = gfx::FontSpec{"", 14, 0, p1};
        break;
    case 0x16:  // objsize
        p1 = code_getdi(64); p2 = code_getdi(24); p3 = code_getdi(0);
        b->ox = p1; b->oy = p2; b->py = p3;
        break;
    case 0x17: {  // picload
        std::string fname = code_gets();
        p1 = code_getdi(0);
        int wid = b->wid;
        if (picload(wid, fname, p1)) throw HSPERR_PICTURE_MISSING;
        H->cur = H->screens->get(wid);
        break;
    }
    case 0x18:  // color
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(0);
        b->color = gfx::rgb(p1, p2, p3);
        break;
    case 0x1b:  // redraw
        p1 = code_getdi(1); p2 = code_getdi(0); p3 = code_getdi(0); p4 = code_getdi(0); p5 = code_getdi(0);
        if (p1 == 0) { b->fl_udraw = 0; break; }
        if (p1 == 1) {
            b->fl_udraw = 1;
            if (is_window(*b)) present(*b);  // Update()/Send(): partial send presents the whole frame
        }
        b->fl_udraw = p1 & 1;
        break;
    case 0x1c:  // width
        p1 = code_getdi(-1); p2 = code_getdi(-1); p3 = code_getdi(-1); p4 = code_getdi(-1); p5 = code_getdi(0);
        b->width_request(p1, p2, p3, p4, p5);
        if (is_window(*b)) configure_window(*b, !b->hidden);
        break;
    case 0x1d:  // gsel
        p1 = code_getdi(0); p2 = code_getdi(0);
        H->cur = &screen_safe(p1);
        H->cur_window = p1;
        if (is_window(*H->cur)) {
            if (p2 < 0) { H->display->hide_window(p1); H->cur->hidden = true; }
            else if (p2 > 0) configure_window(*H->cur, true);
        }
        break;
    case 0x1e: {  // gcopy
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(0); p4 = code_getdi(b->gx); p5 = code_getdi(b->gy);
        Screen& src = screen_safe(p1);
        if (b->copy(src, p2, p3, p4, p5)) throw HSPERR_UNSUPPORTED_FUNCTION;
        break;
    }
    case 0x1f: {  // gzoom
        p1 = code_getdi(b->width()); p2 = code_getdi(b->height()); p3 = code_getdi(0); p4 = code_getdi(0); p5 = code_getdi(0);
        p6 = code_getdi(b->gx);
        int p7 = code_getdi(b->gy), p8 = code_getdi(0);
        Screen& src = screen_safe(p3);
        b->zoom(p1, p2, src, p4, p5, p6, p7, p8);
        break;
    }
    case 0x20:  // gmode
        p1 = code_getdi(0); p2 = code_getdi(32); p3 = code_getdi(32); p4 = code_getdi(0);
        b->gmode = p1; b->gx = p2; b->gy = p3; b->gfrate = p4;
        break;
    case 0x21: {  // bmpsave
        std::string name = code_gets();
        std::filesystem::path path;
        std::string err;
        if (!H->begin_stream_write(name, path, err)) unsupported("file policy", "bmpsave '" + to_utf8(name) + "': " + err);
        if (!gfx::save_bmp(b->dib, path, &err)) throw HSPERR_FILE_IO;
        if (!H->end_stream_write(name, err)) unsupported("file policy", "bmpsave finish: " + err);
        break;
    }
    case 0x22:  // hsvcolor
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(0);
        b->set_hsv_color(p1, p2, p3);
        break;
    case 0x23: {  // getkey
        PVal* pval;
        APTR aptr = code_getva(&pval);
        p1 = code_getdi(1);
        if (H->opt.deterministic) pump_real_events(0);
        else pump_real_events(0);
        p2 = (p1 >= 0 && p1 < 256 && H->key[p1]) ? 1 : 0;
        code_setva(pval, aptr, TYPE_INUM, &p2);
        break;
    }
    case 0x29: case 0x2a: case 0x2b:  // buffer/screen/bgscr
        return cmd_screen(cmd);
    case 0x27: {  // input
        PVal* pval;
        APTR aptr = code_getva(&pval);
        p1 = code_getdi(b->ox);
        p2 = code_getdi(b->oy);
        int size = 32, type = pval->flag;
        std::string def;
        if (type == HSPVAR_FLAG_STR) {
            char* ptr = reinterpret_cast<char*>(HspVarCorePtrAPTR(pval, aptr));
            HspVarCoreGetBlockSize(pval, reinterpret_cast<PDAT*>(ptr), &size);
            def = ptr;
        } else if (type == HSPVAR_FLAG_INT || type == HSPVAR_FLAG_DOUBLE) {
            def = reinterpret_cast<char*>(HspVarCoreCnv(type, HSPVAR_FLAG_STR, HspVarCorePtrAPTR(pval, aptr)));
        } else {
            throw HSPERR_TYPE_MISMATCH;
        }
        p3 = code_getdi(size);
        H->ctx->stat = add_input_object(*b, pval, aptr, p1, p2, def, type == HSPVAR_FLAG_STR ? p3 : 16, type, type);
        break;
    }
    case 0x28: {  // mesbox
        PVal* pval;
        APTR aptr = code_getva(&pval);
        if (pval->flag != HSPVAR_FLAG_STR) throw HSPERR_TYPE_MISMATCH;
        char* ptr = reinterpret_cast<char*>(HspVarCorePtrAPTR(pval, aptr));
        int size = 0;
        HspVarCoreGetBlockSize(pval, reinterpret_cast<PDAT*>(ptr), &size);
        std::string def = ptr;
        p1 = code_getdi(b->ox); p2 = code_getdi(b->oy); p3 = code_getdi(1); p4 = code_getdi(-1);
        int mode = 0x100;
        if ((p3 & 1) == 0) mode |= 0x200;
        if (p3 & ~(1 | 4 | 8)) unsupported("mesbox", "style " + std::to_string(p3));
        if (p4 < 0) p4 = size - 1;
        H->ctx->stat = add_input_object(*b, pval, aptr, p1, p2, def, p4, HSPVAR_FLAG_STR, mode);
        break;
    }
    case 0x2d:  // objsel
        p1 = code_getdi(0);
        if (p1 >= 0) {
            EditObject& o = object_safe(*b, p1);
            if (EditObject* old = focused_edit(b->wid); old && old != &o) {
                edit_cancel_composition(*old);
                H->display->clear_composition(b->wid);
            }
            if (o.kind == 1) o.select_all = true;  // HSPOBJ_TAB_SELALLTEXT
            H->focus_obj[b->wid] = p1;
            H->display->set_text_input(b->wid, true);
            edit_dirty(o);
            H->ctx->stat = 0;
        } else {
            auto it = H->focus_obj.find(b->wid);
            H->ctx->stat = it == H->focus_obj.end() ? -1 : it->second;
        }
        break;
    case 0x30: {  // clrobj
        p1 = code_getdi(0); p2 = code_getdi(-1);
        auto& v = H->objects[b->wid];
        int p4m = int(v.size()) - 1;
        if (p4m < 0) break;
        if (p1 < 0 || p2 > p4m || p1 > p4m) throw HSPERR_ILLEGAL_FUNCTION;
        if (p2 < 0) p2 = p4m;
        for (int i = p1; i <= p2; ++i) {
            edit_cancel_composition(v[i]);
            v[i].kind = 0;
            if (H->focus_obj[b->wid] == i) {
                H->focus_obj[b->wid] = -1;
                H->display->set_text_input(b->wid, false);
            }
        }
        break;
    }
    case 0x32: {  // objprm
        p1 = code_getdi(0);
        if (code_get() <= PARAM_END) throw HSPERR_NO_DEFAULT;
        auto& v = H->objects[b->wid];
        if (p1 < 0 || p1 >= int(v.size())) throw HSPERR_ILLEGAL_FUNCTION;
        EditObject& o = v[p1];
        if (o.kind == 0) break;  // owmode NONE: ignored
        if (o.composing) H->display->clear_composition(b->wid);
        edit_cancel_composition(o);
        o.input_error.clear();
        if (mpval->flag == HSPVAR_FLAG_STR) o.text = mpval->pt;
        else if (mpval->flag == HSPVAR_FLAG_INT || mpval->flag == HSPVAR_FLAG_DOUBLE)
            o.text = reinterpret_cast<char*>(HspVarCoreCnv(mpval->flag, HSPVAR_FLAG_STR, mpval->pt));
        else throw HSPERR_TYPE_MISMATCH;
        o.caret = 0;  // SetWindowText puts the caret at the start
        o.select_all = false;
        edit_sync_var(o);
        break;
    }
    case 0x33:  // objmode
        p1 = code_getdi(0); p2 = code_getdi(b->tabmove);
        b->objmode = p1; b->tabmove = p2;
        break;
    case 0x2c: {  // mouse
        p1 = code_getdi(H->mouse_screen_x); p2 = code_getdi(H->mouse_screen_y); p3 = code_getdi(0);
        if (p3 == 0 && (p1 < 0 || p2 < 0)) { H->cursor_shown = 0; break; }
        if (p3 != 0 || p1 != H->mouse_screen_x || p2 != H->mouse_screen_y)
            unsupported("mouse", "cursor warp to (" + std::to_string(p1) + "," + std::to_string(p2) + ")");
        H->cursor_shown = 1;
        break;
    }
    case 0x2f:  // line
        p1 = code_getdi(0); p2 = code_getdi(0);
        b->cx = code_getdi(b->cx);
        b->cy = code_getdi(b->cy);
        b->line_to(p1, p2);
        break;
    case 0x31:  // boxf
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(b->width()); p4 = code_getdi(b->height());
        b->boxfill(p1, p2, p3, p4);
        break;
    case 0x34: {  // stick
        PVal* pval;
        APTR aptr = code_getva(&pval);
        p1 = code_getdi(0);
        p2 = code_getdi(1);
        int res = 0;
        if (p2 && !H->focus) { code_setva(pval, aptr, TYPE_INUM, &res); break; }
        static const int stick_keys[] = {0x25, 0x26, 0x27, 0x28, 0x20, 0x0d, 0x11, 0x1b, 0x01, 0x02, 0x09, 90, 88, 67, 65, 87, 68, 83};
        int ckey = 0;
        for (std::size_t i = 0; i < sizeof(stick_keys) / sizeof(int); ++i)
            if (H->key[stick_keys[i]]) ckey |= 1 << i;
        int cktrg = (ckey ^ H->cklast) & ckey;
        H->cklast = ckey;
        res = cktrg | (ckey & p1);
        code_setva(pval, aptr, TYPE_INUM, &res);
        break;
    }
    case 0x35: {  // grect
        p1 = code_getdi(0); p2 = code_getdi(0);
        double rot = code_getdd(0.0);
        p3 = code_getdi(b->gx); p4 = code_getdi(b->gy);
        auto p = gfx::make_rect_poly(cnv_rgb(b->color), get_alpha_operation(*b), p1, p2, p3, p4, rot);
        H->poly.draw_f4(b->dib, p);
        b->mark_dirty();
        break;
    }
    case 0x36: {  // grotate
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(0);
        double rot = code_getdd(0.0);
        p4 = code_getdi(b->gx); p5 = code_getdi(b->gy);
        Screen& src = screen_safe(p1);
        grotate_sub(src, p2, p3, b->gx, b->gy, p4, p5, rot, p4 / 2, p5 / 2);
        break;
    }
    case 0x38: {  // gradf
        p1 = code_getdi(0); p2 = code_getdi(0); p3 = code_getdi(b->width()); p4 = code_getdi(b->height());
        int mode = code_getdi(0), col = cnv_rgb(b->color);
        p5 = code_getdi(col); p6 = code_getdi(col);
        b->gradfill(p1, p2, p3, p4, mode, gfx::colorref(p5), gfx::colorref(p6));
        break;
    }
    case 0x3d: {  // celdiv
        p1 = code_getdi(1); p2 = code_getdi(0); p3 = code_getdi(0); p4 = code_getdi(0); p5 = code_getdi(0);
        screen_safe(p1).set_cel_divide(p2, p3, p4, p5);
        break;
    }
    case 0x3e: {  // celput
        p1 = code_getdi(1); p2 = code_getdi(0);
        double zx = code_getdd(1.0), zy = code_getdd(1.0), rot = code_getdd(0.0);
        Screen& src = screen_safe(p1);
        if (rot == 0.0 && zx == 1.0 && zy == 1.0) { b->cel_put(src, p2); break; }
        int srcsx = src.divsx, srcsy = src.divsy;
        int x = (p2 % src.divx) * srcsx, y = (p2 / src.divx) * srcsy;
        int putsx = int(srcsx * zx), putsy = int(srcsy * zy);
        grotate_sub(src, x, y, srcsx, srcsy, putsx, putsy, rot, int(src.celofsx * zx), int(src.celofsy * zy));
        b->cx += putsx;
        break;
    }
    case 0x4d:  // objcolor
        p1 = code_getdi(255); p2 = code_getdi(255); p3 = code_getdi(255);
        b->objcolor = gfx::rgb(p1, p2, p3);
        break;
    case 0x4e:  // rgbcolor
        p1 = code_getdi(0); p2 = code_getdi(0);
        if (p2 == 1) b->objcolor = gfx::rgb(p1 >> 16, p1 >> 8, p1);
        else b->color = gfx::rgb(p1 >> 16, p1 >> 8, p1);
        break;
    case 0x4f:  // viewcalc (no-op in Win32 runtime)
        break;
    case 0x3f: case 0x40: case 0x41: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
    case 0x4b: case 0x4c: case 0x5d: case 0x5e: case 0x5f:
        throw HSPERR_UNSUPPORTED_FUNCTION;  // HSP3Dish-only in the Win32 runtime too
    default:
        unsupported("command", extcmd_name(cmd));
    }
    return RUNMODE_RUN;
}

int cmdfunc_extcmd(int cmd) {
    code_next();
    if (H->opt.trace_commands) ++H->command_counts[extcmd_name(cmd)];
    try {
        return cmdfunc_extcmd_body(cmd);
    } catch (const gfx::Unsupported& e) {
        unsupported(extcmd_name(cmd), e.what());
    } catch (const gfx::InvalidScreen&) {
        throw HSPERR_ILLEGAL_FUNCTION;
    }
}

// Type 48 is reserved by tools/prepare_korean_game.py for managed display
// boundaries. Original 2.335 bytecode does not contain this extension.
int cmdfunc_localization(int cmd) {
    code_next();
    if (cmd == 11) {
        PVal* pval = nullptr;
        APTR aptr = code_getva(&pval);
        int width = H->text ? H->text->selection_label_width(H->cur->font) : 0;
        code_setva(pval, aptr, TYPE_INUM, &width);
        return RUNMODE_RUN;
    }
    if (cmd == 10) {
        if (H->opt.trace_commands) ++H->command_counts["native_ko_input_caption"];
        if (H->text && H->text->draw_input_caption(H->cur->dib, H->cur->cx, H->cur->cy))
            H->cur->mark_dirty();
        return RUNMODE_RUN;
    }
    if (cmd == 9) {
        const int id = code_geti();
        const std::string source = code_gets();
        const int spread = code_geti(), x = code_geti(), y = code_geti();
        if (H->opt.trace_commands) ++H->command_counts["native_ko_book"];
        H->ctx->stat = 0;
        if (H->text && H->text->draw_book(H->cur->dib, id, source, spread, x, y)) {
            H->cur->mark_dirty();
            H->ctx->stat = 1;
        }
        return RUNMODE_RUN;
    }
    if (cmd == 8) {
        std::array<std::string, 7> args;
        for (auto& value : args) value = code_gets();
        const int item = code_geti();
        if (H->text) H->text->prepare_board_row(args, item);
        if (H->opt.trace_commands) ++H->command_counts["native_ko_board"];
        return RUNMODE_RUN;
    }
    if (cmd == 7) {
        const int scope = code_geti();
        const std::string source = code_gets();
        if (H->text) H->text->set_menu_draw(scope, source);
        return RUNMODE_RUN;
    }
    if (cmd == 6) {
        const std::string row = code_gets();
        const std::string base = code_gets();
        const std::string classifier = code_gets();
        const int id = code_geti();
        if (H->text) H->text->prepare_item_row(row, base, classifier, id);
        return RUNMODE_RUN;
    }
    if (cmd == 1) {
        const std::string source = code_gets();
        const int used = code_geti(), limit = code_geti();
        const int starter_pet_id = code_getdi(0);
        if (H->text) H->text->begin_english_log(source, used, limit, starter_pet_id);
        return RUNMODE_RUN;
    }
    if (cmd == 2) {
        const int enabled = code_geti();
        if (H->text) H->text->set_hud_draw(enabled != 0);
        return RUNMODE_RUN;
    }
    if (cmd != 0) unsupported("localization command", std::to_string(cmd));
    const std::string source = code_gets();  // copy before later VM argument reads
    const int x = code_geti(), y = code_geti(), width = code_geti(), shadow = code_geti();
    const int red = code_geti(), green = code_geti(), blue = code_geti();
    if (H->opt.trace_commands) ++H->command_counts["native_ko_gmes"];
    H->ctx->stat = -1;  // explicit original-gmes fallback, not a fake render
    if (!H->cur->text) return RUNMODE_RUN;
    try {
        gfx::FontSpec final_font;
        const int height = H->cur->text->draw_localized_paragraph(
            H->cur->dib, x, y, width, shadow == 1, gfx::rgb(red, green, blue), source, final_font);
        if (height >= 0) {
            H->cur->font = final_font;
            H->cur->mark_dirty();
            H->ctx->stat = height;
            if (H->cur->fl_udraw && is_window(*H->cur)) present(*H->cur);
        }
    } catch (const std::exception& e) {
        unsupported("Korean paragraph", e.what());
    }
    return RUNMODE_RUN;
}

void* reffunc_sysvar(int* type_res, int arg) {
    *type_res = HSPVAR_FLAG_INT;
    void* ptr = &H->reffunc_value;
    Screen& b = *H->cur;
    if (arg & 0x100) {
        if (*H->type != TYPE_MARK || *H->val != '(') throw HSPERR_INVALID_FUNCPARAM;
        code_next();
        switch (arg & 0xff) {
        case 0: H->reffunc_value = get_ginfo(code_geti()); break;
        case 2: {
            int id = code_geti();
            ptr = hsp3ext_getdir(id);
            *type_res = HSPVAR_FLAG_STR;
            break;
        }
        case 1: unsupported("function", "objinfo");
        case 3: unsupported("function", "sysinfo(" + std::to_string(code_geti()) + ")");
        default: unsupported("function", "extsysvar function " + std::to_string(arg));
        }
        if (*H->type != TYPE_MARK || *H->val != ')') throw HSPERR_INVALID_FUNCPARAM;
        code_next();
        return ptr;
    }
    switch (arg) {
    case 0: H->reffunc_value = b.savepos[0]; break;
    case 1: H->reffunc_value = b.savepos[1]; break;
    case 2: H->reffunc_value = b.savepos[3]; b.savepos[3] = 0; break;
    case 3:
        // hwnd: an opaque per-screen token, never a host pointer. Any Win32 API
        // receiving it is an unimplemented DLL call and is diagnosed there.
        H->reffunc_value = 0x00e10000 | (b.wid & 0xffff);
        break;
    case 4: unsupported("sysvar", "hinstance");
    case 5: unsupported("sysvar", "hdc");
    default: unsupported("sysvar", std::to_string(arg));
    }
    return ptr;
}

// ---------------------------------------------------------------- DLL dispatch

std::string ds_string(int index) { return std::string(H->ctx->mem_mds + index); }

std::int32_t invoke_dll(int cmd, bool& legacy) {
    int prmmax = H->ctx->hsphed->max_finfo / int(sizeof(STRUCTDAT));
    if (cmd < 0 || cmd >= prmmax) throw HSPERR_UNSUPPORTED_FUNCTION;
    STRUCTDAT* st = &H->ctx->mem_finfo[cmd];
    std::string symbol = ds_string(st->nameidx);
    std::string library = st->index >= 0 ? ds_string(H->ctx->mem_linfo[st->index].nameidx) : std::string("?");
    for (auto& c : library) c = char(std::tolower(static_cast<unsigned char>(c)));
    legacy = st->subid == STRUCTPRM_SUBID_OLDDLL || st->subid == STRUCTPRM_SUBID_OLDDLLINIT;
    if (st->subid != STRUCTPRM_SUBID_DLL && st->subid != STRUCTPRM_SUBID_DLLINIT && !legacy)
        unsupported("DLL", library + "!" + symbol + " subid " + std::to_string(st->subid));
    const DllHandler* handler = find_dll_handler(library, symbol);
    if (!handler) unsupported("DLL", library + "!" + symbol + " (no native implementation)");
    DllCall call;
    call.library = library;
    call.symbol = symbol;
    call.legacy = legacy;
    call.host = H;
    for (int i = 0; i < st->prmmax; ++i) {
        STRUCTPRM* prm = &H->ctx->mem_minfo[st->prmindex + i];
        DllArg a;
        switch (prm->mptype) {
        case MPTYPE_INUM: a.kind = DllArgKind::Int; a.i = code_getdi(0); break;
        case MPTYPE_DNUM: a.kind = DllArgKind::Double; a.d = code_getdd(0.0); break;
        case MPTYPE_FLOAT: a.kind = DllArgKind::Float; a.d = code_getdd(0.0); break;
        case MPTYPE_LOCALSTRING: a.kind = DllArgKind::String; a.str = code_gets(); break;
        case MPTYPE_PVARPTR: case MPTYPE_PPVAL: {
            // MPTYPE_PPVAL: the original passed a PVal copy; handlers get the
            // same variable as a bounds-checked reference instead.
            PVal* pval;
            APTR aptr = code_getva(&pval);
            a.kind = DllArgKind::VarRef;
            a.pval = pval;
            a.aptr = aptr;
            a.var_type = pval->flag;
            a.data = reinterpret_cast<std::uint8_t*>(HspVarCorePtrAPTR(pval, aptr));
            int size = 0;
            void* block = HspVarCoreGetBlockSize(pval, reinterpret_cast<PDAT*>(a.data), &size);
            a.data = static_cast<std::uint8_t*>(block);
            a.bytes_remaining = size > 0 ? std::size_t(size) : 0;
            break;
        }
        case MPTYPE_PBMSCR: a.kind = DllArgKind::Surface; a.surface_id = H->cur_window; break;
        case MPTYPE_PTR_REFSTR: a.kind = DllArgKind::RefStr; a.refstr = H->ctx->refstr; break;
        case MPTYPE_PTR_EXINFO: a.kind = DllArgKind::ExInfo; break;
        case MPTYPE_NULLPTR: a.kind = DllArgKind::Null; break;
        case MPTYPE_FLEXSPTR: case MPTYPE_FLEXWPTR: {
            if (code_get() <= PARAM_END) throw HSPERR_NO_DEFAULT;
            if (mpval->flag == HSPVAR_FLAG_INT) { a.kind = DllArgKind::Int; a.i = *reinterpret_cast<int*>(mpval->pt); }
            else if (mpval->flag == HSPVAR_FLAG_STR) { a.kind = DllArgKind::String; a.str = mpval->pt; }
            else throw HSPERR_TYPE_MISMATCH;
            break;
        }
        default:
            unsupported("DLL", library + "!" + symbol + " argument type " + std::to_string(prm->mptype));
        }
        call.args.push_back(std::move(a));
    }
    try {
        return (*handler)(call);
    } catch (const DllRefusal& e) {
        unsupported("DLL", library + "!" + symbol + " refused: " + e.what());
    }
}

int exec_dll(int cmd) {
    code_next();
    bool legacy = false;
    std::int32_t result = invoke_dll(cmd, legacy);
    if (legacy) {
        if (result > 0) {
            if (result & 0x20000) result &= 0x1ffff;
            else if (result & 0x10000) result = (result & 0xffff) * 10;
            else throw HSPERR_DLL_ERROR;
            H->ctx->waitcount = result;
            H->ctx->waittick = -1;
            H->ctx->runmode = RUNMODE_AWAIT;
            return RUNMODE_AWAIT;
        }
        H->ctx->stat = -result;
    } else {
        H->ctx->stat = result;
    }
    return RUNMODE_RUN;
}

int cmdfunc_dll(int cmd) { return exec_dll(cmd); }

void* reffunc_dll(int* type_res, int arg) {
    if (*H->type != TYPE_MARK || *H->val != '(') throw HSPERR_INVALID_FUNCPARAM;
    *type_res = HSPVAR_FLAG_INT;
    exec_dll(arg);
    H->reffunc_value = H->ctx->stat;
    if (*H->type != TYPE_MARK || *H->val != ')') throw HSPERR_INVALID_FUNCPARAM;
    code_next();
    return &H->reffunc_value;
}

int cmdfunc_dllctrl(int cmd) { code_next(); unsupported("COM/DLL control", std::to_string(cmd)); }
void* reffunc_dllctrl(int*, int arg) { unsupported("COM/DLL control function", std::to_string(arg)); }


// ---------------------------------------------------------------- clock-dependent builtins

int (*g_orig_intcmd)(int) = nullptr;
int (*g_orig_progcmd)(int) = nullptr;

int progcmd_wrapper(int cmd) {
    if (cmd == 0x10) {  // end
        const auto* pc = code_getpcbak();
        H->end_pc_words = pc ? long(pc - H->ctx->mem_mcs) : -1;
        H->note("HSP end command: pc_words=" + std::to_string(H->end_pc_words) + " frame=" + std::to_string(H->frame));
    }
    return g_orig_progcmd(cmd);
}
void* (*g_orig_intfunc)(int*, int) = nullptr;
int g_gettime_value = 0;

// Deterministic runs replace the time sources of `randomize` (no argument)
// and `gettime` with the virtual clock; real-time runs keep upstream behaviour.
int intcmd_wrapper(int cmd) {
    if (cmd != 0x27 || !H->opt.deterministic) return g_orig_intcmd(cmd);
    code_next();
    // Windows HSP seeds with GetTickCount(); use a fixed virtual boot tick.
    int seed = code_getdi(int(H->opt.seed_tick + H->virtual_ms));
#ifdef ELONA_LEGACY_RNG
    elona_legacy_srand(static_cast<std::uint32_t>(seed));
#else
    srand(seed);
#endif
    return RUNMODE_RUN;
}

void* intfunc_wrapper(int* type_res, int arg) {
    if (arg != 0x008 || !H->opt.deterministic) return g_orig_intfunc(type_res, arg);
    if (*H->type != TYPE_MARK || *H->val != '(') throw HSPERR_INVALID_FUNCPARAM;
    code_next();
    int index = code_geti();
    if (*H->type != TYPE_MARK || *H->val != ')') throw HSPERR_INVALID_FUNCPARAM;
    code_next();
    std::int64_t ms = H->virtual_ms;
    std::time_t t = std::time_t(H->opt.virtual_epoch + ms / 1000);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    switch (index) {
    case 0: g_gettime_value = tmv.tm_year + 1900; break;
    case 1: g_gettime_value = tmv.tm_mon + 1; break;
    case 2: g_gettime_value = tmv.tm_wday; break;
    case 3: g_gettime_value = tmv.tm_mday; break;
    case 4: g_gettime_value = tmv.tm_hour; break;
    case 5: g_gettime_value = tmv.tm_min; break;
    case 6: g_gettime_value = tmv.tm_sec; break;
    case 7: g_gettime_value = int(ms % 1000); break;
    case 8: g_gettime_value = 0; break;
    default: throw HSPERR_ILLEGAL_FUNCTION;
    }
    *type_res = HSPVAR_FLAG_INT;
    return &g_gettime_value;
}

// ---------------------------------------------------------------- startup

std::uint32_t le32(const std::vector<unsigned char>& d, std::size_t o) {
    return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24;
}

std::string validate_ax(const std::filesystem::path& file, bool fixture) {
    std::vector<unsigned char> d;
    if (!read_bytes(file, d)) return "cannot read AX";
    if (d.size() < sizeof(HSPHED) || std::memcmp(d.data(), "HSP3", 4) != 0) return "not an HSP3 object";
    if (le32(d, 12) != d.size()) return "AX size mismatch";
    if (le32(d, 16) < sizeof(HSPHED)) return "legacy 96-byte AX header: normalise with tools/normalize_legacy_ax.py first";
    for (std::size_t o = 16; o < 80; o += 8) {
        auto begin = le32(d, o), size = le32(d, o + 4);
        if (begin > d.size() || size > d.size() - begin) return "AX section exceeds file bounds";
    }
    if (fixture && (le32(d, 52) != 0 || (le32(d, 84) & 0xffffu) != 0)) return "DLL/plugin declarations are not permitted in trusted fixtures";
    return "";
}

// Diagnostic text for native paths. path::string() converts with the Windows
// ANSI code page and throws (or loses characters) for non-ACP names.
std::string path_utf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

void load_script(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read script " + path_utf8(file));
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        long frame;
        ScriptAction a;
        if (!(ss >> frame)) continue;
        if (!(ss >> a.op)) throw std::runtime_error("script line " + std::to_string(n) + ": missing op");
        std::string arg;
        while (ss >> arg) a.args.push_back(arg);
        a.line = n;
        H->script.emplace(frame, a);
    }
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(char(c)); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o.push_back(char(c));
    }
    return o;
}

void write_report(int exit_code, int endcode, const std::string& hsp_error) {
    if (H->opt.report.empty()) return;
    std::ofstream f(H->opt.report);
    f << "{\n  \"host_build\": {\"compiler_timestamp\":\"" << kHostBuildStamp
      << "\",\"architecture\":\"" << kHostArchitecture << "\",\"diagnostics_schema\":2},\n"
      << "  \"vm_return_mode\": " << H->vm_return_mode << ",\n  \"exit_code\": " << exit_code << ",\n  \"endcode\": " << endcode << ",\n  \"exit_reason\": \"" << json_escape(H->exit_reason)
      << "\",\n  \"frames\": " << H->frame << ",\n  \"virtual_ms\": " << H->virtual_ms << ",\n  \"display\": \"" << H->display->name()
      << "\",\n  \"deterministic\": " << (H->opt.deterministic ? "true" : "false") << ",\n  \"hsp_error\": \"" << json_escape(hsp_error)
      << "\",\n  \"first_unsupported\": \"" << json_escape(H->first_unsupported) << "\",\n  \"text_codec\": \"" << gfx::ui_text_codec_backend()
      << "\",\n  \"notes\": [";
    for (std::size_t i = 0; i < H->notes.size(); ++i) f << (i ? ", " : "") << "\"" << json_escape(H->notes[i]) << "\"";
    f << "],\n  \"window_fullscreen\": " << (H->display->has_windows() ? (H->display->is_fullscreen(0) ? "true" : "false") : "null")
      << ",\n  \"fullscreen_logical_size\": [" << H->fullscreen_width << ", " << H->fullscreen_height << "],\n  \"command_counts\": {";
    bool first = true;
    for (auto& [k, v] : H->command_counts) { f << (first ? "" : ", ") << "\"" << json_escape(k) << "\": " << v; first = false; }
    f << "},\n  \"observed_hsp_error_count\": " << H->observed_error_count << ",\n  \"observed_hsp_errors\": [";
    for (std::size_t i = 0; i < H->observed_errors.size(); ++i) {
        const auto& e = H->observed_errors[i];
        f << (i ? ", " : "") << "{\"code\":" << e.code << ",\"line\":" << e.line
          << ",\"pc_words\":" << e.pc_words << ",\"frame\":" << e.frame << "}";
    }
    f << "],\n  \"end_pc_words\": " << H->end_pc_words << ",\n  \"close_requests\": " << H->close_requests
      << ",\n  \"auxiliary_close_hide\": " << (H->opt.auxiliary_close_hide ? "true" : "false")
      << ",\n  \"hidden_help_close_requests\": " << H->hidden_help_close_requests
      << ",\n  \"recent_input_events\": [";
    for (std::size_t i = 0; i < H->recent_input_events.size(); ++i)
        f << (i ? ", " : "") << "\"" << json_escape(H->recent_input_events[i]) << "\"";
    f << "],\n  \"dialog_count\": " << H->dialog_count << ",\n  \"dialogs\": [";
    for (std::size_t i = 0; i < H->dialogs.size(); ++i) {
        const auto& d = H->dialogs[i];
        f << (i ? ", " : "") << "{\"mode\":" << d.mode << ",\"result\":" << d.result
          << ",\"frame\":" << d.frame << ",\"pc_words\":" << d.pc_words
          << ",\"interactive\":" << (d.interactive ? "true" : "false")
          << ",\"completed\":" << (d.completed ? "true" : "false")
          << ",\"title\":\"" << json_escape(d.title) << "\",\"text\":\"" << json_escape(d.text)
          << "\",\"error\":\"" << json_escape(d.error) << "\"}";
    }
    f << "]\n}\n";
}

// Observe before the script's onerror handler runs. Some games catch an error,
// then fail again inside that handler and eventually execute `end 0`; that must
// not erase the original failure from diagnostic evidence. Return 0 so the
// original IRQ delivery/recovery semantics are untouched.
int observe_irq(int event, int wparam, int lparam, void* irq) {
    if (event == HSPEVENT_HSPIRQ && H && irq == code_getirq(HSPIRQ_ONERROR)) {
        const auto* pc = code_getpcbak();
        const long offset = pc ? long(pc - H->ctx->mem_mcs) : -1;
        ++H->observed_error_count;
        if (H->observed_errors.size() < 32) {
            H->observed_errors.push_back({wparam, lparam, offset, H->frame});
            H->note("observed HSP error " + std::to_string(wparam) + " (" + hspd_geterror(HSPERROR(wparam)) +
                    ") pc_words=" + std::to_string(offset) + " frame=" + std::to_string(H->frame));
        } else if (H->observed_error_count == 33) H->note("further HSP error details suppressed (first 32 retained)");
        if (H->opt.fail_on_hsp_error) {
            H->ctx->runmode = RUNMODE_ERROR;
            return 1;
        }
    }
    return 0;
}

}  // namespace

int run_host(const HostOptions& options) {
    Host host;
    H = &host;
    host.opt = options;
    host.note(std::string("host build: ") + kHostBuildStamp + " architecture=" + kHostArchitecture + " diagnostics_schema=2");
    std::string err = validate_ax(options.ax, options.trusted_fixture);
    if (!err.empty()) {
        std::fprintf(stderr, "elona-host: %s\n", err.c_str());
        return 2;
    }
    if (!options.script.empty()) load_script(options.script);
    if (options.game) {
        register_plugin_bindings();
        register_win32_bindings();
        register_media_bindings();
        register_gfx_bindings();
        register_water_bindings();
        register_elona_bindings();
        register_host_win32_bindings();
    }

#ifdef ELONA_HAVE_TTF
    gfx::TtfTextOptions topt;
    topt.translation_catalog = options.translation_catalog;
    topt.translation_misses = options.translation_misses;
    topt.translation_fallback = options.translation_fallback;
    topt.font_files = options.fonts;
    topt.antialias = options.text_antialias;
    std::string terr;
    host.text = gfx::create_ttf_text_renderer(topt, terr);
    if (!host.text) host.note("text renderer unavailable: " + terr + " (mes will be unsupported)");
    if (!host.text && !options.translation_catalog.empty()) return 2;
#else
    if (!options.translation_catalog.empty()) {
        host.note("translation catalog requested, but this host was built without ELONA_TEXT_TTF");
        return 2;
    }
#endif
    host.screens = std::make_unique<gfx::ScreenTable>(host.text.get());

#ifdef ELONA_GUI
    if (!options.headless) {
        std::string derr;
        host.display = platform::create_sdl_display(derr);
        if (!host.display) {
            std::fprintf(stderr, "elona-host: %s\n", derr.c_str());
            return 2;
        }
    }
#else
    if (!options.headless) {
        std::fprintf(stderr, "elona-host: built without ELONA_GUI; use --headless\n");
        return 2;
    }
#endif
    if (!host.display) host.display = platform::create_headless_display();
#ifdef ELONA_GUI
    if (!options.headless && !options.deterministic) {
        std::string aerr;
        host.audio = platform::create_sdl_audio(aerr);
        if (!host.audio) host.note("audio unavailable: " + aerr);
    }
#endif
    if (!host.audio) host.audio = platform::create_null_audio();
#ifdef ELONA_HAVE_MUSIC
    {
        std::filesystem::path sf = options.soundfont.empty() ? std::filesystem::path(ELONA_DEFAULT_SOUNDFONT) : options.soundfont;
        audio::LoadError lerr;
        host.soundfont = audio::SoundFont::load_file(sf, lerr);
        if (!host.soundfont) host.note("SoundFont unavailable (" + lerr.message + "): MIDI will fail to open like a missing MCI sequencer");
        std::string merr;
        Host* hp = &host;
        host.music_output = host.audio->start_music_output([hp](float* out, std::size_t frames) { hp->music.render(out, frames); }, merr);
        if (!host.music_output) host.note("music output: " + merr);
    }
#endif

    auto ax = std::filesystem::absolute(options.ax);
    if (!options.work_dir.empty()) std::filesystem::current_path(options.work_dir);

    // Native-path loading boundary: the host reads the AX through
    // std::filesystem (UTF-16 on Windows) and hands the bytes to the VM via
    // HSP's memory stream ("MEM:" name + dpm_memfile; filepack.cpp pack_fopen).
    // The VM never receives the OS path, so Unicode AX paths need neither an
    // ANSI temporary copy nor a core change. dpm_readalloc copies the bytes and
    // closes the stream inside Reset(), before the game can use memfile itself.
    std::vector<std::uint8_t> ax_bytes;
    if (!read_bytes(ax, ax_bytes) || ax_bytes.empty() || ax_bytes.size() > 0x7fffffffu) {
        std::fprintf(stderr, "elona-host: cannot read AX %s\n", path_utf8(ax).c_str());
        return 2;
    }
    static char memory_ax_name[] = "MEM:start.ax";
    Hsp3 vm;
    dpm_memfile(ax_bytes.data(), static_cast<int>(ax_bytes.size()));
    vm.SetFileName(memory_ax_name);
    if (vm.Reset(0)) {
        std::fprintf(stderr, "elona-host: VM failed to load %s\n", path_utf8(ax).c_str());
        return 2;
    }
    host.ctx = &vm.hspctx;
    host.ctx->msgfunc = msgfunc;
    HSPEXINFO* exinfo = host.ctx->exinfo2;
    host.type = exinfo->nptype;
    host.val = exinfo->npval;
    exinfo->actscr = &host.cur_window;

    if (options.list_dll) {
        int prmmax = host.ctx->hsphed->max_finfo / int(sizeof(STRUCTDAT));
        for (int i = 0; i < prmmax; ++i) {
            STRUCTDAT* st = &host.ctx->mem_finfo[i];
            if (st->subid > STRUCTPRM_SUBID_DLL || st->subid < STRUCTPRM_SUBID_COMOBJ) continue;
            std::string lib = st->index >= 0 ? ds_string(host.ctx->mem_linfo[st->index].nameidx) : "?";
            std::printf("%d %s!%s subid=%d args=", i, lib.c_str(), ds_string(st->nameidx).c_str(), st->subid);
            for (int k = 0; k < st->prmmax; ++k) std::printf("%s%d", k ? "," : "", host.ctx->mem_minfo[st->prmindex + k].mptype);
            std::printf(" handler=%s\n", find_dll_handler(lib, ds_string(st->nameidx)) ? "native" : "MISSING");
        }
        return 0;
    }

#ifdef ELONA_HAVE_GAME_FILES
    if (options.game) {
        platform::FileSystemConfig cfg;
        cfg.game_root = options.game_root;
        cfg.user_root = options.user_root;
        if (!options.packed_dir.empty()) {
            // Members of the EXE's DPM archive (scene1.hsp, scene2.hsp), extracted
            // by tools/elona_extract.py; the game reads them by bare name.
            for (const char* name : {"scene1.hsp", "scene2.hsp"}) {
                std::vector<std::uint8_t> bytes;
                if (!read_bytes(options.packed_dir / name, bytes)) {
                    std::fprintf(stderr, "elona-host: packed member missing: %s\n", path_utf8(options.packed_dir / name).c_str());
                    return 2;
                }
                cfg.packed_members.push_back({name, std::move(bytes)});
            }
        }
        platform::FileDiagnostic d;
        auto e = platform::GameFileSystem::open(cfg, host.files, &d);
        if (e != platform::FileError::ok) {
            std::fprintf(stderr, "elona-host: game filesystem: %s\n", d.detail.c_str());
            return 2;
        }
        // After Reset(): the AX's own plugin types are registered; take a fresh user type.
        if (!platform::hsp::install_file_events(code_gettypeinfo(-1), host.ctx, host.files.get())) {
            std::fprintf(stderr, "elona-host: file event adapter installation failed\n");
            return 2;
        }
        host.note("game root (read-only): " + path_utf8(options.game_root) + "; user root: " + path_utf8(options.user_root) +
                  "; virtual root: " + host.files->virtual_root());
    }
#endif

    // Allocate the explicit managed command before dynamically assigned event
    // observers so their type IDs cannot collide with the reserved ABI.
    // OpenHSP initializes only the requested slot, not holes created by a
    // jump to a larger ID. Allocate consecutively so event scans never see
    // uninitialized option flags/function pointers in slots 18..47.
    for (int id = TYPE_USERDEF; id <= 48; ++id) code_gettypeinfo(id);
    code_gettypeinfo(48)->cmdfunc = cmdfunc_localization;
    auto* observer = code_gettypeinfo(-1);
    observer->option |= HSPEVENT_ENABLE_HSPIRQ;
    observer->eventfunc = observe_irq;
    code_enable_typeinfo(observer);
    auto* ext = code_gettypeinfo(TYPE_EXTCMD);
    ext->cmdfunc = cmdfunc_extcmd;
    code_gettypeinfo(TYPE_EXTSYSVAR)->reffunc = reffunc_sysvar;
    auto* dll = code_gettypeinfo(TYPE_DLLFUNC);
    dll->cmdfunc = cmdfunc_dll;
    dll->reffunc = reffunc_dll;
    {
        auto* ic = code_gettypeinfo(TYPE_INTCMD);
        g_orig_intcmd = ic->cmdfunc;
        ic->cmdfunc = intcmd_wrapper;
        auto* pc = code_gettypeinfo(TYPE_PROGCMD);
        g_orig_progcmd = pc->cmdfunc;
        pc->cmdfunc = progcmd_wrapper;
        auto* ifn = code_gettypeinfo(TYPE_INTFUNC);
        g_orig_intfunc = ifn->reffunc;
        ifn->reffunc = intfunc_wrapper;
    }
    auto* ctrl = code_gettypeinfo(TYPE_DLLCTRL);
    ctrl->cmdfunc = cmdfunc_dllctrl;
    ctrl->reffunc = reffunc_dllctrl;

    // hsp3typeinit_extcmd: initial 640x480 main window (screen 0)
    int exit_code = 0;
    std::string hsp_error;
    try {
        Screen& s0 = host.screens->make(0, gfx::kTypeMain, 640, 480, 0);
        host.cur = &s0;
        configure_window(s0, true);
    } catch (...) {
        std::fprintf(stderr, "elona-host: initial window failed\n");
        return 2;
    }

    int result = code_execcmd();
    host.vm_return_mode = result;  // distinguish RUNMODE_EXITRUN from an HSP end
    int endcode = host.ctx->endcode;
    if (result == RUNMODE_ERROR) {
        char b[256];
        std::snprintf(b, sizeof b, "HSP error %d (%s) line=%d", int(code_geterror()), hspd_geterror(code_geterror()),
                      code_getdebug_line());
        hsp_error = b;
        std::fprintf(stderr, "elona-host: %s\n", b);
        exit_code = host.first_unsupported.empty() ? 1 : 3;
        host.exit_reason = host.first_unsupported.empty() ? "hsp-error" : "unsupported";
    } else if (!host.first_unsupported.empty()) {
        exit_code = 3;
        host.exit_reason = "unsupported";
    } else if (host.forced_exit_code >= 0) {
        exit_code = host.forced_exit_code;
    } else if (host.exit_reason == "max-waits" || host.exit_reason == "timeout") {
        exit_code = 124;
    } else if (host.exit_reason == "stop-idle" || host.exit_reason == "window-closed") {
        exit_code = 0;
    } else {
        exit_code = endcode;
    }
    present_dirty();
    if (!options.screenshot.empty()) save_screenshot(options.screenshot);
    if (!options.window_capture.empty()) capture_window(options.window_capture);
#ifdef ELONA_HAVE_MUSIC
    {
        static const char* names[] = {"idle", "playing", "finished"};
        host.note(std::string("music at exit: state=") + names[int(host.music.state())] + " position_frames=" +
                  std::to_string(host.music.position_frames()) + " loops=" + std::to_string(host.music.loops_completed()) +
                  " output=" + (host.music_output ? "device" : "none"));
    }
#endif
    write_report(exit_code, endcode, hsp_error);
    std::fprintf(stderr, "elona-host: exit=%d reason=%s frames=%ld\n", exit_code, host.exit_reason.c_str(), host.frame);
    host.audio.reset();  // stop the audio thread before the music player is destroyed
    H = nullptr;
    return exit_code;
}

}  // namespace elona::app

// ---- symbols required by the OpenHSP core ----------------------------------

namespace {
std::string g_dir;
}

char* hsp3ext_getdir(int id) {
    using elona::app::H;
    switch (id) {
    case 0:
    case 1:
#ifdef ELONA_HAVE_GAME_FILES
        if (H && H->files) { g_dir = H->files->virtual_root(); break; }
#endif
        // Fixture mode only (game mode returns the CP932 virtual root above).
        if (id == 0) g_dir = elona::app::path_utf8(std::filesystem::current_path());
        else g_dir = elona::app::path_utf8((H && !H->opt.virtual_exe_dir.empty()) ? H->opt.virtual_exe_dir : std::filesystem::current_path());
        break;
    case 4:
        // dir_cmdline: the host passes no Windows command line to the game.
        g_dir.clear();
        break;
    case 5:
        // HSPDEBUG hsp3_fopen fallback ("hsptv" folder). Point at a host-owned
        // directory that is never the game folder; normally it does not exist.
        g_dir = elona::app::path_utf8(std::filesystem::current_path() / ".elona-no-hsptv") + "/";
        break;
    default: {
        if (H && H->ctx) elona::app::unsupported("dirinfo", std::to_string(id));
        throw HSPERR_UNSUPPORTED_FUNCTION;
    }
    }
    return g_dir.data();
}

char* hsp3ext_sysinfo(int id, int*, char*) {
    if (elona::app::H) elona::app::unsupported("sysinfo", std::to_string(id));
    throw HSPERR_UNSUPPORTED_FUNCTION;
}

void hsp3ext_execfile(char*, char*, int mode) {
    if (elona::app::H) elona::app::unsupported("exec", "mode " + std::to_string(mode));
    throw HSPERR_UNSUPPORTED_FUNCTION;
}

void* ex_getbmscr(int id) {
    if (elona::app::H) elona::app::unsupported("BMSCR pointer", "screen " + std::to_string(id));
    throw HSPERR_UNSUPPORTED_FUNCTION;
}

void ex_mref(PVal* pval, int prm) {
    using elona::app::H;
    switch (prm) {
    case 0x40: HspVarCoreDupPtr(pval, HSPVAR_FLAG_INT, &H->ctx->stat, sizeof(int)); return;
    case 0x41: HspVarCoreDupPtr(pval, HSPVAR_FLAG_STR, H->ctx->refstr, 1024); return;
    default:
        if (H) elona::app::unsupported("mref", std::to_string(prm));
        throw HSPERR_UNSUPPORTED_FUNCTION;
    }
}

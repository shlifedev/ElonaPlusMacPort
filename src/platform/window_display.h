// Presentation/input boundary for the GUI host. The SDL3 implementation lives
// in window_sdl.cpp and is only built with ELONA_GUI=ON; the headless
// implementation has no dependencies so headless builds stay possible.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "graphics/hsp_screen.h"

namespace elona::platform {

struct InputEvent {
    enum Kind { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, Wheel, Text, TextEditing, CloseRequest, FocusGained, FocusLost } kind;
    int window = 0;   // HSP screen id
    int vk = 0;       // Windows virtual-key code (KeyDown/KeyUp)
    bool repeat = false;
    int x = 0, y = 0;  // client coordinates in HSP pixels
    int button = 0;   // 0 left, 1 right, 2 middle (WM_*BUTTONDOWN order)
    int wheel = 0;    // WHEEL_DELTA units (120 per notch)
    std::string text_utf8;
    int edit_start = 0, edit_length = 0;  // TextEditing: Unicode character counts, not UTF-8 bytes
    enum class CloseSource { unknown, application_quit, window_close, script };
    CloseSource close_source = CloseSource::unknown;
};

struct WindowSpec {
    int wid = 0;
    int width = 640, height = 480;  // client size (HSP wx/wy)
    int x = -1, y = -1;
    bool borderless = false;
    bool visible = true;
    std::string title_utf8;
};

struct DialogLabels {
    std::string ok = "OK", yes = "Yes", no = "No";
};

class Display {
public:
    virtual ~Display() = default;
    virtual const char* name() const = 0;
    virtual bool has_windows() const = 0;
    // Create or reconfigure the window for a main/bgscr screen.
    virtual bool configure_window(const WindowSpec& spec, std::string& error) = 0;
    virtual void hide_window(int wid) = 0;
    virtual void set_title(int wid, const std::string& title_utf8) = 0;
    // Show dib[viewx.., viewy..] at the window's client size.
    virtual void present(int wid, const gfx::Dib& dib, int viewx, int viewy, int wx, int wy) = 0;
    // Non-blocking event collection; `timeout_ms` > 0 waits at most that long for the first event.
    virtual void poll(std::vector<InputEvent>& out, int timeout_ms) = 0;
    // Read back what was actually presented (top-down RGB24) for verification.
    virtual bool capture(int wid, std::vector<std::uint8_t>& rgb, int& width, int& height, std::string& error) = 0;
    virtual bool set_fullscreen(int wid, bool on, std::string& error) = 0;
    virtual bool is_fullscreen(int wid) const = 0;
    virtual bool desktop_size(int& w, int& h) = 0;
    virtual bool window_rect(int wid, int& x, int& y, int& w, int& h) = 0;
    virtual bool has_focus(int wid) = 0;
    // Enable/disable OS text input (IME, composed characters) for a window.
    virtual void set_text_input(int wid, bool on) = 0;
    // Optional native IME positioning/cancellation. Coordinates are HSP client
    // pixels; SDL translates them to window coordinates (letterbox/HiDPI).
    virtual void set_text_input_area(int wid, int x, int y, int w, int h, int cursor) {}
    virtual void clear_composition(int wid) {}
    // Modal message box. buttons: 0 = OK, 1 = Yes/No. Returns 1 (IDOK), 6 (IDYES),
    // 7 (IDNO), or -1 when no interactive UI is available.
    virtual int message_box(int wid, const std::string& text_utf8, const std::string& title_utf8,
                            bool warning, int buttons, const DialogLabels& labels) = 0;
    // Diagnostic only: error from the last message_box failure, if available.
    virtual std::string message_box_error() const { return {}; }
};

std::unique_ptr<Display> create_headless_display();
#ifdef ELONA_GUI
std::unique_ptr<Display> create_sdl_display(std::string& error);
#endif

}  // namespace elona::platform

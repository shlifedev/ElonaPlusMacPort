// SDL3 window/event/presentation backend for the HSP GUI host.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "keymap_vk.h"
#include "window_display.h"

namespace elona::platform {
namespace {

struct Win {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int tex_w = 0, tex_h = 0;
    int client_w = 0, client_h = 0;
    std::vector<std::uint8_t> staging;
};

class SdlDisplay final : public Display {
public:
    ~SdlDisplay() override {
        for (auto& [id, w] : windows_) destroy(w);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    const char* name() const override { return "sdl3"; }
    bool has_windows() const override { return true; }

    bool configure_window(const WindowSpec& spec, std::string& error) override {
        Win& w = windows_[spec.wid];
        if (!w.window) {
            SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
            if (spec.borderless) flags |= SDL_WINDOW_BORDERLESS;
            if (!spec.visible) flags |= SDL_WINDOW_HIDDEN;
            w.window = SDL_CreateWindow(spec.title_utf8.c_str(), spec.width, spec.height, flags);
            if (!w.window) { error = std::string("SDL_CreateWindow: ") + SDL_GetError(); return false; }
            w.renderer = SDL_CreateRenderer(w.window, nullptr);
            if (!w.renderer) { error = std::string("SDL_CreateRenderer: ") + SDL_GetError(); return false; }
            SDL_SetRenderVSync(w.renderer, 0);
        } else {
            SDL_SetWindowSize(w.window, spec.width, spec.height);
            SDL_SetWindowBordered(w.window, !spec.borderless);
            SDL_SetWindowTitle(w.window, spec.title_utf8.c_str());
            if (spec.visible) SDL_ShowWindow(w.window);
        }
        if (spec.x >= 0 && spec.y >= 0) SDL_SetWindowPosition(w.window, spec.x, spec.y);
        w.client_w = spec.width;
        w.client_h = spec.height;
        SDL_SetRenderLogicalPresentation(w.renderer, spec.width, spec.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        if (spec.visible) SDL_ShowWindow(w.window);
        return true;
    }

    void hide_window(int wid) override {
        auto it = windows_.find(wid);
        if (it != windows_.end() && it->second.window) SDL_HideWindow(it->second.window);
    }
    void set_title(int wid, const std::string& title) override {
        auto it = windows_.find(wid);
        if (it != windows_.end() && it->second.window) SDL_SetWindowTitle(it->second.window, title.c_str());
    }

    void present(int wid, const gfx::Dib& dib, int viewx, int viewy, int wx, int wy) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.window) return;
        Win& w = it->second;
        wx = std::max(1, std::min(wx, dib.width()));
        wy = std::max(1, std::min(wy, dib.height()));
        if (!w.texture || w.tex_w != wx || w.tex_h != wy) {
            if (w.texture) SDL_DestroyTexture(w.texture);
            w.texture = SDL_CreateTexture(w.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, wx, wy);
            if (!w.texture) return;
            SDL_SetTextureScaleMode(w.texture, SDL_SCALEMODE_NEAREST);
            w.tex_w = wx;
            w.tex_h = wy;
        }
        w.staging.assign(std::size_t(wx) * wy * 3, 0);
        for (int y = 0; y < wy; ++y) {
            int sy = viewy + y;
            if (sy < 0 || sy >= dib.height()) continue;
            const std::uint8_t* s = dib.row(sy);
            std::uint8_t* d = &w.staging[std::size_t(y) * wx * 3];
            for (int x = 0; x < wx; ++x) {
                int sx = viewx + x;
                if (sx < 0 || sx >= dib.width()) continue;
                d[x * 3] = s[sx * 3 + 2];
                d[x * 3 + 1] = s[sx * 3 + 1];
                d[x * 3 + 2] = s[sx * 3];
            }
        }
        SDL_UpdateTexture(w.texture, nullptr, w.staging.data(), wx * 3);
        SDL_SetRenderDrawColor(w.renderer, 0, 0, 0, 255);
        SDL_RenderClear(w.renderer);
        SDL_RenderTexture(w.renderer, w.texture, nullptr, nullptr);
        SDL_RenderPresent(w.renderer);
    }

    void poll(std::vector<InputEvent>& out, int timeout_ms) override {
        SDL_Event e;
        bool have = timeout_ms > 0 ? SDL_WaitEventTimeout(&e, timeout_ms) : SDL_PollEvent(&e);
        while (have) {
            translate(e, out);
            have = SDL_PollEvent(&e);
        }
    }

    bool capture(int wid, std::vector<std::uint8_t>& rgb, int& width, int& height, std::string& error) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.renderer) { error = "no window for screen " + std::to_string(wid); return false; }
        Win& w = it->second;
        if (!w.texture) { error = "nothing presented yet"; return false; }
        // The back buffer is undefined after SDL_RenderPresent: redraw the last
        // frame and read it back before presenting again.
        SDL_SetRenderDrawColor(w.renderer, 0, 0, 0, 255);
        SDL_RenderClear(w.renderer);
        SDL_RenderTexture(w.renderer, w.texture, nullptr, nullptr);
        SDL_Surface* s = SDL_RenderReadPixels(w.renderer, nullptr);
        SDL_RenderPresent(w.renderer);
        if (!s) { error = std::string("SDL_RenderReadPixels: ") + SDL_GetError(); return false; }
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface(s);
        if (!c) { error = std::string("SDL_ConvertSurface: ") + SDL_GetError(); return false; }
        width = c->w;
        height = c->h;
        rgb.resize(std::size_t(width) * height * 3);
        for (int y = 0; y < height; ++y)
            std::memcpy(&rgb[std::size_t(y) * width * 3], static_cast<std::uint8_t*>(c->pixels) + y * c->pitch, std::size_t(width) * 3);
        SDL_DestroySurface(c);
        return true;
    }

    bool set_fullscreen(int wid, bool on, std::string& error) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.window) { error = "no window"; return false; }
        SDL_Window* window = it->second.window;
        if (on && !SDL_SetWindowFullscreenMode(window, nullptr)) { error = SDL_GetError(); return false; }
        if (!SDL_SetWindowFullscreen(window, on) || !SDL_SyncWindow(window)) { error = SDL_GetError(); return false; }
        if (is_fullscreen(wid) != on) {
            error = "window manager did not apply fullscreen transition";
            return false;
        }
        return true;
    }
    bool is_fullscreen(int wid) const override {
        auto it = windows_.find(wid);
        return it != windows_.end() && it->second.window && (SDL_GetWindowFlags(it->second.window) & SDL_WINDOW_FULLSCREEN);
    }
    bool desktop_size(int& w, int& h) override {
        const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (!m) return false;
        w = m->w;
        h = m->h;
        return true;
    }
    bool window_rect(int wid, int& x, int& y, int& w, int& h) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.window) return false;
        SDL_GetWindowPosition(it->second.window, &x, &y);
        SDL_GetWindowSize(it->second.window, &w, &h);
        return true;
    }
    bool has_focus(int wid) override {
        auto it = windows_.find(wid);
        return it != windows_.end() && it->second.window && (SDL_GetWindowFlags(it->second.window) & SDL_WINDOW_INPUT_FOCUS);
    }

    void set_text_input(int wid, bool on) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.window) return;
        // Elona's keylog calls objsel on every frame. Restarting text input
        // here would cancel an in-progress composition on some platforms.
        if (on && !SDL_TextInputActive(it->second.window)) SDL_StartTextInput(it->second.window);
        else if (!on && SDL_TextInputActive(it->second.window)) SDL_StopTextInput(it->second.window);
    }

    void clear_composition(int wid) override {
        auto it = windows_.find(wid);
        if (it != windows_.end() && it->second.window) SDL_ClearComposition(it->second.window);
    }

    void set_text_input_area(int wid, int x, int y, int w, int h, int cursor) override {
        auto it = windows_.find(wid);
        if (it == windows_.end() || !it->second.window) return;
        float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h), cx = float(x + cursor), cy = float(y);
        SDL_RenderCoordinatesToWindow(it->second.renderer, x0, y0, &x0, &y0);
        SDL_RenderCoordinatesToWindow(it->second.renderer, x1, y1, &x1, &y1);
        SDL_RenderCoordinatesToWindow(it->second.renderer, cx, cy, &cx, &cy);
        SDL_Rect area{int(x0), int(y0), std::max(1, int(x1 - x0)), std::max(1, int(y1 - y0))};
        SDL_SetTextInputArea(it->second.window, &area, std::max(0, int(cx - x0)));
    }

    int message_box(int wid, const std::string& text, const std::string& title, bool warning, int buttons) override {
        message_box_error_.clear();
        SDL_MessageBoxButtonData ok[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "OK"}};
        SDL_MessageBoxButtonData yn[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 6, "Yes"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 7, "No"}};
        SDL_MessageBoxData data{};
        data.flags = warning ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
        auto it = windows_.find(wid);
        data.window = it != windows_.end() ? it->second.window : nullptr;
        data.title = title.c_str();
        data.message = text.c_str();
        data.numbuttons = buttons ? 2 : 1;
        data.buttons = buttons ? yn : ok;
        int pressed = -1;
        if (!SDL_ShowMessageBox(&data, &pressed)) {
            message_box_error_ = std::string("SDL_ShowMessageBox: ") + SDL_GetError();
            return -1;
        }
        return pressed;
    }
    std::string message_box_error() const override { return message_box_error_; }

private:
    std::string message_box_error_;
    int wid_of(SDL_WindowID id) {
        for (auto& [wid, w] : windows_)
            if (w.window && SDL_GetWindowID(w.window) == id) return wid;
        return 0;
    }
    SDL_Renderer* renderer_of(int wid) {
        auto it = windows_.find(wid);
        return it == windows_.end() ? nullptr : it->second.renderer;
    }
    void translate(SDL_Event& e, std::vector<InputEvent>& out) {
        InputEvent ev{InputEvent::KeyDown};
        switch (e.type) {
        case SDL_EVENT_QUIT:
            ev.kind = InputEvent::CloseRequest;
            ev.close_source = InputEvent::CloseSource::application_quit;
            out.push_back(ev);
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            ev.kind = InputEvent::CloseRequest;
            ev.close_source = InputEvent::CloseSource::window_close;
            ev.window = wid_of(e.window.windowID);
            out.push_back(ev);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            ev.kind = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? InputEvent::FocusGained : InputEvent::FocusLost;
            ev.window = wid_of(e.window.windowID);
            out.push_back(ev);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            int vk = scancode_to_vk(int(e.key.scancode));
            if (!vk) break;
            ev.kind = e.type == SDL_EVENT_KEY_DOWN ? InputEvent::KeyDown : InputEvent::KeyUp;
            ev.vk = vk;
            ev.repeat = e.key.repeat;
            ev.window = wid_of(e.key.windowID);
            out.push_back(ev);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL: {
            SDL_WindowID wid_sdl = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.windowID
                                   : e.type == SDL_EVENT_MOUSE_WHEEL ? e.wheel.windowID : e.button.windowID;
            ev.window = wid_of(wid_sdl);
            if (SDL_Renderer* r = renderer_of(ev.window)) SDL_ConvertEventToRenderCoordinates(r, &e);
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                ev.kind = InputEvent::MouseMove;
                ev.x = int(e.motion.x);
                ev.y = int(e.motion.y);
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                ev.kind = InputEvent::Wheel;
                ev.wheel = int(e.wheel.y * 120);
                ev.x = int(e.wheel.mouse_x);
                ev.y = int(e.wheel.mouse_y);
            } else {
                ev.kind = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEvent::MouseDown : InputEvent::MouseUp;
                ev.button = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : 2;
                ev.x = int(e.button.x);
                ev.y = int(e.button.y);
            }
            out.push_back(ev);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            ev.kind = InputEvent::Text;
            ev.text_utf8 = e.text.text ? e.text.text : "";
            ev.window = wid_of(e.text.windowID);
            out.push_back(ev);
            break;
        case SDL_EVENT_TEXT_EDITING:
            ev.kind = InputEvent::TextEditing;
            ev.text_utf8 = e.edit.text ? e.edit.text : "";
            ev.edit_start = e.edit.start;
            ev.edit_length = e.edit.length;
            ev.window = wid_of(e.edit.windowID);
            out.push_back(ev);
            break;
        default:
            break;
        }
    }
    static void destroy(Win& w) {
        if (w.texture) SDL_DestroyTexture(w.texture);
        if (w.renderer) SDL_DestroyRenderer(w.renderer);
        if (w.window) SDL_DestroyWindow(w.window);
        w = Win{};
    }
    std::map<int, Win> windows_;
};

}  // namespace

std::unique_ptr<Display> create_sdl_display(std::string& error) {
    SDL_SetHint(SDL_HINT_APP_NAME, "ElonaPlus Native");
#ifdef _WIN32
    // The game draws into a CPU DIB. Present it through the native window
    // surface so Win32 also sees the *current* frame after a window resize.
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_DEFAULT);
    SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0", SDL_HINT_DEFAULT);
#endif
    // The edit overlay draws the preedit; candidate selection remains native.
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error = std::string("SDL_Init(VIDEO): ") + SDL_GetError();
        return nullptr;
    }
    return std::make_unique<SdlDisplay>();
}

}  // namespace elona::platform

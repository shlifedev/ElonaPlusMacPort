// Synthetic SDL event boundary check; this is NOT an OS Japanese IME test.
// Creates only its own hidden windows and injects events into those window IDs.
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "platform/window_display.h"

int main() {
    using namespace elona::platform;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s (%s)\n", what, SDL_GetError()); }
    };
    std::string error;
    auto d = create_sdl_display(error);
    if (!d) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    WindowSpec spec;
    spec.wid = 7; spec.width = 320; spec.height = 120; spec.visible = false;
    spec.title_utf8 = "Elona IME event test (hidden)";
    if (!d->configure_window(spec, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    check(count == 1, "one owned window");
    if (count != 1) { SDL_free(windows); return 1; }
    SDL_Window* w = windows[0];
    SDL_free(windows);
    const SDL_WindowID id = SDL_GetWindowID(w);
    d->set_text_input(7, true);
    check(SDL_TextInputActive(w), "text input started");
    d->set_text_input(7, true);
    check(SDL_TextInputActive(w), "repeated focus keeps text input active");
    check(std::strcmp(SDL_GetHint(SDL_HINT_IME_IMPLEMENTED_UI), "composition") == 0, "native candidates / app preedit contract");
    d->set_text_input_area(7, 10, 20, 100, 16, 24);
    SDL_Rect area{}; int cursor = 0;
    check(SDL_GetTextInputArea(w, &area, &cursor), "input area query");
    check(area.x == 10 && area.y == 20 && area.w == 100 && area.h == 16 && cursor == 24, "input area coordinates");
    std::vector<InputEvent> got;
    d->poll(got, 0); got.clear();
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_EDITING;
    e.edit.windowID = id; e.edit.text = u8"日本"; e.edit.start = 1; e.edit.length = 1;
    check(SDL_PushEvent(&e), "push composition");
    e = {};
    e.type = SDL_EVENT_TEXT_INPUT; e.text.windowID = id; e.text.text = u8"日本";
    check(SDL_PushEvent(&e), "push commit");
    e = {};
    e.type = SDL_EVENT_TEXT_EDITING; e.edit.windowID = id; e.edit.text = ""; e.edit.start = -1; e.edit.length = -1;
    check(SDL_PushEvent(&e), "push cancel");
    d->poll(got, 0);
    std::vector<InputEvent> text;
    for (auto& v : got) if (v.kind == InputEvent::Text || v.kind == InputEvent::TextEditing) text.push_back(v);
    check(text.size() == 3, "three translated text events");
    if (text.size() == 3) {
        check(text[0].kind == InputEvent::TextEditing && text[0].window == 7 && text[0].text_utf8 == u8"日本"
              && text[0].edit_start == 1 && text[0].edit_length == 1, "composition preserves UTF-8 and character selection");
        check(text[1].kind == InputEvent::Text && text[1].window == 7 && text[1].text_utf8 == u8"日本", "commit routes to HSP screen 7");
        check(text[2].kind == InputEvent::TextEditing && text[2].text_utf8.empty()
              && text[2].edit_start == -1 && text[2].edit_length == -1, "cancel / unset range preserved");
    }
    d->clear_composition(7);
    check(SDL_TextInputActive(w), "cancel does not disable text input");
    d->set_text_input(7, false);
    check(!SDL_TextInputActive(w), "text input stopped");
    got.clear();
    e = {};
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED; e.window.windowID = id;
    check(SDL_PushEvent(&e), "push window close");
    e = {}; e.type = SDL_EVENT_QUIT;
    check(SDL_PushEvent(&e), "push application quit");
    d->poll(got, 0);
    std::vector<InputEvent> close_events;
    for (auto& v : got) if (v.kind == InputEvent::CloseRequest) close_events.push_back(v);
    check(close_events.size() == 2, "two translated close requests");
    if (close_events.size() == 2) {
        check(close_events[0].window == 7 && close_events[0].close_source == InputEvent::CloseSource::window_close,
              "window close records source and target");
        check(close_events[1].close_source == InputEvent::CloseSource::application_quit,
              "application quit remains distinct from window close");
    }
    std::printf("SDL synthetic IME/close boundary: %d checks, %d failures; actual OS IME not tested\n", checks, failures);
    return failures ? 1 : 0;
}

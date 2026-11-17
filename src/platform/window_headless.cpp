#include "window_display.h"

namespace elona::platform {
namespace {
class HeadlessDisplay final : public Display {
public:
    const char* name() const override { return "headless"; }
    bool has_windows() const override { return false; }
    bool configure_window(const WindowSpec&, std::string&) override { return true; }
    void hide_window(int) override {}
    void set_title(int, const std::string&) override {}
    void present(int, const gfx::Dib&, int, int, int, int) override {}
    void poll(std::vector<InputEvent>&, int) override {}
    bool capture(int, std::vector<std::uint8_t>&, int&, int&, std::string& error) override {
        error = "headless display has no window to capture";
        return false;
    }
    bool set_fullscreen(int, bool, std::string& error) override {
        error = "headless display has no window";
        return false;
    }
    bool desktop_size(int& w, int& h) override {
        w = 1920;  // fixed, documented value for deterministic headless runs
        h = 1080;
        return true;
    }
    bool window_rect(int, int&, int&, int&, int&) override { return false; }
    bool has_focus(int) override { return true; }
    void set_text_input(int, bool) override {}
    int message_box(int, const std::string&, const std::string&, bool, int) override { return -1; }
    std::string message_box_error() const override { return "headless display has no interactive message box"; }
};
}  // namespace

std::unique_ptr<Display> create_headless_display() { return std::make_unique<HeadlessDisplay>(); }

}  // namespace elona::platform

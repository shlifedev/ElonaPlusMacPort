// CPU pixel model for HSP3 screens/buffers.
//
// Each screen owns a Win32-HSP3-compatible device-independent bitmap:
// 24-bit BGR, bottom-up rows, stride padded to 4 bytes (Bmscr::Init in
// OpenHSP src/hsp3/win32gui/hspwnd_win.cpp). Keeping the exact memory layout
// lets the copy/blend code be ported literally, makes bmpsave byte-identical,
// and gives native replacements of original pixel DLLs (gfini/gfdec2/...) the
// same view that the x86 DLLs had through BMSCR.pBit.
//
// This header has no SDL or OS dependency so headless builds and unit tests
// can use it on every platform.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace elona::gfx {

using colorref = std::uint32_t;  // Win32 COLORREF layout: 0x00BBGGRR
constexpr colorref kInvalidColor = 0xffffffffu;  // CLR_INVALID (GetPixel outside)

constexpr colorref rgb(int r, int g, int b) {
    return colorref(r & 255) | (colorref(g & 255) << 8) | (colorref(b & 255) << 16);
}
constexpr int red_of(colorref c) { return int(c & 255); }
constexpr int green_of(colorref c) { return int((c >> 8) & 255); }
constexpr int blue_of(colorref c) { return int((c >> 16) & 255); }

// HSPWND_TYPE_* values from hspwnd_win.h
enum ScreenType : int { kTypeNone = 0, kTypeBuffer = 1, kTypeMain = 2, kTypeBgscr = 3 };

// Explicit, catchable error for unimplemented graphics behaviour. The host
// converts it into a diagnostic plus an HSP error; it is never success.
struct Unsupported : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Dib {
public:
    Dib() = default;
    Dib(int width, int height) { reset(width, height); }
    void reset(int width, int height);
    int width() const { return sx_; }
    int height() const { return sy_; }
    int stride() const { return sx2_; }
    std::size_t byte_size() const { return bits_.size(); }
    std::uint8_t* data() { return bits_.data(); }
    const std::uint8_t* data() const { return bits_.data(); }
    // Row in top-down screen coordinates (memory is bottom-up).
    std::uint8_t* row(int y) { return bits_.data() + std::size_t(sy_ - 1 - y) * sx2_; }
    const std::uint8_t* row(int y) const { return bits_.data() + std::size_t(sy_ - 1 - y) * sx2_; }
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < sx_ && y < sy_; }
    colorref get(int x, int y) const {
        const std::uint8_t* p = row(y) + x * 3;
        return rgb(p[2], p[1], p[0]);
    }
    void set(int x, int y, colorref c) {
        std::uint8_t* p = row(y) + x * 3;
        p[0] = std::uint8_t(blue_of(c));
        p[1] = std::uint8_t(green_of(c));
        p[2] = std::uint8_t(red_of(c));
    }
    void fill(colorref c);
    // Write a top-down RGB24/BGRX copy for presentation or tests.
    void copy_top_down_rgb24(std::vector<std::uint8_t>& out) const;

private:
    int sx_ = 0, sy_ = 0, sx2_ = 0;
    std::vector<std::uint8_t> bits_;
};

struct FontSpec {
    std::string face_cp932;  // exactly as passed to `font` (CP932 bytes)
    int pixel_height = 14;   // lfHeight = -fpts (character height in pixels)
    int style = 0;           // bit0 bold, bit1 italic, bit2 underline, bit3 strike, bit4 antialias
    int stock = 0;           // non-zero: sysfont stock object id
    bool operator==(const FontSpec& o) const {
        return face_cp932 == o.face_cp932 && pixel_height == o.pixel_height && style == o.style && stock == o.stock;
    }
};

struct TextExtent {
    int cx = 0;
    int cy = 0;
};

// Text drawing boundary. Strings are CP932 bytes; conversion to Unicode happens
// inside the implementation (UI boundary only).
class TextRenderer {
public:
    virtual ~TextRenderer() = default;
    // Validate/select the font. Returns false when the font cannot be created
    // (HSP sets stat=-1 for font in that case).
    virtual bool prepare(const FontSpec& spec) = 0;
    // Native UI can share the renderer's loaded display catalog.
    virtual const std::string* lookup_translation(std::string_view) const { return nullptr; }
    // Draw one line (no line breaks) at top-left (x,y) with transparent
    // background; returns the TabbedTextOut-equivalent extent.
    virtual TextExtent draw_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) = 0;
    // Game display text may be localized; editable values must use draw_line
    // so a player name/selection is never silently replaced by a translation.
    virtual TextExtent draw_display_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) {
        return draw_line(spec, dib, x, y, cp932, color);
    }
    virtual void begin_english_log(std::string_view, int, int, int) {}
    virtual void set_log_word_draw(bool) {}
    virtual void set_hud_draw(bool) {}
    virtual int alias_label_width(const FontSpec&) { return 0; }
    virtual void set_menu_draw(int, std::string_view) {}
    virtual bool draw_input_caption(Dib&, int, int) { return false; }
    virtual bool draw_book(Dib&, int, std::string_view, int, int, int) { return false; }
    virtual void prepare_board_row(const std::array<std::string, 7>&, int) {}
    virtual void prepare_item_row(std::string_view, std::string_view, std::string_view, int) {}
    // Explicit whole-paragraph boundary for managed game-source transforms.
    // -1 means no reviewed whole-string translation: caller runs original gmes.
    virtual int draw_localized_paragraph(Dib&, int, int, int, bool, colorref,
                                         std::string_view, FontSpec&) { return -1; }
    // Extent of " " (used by empty lines) and line height.
    virtual TextExtent measure_line(const FontSpec& spec, std::string_view cp932) = 0;
    virtual std::string describe(const FontSpec& spec) = 0;
};

class Screen {
public:
    Screen(int id, int type, int width, int height, TextRenderer* text);

    int wid;
    int type;
    Dib dib;
    TextRenderer* text;

    // HSP drawing state (Bmscr fields)
    int cx = 0, cy = 0;
    colorref color = 0, objcolor = 0;
    int gmode = 0, gx = 32, gy = 32, gfrate = 0;
    int ox = 64, oy = 24, py = 0;
    int textspeed = 0;
    int fl_udraw = 1, fl_dispw = 1;
    int viewx = 0, viewy = 0;
    int wx = 0, wy = 0;          // client (visible) size for windows
    int window_x = -1, window_y = -1;
    TextExtent printsize;
    int fonteff_size = 1;
    int divsx = 0, divsy = 0, divx = 1, divy = 1, celofsx = 0, celofsy = 0;
    std::array<int, 8> savepos{};  // mouse x/y/z/w etc.
    std::string resname;
    std::string title_cp932;
    FontSpec font;
    int objmode = 1, tabmove = 1;
    bool hidden = false;
    // presentation bookkeeping
    bool dirty = true;

    int width() const { return dib.width(); }
    int height() const { return dib.height(); }

    void cls(int mode);
    void set_font_default();  // Sysfont(0): FixedSys 14
    void boxfill(int x1, int y1, int x2, int y2);
    void pset(int x, int y);
    colorref pget(int x, int y);  // also selects the colour (Win32 Pget)
    void line_to(int x, int y);
    void circle(int x1, int y1, int x2, int y2, int fill);
    // gcopy: returns non-zero on palette-mode mismatch (HSPERR_UNSUPPORTED_FUNCTION)
    int copy(const Screen& src, int xx, int yy, int psx, int psy);
    int zoom(int dx, int dy, const Screen& src, int xx, int yy, int psx, int psy, int mode);
    void print(std::string_view cp932, int option, bool localize = true);
    void set_hsv_color(int h, int s, int v);
    void set_cel_divide(int divsx, int divsy, int ofsx, int ofsy);
    int cel_put(const Screen& src, int id);
    void gradfill(int x, int y, int sx, int sy, int mode, colorref col1_rgb, colorref col2_rgb);
    void width_request(int x, int y, int wposx, int wposy, int mode);
    void set_scroll(int xbase, int ybase);
    // Render RGBA (stb_image order) at (cx,cy) with Win32 RenderAlphaBitmap rules.
    int render_rgba(int w, int h, int components, const std::uint8_t* rgba);
    void mark_dirty() { dirty = true; }
    void posinc(int pp) { cy += (pp < py) ? py : pp; }  // Bmscr::Posinc

private:
    void blt(int mode, const Screen& src, int xx, int yy, int asx, int asy);
    int print_sub(std::string_view cp932, bool localize);
    void print_line(std::string_view line, bool localize);
};

// HspWnd-equivalent table of screens.
class ScreenTable {
public:
    explicit ScreenTable(TextRenderer* text) : text_(text) {}
    Screen* get(int id) { return (id >= 0 && id < int(screens_.size())) ? screens_[id].get() : nullptr; }
    Screen& get_safe(int id);  // throws std::out_of_range-equivalent via HSP error in host
    Screen& make(int id, int type, int width, int height, int mode);
    int empty_id() const;
    int size() const { return int(screens_.size()); }
    TextRenderer* text() const { return text_; }

private:
    TextRenderer* text_;
    std::vector<std::unique_ptr<Screen>> screens_;
};

struct InvalidScreen : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace elona::gfx

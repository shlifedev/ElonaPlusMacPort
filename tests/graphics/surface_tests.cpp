// Unit tests for the CPU DIB pixel model. Expected values come from the ported
// Win32 integer formulas and from the Windows HSP 3.4 x86 GUI reference run
// (work/windows/reference-build/gui-fixtures-0x: gcopy gmode0/2, nearest 2x gzoom).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "graphics/hsp_screen.h"
#include "graphics/image_io.h"
#include "graphics/poly_raster.h"

using namespace elona::gfx;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static colorref pat(int x, int y) { return rgb(x * 17, y * 17, (x + y) * 8); }

static void test_layout() {
    Dib d(5, 3);
    CHECK(d.stride() == 16);  // 15 bytes padded to 16
    CHECK(d.byte_size() == 48);
    d.set(0, 0, rgb(1, 2, 3));
    // top row lives at the end of memory (bottom-up), BGR order
    CHECK(d.data()[32] == 3 && d.data()[33] == 2 && d.data()[34] == 1);
    auto bmp = encode_bmp(d);
    CHECK(bmp.size() == 14 + 40 + 48);
    CHECK(bmp[0] == 'B' && bmp[1] == 'M');
    CHECK(bmp[28] == 24);
}

static void test_bitmap_export_reference() {
    // Windows reference: 8x8 buffer filled with (17,34,51) -> 246-byte BMP
    ScreenTable t(nullptr);
    Screen& b = t.make(1, kTypeBuffer, 8, 8, 0);
    b.color = rgb(17, 34, 51);
    b.boxfill(0, 0, 7, 7);
    auto bmp = encode_bmp(b.dib);
    CHECK(bmp.size() == 246);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) CHECK(b.dib.get(x, y) == rgb(17, 34, 51));
}

static void test_cls_and_boxf() {
    ScreenTable t(nullptr);
    Screen& s = t.make(0, kTypeBuffer, 4, 4, 0);
    CHECK(s.dib.get(0, 0) == rgb(255, 255, 255));
    s.cls(4);
    CHECK(s.dib.get(3, 3) == 0);
    s.color = rgb(9, 9, 9);
    s.boxfill(3, 3, 1, 1);  // unnormalised: FillRect draws nothing
    CHECK(s.dib.get(2, 2) == 0);
    s.boxfill(-5, -5, 0, 0);
    CHECK(s.dib.get(0, 0) == rgb(9, 9, 9) && s.dib.get(1, 1) == 0);
}

static void test_pget() {
    ScreenTable t(nullptr);
    Screen& s = t.make(0, kTypeBuffer, 4, 4, 0);
    s.dib.set(1, 2, rgb(10, 20, 30));
    CHECK(s.pget(1, 2) == rgb(10, 20, 30));
    CHECK(s.color == rgb(10, 20, 30));
    s.pget(-1, 0);
    CHECK(s.color == kInvalidColor);
    CHECK(red_of(s.color) == 255 && blue_of(s.color) == 255);
}

static void test_reference_pattern() {
    // Mirrors tools/reference/gui_fixtures/gui_pattern_dump.hsp observed on Windows.
    ScreenTable t(nullptr);
    Screen& src = t.make(1, kTypeBuffer, 16, 16, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) { src.color = pat(x, y); src.pset(x, y); }
    Screen& dst = t.make(0, kTypeBuffer, 48, 48, 0);
    dst.gmode = 0; dst.cx = 0; dst.cy = 0; dst.copy(src, 0, 0, 16, 16);
    dst.gmode = 2; dst.cx = 16; dst.cy = 0; dst.copy(src, 0, 0, 16, 16);
    dst.gmode = 0; dst.cx = 0; dst.cy = 16; dst.zoom(32, 32, src, 0, 0, 16, 16, 0);
    struct { int x, y, r, g, b; } probes[] = {{0, 0, 0, 0, 0}, {1, 0, 17, 0, 8}, {16, 0, 255, 255, 255}, {17, 0, 17, 0, 8},
                                              {15, 15, 255, 255, 240}, {31, 15, 255, 255, 240}, {0, 16, 0, 0, 0},
                                              {1, 17, 0, 0, 0}, {31, 47, 255, 255, 240}};
    for (auto& p : probes) {
        colorref c = dst.pget(p.x, p.y);
        if (!(red_of(c) == p.r && green_of(c) == p.g && blue_of(c) == p.b))
            std::fprintf(stderr, "  probe (%d,%d) got %d,%d,%d\n", p.x, p.y, red_of(c), green_of(c), blue_of(c));
        CHECK(red_of(c) == p.r && green_of(c) == p.g && blue_of(c) == p.b);
    }
    for (int y = 16; y < 48; ++y)
        for (int x = 0; x < 32; ++x) CHECK(dst.dib.get(x, y) == pat(x / 2, (y - 16) / 2));
}

static void test_blend_modes() {
    ScreenTable t(nullptr);
    Screen& src = t.make(1, kTypeBuffer, 2, 1, 0);
    src.dib.set(0, 0, rgb(200, 100, 0));
    src.dib.set(1, 0, rgb(0, 0, 0));
    Screen& dst = t.make(0, kTypeBuffer, 2, 1, 0);
    auto reset = [&] { dst.dib.set(0, 0, rgb(100, 100, 100)); dst.dib.set(1, 0, rgb(100, 100, 100)); dst.cx = dst.cy = 0; };
    reset(); dst.gmode = 3; dst.gfrate = 128; dst.copy(src, 0, 0, 2, 1);
    CHECK(dst.dib.get(0, 0) == rgb((100 * 128 >> 8) + (200 * 128 >> 8), 100, 50));
    reset(); dst.gmode = 5; dst.gfrate = 256; dst.copy(src, 0, 0, 2, 1);
    CHECK(dst.dib.get(0, 0) == rgb(255, 200, 100));
    reset(); dst.gmode = 6; dst.gfrate = 256; dst.copy(src, 0, 0, 2, 1);
    CHECK(dst.dib.get(0, 0) == rgb(0, 0, 100));
    reset(); dst.gmode = 4; dst.gfrate = 256; dst.color = rgb(0, 0, 0); dst.copy(src, 0, 0, 2, 1);
    CHECK(dst.dib.get(1, 0) == rgb(100, 100, 100));  // colour key excluded
    reset(); dst.gmode = 9; dst.copy(src, 0, 0, 2, 1);  // no case in Win32 Copy
    CHECK(dst.dib.get(0, 0) == rgb(100, 100, 100));
}

static void test_overlap() {
    ScreenTable t(nullptr);
    Screen& s = t.make(0, kTypeBuffer, 4, 1, 0);
    for (int x = 0; x < 4; ++x) s.dib.set(x, 0, rgb(x, 0, 0));
    s.gmode = 0; s.cx = 1; s.cy = 0;
    s.copy(s, 0, 0, 3, 1);  // BitBlt is overlap-safe: 0 0 1 2
    CHECK(s.dib.get(1, 0) == rgb(0, 0, 0) && s.dib.get(2, 0) == rgb(1, 0, 0) && s.dib.get(3, 0) == rgb(2, 0, 0));
    for (int x = 0; x < 4; ++x) s.dib.set(x, 0, rgb(x + 1, 0, 0));
    s.gmode = 1; s.cx = 1;
    s.copy(s, 0, 0, 3, 1);  // Win32 Blt forward byte scan smears: 1 1 1 1
    for (int x = 0; x < 4; ++x) CHECK(s.dib.get(x, 0) == rgb(1, 0, 0));
}

static void test_clipping() {
    ScreenTable t(nullptr);
    Screen& src = t.make(1, kTypeBuffer, 4, 4, 0);
    src.dib.fill(rgb(1, 2, 3));
    Screen& dst = t.make(0, kTypeBuffer, 4, 4, 0);
    dst.cls(4);
    dst.gmode = 1; dst.cx = -2; dst.cy = 3;
    dst.copy(src, 0, 0, 4, 4);
    CHECK(dst.dib.get(0, 3) == rgb(1, 2, 3) && dst.dib.get(1, 3) == rgb(1, 2, 3) && dst.dib.get(2, 3) == 0);
    CHECK(dst.dib.get(0, 2) == 0);
}

static void test_rotate_identity() {
    // grotate with angle 0 maps the source 1:1 (fcpoly half-open spans)
    ScreenTable t(nullptr);
    Screen& src = t.make(1, kTypeBuffer, 8, 8, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) src.dib.set(x, y, pat(x, y));
    Screen& dst = t.make(0, kTypeBuffer, 16, 16, 0);
    dst.cls(4);
    PolyRaster raster;
    int tx0 = 0, ty0 = 8 - 1 - 0, tx1 = 7, ty1 = 8 - 1 - 7;
    auto p = make_sprite_poly(0, 0x100, 8, 8, 8, 8, 0.0, tx0, ty1, tx1, ty0, 4, 4);
    auto r = raster.draw_tex(dst.dib, src.dib, p);
    CHECK(r.res_y0 >= 0);
    int drawn = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) if (dst.dib.get(x, y) != 0) ++drawn;
    CHECK(drawn > 30);  // documented approximation until compared with Windows
}

int main() {
    test_layout();
    test_bitmap_export_reference();
    test_cls_and_boxf();
    test_pget();
    test_reference_pattern();
    test_blend_modes();
    test_overlap();
    test_clipping();
    test_rotate_identity();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("graphics surface tests passed");
    return 0;
}

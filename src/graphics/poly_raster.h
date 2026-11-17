// Port of OpenHSP win32gui/fcpoly.cpp (onion software/onitama 2001) for the
// full-colour (24-bit DIB) path used by grotate / grect / gsquare / celput.
// The original ran as 32-bit x86 code where DWORD_PTR is 32 bits wide; texture
// coordinate arithmetic here therefore uses uint32_t, and pointers are kept
// separate from that arithmetic.
#pragma once
#include <cstdint>

#include "hsp_screen.h"

namespace elona::gfx {

constexpr int kPolyAttrColorKey = 0x8000;

struct PolyVertex {
    short x = 0, y = 0, tx = 0, ty = 0;
};

struct Poly4 {
    int attr = 0;
    PolyVertex v[4];
    int color = 0;      // 0x00RRGGBB-in-byte-order (CnvRGB of COLORREF)
    int alpha = 0x100;  // GetAlphaOperation() value
};

struct PolyResult {
    int res_y0 = -1, res_y1 = -1;
    long long skipped_out_of_range = 0;  // texels the 32-bit original would read before the texture
};

class PolyRaster {
public:
    void set_color_key(colorref color);  // SetPolyColorKey (takes the Bmscr COLORREF)
    PolyResult draw_f4(Dib& dest, const Poly4& poly);
    PolyResult draw_tex(Dib& dest, const Dib& tex, const Poly4& poly);

private:
    struct Scan {
        int minX, maxX, minTx, minTy, maxTx, maxTy;
    };
    void scan_line(const Poly4& poly, int w, int h, int& start_y, int& end_y);
    std::uint8_t ck1_ = 0, ck2_ = 0, ck3_ = 0;
    std::vector<Scan> scan_;
};

// Win32 DrawSpriteEx / DrawRectEx / GRotateSub vertex setup.
Poly4 make_sprite_poly(int attr, int alpha, int x, int y, int sx, int sy, double ang, int tx0, int ty0, int tx1, int ty1,
                       int opx, int opy);
Poly4 make_rect_poly(int color, int alpha, int x, int y, int sx, int sy, double ang);

}  // namespace elona::gfx

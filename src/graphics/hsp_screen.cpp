// Port of the Win32 HSP3 Bmscr pixel semantics onto a CPU DIB.
// Reference: OpenHSP 3dbb8729 src/hsp3/win32gui/hspwnd_win.cpp
// (Copyright onion software/onitama, BSD-style license; see
// THIRD_PARTY_NOTICES.md). Functions marked "literal port" keep the original
// integer arithmetic, clipping order and in-place scan order, including the
// behaviour for overlapping source/destination rectangles in the same buffer.
// Functions marked "GDI approximation" emulate a Windows GDI call whose exact
// rasterisation is not documented and still require Windows pixel comparison.
#include "hsp_screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace elona::gfx {

void Dib::reset(int width, int height) {
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        throw Unsupported("buffer size out of supported range: " + std::to_string(width) + "x" + std::to_string(height));
    sx_ = width;
    sy_ = height;
    sx2_ = (width * 3 + 3) & ~3;
    bits_.assign(std::size_t(sx2_) * std::size_t(sy_), 0);
}

void Dib::fill(colorref c) {
    for (int y = 0; y < sy_; ++y)
        for (int x = 0; x < sx_; ++x) set(x, y, c);
}

void Dib::copy_top_down_rgb24(std::vector<std::uint8_t>& out) const {
    out.resize(std::size_t(sx_) * sy_ * 3);
    std::uint8_t* d = out.data();
    for (int y = 0; y < sy_; ++y) {
        const std::uint8_t* s = row(y);
        for (int x = 0; x < sx_; ++x) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d += 3;
            s += 3;
        }
    }
}

Screen::Screen(int id, int screen_type, int width, int height, TextRenderer* renderer)
    : wid(id), type(screen_type), dib(width, height), text(renderer) {
    // Bmscr::Init: Cls(0) with fl_dispw=0, then fl_dispw=fl_udraw=1.
    wx = width;
    wy = height;
    fl_dispw = 0;
    cls(0);
    fl_dispw = 1;
    fl_udraw = 1;
}

void Screen::set_font_default() {
    // Bmscr::Sysfont(0) -> Newfont("FixedSys", 14, 0, 0)
    font = FontSpec{"FixedSys", 14, 0, 0};
    if (text) text->prepare(font);
}

void Screen::cls(int mode) {
    // literal port of Bmscr::Cls (stock brushes WHITE/LTGRAY/GRAY/DKGRAY/BLACK)
    static const int levels[] = {255, 192, 128, 64, 0};
    if (mode < 0 || mode > 4) throw Unsupported("cls mode " + std::to_string(mode) + " (only stock brushes 0-4)");
    set_font_default();
    fonteff_size = 1;
    viewx = viewy = 0;
    cx = cy = 0;
    color = 0;
    objcolor = 0;
    int v = levels[mode];
    dib.fill(rgb(v, v, v));
    textspeed = 0;
    ox = 64;
    oy = 24;
    py = 0;
    gx = 32;
    gy = 32;
    gmode = 0;
    savepos.fill(0);
    set_cel_divide(0, 0, 0, 0);
    resname.clear();
    fl_udraw = fl_dispw;
    mark_dirty();
}

void Screen::boxfill(int x1, int y1, int x2, int y2) {
    // FillRect(left=x1, top=y1, right=x2+1, bottom=y2+1); unnormalised rects are empty.
    int left = std::max(x1, 0), top = std::max(y1, 0);
    int right = std::min(x2 + 1, width()), bottom = std::min(y2 + 1, height());
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) dib.set(x, y, color);
    mark_dirty();
}

void Screen::pset(int x, int y) {
    if (dib.contains(x, y)) dib.set(x, y, color);
    mark_dirty();
}

colorref Screen::pget(int x, int y) {
    // GetPixel returns CLR_INVALID outside the bitmap; Win32 Pget stores it as colour.
    color = dib.contains(x, y) ? dib.get(x, y) : kInvalidColor;
    return color;
}

void Screen::line_to(int xx, int yy) {
    // GDI approximation: LineTo with a 1-pixel cosmetic pen, end point excluded.
    int x0 = cx, y0 = cy;
    int dx = std::abs(xx - x0), sx = x0 < xx ? 1 : -1;
    int dy = -std::abs(yy - y0), sy = y0 < yy ? 1 : -1;
    int err = dx + dy;
    while (!(x0 == xx && y0 == yy)) {
        if (dib.contains(x0, y0)) dib.set(x0, y0, color);
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    cx = xx;
    cy = yy;
    mark_dirty();
}

void Screen::circle(int x1, int y1, int x2, int y2, int fill) {
    // GDI approximation: Ellipse() inside the [x1,x2) x [y1,y2) bounding box.
    int left = std::min(x1, x2), right = std::max(x1, x2) - 1;
    int top = std::min(y1, y2), bottom = std::max(y1, y2) - 1;
    if (right < left || bottom < top) return;
    double cxd = (left + right) / 2.0, cyd = (top + bottom) / 2.0;
    double rx = (right - left) / 2.0 + 0.5, ry = (bottom - top) / 2.0 + 0.5;
    auto inside = [&](int x, int y) {
        double nx = (x - cxd) / rx, ny = (y - cyd) / ry;
        return nx * nx + ny * ny <= 1.0;
    };
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!inside(x, y)) continue;
            bool edge = !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
            if ((fill || edge) && dib.contains(x, y)) dib.set(x, y, color);
        }
    }
    mark_dirty();
}

// literal port of Bmscr::Blt (24-bit path; palette buffers are rejected earlier)
void Screen::blt(int mode, const Screen& src, int xx, int yy, int asx, int asy) {
    int ssx = src.dib.width(), ssy = src.dib.height(), ssx2 = src.dib.stride();
    int vv, cx_, cy_, sx, sy, bsx, bsy;

    cx_ = this->cx; sx = asx; bsx = dib.width();
    if (xx < 0) { sx += xx; cx_ -= xx; xx = 0; }
    if ((xx + sx) >= ssx) sx -= (xx + sx) - ssx;
    if (cx_ > bsx) return;
    if (cx_ < 0) {
        sx = cx_ + sx;
        if (sx <= 0) return;
        xx -= cx_; cx_ = 0;
    }
    vv = bsx - sx;
    if (cx_ > vv) sx -= cx_ - vv;

    cy_ = this->cy; sy = asy; bsy = dib.height();
    if (yy < 0) { sy += yy; cy_ -= yy; yy = 0; }
    if ((yy + sy) >= ssy) sy -= (yy + sy) - ssy;
    if (cy_ > bsy) return;
    if (cy_ < 0) {
        sy = cy_ + sy;
        if (sy <= 0) return;
        yy -= cy_; cy_ = 0;
    }
    vv = bsy - sy;
    if (cy_ > vv) sy -= cy_ - vv;

    // The original loops simply do not execute for empty extents.
    if (sx <= 0 || sy <= 0) return;

    std::uint8_t* base = dib.data();
    const std::uint8_t* sbase = src.dib.data();
    // Same buffer: reads observe earlier writes exactly like the original.
    std::uint8_t* p = base + std::ptrdiff_t(bsy - cy_ - 1) * dib.stride() + cx_ * 3;
    const std::uint8_t* p2 = sbase + std::ptrdiff_t(ssy - yy - 1) * ssx2 + xx * 3;
    std::ptrdiff_t tofs = dib.stride() + sx * 3;
    std::ptrdiff_t sofs = ssx2 + sx * 3;

    switch (mode) {
    case 1:
        break;
    case 2:
        for (int b = 0; b < sy; ++b) {
            for (int a = 0; a < sx; ++a) {
                std::uint8_t a1 = *p2++, a2 = *p2++, a3 = *p2++;
                if (a1 | a2 | a3) { *p++ = a1; *p++ = a2; *p++ = a3; }
                else p += 3;
            }
            p -= tofs; p2 -= sofs;
        }
        return;
    case 3: {
        short srcht = short(gfrate), dstht, ha;
        if (gfrate > 256) srcht = 256;
        if (srcht <= 0) return;
        dstht = short(256 - srcht);
        if (dstht == 0) break;
        int n = sx * 3;
        for (int b = 0; b < sy; ++b) {
            for (int a = 0; a < n; ++a) {
                ha = short((short(*p) * dstht >> 8) + (short(*p2++) * srcht >> 8));
                if (ha > 255) ha = 255;
                *p++ = std::uint8_t(ha);
            }
            p -= tofs; p2 -= sofs;
        }
        return;
    }
    case 4: {
        short srcht = short(gfrate), dstht;
        if (gfrate > 256) srcht = 256;
        if (srcht <= 0) return;
        dstht = short(256 - srcht);
        short cl3 = short(color & 0xff), cl2 = short((color >> 8) & 0xff), cl1 = short((color >> 16) & 0xff);
        for (int b = 0; b < sy; ++b) {
            for (int a = 0; a < sx; ++a) {
                short ha1 = *p2++, ha2 = *p2++, ha3 = *p2++, ha;
                if (ha1 != cl1 || ha2 != cl2 || ha3 != cl3) {
                    ha = short((short(*p) * dstht >> 8) + (ha1 * srcht >> 8)); if (ha > 255) ha = 255; *p++ = std::uint8_t(ha);
                    ha = short((short(*p) * dstht >> 8) + (ha2 * srcht >> 8)); if (ha > 255) ha = 255; *p++ = std::uint8_t(ha);
                    ha = short((short(*p) * dstht >> 8) + (ha3 * srcht >> 8)); if (ha > 255) ha = 255; *p++ = std::uint8_t(ha);
                } else {
                    p += 3;
                }
            }
            p -= tofs; p2 -= sofs;
        }
        return;
    }
    case 5:
    case 6: {
        short srcht = short(gfrate), ha;
        if (gfrate > 256) srcht = 256;
        if (srcht <= 0) return;
        int n = sx * 3;
        for (int b = 0; b < sy; ++b) {
            for (int a = 0; a < n; ++a) {
                if (mode == 5) {
                    ha = short(short(*p) + (short(*p2++) * srcht >> 8));
                    if (ha > 255) ha = 255;
                } else {
                    ha = short(short(*p) - (short(*p2++) * srcht >> 8));
                    if (ha < 0) ha = 0;
                }
                *p++ = std::uint8_t(ha);
            }
            p -= tofs; p2 -= sofs;
        }
        return;
    }
    case 7: {
        // Pixel alpha: the alpha plane is the asx-wide region right of the source.
        int n = sx * 3, sx2 = asx * 3;
        const std::uint8_t* send = sbase + src.dib.byte_size();
        for (int b = 0; b < sy; ++b) {
            for (int a = 0; a < n; ++a) {
                const std::uint8_t* ap = p2 + sx2;
                if (ap < sbase || ap >= send)
                    throw Unsupported("gmode 7 alpha plane outside source buffer (original reads out of bounds)");
                short srcht = *ap, ha;
                if (srcht != 0) {
                    if (srcht == 255) *p = *p2;
                    else {
                        ha = short((short(*p) * (255 - srcht) >> 8) + (short(*p2) * srcht >> 8));
                        *p = std::uint8_t(ha);
                    }
                }
                ++p; ++p2;
            }
            p -= tofs; p2 -= sofs;
        }
        return;
    }
    }
    // FC normal copy
    int n = sx * 3;
    for (int b = 0; b < sy; ++b) {
        for (int a = 0; a < n; ++a) *p++ = *p2++;
        p -= tofs; p2 -= sofs;
    }
}

int Screen::copy(const Screen& src, int xx, int yy, int psx, int psy) {
    switch (gmode) {
    case 0: {
        // GDI BitBlt SRCCOPY: rectangle intersection clipping, overlap-safe.
        if (psx <= 0 || psy <= 0) break;
        int dx0 = cx, dy0 = cy, sx0 = xx, sy0 = yy, w = psx, h = psy;
        if (sx0 < 0) { dx0 -= sx0; w += sx0; sx0 = 0; }
        if (sy0 < 0) { dy0 -= sy0; h += sy0; sy0 = 0; }
        if (dx0 < 0) { sx0 -= dx0; w += dx0; dx0 = 0; }
        if (dy0 < 0) { sy0 -= dy0; h += dy0; dy0 = 0; }
        w = std::min({w, src.width() - sx0, width() - dx0});
        h = std::min({h, src.height() - sy0, height() - dy0});
        if (w <= 0 || h <= 0) break;
        std::vector<std::uint8_t> tmp(std::size_t(w) * h * 3);
        for (int y = 0; y < h; ++y) std::memcpy(&tmp[std::size_t(y) * w * 3], src.dib.row(sy0 + y) + sx0 * 3, std::size_t(w) * 3);
        for (int y = 0; y < h; ++y) std::memcpy(dib.row(dy0 + y) + dx0 * 3, &tmp[std::size_t(y) * w * 3], std::size_t(w) * 3);
        break;
    }
    case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        blt(gmode, src, xx, yy, psx, psy);
        break;
    default:
        break;  // Win32 Copy has no default case: nothing is drawn.
    }
    mark_dirty();
    return 0;
}

int Screen::zoom(int dx, int dy, const Screen& src, int xx, int yy, int psx, int psy, int mode) {
    // GDI approximation: StretchBlt COLORONCOLOR (nearest). mode 1 (HALFTONE)
    // uses a box filter. Mirroring follows the sign of each extent.
    if (dx == 0 || dy == 0 || psx == 0 || psy == 0) { mark_dirty(); return 0; }
    bool mirror_x = (dx < 0) != (psx < 0), mirror_y = (dy < 0) != (psy < 0);
    int dleft = dx < 0 ? cx + dx + 1 : cx, dtop = dy < 0 ? cy + dy + 1 : cy;
    int dw = std::abs(dx), dh = std::abs(dy);
    int sleft = psx < 0 ? xx + psx + 1 : xx, stop = psy < 0 ? yy + psy + 1 : yy;
    int sw = std::abs(psx), sh = std::abs(psy);
    std::vector<std::uint8_t> snapshot;  // overlap-safe like GDI
    const Dib* source = &src.dib;
    Dib copy_dib;
    if (&src == this) { copy_dib = src.dib; source = &copy_dib; }
    for (int j = 0; j < dh; ++j) {
        int ty = dtop + j;
        if (ty < 0 || ty >= height()) continue;
        int jj = mirror_y ? dh - 1 - j : j;
        for (int i = 0; i < dw; ++i) {
            int tx = dleft + i;
            if (tx < 0 || tx >= width()) continue;
            int ii = mirror_x ? dw - 1 - i : i;
            if (mode == 0) {
                int sxp = sleft + int((long long)ii * sw / dw), syp = stop + int((long long)jj * sh / dh);
                if (source->contains(sxp, syp)) dib.set(tx, ty, source->get(sxp, syp));
            } else {
                int x0 = sleft + int((long long)ii * sw / dw), x1 = sleft + int((long long)(ii + 1) * sw / dw);
                int y0 = stop + int((long long)jj * sh / dh), y1 = stop + int((long long)(jj + 1) * sh / dh);
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                int r = 0, g = 0, b = 0, n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        if (source->contains(x, y)) { colorref c = source->get(x, y); r += red_of(c); g += green_of(c); b += blue_of(c); ++n; }
                if (n) dib.set(tx, ty, rgb(r / n, g / n, b / n));
            }
        }
    }
    mark_dirty();
    return 0;
}

void Screen::print(std::string_view mes, int option) {
    // literal port of Bmscr::Print (shadow/outline use objcolor)
    int x = cx, y = cy;
    colorref mycolor = color;
    auto sub_mul = [&](int sx, int sy, int px, int pyy, int times) {
        for (int i = 0; i < times; ++i) { cx = sx; cy = sy; print_sub(mes); sx += px; sy += pyy; }
    };
    if (option & 2) {
        int i = fonteff_size;
        if (i > 0) {
            color = objcolor;
            if (i == 1 || (option & 8)) { cx = x + i; cy = y + i; print_sub(mes); }
            else { sub_mul(x, y + i, 1, -1, i); sub_mul(x + i, y, -1, -1, i); }
        }
        color = mycolor; cx = x; cy = y;
    }
    if (option & 4) {
        int i = fonteff_size;
        if (i > 0) {
            color = objcolor;
            if (i == 1 || (option & 8)) {
                cx = x; cy = y - i; print_sub(mes);
                cx = x - i; cy = y; print_sub(mes);
                cx = x + i; cy = y; print_sub(mes);
                cx = x; cy = y + i; print_sub(mes);
            } else {
                sub_mul(x, y - i, -1, 1, i); sub_mul(x - i, y, 1, 1, i);
                sub_mul(x, y + i, 1, -1, i); sub_mul(x + i, y, -1, -1, i);
            }
        }
        color = mycolor; cx = x; cy = y;
    }
    int px = print_sub(mes);
    if (option & 1) { cx = x + px; cy = y; }
    mark_dirty();
}

int Screen::print_sub(std::string_view mes) {
    // strsp_get(mes, stmp, 0, 4095) loop: split on CR, CRLF and (HSPLINUX) LF.
    // The Windows reference only splits on CR/CRLF; a bare LF stays in the line.
    // CP932 lead bytes copy their trail byte unconditionally.
    int px = 0;
    std::size_t pos = 0;
    while (true) {
        std::string line;
        unsigned char a1 = 0;
        while (true) {
            if (pos >= mes.size() || mes[pos] == 0) { a1 = 0; break; }
            a1 = static_cast<unsigned char>(mes[pos++]);
            bool sj = (a1 >= 0x81 && a1 < 0xa0) || a1 >= 0xe0;
            if (a1 == 13) {
                if (pos < mes.size() && mes[pos] == 10) ++pos;
                break;
            }
            line.push_back(char(a1));
            if (sj && pos < mes.size()) line.push_back(mes[pos++]);
            if (line.size() >= 4095) break;
        }
        print_line(line);
        if (px < printsize.cx) px = printsize.cx;
        if (a1 == 0) break;
    }
    return px;
}

void Screen::print_line(std::string_view line) {
    if (!text) throw Unsupported("mes/print without a font backend (build without SDL3_ttf)");
    if (!line.empty()) {
        printsize = text->draw_display_line(font, dib, cx, cy, line, color);
    } else {
        printsize = text->measure_line(font, " ");
        printsize.cx = 0;
    }
    posinc(printsize.cy);
}

void Screen::set_hsv_color(int hval, int sval, int vval) {
    // literal port of Bmscr::SetHSVColor
    int v = vval & 255, s = sval & 255, h = hval % 192;
    int i = h / 32, t = h % 32, mv = 8160, mp = 4080;
    int v1 = (v * (mv - s * 32) + mp) / mv;
    int v2 = (v * (mv - s * t) + mp) / mv;
    int v3 = (v * (mv - s * (32 - t)) + mp) / mv;
    int r = 0, g = 0, b = 0;
    switch (i) {
    case 0: case 6: r = v; g = v3; b = v1; break;
    case 1: r = v2; g = v; b = v1; break;
    case 2: r = v1; g = v; b = v3; break;
    case 3: r = v1; g = v2; b = v; break;
    case 4: r = v3; g = v1; b = v; break;
    case 5: r = v; g = v1; b = v2; break;
    default: throw Unsupported("hsvcolor with negative hue");  // original: uninitialised locals
    }
    color = rgb(r, g, b);
}

void Screen::set_cel_divide(int nsx, int nsy, int ofsx, int ofsy) {
    divsx = nsx > 0 ? nsx : width();
    divsy = nsy > 0 ? nsy : height();
    divx = width() / divsx;
    divy = height() / divsy;
    celofsx = ofsx;
    celofsy = ofsy;
}

int Screen::cel_put(const Screen& src, int id) {
    int srcsx = src.divsx, srcsy = src.divsy;
    if (src.divx <= 0) throw Unsupported("celput with zero cel columns");
    int x = (id % src.divx) * srcsx, y = (id / src.divx) * srcsy;
    int bak_cx = cx + srcsx, bak_cy = cy;
    cx -= src.celofsx;
    cy -= src.celofsy;
    int res = copy(src, x, y, srcsx, srcsy);
    cx = bak_cx;
    cy = bak_cy;
    return res;
}

void Screen::gradfill(int x, int y, int sx, int sy, int mode, colorref c1, colorref c2) {
    // GDI approximation of GradientFill(GRADIENT_FILL_RECT_H/V). Colours are 0xRRGGBB.
    for (int j = 0; j < sy; ++j) {
        for (int i = 0; i < sx; ++i) {
            int tx = x + i, ty = y + j;
            if (!dib.contains(tx, ty)) continue;
            int num = mode ? j : i, den = mode ? sy : sx;
            auto ch = [&](int shift) {
                int a = (c1 >> shift) & 255, b = (c2 >> shift) & 255;
                return a + (b - a) * num / std::max(den, 1);
            };
            dib.set(tx, ty, rgb(ch(16), ch(8), ch(0)));
        }
    }
    mark_dirty();
}

void Screen::width_request(int x, int y, int wposx, int wposy, int mode) {
    if (x > 0) wx = std::min(x, width());
    if (y > 0) wy = std::min(y, height());
    if (type < kTypeMain) return;
    if (mode == 0) {
        if (wposx >= 0) window_x = wposx;
        if (wposy >= 0) window_y = wposy;
    } else {
        window_x = wposx;
        window_y = wposy;
    }
    mark_dirty();
}

void Screen::set_scroll(int xbase, int ybase) {
    viewx = std::max(xbase, 0);
    viewy = std::max(ybase, 0);
    if (viewx + wx > width()) viewx = width() - wx;
    if (viewy + wy > height()) viewy = height() - wy;
    mark_dirty();
}

int Screen::render_rgba(int t_psx, int t_psy, int components, const std::uint8_t* src) {
    // literal port of Bmscr::RenderAlphaBitmap
    int x = cx, y = cy, x2 = 0, y2 = 0, psx = t_psx, psy = t_psy;
    if (x > width()) return -1;
    if (y > height()) return -1;
    if (x < 0) { x2 = -x; x = 0; psx -= x2; }
    if (y < 0) { y2 = -y; y = 0; psy -= y2; }
    if (x + psx > width()) psx = width() - x;
    if (y + psy > height()) psy = height() - y;
    for (int b = 0; b < psy; ++b) {
        std::uint8_t* p = dib.row(y + b) + x * 3;
        const std::uint8_t* p2 = src + (std::size_t(y2 + b) * t_psx + x2) * 4;
        for (int a = 0; a < psx; ++a) {
            std::uint8_t a1 = p2[0], a2 = p2[1], a3 = p2[2];
            int a4 = p2[3];
            p2 += 4;
            if (components < 4) { p[0] = a3; p[1] = a2; p[2] = a1; p += 3; continue; }
            a4 += a4 >> 7;
            int a4r = 256 - a4;
            if (a4r == 0) { p[0] = a3; p[1] = a2; p[2] = a1; }
            else {
                p[0] = std::uint8_t(((p[0] * a4r) >> 8) + ((a3 * a4) >> 8));
                p[1] = std::uint8_t(((p[1] * a4r) >> 8) + ((a2 * a4) >> 8));
                p[2] = std::uint8_t(((p[2] * a4r) >> 8) + ((a1 * a4) >> 8));
            }
            p += 3;
        }
    }
    mark_dirty();
    return 0;
}

Screen& ScreenTable::get_safe(int id) {
    Screen* s = get(id);
    if (!s) throw InvalidScreen("screen id " + std::to_string(id) + " is not initialised");
    return *s;
}

Screen& ScreenTable::make(int id, int type, int width, int height, int mode) {
    if (id < 0) throw InvalidScreen("negative screen id");
    if (mode & 1) throw Unsupported("palette-mode (8-bit) screen/buffer");
    if (id >= int(screens_.size())) screens_.resize(id + 1);
    // HspWnd::MakeBmscr: a window id that was a buffer stays a buffer.
    int final_type = type;
    if (type != kTypeBuffer && screens_[id] && screens_[id]->type == kTypeBuffer) final_type = kTypeBuffer;
    auto previous = std::move(screens_[id]);
    screens_[id] = std::make_unique<Screen>(id, final_type, width, height, text_);
    if (previous && final_type != kTypeBuffer) {
        screens_[id]->window_x = previous->window_x;
        screens_[id]->window_y = previous->window_y;
        screens_[id]->title_cp932 = previous->title_cp932;
    }
    return *screens_[id];
}

int ScreenTable::empty_id() const {
    for (int i = 0; i < int(screens_.size()); ++i)
        if (!screens_[i]) return i;
    return int(screens_.size());
}

}  // namespace elona::gfx

// See poly_raster.h. Literal port of fcpoly.cpp ScanLine, DrawPolygonF4 and
// DrawPolygonTex including their quirks (half-open scan ranges, the `||` range
// test, destination pointer only advancing for in-range texels).
#include "poly_raster.h"

#include <algorithm>
#include <cmath>

namespace elona::gfx {

void PolyRaster::set_color_key(colorref color) {
    ck1_ = std::uint8_t(color >> 16);
    ck2_ = std::uint8_t(color >> 8);
    ck3_ = std::uint8_t(color);
}

void PolyRaster::scan_line(const Poly4& poly, int w, int h, int& start_y, int& end_y) {
    start_y = h;
    end_y = -1;
    if (int(scan_.size()) < h) scan_.resize(std::size_t(h));
    for (int i = 0; i < h; ++i) {
        scan_[i].minX = w;
        scan_[i].maxX = -1;
    }
    for (int i = 0; i < 4; ++i) {
        const PolyVertex* fv1 = &poly.v[i];
        const PolyVertex* fv2 = (i + 1) < 4 ? fv1 + 1 : &poly.v[0];
        if (fv1->y == fv2->y) {
            int y = fv1->y;
            if (y < h && y >= 0) {
                start_y = std::min(y, start_y);
                end_y = std::max(y, end_y);
                if (fv1->x > fv2->x) std::swap(fv1, fv2);
                if (fv1->x < scan_[y].minX) { scan_[y].minX = fv1->x; scan_[y].minTx = fv1->tx; scan_[y].minTy = fv1->ty; }
                if (fv2->x > scan_[y].maxX) { scan_[y].maxX = fv2->x; scan_[y].maxTx = fv2->tx; scan_[y].maxTy = fv2->ty; }
            }
        } else {
            if (fv1->y > fv2->y) std::swap(fv1, fv2);
            int v1y = fv1->y, v2y = fv2->y;
            if (v1y < h || v2y >= 0) {  // sic: original uses ||
                start_y = std::min(v1y, start_y);
                end_y = std::max(v2y, end_y);
                double dd = 1.0f / double(v2y - v1y);
                double dx = double(fv2->x - fv1->x) * dd;
                double tdx = double(fv2->tx - fv1->tx) * dd;
                double tdy = double(fv2->ty - fv1->ty) * dd;
                double x = fv1->x, tx = fv1->tx, ty = fv1->ty;
                int endY = std::min(h - 1, v2y);
                for (int y = v1y; y <= endY; ++y) {
                    if (y >= 0) {
                        if (x < scan_[y].minX) { scan_[y].minX = int(x); scan_[y].minTx = int(tx); scan_[y].minTy = int(ty); }
                        if (x > scan_[y].maxX) { scan_[y].maxX = int(x); scan_[y].maxTx = int(tx); scan_[y].maxTy = int(ty); }
                    }
                    x += dx;
                    tx += tdx;
                    ty += tdy;
                }
            }
        }
    }
    start_y = std::max(0, start_y);
    end_y = std::min(h, end_y);
}

PolyResult PolyRaster::draw_f4(Dib& dest, const Poly4& poly) {
    PolyResult r;
    int w = dest.width(), h = dest.height(), start_y, end_y;
    scan_line(poly, w, h, start_y, end_y);
    if (start_y >= h || end_y < 0) return r;
    r.res_y0 = start_y;
    r.res_y1 = end_y;
    std::uint8_t c0 = std::uint8_t(poly.color), c1 = std::uint8_t(poly.color >> 8), c2 = std::uint8_t(poly.color >> 16);
    int op = -1;
    short alpha = 0, cc1 = 0, cc2 = 0, cc3 = 0;
    if (poly.alpha != 0x100) {
        op = poly.alpha >> 8;
        alpha = short((poly.alpha & 255) + 1);
        cc1 = short((c0 * alpha) >> 8);
        cc2 = short((c1 * alpha) >> 8);
        cc3 = short((c2 * alpha) >> 8);
        if (op != 0 && op != 2 && op != 3) op = -1;
    }
    short ialpha = short(0x100 - alpha);
    for (int y = start_y; y < end_y; ++y) {
        int minX = scan_[y].minX, maxX = scan_[y].maxX;
        if (minX < 0) minX = 0;
        if (maxX < 0) continue;
        maxX = std::min(w, maxX);
        std::uint8_t* up = dest.row(y) + minX * 3;
        for (int x = minX; x < maxX; ++x, up += 3) {
            switch (op) {
            case -1: up[0] = c0; up[1] = c1; up[2] = c2; break;
            case 0:
                up[0] = std::uint8_t(((up[0] * ialpha) >> 8) + cc1);
                up[1] = std::uint8_t(((up[1] * ialpha) >> 8) + cc2);
                up[2] = std::uint8_t(((up[2] * ialpha) >> 8) + cc3);
                break;
            case 2:
                up[0] = std::uint8_t(std::min(255, cc1 + up[0]));
                up[1] = std::uint8_t(std::min(255, cc2 + up[1]));
                up[2] = std::uint8_t(std::min(255, cc3 + up[2]));
                break;
            case 3:
                up[0] = std::uint8_t(std::max(0, up[0] - cc1));
                up[1] = std::uint8_t(std::max(0, up[1] - cc2));
                up[2] = std::uint8_t(std::max(0, up[2] - cc3));
                break;
            }
        }
    }
    return r;
}

PolyResult PolyRaster::draw_tex(Dib& dest, const Dib& tex, const Poly4& poly) {
    PolyResult r;
    int w = dest.width(), h = dest.height(), start_y, end_y;
    const int tex_wbyte = tex.stride();
    const std::uint32_t tex_size = std::uint32_t(tex.height()) * std::uint32_t(tex_wbyte);
    scan_line(poly, w, h, start_y, end_y);
    if (start_y >= h || end_y < 0) return r;
    r.res_y0 = start_y;
    r.res_y1 = end_y;
    enum { kCopy, kTrans, kBlend, kTBlend, kModulate, kSubtract } mode = kCopy;
    short alpha = 0;
    if (poly.alpha != 0x100) {
        int op = poly.alpha >> 8;
        alpha = short((poly.alpha & 255) + 1);
        if (op == 0) mode = (poly.attr & kPolyAttrColorKey) ? kTBlend : kBlend;
        else if (op == 2) mode = kModulate;
        else if (op == 3) mode = kSubtract;
        else mode = (poly.attr & kPolyAttrColorKey) ? kTrans : kCopy;
    } else if (poly.attr & kPolyAttrColorKey) {
        mode = kTrans;
    }
    short ialpha = short(0x100 - alpha);
    const std::uint8_t* tbits = tex.data();
    for (int y = start_y; y < end_y; ++y) {
        int minX = scan_[y].minX, maxX = scan_[y].maxX;
        if (maxX < 0) continue;
        std::uint32_t tx = std::uint32_t(scan_[y].minTx) << 16, ty = std::uint32_t(scan_[y].minTy) << 16, tdx = 0, tdy = 0;
        if (maxX != minX) {
            int i2 = 0x10000 / (maxX - minX);
            tdx = std::uint32_t((scan_[y].maxTx - scan_[y].minTx) * i2);
            tdy = std::uint32_t((scan_[y].maxTy - scan_[y].minTy) * i2);
        }
        while (minX < 0) { ++minX; tx += tdx; ty += tdy; }
        maxX = std::min(w, maxX);
        std::uint8_t* p = dest.row(y) + minX * 3;
        std::uint8_t* const row_end = dest.row(y) + w * 3;
        for (int x = minX; x < maxX; ++x) {
            std::int32_t i2 = std::int32_t((ty >> 16) * std::uint32_t(tex_wbyte) + (tx >> 16) * 3u);
            if (i2 < std::int32_t(tex_size)) {
                if (i2 < 0) {
                    // The 32-bit original reads memory before the texture here.
                    ++r.skipped_out_of_range;
                    if (mode == kTrans || mode == kTBlend || mode == kBlend || mode == kModulate || mode == kSubtract || mode == kCopy) p += 3;
                } else if (p + 3 <= row_end) {
                    const std::uint8_t* s = tbits + i2;
                    switch (mode) {
                    case kCopy: p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; break;
                    case kTrans: if (s[0] | s[1] | s[2]) { p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; } break;
                    case kBlend:
                        for (int k = 0; k < 3; ++k) p[k] = std::uint8_t((s[k] * alpha + p[k] * ialpha) >> 8);
                        break;
                    case kTBlend:
                        if (!(s[0] == ck1_ && s[1] == ck2_ && s[2] == ck3_))
                            for (int k = 0; k < 3; ++k) p[k] = std::uint8_t((s[k] * alpha + p[k] * ialpha) >> 8);
                        break;
                    case kModulate:
                        for (int k = 0; k < 3; ++k) p[k] = std::uint8_t(std::min(255, ((s[k] * alpha) >> 8) + p[k]));
                        break;
                    case kSubtract:
                        for (int k = 0; k < 3; ++k) p[k] = std::uint8_t(std::max(0, p[k] - ((s[k] * alpha) >> 8)));
                        break;
                    }
                    p += 3;
                }
            }
            tx += tdx;
            ty += tdy;
        }
    }
    return r;
}

Poly4 make_sprite_poly(int attr, int alpha, int x, int y, int sx, int sy, double ang, int tx0, int ty0, int tx1, int ty1,
                       int opx, int opy) {
    // literal port of DrawSpriteEx vertex math
    Poly4 poly;
    double mx0 = -std::sin(ang), my0 = std::cos(ang), mx1 = -my0, my1 = mx0;
    double ofsx = -opx, ofsy = -opy;
    double x0 = mx0 * ofsy, y0 = my0 * ofsy, x1 = mx1 * ofsx, y1 = my1 * ofsx;
    double xx = double(x) - (-x0 + x1), yy = double(y) - (-y0 + y1);
    ofsx = -sx;
    ofsy = -sy;
    x0 = mx0 * ofsy; y0 = my0 * ofsy; x1 = mx1 * ofsx; y1 = my1 * ofsx;
    poly.v[0] = {short((-x0 + x1) + xx), short((-y0 + y1) + yy), short(tx1), short(ty0)};
    poly.v[1] = {short(x1 + xx), short(y1 + yy), short(tx1), short(ty1)};
    poly.v[2] = {short(xx), short(yy), short(tx0), short(ty1)};
    poly.v[3] = {short(-x0 + xx), short(-y0 + yy), short(tx0), short(ty0)};
    poly.attr = attr;
    poly.color = 0xffffff;
    poly.alpha = alpha;
    return poly;
}

Poly4 make_rect_poly(int color, int alpha, int x, int y, int sx, int sy, double ang) {
    // literal port of DrawRectEx (short arithmetic on the offsets)
    Poly4 poly;
    short xx = short(x), yy = short(y);
    short tx0 = 0, ty0 = 0, tx1 = short(sx - 1), ty1 = short(sy - 1);
    if (tx1 < 0) tx1 = 0;
    if (ty1 < 0) ty1 = 0;
    double ofsx = tx1 - tx0 + 1, ofsy = ty1 - ty0 + 1;
    double x0 = -std::sin(ang), y0 = std::cos(ang), x1 = -y0, y1 = x0;
    ofsx *= -0.5f;
    ofsy *= -0.5f;
    x0 *= ofsy; y0 *= ofsy; x1 *= ofsx; y1 *= ofsx;
    poly.v[0] = {short(short(-x0 + x1) + xx), short(short(-y0 + y1) + yy), tx1, ty1};
    poly.v[1] = {short(short(x0 + x1) + xx), short(short(y0 + y1) + yy), tx1, ty0};
    poly.v[2] = {short(short(x0 - x1) + xx), short(short(y0 - y1) + yy), tx0, ty0};
    poly.v[3] = {short(short(-x0 - x1) + xx), short(short(-y0 - y1) + yy), tx0, ty1};
    poly.attr = 0;
    poly.color = color;
    poly.alpha = alpha;
    return poly;
}

}  // namespace elona::gfx

// hspext_ext.dll full-colour screen effects used by ElonaPlus 2.335
// (gfini / gfdec / gfdec2 / gfinc), ported from the shipped DLL
// (SHA-256 6ee3f3ec...e7dc, Ghidra 12.2 static analysis of a private copy:
// gfini @0x10002e70, address helper @0x10002e20, gfdec2 @0x10003060,
// gfdec @0x10003150, gfinc @0x10003200).
//
// Observed contract:
//   gfini bmscr, w, h : w/h 0 -> BMSCR.sx/sy. Row step is sx*3 bytes (the DLL
//     does NOT use the 4-byte padded DIB stride). Start address =
//     pBit + (sy - cy - 1) * sx*3 + cx*3, rows advance towards lower memory
//     (downwards on screen). Returns EAX 0.
//   gfdec r,g,b  : each B,G,R byte -= low byte of b,g,r, clamped at 0.
//   gfinc r,g,b  : each byte += value, clamped at 255.
//   gfdec2 r,g,b : only non-zero bytes are reduced, clamped at 1 (keeps the
//     gmode 2 transparent black untouched and never creates new black).
// The original keeps a raw pointer from gfini; this port stores the screen id
// and byte offset and refuses accesses outside that screen's pixel memory.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "hsp3code.h"
#include "native_dll.h"

namespace elona::app {
namespace {
struct GfState {
    bool valid = false;
    int surface = -1;
    int w = 0, h = 0;
    long long row_step = 0;  // bytes, sx*3
    long long start = 0;     // byte offset into the surface memory
} g_gf;

template <typename Op>
std::int32_t gf_apply(DllCall& c, Op op) {
    if (c.args.size() < 3) throw DllRefusal("unexpected argument layout");
    if (!g_gf.valid) throw DllRefusal("gf effect before gfini (original would use an uninitialised pointer)");
    std::uint8_t* bits;
    int sw, sh, stride;
    if (!c.host->surface(g_gf.surface, bits, sw, sh, stride)) throw DllRefusal("gfini target screen no longer exists");
    const long long size = (long long)stride * sh;
    std::uint8_t pr = std::uint8_t(c.args[0].i), pg = std::uint8_t(c.args[1].i), pb = std::uint8_t(c.args[2].i);
    long long row = g_gf.start;
    for (int y = 0; y < g_gf.h; ++y, row -= g_gf.row_step) {
        if (g_gf.w <= 0) continue;
        if (row < 0 || row + (long long)g_gf.w * 3 > size)
            throw DllRefusal("gf effect region leaves the screen buffer (original writes outside the DIB)");
        std::uint8_t* p = bits + row;
        for (int x = 0; x < g_gf.w; ++x, p += 3) {
            p[0] = op(p[0], pb);
            p[1] = op(p[1], pg);
            p[2] = op(p[2], pr);
        }
    }
    c.host->mark_surface_dirty(g_gf.surface);
    return 0;
}
}  // namespace

void register_gfx_bindings() {
    register_dll_handler("hspext_ext.dll", "_gfini@16", [](DllCall& c) -> std::int32_t {
        if (c.args.size() < 3 || c.args[0].kind != DllArgKind::Surface) throw DllRefusal("unexpected argument layout");
        std::uint8_t* bits;
        int sw, sh, stride;
        int id = c.args[0].surface_id;
        if (!c.host->surface(id, bits, sw, sh, stride)) throw DllRefusal("gfini on a missing screen");
        int cx = 0, cy = 0;
        if (!c.host->surface_cursor(id, cx, cy)) throw DllRefusal("gfini cursor unavailable");
        g_gf.surface = id;
        g_gf.w = c.args[1].i ? c.args[1].i : sw;
        g_gf.h = c.args[2].i ? c.args[2].i : sh;
        g_gf.row_step = (long long)sw * 3;
        g_gf.start = ((long long)sh - cy - 1) * g_gf.row_step + (long long)cx * 3;
        g_gf.valid = true;
        return 0;
    });
    register_dll_handler("hspext_ext.dll", "_gfdec@16", [](DllCall& c) {
        return gf_apply(c, [](std::uint8_t v, std::uint8_t d) { return std::uint8_t(v > d ? v - d : 0); });
    });
    register_dll_handler("hspext_ext.dll", "_gfinc@16", [](DllCall& c) {
        return gf_apply(c, [](std::uint8_t v, std::uint8_t d) { return std::uint8_t(v + d > 255 ? 255 : v + d); });
    });
    register_dll_handler("hspext_ext.dll", "_gfdec2@16", [](DllCall& c) {
        return gf_apply(c, [](std::uint8_t v, std::uint8_t d) {
            if (v == 0) return v;
            int r = int(v) - int(d);
            return std::uint8_t(r < 1 ? 1 : r);
        });
    });
}

}  // namespace elona::app

// ---------------------------------------------------------------------------
// water.hpi (SHA-256 93ebb568...963e) title-screen ripple effect, ported from
// Ghidra 12.2 analysis of a private copy (getimage @0x1048, draw @0x115c,
// refresh @0x1410, setlight @0x1454 [ordinal 4, not called by 2.335],
// setripple @0x1470, calc @0x1598). Light position starts at (0,0).
// Height fields are two int32 planes of width*height; g_disp selects the
// current plane. draw() reproduces the original destination walk, whose row
// step assumes width*3 == stride; it refuses if that walk would leave the
// destination buffer.
#include <cmath>
#include <vector>

namespace elona::app {
namespace {
struct WaterState {
    bool has_img = false;
    int w = 0, h = 0;
    std::vector<std::uint8_t> img;  // copy of the source DIB (padded stride)
    std::vector<std::int32_t> plane[2];
    int disp = 0;
    int lx = 0, ly = 0;
} g_water;

const DllArg& surface_arg(DllCall& c) {
    if (c.args.empty() || c.args[0].kind != DllArgKind::Surface) throw DllRefusal("unexpected argument layout");
    return c.args[0];
}
}  // namespace

void register_water_bindings() {
    register_dll_handler("water.hpi", "_water_getimage@16", [](DllCall& c) -> std::int32_t {
        std::uint8_t* bits;
        int sw, sh, stride;
        if (!c.host->surface(surface_arg(c).surface_id, bits, sw, sh, stride) || sw <= 0 || sh <= 0) return -1;
        g_water = WaterState{};
        g_water.w = sw;
        g_water.h = sh;
        g_water.img.assign(bits, bits + std::size_t(stride) * sh);
        g_water.plane[0].assign(std::size_t(sw) * sh, 0);
        g_water.plane[1].assign(std::size_t(sw) * sh, 0);
        g_water.has_img = true;
        return 0;
    });
    register_dll_handler("water.hpi", "_water_refresh@16", [](DllCall&) -> std::int32_t {
        if (!g_water.has_img) return -1;
        for (auto& p : g_water.plane) std::fill(p.begin(), p.end(), 0);
        return 0;
    });
    register_dll_handler("water.hpi", "_water_setripple@16", [](DllCall& c) -> std::int32_t {
        if (c.args.size() < 4) throw DllRefusal("unexpected argument layout");
        int px = c.args[0].i, py = c.args[1].i, power = c.args[2].i, r = c.args[3].i;
        auto& W = g_water;
        if (!W.has_img || px < 0 || px > W.w - 1 || py < 0 || py > W.h - 1) return -1;
        if (r < 0) r = -r;
        auto& hp = W.plane[W.disp];
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                int d2 = dy * dy + dx * dx;
                if (d2 >= r * r) continue;
                int x = px + dx, y = py + dy;
                if (x > 0 && x < W.w - 1 && y > 0 && y < W.h - 1) {
                    // x87: fild/fidiv/fimul then fistp (round to nearest even)
                    long double v = (long double)power * ((long double)d2 / (long double)r);
                    hp[std::size_t(x) + std::size_t(y) * W.w] += std::int32_t(std::nearbyint(v));
                }
            }
        }
        return 0;
    });
    register_dll_handler("water.hpi", "_water_calc@16", [](DllCall&) -> std::int32_t {
        auto& W = g_water;
        if (!W.has_img) return -1;
        auto& next = W.plane[W.disp ^ 1];
        const auto& cur = W.plane[W.disp];
        int w = W.w;
        for (int y = 1; y < W.h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                std::size_t i = std::size_t(y) * w + x;
                std::int32_t v = ((cur[i - w] + cur[i - 1] + cur[i + 1] + cur[i + w]) >> 1) - next[i];
                next[i] = v - (v >> 5);
            }
        }
        W.disp ^= 1;
        return 0;
    });
    register_dll_handler("water.hpi", "_water_draw@16", [](DllCall& c) -> std::int32_t {
        auto& W = g_water;
        std::uint8_t* dst;
        int sw, sh, stride;
        if (!W.has_img || !c.host->surface(surface_arg(c).surface_id, dst, sw, sh, stride) || sw != W.w || sh != W.h) return -1;
        const auto& hp = W.plane[W.disp];
        const int w = W.w, h = W.h;
        const long long ustride = stride;  // (w*3+3)&~3, same as the original uVar5
        const long long dsize = ustride * h;
        long long out = (long long)(h - 2) * ustride + 3;
        std::size_t idx = std::size_t(w) + 1;
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x, ++idx) {
                int dx = (hp[idx - 1] - hp[idx + 1]) >> 3;
                int dy = (hp[idx - w] - hp[idx + w]) >> 3;
                int shade = ((W.ly - y) * dy + (W.lx - x) * dx) >> 6;
                int sx = std::clamp(dx + x, 0, w - 1), sy = std::clamp(dy + y, 0, h - 1);
                const std::uint8_t* s = W.img.data() + (long long)(h - sy - 1) * ustride + sx * 3;
                if (out < 0 || out + 3 > dsize) throw DllRefusal("water_draw destination walk leaves the screen buffer");
                for (int k = 0; k < 3; ++k) {
                    int v = s[k] + shade;
                    dst[out + k] = std::uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                out += 3;
            }
            out -= ustride * 2 - 6;
            idx += 2;
        }
        c.host->mark_surface_dirty(surface_arg(c).surface_id);
        return 0;
    });
}

}  // namespace elona::app

// ---------------------------------------------------------------------------
// elona.dll (SHA-256 7c0f9153...3dae) `_grotate@16` (#func func_3 ... $202).
// Despite the name this is a rotated SHADOW blit (Ghidra 12.2 on a private
// copy: entry @0x10001eb0, vertex setup @0x10001a50, scanline @0x10002110,
// span fill @0x10002720):
//   func_3 src_id, sx, sy, angle[, w=gx][, h=gy]   (HSP2 exinfo parameters)
//   * angle unit: 4096 = 2*pi (float multiply by 0.0015339808f)
//   * source rect: tx0=clamp(sx), ty0=clamp(sy), tx1=tx0+gx-1, ty1=ty0+gy-1
//     (gx/gy of the DESTINATION screen), clamped to the source size
//   * quad centred on the destination pos (cx,cy), half sizes w/2,h/2
//   * every destination pixel whose texel is not black (0,0,0) is darkened by
//     0x50 per channel (clamped at 0); texels are never copied; gmode/alpha
//     are ignored by the span routine.
//   * spans end at width-1 and the last scanline at height-1 (differs from
//     the HSP3 runtime fcpoly).
//   * return: 0 ok, 3 src id outside 0..31, 0x15 palette mismatch.
namespace elona::app {
namespace {
struct ShadowVertex { short x, y, tx, ty; };
struct ShadowScan { int minX, maxX, minTx, minTy, maxTx, maxTy; };

void shadow_scanline(const ShadowVertex v[4], int w, int h, std::vector<ShadowScan>& scan, int& sy, int& ey) {
    scan.assign(std::size_t(std::max(h, 0)), ShadowScan{w, -1, 0, 0, 0, 0});
    sy = h;
    ey = -1;
    for (int i = 0; i < 4; ++i) {
        const ShadowVertex* a = &v[i];
        const ShadowVertex* b = &v[(i + 1) & 3];
        if (a->y == b->y) {
            int y = a->y;
            if (y < h && y >= 0) {
                sy = std::min(sy, y);
                ey = std::max(ey, y);
                if (b->x < a->x) std::swap(a, b);
                if (a->x < scan[y].minX) { scan[y].minX = a->x; scan[y].minTx = a->tx; scan[y].minTy = a->ty; }
                if (scan[y].maxX < b->x) { scan[y].maxX = b->x; scan[y].maxTx = b->tx; scan[y].maxTy = b->ty; }
            }
        } else {
            if (b->y < a->y) std::swap(a, b);
            int y0 = a->y, y1 = b->y;
            if (y0 < h || y1 >= 0) {  // sic
                sy = std::min(sy, y0);
                ey = std::max(ey, y1);
                long double inv = 1.0L / (long double)(y1 - y0);
                long double x = a->x, tx = a->tx, ty = a->ty;
                long double dx = (long double)(b->x - a->x) * inv, dtx = (long double)(b->tx - a->tx) * inv,
                            dty = (long double)(b->ty - a->ty) * inv;
                int end = std::min(y1, h - 1);
                for (int y = y0; y <= end; ++y) {
                    if (y >= 0) {
                        if (x < scan[y].minX) { scan[y].minX = int(x); scan[y].minTx = int(tx); scan[y].minTy = int(ty); }
                        if (scan[y].maxX < x) { scan[y].maxX = int(x); scan[y].maxTx = int(tx); scan[y].maxTy = int(ty); }
                    }
                    x += dx; tx += dtx; ty += dty;
                }
            }
        }
    }
    sy = std::max(sy, 0);
    ey = std::min(ey, h - 1);
}
}  // namespace

void register_elona_bindings() {
    register_dll_handler("elona.dll", "_grotate@16", [](DllCall& c) -> std::int32_t {
        if (c.args.empty() || c.args[0].kind != DllArgKind::ExInfo) throw DllRefusal("unexpected argument layout");
        int dst_id = c.host->active_surface();
        int gx = 0, gy = 0, gmode = 0, cx = 0, cy = 0;
        if (!c.host->surface_state(dst_id, gx, gy, gmode) || !c.host->surface_cursor(dst_id, cx, cy))
            throw DllRefusal("active screen unavailable");
        int src_id = code_getdi(0), sx = code_getdi(0), sy = code_getdi(0), angle = code_getdi(0);
        int w = code_getdi(gx), h = code_getdi(gy);
        if (src_id < 0 || src_id > 31) return 3;
        std::uint8_t *dbits, *sbits;
        int dw, dh, dstride, sw, sh, sstride;
        if (!c.host->surface(dst_id, dbits, dw, dh, dstride)) throw DllRefusal("destination screen missing");
        if (!c.host->surface(src_id, sbits, sw, sh, sstride)) throw DllRefusal("source screen " + std::to_string(src_id) + " missing");
        float a = float(angle) * 0.0015339808f;
        auto clampv = [](int v, int hi) { return v > hi ? hi : (v < 0 ? 0 : v); };
        int tx0 = clampv(sx, sw), ty0 = clampv(sy, sh);
        int tx1 = clampv(gx - 1 + tx0, sw), ty1 = clampv(gy - 1 + ty0, sh);
        short tex_top = short(sh - ty0 - 1), tex_bottom = short(sh - ty1 - 1);
        // x87 evaluation order of @0x10001a50 (float stores where the original stores floats)
        long double s = std::sin((long double)a), co = std::cos((long double)a);
        float L1 = float(-co), A30s = float(-s), L0 = float(w * 0.5f);
        long double hy = (long double)h * 0.5f;
        float R = float(-s * hy);
        long double C = co * hy;
        long double P = (long double)L1 * L0;
        float Q = float((long double)A30s * L0);
        auto t = [](long double v) { return short(int(v)); };  // _ftol truncation to 16 bits
        short bx = t(R + P), bp = t((long double)Q + C);
        ShadowVertex v[4] = {
            {short(cx + t(P - R)), short(cy + t((long double)Q - C)), short(tx1), tex_bottom},
            {short(cx + bx), short(cy + bp), short(tx1), tex_top},
            {short(cx + t((long double)R - P)), short(cy + t(C - Q)), short(tx0), tex_top},
            {short(cx - bx), short(cy - bp), short(tx0), tex_bottom},
        };
        std::vector<ShadowScan> scan;
        int y0, y1;
        shadow_scanline(v, dw, dh, scan, y0, y1);
        if (y0 >= dh || y1 < 0) return 0;
        const std::int32_t tex_size = sstride * sh;
        for (int y = y0; y < y1; ++y) {
            const ShadowScan& sc = scan[y];
            int minX = sc.minX, maxX = sc.maxX;
            if (maxX < 0) continue;
            std::uint32_t tx = std::uint32_t(sc.minTx) << 16, ty = std::uint32_t(sc.minTy) << 16, tdx = 0, tdy = 0;
            if (maxX != minX) {
                int i5 = int(0x10000LL / (long long)(maxX - minX));
                tdx = std::uint32_t((sc.maxTx - sc.minTx) * i5);
                tdy = std::uint32_t((sc.maxTy - sc.minTy) * i5);
            }
            if (minX < 0) { tx += std::uint32_t(-minX) * tdx; ty += std::uint32_t(-minX) * tdy; minX = 0; }
            if (maxX > dw - 1) maxX = dw - 1;
            std::uint8_t* p = dbits + std::ptrdiff_t(dh - y - 1) * dstride + minX * 3;
            for (int x = minX; x < maxX; ++x) {
                std::int32_t idx = std::int32_t((ty >> 16) * std::uint32_t(sstride) + (tx >> 16) * 3u);
                if (idx < tex_size) {
                    if (idx < 0) throw DllRefusal("shadow blit texel before the source buffer (original reads stray memory)");
                    const std::uint8_t* q = sbits + idx;
                    if (!(q[0] == 0 && q[1] == 0 && q[2] == 0))
                        for (int k = 0; k < 3; ++k) p[k] = std::uint8_t(p[k] > 0x50 ? p[k] - 0x50 : 0);
                    p += 3;
                }
                tx += tdx;
                ty += tdy;
            }
        }
        c.host->mark_surface_dirty(dst_id);
        return 0;
    });
}

}  // namespace elona::app

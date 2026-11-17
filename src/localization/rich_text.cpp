#include "rich_text.h"
#include "catalog.h"
#include <algorithm>
#include <stdexcept>

namespace elona::localization {
int unicode_cell_width(std::uint32_t cp, int pixels) {
    const bool wide = (cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf) ||
                      (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
                      (cp >= 0xff01 && cp <= 0xff60) || cp == 0x2026 || cp == 0x203b ||
                      cp == 0x2605 || cp == 0x2606 || cp == 0x25cb || cp == 0x25ce ||
                      cp == 0x25cf || cp == 0x266a;
    return wide ? pixels : pixels / 2;
}
namespace {
constexpr std::uint32_t rgb(int r, int g, int b) { return r | (g << 8) | (b << 16); }
std::uint32_t codepoint(std::string_view s) {
    auto c = static_cast<unsigned char>(s[0]);
    if (s.size() == 1) return c;
    std::uint32_t value = c & (s.size() == 2 ? 31 : s.size() == 3 ? 15 : 7);
    for (std::size_t i = 1; i < s.size(); ++i) value = (value << 6) | (static_cast<unsigned char>(s[i]) & 63);
    return value;
}
}
RichLayout layout_rich_text(std::string_view text, int width, std::uint32_t base) {
    if (!valid_utf8(text) || text.size() > 16384) throw std::runtime_error("invalid rich-text UTF-8");
    if (width < 14 || width > 16384) throw std::runtime_error("invalid rich-text width");
    RichLayout out;
    int size = 14, style = 0, y = 0;
    std::uint32_t color = base;
    std::vector<RichGlyph> line;
    auto flush = [&](std::size_t count, bool advance) {
        int x = 0, height = size;
        for (std::size_t k = 0; k < count; ++k) {
            auto g = line[k];
            g.x = x; g.y = y; x += g.advance;
            height = std::max(height, g.size);
            out.glyphs.push_back(std::move(g));
        }
        line.erase(line.begin(), line.begin() + count);
        if (advance) y += height + 2;
    };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '<') {
            auto end = text.find('>', i + 1);
            if (end == std::string_view::npos) throw std::runtime_error("unfinished rich-text tag");
            auto tag = text.substr(i + 1, end - i - 1);
            i = end + 1;
            if (tag == "emp1") { style = 4; color = rgb(50, 50, 255); }
            else if (tag == "emp2") { style = 1; color = rgb(40, 130, 40); }
            else if (tag == "title1") { size = 12; style = 1; color = rgb(100, 50, 50); }
            else if (tag == "def") { size = 14; style = 0; color = base; }
            else if (tag == "b") style = 1;
            else if (tag == "green") color = rgb(20, 120, 20);
            else if (tag == "red") color = rgb(120, 20, 20);
            else if (tag == "col") color = base;
            else if (tag == "br" || tag == "p") { flush(line.size(), false); y += tag == "p" ? 24 : 16; }
            else throw std::runtime_error("unknown rich-text tag: " + std::string(tag));
            continue;
        }
        if (static_cast<unsigned char>(text[i]) < 32 || text[i] == '$' || text[i] == '^')
            throw std::runtime_error("unsupported rich-text control");
        const unsigned char c = text[i];
        const std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        const auto bytes = text.substr(i, n); i += n;
        RichGlyph g{std::string(bytes), 0, 0, size, style, unicode_cell_width(codepoint(bytes), size), color};
        line.push_back(std::move(g));
        int used = 0;
        for (const auto& part : line) used += part.advance;
        if (used <= width) continue;
        std::size_t split = line.size() - 1;
        for (std::size_t k = line.size() - 1; k > 0; --k) {
            if (line[k].utf8 == " ") { split = k; break; }
        }
        // Never start a line with a space left over from the wrap.
        flush(split, true);
        while (!line.empty() && line.front().utf8 == " ") line.erase(line.begin());
    }
    flush(line.size(), false);
    out.height = y + size + 4;
    out.final_size = size; out.final_style = style; out.final_color = color;
    return out;
}
}  // namespace elona::localization

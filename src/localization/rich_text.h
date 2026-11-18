#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace elona::localization {
// Korean display layout only. No HSP variables or original text are modified.
struct RichGlyph {
    std::string utf8;
    int x = 0, y = 0, size = 14, style = 0, advance = 0;
    std::uint32_t color = 0;
};
struct RichLayout {
    std::vector<RichGlyph> glyphs;
    int height = 0, final_size = 14, final_style = 0;
    std::uint32_t final_color = 0;
};
int unicode_cell_width(std::uint32_t cp, int pixels);
// Known gmes tags only. Unknown controls fail explicitly before any drawing.
RichLayout layout_rich_text(std::string_view utf8, int width, std::uint32_t color, bool parse_tags = true);
}  // namespace elona::localization

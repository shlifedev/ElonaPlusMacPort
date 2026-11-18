#pragma once
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "hsp_screen.h"

namespace elona::gfx {

struct TtfTextOptions {
    // Font files tried in order. Empty -> platform defaults (Hiragino on macOS,
    // MS Gothic on Windows). Face index is used for .ttc collections.
    std::vector<std::string> font_files;
    int face_index = 0;
    // Grayscale anti-aliasing (Windows ClearType/DEFAULT_QUALITY approximation).
    bool antialias = true;
    // Lay out CP932 text in fixed cells: single-byte = height/2, double-byte =
    // height. This reproduces the advance metrics of MS Gothic, the usual
    // ElonaPlus configuration font, independently of the host glyph source.
    bool cp932_cell_metrics = true;
    // Optional display-only UTF-8 catalog; VM strings stay CP932. Empty = original.
    std::filesystem::path translation_catalog;
    std::filesystem::path translation_fallback;
    std::filesystem::path translation_misses;
};

// SDL3_ttf backed renderer. Does not require SDL video, so it also works in
// headless builds that link SDL3_ttf.
std::unique_ptr<TextRenderer> create_ttf_text_renderer(const TtfTextOptions& options, std::string& error);

}  // namespace elona::gfx

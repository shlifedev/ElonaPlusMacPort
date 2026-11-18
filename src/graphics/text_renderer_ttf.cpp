#include "text_renderer_ttf.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <filesystem>
#include <map>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <sstream>

#include "ui_text_codec.h"
#include "localization/catalog.h"
#include "localization/rich_text.h"

namespace elona::gfx {
namespace {

struct Glyph {
    int w = 0, h = 0;
    std::vector<std::uint8_t> alpha;
};

std::vector<std::string> default_fonts() {
#if defined(__APPLE__)
    return {"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/Library/Fonts/Arial Unicode.ttf"};
#elif defined(_WIN32)
    return {"C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/YuGothR.ttc"};
#else
    return {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"};
#endif
}

class TtfRenderer final : public TextRenderer {
public:
    TtfRenderer(TtfTextOptions options, std::string file, std::optional<localization::Catalog> catalog, std::string ko_font)
        : options_(std::move(options)), file_(std::move(file)), ko_font_(std::move(ko_font)), catalog_(std::move(catalog)) {}
    ~TtfRenderer() override {
        for (auto& [k, f] : fonts_) TTF_CloseFont(f);
        if (!options_.translation_misses.empty()) {
            std::ofstream out(options_.translation_misses, std::ios::binary);
            out << "# UTF-8 source<TAB>draw_count; display observations only\n";
            for (const auto& [s, count] : misses_) {
                for (char c : s) {
                    if (c == '\\') out << "\\\\";
                    else if (c == '\t') out << "\\t";
                    else if (c == '\r') out << "\\r";
                    else if (c == '\n') out << "\\n";
                    else out << c;
                }
                out << '\t' << count << '\n';
            }
        }
    }

    bool prepare(const FontSpec& spec) override { return font_for(spec) != nullptr; }
    const std::string* lookup_translation(std::string_view key) const override {
        return catalog_ ? catalog_->find(key) : nullptr;
    }

    void begin_english_log(std::string_view cp932, int used, int limit) override {
        log_words_.clear();
        std::string source;
        if (catalog_ && cp932.size() <= 512 && ui_cp932_to_utf8(cp932, source))
            log_words_ = catalog_->log_words(source, used, limit);
    }
    void set_log_word_draw(bool active) override { log_word_draw_ = active; }
    void set_hud_draw(bool active) override { hud_draw_ = active; }

    TextExtent draw_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) override {
        return layout(spec, &dib, x, y, cp932, color);
    }
    TextExtent draw_display_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) override {
        if (catalog_) {
            std::string source;
            if (ui_cp932_to_utf8(cp932, source)) {
                if (log_word_draw_ && !log_words_.empty()) {
                    if (source == log_words_.front().source) {
                        auto word = std::move(log_words_.front());
                        log_words_.pop_front();
                        if (!word.display.empty()) layout_utf8(spec, dib, x, y, word.display, color);
                        return measure_line(spec, cp932);
                    }
                    log_words_.clear();
                }
                if (hud_draw_) {
                    if (auto label = catalog_->hud_text(source)) {
                        layout_utf8(spec, dib, x, y, *label, color);
                        return measure_line(spec, cp932);
                    }
                }
                if (const auto* target = catalog_->find(source)) return layout_utf8(spec, dib, x, y, *target, color);
                if (!options_.translation_misses.empty() && source.size() <= 2048 && misses_.size() < 10000) ++misses_[source];
            }
        }
        return draw_line(spec, dib, x, y, cp932, color);
    }
    TextExtent measure_line(const FontSpec& spec, std::string_view cp932) override {
        return layout(spec, nullptr, 0, 0, cp932, 0);
    }
    int draw_localized_paragraph(Dib& dib, int x, int y, int width, bool shadow, colorref color,
                                 std::string_view cp932, FontSpec& final_font) override {
        if (!catalog_) return -1;
        std::string source;
        if (!ui_cp932_to_utf8(cp932, source)) return -1;
        const auto* target = catalog_->find(source);
        if (!target) return -1;
        // Validate the complete run before drawing any pixels.
        const auto paragraph = localization::layout_rich_text(*target, width, color);
        FontSpec spec{"", 14, 0, 0};
        for (const auto& g : paragraph.glyphs) {
            spec.pixel_height = g.size; spec.style = g.style;
            if (shadow) layout_utf8(spec, dib, x + g.x + 1, y + g.y + 1, g.utf8, rgb(180, 160, 140));
            layout_utf8(spec, dib, x + g.x, y + g.y, g.utf8, g.color);
        }
        final_font = {"", paragraph.final_size, paragraph.final_style, 0};
        return paragraph.height;
    }
    std::string describe(const FontSpec& spec) override {
        return file_ + " px=" + std::to_string(height_of(spec)) + (options_.cp932_cell_metrics ? " cp932-cells" : " proportional");
    }

private:
    static int height_of(const FontSpec& spec) {
        if (spec.stock) return 14;  // stock fonts: approximate as 14px system font
        return spec.pixel_height > 0 ? spec.pixel_height : (spec.pixel_height < 0 ? -spec.pixel_height : 12);
    }

    TTF_Font* font_for(const FontSpec& spec, bool korean = false) {
        int px = height_of(spec);
        int style = spec.stock ? 0 : spec.style;
        long key = long(px) * 64 + (style & 15) + (korean ? 0x1000000 : 0);
        auto it = fonts_.find(key);
        if (it != fonts_.end()) return it->second;
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING, (korean ? ko_font_ : file_).c_str());
        SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, float(px));
        SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, korean ? 0 : options_.face_index);
        TTF_Font* f = TTF_OpenFontWithProperties(props);
        SDL_DestroyProperties(props);
        if (!f) return nullptr;
        TTF_FontStyleFlags st = TTF_STYLE_NORMAL;
        if (style & 1) st |= TTF_STYLE_BOLD;
        if (style & 2) st |= TTF_STYLE_ITALIC;
        if (style & 4) st |= TTF_STYLE_UNDERLINE;
        if (style & 8) st |= TTF_STYLE_STRIKETHROUGH;
        TTF_SetFontStyle(f, st);
        TTF_SetFontHinting(f, TTF_HINTING_NORMAL);
        fonts_[key] = f;
        return f;
    }

    const Glyph& glyph(TTF_Font* f, long font_key, Uint32 cp) {
        auto key = std::make_pair(font_key, cp);
        auto it = glyphs_.find(key);
        if (it != glyphs_.end()) return it->second;
        Glyph g;
        SDL_Surface* s = TTF_RenderGlyph_Blended(f, cp, SDL_Color{255, 255, 255, 255});
        if (s) {
            SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
            SDL_DestroySurface(s);
            if (c) {
                g.w = c->w;
                g.h = c->h;
                g.alpha.resize(std::size_t(g.w) * g.h);
                for (int yy = 0; yy < g.h; ++yy) {
                    auto* row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(c->pixels) + yy * c->pitch);
                    for (int xx = 0; xx < g.w; ++xx) g.alpha[std::size_t(yy) * g.w + xx] = Uint8(row[xx] >> 24);
                }
                SDL_DestroySurface(c);
            }
        }
        return glyphs_.emplace(key, std::move(g)).first->second;
    }

    void blit(const Glyph& g, Dib& dib, int x, int y, colorref color) {
        int cr = red_of(color), cg = green_of(color), cb = blue_of(color);
        for (int yy = 0; yy < g.h; ++yy) {
            int ty = y + yy;
            if (ty < 0 || ty >= dib.height()) continue;
            for (int xx = 0; xx < g.w; ++xx) {
                int tx = x + xx;
                if (tx < 0 || tx >= dib.width()) continue;
                int a = g.alpha[std::size_t(yy) * g.w + xx];
                if (!options_.antialias) a = a >= 128 ? 255 : 0;
                if (a == 0) continue;
                if (a == 255) { dib.set(tx, ty, color); continue; }
                colorref d = dib.get(tx, ty);
                auto mix = [a](int dst, int src) { return (dst * (255 - a) + src * a + 127) / 255; };
                dib.set(tx, ty, rgb(mix(red_of(d), cr), mix(green_of(d), cg), mix(blue_of(d), cb)));
            }
        }
    }

    TextExtent layout(const FontSpec& spec, Dib* dib, int x, int y, std::string_view cp932, colorref color) {
        TTF_Font* f = font_for(spec);
        if (!f) throw Unsupported("font could not be opened: " + file_);
        int px = height_of(spec);
        long font_key = long(px) * 64 + ((spec.stock ? 0 : spec.style) & 15);
        TextExtent ext;
        ext.cy = options_.cp932_cell_metrics ? px : TTF_GetFontHeight(f);
        int pen = x;
        for (std::size_t i = 0; i < cp932.size();) {
            unsigned char c = static_cast<unsigned char>(cp932[i]);
            bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
            std::size_t n = (lead && i + 1 < cp932.size()) ? 2 : 1;
            std::string utf8;
            ui_cp932_to_utf8(cp932.substr(i, n), utf8);
            i += n;
            if (c == '\t') {  // TabbedTextOut default tab stops: 8 average chars
                int tab = std::max(1, px / 2) * 8;
                pen = x + ((pen - x) / tab + 1) * tab;
                continue;
            }
            Uint32 cp = decode_utf8(utf8);
            int cell = options_.cp932_cell_metrics ? (n == 2 ? px : px / 2) : -1;
            const Glyph& g = glyph(f, font_key, cp);
            int advance = cell >= 0 ? cell : g.w;
            if (dib && !g.alpha.empty()) {
                int gx = pen + (cell >= 0 ? (cell - g.w) / 2 : 0);
                int gy = y + (cell >= 0 ? (px - g.h) / 2 : 0);
                blit(g, *dib, gx, gy, color);
            }
            pen += advance;
        }
        ext.cx = pen - x;
        return ext;
    }

    TextExtent layout_utf8(const FontSpec& spec, Dib& dib, int x, int y, const std::string& utf8, colorref color) {
        TTF_Font* f = font_for(spec, true);
        if (!f) throw Unsupported("Korean font could not be opened: " + ko_font_);
        int px = height_of(spec), pen = x;
        long key = long(px) * 64 + ((spec.stock ? 0 : spec.style) & 15) + 0x1000000;
        for (std::size_t i = 0; i < utf8.size();) {
            unsigned char c = utf8[i];
            std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            Uint32 cp = decode_utf8(utf8.substr(i, n));
            i += n;
            if (cp == '\t') { int tab = std::max(1, px / 2) * 8; pen = x + ((pen - x) / tab + 1) * tab; continue; }
            TTF_Font* glyph_font = f;
            long glyph_key = key;
            if (!TTF_FontHasGlyph(glyph_font, cp)) {
                // Malgun covers Hangul but omits some original Japanese
                // punctuation (for example U+30FB). Preserve the character
                // using the already installed original-text font, with a
                // separate cache key; never replace it with '?' or blank.
                glyph_font = font_for(spec, false);
                glyph_key -= 0x1000000;
            }
            if (!glyph_font || !TTF_FontHasGlyph(glyph_font, cp)) {
                std::ostringstream message;
                message << "Korean and original text fonts missing glyph U+" << std::uppercase << std::hex << cp;
                throw Unsupported(message.str());
            }
            const Glyph& g = glyph(glyph_font, glyph_key, cp);
            int advance = options_.cp932_cell_metrics ? localization::unicode_cell_width(cp, px) : g.w;
            blit(g, dib, pen + (advance - g.w) / 2, y + (px - g.h) / 2, color);
            pen += advance;
        }
        return {pen - x, options_.cp932_cell_metrics ? px : TTF_GetFontHeight(f)};
    }

    static Uint32 decode_utf8(const std::string& s) {
        if (s.empty()) return 0xfffd;
        const auto* p = reinterpret_cast<const unsigned char*>(s.data());
        if (p[0] < 0x80) return p[0];
        if ((p[0] & 0xe0) == 0xc0 && s.size() >= 2) return ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        if ((p[0] & 0xf0) == 0xe0 && s.size() >= 3) return ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        if ((p[0] & 0xf8) == 0xf0 && s.size() >= 4)
            return ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        return 0xfffd;
    }

    TtfTextOptions options_;
    std::string file_;
    std::string ko_font_;
    bool log_word_draw_ = false, hud_draw_ = false;
    std::deque<localization::LogWord> log_words_;
    std::optional<localization::Catalog> catalog_;
    std::map<std::string, std::size_t> misses_;
    std::map<long, TTF_Font*> fonts_;
    std::map<std::pair<long, Uint32>, Glyph> glyphs_;
};

}  // namespace

std::unique_ptr<TextRenderer> create_ttf_text_renderer(const TtfTextOptions& options, std::string& error) {
    if (!TTF_WasInit() && !TTF_Init()) {
        error = std::string("TTF_Init failed: ") + SDL_GetError();
        return nullptr;
    }
    auto candidates = options.font_files.empty() ? default_fonts() : options.font_files;
    std::optional<localization::Catalog> catalog;
    std::string ko_font;
    if (!options.translation_catalog.empty()) {
        try { catalog = localization::Catalog::load(options.translation_catalog); }
        catch (const std::exception& e) { error = e.what(); return nullptr; }
        std::vector<std::string> korean_candidates = options.font_files;
#if defined(__APPLE__)
        korean_candidates.push_back("/System/Library/Fonts/AppleSDGothicNeo.ttc");
        korean_candidates.push_back("/System/Library/Fonts/Supplemental/AppleGothic.ttf");
#elif defined(_WIN32)
        korean_candidates.push_back("C:/Windows/Fonts/malgun.ttf");
        korean_candidates.push_back("C:/Windows/Fonts/gulim.ttc");
#endif
        for (const auto& path : korean_candidates) {
            TTF_Font* probe = TTF_OpenFont(path.c_str(), 12.0f);
            if (!probe) continue;
            bool hangul = TTF_FontHasGlyph(probe, 0xd55c) && TTF_FontHasGlyph(probe, 0xae00);
            TTF_CloseFont(probe);
            if (hangul) { ko_font = path; break; }
        }
        if (ko_font.empty()) { error = "Korean catalog requires a Hangul font (use --font FILE)"; return nullptr; }
    }
    for (const auto& file : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(file), ec)) continue;
        TTF_Font* probe = TTF_OpenFont(file.c_str(), 12.0f);
        if (!probe) continue;
        TTF_CloseFont(probe);
        return std::make_unique<TtfRenderer>(options, file, std::move(catalog), ko_font);
    }
    error = "no usable font file found (use --font FILE)";
    return nullptr;
}

}  // namespace elona::gfx

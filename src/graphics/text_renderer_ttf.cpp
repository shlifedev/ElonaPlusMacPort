#include "text_renderer_ttf.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <sstream>

#include "ui_text_codec.h"
#include "localization/catalog.h"
#include "localization/rich_text.h"
#include "localization/quest_board.h"
#include "localization/book_reader.h"
#include "localization/journal.h"
#include "localization/manual.h"
#include "localization/evolution.h"
#include "localization/recipe.h"
#include "localization/options.h"
#include "localization/alias.h"

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
            write_misses(options_.translation_misses, misses_);
            auto log_path = options_.translation_misses;
            log_path += ".log.tsv";
            write_misses(log_path, log_misses_);
        }
    }

    bool prepare(const FontSpec& spec) override { return font_for(spec) != nullptr; }
    const std::string* lookup_translation(std::string_view key) const override {
        return catalog_ ? catalog_->find(key) : nullptr;
    }

    void begin_log(std::string_view cp932, int used, int limit, int starter_pet_id,
                   std::string_view identity, bool japanese) override {
        log_words_.clear();
        japanese_log_.begin({}, {});
        std::string source, key;
        if (!catalog_ || cp932.size() > 4096 || !ui_cp932_to_utf8(cp932, source)) return;
        std::optional<std::string> target;
        if (identity.size() <= 2048 && ui_cp932_to_utf8(identity, key)) {
            if (key.rfind("@fixed-id/", 0) == 0) {
                if (const auto* caption = catalog_->find(key)) target = *caption;
            } else if (key.rfind("@evolution/", 0) == 0) {
                target = localization::evolution_message(*catalog_, key);
            } else if (key.rfind("@recipe/", 0) == 0) {
                target = localization::recipe_log(*catalog_, key, source);
            }
        }
        if (target && !source.empty() && source.front() == '[') {
            const auto end = source.find("] ");
            const auto colon = source.find(':');
            if (colon != source.npos && colon < end && end != source.npos &&
                source.substr(1, end - 1).find_first_not_of("0123456789:") == source.npos)
                target->insert(0, source.substr(0, end + 2));
        }
        if (japanese) {
            if (!target) target = catalog_->log_text(source, starter_pet_id);
            if (target) japanese_log_.begin(cp932, std::move(*target));
        } else {
            log_words_ = catalog_->log_words(source, used, limit, starter_pet_id, std::move(target));
            if (log_words_.empty() && !options_.translation_misses.empty() && log_misses_.size() < 10000)
                ++log_misses_[source];
        }
    }
    void set_log_word_draw(bool active) override { log_word_draw_ = active; }
    void set_hud_draw(bool active) override { hud_draw_ = active; }
    int selection_label_width(const FontSpec& spec) override {
        if ((menu_scope_ != 11 && menu_scope_ != 21 && menu_scope_ != 23) || menu_label_.display.empty()) return 0;
        return layout_utf8(spec, nullptr, 0, 0, menu_label_.display, 0).cx;
    }
    void set_menu_draw(int scope, std::string_view cp932) override {
        menu_label_ = {};
        menu_scope_ = scope;
        map_label_ = nullptr;
        if (scope == 19 && catalog_ && cp932.size() <= 256) {
            std::string name;
            if (ui_cp932_to_utf8(cp932, name)) {
                map_label_ = catalog_->find("@map-name/" + name);
                if (!map_label_) map_label_ = catalog_->find("@log-noun/{place}/" + name);
            }
        }
        option_key_.clear();
        if (scope == 28 && catalog_ && cp932.size() <= 2048) {
            std::string fields;
            if (ui_cp932_to_utf8(cp932, fields)) {
                if (auto caption = localization::recipe_caption(*catalog_, fields)) {
                    const auto last = cp932.rfind('\t');
                    menu_label_ = {std::string(cp932.substr(last + 1)), std::move(*caption)};
                }
            }
            return;
        }
        if (scope >= 24 && scope <= 26 && catalog_ && cp932.size() <= 1024) {
            std::string fields;
            if (ui_cp932_to_utf8(cp932, fields)) {
                if (auto caption = localization::town_caption(*catalog_, scope, fields)) {
                    const auto start = cp932.rfind('\t');
                    menu_label_ = {std::string(start == cp932.npos ? cp932 : cp932.substr(start+1)),
                                   std::move(*caption)};
                }
            }
            return;
        }
        if (scope == 22 && catalog_) {
            manual_pages_ = localization::manual_pages(*catalog_, cp932);
            return;
        }
        if (scope == 23 && catalog_) {
            std::string label;
            if (cp932.size() <= 512 && ui_cp932_to_utf8(cp932, label)) {
                if (const auto* target = catalog_->find("@manual-label/" + label))
                    menu_label_ = {std::string(cp932), *target};
            }
            return;
        }
        if (scope == 18 && catalog_ && !cp932.empty() && cp932.size() <= 8) {
            option_key_ = "@option/" + std::string(cp932) + "/";
            return;
        }
        if (scope == 10) return;
        if (scope == 20 && catalog_) {
            // Some English rows in the mode descriptions consist of just a
            // space or a period. Identify the authored line by its Japanese text.
            const auto tab = cp932.find('\t');
            std::string japanese;
            if (tab != cp932.npos && cp932.size() <= 2048 && ui_cp932_to_utf8(cp932.substr(0, tab), japanese)) {
                if (const auto* label = catalog_->find(japanese, true))
                    menu_label_ = {std::string(cp932.substr(tab + 1)), *label};
            }
            return;
        }
        if (scope == 13) {
            journal_.clear();
            std::string notes;
            if (catalog_ && cp932.size() <= 1024 * 1024 && ui_cp932_to_utf8(cp932, notes))
                journal_ = localization::journal_rows(*catalog_, notes);
            return;
        }
        if (scope == 15) {
            const auto slash = cp932.find('/');
            if (slash == cp932.npos) return;
            std::size_t row = 0;
            auto parsed = std::from_chars(cp932.data(), cp932.data() + slash, row);
            if (parsed.ec == std::errc{} && parsed.ptr == cp932.data() + slash &&
                row < journal_.size() && journal_[row])
                menu_label_ = {std::string(cp932.substr(slash + 1)), *journal_[row]};
            return;
        }
        if (scope == 17) {
            std::string feat;
            if (catalog_ && cp932.size() <= 512 && ui_cp932_to_utf8(cp932, feat)) {
                if (const auto* label = catalog_->find("@feat/" + feat))
                    menu_label_ = {std::string(cp932), *label};
            }
            return;
        }
        if (scope == 14) {
            std::string header;
            if (catalog_ && cp932.size() <= 512 && ui_cp932_to_utf8(cp932, header)) {
                if (auto display = localization::save_header(*catalog_, header))
                    menu_label_ = {std::string(cp932), std::move(*display)};
            }
            return;
        }
        if (scope >= 31 && scope <= 35 && catalog_) {
            std::string source;
            if (cp932.size() <= 64 && ui_cp932_to_utf8(cp932, source)) {
                if (auto* reply = catalog_->find("@reply/" + std::to_string(scope) + "/" + source))
                    menu_label_ = {std::string(cp932), *reply};
            }
            return;
        }
        if (scope == 12 && catalog_) {
            constexpr std::string_view prefix = "Gene from ";
            std::string name;
            if (cp932.substr(0, prefix.size()) == prefix &&
                ui_cp932_to_utf8(cp932.substr(prefix.size()), name))
                menu_label_ = {std::string(cp932), name + "에게서 물려받은 유전자"};
            return;
        }
        if (scope == 21 && catalog_) {
            std::string source;
            if (ui_cp932_to_utf8(cp932, source)) {
                if (const auto* label = catalog_->find(source))
                    menu_label_ = {std::string(cp932), *label};
            }
            return;
        }
        if (scope == 11 && catalog_) {
            std::string source;
            if (cp932.size() <= 160 && ui_cp932_to_utf8(cp932, source)) {
                if (auto alias = localization::localized_alias(*catalog_, source)) {
                    menu_label_ = {std::string(cp932), std::move(*alias)};
                    return;
                }
            }
            return;
        }
        static constexpr std::string_view prefixes[] = {
            "", "@equip-slot/", "@inv-action/", "@inv-column/", "@game-menu/",
            "@status-label/", "@sprite-menu/", "@portrait-menu/", "@sheet/", "@profile/"
        };
        if (!catalog_ || scope < 1 || scope >= static_cast<int>(std::size(prefixes)) ||
            cp932.empty() || cp932.size() > 512) return;
        std::string source;
        if (ui_cp932_to_utf8(cp932, source)) {
            if (const auto* target = catalog_->find(std::string(prefixes[scope]) + source))
                menu_label_ = {std::string(cp932), *target};
            else if (scope == 8) {
                if (auto metric = catalog_->sheet_text(source))
                    menu_label_ = {std::string(cp932), std::move(*metric)};
            }
            if (menu_label_.source.empty()) {
                if (const auto* fixed = catalog_->find("@fixed/" + source))
                    menu_label_ = {std::string(cp932), *fixed};
            }
        }
    }
    bool draw_input_caption(Dib& dib, int x, int y) override {
        const auto* label = lookup_translation("@sprite/input");
        if (!label || !dib.contains(x, y) || !dib.contains(x + 100, y + 31)) return false;
        // The 2.335 input ribbon has its English lettering baked into the paper.
        // Match its ink silhouette before retouching it, so custom art stays intact.
        std::uint32_t ink = 2166136261u;
        for (int row = 19; row <= 30; ++row) {
            for (int col = 36; col <= 78; ++col) {
                const auto pixel = dib.get(x + col, y + row);
                ink = (ink ^ (std::max({red_of(pixel), green_of(pixel), blue_of(pixel)}) < 100)) * 16777619u;
            }
        }
        if (ink != 0x88ab2d34u) return false;
        for (int row = 19; row <= 30; ++row) {
            for (int col = 36; col <= 78; ++col)
                dib.set(x + col, y + row, dib.get(x + 81 + (col - 36) % 18, y + row));
        }
        layout_utf8(FontSpec{"", 12, 1, 0}, &dib, x + 45, y + 18, *label, rgb(32, 24, 12));
        return true;
    }
    bool draw_book(Dib& dib, int id, std::string_view cp932, int spread, int x, int y) override {
        if (!catalog_) return false;
        auto lines = localization::book_spread(*catalog_, id, cp932, spread);
        if (!lines) return false;
        for (const auto& line : *lines) {
            FontSpec spec{"", line.size, line.bold ? 1 : 0, 0};
            layout_utf8(spec, &dib, x + 2 + 306 * line.column, y + 16 * line.row, line.text, rgb(0, 0, 0));
        }
        return true;
    }
    void prepare_board_row(const std::array<std::string, 7>& raw, int item) override {
        board_body_.clear();
        menu_label_ = {};
        if (!catalog_) return;
        std::array<std::string, 7> args;
        for (std::size_t i = 0; i < args.size(); ++i)
            if (raw[i].size() > 4096 || !ui_cp932_to_utf8(raw[i], args[i])) return;
        if (auto post = localization::board_post(*catalog_, args, item)) {
            menu_label_ = {raw[1].substr(0, raw[1].find(':')), std::move(post->title)};
            board_body_ = std::move(post->body);
        }
    }
    void prepare_item_row(std::string_view row, std::string_view base, std::string_view classifier, int id) override {
        item_row_ = {};
        std::string source, name, kind;
        if (catalog_ && row.size() <= 1024 && ui_cp932_to_utf8(row, source) &&
            ui_cp932_to_utf8(base, name) && ui_cp932_to_utf8(classifier, kind)) {
            if (auto translated = catalog_->item_label(source, name, kind, id))
                item_row_ = {std::string(row), std::move(*translated)};
        }
    }

    TextExtent draw_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) override {
        return layout(spec, &dib, x, y, cp932, color);
    }
    TextExtent draw_display_line(const FontSpec& spec, Dib& dib, int x, int y, std::string_view cp932, colorref color) override {
        if (map_label_) {
            layout_utf8(spec, &dib, x, y, *map_label_, color);
            return measure_line(spec, cp932);
        }
        if (!menu_label_.source.empty() && cp932 == menu_label_.source) {
            if (!menu_label_.display.empty()) layout_utf8(spec, &dib, x, y, menu_label_.display, color);
            return measure_line(spec, cp932);
        }
        if (menu_scope_ == 10 || menu_scope_ == 11 || menu_scope_ == 25 || menu_scope_ == 26) {
            // Player text and unknown saved aliases can coincide with a UI key.
            // Count a missing alias where its original text is actually drawn.
            if (menu_scope_ == 11 && !options_.translation_misses.empty() && misses_.size() < 10000) {
                std::string source;
                if (ui_cp932_to_utf8(cp932, source)) ++misses_[source];
            }
            return draw_line(spec, dib, x, y, cp932, color);
        }
        if (!item_row_.source.empty()) {
            auto item = std::move(item_row_);
            item_row_ = {};
            if (cp932 == item.source) {
                layout_utf8(spec, &dib, x, y, item.display, color);
                return measure_line(spec, cp932);
            }
        }
        if (catalog_) {
            std::string source;
            if (ui_cp932_to_utf8(cp932, source)) {
                if (menu_scope_ == 27) {
                    if (const auto* label = catalog_->find("@card-ui/" + source))
                        return layout_utf8(spec, &dib, x, y, *label, color);
                    // These draws contain only a fixed TCG field and one integer or ratio.
                    const auto space = source.rfind(' ');
                    if (space != source.npos && space + 1 < source.size()) {
                        const auto value = source.substr(space + 1);
                        if (value.find_first_not_of("0123456789/") == value.npos) {
                            if (const auto* label = catalog_->find("@card-counter/" + source.substr(0, space)))
                                return layout_utf8(spec, &dib, x, y, *label + " " + value, color);
                        }
                    }
                }
                if (!option_key_.empty()) {
                    if (auto label = localization::option_text(*catalog_, option_key_, source))
                        return layout_utf8(spec, &dib, x, y, *label, color);
                }
                if (log_word_draw_ && !log_words_.empty()) {
                    if (source == log_words_.front().source) {
                        auto word = std::move(log_words_.front());
                        log_words_.pop_front();
                        if (!word.display.empty()) layout_utf8(spec, &dib, x, y, word.display, color);
                        return measure_line(spec, cp932);
                    }
                    log_words_.clear();
                }
                if (log_word_draw_) {
                    if (auto chunk = japanese_log_.consume(cp932)) {
                        if (!chunk->empty()) layout_utf8(spec, &dib, x, y, *chunk, color);
                        return measure_line(spec, cp932);
                    }
                }
                // Japanese msg_write draws complete CP932 chunks without the English word queue.
                if (log_word_draw_ && std::any_of(cp932.begin(), cp932.end(), [](unsigned char c) { return c >= 0x80; })) {
                    if (auto text = catalog_->log_text(source)) {
                        layout_utf8(spec, &dib, x, y, *text, color);
                        return measure_line(spec, cp932);
                    }
                }
                if (hud_draw_) {
                    if (auto label = catalog_->hud_text(source)) {
                        layout_utf8(spec, &dib, x, y, *label, color);
                        return measure_line(spec, cp932);
                    }
                }
                if (const auto* target = catalog_->find(source)) return layout_utf8(spec, &dib, x, y, *target, color);
                // Scene 0 starts its first note one byte into the CRLF after {txt}.
                // Only reviewed prologue lines have this source-qualified alias.
                if (!source.empty() && source.front() == '\n') {
                    if (const auto* target = catalog_->find("@prologue:" + source.substr(1)))
                        return layout_utf8(spec, &dib, x, y, *target, color);
                }
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
        std::string formatted;
        constexpr std::string_view prefix = "@home-name/";
        const bool named = source.compare(0, prefix.size(), prefix) == 0;
        const auto separator = named ? source.find('/', prefix.size()) : source.npos;
        if (named && separator == source.npos) return -1;
        const bool board = source == "@board/body";
        const bool manual = source.rfind("@manual/page/", 0) == 0;
        const std::string* target = nullptr;
        if (manual) {
            const auto page = manual_pages_.find(source);
            if (page == manual_pages_.end()) return -1;
            target = page->second;
        } else if (board) {
            if (!board_body_.empty()) target = &board_body_;
        } else {
            target = catalog_->find(source.substr(0, separator), true);
        }
        if (!target) return -1;
        if (named) {
            auto name = source.substr(separator + 1);
            if (name.find_first_of("\r\n\t") != name.npos) return -1;
            const bool alias = target->find("{alias}") != target->npos;
            const std::string_view placeholder = alias ? "{alias}" : "{name}";
            const auto marker = target->find(placeholder);
            if (marker == target->npos) return -1;
            if (alias) {
                if (auto translated = localization::localized_alias(*catalog_, name))
                    name = std::move(*translated);
            }
            formatted = *target;
            formatted.replace(marker, placeholder.size(), name);
        }
        // A name containing a tag, a dollar sign, or a caret is literal text.
        const auto paragraph = localization::layout_rich_text(named ? formatted : *target, width, color, !named && !board);
        if ((board && paragraph.height > 98) || (manual && paragraph.height > 390)) return -1;
        FontSpec spec{"", 14, 0, 0};
        for (const auto& g : paragraph.glyphs) {
            spec.pixel_height = g.size; spec.style = g.style;
            if (shadow) layout_utf8(spec, &dib, x + g.x + 1, y + g.y + 1, g.utf8, rgb(180, 160, 140));
            layout_utf8(spec, &dib, x + g.x, y + g.y, g.utf8, g.color);
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

    TextExtent layout_utf8(const FontSpec& spec, Dib* dib, int x, int y, const std::string& utf8, colorref color) {
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
            if (dib) blit(g, *dib, pen + (advance - g.w) / 2, y + (px - g.h) / 2, color);
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

    static void write_misses(const std::filesystem::path& path, const std::map<std::string, std::size_t>& values) {
        std::ofstream out(path, std::ios::binary);
        out << "# UTF-8 source<TAB>count; display observations only\n";
        for (const auto& [s, count] : values) {
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
    TtfTextOptions options_;
    std::string file_;
    std::string ko_font_;
    bool log_word_draw_ = false, hud_draw_ = false;
    std::deque<localization::LogWord> log_words_;
    localization::JapaneseLog japanese_log_;
    std::vector<std::optional<std::string>> journal_;
    std::unordered_map<std::string, const std::string*> manual_pages_;
    const std::string* map_label_ = nullptr;
    std::string option_key_;
    int menu_scope_ = 0;
    std::string board_body_;
    localization::LogWord item_row_, menu_label_;
    std::optional<localization::Catalog> catalog_;
    std::map<std::string, std::size_t> misses_, log_misses_;
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
        try { catalog = localization::Catalog::load(options.translation_catalog, options.translation_fallback); }
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

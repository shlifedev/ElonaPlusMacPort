#pragma once
#include <filesystem>
#include <optional>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace elona::localization {
struct LogWord { std::string source, display; };
// The Japanese VM supplies the actual CP932 chunks after wrapping. Consume
// those chunks in order so translation never changes its counters or log.
class JapaneseLog {
public:
    void begin(std::string_view original, std::string target);
    std::optional<std::string> consume(std::string_view chunk);
private:
    std::string original_, target_;
    std::vector<std::size_t> glyphs_;
    std::size_t consumed_ = 0, displayed_ = 0;
};
// Display-only UTF-8 lookup. No pointers to VM variables or saved data.
class Catalog {
public:
    static Catalog load(const std::filesystem::path& path, const std::filesystem::path& fallback = {});
    const std::string* find(std::string_view source_utf8, bool prose_context = false) const;
    std::optional<std::string> sheet_text(std::string_view source) const;
    std::optional<std::string> hud_text(std::string_view source) const;
    std::optional<std::string> item_label(std::string_view row, std::string_view base,
                                          std::string_view classifier, int id) const;
    std::optional<std::string> log_text(std::string_view source, int starter_pet_id = 0) const;
    std::optional<std::string> news_text(std::string_view source) const;
    std::deque<LogWord> log_words(std::string source, int used, int limit, int starter_pet_id = 0,
                                  std::optional<std::string> target = {}) const;
    std::size_t size() const { return entries_.size(); }
private:
    struct LogTemplate {
        std::string prefix, suffix, argument, translation;
    };
    void add_log_template(std::string_view pattern, const std::string& translation);
    std::optional<std::string> adventure_log(std::string_view source, int starter_pet_id) const;
    std::vector<LogTemplate> log_templates_;
    std::unordered_map<std::string, std::string> entries_;
    std::unordered_map<std::string, std::string> draft_by_hash_;
};
bool valid_utf8(std::string_view text);
}  // namespace elona::localization

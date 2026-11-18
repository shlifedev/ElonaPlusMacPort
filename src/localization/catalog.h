#pragma once
#include <filesystem>
#include <optional>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace elona::localization {
struct LogWord { std::string source, display; };
// Display-only UTF-8 lookup. No pointers to VM variables or saved data.
class Catalog {
public:
    static Catalog load(const std::filesystem::path& path);
    const std::string* find(std::string_view source_utf8) const;
    std::optional<std::string> hud_text(std::string_view source) const;
    std::optional<std::string> log_text(std::string_view source) const;
    std::deque<LogWord> log_words(std::string source, int used, int limit) const;
    std::size_t size() const { return entries_.size(); }
private:
    std::unordered_map<std::string, std::string> entries_;
};
bool valid_utf8(std::string_view text);
}  // namespace elona::localization

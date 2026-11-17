#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace elona::localization {
// Display-only UTF-8 lookup. No pointers to VM variables or saved data.
class Catalog {
public:
    static Catalog load(const std::filesystem::path& path);
    const std::string* find(std::string_view source_utf8) const;
    std::size_t size() const { return entries_.size(); }
private:
    std::unordered_map<std::string, std::string> entries_;
};
bool valid_utf8(std::string_view text);
}  // namespace elona::localization

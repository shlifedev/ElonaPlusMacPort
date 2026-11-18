#include "manual.h"
#include "platform/game_files.h"

namespace elona::localization {
std::unordered_map<std::string, const std::string*> manual_pages(const Catalog& catalog, std::string_view cp932) {
    std::unordered_map<std::string, const std::string*> pages;
    if (cp932.size() > 1024 * 1024) return pages;
    std::string section;
    int first_line = 0;
    auto finish = [&] {
        if (section.empty()) return;
        const auto hash = platform::sha256_hex(section.data(), section.size());
        if (const auto* body = catalog.find("@manual/body/" + hash))
            pages.emplace("@manual/page/" + std::to_string(first_line), body);
    };
    for (int line_number = 1; !cp932.empty(); ++line_number) {
        const auto end = cp932.find('\n');
        auto line = cp932.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.substr(0, 3) == "{} ") {
            finish();
            section.clear();
            first_line = line_number;
        }
        if (first_line) {
            section.append(line);
            section += '\n';
        }
        if (end == cp932.npos) break;
        cp932.remove_prefix(end + 1);
    }
    finish();
    return pages;
}
}

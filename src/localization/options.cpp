#include "options.h"
#include <regex>

namespace elona::localization {
std::optional<std::string> option_text(const Catalog& catalog, std::string_view key, std::string_view source) {
    if (const auto* fixed = catalog.find(std::string(key).append(source))) return *fixed;
    // Only presentation values assembled by the option editor can contain these units.
    static const std::regex steps(R"(^(?:After ([0-9]+) steps|([0-9]+)歩目から)$)");
    static const std::regex delay(R"(^([0-9]+) wait$)");
    static const std::regex button(R"(^(?:Button|ボタン)([0-9]+)$)");
    std::match_results<std::string_view::const_iterator> match;
    if (key == "@option/1/4/" && std::regex_match(source.begin(), source.end(), match, steps))
        return (match[1].matched ? match[1] : match[2]).str() + "걸음부터";
    if (key.substr(0, 10) == "@option/5/" && std::regex_match(source.begin(), source.end(), match, button))
        return "버튼 " + match[1].str();
    if (std::regex_match(source.begin(), source.end(), match, delay))
        return "대기 " + match[1].str();
    return {};
}
}

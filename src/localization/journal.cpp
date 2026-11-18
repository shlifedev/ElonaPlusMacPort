#include "journal.h"
#include "rich_text.h"
#include <regex>

namespace elona::localization {
namespace {
std::optional<std::string> translate(const Catalog& catalog, std::string_view line, bool news) {
    if (const auto* label = catalog.find("@journal/" + std::string(line))) return *label;
    struct Form { std::regex pattern; const char* key; };
    static const Form forms[] = {
        {std::regex(R"(^<(?:Progress |進行度：)([0-9]+)>$)"), "progress"},
        {std::regex(R"(^<(?:Now |現在：)([0-9]+) / (?:RewardGet |目標ポイント：)([0-9]+)>$)"), "goal"},
        {std::regex(R"(^ClearDeep: ([0-9]+)level$)"), "depth"},
        {std::regex(R"(^\[Allies up to level ([0-9]+) will follow in battle\]$)"), "allies"},
        {std::regex(R"(^Prepaid Balance ([0-9]+) GP$)"), "prepaid"},
        {std::regex(R"(^You have ([0-9]+) unpaid bills\.$)"), "bills"},
        {std::regex(R"(^  (?:Sum    : About |Sum    :  )([0-9]+) GP$)"), "sum"},
        {std::regex(R"(^  (?:Labor  : About |Labor  :  )([0-9]+) GP$)"), "labor"},
        {std::regex(R"(^  (?:Maint\. : About |Maint\. :  )([0-9]+) GP$)"), "maintenance"},
        {std::regex(R"(^  (?:Tax    : About |Tax    :  )([0-9]+) GP$)"), "tax"},
        {std::regex(R"(^  Days   :  ([0-9]+) Days$)"), "days"},
        {std::regex(R"(^  Kills  :  ([0-9]+)$)"), "kills"},
    };
    std::match_results<std::string_view::const_iterator> fields;
    if (line.find_first_of("0123456789") != line.npos) {
        for (const auto& form : forms) {
            if (!std::regex_match(line.begin(), line.end(), fields, form.pattern)) continue;
            const auto* format = catalog.find(std::string("@journal/") + form.key);
            if (!format) return {};
            std::string result = *format;
            for (std::size_t i = 1; i < fields.size(); ++i) {
                const auto marker = "{" + std::to_string(i) + "}";
                const auto at = result.find(marker);
                if (at == result.npos) return {};
                result.replace(at, marker.size(), fields[i].str());
            }
            return result;
        }
    }
    if (news) {
        static const std::regex topic(R"(^ ([0-9]+/[0-9]+/[0-9]+) h([0-9]+) (.+)$)");
        if (std::regex_match(line.begin(), line.end(), fields, topic)) {
            if (const auto* label = catalog.find("@journal/" + fields[3].str()))
                return fields[1].str() + " " + fields[2].str() + "시 · " + *label;
        }
        return catalog.news_text(line);
    }
    // Authored quest objectives reach the journal after talk_conv splits the original.
    if (const auto* text = catalog.find(line, true)) return *text;
    return {};
}
}

std::vector<std::optional<std::string>> journal_rows(const Catalog& catalog, std::string_view notes) {
    std::vector<std::string_view> lines;
    while (!notes.empty()) {
        const auto next = notes.find('\n');
        auto line = notes.substr(0, next);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (next == notes.npos) break;
        notes.remove_prefix(next + 1);
    }
    std::vector<std::optional<std::string>> result(lines.size());
    bool news = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto first = lines[i];
        if (first.empty()) continue;
        if (first.substr(0, 3) == " - ") news = first == " - News - ";
        if (first.front() == '@' && first.size() >= 3) first.remove_prefix(3);
        std::string joined(first);
        for (std::size_t j = i; j < lines.size() && j < i + 24 && joined.size() <= 2048; ++j) {
            if (j > i) {
                if (lines[j].empty() || lines[j].front() == '@' || lines[j].substr(0, 3) == " - ") break;
                joined += lines[j];  // Original English wrap keeps the word's trailing space.
            }
            auto text = translate(catalog, joined, news);
            if (!text || text->find_first_of("\t\r\n") != text->npos) continue;
            // The journal's content area is 306px per column. Retain original
            // page numbers, colours and row indices even across a page turn.
            const auto layout = layout_rich_text(*text, 294, 0, false);
            if (layout.glyphs.empty()) break;
            const auto count = 1 + static_cast<std::size_t>(layout.glyphs.back().y / 16);
            if (count > j - i + 1) break;
            for (std::size_t k = i; k <= j; ++k) result[k].emplace();
            for (const auto& glyph : layout.glyphs) *result[i + glyph.y / 16] += glyph.utf8;
            i = j;
            break;
        }
    }
    return result;
}

std::optional<std::string> save_header(const Catalog& catalog, std::string_view header) {
    // Names and map names may contain spaces. Only parse the game's level marker.
    static const std::regex regular(R"(^([^\r\n]+) Lv:([0-9]+) ([^\r\n]+)$)");
    static const std::regex gene(R"(^([^\r\n]+)\(Lv([0-9]+)\)の遺伝子$)");
    std::match_results<std::string_view::const_iterator> match;
    const auto marker = header.find(" Lv:");
    if (marker != header.npos && header.find(" Lv:", marker + 4) != header.npos) return {};
    if (std::regex_match(header.begin(), header.end(), match, regular)) {
        const auto place = match[3].str();
        auto* known = catalog.find("@map-name/" + place);
        if (!known) known = catalog.find("@log-noun/{place}/" + place);
        return match[1].str() + " · 레벨 " + match[2].str() + " · " + (known ? *known : place);
    }
    if (std::regex_match(header.begin(), header.end(), match, gene))
        return match[1].str() + "의 유전자 · 레벨 " + match[2].str();
    return {};
}
}

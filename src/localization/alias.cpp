#include "alias.h"
#include "catalog.h"
#include "platform/game_files.h"

namespace elona::localization {
namespace {
struct Forms {
    std::string name;
    std::string modifier;
};

std::optional<Forms> word(const Catalog& catalog, std::string_view part, bool noun_only = false) {
    if (part.empty()) return {};
    // Keys identify one complete cell of ndata(-e).csv. Targets optionally give
    // a separate attributive form after '|', e.g. "진홍빛|진홍빛으로 물든".
    const auto hash = platform::sha256_hex(part.data(), part.size());
    const std::string* row = nullptr;
    if (static_cast<unsigned char>(part.front()) < 128)
        row = catalog.find("@alias-noun/" + hash);
    if (!row && !noun_only) row = catalog.find("@alias-word/" + hash);
    if (!row) return {};
    auto separator = row->find('|');
    if (separator == row->npos) return Forms{*row, *row + "의"};
    auto modifier = row->substr(separator + 1);
    return Forms{separator == 0 ? modifier + " 자" : row->substr(0, separator), modifier};
}

bool starts(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

std::optional<std::string> title(const Catalog& catalog, std::string_view source) {
    const bool english = !source.empty() && static_cast<unsigned char>(source.front()) < 128;
    if (english) {
        if (starts(source, "the ")) {
            if (auto single = word(catalog, source.substr(4), true)) return single->name;
        }
        for (auto split = source.find(' '); split != source.npos; split = source.find(' ', split + 1)) {
            auto tail = source.substr(split + 1);
            const bool reversed = starts(tail, "of ");
            auto second = word(catalog, reversed ? tail.substr(3) : tail, true);
            if (!second) continue;
            if (auto first = word(catalog, source.substr(0, split), reversed)) {
                return reversed ? second->modifier + " " + first->name : first->modifier + " " + second->name;
            }
        }
        return {};
    }
    constexpr std::string_view article = "ザ・";
    if (starts(source, article)) {
        auto remaining = source.substr(article.size());
        if (!remaining.empty() && static_cast<unsigned char>(remaining.front()) >= 128) {
            if (auto single = word(catalog, remaining)) return single->name;
        }
    }

    // Match the longest complete first cell. Both cells may contain spaces or
    // kana particles. Neither an unknown suffix nor a partial name is accepted.
    for (std::size_t split = source.size(); split-- > 1;) {
        if ((static_cast<unsigned char>(source[split]) & 0xc0) == 0x80) continue;
        auto tail = source.substr(split);
        auto first = word(catalog, source.substr(0, split));
        if (!first) continue;
        for (std::string_view join : {"・オブ・", "のの", "の", "・", ""}) {
            if (!starts(tail, join)) continue;
            auto remaining = tail.substr(join.size());
            if (remaining.empty() || static_cast<unsigned char>(remaining.front()) < 128) continue;
            auto second = word(catalog, remaining);
            if (!second) continue;
            if (join == "・オブ・") return second->modifier + " " + first->name;
            return first->modifier + " " + second->name;
        }
    }
    return {};
}
} // namespace

std::optional<std::string> localized_alias(const Catalog& catalog, std::string_view source) {
    if (source.empty() || source.size() > 160) return {};
    if (source == "*Debug*") return "시험용 이명";
    std::string lower(source);
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (auto result = title(catalog, lower)) return result;

    // random_title(2) adds a party designator to an otherwise complete alias.
    struct Affix { std::string_view source, korean; };
    static constexpr Affix prefixes[] = {{"the army of ", "군단"}, {"the party of ", "일행"},
        {"the house of ", "가문"}, {"clan ", "일족"}};
    for (const auto& affix : prefixes) {
        if (starts(lower, affix.source)) {
            if (auto base = title(catalog, std::string_view(lower).substr(affix.source.size())))
                return *base + " " + std::string(affix.korean);
        }
    }
    static constexpr Affix suffixes[] = {{" clan", "일족"}, {" party", "일행"}, {" band", "무리"},
        {" gangs", "패거리"}, {" gathering", "모임"}, {" house", "가문"}, {" army", "군단"},
        {"の集い", "모임"}, {"の軍", "군단"}, {"の一家", "일가"}, {"の隊", "부대"}, {"の団", "단"},
        {"団", "단"}, {"チーム", "팀"}, {"パーティー", "일행"}, {"アーミー", "군단"},
        {"隊", "부대"}, {"軍", "군단"}};
    for (const auto& affix : suffixes) {
        if (lower.size() <= affix.source.size()) continue;
        auto end = lower.size() - affix.source.size();
        if (lower.compare(end, affix.source.size(), affix.source) == 0) {
            if (auto base = title(catalog, std::string_view(lower).substr(0, end)))
                return *base + " " + std::string(affix.korean);
        }
    }
    return {};
}
}

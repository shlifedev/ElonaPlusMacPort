#include "catalog.h"
#include <regex>

namespace elona::localization {
namespace {
bool take_prefix(std::string_view& text, std::string_view prefix) {
    if (text.substr(0, prefix.size()) != prefix) return false;
    text.remove_prefix(prefix.size());
    return true;
}

bool take_suffix(std::string_view& text, std::string_view suffix) {
    if (suffix.size() > text.size() || text.substr(text.size() - suffix.size()) != suffix) return false;
    text.remove_suffix(suffix.size());
    return true;
}

std::optional<std::string> gather_label(const Catalog& catalog, std::string_view row,
                                        std::string_view base, std::string_view index, bool description) {
    if (base.empty() || index.empty() || index.size() > 3 || index.find_first_not_of("0123456789") != index.npos)
        return {};
    const auto key = "@gather/" + std::string(index) + (description ? "/desc/" : "/name/") + std::string(base);
    const auto* translated = catalog.find(key);
    if (!translated) return {};
    if (description) return row == base ? std::optional<std::string>(*translated) : std::nullopt;
    if (!take_prefix(row, base) || (!take_prefix(row, " x ") && !take_prefix(row, " × "))) return {};
    if (row.empty() || row.find_first_not_of("0123456789") != row.npos) return {};
    return *translated + " ×" + std::string(row);
}

std::optional<std::string> detail_line(const Catalog& catalog, std::string_view row) {
    if (row.empty() || row.size() > 512) return {};
    std::string rating;
    const auto bracket = row.rfind(" [");
    if (bracket != row.npos && row.back() == ']') {
        const auto level = row.substr(bracket + 2, row.size() - bracket - 3);
        if (!level.empty() && level.find_first_not_of("#*+") == level.npos) {
            rating = row.substr(bracket);
            row = row.substr(0, bracket);
        }
    }
    if (const auto* exact = catalog.find("@item-detail/" + std::string(row)))
        return *exact + rating;

    struct Form { const char* key; std::regex en, ja; const char* noun_key; };
    static const Form forms[] = {
        {"material", std::regex(R"(^It is made of (.+)\.$)"), std::regex(R"(^それは(.+)で作られている$)"), "@item-mod/"},
        {"weapon", std::regex(R"(^It can be wielded as a weapon\. \(([0-9]+d[0-9]+) Pierce (-?[0-9]+)%\)$)"),
         std::regex(R"(^それは武器として扱うことができる \(([0-9]+d[0-9]+) 貫通 (-?[0-9]+)%\)$)"), nullptr},
        {"attack", std::regex(R"(^It modifies hit bonus by (-?[0-9]+) and damage bonus by (-?[0-9]+)\.$)"),
         std::regex(R"(^それは命中修正に(-?[0-9]+)を加え、ダメージを(-?[0-9]+)増加させる$)"), nullptr},
        {"defense", std::regex(R"(^It modifies DV by (-?[0-9]+) and PV by (-?[0-9]+)\.$)"),
         std::regex(R"(^それはDVを(-?[0-9]+)上昇させ、PVを(-?[0-9]+)上昇させる$)"), nullptr},
        {"alive", std::regex(R"(^It is alive\. \[Lv:([0-9]+) Exp:([0-9]+)%\]$)"),
         std::regex(R"(^それは生きている \[Lv:([0-9]+) Exp:([0-9]+)%\]$)"), nullptr},
        {"experience", std::regex(R"(^Exp:([0-9]+) Next:([0-9]+)$)"), std::regex(), nullptr},
        {"resist", std::regex(R"(^It grants your resistance to (.+)\.$)"), std::regex(R"(^それは(.+)への耐性を授ける$)"), "@item-detail/element/"},
        {"weak", std::regex(R"(^It weaken your resistance to (.+)\.$)"), std::regex(R"(^それは(.+)への耐性を弱化する$)"), "@item-detail/element/"},
    };
    std::match_results<std::string_view::const_iterator> parts;
    for (const auto& form : forms) {
        if (!std::regex_match(row.begin(), row.end(), parts, form.en) &&
            !std::regex_match(row.begin(), row.end(), parts, form.ja)) continue;
        const auto* pattern = catalog.find(std::string("@item-detail/") + form.key);
        if (!pattern) return {};
        std::string result = *pattern;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            std::string value = parts[i].str();
            if (form.noun_key) {
                const auto* name = catalog.find(form.noun_key + value);
                if (!name) return {};
                value = *name;
            }
            const auto mark = "{" + std::to_string(i) + "}";
            const auto at = result.find(mark);
            if (at == result.npos) return {};
            result.replace(at, mark.size(), value);
        }
        return result + rating;
    }
    return {};
}
}

// Only called with the original item identity captured at an inventory or
// equipment row. Unknown appearances, truncated names and custom titles fall
// back to their original display. Never look up the player's input by item ID.
std::optional<std::string> Catalog::item_label(std::string_view row, std::string_view base,
                                              std::string_view classifier, int id) const {
    if (id == -2 || id == -3) return gather_label(*this, row, base, classifier, id == -3);
    if (id == -1 && base.empty() && classifier == "detail") return detail_line(*this, row);
    if (base.empty() || row.size() > 1024 || id < 0 || id > 1333) return {};
    const bool is_catch = id == 618 || id == 619;
    const auto* name = is_catch ? find("@catch/" + std::to_string(id) + "/" + std::string(classifier))
                                : find("@item/" + std::to_string(id));
    if (!name) return {};

    std::string count, prefix;
    if (take_prefix(row, "★")) prefix = "★";
    else if (take_prefix(row, "☆")) prefix = "☆";
    const auto digits = row.find_first_not_of("0123456789");
    if (digits > 0 && digits != row.npos) {
        auto remainder = row.substr(digits);
        for (const char* counter : {" ", "個の", "着の", "冊の", "本の", "服の", "巻の", "対の", "枚の", "匹の", "通の"}) {
            if (take_prefix(remainder, counter)) {
                count = row.substr(0, digits);
                row = remainder;
                break;
            }
        }
    }
    if (count.empty()) {
        for (const char* article : {"an ", "a ", "the "}) if (take_prefix(row, article)) break;
    }
    if (id == 24) {
        if (const auto* title = find("@book-item/" + std::string(row))) {
            std::string label = prefix + *title;
            if (!count.empty()) label += " ×" + count;
            return label;
        }
    }
    // Locate the actual noun before its stats. A display like "a bronze coin"
    // may contain "bronze" in the noun itself; never strip it as a material first.
    const auto pos = row.rfind(base);
    if (pos == row.npos) return {};
    auto before = row.substr(0, pos);
    auto after = row.substr(pos + base.size());

    if (is_catch) {
        // The producer supplies the fishdatan entry for this exact instance.
        // Only the stock foodname patterns may surround that exact species.
        for (auto modifier : {std::pair{"blessed ", "축복받은 "}, {"祝福された", "축복받은 "},
                              {"cursed ", "저주받은 "}, {"呪われた", "저주받은 "},
                              {"doomed ", "타락한 "}, {"堕落した", "타락한 "}}) {
            if (take_prefix(before, modifier.first)) prefix += modifier.second;
        }
        const bool dish_counter = take_prefix(before, count.empty() ? "dish of " : "dishs of ");
        for (auto modifier : {std::pair{"sample ", "견본용 "}, {"見本用の", "견본용 "},
                              {"rotten ", "썩은 "}, {"腐った", "썩은 "}}) {
            if (take_prefix(before, modifier.first)) prefix += modifier.second;
        }
        const auto digits_end = before.find_first_not_of("0123456789");
        if (digits_end > 0 && digits_end != before.npos) {
            auto remaining = before.substr(digits_end);
            if (!take_prefix(remaining, "cm ") && !take_prefix(remaining, "cmの")) return {};
            prefix += std::string(before.substr(0, digits_end)) + "센티미터 ";
            before = remaining;
        }
        if (id == 619 && !count.empty()) take_prefix(after, "s");
        std::string rank;
        const auto rank_at = after.rfind("(Rank");
        if (rank_at != after.npos && after.back() == ')') {
            const auto number = after.substr(rank_at + 5, after.size() - rank_at - 6);
            if (number.size() != 1 || number[0] < '1' || number[0] > '9') return {};
            const auto* label = find("@catch-dish/rank");
            if (!label) return {};
            rank = " (" + *label + " " + std::string(number) + ")";
            after = after.substr(0, rank_at);
        }
        std::string dish = *name;
        if (!before.empty() || !after.empty()) {
            if (id != 618) return {};
            const auto* format = find("@catch-dish/" + std::string(before) + "{}" + std::string(after));
            if (!format) return {};
            dish = *format;
            const auto place = dish.find("{}");
            if (place == dish.npos) return {};
            dish.replace(place, 2, *name);
        }
        else if (dish_counter || !rank.empty()) return {};
        return prefix + dish + rank + (count.empty() ? "" : " ×" + count);
    }
    // The HSP classifier supplies words such as "scroll of", "rods of" or
    // "bottles of". The Korean name already includes its appropriate item type.
    if (!classifier.empty() && !before.empty()) {
        for (const char* join : {" of ", "s of ", " with ", "s with "}) {
            if (take_suffix(before, std::string(classifier) + join)) break;
        }
    }
    // These two classifiers are generated by the VM even with an empty
    // classifier field, ahead of the material (e.g. "a pair of bronze heavy boots").
    for (const char* extra : {"pair of ", "pairs of ", "cargo of ", "cargos of "}) {
        if (take_prefix(before, extra)) break;
    }
    if (!before.empty()) {
        take_suffix(before, " ");
        take_suffix(before, "の");
        const auto* modifier = find("@item-mod/" + std::string(before));
        if (!modifier) return {};
        prefix += *modifier + " ";
    }
    if (!count.empty()) take_prefix(after, "s");
    // Only numeric combat annotations, weights and shortcuts may follow the
    // base noun. An unidentified alias or a named artifact uses the VM text.
    if (after.find_first_not_of(" 0123456789[](){},.+-dxs%") != after.npos) return {};
    std::string label = prefix + *name;
    if (!count.empty()) label += " ×" + count;
    return label.append(after);
}
}  // namespace elona::localization

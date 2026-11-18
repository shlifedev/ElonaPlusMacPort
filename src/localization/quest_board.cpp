#include "quest_board.h"
#include "alias.h"
#include "platform/game_files.h"
#include <algorithm>
#include <regex>

namespace elona::localization {
namespace {
std::string known(const Catalog& catalog, const std::string& source) {
    if (const auto* translated = catalog.find(source)) return *translated;
    return source;
}

std::optional<std::string> town_name(const Catalog& catalog, const std::string& name) {
    // Shipped named characters may already have a reviewed full appellation.
    const bool named = (name.find('<') != name.npos && name.find('>') != name.npos)
                    || (name.find("『") != name.npos && name.find("』") != name.npos);
    if (named) {
        if (const auto* title = catalog.find(name)) return *title;
    }
    // snbakery/sngeneral and the NPC database form names in opposite orders
    // in English and Japanese. Match only a reviewed, complete occupational title.
    const auto split = name.rfind(" the ");
    if (split != name.npos && split != 0) {
        if (const auto* role = catalog.find("@npc-title/" + name.substr(split+5)))
            return *role + " " + name.substr(0, split);
    }
    // Some Japanese occupations contain の. Try the longest matching prefix.
    if (name.empty() || static_cast<unsigned char>(name.front()) < 128) return {};
    for (auto split = name.size()-1; split > 0; --split) {
        if ((static_cast<unsigned char>(name[split]) & 0xc0) == 0x80) continue;
        if (name.compare(0, split, "見習い") == 0 && name.compare(split, 3, "『") != 0) continue;
        if (const auto* role = catalog.find("@npc-prefix/" + name.substr(0, split)))
            return *role + " " + name.substr(split);
    }
    return {};
}

std::string npc_details(const Catalog& catalog, std::string details) {
    // Only the independently provided name/alias are stripped before this point.
    // All remaining words originate from the original gender and stat branches.
    static const std::regex format(
        R"(^(Female|Male|male\?|female\?|male|female|unknown|bisexual|自称女性|自称男性|性別不明|両性具有|男性|女性)?(?: (?:Fame: |名声 )(-?[0-9]+))?(?: (?:Guild Size:|ギルド規模:)(-?[0-9]+))?(?: (?:Shop Rank:|店の規模:)(-?[0-9]+))?(?: (?:Activity:|活性度:)(-?[0-9]+) )?$)");
    std::smatch match;
    if (!std::regex_match(details, match, format)) return details;
    std::string result;
    if (match[1].matched) {
        auto gender = match[1].str();
        if (gender == "male") gender = "Male";
        if (gender == "female") gender = "Female";
        result = known(catalog, gender);
    }
    const char* labels[] = {"명성", "길드 규모", "상점 규모", "활성도"};
    for (size_t i = 2; i < match.size(); ++i) {
        if (!match[i].matched) continue;
        if (!result.empty()) result += " · ";
        result += std::string(labels[i-2]) + " " + match[i].str();
    }
    return result;
}

std::string reward_text(const Catalog& catalog, const std::string& source) {
    static const std::regex coins_en(R"(^([0-9]+) gold pieces(?: and (.+))?$)");
    static const std::regex coins_jp(R"(^金貨([0-9]+)枚(?:と(.+))?$)");
    std::smatch match;
    if (!std::regex_match(source, match, coins_en) && !std::regex_match(source, match, coins_jp))
        return known(catalog, source);
    std::string result = "금화 " + match[1].str() + "닢";
    if (match[2].matched) {
        const auto extra = match[2].str();
        const auto* label = catalog.find("@board-reward/" + extra);
        result += ", " + (label ? *label : known(catalog, extra));
    }
    return result;
}
}

std::optional<std::string> town_caption(const Catalog& catalog, int scope, const std::string& fields) {
    if (scope == 24) {
        static const std::regex pages(R"(^Page ([0-9]+)/([0-9]+)$)");
        std::smatch match;
        if (!std::regex_match(fields, match, pages)) return {};
        return match[1].str() + "/" + match[2].str() + "쪽";
    }
    const auto tab = fields.find('\t');
    if (tab == fields.npos || tab == 0) return {};
    const auto name = fields.substr(0, tab);
    const auto translated_name = town_name(catalog, name);
    if (scope == 25) return fields.find('\t', tab+1) == fields.npos ? translated_name : std::nullopt;
    const auto end = fields.find('\t', tab+1);
    if (scope != 26 || end == fields.npos || fields.find('\t', end+1) != fields.npos) return {};
    const auto alias = fields.substr(tab+1, end-tab-1);
    const auto original = fields.substr(end+1);
    if (translated_name && alias.empty()) {
        if (const auto* full = catalog.find(original)) return *full;
    }
    const auto english = name + (alias.empty() ? "" : " of " + alias) + " ";
    const auto japanese = (alias.empty() ? "" : alias + " ") + name + " ";
    if (original.compare(0, english.size(), english) != 0 &&
        original.compare(0, japanese.size(), japanese) != 0) return {};
    const auto prefix_size = original.compare(0, english.size(), english) == 0 ? english.size() : japanese.size();
    auto result = translated_name.value_or(name);
    if (!alias.empty()) result += " 〈" + localized_alias(catalog, alias).value_or(alias) + "〉";
    const auto details = npc_details(catalog, original.substr(prefix_size));
    if (!details.empty()) result += " " + details;
    return result;
}

std::optional<BoardPost> board_post(const Catalog& catalog, const std::array<std::string, 7>& args, int item) {
    const auto& section = args[0];
    const auto& row = args[1];
    const std::string key = "@board/" + section + "/" + platform::sha256_hex(row.data(), row.size());
    const auto* title = catalog.find(key + "/title");
    const auto* pattern = catalog.find(key + "/body");
    if (!title || !pattern) return {};
    const auto group = section.substr(0, section.find(','));
    std::string objective = args[2], ref = args[3], reward = reward_text(catalog, args[4]);
    const std::string client = town_name(catalog, args[5]).value_or(args[5]);
    const std::string map = known(catalog, args[6]);
    if (group == "%SUPPLY" || group == "%DELIVER" || group == "%COLLECT") {
        auto& object = group == "%SUPPLY" ? objective : ref;
        if (item >= 0 && item != 1061) {
            if (auto* label = catalog.find("@item/" + std::to_string(item))) {
                const bool heavy = object.find("(Heavy!)") != object.npos || object.find("(凄く重い)") != object.npos;
                object = *label;
                if (heavy) object += " (매우 무거움!)";
            }
        }
        if (group == "%DELIVER") objective = map + "의 " + client + "에게 " + ref + " 배달";
    } else {
        objective = known(catalog, objective);
    }
    if (group == "%PARTY" || group == "%TRAP") {
        for (std::string_view suffix : {" points", "ポイント"}) {
            if (ref.size() > suffix.size() && ref.compare(ref.size() - suffix.size(), suffix.size(), suffix) == 0)
                ref = ref.substr(0, ref.size() - suffix.size()) + "점";
        }
    }
    const std::string_view names[] = {"objective", "ref", "reward", "client", "map"};
    const std::string_view values[] = {objective, ref, reward, client, map};
    std::string body;
    for (std::size_t i = 0; i < pattern->size();) {
        auto open = pattern->find('{', i);
        body.append(*pattern, i, open == pattern->npos ? pattern->npos : open-i);
        if (open == pattern->npos) break;
        auto close = pattern->find('}', open+1);
        if (close == pattern->npos) return {};
        const auto name = pattern->substr(open+1, close-open-1);
        auto field = std::find(std::begin(names), std::end(names), name);
        if (field == std::end(names)) return {};
        body += values[field-std::begin(names)];  // append opaquely, never re-expand a name
        i = close+1;
    }
    if (body.find_first_of("\r\n\t") != body.npos || body.size() > 16384) return {};
    return BoardPost{*title, std::move(body)};
}
}

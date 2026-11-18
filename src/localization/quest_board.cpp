#include "quest_board.h"
#include "platform/game_files.h"
#include <algorithm>
#include <regex>

namespace elona::localization {
namespace {
std::string known(const Catalog& catalog, const std::string& source) {
    if (const auto* translated = catalog.find(source)) return *translated;
    return source;
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

std::optional<BoardPost> board_post(const Catalog& catalog, const std::array<std::string, 7>& args, int item) {
    const auto& section = args[0];
    const auto& row = args[1];
    const std::string key = "@board/" + section + "/" + platform::sha256_hex(row.data(), row.size());
    const auto* title = catalog.find(key + "/title");
    const auto* pattern = catalog.find(key + "/body");
    if (!title || !pattern) return {};
    const auto group = section.substr(0, section.find(','));
    std::string objective = args[2], ref = args[3], reward = reward_text(catalog, args[4]);
    const std::string& client = args[5];  // includes names chosen by the player
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

#include "catalog.h"
#include "platform/game_files.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <vector>
#include <stdexcept>

namespace elona::localization {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = text[i++];
        if (c < 0x80) { if (!c) return false; continue; }
        unsigned n, value, minimum;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (i + n > text.size()) return false;
        while (n--) { c = text[i++]; if ((c & 0xc0) != 0x80) return false; value = (value << 6) | (c & 63); }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
namespace {
std::string unescape(std::string_view in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '\\') {
            if (++i == in.size()) throw std::runtime_error("unfinished escape");
            switch (in[i]) {
            case '\\': c = '\\'; break;
            case 't': c = '\t'; break;
            // draw_line receives one original line; multiline translation must
            // be laid out upstream, never smuggled through a one-line entry.
            default: throw std::runtime_error("unknown escape (only \\\\ and \\t supported)");
            }
        }
        if (static_cast<unsigned char>(c) < 32 && c != '\t') throw std::runtime_error("control character");
        out += c;
    }
    if (!valid_utf8(out)) throw std::runtime_error("invalid UTF-8");
    return out;
}
}
Catalog Catalog::load(const std::filesystem::path& path, const std::filesystem::path& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open translation catalog");
    Catalog catalog;
    std::string line;
    std::size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (line.empty() || line.front() == '#') continue;
        try {
            auto tab = line.find('\t');
            if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos)
                throw std::runtime_error("expected source<TAB>translation");
            auto source = unescape(std::string_view(line).substr(0, tab));
            auto target = unescape(std::string_view(line).substr(tab + 1));
            if (source.empty() || target.empty()) throw std::runtime_error("empty source or translation");
            if (source.size() > 16384 || target.size() > 16384) throw std::runtime_error("entry too long");
            auto [it, added] = catalog.entries_.emplace(source, target);
            constexpr std::string_view log_prefix = "@log-template/";
            if (added && source.compare(0, log_prefix.size(), log_prefix) == 0)
                catalog.add_log_template(std::string_view(source).substr(log_prefix.size()), target);
            if (!added && it->second != target) throw std::runtime_error("conflicting translations for one source");
        } catch (const std::exception& e) {
            throw std::runtime_error("translation catalog line " + std::to_string(number) + ": " + e.what());
        }
    }
    if (!fallback.empty()) {
        auto lower_priority = Catalog::load(fallback);
        for (const auto& entry : lower_priority.entries_) {
            if (entry.first.size() != 64 || entry.first.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw std::runtime_error("translation fallback requires lowercase SHA-256 display keys");
        }
        catalog.draft_by_hash_ = std::move(lower_priority.entries_);
    }
    return catalog;
}
const std::string* Catalog::find(std::string_view source, bool prose_context) const {
    const auto it = entries_.find(std::string(source));
    if (it != entries_.end()) return &it->second;
    // Short authored labels may also be save names. These aliases are valid
    // only at a known game-text boundary, including a verified source literal.
    if (prose_context && !source.empty() && source.front() != '@') {
        const auto fixed = entries_.find(std::string("@fixed/").append(source));
        if (fixed != entries_.end()) return &fixed->second;
    }
    if (draft_by_hash_.empty() || source.size() < 3 || source.front() == '@' || source.size() > 2048)
        return nullptr;
    // Arbitrary short labels can be a player name or a saved character's alias.
    // Standalone draft words are allowed only at a known prose boundary.
    if (!prose_context && (source.size() < 12 ||
            (source.find_first_of(".!?") == source.npos && source.find("。") == source.npos &&
             source.find("？") == source.npos && source.find("！") == source.npos &&
             (source.size() < 24 || source.find(' ') == source.npos))))
        return nullptr;
    const auto match = draft_by_hash_.find(platform::sha256_hex(source.data(), source.size()));
    return match == draft_by_hash_.end() ? nullptr : &match->second;
}
namespace {
bool digits(std::string_view s) {
    return !s.empty() && s.find_first_not_of("0123456789") == s.npos;
}
std::string scoped_key(std::string_view prefix, std::string_view value) {
    return std::string(prefix).append(value);
}
}
std::optional<std::string> Catalog::sheet_text(std::string_view source) const {
    if (source.empty() || source.size() > 192) return {};
    constexpr std::string_view race_en = "Race: ";
    constexpr std::string_view race_ja = "種族: ";
    constexpr std::string_view extra = "(extra)";
    const bool has_en_title = source.substr(0, race_en.size()) == race_en;
    const bool has_ja_title = source.substr(0, race_ja.size()) == race_ja;
    if (has_en_title || has_ja_title || source.substr(0, extra.size()) == extra) {
        if (has_en_title) source.remove_prefix(race_en.size());
        if (has_ja_title) source.remove_prefix(race_ja.size());
        const bool is_extra = source.substr(0, extra.size()) == extra;
        if (is_extra) source.remove_prefix(extra.size());
        const auto* race = find(scoped_key("@sheet/race/", source));
        const auto* title = has_en_title || has_ja_title ? find("@sheet/Race") : nullptr;
        const auto* marker = is_extra ? find("@sheet/extra") : nullptr;
        if (!race || ((has_en_title || has_ja_title) && !title) || (is_extra && !marker)) return {};
        std::string result;
        if (title) result = *title + ": ";
        if (marker) result += "(" + *marker + ") ";
        return result + *race;
    }
    constexpr std::string_view proficiency = "Proficient in ";
    if (source.substr(0, proficiency.size()) == proficiency) {
        const auto* title = find("@sheet/proficiency");
        if (!title) return {};
        std::string result(*title);
        source.remove_prefix(proficiency.size());
        for (;;) {
            const auto comma = source.find(',');
            const auto* weapon = find(scoped_key("@sheet/weapon/", source.substr(0, comma)));
            if (!weapon) return {};
            result += *weapon;
            if (comma == source.npos) return result;
            result += ", ";
            source.remove_prefix(comma + 1);
        }
    }
    struct Form {
        const char* key;
        const char* marker;
        std::regex pattern;
        std::size_t label_group = 0;
    };
    static const Form forms[] = {
        {"page", "Page.", std::regex(R"(^Page\.([0-9]+)/([0-9]+)$)")},
        {"appearance", " ", std::regex(R"(^(.+?)(?: |　)+(u?[0-9]+|N/A)$)"), 1},
        {"ability-cost", " Sp", std::regex(R"(^([0-9]+) Sp$)")},
        {"turns", " Turns", std::regex(R"(^([0-9]+) Turns$)")},
        {"level", " level", std::regex(R"(^([0-9]+) level$)")},
        {"time", " Sec", std::regex(R"(^([0-9]+):([0-9]+):([0-9]+) Sec$)")},
        {"load", " (", std::regex(R"(^([0-9]+\.[0-9]+s) \((Light|Medium|Heavy)\)$)"), 2},
        {"items", " items  (Weight ", std::regex(R"(^([0-9]+) items  \(Weight ([0-9]+\.[0-9]+s)/([0-9]+\.[0-9]+s) Cargo ([0-9]+\.[0-9]+s)/([0-9]+\.[0-9]+s)\)$)")},
        {"equipment", "Equip weight: ", std::regex(R"(^Equip weight: ([0-9]+\.[0-9]+s)\((Light|Medium|Heavy)\) Hit Bonus:(-?[0-9]+) Damage Bonus:(-?[0-9]+)  DV/PV:(-?[0-9]+)/(-?[0-9]+)$)"), 2},
    };
    std::match_results<std::string_view::const_iterator> groups;
    for (const auto& form : forms) {
        if (source.find(form.marker) == source.npos || !std::regex_match(source.begin(), source.end(), groups, form.pattern))
            continue;
        const auto* format = find(scoped_key("@metric/", form.key));
        if (!format) return {};
        std::string result(*format);
        for (std::size_t i = 1; i < groups.size(); ++i) {
            std::string value = groups[i].str();
            if (i == form.label_group) {
                const auto* label = find(scoped_key(std::string_view(form.key) == "appearance" ? "@appearance/" : "@weight/", value));
                if (!label) return {};
                value = *label;
            } else if (value == "N/A") {
                const auto* unavailable = find("@appearance/N/A");
                if (!unavailable) return {};
                value = *unavailable;
            }
            const auto mark = "{" + std::to_string(i) + "}";
            auto at = result.find(mark);
            if (at == result.npos) return {};
            result.replace(at, mark.size(), value);
        }
        return result;
    }
    return {};
}

std::optional<std::string> Catalog::hud_text(std::string_view s) const {
    if (s.size() < 3) return {};
    auto prefix = s.substr(0, 2);
    auto slash = s.find('/');
    if ((prefix == "Lv" || prefix == "Sp") && slash != s.npos) {
        auto number = [](std::string_view v) {
            if (!v.empty() && v.front() == '-') v.remove_prefix(1);
            return digits(v);
        };
        if (number(s.substr(2, slash-2)) && number(s.substr(slash+1))) {
            if (auto label = find(scoped_key("@hud/", prefix))) return *label + " " + std::string(s.substr(2));
        }
        return {};
    }
    auto day_end = s.find(" / ");
    if (day_end != s.npos) {
        const auto progress = s.substr(day_end + 3);
        const auto* day = find(scoped_key("@day/", s.substr(0, day_end)));
        if (!day || std::string_view("DESTINY").substr(0, progress.size()) != progress) return {};
        if (progress.empty()) return *day;
        if (const auto* label = find("@hud/destiny"))
            return *day + " / " + *label + " " + std::to_string(progress.size()) + "/7";
        return {};
    }
    auto split = s.find(' ');
    if (split == s.npos) return {};
    const auto unit = s.substr(split + 1);
    if ((unit == "gp" || unit == "pp") && digits(s.substr(0, split))) {
        if (const auto* label = find(scoped_key("@hud/", unit)))
            return *label + " " + std::string(s.substr(0, split));
        return {};
    }
    auto time = find(scoped_key("@clock/", s.substr(0, split)));
    if (!time) return {};
    auto weather = s.substr(split+1);
    if (weather.empty()) return *time;
    if (auto w = find(scoped_key("@weather/", weather))) return *time + " · " + *w;
    return {};
}

std::optional<std::string> Catalog::log_text(std::string_view s, int starter_pet_id) const {
    std::string timestamp;
    if (!s.empty() && s.front() == '[' && s.substr(0, 7) != "[News] ") {
        auto end = s.find("] "), colon = s.find(':');
        if (end == s.npos || colon == s.npos || colon >= end ||
            !digits(s.substr(1, colon-1)) || !digits(s.substr(colon+1, end-colon-1))) return {};
        timestamp.assign(s.substr(0, end+2));
        s.remove_prefix(end+2);
    }
    if (s.substr(0, 7) == "[News] ") {
        const auto* tag = find("@news/tag");
        if (tag) {
            if (auto news = news_text(s.substr(7))) return timestamp + *tag + " " + *news;
        }
        return {};
    }
    if (s != "home-rank") {
        if (auto target = find(scoped_key("@log/", s))) return timestamp + *target;
    }
    if (auto matched = adventure_log(s, starter_pet_id)) return timestamp + *matched;
    // msg_write splits a complete English sentence into individual words.
    if (s.size() > 3 && s.find(' ') != s.npos && s.find_last_of(".?!") == s.size()-1) {
        if (auto target = find(s, true)) return timestamp + *target;
    }
    if (s.size() > 256 || s.substr(0, 16) != "Furniture Value:") return {};
    static const std::regex rank(R"(^Furniture Value:(-?[0-9]+) Heirloom Value:(-?[0-9]+) Home Rank:([0-9]+)(?:st|nd|rd|th)->([0-9]+)(?:st|nd|rd|th) Your home is now known as <([^<>]+)>\.$)");
    std::match_results<std::string_view::const_iterator> parts;
    if (!std::regex_match(s.begin(), s.end(), parts, rank)) return {};
    auto title = find(scoped_key("@home/", parts[5].str()));
    auto format = find("@log/home-rank");
    if (!title || !format) return {};
    std::string result = *format;
    for (int n = 1; n <= 5; ++n) {
        auto marker = "{" + std::to_string(n) + "}";
        auto at = result.find(marker);
        if (at == result.npos) return {};
        result.replace(at, marker.size(), n == 5 ? *title : parts[n].str());
    }
    return timestamp + result;
}

// The original English logger wraps and prints one space-terminated word at a
// time. Paint each translated row on its first word, while allowing the VM to
// append, measure, scroll and save its original message exactly as before.
std::deque<LogWord> Catalog::log_words(std::string source, int used, int limit, int starter_pet_id,
                                       std::optional<std::string> target) const {
    std::deque<LogWord> words;
    if (!target) target = log_text(source, starter_pet_id);
    if (!target || used < 0 || limit <= 0) return words;
    source += ' ';
    std::size_t begin = 0, row_start = 0, target_start = 0;
    // Map row breaks onto UTF-8 syllable boundaries. Prefer a nearby word
    // boundary to avoid breaking labels, values, or a Korean word.
    std::vector<std::size_t> boundaries{0};
    for (std::size_t i=1; i<target->size(); ++i) {
        if ((static_cast<unsigned char>((*target)[i]) & 0xc0) != 0x80) boundaries.push_back(i);
    }
    boundaries.push_back(target->size());
    auto finish = [&](std::size_t source_end) {
        auto glyph = source_end * (boundaries.size()-1) / source.size();
        // Consecutive short rows can map to the same glyph after the previous
        // row consumed a word-ending space. Never emit that span twice.
        auto end = std::max(target_start, boundaries[glyph]);
        if (source_end < source.size()) {
            auto space = target->rfind(' ', end);
            if (space != target->npos && space > target_start && end-space < 12) end = space+1;
        }
        words[row_start].display = target->substr(target_start, end-target_start);
        target_start = end;
    };
    while (begin < source.size()) {
        auto end = source.find(' ', begin)+1;
        int length = static_cast<int>(end-begin);
        if (length > limit) return {};
        if (used > limit-length) {
            if (!words.empty()) finish(begin);
            row_start = words.size();
            used = 0;
        }
        words.push_back({source.substr(begin,end-begin), {}});
        used += length;
        begin = end;
    }
    finish(source.size());
    return words;
}

void JapaneseLog::begin(std::string_view original, std::string target) {
    original_.assign(original);
    target_ = std::move(target);
    consumed_ = displayed_ = 0;
    glyphs_.clear();
    for (std::size_t i = 0; i < target_.size(); ++i) {
        if ((static_cast<unsigned char>(target_[i]) & 0xc0) != 0x80) glyphs_.push_back(i);
    }
    glyphs_.push_back(target_.size());
}

std::optional<std::string> JapaneseLog::consume(std::string_view chunk) {
    if (original_.empty() || chunk.empty()) return {};
    if (original_.compare(consumed_, chunk.size(), chunk) != 0) {
        original_.clear();
        return {};
    }
    consumed_ += chunk.size();
    auto end = glyphs_[consumed_ * (glyphs_.size() - 1) / original_.size()];
    if (consumed_ == original_.size()) {
        original_.clear();
    } else {
        const auto space = target_.rfind(' ', end);
        if (space != target_.npos && space > displayed_ && end - space < 12) end = space + 1;
    }
    end = std::max(end, displayed_);
    auto part = target_.substr(displayed_, end - displayed_);
    displayed_ = end;
    return part;
}
}  // namespace elona::localization

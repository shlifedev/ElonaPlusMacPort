#include "catalog.h"
#include "alias.h"
#include <regex>
#include <stdexcept>

namespace elona::localization {
void Catalog::add_log_template(std::string_view pattern, const std::string& translation) {
    const auto open = pattern.find('{'), close = pattern.find('}');
    if (open == pattern.npos || close == pattern.npos || close <= open ||
        pattern.find('{', open+1) != pattern.npos || pattern.find('}', close+1) != pattern.npos)
        throw std::runtime_error("expected one argument in a message log template");
    std::string argument(pattern.substr(open, close-open+1));
    const auto at = translation.find(argument);
    if ((argument != "{name}" && argument != "{place}" && argument != "{object}" && argument != "{enemy}" &&
         argument != "{skill}") ||
        at == translation.npos || translation.find('{') != at ||
        translation.find('{', at+1) != translation.npos || translation.find('}') != at+argument.size()-1 ||
        translation.find('}', at+argument.size()) != translation.npos ||
        pattern.size() - argument.size() < 6)
        throw std::runtime_error("invalid message log template argument");
    log_templates_.push_back({std::string(pattern.substr(0, open)), std::string(pattern.substr(close+1)),
                              std::move(argument), translation});
}

namespace {
std::optional<std::string> collection_log(const Catalog& catalog, std::string_view message) {
    if (message.substr(0, 8) != "You get ") return {};
    message.remove_prefix(8);
    static const std::regex catch_form(R"(^(?:an? |the )?(?:([0-9]+)cm )?(.+)!$)");
    static const std::regex material_form(R"(^([0-9]+) (.+)\. $)");
    std::match_results<std::string_view::const_iterator> parts;
    const bool caught = std::regex_match(message.begin(), message.end(), parts, catch_form);
    if (!caught && !std::regex_match(message.begin(), message.end(), parts, material_form)) return {};
    const char* domain = "@gather-log/name/";
    if (caught) domain = parts[1].matched ? "@catch-log/fish/" : "@catch-log/object/";
    const auto* noun = catalog.find(domain + parts[2].str());
    const auto* pattern = catalog.find(caught ? "@catch-log/land" : "@gather-log/get");
    if (!noun || !pattern) return {};
    std::string result = *pattern;
    if (caught && parts[1].matched) {
        const auto* size = catalog.find("@catch-log/size");
        const auto at = result.find("{item}");
        if (!size || at == result.npos) return {};
        result.replace(at, 6, *size);
    }
    if (parts[1].matched) {
        const auto at = result.find("{size}");
        if (at == result.npos) return {};
        result.replace(at, 6, parts[1].str());
    }
    const auto at = result.find("{item}");
    if (at == result.npos) return {};
    result.replace(at, 6, *noun);
    return result;
}
}
std::optional<std::string> Catalog::adventure_log(std::string_view source, int starter_pet_id) const {
    if (source.size() > 512) return {};
    if (auto collected = collection_log(*this, source)) return collected;
    const LogTemplate* match = nullptr;
    std::string replacement;
    for (const auto& rule : log_templates_) {
        if (source.size() <= rule.prefix.size() + rule.suffix.size() ||
            source.substr(0, rule.prefix.size()) != rule.prefix ||
            source.substr(source.size()-rule.suffix.size()) != rule.suffix) continue;
        const auto value = source.substr(rule.prefix.size(), source.size()-rule.prefix.size()-rule.suffix.size());
        if (value.size() > 150 || value.find_first_of("\r\n\t{}") != value.npos) continue;
        std::string argument(value);
        if (rule.argument != "{name}") {
            // An unknown monster, place or inventory format might be a custom
            // name. Only use identifiers reviewed for this notification.
            const auto key = "@log-noun/" + rule.argument + "/" + argument;
            const auto* known = find(key);
            if (!known) continue;
            argument = *known;
        }
        if (starter_pet_id && rule.argument == "{name}" && rule.prefix.empty() &&
            rule.suffix == " joins your party!") {
            const auto split = value.rfind(" the ");
            if (split != value.npos && split > 0) {
                const auto key = "@starter-pet/" + std::to_string(starter_pet_id) + "/" +
                                 std::string(value.substr(split + 5));
                if (const auto* species = find(key))
                    argument = *species + " " + std::string(value.substr(0, split));
            }
        }
        if (match) return {}; // ambiguous templates leave the original visible
        match = &rule;
        replacement = std::move(argument);
    }
    if (!match) return {};
    std::string result = match->translation;
    result.replace(result.find(match->argument), match->argument.size(), replacement);
    return result;
}
namespace {
std::string news_item(const Catalog& catalog, const std::string& original) {
    if (auto* known = catalog.find("@log-noun/{object}/" + original)) return *known;
    // Parse only the generated equipment form. A player-supplied title remains literal.
    static const std::regex gear(R"(^(?:the |a |an )?(?:pair of )?([^<>]+?)(?: <([^<>]+)>)?( [\[\]()0-9,d+% x-]+)?$)");
    std::smatch parts;
    if (!std::regex_match(original, parts, gear)) return original;
    std::string noun = parts[1].str();
    const std::string* base = catalog.find("@news-item/" + noun);
    std::string prefix;
    for (auto split = noun.find(' '); !base && split != noun.npos; split = noun.find(' ', split + 1)) {
        if (auto* modifier = catalog.find("@item-mod/" + noun.substr(0, split))) {
            base = catalog.find("@news-item/" + noun.substr(split + 1));
            if (base) prefix = *modifier + " ";
        }
    }
    if (!base) return original;
    std::string label = prefix + *base;
    if (parts[2].matched) {
        auto alias = parts[2].str();
        label += " 〈" + localized_alias(catalog, alias).value_or(alias) + "〉";
    }
    return label + parts[3].str();
}
}

std::optional<std::string> Catalog::news_text(std::string_view source) const {
    if (source.size() > 1024 || source.find_first_of("\r\n\t") != source.npos) return {};
    struct Rule { const char* key; std::regex pattern; };
    static const Rule rules[] = {
        {"@news/accomplishment", std::regex(R"(^(.+?) has finished a quest and gained ([0-9]+) fame\.$)")},
        {"@news/growth", std::regex(R"(^(.+?) has gained experience and achieved level ([0-9]+)\.$)")},
        {"@news/recovery", std::regex(R"(^(.+?) has fully recovered from injury\.$)")},
        {"@news/retirement", std::regex(R"(^(.+?) realized the limitations and retired\.$)")},
        {"@news/discovery", std::regex(R"(^(.+?) has discovered (.+) in (.+)\.$)")},
    };
    const std::string text(source);
    std::smatch parts;
    for (const auto& rule : rules) {
        if (!std::regex_match(text, parts, rule.pattern)) continue;
        const auto* format = find(rule.key);
        if (!format) return {};
        const std::string name = parts[1].str();
        std::string result;
        for (std::size_t cursor = 0; cursor < format->size();) {
            const auto open = format->find('{', cursor);
            result.append(*format, cursor, open - cursor);
            if (open == format->npos) break;
            const auto close = format->find('}', open + 1);
            if (close == format->npos) return {};
            auto field = format->substr(open, close - open + 1);
            if (field == "{name}") result += localized_alias(*this, name).value_or(name);
            else if (field == "{number}" && parts.size() == 3) result += parts[2].str();
            else if (field == "{item}" && parts.size() == 4) result += news_item(*this, parts[2].str());
            else if (field == "{place}" && parts.size() == 4) {
                auto place = parts[3].str();
                const auto* label = find("@log-noun/{place}/" + place);
                result += label ? *label : place;
            } else return {};
            cursor = close + 1;
        }
        return result;
    }
    return {};
}
} // namespace elona::localization

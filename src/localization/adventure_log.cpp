#include "catalog.h"
#include <stdexcept>

namespace elona::localization {
void Catalog::add_log_template(std::string_view pattern, const std::string& translation) {
    const auto open = pattern.find('{'), close = pattern.find('}');
    if (open == pattern.npos || close == pattern.npos || close <= open ||
        pattern.find('{', open+1) != pattern.npos || pattern.find('}', close+1) != pattern.npos)
        throw std::runtime_error("expected one argument in a message log template");
    std::string argument(pattern.substr(open, close-open+1));
    const auto at = translation.find(argument);
    if ((argument != "{name}" && argument != "{place}" && argument != "{object}" && argument != "{enemy}") ||
        at == translation.npos || translation.find('{') != at ||
        translation.find('{', at+1) != translation.npos || translation.find('}') != at+argument.size()-1 ||
        translation.find('}', at+argument.size()) != translation.npos ||
        pattern.size() - argument.size() < 6)
        throw std::runtime_error("invalid message log template argument");
    log_templates_.push_back({std::string(pattern.substr(0, open)), std::string(pattern.substr(close+1)),
                              std::move(argument), translation});
}

std::optional<std::string> Catalog::adventure_log(std::string_view source) const {
    if (source.size() > 512) return {};
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
        if (match) return {}; // ambiguous templates leave the original visible
        match = &rule;
        replacement = std::move(argument);
    }
    if (!match) return {};
    std::string result = match->translation;
    result.replace(result.find(match->argument), match->argument.size(), replacement);
    return result;
}
} // namespace elona::localization

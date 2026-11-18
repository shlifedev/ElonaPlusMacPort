#include "evolution.h"
#include "catalog.h"

namespace elona::localization {
namespace {
std::optional<std::string_view> field(std::string_view& input, char delimiter) {
    const auto end = input.find(delimiter);
    if (end == input.npos) return std::nullopt;
    const auto part = input.substr(0, end);
    input.remove_prefix(end + 1);
    return part;
}

bool number(std::string_view value) {
    return !value.empty() && value.size() <= 10 && value.find_first_not_of("0123456789") == value.npos;
}
}

std::optional<std::string> evolution_message(const Catalog& catalog, std::string_view identity) {
    constexpr std::string_view prefix = "@evolution/";
    if (identity.substr(0, prefix.size()) != prefix) return std::nullopt;
    identity.remove_prefix(prefix.size());
    const auto mode = field(identity, '/');
    const auto race = field(identity, '/');
    const auto stage = field(identity, '\t');
    const auto original = field(identity, '\t');
    if (!mode || !race || !stage || !original || original->empty() ||
        !number(*mode) || !number(*race) || !number(*stage)) return std::nullopt;

    const auto base = std::string(prefix) + std::string(*mode);
    const auto species = [&](const char* form) -> const std::string* {
        if (auto text = catalog.find(base + form)) return text;
        if (auto text = catalog.find(base + "/race/" + std::string(*race) + form)) return text;
        return catalog.find(base + "/stage/" + std::string(*stage) + form);
    };
    const auto old_form = species("/old");
    const auto new_form = species("/new");
    if (!old_form || !new_form) return std::nullopt;

    std::string message = "축하합니다! ";
    // A nickname may start with the engine's default name. Only omit it
    // when the full original field equals evold in the active language.
    if (!identity.empty() && identity != *original) {
        message += "『";
        message += identity;
        message += "』 진화! ";
    } else {
        message += "동료 진화! ";
    }
    return message + *old_form + " → " + *new_form;
}
}

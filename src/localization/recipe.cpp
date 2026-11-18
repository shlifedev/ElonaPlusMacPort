#include "recipe.h"
#include "catalog.h"
#include <charconv>
#include <vector>

namespace elona::localization {
namespace {
int number(std::string_view field) {
    int value = -1;
    const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size() ? value : -1;
}

std::string named(const Catalog& catalog, const std::string& source) {
    if (const auto* value = catalog.find("@recipe/name/" + source)) return *value;
    if (const auto* value = catalog.find(source, true)) return *value;
    return source;
}
}

std::optional<std::string> recipe_caption(const Catalog& catalog, std::string_view fields) {
    std::vector<std::string> parts;
    while (true) {
        const auto tab = fields.find('\t');
        parts.emplace_back(fields.substr(0, tab));
        if (tab == fields.npos) break;
        fields.remove_prefix(tab + 1);
    }
    if (parts.size() < 2) return {};
    const std::string kind = parts.front();
    const auto* pattern = catalog.find("@recipe/form/" + kind);
    if (!pattern) return {};
    const std::string original = std::move(parts.back());
    parts.pop_back(); // Exact CP932 draw is matched separately by the renderer.
    parts.erase(parts.begin());

    if (kind == "recipe" || kind == "chosen") {
        if (parts.size() < 2) return {};
        const int id = number(parts.back());
        parts.pop_back();
        auto& name = parts.back();
        const auto* item = id >= 400 ? catalog.find("@item/" + std::to_string(id-400)) : nullptr;
        name = item ? *item : named(catalog, name);
    } else if (kind == "add" || kind == "stock") {
        if (parts.size() != (kind == "stock" ? 5u : 3u)) return {};
        const int ext = number(parts.back());
        parts.pop_back();
        const int id = number(parts.back());
        parts.pop_back();
        auto& name = parts[kind == "add" ? 0 : 1];
        const auto* noun = id >= 9000 && id < 10000 ? catalog.find("@recipe/material/" + std::to_string(id))
                           : id >= 0 && id < 9000 && ext < 10000 ? catalog.find("@item/" + std::to_string(id)) : nullptr;
        name = noun ? *noun : named(catalog, name);
    } else if (kind == "item" || kind == "added" || kind == "selected") {
        if (parts.size() != (kind == "selected" ? 5u : 4u)) return {};
        const auto i = parts.size()-4;
        if (auto item = catalog.item_label(parts[i], parts[i+1], parts[i+2], number(parts[i+3])))
            parts[i] = *item;
        parts.resize(i+1);
    } else if (kind == "rate" || kind == "skill") {
        if (parts.empty()) return {};
        parts[0] = named(catalog, parts[0]);
    }

    // Expand each placeholder from the template only. A custom item name can
    // itself contain braces and must never be interpreted as another slot.
    std::string result;
    for (std::size_t i = 0; i < pattern->size(); ++i) {
        if ((*pattern)[i] != '{') { result += (*pattern)[i]; continue; }
        if (i+2 >= pattern->size() || (*pattern)[i+2] != '}') return {};
        int slot = (*pattern)[i+1]-'1';
        if (slot < 0 || static_cast<std::size_t>(slot) >= parts.size()) return {};
        result += parts[slot];
        i += 2;
    }
    if (kind == "recipe") {
        const auto colon = original.rfind(':');
        if (colon != original.npos && number(std::string_view(original).substr(colon+1)) >= 0)
            result += original.substr(colon);
    }
    return result;
}

std::optional<std::string> recipe_log(const Catalog& catalog, std::string_view identity,
                                      std::string_view source) {
    if (identity.substr(0, 8) != "@recipe/") return {};
    identity.remove_prefix(8);
    if (identity.substr(0, 4) == "add\t")
        return recipe_caption(catalog, std::string(identity) + "\t");
    if (identity.substr(0, 6) != "added\t") return {};
    if (source.substr(0, 1) == "[") {
        const auto end = source.find("] ");
        if (end == source.npos) return {};
        source.remove_prefix(end+2);
    }
    const std::string_view ja = "を選んだ。";
    if ((source.substr(0, 8) == "You add " || source.substr(0, 8) == "you add ") && source.back() == '.') {
        source.remove_prefix(8);
        source.remove_suffix(1);
    } else if (source.size() >= ja.size() && source.substr(source.size()-ja.size()) == ja) {
        source.remove_suffix(ja.size());
    } else return {};
    return recipe_caption(catalog, "added\t" + std::string(source) + "\t" + std::string(identity.substr(6)) + "\t");
}
}

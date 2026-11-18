#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace elona::localization {
class Catalog;
std::optional<std::string> recipe_caption(const Catalog& catalog, std::string_view fields);
std::optional<std::string> recipe_log(const Catalog& catalog, std::string_view identity, std::string_view source);
}

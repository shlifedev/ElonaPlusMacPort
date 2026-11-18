#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace elona::localization {
class Catalog;
std::optional<std::string> evolution_message(const Catalog&, std::string_view identity);
}

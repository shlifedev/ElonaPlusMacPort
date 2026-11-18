#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace elona::localization {
class Catalog;
// Call only at a verified random_title display boundary. Unknown names pass through.
std::optional<std::string> localized_alias(const Catalog& catalog, std::string_view source);
}

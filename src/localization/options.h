#pragma once
#include "catalog.h"

namespace elona::localization {
std::optional<std::string> option_text(const Catalog& catalog, std::string_view key, std::string_view source);
}

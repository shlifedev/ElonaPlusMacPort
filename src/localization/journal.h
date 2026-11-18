#pragma once
#include "catalog.h"

namespace elona::localization {
// Each optional display row occupies the same slot in the original two-page spread.
std::vector<std::optional<std::string>> journal_rows(const Catalog& catalog, std::string_view notes);
std::optional<std::string> save_header(const Catalog& catalog, std::string_view header);
}

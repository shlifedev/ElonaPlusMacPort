#pragma once
#include "catalog.h"

namespace elona::localization {
// The HSP menu indexes a topic by the first body line after its heading.
// Entries refer to strings owned by the catalog.
std::unordered_map<std::string, const std::string*> manual_pages(const Catalog&, std::string_view cp932);
}

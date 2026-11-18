#pragma once
#include "catalog.h"
#include <string_view>
#include <vector>

namespace elona::localization {
struct BookLine {
    int column, row, size;
    bool bold;
    std::string text;
};
// The VM paginates by 40 original lines. Each spread contains two 20-row leaves.
std::optional<std::vector<BookLine>> book_spread(const Catalog&, int id, std::string_view cp932, int spread);
}

#pragma once
#include <array>
#include <optional>
#include <string>
#include "catalog.h"

namespace elona::localization {
struct BoardPost { std::string title, body; };
// args: section, original unexpanded board.txt row, objective, ref, reward, client, map.
std::optional<BoardPost> board_post(const Catalog&, const std::array<std::string, 7>& args, int item);
}

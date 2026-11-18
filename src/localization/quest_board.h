#pragma once
#include <array>
#include <optional>
#include <string>
#include "catalog.h"

namespace elona::localization {
// Caption hooks pass the complete stored name apart from the clipped or decorated draw text.
std::optional<std::string> town_caption(const Catalog&, int scope, const std::string& fields);
struct BoardPost { std::string title, body; };
// args: section, original unexpanded board.txt row, objective, ref, reward, client, map.
std::optional<BoardPost> board_post(const Catalog&, const std::array<std::string, 7>& args, int item);
}

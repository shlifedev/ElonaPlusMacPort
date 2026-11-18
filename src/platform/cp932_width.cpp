#include "encoding.h"
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace elona::platform {
namespace {
struct WidthEntry { std::uint32_t source, mapped; };
#include "cp932_width_table.inc"

template <std::size_t N>
std::uint32_t lookup(const WidthEntry (&table)[N], std::uint32_t key) {
    const auto hit = std::lower_bound(std::begin(table), std::end(table), key,
        [](const WidthEntry& entry, std::uint32_t value) { return entry.source < value; });
    return hit != std::end(table) && hit->source == key ? hit->mapped : 0;
}

unsigned next_unit(std::string_view text, std::size_t& offset) {
    unsigned unit = static_cast<unsigned char>(text[offset++]);
    if (cp932_is_lead_byte(unit))
        unit = (unit << 8) | static_cast<unsigned char>(text[offset++]);
    return unit;
}
}

EncodingResult map_cp932_width(std::string_view original, bool fullwidth, std::string& output) {
    std::string decoded;
    const auto check = cp932_to_utf8(original, decoded);
    output.clear();
    if (!check) return check;
    const auto mapped = [&](std::uint32_t key) {
        const auto specific = fullwidth ? lookup(width_full, key) : lookup(width_half, key);
        return specific ? specific : lookup(width_common, key);
    };
    std::size_t offset = 0;
    while (offset < original.size()) {
        auto unit = next_unit(original, offset);
        std::uint32_t value = 0;
        if (offset < original.size()) {
            auto after = offset;
            const auto mark = next_unit(original, after);
            if (mark == 0xde || mark == 0xdf || mark == 0x814a || mark == 0x814b)
                value = mapped((std::uint32_t(unit) << 16) | mark);
            if (value) offset = after;
        }
        if (!value) value = mapped(unit);
        if (!value) value = unit;
        for (int shift = 24; shift > 0; shift -= 8)
            if (value >> shift) output.push_back(static_cast<char>(value >> shift));
        output.push_back(static_cast<char>(value));
    }
    return {};
}
}  // namespace elona::platform

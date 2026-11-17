#include "catalog.h"
#include <fstream>
#include <stdexcept>

namespace elona::localization {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = text[i++];
        if (c < 0x80) { if (!c) return false; continue; }
        unsigned n, value, minimum;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (i + n > text.size()) return false;
        while (n--) { c = text[i++]; if ((c & 0xc0) != 0x80) return false; value = (value << 6) | (c & 63); }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
namespace {
std::string unescape(std::string_view in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '\\') {
            if (++i == in.size()) throw std::runtime_error("unfinished escape");
            switch (in[i]) {
            case '\\': c = '\\'; break;
            case 't': c = '\t'; break;
            // draw_line receives one original line; multiline translation must
            // be laid out upstream, never smuggled through a one-line entry.
            default: throw std::runtime_error("unknown escape (only \\\\ and \\t supported)");
            }
        }
        if (static_cast<unsigned char>(c) < 32 && c != '\t') throw std::runtime_error("control character");
        out += c;
    }
    if (!valid_utf8(out)) throw std::runtime_error("invalid UTF-8");
    return out;
}
}
Catalog Catalog::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open translation catalog");
    Catalog catalog;
    std::string line;
    std::size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (line.empty() || line.front() == '#') continue;
        try {
            auto tab = line.find('\t');
            if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos)
                throw std::runtime_error("expected source<TAB>translation");
            auto source = unescape(std::string_view(line).substr(0, tab));
            auto target = unescape(std::string_view(line).substr(tab + 1));
            if (source.empty() || target.empty()) throw std::runtime_error("empty source or translation");
            if (source.size() > 16384 || target.size() > 16384) throw std::runtime_error("entry too long");
            auto [it, added] = catalog.entries_.emplace(source, target);
            if (!added && it->second != target) throw std::runtime_error("conflicting translations for one source");
        } catch (const std::exception& e) {
            throw std::runtime_error("translation catalog line " + std::to_string(number) + ": " + e.what());
        }
    }
    return catalog;
}
const std::string* Catalog::find(std::string_view source) const {
    const auto it = entries_.find(std::string(source));
    return it == entries_.end() ? nullptr : &it->second;
}
}  // namespace elona::localization

#include "book_reader.h"
#include "platform/game_files.h"

namespace elona::localization {
std::optional<std::vector<BookLine>> book_spread(const Catalog& catalog, int id, std::string_view cp932, int spread) {
    if ((id != 0 && id != 1 && id != 30 && id != 31) || cp932.size() > 65536 || spread < 0 || spread > 64)
        return {};
    // HSP notes strip the first header and store their lines with CRLF. Keep
    // meaningful blank lines and spaces, normalizing only the line delimiter.
    std::string body;
    body.reserve(cp932.size());
    for (std::size_t i = 0; i < cp932.size(); ++i) {
        if (cp932[i] == '\r' && i + 1 < cp932.size() && cp932[i + 1] == '\n') continue;
        body += cp932[i];
    }
    if (!body.empty() && body.back() == '\n') body.pop_back();
    const std::string key = "@book/" + std::to_string(id) + "/" +
                            platform::sha256_hex(body.data(), body.size());
    const auto* title = catalog.find(key + "/title");
    if (!title) return {};
    std::vector<BookLine> lines;
    for (int column = 0; column < 2; ++column) {
        const int leaf = spread * 2 + column + 1;
        const auto* text = catalog.find(key + "/page/" + std::to_string(leaf));
        if (!text) {
            if (column == 0) return {};
            break;  // a final left-hand page, e.g. the diary
        }
        int row = 0;
        if (leaf == 1) {
            lines.push_back({column, 0, 12, true, *title});
            if (const auto* author = catalog.find(key + "/author"))
                lines.push_back({column, 1, 10, false, *author});
            row = 3;
        }
        std::size_t begin = 0;
        do {
            if (row >= 20) return {};
            const auto end = text->find('\t', begin);
            auto line = text->substr(begin, end == text->npos ? end : end - begin);
            if (!line.empty()) lines.push_back({column, row, 12, false, std::move(line)});
            ++row;
            if (end == text->npos) break;
            begin = end + 1;
        } while (begin < text->size());
    }
    return lines;
}
}

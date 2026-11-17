#include "legacy_logical_path.h"
#include <cstring>
#include <string>

namespace {
bool cp932_lead(unsigned char byte) {
    return (byte >= 0x81 && byte <= 0x9f) || (byte >= 0xe0 && byte <= 0xfc);
}
}

void elona_legacy_getpath(const char* input, char* output, int flags) {
    std::string path(input);
    std::size_t filename = path.size() >= 2 && path[1] == ':' ? 2 : 0;
    std::size_t extension = std::string::npos;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto byte = static_cast<unsigned char>(path[i]);
        if (cp932_lead(byte) && i + 1 < path.size()) {
            ++i;
            continue;
        }
        if ((flags & 16) && byte >= 'A' && byte <= 'Z') path[i] += 'a' - 'A';
        if (byte == '/' || byte == '\\') {
            filename = i + 1;
            extension = std::string::npos;
        } else if (byte == '.') {
            extension = i;
        }
    }
    const std::string suffix = extension == std::string::npos ? "" : path.substr(extension);
    std::string result = (flags & 8) ? path.substr(filename) :
                         (flags & 32) ? path.substr(0, filename) : path;
    switch (flags & 7) {
    case 1:
        // A directory selection has no filename extension to remove.
        if (!(flags & 32) || (flags & 8)) result.resize(result.size() - suffix.size());
        break;
    case 2: result = suffix; break;
    default: break;
    }
    std::memcpy(output, result.c_str(), result.size() + 1);
}

#include "ui_text_codec.h"

#ifdef ELONA_HAVE_PLATFORM_ENCODING
#include "platform/encoding.h"
#elif defined(_WIN32)
#include <windows.h>
#else
#include <iconv.h>
#include <mutex>
#endif

namespace elona::gfx {
namespace {
void lossy(std::string_view in, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) out.push_back(char(c));
        else {
            bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
            if (lead && i + 1 < in.size()) ++i;
            out += "\xEF\xBF\xBD";
        }
    }
}
}  // namespace

#ifdef ELONA_HAVE_PLATFORM_ENCODING
bool ui_cp932_to_utf8(std::string_view cp932, std::string& out) {
    if (elona::platform::cp932_to_utf8(cp932, out)) return true;
    lossy(cp932, out);
    return false;
}
const char* ui_text_codec_backend() { return "elona_platform"; }
#elif defined(_WIN32)
bool ui_cp932_to_utf8(std::string_view cp932, std::string& out) {
    out.clear();
    if (cp932.empty()) return true;
    int n = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, cp932.data(), int(cp932.size()), nullptr, 0);
    if (n <= 0) { lossy(cp932, out); return false; }
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, cp932.data(), int(cp932.size()), w.data(), n);
    int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    out.resize(std::size_t(m));
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), m, nullptr, nullptr);
    return true;
}
const char* ui_text_codec_backend() { return "temporary-win32"; }
#else
bool ui_cp932_to_utf8(std::string_view cp932, std::string& out) {
    static std::mutex mutex;
    static iconv_t cd = iconv_open("UTF-8", "CP932");
    out.clear();
    if (cp932.empty()) return true;
    if (cd == iconv_t(-1)) { lossy(cp932, out); return false; }
    std::lock_guard<std::mutex> lock(mutex);
    iconv(cd, nullptr, nullptr, nullptr, nullptr);
    std::string buffer(cp932.size() * 4 + 4, '\0');
    char* in = const_cast<char*>(cp932.data());
    std::size_t inleft = cp932.size();
    char* o = buffer.data();
    std::size_t outleft = buffer.size();
    if (iconv(cd, &in, &inleft, &o, &outleft) == std::size_t(-1)) { lossy(cp932, out); return false; }
    out.assign(buffer.data(), buffer.size() - outleft);
    return true;
}
const char* ui_text_codec_backend() { return "temporary-iconv"; }
#endif

}  // namespace elona::gfx

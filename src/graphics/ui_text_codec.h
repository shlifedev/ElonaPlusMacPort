// UI-boundary CP932 -> UTF-8 conversion used by font rendering and window
// titles only. When the shared platform encoding module (src/platform/
// encoding.h, target elona_platform) is linked, ELONA_HAVE_PLATFORM_ENCODING is
// defined and this forwards to it. Otherwise a small temporary backend is used
// (iconv on POSIX, MultiByteToWideChar on Windows). Game logic never calls this.
#pragma once
#include <string>
#include <string_view>

namespace elona::gfx {

// Returns false on invalid/unmappable input; `out` then holds a lossy rendering
// with U+FFFD so the caller can still diagnose what was drawn.
bool ui_cp932_to_utf8(std::string_view cp932, std::string& out);
const char* ui_text_codec_backend();

}  // namespace elona::gfx

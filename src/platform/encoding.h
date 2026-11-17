#pragma once
// CP932 <-> UTF-8 conversion for the HSP/ElonaPlus native host.
//
// The game keeps CP932 bytes internally; conversion only happens at UI, font,
// IME and native filesystem boundaries. Conversion is strict: an invalid or
// unmappable sequence fails with the byte offset instead of inserting '?'.
//
// Mapping contract: identical to Windows MultiByteToWideChar(932,
// MB_ERR_INVALID_CHARS) / WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS) for every
// single byte, every two-byte pair and every BMP code point (verified against a
// table dumped on Windows 11, see docs/PLATFORM_IMPLEMENTATION.md).
//   Windows : the Win32 API itself.
//   macOS   : iconv("CP932") plus the 5 Windows-only single-byte mappings that
//             libiconv lacks (0x80<->U+0080, U+F8F0..U+F8F3 -> A0/FD/FE/FF encode).
//             CoreFoundation's kCFStringEncodingDOSJapanese differs in 385 encode
//             entries (NEC-selected rows, best fit) and is NOT used for text.
#include <cstddef>
#include <string>
#include <string_view>

namespace elona::platform {

enum class EncodingError {
    ok = 0,
    invalid_sequence,   // malformed input (truncated lead byte, bad UTF-8, ...)
    unmappable,         // valid input with no exact mapping in the target encoding
    backend_failure,    // iconv_open / Win32 failure
};

struct EncodingResult {
    EncodingError error = EncodingError::ok;
    std::size_t offset = 0;   // input byte offset of the first failing sequence
    explicit operator bool() const { return error == EncodingError::ok; }
};

const char* to_string(EncodingError error);
const char* encoding_backend_name();

// HSP/Win32 CP932 lead byte (0x81-0x9F, 0xE0-0xFC). Trail bytes may be 0x40-0xFC,
// including 0x5C ('\\') and 0x7C ('|'), so byte scanners must skip them.
constexpr bool cp932_is_lead_byte(unsigned char byte) {
    return (byte >= 0x81 && byte <= 0x9f) || (byte >= 0xe0 && byte <= 0xfc);
}

// Strict conversions. On failure `out` is left empty.
EncodingResult cp932_to_utf8(std::string_view cp932, std::string& out);
EncodingResult utf8_to_cp932(std::string_view utf8, std::string& out);

// Convenience wrappers: throw std::runtime_error with offset on failure.
std::string cp932_to_utf8_or_throw(std::string_view cp932);
std::string utf8_to_cp932_or_throw(std::string_view utf8);

// Unicode NFC (macOS: CoreFoundation, Windows: NormalizeString). Used for native
// directory listings: macOS filenames may come back decomposed (NFD), and NFD
// kana (e.g. U+304B U+3099) has no CP932 mapping while NFC (U+304C) does.
EncodingResult utf8_to_nfc(std::string_view utf8, std::string& out);

#ifdef _WIN32
EncodingResult utf8_to_wide(std::string_view utf8, std::wstring& out);
EncodingResult wide_to_utf8(std::wstring_view wide, std::string& out);
#endif

}  // namespace elona::platform

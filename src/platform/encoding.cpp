#include "encoding.h"

#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace elona::platform {

const char* to_string(EncodingError error) {
    switch (error) {
    case EncodingError::ok: return "ok";
    case EncodingError::invalid_sequence: return "invalid_sequence";
    case EncodingError::unmappable: return "unmappable";
    case EncodingError::backend_failure: return "backend_failure";
    }
    return "unknown";
}

namespace {

EncodingResult fail(EncodingError error, std::size_t offset, std::string& out) {
    out.clear();
    return {error, offset};
}

// Strict UTF-8 decoder (no overlongs, no surrogates, max U+10FFFF).
// Returns code point length in bytes, or 0 when invalid.
std::size_t utf8_next(std::string_view s, std::size_t i, char32_t& cp) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) { cp = b0; return 1; }
    std::size_t len;
    char32_t min;
    if ((b0 & 0xe0) == 0xc0) { len = 2; cp = b0 & 0x1f; min = 0x80; }
    else if ((b0 & 0xf0) == 0xe0) { len = 3; cp = b0 & 0x0f; min = 0x800; }
    else if ((b0 & 0xf8) == 0xf0) { len = 4; cp = b0 & 0x07; min = 0x10000; }
    else return 0;
    if (i + len > s.size()) return 0;
    for (std::size_t k = 1; k < len; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (b & 0x3f);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    return len;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

#ifndef _WIN32
// ---- iconv backend (macOS libiconv "CP932"; Linux glibc is NOT verified) ----
class Iconv {
public:
    Iconv(const char* to, const char* from) : cd_(iconv_open(to, from)) {}
    ~Iconv() { if (ok()) iconv_close(cd_); }
    Iconv(const Iconv&) = delete;
    Iconv& operator=(const Iconv&) = delete;
    bool ok() const { return cd_ != reinterpret_cast<iconv_t>(-1); }
    // Converts the whole segment; on failure returns errno and the failing
    // input offset relative to the segment.
    int run(std::string_view in, std::string& out, std::size_t& fail_offset) {
        iconv(cd_, nullptr, nullptr, nullptr, nullptr);
        char* inptr = const_cast<char*>(in.data());
        std::size_t inleft = in.size();
        char buffer[4096];
        while (true) {
            char* outptr = buffer;
            std::size_t outleft = sizeof buffer;
            const std::size_t r = iconv(cd_, inleft ? &inptr : nullptr, inleft ? &inleft : nullptr, &outptr, &outleft);
            out.append(buffer, static_cast<std::size_t>(outptr - buffer));
            if (r == static_cast<std::size_t>(-1)) {
                if (errno == E2BIG) continue;
                fail_offset = static_cast<std::size_t>(inptr - in.data());
                return errno == 0 ? EILSEQ : errno;
            }
            if (inleft == 0) {
                // flush shift state (none for CP932, but keep iconv contract)
                outptr = buffer;
                outleft = sizeof buffer;
                if (iconv(cd_, nullptr, nullptr, &outptr, &outleft) == static_cast<std::size_t>(-1)) {
                    fail_offset = in.size();
                    return errno;
                }
                out.append(buffer, static_cast<std::size_t>(outptr - buffer));
                return 0;
            }
        }
    }
private:
    iconv_t cd_;
};

// Windows-only single-byte mappings missing from libiconv's CP932 (verified by
// the full-table comparison): decode 0x80 -> U+0080; encode U+0080 -> 0x80 and
// U+F8F0..U+F8F3 -> A0/FD/FE/FF (Windows decodes those bytes as invalid).
int windows_extra_encode(char32_t cp) {
    switch (cp) {
    case 0x0080: return 0x80;
    case 0xf8f0: return 0xa0;
    case 0xf8f1: return 0xfd;
    case 0xf8f2: return 0xfe;
    case 0xf8f3: return 0xff;
    default: return -1;
    }
}
#endif

}  // namespace

const char* encoding_backend_name() {
#ifdef _WIN32
    return "win32-codepage-932";
#elif defined(__APPLE__)
    return "macos-iconv-cp932+windows-single-byte-fixups";
#else
    return "iconv-cp932-unverified";
#endif
}

#ifdef _WIN32
EncodingResult utf8_to_wide(std::string_view utf8, std::wstring& out) {
    out.clear();
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        std::size_t i = 0;
        char32_t cp;
        while (i < utf8.size()) { const auto l = utf8_next(utf8, i, cp); if (!l) break; i += l; }
        return {EncodingError::invalid_sequence, i};
    }
    out.resize(static_cast<std::size_t>(n));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return {};
}

EncodingResult wide_to_utf8(std::wstring_view wide, std::string& out) {
    out.clear();
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {EncodingError::invalid_sequence, 0};
    out.resize(static_cast<std::size_t>(n));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return {};
}

namespace {
bool decode_one_cp932(const char* p, int len, wchar_t* dst, int& produced) {
    produced = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, p, len, dst, 4);
    return produced > 0;
}
}  // namespace

EncodingResult cp932_to_utf8(std::string_view cp932, std::string& out) {
    out.clear();
    std::wstring wide;
    wide.reserve(cp932.size());
    // Character-by-character (DBCS aware) to report exact failure offsets.
    for (std::size_t i = 0; i < cp932.size();) {
        const auto b = static_cast<unsigned char>(cp932[i]);
        const int len = cp932_is_lead_byte(b) ? 2 : 1;
        if (i + len > cp932.size()) return fail(EncodingError::invalid_sequence, i, out);
        wchar_t buf[4];
        int produced = 0;
        if (!decode_one_cp932(cp932.data() + i, len, buf, produced) || produced != 1)
            return fail(EncodingError::invalid_sequence, i, out);
        wide.push_back(buf[0]);
        i += static_cast<std::size_t>(len);
    }
    if (!wide_to_utf8(wide, out)) return fail(EncodingError::backend_failure, 0, out);
    return {};
}

EncodingResult utf8_to_cp932(std::string_view utf8, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < utf8.size();) {
        char32_t cp;
        const auto l = utf8_next(utf8, i, cp);
        if (!l) return fail(EncodingError::invalid_sequence, i, out);
        if (cp > 0xffff) return fail(EncodingError::unmappable, i, out);
        const wchar_t w = static_cast<wchar_t>(cp);
        char buf[8];
        BOOL used_default = FALSE;
        const int n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &w, 1, buf, sizeof buf, nullptr, &used_default);
        if (n <= 0) return fail(EncodingError::backend_failure, i, out);
        if (used_default) return fail(EncodingError::unmappable, i, out);
        out.append(buf, static_cast<std::size_t>(n));
        i += l;
    }
    return {};
}

EncodingResult utf8_to_nfc(std::string_view utf8, std::string& out) {
    std::wstring wide;
    if (auto r = utf8_to_wide(utf8, wide); !r) { out.clear(); return r; }
    if (wide.empty()) { out.clear(); return {}; }
    int n = NormalizeString(NormalizationC, wide.data(), static_cast<int>(wide.size()), nullptr, 0);
    if (n <= 0) return fail(EncodingError::backend_failure, 0, out);
    std::wstring normalized(static_cast<std::size_t>(n), L'\0');
    n = NormalizeString(NormalizationC, wide.data(), static_cast<int>(wide.size()), normalized.data(), n);
    if (n <= 0) return fail(EncodingError::backend_failure, 0, out);
    normalized.resize(static_cast<std::size_t>(n));
    return wide_to_utf8(normalized, out);
}

#else  // iconv backends

EncodingResult cp932_to_utf8(std::string_view cp932, std::string& out) {
    out.clear();
    Iconv cd("UTF-8", "CP932");
    if (!cd.ok()) return fail(EncodingError::backend_failure, 0, out);
    std::size_t segment = 0;
    auto flush = [&](std::size_t end) -> EncodingResult {
        if (end > segment) {
            std::size_t bad = 0;
            if (cd.run(cp932.substr(segment, end - segment), out, bad) != 0)
                return {EncodingError::invalid_sequence, segment + bad};
        }
        return {};
    };
    for (std::size_t i = 0; i < cp932.size();) {
        const auto b = static_cast<unsigned char>(cp932[i]);
        if (cp932_is_lead_byte(b)) {
            if (i + 1 >= cp932.size()) {
                if (auto r = flush(i); !r) return fail(r.error, r.offset, out);
                return fail(EncodingError::invalid_sequence, i, out);
            }
            i += 2;
            continue;
        }
        if (b == 0x80) {
            if (auto r = flush(i); !r) return fail(r.error, r.offset, out);
            append_utf8(out, 0x80);
            segment = ++i;
            continue;
        }
        ++i;
    }
    if (auto r = flush(cp932.size()); !r) return fail(r.error, r.offset, out);
    return {};
}

EncodingResult utf8_to_cp932(std::string_view utf8, std::string& out) {
    out.clear();
    Iconv cd("CP932", "UTF-8");
    if (!cd.ok()) return fail(EncodingError::backend_failure, 0, out);
    std::size_t segment = 0;
    auto flush = [&](std::size_t end) -> EncodingResult {
        if (end > segment) {
            std::size_t bad = 0;
            if (cd.run(utf8.substr(segment, end - segment), out, bad) != 0)
                return {EncodingError::unmappable, segment + bad};   // input already validated
        }
        return {};
    };
    for (std::size_t i = 0; i < utf8.size();) {
        char32_t cp;
        const auto l = utf8_next(utf8, i, cp);
        if (!l) return fail(EncodingError::invalid_sequence, i, out);
        if (cp > 0xffff) {
            if (auto r = flush(i); !r) return fail(r.error, r.offset, out);
            return fail(EncodingError::unmappable, i, out);
        }
        const int extra = windows_extra_encode(cp);
        if (extra >= 0) {
            if (auto r = flush(i); !r) return fail(r.error, r.offset, out);
            out.push_back(static_cast<char>(extra));
            segment = i + l;
        }
        i += l;
    }
    if (auto r = flush(utf8.size()); !r) return fail(r.error, r.offset, out);
    return {};
}

EncodingResult utf8_to_nfc(std::string_view utf8, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < utf8.size();) {
        char32_t cp;
        const auto l = utf8_next(utf8, i, cp);
        if (!l) return {EncodingError::invalid_sequence, i};
        i += l;
    }
#ifdef __APPLE__
    CFStringRef source = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(utf8.data()),
                                                 static_cast<CFIndex>(utf8.size()), kCFStringEncodingUTF8, false);
    if (!source) return {EncodingError::backend_failure, 0};
    CFMutableStringRef mutable_string = CFStringCreateMutableCopy(nullptr, 0, source);
    CFRelease(source);
    if (!mutable_string) return {EncodingError::backend_failure, 0};
    CFStringNormalize(mutable_string, kCFStringNormalizationFormC);
    const CFIndex length = CFStringGetLength(mutable_string);
    CFIndex bytes = 0;
    CFStringGetBytes(mutable_string, CFRangeMake(0, length), kCFStringEncodingUTF8, 0, false, nullptr, 0, &bytes);
    out.resize(static_cast<std::size_t>(bytes));
    CFStringGetBytes(mutable_string, CFRangeMake(0, length), kCFStringEncodingUTF8, 0, false,
                     reinterpret_cast<UInt8*>(out.data()), bytes, &bytes);
    CFRelease(mutable_string);
#else
    out.assign(utf8);   // no normalizer on this platform (not a supported target)
#endif
    return {};
}
#endif

std::string cp932_to_utf8_or_throw(std::string_view cp932) {
    std::string out;
    if (auto r = cp932_to_utf8(cp932, out); !r)
        throw std::runtime_error(std::string("CP932->UTF-8 ") + to_string(r.error) + " at byte " + std::to_string(r.offset));
    return out;
}

std::string utf8_to_cp932_or_throw(std::string_view utf8) {
    std::string out;
    if (auto r = utf8_to_cp932(utf8, out); !r)
        throw std::runtime_error(std::string("UTF-8->CP932 ") + to_string(r.error) + " at byte " + std::to_string(r.offset));
    return out;
}

}  // namespace elona::platform

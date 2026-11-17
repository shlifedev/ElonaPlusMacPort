// CP932 <-> UTF-8 conversion tests. Optional argv[1]: directory holding the
// Windows-dumped cp932-decode.tsv / cp932-encode.tsv for a full-table check.
#include "platform/encoding.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace elona::platform;

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string bytes(std::initializer_list<int> list) {
    std::string s;
    for (int b : list) s.push_back(static_cast<char>(b));
    return s;
}

static void round_trip(const std::string& cp932, const std::string& utf8) {
    std::string out;
    auto r = cp932_to_utf8(cp932, out);
    CHECK(r);
    CHECK(out == utf8);
    r = utf8_to_cp932(utf8, out);
    CHECK(r);
    CHECK(out == cp932);
}

static void vectors() {
    round_trip("", "");
    round_trip(bytes({0x82, 0xa0, 0x41}), "\xe3\x81\x82" "A");                 // あA
    round_trip(bytes({0x95, 0x5c}), "\xe8\xa1\xa8");                          // 表 (trail 0x5C)
    round_trip(bytes({0x83, 0x5c}), "\xe3\x82\xbd");                          // ソ (trail 0x5C)
    round_trip(bytes({0x5c, 0x7e}), "\\~");                                   // ASCII stays ASCII (Windows)
    round_trip(bytes({0x81, 0x5f}), "\xef\xbc\xbc");                          // U+FF3C
    round_trip(bytes({0x81, 0x60}), "\xef\xbd\x9e");                          // U+FF5E (not U+301C)
    round_trip(bytes({0x81, 0x7c}), "\xef\xbc\x8d");                          // U+FF0D
    round_trip(bytes({0xb1, 0xdf}), "\xef\xbd\xb1\xef\xbe\x9f");              // half-width ｱﾟ
    round_trip(bytes({0xf0, 0x40}), "\xee\x80\x80");                          // user-defined -> U+E000
    round_trip(bytes({0x80}), "\xc2\x80");                                     // Windows-only 0x80 <-> U+0080
    round_trip(bytes({0xfa, 0x40}), "\xe2\x85\xb0");                          // IBM ext ⅰ (U+2170)
    std::string out;
    // Duplicates decode but encode to Microsoft's preferred code.
    CHECK(cp932_to_utf8(bytes({0x87, 0x90}), out) && out == "\xe2\x89\x92");  // NEC ≒
    CHECK(utf8_to_cp932("\xe2\x89\x92", out) && out == bytes({0x81, 0xe0}));
    CHECK(cp932_to_utf8(bytes({0xee, 0xef}), out) && out == "\xe2\x85\xb0");  // NEC-selected IBM ⅰ
    CHECK(utf8_to_cp932("\xef\xa3\xb0", out) && out == bytes({0xa0}));        // U+F8F0 -> A0 (Windows)
    CHECK(utf8_to_cp932("\xef\xa3\xb3", out) && out == bytes({0xff}));        // U+F8F3 -> FF
    // Invalid / unmappable with exact offsets, no '?' substitution.
    auto r = cp932_to_utf8(bytes({0x41, 0xa0}), out);
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 1 && out.empty());
    r = cp932_to_utf8(bytes({0x41, 0x42, 0x82}), out);                          // truncated lead
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 2);
    r = cp932_to_utf8(bytes({0x82, 0xa0, 0x81, 0x20}), out);                     // bad trail
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 2);
    r = cp932_to_utf8(bytes({0x80, 0xfd}), out);
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 1);
    r = utf8_to_cp932("A\xc2\xa2", out);                                         // U+00A2: no exact mapping
    CHECK(r.error == EncodingError::unmappable && r.offset == 1);
    r = utf8_to_cp932("AB\xf0\x9f\x98\x80", out);                                // non-BMP
    CHECK(r.error == EncodingError::unmappable && r.offset == 2);
    r = utf8_to_cp932("\xc0\x80", out);                                          // overlong NUL
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 0);
    r = utf8_to_cp932("x\xed\xa0\x80", out);                                     // surrogate
    CHECK(r.error == EncodingError::invalid_sequence && r.offset == 1);
    r = utf8_to_cp932("\xe3\x81", out);                                          // truncated
    CHECK(r.error == EncodingError::invalid_sequence);
    r = utf8_to_cp932("\xe2\x80\x94", out);                                      // U+2014: Windows DEFAULT
    CHECK(r.error == EncodingError::unmappable);
    // NUL bytes are data, not terminators.
    round_trip(std::string("a\0b", 3), std::string("a\0b", 3));
    // NFC: decomposed が (か + U+3099) -> composed, then CP932 82 AA.
    std::string nfc;
    CHECK(utf8_to_nfc("\xe3\x81\x8b\xe3\x82\x99", nfc));
#ifndef __linux__
    CHECK(nfc == "\xe3\x81\x8c");
    CHECK(utf8_to_cp932(nfc, out) && out == bytes({0x82, 0xaa}));
#endif
    CHECK(!utf8_to_cp932("\xe3\x81\x8b\xe3\x82\x99", out));                       // NFD itself not encodable
    CHECK(cp932_is_lead_byte(0x81) && cp932_is_lead_byte(0x9f) && cp932_is_lead_byte(0xe0) && cp932_is_lead_byte(0xfc));
    CHECK(!cp932_is_lead_byte(0x80) && !cp932_is_lead_byte(0xa0) && !cp932_is_lead_byte(0xdf) && !cp932_is_lead_byte(0xfd));
    bool threw = false;
    try { cp932_to_utf8_or_throw(bytes({0x82})); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static std::string hex_of(const std::string& s) {
    std::string h;
    char b[4];
    for (unsigned char c : s) { std::snprintf(b, sizeof b, "%02X", c); h += b; }
    return h;
}

static std::string utf8_of(unsigned cp) {
    std::string s;
    if (cp < 0x80) s.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { s.push_back(static_cast<char>(0xc0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
    else { s.push_back(static_cast<char>(0xe0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); s.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
    return s;
}

// Compares against the Windows table: every 1-byte value, every lead(0x80-FF) x
// trail(0x00-FF) pair, and every BMP code point (excluding surrogates).
static void full_table(const std::string& dir) {
    std::ifstream decode(dir + "/cp932-decode.tsv"), encode(dir + "/cp932-encode.tsv");
    if (!decode || !encode) { std::fprintf(stderr, "FAIL cannot open tables in %s\n", dir.c_str()); ++failures; return; }
    std::string line;
    int checked = 0, mismatched = 0;
    while (std::getline(decode, line)) {
        const auto tab = line.find('\t');
        const std::string key = line.substr(0, tab), value = line.substr(tab + 1);
        std::string in;
        for (std::size_t i = 0; i < key.size(); i += 2) in.push_back(static_cast<char>(std::strtol(key.substr(i, 2).c_str(), nullptr, 16)));
        std::string expect;
        const bool win_ok = value.rfind("ERR", 0) != 0;
        if (win_ok) {
            for (std::size_t i = 0; i < value.size(); i += 5) expect += utf8_of(static_cast<unsigned>(std::strtol(value.substr(i, 4).c_str(), nullptr, 16)));
        }
        std::string out;
        const bool ok = static_cast<bool>(cp932_to_utf8(in, out));
        ++checked;
        if (ok != win_ok || (ok && out != expect)) {
            if (++mismatched <= 10) std::fprintf(stderr, "decode mismatch %s win=%s ours=%s\n", key.c_str(), value.c_str(), ok ? hex_of(out).c_str() : "ERR");
        }
    }
    while (std::getline(encode, line)) {
        const auto tab = line.find('\t');
        const unsigned cp = static_cast<unsigned>(std::strtol(line.substr(0, tab).c_str(), nullptr, 16));
        const std::string value = line.substr(tab + 1);
        const bool win_ok = value.rfind("ERR", 0) != 0 && value != "DEFAULT";
        std::string out;
        const bool ok = static_cast<bool>(utf8_to_cp932(utf8_of(cp), out));
        ++checked;
        if (ok != win_ok || (ok && hex_of(out) != value)) {
            if (++mismatched <= 10) std::fprintf(stderr, "encode mismatch U+%04X win=%s ours=%s\n", cp, value.c_str(), ok ? hex_of(out).c_str() : "X");
        }
    }
    std::printf("full-table: %d entries checked, %d mismatches (backend %s)\n", checked, mismatched, encoding_backend_name());
    CHECK(checked == 33024 + 63488);
    CHECK(mismatched == 0);
}

int main(int argc, char** argv) {
    vectors();
    if (argc > 1 && argv[1][0]) full_table(argv[1]);
    else std::printf("full-table: skipped (no table directory given)\n");
    std::printf("%s: %d/%d checks failed, backend %s\n", failures ? "FAILED" : "OK", failures, checks, encoding_backend_name());
    return failures ? 1 : 0;
}

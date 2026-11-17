// hspda replacement tests. Reference files (when present) are recorded from
// the bundled 2.335 hspda.dll and from Windows CompareStringA.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "plugins/hspda.h"

namespace fs = std::filesystem;
using namespace elona::plugins;

namespace {
int* g_checks_ptr = nullptr;
int* g_failures_ptr = nullptr;

void check(bool ok, const char* what, int line) {
    ++*g_checks_ptr;
    if (!ok) {
        ++*g_failures_ptr;
        std::fprintf(stderr, "hspda_tests.cpp:%d: CHECK failed: %s\n", line, what);
    }
}
#define HCHECK(cond) check((cond), #cond, __LINE__)

[[maybe_unused]] std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 2 < s.size() && s[i + 1] == 'x') {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

void test_sortval() {
    std::vector<std::int32_t> v = {5, 3, 9, 1, 7};
    PluginResult r = hspda_sortval(HspVarType::int32, v.data(), v.size() * 4, 5, 1);
    HCHECK(r.is_ok() && r.stat == 0 && r.raw_return == 0);
    HCHECK((v == std::vector<std::int32_t>{9, 7, 5, 3, 1}));
    r = hspda_sortval(HspVarType::int32, v.data(), v.size() * 4, 5, 0);
    HCHECK((v == std::vector<std::int32_t>{1, 3, 5, 7, 9}));
    // Only len[1] elements are touched.
    std::vector<std::int32_t> w = {3, 2, 1, 0};
    hspda_sortval(HspVarType::int32, w.data(), w.size() * 4, 3, 0);
    HCHECK((w == std::vector<std::int32_t>{1, 2, 3, 0}));
    // Errors.
    r = hspda_sortval(HspVarType::str, w.data(), 16, 4, 0);
    HCHECK(r.is_ok() && r.stat == 1 && r.raw_return == -1);
    r = hspda_sortval(HspVarType::int32, w.data(), 16, 0, 0);
    HCHECK(r.is_ok() && r.stat == 1);
    HCHECK(hspda_sortval(HspVarType::dbl, w.data(), 16, 2, 0).outcome == PluginOutcome::unsupported);
    HCHECK(hspda_sortval(HspVarType::int32, w.data(), 16, 5, 0).outcome == PluginOutcome::unsafe_input);
    // Many values (duplicates, negatives) are sorted for both orders.
    std::vector<std::int32_t> big;
    std::uint32_t x = 1;
    for (int i = 0; i < 2000; ++i) {
        x = x * 1103515245u + 12345u;
        big.push_back(static_cast<std::int32_t>(x >> 8) % 1000 - 500);
    }
    for (int order : {0, 1}) {
        auto copy = big;
        std::vector<std::int32_t> idx;
        HCHECK(hspda_sortval(HspVarType::int32, copy.data(), copy.size() * 4, 2000, order, &idx).is_ok());
        auto expected = big;
        if (order == 0) std::sort(expected.begin(), expected.end());
        else std::sort(expected.begin(), expected.end(), std::greater<>());
        HCHECK(copy == expected);
        bool idx_ok = true;
        for (std::size_t i = 0; i < copy.size(); ++i) idx_ok &= big[static_cast<std::size_t>(idx[i])] == copy[i];
        HCHECK(idx_ok);
    }
}

void test_lstrcmp_model_basics() {
    HCHECK(*hspda_lstrcmpa_ascii("abc", "abc") == 0);
    HCHECK(*hspda_lstrcmpa_ascii("abc", "abd") < 0);
    HCHECK(*hspda_lstrcmpa_ascii("a", "B") < 0);
    HCHECK(*hspda_lstrcmpa_ascii("a", "A") < 0);
    HCHECK(*hspda_lstrcmpa_ascii("ab", "a") > 0);
    HCHECK(!hspda_lstrcmpa_ascii("\x82\xa0", "a").has_value());
}

void test_sortnote() {
    std::string out;
    // dirlist-style CRLF list: the trailing empty entry gains another CRLF.
    PluginResult r = hspda_sortnote("b.npc\r\na.npc\r\nc.npc\r\n", 256, 1, out);
    HCHECK(r.is_ok() && r.stat == 0);
    HCHECK(out == "c.npc\r\nb.npc\r\na.npc\r\n\r\n");
    r = hspda_sortnote("b\r\na\r\nc", 256, 0, out);
    HCHECK(r.is_ok());
    HCHECK(out == "a\r\nb\r\nc\r\n");
    r = hspda_sortnote("", 4, 0, out);
    HCHECK(r.is_ok() && r.stat == 0);
    HCHECK(out == "\r\n");
    // LF LF is unsafe in the original.
    HCHECK(hspda_sortnote("a\n\nb", 256, 0, out).outcome == PluginOutcome::unsafe_input);
    // Lone LF / CR separators.
    r = hspda_sortnote("b\na\rc", 256, 0, out);
    HCHECK(r.is_ok() && out == "a\r\nb\r\nc\r\n");
    // Overflowing the original temp buffer.
    HCHECK(hspda_sortnote("b\na", 6, 0, out).outcome == PluginOutcome::unsafe_input);
    // Non-ASCII ordering is not reproduced.
    HCHECK(hspda_sortnote("\x82\xa0\r\nb", 256, 0, out).outcome == PluginOutcome::unsupported);
}

void test_xnote() {
    std::vector<std::uint8_t> buf(64, 0xcc);
    buf[0] = 0;
    HspdaXNote x;
    PluginResult r = x.select(HspVarType::str, buf.data(), buf.size(), 0);
    HCHECK(r.is_ok() && r.stat == 0);
    r = x.add(buf.data(), buf.size(), "sword");
    HCHECK(r.is_ok() && r.stat == 0 && r.raw_return == 0);
    r = x.add(buf.data(), buf.size(), "shield");
    HCHECK(r.is_ok() && r.stat == 1 && r.raw_return == -1);
    r = x.add(buf.data(), buf.size(), "sword");
    HCHECK(r.is_ok() && r.stat == 0);
    HCHECK(std::strcmp(reinterpret_cast<char*>(buf.data()), "sword\r\nshield\r\n") == 0);
    HCHECK(x.counts()[0] == 2 && x.counts()[1] == 1);
    // Overflow and stale buffer are refused.
    HCHECK(x.add(buf.data(), buf.size(), std::string(60, 'x')).outcome == PluginOutcome::unsafe_input);
    std::vector<std::uint8_t> other(64, 0);
    HCHECK(x.add(other.data(), other.size(), "a").outcome == PluginOutcome::unsafe_input);
    // LF alone is line content; a line without CRLF gets the item appended.
    std::vector<std::uint8_t> b2(64, 0);
    std::memcpy(b2.data(), "a\nb", 4);
    HspdaXNote y;
    y.select(HspVarType::str, b2.data(), b2.size(), 4);
    r = y.add(b2.data(), b2.size(), "c");
    HCHECK(r.is_ok() && r.stat == 0);
    HCHECK(std::strcmp(reinterpret_cast<char*>(b2.data()), "a\nbc\r\n") == 0);
    // Item table bound.
    HspdaXNote z;
    std::vector<std::uint8_t> b3(64, 0);
    z.select(HspVarType::str, b3.data(), b3.size(), 1);
    HCHECK(z.add(b3.data(), b3.size(), "a").is_ok());
    HCHECK(z.add(b3.data(), b3.size(), "b").outcome == PluginOutcome::unsafe_input);
    // Non-string selection.
    HspdaXNote q;
    r = q.select(HspVarType::int32, b3.data(), b3.size(), 0);
    HCHECK(r.is_ok() && r.stat == 1);
    HCHECK(q.add(b3.data(), b3.size(), "a").outcome == PluginOutcome::unsafe_input);
}

// lstrcmpa_ascii.txt: "<sign> <hex a> <hex b>" (sign of CompareStringA - 2).
void test_lstrcmp_reference(const fs::path& ref_dir) {
    const fs::path path = ref_dir / "lstrcmpa_ascii.txt";
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "SKIP lstrcmpA reference: %s missing\n", path.string().c_str());
        return;
    }
    auto hex = [](const std::string& h) {
        std::string s;
        if (h == "-") return s;
        for (std::size_t i = 0; i + 1 < h.size(); i += 2) s.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
        return s;
    };
    std::string line;
    int rows = 0, bad = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        int expected = 0;
        std::string ha, hb;
        row >> expected >> ha >> hb;
        const std::string a = hex(ha), b = hex(hb);
        const auto got = hspda_lstrcmpa_ascii(a, b);
        ++rows;
        if (!got || *got != expected) {
            if (++bad <= 20) std::fprintf(stderr, "lstrcmpA model mismatch: '%s' vs '%s' expected %d got %d\n", a.c_str(), b.c_str(), expected, got ? *got : 99);
        }
    }
    check(rows > 0, "lstrcmpA reference rows", __LINE__);
    check(bad == 0, "lstrcmpA model matches CompareStringA", __LINE__);
    std::printf("lstrcmpA reference rows: %d, mismatches: %d\n", rows, bad);
}

}  // namespace

void run_hspda_tests(int& checks, int& failures, const fs::path& ref_dir) {
    g_checks_ptr = &checks;
    g_failures_ptr = &failures;
    test_sortval();
    test_lstrcmp_model_basics();
    test_sortnote();
    test_xnote();
    test_lstrcmp_reference(ref_dir);
}

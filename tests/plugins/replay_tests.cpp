// Replays the x86 fixture scripts (tests/plugins/fixtures/plugin_fixtures.py)
// against the native modules and compares the produced text line by line
// with the recorded original output in tests/plugins/reference/.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "plugins/exrand.h"
#include "plugins/hmm_bits.h"
#include "plugins/hspda.h"
#include "plugins/zgzip.h"
#include "test_patterns.h"

namespace fs = std::filesystem;
using namespace elona::plugins;
using elona::plugin_tests::pattern_byte;
using elona::plugin_tests::rnd_byte;

namespace {

std::vector<std::string> read_reference(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }
    return lines;
}

bool compare_lines(const char* what, const std::vector<std::string>& expected, const std::vector<std::string>& got,
                   int& checks, int& failures) {
    ++checks;
    if (expected.empty()) {
        std::fprintf(stderr, "SKIP %s replay: reference missing\n", what);
        return true;
    }
    bool ok = expected.size() == got.size();
    std::size_t shown = 0;
    for (std::size_t i = 0; i < std::max(expected.size(), got.size()); ++i) {
        const std::string e = i < expected.size() ? expected[i] : "<none>";
        const std::string g = i < got.size() ? got[i] : "<none>";
        if (e != g) {
            ok = false;
            if (shown++ < 8) std::fprintf(stderr, "%s line %zu\n  x86:    %s\n  native: %s\n", what, i + 1, e.c_str(), g.c_str());
        }
    }
    if (!ok) ++failures;
    std::printf("%s replay: %zu lines %s\n", what, expected.size(), ok ? "match" : "DIFFER");
    return ok;
}

std::string hex(const std::uint8_t* p, std::size_t n) {
    if (n == 0) return "-";
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 15]);
    }
    return s;
}

std::string hex(const std::string& s) { return hex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

std::string unhex(const std::string& h) {
    std::string s;
    if (h == "-") return s;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) s.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return s;
}

// ------------------------------------------------------------------ HMM
std::vector<std::string> replay_hmm() {
    std::vector<std::string> out;
    const std::int32_t init[] = {0, 1, -1, INT32_MIN, 305419896};
    const std::int32_t bits[] = {-33, -32, -1, 0, 1, 30, 31, 32, 33, 63, 64, 2147483647, INT32_MIN};
    for (std::int32_t i : init) {
        for (std::int32_t b : bits) {
            std::int32_t a[3] = {286331153, i, 572662306};
            const int s1 = hmm_stat_from_raw(hmm_bit_on_raw(a[1], b));
            const std::int32_t v1 = a[1];
            const int c1 = hmm_stat_from_raw(hmm_bit_check_raw(a[1], b));
            const int s2 = hmm_stat_from_raw(hmm_bit_off_raw(a[1], b));
            const std::int32_t v2 = a[1];
            const int c2 = hmm_stat_from_raw(hmm_bit_check_raw(a[1], b));
            const int c3 = hmm_stat_from_raw(hmm_bit_check_raw(i, b));
            std::ostringstream line;
            line << "hmm " << i << ' ' << b << ' ' << s1 << ' ' << v1 << ' ' << c1 << ' ' << s2 << ' ' << v2 << ' ' << c2
                 << ' ' << c3 << ' ' << a[0] << ' ' << a[2];
            out.push_back(line.str());
        }
    }
    std::int32_t v = 0;
    for (int k = 0; k < 3; ++k) {
        const int s = hmm_stat_from_raw(hmm_bit_on_raw(v, 5));
        out.push_back("repeat_on " + std::to_string(v) + " " + std::to_string(s));
    }
    for (int k = 0; k < 2; ++k) {
        const int s = hmm_stat_from_raw(hmm_bit_off_raw(v, 5));
        out.push_back("repeat_off " + std::to_string(v) + " " + std::to_string(s));
    }
    out.push_back("end");
    return out;
}

// ------------------------------------------------------------------ gzip
struct GzReplay {
    GzipRegistry z;
    fs::path dir;
    std::vector<std::string> out;

    fs::path at(const std::string& name) const { return dir / name; }

    static std::string s(int v) { return std::to_string(v); }

    int stat_of(const PluginResult& r) {
        if (!r.is_ok()) return 9999;  // never matches a reference line
        return r.stat;
    }

    void readcheck(const std::string& name, int chunk, int mode) {
        std::vector<std::uint8_t> rb(static_cast<std::size_t>(chunk) + 1);
        std::int32_t h = -7;
        const int so = stat_of(z.open(h, at(name), 0, 0));
        std::string line = "r " + name + " " + s(chunk) + " open=" + s(so) + " hnz=" + s(h != 0);
        if (h == 0) {
            out.push_back(line);
            return;
        }
        int pos = 0, bad = 0;
        for (int k = 0; k < 400; ++k) {
            const int st = stat_of(z.read(h, rb.data(), rb.size(), chunk));
            line += " " + s(st);
            if (st <= 0) break;
            for (int i = 0; i < st; ++i) {
                const std::uint8_t e = mode == 0 ? pattern_byte(static_cast<std::size_t>(pos + i)) : rnd_byte(static_cast<std::size_t>(pos + i));
                if (rb[static_cast<std::size_t>(i)] != e) ++bad;
            }
            pos += st;
        }
        line += " again=" + s(stat_of(z.read(h, rb.data(), rb.size(), chunk)));
        line += " close=" + s(stat_of(z.close(h))) + " total=" + s(pos) + " bad=" + s(bad);
        out.push_back(line);
    }

    std::string write_chunks(const std::string& name, int size, bool rnd, bool show_hnz) {
        std::vector<std::uint8_t> chunk(4100);
        std::int32_t h = -7;
        const int so = stat_of(z.open(h, at(name), 1, 3));
        std::string line = "w " + name + " " + s(size) + " open=" + s(so);
        if (show_hnz) line += " hnz=" + s(h != 0);
        int pos = 0;
        for (;;) {
            int n = size - pos;
            if (n > 4096) n = 4096;
            for (int i = 0; i < n; ++i)
                chunk[static_cast<std::size_t>(i)] = rnd ? rnd_byte(static_cast<std::size_t>(pos + i)) : pattern_byte(static_cast<std::size_t>(pos + i));
            line += " " + s(stat_of(z.write(h, chunk.data(), chunk.size(), n)));
            pos += n;
            if (pos >= size) break;
        }
        line += " close=" + s(stat_of(z.close(h)));
        return line;
    }

    void run() {
        const int sizes[] = {0, 1, 4, 16384, 16385, 100000};
        for (int size : sizes) out.push_back(write_chunks("orig_" + s(size) + ".gz", size, false, true));
        {
            std::vector<std::uint8_t> big(100001);
            for (std::size_t i = 0; i < 100000; ++i) big[i] = pattern_byte(i);
            std::int32_t h = 0;
            std::string line = "w orig_whole_100000.gz 100000 open=" + s(stat_of(z.open(h, at("orig_whole_100000.gz"), 1, 3)));
            line += " " + s(stat_of(z.write(h, big.data(), big.size(), 100000)));
            line += " close=" + s(stat_of(z.close(h)));
            out.push_back(line);
        }
        out.push_back(write_chunks("orig_rand_100000.gz", 100000, true, false));
        readcheck("orig_rand_100000.gz", 4096, 1);
        readcheck("orig_rand_100000.gz", 16385, 1);
        readcheck("native_rand_100000.gz", 4096, 1);
        readcheck("native_rand_100000.gz", 16385, 1);
        readcheck("native_rand_100000.gz", 100000, 1);
        for (int size : sizes) {
            readcheck("orig_" + s(size) + ".gz", 4096, 0);
            readcheck("native_" + s(size) + ".gz", 4096, 0);
            readcheck("native_" + s(size) + ".gz", 16385, 0);
        }
        readcheck("orig_4.gz", 1, 0);
        readcheck("native_4.gz", 1, 0);
        readcheck("orig_whole_100000.gz", 100000, 0);
        readcheck("crc.gz", 50000, 0);
        readcheck("truncated.gz", 50000, 0);
        readcheck("plain.bin", 3000, 0);
        readcheck("empty.bin", 10, 0);
        readcheck("cat.gz", 1024, 0);
        readcheck("header.gz", 7777, 0);
        readcheck("badmethod.gz", 100, 0);
        readcheck("rand_crc.gz", 4096, 1);
        readcheck("rand_trunc.gz", 4096, 1);
        std::int32_t h = 55;
        int st = stat_of(z.open(h, at("missing_dir") / "none.gz", 0, 0));
        out.push_back("missing open=" + s(st) + " h=" + s(h));
        fs::create_directories(at("subdir"));
        h = 55;
        st = stat_of(z.open(h, at("subdir"), 0, 0));
        out.push_back("dir open=" + s(st) + " h=" + s(h));
        std::vector<std::uint8_t> rb(65, 0);
        out.push_back("null read=" + s(stat_of(z.read(0, rb.data(), rb.size(), 10))));
        out.push_back("null close=" + s(stat_of(z.close(0))));
        std::string line = "wr open=" + s(stat_of(z.open(h, at("wr.gz"), 1, 3)));
        line += " read_on_w=" + s(stat_of(z.read(h, rb.data(), rb.size(), 8)));
        line += " write0=" + s(stat_of(z.write(h, rb.data(), rb.size(), 0)));
        std::memcpy(rb.data(), "abcdefgh", 9);
        line += " write8=" + s(stat_of(z.write(h, rb.data(), rb.size(), 8)));
        line += " close=" + s(stat_of(z.close(h)));
        out.push_back(line);
        line = "rd open=" + s(stat_of(z.open(h, at("wr.gz"), 0, 0)));
        line += " read0=" + s(stat_of(z.read(h, rb.data(), rb.size(), 0)));
        std::fill(rb.begin(), rb.end(), 0);
        const int read64 = stat_of(z.read(h, rb.data(), rb.size(), 64));  // sequence before hex(rb)
        line += " read64=" + s(read64) + " data=" + hex(rb.data(), 8);
        line += " again=" + s(stat_of(z.read(h, rb.data(), rb.size(), 64)));
        line += " again2=" + s(stat_of(z.read(h, rb.data(), rb.size(), 64)));
        line += " close=" + s(stat_of(z.close(h)));
        out.push_back(line);
        std::vector<std::uint8_t> lv(5001);
        for (std::size_t i = 0; i < 5000; ++i) lv[i] = pattern_byte(i);
        const int levels[] = {54, 56, -48, 259};
        for (int k = 0; k < 4; ++k) {
            const std::string name = "lv_" + s(k) + ".gz";
            line = "lv " + s(levels[k]) + " open=" + s(stat_of(z.open(h, at(name), 1, levels[k])));
            line += " write=" + s(stat_of(z.write(h, lv.data(), lv.size(), 5000)));
            line += " close=" + s(stat_of(z.close(h)));
            out.push_back(line);
            readcheck(name, 1000, 0);
        }
        out.push_back("end");
    }
};

// ------------------------------------------------------------------ hspda
std::vector<std::string> replay_hspda(const std::vector<std::string>& reference) {
    // sortval/sortnote lines carry their own inputs; xnote follows the harness script.
    std::vector<std::string> out;
    for (const std::string& line : reference) {
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "sortval") {
            int order = 0, len1 = 0, total = 0;
            row >> order >> len1 >> total;
            std::vector<std::int32_t> v(static_cast<std::size_t>(total));
            for (auto& x : v) row >> x;
            const PluginResult r = hspda_sortval(HspVarType::int32, v.data(), v.size() * 4, len1, order);
            std::ostringstream o;
            o << "sortval " << order << ' ' << len1 << ' ' << total;
            std::string tok;
            std::istringstream inputs(line);
            for (int i = 0; i < 4; ++i) inputs >> tok;
            for (int i = 0; i < total; ++i) { inputs >> tok; o << ' ' << tok; }
            o << " =>";
            for (auto x : v) o << ' ' << x;
            o << " stat=" << (r.is_ok() ? r.stat : 9999);
            out.push_back(o.str());
        } else if (kind == "sortval_str") {
            std::int32_t dummy[16] = {};
            const PluginResult r = hspda_sortval(HspVarType::str, dummy, sizeof dummy, 16, 0);
            out.push_back("sortval_str stat=" + std::to_string(r.is_ok() ? r.stat : 9999));
        } else if (kind == "sortnote") {
            int order = 0;
            std::string hin;
            row >> order >> hin;
            std::string text = unhex(hin), result;
            const PluginResult r = hspda_sortnote(text, 4096, order, result);
            const std::string shown = r.is_ok() && r.stat == 0 ? result : text;
            out.push_back("sortnote " + std::to_string(order) + " " + hin + " " + hex(shown) + " stat=" +
                          std::to_string(r.is_ok() ? r.stat : 9999));
        }
    }
    // XnoteHarness.cs sequence.
    auto xline = [&](const char* label, const PluginResult& r, const std::uint8_t* buf) {
        const std::string stat = std::to_string(r.is_ok() ? r.stat : 9999);
        out.push_back(std::string("xnote ") + label + " " + stat + " " +
                      (buf ? hex(buf, std::strlen(reinterpret_cast<const char*>(buf))) : std::string("-")));
    };
    HspdaXNote x;
    std::vector<std::uint8_t> xb(256, 0);
    xline("sel1", x.select(HspVarType::str, xb.data(), xb.size(), 0), xb.data());
    xline("add1", x.add(xb.data(), xb.size(), "sword"), xb.data());
    xline("add2", x.add(xb.data(), xb.size(), "shield"), xb.data());
    xline("add3", x.add(xb.data(), xb.size(), "sword"), xb.data());
    xline("add4", x.add(xb.data(), xb.size(), ""), xb.data());
    xline("add5", x.add(xb.data(), xb.size(), ""), xb.data());
    std::vector<std::uint8_t> xb2(64, 0);
    xb2[0] = 'a';
    xb2[1] = '\n';
    xb2[2] = 'b';
    xline("sel2", x.select(HspVarType::str, xb2.data(), xb2.size(), 4), xb2.data());
    xline("add6", x.add(xb2.data(), xb2.size(), "c"), xb2.data());
    std::vector<std::uint8_t> xb3(256, 0);
    std::memcpy(xb3.data(), "old", 3);
    xline("sel4", x.select(HspVarType::str, xb3.data(), xb3.size(), 0), xb3.data());
    xb3[0] = 0;
    xline("add7", x.add(xb3.data(), xb3.size(), "game"), xb3.data());
    xline("add8", x.add(xb3.data(), xb3.size(), "game"), xb3.data());
    xline("add9", x.add(xb3.data(), xb3.size(), "item b"), xb3.data());
    xline("add10", x.add(xb3.data(), xb3.size(), "game"), xb3.data());
    std::vector<std::uint8_t> iv(16, 0);
    xline("sel3", x.select(HspVarType::int32, iv.data(), iv.size(), 0), nullptr);
    out.push_back("end");
    return out;
}

}  // namespace

void run_replay_tests(int& checks, int& failures, const fs::path& ref_dir, const fs::path& work) {
    compare_lines("hmm_bits", read_reference(ref_dir / "hmm_bits.txt"), replay_hmm(), checks, failures);

    const fs::path gz_dir = work / "gz-replay";
    std::error_code ec;
    fs::remove_all(gz_dir, ec);
    fs::create_directories(gz_dir);
    if (fs::exists(ref_dir / "gz_inputs")) {
        for (const auto& entry : fs::directory_iterator(ref_dir / "gz_inputs")) {
            fs::copy_file(entry.path(), gz_dir / entry.path().filename(), fs::copy_options::overwrite_existing);
        }
        GzReplay replay;
        replay.dir = gz_dir;
        replay.run();
        compare_lines("gz_contract", read_reference(ref_dir / "gz_contract.txt"), replay.out, checks, failures);
    } else {
        std::fprintf(stderr, "SKIP gz_contract replay: gz_inputs missing\n");
    }

    std::vector<std::string> hspda = read_reference(ref_dir / "hspda_reference.txt");
    // Drop the console fixture's "end" so only the harness "end" remains, as in the replay.
    std::vector<std::string> expected;
    for (std::size_t i = 0; i < hspda.size(); ++i) {
        if (hspda[i] == "end" && i + 1 < hspda.size()) continue;
        expected.push_back(hspda[i]);
    }
    compare_lines("hspda", expected, replay_hspda(expected), checks, failures);
    // GUI-runtime xnote (HSP call path) must agree with the direct-call harness lines.
    const std::vector<std::string> gui = read_reference(ref_dir / "hspda_xnote_gui.txt");
    if (!gui.empty()) {
        const std::vector<std::string> native = replay_hspda({});
        std::vector<std::string> picked;
        for (const std::string& line : gui) {
            std::string found = "<missing>";
            const std::string key = line.substr(0, line.find(' ', 6) + 1);
            for (const std::string& n : native)
                if (n.rfind(key, 0) == 0 || (line == "end" && n == "end")) found = n;
            picked.push_back(found);
        }
        compare_lines("hspda_xnote_gui", gui, picked, checks, failures);
    }
    const std::vector<std::string> edges = read_reference(ref_dir / "hspda_sortnote_edges.txt");
    std::vector<std::string> got = replay_hspda(edges);
    got.resize(edges.size());  // edges file has no xnote part
    compare_lines("hspda_sortnote_edges", edges, got, checks, failures);
}

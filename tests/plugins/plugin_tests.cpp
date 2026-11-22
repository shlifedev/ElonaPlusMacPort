// Unit and reference tests for src/plugins (HMM bits, exrand, z.hpi gzip, hspda).
//
// Expected values come from three sources and are labelled as such:
//   [x86]    observed from the original DLL in the official HSP 3.4 x86 runtime
//            (tests/plugins/reference/*, recorded by scripts/reference plugin fixtures)
//   [static] derived from the shipped DLL instructions / public algorithm
//   [self]   native round-trip properties
#include <array>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

#include "plugins/exrand.h"
#include "plugins/hmm_bits.h"
#include "plugins/hspda.h"
#include "plugins/zgzip.h"
#include "test_patterns.h"

namespace fs = std::filesystem;
using namespace elona::plugins;

namespace {

int g_failures = 0;
int g_checks = 0;
fs::path g_work;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        if (!(cond)) {                                                                                   \
            ++g_failures;                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        const auto va_ = (a);                                                                            \
        const auto vb_ = (b);                                                                            \
        if (!(va_ == vb_)) {                                                                             \
            ++g_failures;                                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b " (" << va_ \
                      << " vs " << vb_ << ")\n";                                                         \
        }                                                                                                \
    } while (0)

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

using elona::plugin_tests::pattern;
using elona::plugin_tests::rnd_pattern;

// ---------------------------------------------------------------- HMM bits
void test_hmm_bits() {
    const std::int32_t initial[] = {0, 1, -1, INT32_MIN, 0x12345678};
    const std::int32_t bits[] = {-33, -32, -1, 0, 1, 30, 31, 32, 33, 63, 64, INT32_MAX, INT32_MIN};
    for (std::int32_t init : initial) {
        for (std::int32_t bit : bits) {
            const std::uint32_t shift = static_cast<std::uint32_t>(bit) % 32u;  // [static] shl cl masks 5 bits
            const std::uint32_t mask = std::uint32_t{1} << shift;
            std::int32_t v = init;
            CHECK_EQ(hmm_bit_on_raw(v, bit), -1);
            CHECK_EQ(static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(init) | mask);
            CHECK_EQ(hmm_bit_check_raw(v, bit), -1);
            CHECK_EQ(hmm_stat_from_raw(hmm_bit_check_raw(v, bit)), 1);
            CHECK_EQ(hmm_bit_off_raw(v, bit), -1);
            CHECK_EQ(static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(init) & ~mask);
            CHECK_EQ(hmm_bit_check_raw(v, bit), 0);
            CHECK_EQ(hmm_bit_check_raw(init, bit), (static_cast<std::uint32_t>(init) & mask) ? -1 : 0);
        }
    }
    // Array neighbours untouched (the host passes one element).
    std::int32_t arr[3] = {0x11111111, 0, 0x22222222};
    hmm_bit_on_raw(arr[1], 31);
    CHECK_EQ(arr[0], 0x11111111);
    CHECK_EQ(arr[1], INT32_MIN);
    CHECK_EQ(arr[2], 0x22222222);
    static_assert(hmm_bit_check_raw(INT32_MIN, 31) == -1, "constexpr check");
}

// ---------------------------------------------------------------- exrand
void test_exrand() {
    // [x86] exrand_randomize 12345 / exrand_rnd v,32768 x10 (docs/WINDOWS_REFERENCE.md)
    {
        ExRand r;
        CHECK(r.randomize(12345).is_ok());
        const int expected[] = {30461, 29168, 10366, 4283, 6026, 1302, 6703, 27080, 18603, 17435};
        for (int e : expected) {
            std::int32_t v = -1;
            const PluginResult res = r.rnd(v, 32768);
            CHECK(res.is_ok());
            CHECK_EQ(res.raw_return, 0);
            CHECK_EQ(res.stat, 0);
            CHECK_EQ(v, e);
        }
    }
    // [static] public MT19937 vector: init_genrand(5489) first output 3499211612;
    // the DLL lazily seeds 5489 when unseeded.
    {
        ExRand r;
        CHECK_EQ(r.next_u32(), 3499211612u);
        ExRand s;
        CHECK(s.randomize(5489).is_ok());
        CHECK_EQ(s.next_u32(), 3499211612u);
    }
    // Errors: EAX 3 -> HSP DLL error; state and output unchanged.
    {
        ExRand a, b;
        a.randomize(777);
        b.randomize(777);
        std::int32_t v = 1234;
        PluginResult res = a.randomize(-1);
        CHECK(res.outcome == PluginOutcome::hsp_dll_error);
        CHECK_EQ(res.raw_return, 3);
        res = a.rnd(v, -5);
        CHECK(res.outcome == PluginOutcome::hsp_dll_error);
        CHECK_EQ(res.raw_return, 3);
        CHECK_EQ(v, 1234);
        res = a.randomize(INT32_MIN);
        CHECK(res.outcome == PluginOutcome::hsp_dll_error);
        std::int32_t va = 0, vb = 0;
        a.rnd(va, 1000000);
        b.rnd(vb, 1000000);
        CHECK_EQ(va, vb);
    }
    // range 0 returns 0 but consumes a draw.
    {
        ExRand a, b;
        a.randomize(42);
        b.randomize(42);
        std::int32_t v = 99;
        CHECK(a.rnd(v, 0).is_ok());
        CHECK_EQ(v, 0);
        b.next_u32();
        std::int32_t va = 0, vb = 0;
        a.rnd(va, 1 << 30);
        b.rnd(vb, 1 << 30);
        CHECK_EQ(va, vb);
    }
    // Scale boundary: exact floor((2^62-1)/2^32) = 2^30-1, but one IEEE double
    // (= x87 53-bit) rounding gives 2^30. The port follows the 53-bit result;
    // whether the original process used 53-bit precision is checked by the x86
    // fixture `exrand_precision` (see reference test below).
    CHECK_EQ(ExRand::scale(0x80000001u, 0x7fffffff), 1073741824);
    CHECK_EQ(ExRand::scale(0xffffffffu, 0x7fffffff), 2147483646);
    CHECK_EQ(ExRand::scale(0xffffffffu, 1), 0);
    CHECK_EQ(ExRand::scale(0u, 0x7fffffff), 0);
    // rnd2 adds base modulo 2^32.
    {
        ExRand a, b;
        a.randomize(9);
        b.randomize(9);
        std::int32_t v1 = 0, v2 = 0;
        a.rnd(v1, 100);
        b.rnd2(v2, INT32_MAX, 100);
        CHECK_EQ(v2, static_cast<std::int32_t>(static_cast<std::uint32_t>(v1) + 0x7fffffffu));
    }
}

// Reference vectors produced by the original exrand.dll:
// "<seed|-1 unseeded> <k> (<range> <value>)*k", generator reset per line.
void test_exrand_reference(const fs::path& ref_dir) {
    const fs::path path = ref_dir / "exrand_vectors.txt";
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "SKIP exrand reference: %s missing\n", path.string().c_str());
        return;
    }
    std::string line;
    int lines = 0, values = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        CHECK(line.find("STAT") == std::string::npos);
        std::istringstream row(line);
        long long seed = 0;
        int k = 0;
        row >> seed >> k;
        ExRand r;
        if (seed != -1) CHECK(r.randomize(static_cast<std::int32_t>(seed)).is_ok());
        for (int i = 0; i < k; ++i) {
            long long range = 0, expected = 0;
            row >> range >> expected;
            std::int32_t v = 0;
            CHECK(r.rnd(v, static_cast<std::int32_t>(range)).is_ok());
            ++g_checks;
            ++values;
            if (v != expected) {
                ++g_failures;
                std::fprintf(stderr, "exrand mismatch seed=%lld range=%lld i=%d native=%d x86=%lld\n", seed, range, i, v,
                             expected);
            }
        }
        CHECK(static_cast<bool>(row));
        ++lines;
    }
    CHECK(lines > 0);
    std::printf("exrand reference: %d lines, %d values\n", lines, values);
}

// ---------------------------------------------------------------- gzip
std::vector<std::uint8_t> gz_read_all(GzipRegistry& z, const fs::path& path, std::int32_t chunk, int* stat_open,
                                      std::vector<std::int32_t>* stats = nullptr) {
    std::int32_t h = -7;
    PluginResult r = z.open(h, path, 0, 0);
    *stat_open = r.stat;
    std::vector<std::uint8_t> out;
    if (!r.is_ok() || r.stat != 0) return out;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(chunk));
    for (int guard = 0; guard < 100000; ++guard) {
        r = z.read(h, buf.data(), buf.size(), chunk);
        CHECK(r.is_ok());
        if (stats) stats->push_back(r.stat);
        if (r.stat <= 0) break;
        out.insert(out.end(), buf.begin(), buf.begin() + r.stat);
    }
    const PluginResult c = z.close(h);
    CHECK(c.is_ok());
    return out;
}

bool gz_write_file(GzipRegistry& z, const fs::path& path, const std::vector<std::uint8_t>& data, std::int32_t level,
                   std::size_t chunk) {
    std::int32_t h = 0;
    PluginResult r = z.open(h, path, 1, level);
    if (!r.is_ok() || r.stat != 0) return false;
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min(chunk, data.size() - pos);
        r = z.write(h, data.data() + pos, n, static_cast<std::int32_t>(n));
        CHECK(r.is_ok());
        CHECK_EQ(r.stat, static_cast<std::int32_t>(n));
        pos += n;
    } while (pos < data.size());
    r = z.close(h);
    return r.is_ok() && r.stat == 0;
}

// Independent decoder (zlib's own gzip wrapper) for cross-checking native writes.
std::vector<std::uint8_t> zlib_gunzip(const std::vector<std::uint8_t>& gz, bool* ok) {
    z_stream s{};
    std::vector<std::uint8_t> out;
    *ok = inflateInit2(&s, 15 + 16) == Z_OK;
    if (!*ok) return out;
    s.next_in = const_cast<Bytef*>(gz.data());
    s.avail_in = static_cast<uInt>(gz.size());
    std::array<std::uint8_t, 4096> buf{};
    int rc = Z_OK;
    do {
        s.next_out = buf.data();
        s.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&s, Z_NO_FLUSH);
        out.insert(out.end(), buf.data(), buf.data() + (buf.size() - s.avail_out));
    } while (rc == Z_OK);
    *ok = rc == Z_STREAM_END && s.avail_in == 0;
    inflateEnd(&s);
    return out;
}

void test_gzip_roundtrip() {
    GzipRegistry z;
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{4}, std::size_t{16383}, std::size_t{16384},
                             std::size_t{16385}, std::size_t{100000}}) {
        for (std::int32_t level : {3, 0, 9}) {
            const auto data = pattern(size);
            const fs::path p = g_work / ("rt_" + std::to_string(size) + "_" + std::to_string(level) + ".gz");
            CHECK(gz_write_file(z, p, data, level, 7000));
            const auto file = read_file(p);
            CHECK(file.size() >= 18);
            CHECK(file.size() >= 10 && file[0] == 0x1f && file[1] == 0x8b && file[2] == 8 && file[3] == 0 &&
                  file[9] == 0x0b);
            bool ok = false;
            CHECK(zlib_gunzip(file, &ok) == data);
            CHECK(ok);
            int st = -1;
            for (std::int32_t chunk : {1, 4, 4096, 16384, 16385, 200000}) {
                if (size > 20000 && chunk < 16) continue;
                std::vector<std::int32_t> stats;
                CHECK(gz_read_all(z, p, chunk, &st, &stats) == data);
                CHECK_EQ(st, 0);
                CHECK(!stats.empty() && stats.back() == 0);
            }
        }
    }
    {
        const auto data = rnd_pattern(200000);
        const fs::path p = g_work / "rt_rand.gz";
        CHECK(gz_write_file(z, p, data, 3, 4096));
        CHECK(read_file(p).size() > 3 * 16384);  // exercises several 16 KiB fills
        bool ok = false;
        CHECK(zlib_gunzip(read_file(p), &ok) == data);
        CHECK(ok);
        int st = -1;
        for (std::int32_t chunk : {4096, 16385, 200000}) CHECK(gz_read_all(z, p, chunk, &st) == data);
    }
    CHECK_EQ(z.open_count(), std::size_t{0});
}

void test_gzip_contract() {
    GzipRegistry z;
    std::uint8_t buf[64] = {};
    // Missing file: stat 1, handle written as 0.
    std::int32_t h = 55;
    PluginResult r = z.open(h, g_work / "missing" / "nothing.gz", 0, 0);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 1);
    CHECK_EQ(r.raw_return, -1);
    CHECK_EQ(h, 0);
    // Directory path behaves like MSVC fopen failure.
    h = 55;
    r = z.open(h, g_work, 0, 0);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 1);
    CHECK_EQ(h, 0);
    // NULL handle branches of the original.
    r = z.read(0, buf, sizeof buf, 10);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 0);
    r = z.write(0, buf, sizeof buf, 10);
    CHECK(r.outcome == PluginOutcome::hsp_dll_error);
    CHECK_EQ(r.raw_return, 2);
    r = z.close(0);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 1);
    // Unknown handle, bounds and negative count are refused, never "succeed".
    CHECK(z.read(12345, buf, sizeof buf, 1).outcome == PluginOutcome::unsafe_input);
    CHECK(z.write(12345, buf, sizeof buf, 1).outcome == PluginOutcome::unsafe_input);
    CHECK(z.close(12345).outcome == PluginOutcome::unsafe_input);
    // Mode mismatches.
    const fs::path p = g_work / "contract.gz";
    r = z.open(h, p, 1, 3);
    CHECK(r.is_ok() && r.stat == 0 && h > 0);
    const std::int32_t wh = h;
    CHECK(z.read(wh, buf, sizeof buf, 65).outcome == PluginOutcome::unsafe_input);
    CHECK(z.read(wh, buf, sizeof buf, -1).outcome == PluginOutcome::unsafe_input);
    r = z.read(wh, buf, sizeof buf, 8);  // gzread on a write stream -> -2 -> stat 0
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 0);
    r = z.write(wh, buf, sizeof buf, 0);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 0);
    r = z.write(wh, nullptr, 0, 0);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 0);
    std::memcpy(buf, "abcdefgh", 8);
    r = z.write(wh, buf, sizeof buf, 8);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 8);
    CHECK_EQ(r.raw_return, -8);
    r = z.close(wh);
    CHECK(r.is_ok() && r.stat == 0);
    CHECK(z.close(wh).outcome == PluginOutcome::unsafe_input);  // stale handle
    // Handles are never reused.
    std::int32_t rh = 0;
    r = z.open(rh, p, 0, 0);
    CHECK(r.is_ok() && r.stat == 0);
    CHECK(rh > wh);
    r = z.write(rh, buf, sizeof buf, 4);
    CHECK(r.outcome == PluginOutcome::hsp_dll_error);
    CHECK_EQ(r.raw_return, 2);
    r = z.read(rh, buf, sizeof buf, 0);
    CHECK(r.is_ok() && r.stat == 0);
    r = z.read(rh, buf, sizeof buf, 64);
    CHECK(r.is_ok());
    CHECK_EQ(r.stat, 8);
    CHECK(std::memcmp(buf, "abcdefgh", 8) == 0);
    r = z.read(rh, buf, sizeof buf, 64);
    CHECK(r.is_ok() && r.stat == 0);
    r = z.read(rh, buf, sizeof buf, 64);
    CHECK(r.is_ok() && r.stat == 0);
    CHECK(z.close(rh).is_ok());
    // Level characters.
    for (std::int32_t level : {54 /* 'f' */, 56 /* 'h' */, -48 /* NUL */, 0x103 /* low byte 3 */}) {
        const fs::path q = g_work / ("level_" + std::to_string(level) + ".gz");
        CHECK(gz_write_file(z, q, pattern(5000), level, 5000));
        int st = -1;
        CHECK(gz_read_all(z, q, 1000, &st) == pattern(5000));
    }
    r = z.open(h, g_work / "bad_level.gz", 1, 17);  // 'A' would go to fopen's mode
    CHECK(r.outcome == PluginOutcome::unsupported);
    CHECK(!fs::exists(g_work / "bad_level.gz"));
    // Abandoned write stream (process exit): no trailer.
    {
        GzipRegistry temp;
        std::int32_t th = 0;
        temp.open(th, g_work / "abandoned.gz", 1, 3);
        temp.write(th, buf, sizeof buf, 64);
    }
    bool ok = true;
    zlib_gunzip(read_file(g_work / "abandoned.gz"), &ok);
    CHECK(!ok);
}

void test_gzip_damaged() {
    GzipRegistry z;
    const auto data = pattern(40000);
    const fs::path good = g_work / "good.gz";
    CHECK(gz_write_file(z, good, data, 3, 40000));
    const auto gz = read_file(good);
    std::vector<std::uint8_t> buf(50000);
    std::int32_t h = 0;
    // CRC error: gzio 1.1.4 returns the full count, then 0; close stat 1.
    {
        auto bad = gz;
        bad[bad.size() - 8] ^= 0xff;
        const fs::path p = g_work / "crc.gz";
        write_file(p, bad);
        CHECK(z.open(h, p, 0, 0).stat == 0);
        PluginResult r = z.read(h, buf.data(), buf.size(), 50000);
        CHECK_EQ(r.stat, 40000);
        CHECK(std::equal(data.begin(), data.end(), buf.begin()));
        CHECK_EQ(z.read(h, buf.data(), buf.size(), 10).stat, 0);
        CHECK_EQ(z.close(h).stat, 1);
    }
    // Truncated deflate data: partial count, then 0; close stat 1.
    {
        std::vector<std::uint8_t> cut(gz.begin(), gz.begin() + static_cast<std::ptrdiff_t>(gz.size() / 2));
        const fs::path p = g_work / "truncated.gz";
        write_file(p, cut);
        CHECK(z.open(h, p, 0, 0).stat == 0);
        PluginResult r = z.read(h, buf.data(), buf.size(), 50000);
        CHECK(r.is_ok());
        CHECK(r.stat > 0 && r.stat < 40000);
        CHECK(std::equal(buf.begin(), buf.begin() + r.stat, data.begin()));
        CHECK_EQ(z.read(h, buf.data(), buf.size(), 10).stat, 0);
        CHECK_EQ(z.close(h).stat, 1);
    }
    // Plain (non-gzip) file is read transparently.
    {
        const fs::path p = g_work / "plain.bin";
        write_file(p, data);
        int st = -1;
        CHECK(gz_read_all(z, p, 3000, &st) == data);
        CHECK_EQ(st, 0);
    }
    // Empty file: open succeeds, read 0, close 0.
    {
        const fs::path p = g_work / "empty.bin";
        write_file(p, {});
        CHECK(z.open(h, p, 0, 0).stat == 0);
        CHECK_EQ(z.read(h, buf.data(), buf.size(), 10).stat, 0);
        CHECK_EQ(z.close(h).stat, 0);
    }
    // Concatenated members are read as one stream.
    {
        const fs::path a = g_work / "a.gz";
        const fs::path b = g_work / "b.gz";
        CHECK(gz_write_file(z, a, pattern(3000), 3, 3000));
        CHECK(gz_write_file(z, b, pattern(5000), 3, 5000));
        auto cat = read_file(a);
        const auto tail = read_file(b);
        cat.insert(cat.end(), tail.begin(), tail.end());
        const fs::path p = g_work / "cat.gz";
        write_file(p, cat);
        auto expected = pattern(3000);
        const auto p2 = pattern(5000);
        expected.insert(expected.end(), p2.begin(), p2.end());
        int st = -1;
        CHECK(gz_read_all(z, p, 1024, &st) == expected);
    }
    // Header with FNAME/FCOMMENT/FEXTRA/FHCRC from zlib's gzip writer.
    {
        z_stream s{};
        CHECK(deflateInit2(&s, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK);
        gz_header head{};
        unsigned char extra[3] = {1, 2, 3};
        unsigned char name[] = "name.bin";
        unsigned char comment[] = "comment";
        head.extra = extra;
        head.extra_len = 3;
        head.name = name;
        head.comment = comment;
        head.hcrc = 1;
        deflateSetHeader(&s, &head);
        std::vector<std::uint8_t> out(70000);
        s.next_in = const_cast<Bytef*>(data.data());
        s.avail_in = static_cast<uInt>(data.size());
        s.next_out = out.data();
        s.avail_out = static_cast<uInt>(out.size());
        CHECK(deflate(&s, Z_FINISH) == Z_STREAM_END);
        out.resize(out.size() - s.avail_out);
        deflateEnd(&s);
        const fs::path p = g_work / "header.gz";
        write_file(p, out);
        int st = -1;
        CHECK(gz_read_all(z, p, 7777, &st) == data);
    }
}

void test_gzip_unicode_path() {
    GzipRegistry z;
    const fs::path dir = g_work / fs::u8path(u8"ユーザー 데이터");
    fs::create_directories(dir);
    const fs::path p = dir / fs::u8path(u8"セーブ.gz");
    CHECK(gz_write_file(z, p, pattern(1234), 3, 1234));
    CHECK(fs::exists(p));
    int st = -1;
    CHECK(gz_read_all(z, p, 999, &st) == pattern(1234));
    CHECK_EQ(st, 0);
}

// Cross-check against files written by the original z.hpi (tests/plugins/reference/gz/*).
// manifest.txt lines: "<file> <pattern-size>" produced by the x86 fixture.
void test_gzip_reference(const fs::path& ref_dir) {
    const fs::path manifest = ref_dir / "gz" / "manifest.txt";
    std::ifstream in(manifest);
    if (!in) {
        std::fprintf(stderr, "SKIP gzip reference: %s missing\n", manifest.string().c_str());
        return;
    }
    GzipRegistry z;
    std::string name, kind;
    std::size_t size = 0;
    int files = 0;
    while (in >> name >> size >> kind) {
        int st = -1;
        const auto got = gz_read_all(z, ref_dir / "gz" / name, 4096, &st);
        CHECK_EQ(st, 0);
        if (got != (kind == "r" ? rnd_pattern(size) : pattern(size))) {
            ++g_failures;
            std::fprintf(stderr, "gzip reference mismatch: %s\n", name.c_str());
        }
        ++g_checks;
        ++files;
    }
    CHECK(files > 0);
    std::printf("gzip reference files: %d\n", files);
}

// Writes native gzip files for the x86 fixture to read back (stage step).
void write_native_samples(const fs::path& out) {
    fs::create_directories(out);
    GzipRegistry z;
    std::ofstream manifest(out / "manifest.txt");
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{4}, std::size_t{16384}, std::size_t{16385},
                             std::size_t{100000}}) {
        const std::string name = "native_" + std::to_string(size) + ".gz";
        gz_write_file(z, out / name, pattern(size), 3, 4096);
        manifest << name << ' ' << size << " p\n";
    }
    gz_write_file(z, out / "native_rand_100000.gz", rnd_pattern(100000), 3, 4096);
    manifest << "native_rand_100000.gz 100000 r\n";
}

}  // namespace

void run_hspda_tests(int& checks, int& failures, const fs::path& ref_dir);
void run_replay_tests(int& checks, int& failures, const fs::path& ref_dir, const fs::path& work);

int main(int argc, char** argv) {
    fs::path work = fs::current_path() / "plugin-tests";
    fs::path ref_dir = ELONA_PLUGINS_REFERENCE_DIR;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--work") work = argv[i + 1];
        else if (key == "--reference") ref_dir = argv[i + 1];
        else if (key == "--write-native-samples") {
            write_native_samples(argv[i + 1]);
            std::printf("wrote native samples to %s\n", argv[i + 1]);
            return 0;
        }
    }
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);
    g_work = work;

    test_hmm_bits();
    test_exrand();
    test_exrand_reference(ref_dir);
    test_gzip_roundtrip();
    test_gzip_contract();
    test_gzip_damaged();
    test_gzip_unicode_path();
    test_gzip_reference(ref_dir);
    run_hspda_tests(g_checks, g_failures, ref_dir);
    run_replay_tests(g_checks, g_failures, ref_dir, work);

    std::printf("plugin tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#include "game_files.h"

#include "encoding.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace elona::platform {
namespace fs = std::filesystem;

const char* to_string(FileError error) {
    switch (error) {
    case FileError::ok: return "ok";
    case FileError::invalid_path: return "invalid_path";
    case FileError::outside_root: return "outside_root";
    case FileError::not_found: return "not_found";
    case FileError::not_writable: return "not_writable";
    case FileError::already_exists: return "already_exists";
    case FileError::is_directory: return "is_directory";
    case FileError::not_directory: return "not_directory";
    case FileError::ambiguous_name: return "ambiguous_name";
    case FileError::encoding_error: return "encoding_error";
    case FileError::io_error: return "io_error";
    case FileError::invalid_config: return "invalid_config";
    }
    return "unknown";
}

namespace {

constexpr const char* kMetaDir = ".elonaplus";
constexpr const char* kExternal = "@external";

FileError report(FileDiagnostic* d, FileError e, std::string detail) {
    if (d) { d->error = e; d->detail = std::move(detail); }
    return e;
}

bool is_sep(unsigned char c) { return c == '\\' || c == '/'; }

std::string printable(std::string_view cp932) {
    std::string utf8;
    if (cp932_to_utf8(cp932, utf8)) return utf8;
    std::string hex = "<cp932:";
    char b[4];
    for (unsigned char c : cp932) { std::snprintf(b, sizeof b, "%02x", c); hex += b; }
    return hex + ">";
}

// --- native name conversion ---------------------------------------------------
bool component_to_native(const std::string& cp932, fs::path& out) {
    std::string utf8;
    if (!cp932_to_utf8(cp932, utf8)) return false;
#ifdef _WIN32
    std::wstring wide;
    if (!utf8_to_wide(utf8, wide)) return false;
    out = fs::path(wide);
#else
    out = fs::u8path(utf8);
#endif
    return true;
}

bool native_to_component(const fs::path& name, std::string& cp932) {
    std::string utf8;
#ifdef _WIN32
    if (!wide_to_utf8(name.native(), utf8)) return false;
#else
    utf8 = name.native();
#endif
    std::string nfc;
    if (!utf8_to_nfc(utf8, nfc)) return false;
    return static_cast<bool>(utf8_to_cp932(nfc, cp932));
}

std::string path_utf8(const fs::path& p) { return p.u8string(); }

bool ascii_ci_equal(std::string_view a, std::string_view b) {
    return cp932_ascii_lower(a) == cp932_ascii_lower(b);
}

bool is_device_name(std::string_view component) {
    std::string base = cp932_ascii_lower(component.substr(0, component.find('.')));
    static const char* names[] = {"con", "prn", "aux", "nul"};
    for (auto n : names) if (base == n) return true;
    if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
        base[3] >= '1' && base[3] <= '9')
        return true;
    return false;
}

FileError validate_component(const std::string& c, bool allow_wildcard, std::string& why) {
    for (std::size_t i = 0; i < c.size(); ++i) {
        const auto b = static_cast<unsigned char>(c[i]);
        if (cp932_is_lead_byte(b)) {
            if (i + 1 >= c.size()) { why = "truncated CP932 double-byte character"; return FileError::invalid_path; }
            ++i;
            continue;
        }
        if (b < 0x20) { why = "control byte"; return FileError::invalid_path; }
        if (b == '<' || b == '>' || b == ':' || b == '"' || b == '|') { why = "forbidden character"; return FileError::invalid_path; }
        if ((b == '*' || b == '?') && !allow_wildcard) { why = "wildcard outside a list pattern"; return FileError::invalid_path; }
    }
    if (is_device_name(c)) { why = "Windows device name"; return FileError::invalid_path; }
    std::string utf8;
    if (!cp932_to_utf8(c, utf8)) { why = "not valid CP932"; return FileError::encoding_error; }
    return FileError::ok;
}

bool is_dir_no_follow(const fs::path& p, bool& exists, bool& symlink) {
    std::error_code ec;
    const auto st = fs::symlink_status(p, ec);
    exists = !ec && fs::exists(st);
    symlink = exists && fs::is_symlink(st);
    return exists && fs::is_directory(st);
}

// Windows-style '*' / '?' match on CP932 characters, ASCII case-insensitive.
bool wildcard_match_chars(const std::vector<std::string>& p, std::size_t pi,
                          const std::vector<std::string>& n, std::size_t ni) {
    while (pi < p.size()) {
        if (p[pi] == "*") {
            while (pi < p.size() && p[pi] == "*") ++pi;
            if (pi == p.size()) return true;
            for (std::size_t k = ni; k <= n.size(); ++k)
                if (wildcard_match_chars(p, pi, n, k)) return true;
            return false;
        }
        if (ni >= n.size()) return false;
        if (p[pi] != "?" && cp932_ascii_lower(p[pi]) != cp932_ascii_lower(n[ni])) return false;
        ++pi;
        ++ni;
    }
    return ni == n.size();
}

std::vector<std::string> split_chars(std::string_view s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t len = (cp932_is_lead_byte(static_cast<unsigned char>(s[i])) && i + 1 < s.size()) ? 2 : 1;
        out.emplace_back(s.substr(i, len));
        i += len;
    }
    return out;
}

// --- SHA-256 (FIPS 180-4), for import verification -------------------------------
struct Sha256 {
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t bits = 0;
    unsigned char block[64];
    std::size_t used = 0;
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress() {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
                   (std::uint32_t(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto a = h;
        for (int i = 0; i < 64; ++i) {
            const auto s1 = rotr(a[4], 6) ^ rotr(a[4], 11) ^ rotr(a[4], 25);
            const auto ch = (a[4] & a[5]) ^ (~a[4] & a[6]);
            const auto t1 = a[7] + s1 + ch + k[i] + w[i];
            const auto s0 = rotr(a[0], 2) ^ rotr(a[0], 13) ^ rotr(a[0], 22);
            const auto maj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
            const auto t2 = s0 + maj;
            a[7] = a[6]; a[6] = a[5]; a[5] = a[4]; a[4] = a[3] + t1;
            a[3] = a[2]; a[2] = a[1]; a[1] = a[0]; a[0] = t1 + t2;
        }
        for (int i = 0; i < 8; ++i) h[i] += a[i];
    }
    void update(const unsigned char* p, std::size_t n) {
        bits += std::uint64_t(n) * 8;
        while (n--) { block[used++] = *p++; if (used == 64) { compress(); used = 0; } }
    }
    std::string finish() {
        const std::uint64_t total = bits;
        const unsigned char pad = 0x80, zero = 0;
        update(&pad, 1);
        while (used != 56) update(&zero, 1);
        for (int i = 7; i >= 0; --i) { block[used++] = static_cast<unsigned char>(total >> (i * 8)); }
        compress();
        std::string hex;
        char b[9];
        for (auto v : h) { std::snprintf(b, sizeof b, "%08x", v); hex += b; }
        return hex;
    }
};

FileError read_native(const fs::path& path, std::vector<std::uint8_t>& out, std::uint64_t offset,
                      std::size_t max_bytes, FileDiagnostic* d) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return report(d, FileError::io_error, "cannot open " + path_utf8(path));
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(in.tellg());
    if (offset > size) offset = size;
    const std::uint64_t available = size - offset;
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(available, max_bytes));
    out.resize(count);
    in.seekg(static_cast<std::streamoff>(offset));
    if (count && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count)))
        return report(d, FileError::io_error, "read failed " + path_utf8(path));
    return FileError::ok;
}

}  // namespace

std::string cp932_ascii_lower(std::string_view cp932) {
    std::string out(cp932);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto b = static_cast<unsigned char>(out[i]);
        if (cp932_is_lead_byte(b) && i + 1 < out.size()) { ++i; continue; }
        if (b >= 'A' && b <= 'Z') out[i] = static_cast<char>(b + ('a' - 'A'));
    }
    return out;
}

bool cp932_wildcard_match(std::string_view pattern, std::string_view name) {
    // Windows FindFirstFile: "*.*" matches every name; "x.*" also matches "x".
    if (pattern == "*.*") pattern = "*";
    const auto n = split_chars(name);
    if (wildcard_match_chars(split_chars(pattern), 0, n, 0)) return true;
    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == ".*" && name.find('.') == std::string_view::npos)
        return wildcard_match_chars(split_chars(pattern.substr(0, pattern.size() - 2)), 0, n, 0);
    return false;
}

std::string sha256_hex(const void* data, std::size_t size) {
    Sha256 s;
    s.update(static_cast<const unsigned char*>(data), size);
    return s.finish();
}

std::string LogicalPath::cp932() const {
    std::string out;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i) out.push_back('\\');
        out += components[i];
    }
    return out;
}

// -----------------------------------------------------------------------------

struct GameFileSystem::Located {
    enum class Source { none, user, game, packed, grant } source = Source::none;
    fs::path native;
    bool is_dir = false;
    const FileSystemConfig::PackedMember* packed = nullptr;
    LogicalPath logical;
};

FileError GameFileSystem::open(const FileSystemConfig& config, std::unique_ptr<GameFileSystem>& out,
                               FileDiagnostic* d) {
    out.reset();
    std::error_code ec;
    if (config.game_root.empty() || config.user_root.empty())
        return report(d, FileError::invalid_config, "game_root and user_root are required");
    const fs::path game = fs::weakly_canonical(fs::absolute(config.game_root, ec), ec);
    if (ec || !fs::is_directory(game, ec)) return report(d, FileError::invalid_config, "game_root is not a directory: " + path_utf8(config.game_root));
    fs::create_directories(config.user_root, ec);
    const fs::path user = fs::weakly_canonical(fs::absolute(config.user_root, ec), ec);
    if (ec || !fs::is_directory(user, ec)) return report(d, FileError::invalid_config, "user_root is not a directory: " + path_utf8(config.user_root));
    auto inside = [](const fs::path& child, const fs::path& parent) {
        auto c = child.begin(), p = parent.begin();
        for (; p != parent.end(); ++p, ++c) { if (c == child.end() || *c != *p) return false; }
        return true;
    };
    if (inside(user, game) || inside(game, user))
        return report(d, FileError::invalid_config, "game_root and user_root must not contain each other");
    const std::string& vr = config.virtual_root;
    if (vr.empty() || is_sep(static_cast<unsigned char>(vr.back())))
        return report(d, FileError::invalid_config, "virtual_root must be non-empty without trailing separator");
    for (unsigned char c : vr) if (c >= 0x80 || c < 0x20) return report(d, FileError::invalid_config, "virtual_root must be printable ASCII");

    std::unique_ptr<GameFileSystem> self(new GameFileSystem());
    self->game_root_ = game;
    self->user_root_ = user;
    self->virtual_root_ = vr;
    for (const auto& p : config.writable_prefixes) self->writable_prefixes_.push_back(cp932_ascii_lower(p));
    self->packed_ = config.packed_members;
    // Load tombstones (UTF-8 lines of logical lowercase paths).
    std::ifstream t(user / kMetaDir / "deleted.txt", std::ios::binary);
    std::string line;
    while (std::getline(t, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string cp932;
        if (!utf8_to_cp932(line, cp932)) return report(d, FileError::invalid_config, "corrupt tombstone file line: " + line);
        self->tombstones_.push_back(cp932_ascii_lower(cp932));
    }
    std::sort(self->tombstones_.begin(), self->tombstones_.end());
    self->tombstones_.erase(std::unique(self->tombstones_.begin(), self->tombstones_.end()), self->tombstones_.end());
    out = std::move(self);
    return FileError::ok;
}

FileError GameFileSystem::parse(std::string_view hsp, LogicalPath& out) const {
    return parse_impl(hsp, out, false);
}

FileError GameFileSystem::parse_impl(std::string_view hsp, LogicalPath& out, bool allow_wildcard_last) const {
    out.components.clear();
    if (hsp.find('\0') != std::string_view::npos) return FileError::invalid_path;
    std::string_view rest = hsp;
    // Strip the virtual root: ASCII case-insensitive, '/' == '\\', must end at a separator.
    const std::string& vr = virtual_root_;
    bool rooted = false;
    if (hsp.size() >= vr.size()) {
        bool match = true;
        for (std::size_t i = 0; i < vr.size() && match; ++i) {
            const auto a = static_cast<unsigned char>(hsp[i]), b = static_cast<unsigned char>(vr[i]);
            if (is_sep(a) && is_sep(b)) continue;
            match = std::tolower(a) == std::tolower(b);
        }
        if (match && (hsp.size() == vr.size() || is_sep(static_cast<unsigned char>(hsp[vr.size()])))) {
            rest = hsp.substr(vr.size());
            rooted = true;
        }
    }
    if (!rooted) {
        if (!hsp.empty() && is_sep(static_cast<unsigned char>(hsp[0]))) return FileError::outside_root;   // \x, \\server
        if (hsp.size() >= 2 && hsp[1] == ':') return FileError::outside_root;                                // other drive / root
    }
    // Tokenise on single-byte separators only (CP932 trail 0x5C is data).
    std::vector<std::string> raw;
    std::string current;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const auto b = static_cast<unsigned char>(rest[i]);
        if (cp932_is_lead_byte(b)) {
            if (i + 1 >= rest.size()) return FileError::invalid_path;   // truncated
            current.push_back(rest[i]);
            current.push_back(rest[++i]);
            continue;
        }
        if (is_sep(b)) { raw.push_back(current); current.clear(); continue; }
        current.push_back(rest[i]);
    }
    raw.push_back(current);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::string& c = raw[i];
        if (c.empty() || c == ".") continue;
        if (c == "..") {
            if (out.components.empty()) return FileError::outside_root;
            out.components.pop_back();
            continue;
        }
        // Win32 path normalisation strips trailing dots and spaces from each
        // component ("f1.bin." and "f1.bin " open f1.bin; verified on HSP 3.4
        // x86, platform-files-03). CP932 trail bytes are never 0x20/0x2E.
        std::string component = c;
        while (!component.empty() && (component.back() == '.' || component.back() == ' ')) component.pop_back();
        if (component.empty()) return FileError::invalid_path;
        const bool last = i + 1 == raw.size();
        std::string why;
        const FileError e = validate_component(component, allow_wildcard_last && last, why);
        if (e != FileError::ok) return e;
        out.components.push_back(component);
    }
    if (!out.components.empty() && ascii_ci_equal(out.components[0], kMetaDir)) return FileError::invalid_path;
    return FileError::ok;
}

// Resolve `count` leading components below `root`. On success, `native` is the
// resolved path (existing prefix uses on-disk spelling, the remainder the given
// spelling). Symlinks are rejected anywhere below the root.
FileError GameFileSystem::lookup(const fs::path& root, const LogicalPath& path, std::size_t count,
                                 fs::path& native, bool& exists, bool& is_dir, FileDiagnostic* d) const {
    native = root;
    exists = true;
    is_dir = true;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string& comp = path.components[i];
        fs::path name;
        if (!component_to_native(comp, name)) return report(d, FileError::encoding_error, "unconvertible " + printable(comp));
        if (!exists || !is_dir) {
            if (exists && !is_dir) return report(d, FileError::not_directory, path_utf8(native) + " is a file");
            native /= name;
            continue;
        }
        fs::path candidate = native / name;
        bool e, link;
        const bool dir = is_dir_no_follow(candidate, e, link);
        if (link) return report(d, FileError::invalid_path, "symlink rejected: " + path_utf8(candidate));
        if (e) {
            // On case-insensitive volumes the exact lookup can succeed with another case;
            // prefer the on-disk spelling so materialised directories match.
            native = candidate;
            is_dir = dir;
            continue;
        }
        // ASCII case-insensitive fallback (case-sensitive volumes).
        std::vector<fs::path> matches;
        std::error_code ec;
        for (fs::directory_iterator it(native, ec), end; !ec && it != end; it.increment(ec)) {
            std::string entry;
            if (!native_to_component(it->path().filename(), entry)) continue;
            if (ascii_ci_equal(entry, comp)) matches.push_back(it->path());
        }
        if (matches.size() > 1) return report(d, FileError::ambiguous_name, "several case variants of " + printable(comp) + " in " + path_utf8(native));
        if (matches.size() == 1) {
            const bool mdir = is_dir_no_follow(matches[0], e, link);
            if (link) return report(d, FileError::invalid_path, "symlink rejected: " + path_utf8(matches[0]));
            native = matches[0];
            is_dir = mdir;
            continue;
        }
        exists = false;
        is_dir = false;
        native = candidate;
    }
    if (count == 0) { exists = true; is_dir = true; }
    return FileError::ok;
}

const GameFileSystem::Grant* GameFileSystem::find_grant(const LogicalPath& path) const {
    if (path.components.size() != 3 || path.components[0] != kExternal) return nullptr;
    const std::string& id = path.components[1];
    if (id.empty() || id.size() > 6 || id.find_first_not_of("0123456789") != std::string::npos) return nullptr;
    const std::size_t index = static_cast<std::size_t>(std::stoul(id));
    if (index >= grants_.size()) return nullptr;
    const Grant& g = grants_[index];
    return g.name_cp932 == path.components[2] ? &g : nullptr;
}

const FileSystemConfig::PackedMember* GameFileSystem::find_packed(std::string_view hsp) const {
    for (std::size_t i = 0; i < hsp.size(); ++i) {
        const auto b = static_cast<unsigned char>(hsp[i]);
        if (cp932_is_lead_byte(b)) { ++i; continue; }
        if (is_sep(b) || b == ':') return nullptr;   // HSP matches bare member names only
    }
    for (const auto& m : packed_)
        if (ascii_ci_equal(m.name_cp932, hsp)) return &m;
    return nullptr;
}

bool GameFileSystem::is_tombstoned(std::string_view hsp) const {
    LogicalPath p;
    if (parse(hsp, p) != FileError::ok) return false;
    return std::binary_search(tombstones_.begin(), tombstones_.end(), cp932_ascii_lower(p.cp932()));
}

FileError GameFileSystem::locate_read(std::string_view hsp, Located& out, FileDiagnostic* d) const {
    out = Located{};
    if (const auto* m = find_packed(hsp)) {
        out.source = Located::Source::packed;
        out.packed = m;
        return FileError::ok;
    }
    const FileError pe = parse(hsp, out.logical);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(hsp));
    if (const Grant* g = find_grant(out.logical)) {
        out.source = Located::Source::grant;
        out.native = g->native;
        return FileError::ok;
    }
    bool exists, dir;
    FileError e = lookup(user_root_, out.logical, out.logical.components.size(), out.native, exists, dir, d);
    if (e != FileError::ok && e != FileError::not_directory) return e;
    if (e == FileError::ok && exists) {
        out.source = Located::Source::user;
        out.is_dir = dir;
        return FileError::ok;
    }
    const std::string key = cp932_ascii_lower(out.logical.cp932());
    if (std::binary_search(tombstones_.begin(), tombstones_.end(), key))
        return report(d, FileError::not_found, "deleted (tombstone): " + printable(out.logical.cp932()));
    e = lookup(game_root_, out.logical, out.logical.components.size(), out.native, exists, dir, d);
    if (e == FileError::not_directory) return report(d, FileError::not_found, "missing: " + printable(hsp));
    if (e != FileError::ok) return e;
    if (!exists) return report(d, FileError::not_found, "missing: " + printable(hsp));
    out.source = Located::Source::game;
    out.is_dir = dir;
    return FileError::ok;
}

FileError GameFileSystem::resolve_read(std::string_view hsp, fs::path& out, FileDiagnostic* d) const {
    Located loc;
    const FileError e = locate_read(hsp, loc, d);
    if (e != FileError::ok) return e;
    if (loc.source == Located::Source::packed)
        return report(d, FileError::not_found, "DPM packed member has no native path; use read_file: " + printable(hsp));
    out = loc.native;
    return FileError::ok;
}

FileError GameFileSystem::check_writable(const LogicalPath& path, FileDiagnostic* d) const {
    if (path.empty()) return report(d, FileError::not_writable, "cannot write the root");
    const std::string key = cp932_ascii_lower(path.cp932());
    for (const auto& prefix : writable_prefixes_) {
        if (!prefix.empty() && prefix.back() == '\\') {
            if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0) return FileError::ok;
            if (key + "\\" == prefix) return FileError::ok;   // the directory itself (mkdir)
        } else if (key == prefix) {
            return FileError::ok;
        }
    }
    return report(d, FileError::not_writable,
                  "write outside the 2.335 writable set (save\\ user\\ tmp\\ map\\ config.txt error.txt server.txt): " +
                      printable(path.cp932()));
}

// Ensure every parent directory exists in the user root, creating those that
// exist logically (game root or user root) with the game root's spelling.
FileError GameFileSystem::materialize_parent(const LogicalPath& path, fs::path& parent, FileDiagnostic* d) {
    parent = user_root_;
    for (std::size_t i = 0; i + 1 < path.components.size(); ++i) {
        LogicalPath prefix;
        prefix.components.assign(path.components.begin(), path.components.begin() + static_cast<long>(i) + 1);
        fs::path user_native, game_native;
        bool ue, ud, ge, gd;
        FileError e = lookup(user_root_, prefix, prefix.components.size(), user_native, ue, ud, d);
        if (e == FileError::not_directory) return report(d, FileError::not_directory, "parent is a file: " + printable(prefix.cp932()));
        if (e != FileError::ok) return e;
        if (ue) {
            if (!ud) return report(d, FileError::not_directory, "parent is a file: " + printable(prefix.cp932()));
            parent = user_native;
            continue;
        }
        e = lookup(game_root_, prefix, prefix.components.size(), game_native, ge, gd, d);
        if (e != FileError::ok && e != FileError::not_directory) return e;
        if (e != FileError::ok || !ge || !gd)
            return report(d, FileError::not_found, "parent directory missing: " + printable(prefix.cp932()));
        fs::path created = parent / game_native.filename();
        std::error_code ec;
        fs::create_directory(created, ec);
        if (ec) return report(d, FileError::io_error, "cannot materialise " + path_utf8(created) + ": " + ec.message());
        parent = created;
    }
    return FileError::ok;
}

FileError GameFileSystem::resolve_write(std::string_view hsp, fs::path& out, FileDiagnostic* d) const {
    LogicalPath p;
    const FileError pe = parse(hsp, p);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(hsp));
    if (const Grant* g = find_grant(p)) {
        if (!g->writable) return report(d, FileError::not_writable, "read-only grant: " + printable(hsp));
        out = g->native;
        return FileError::ok;
    }
    if (const FileError e = check_writable(p, d); e != FileError::ok) return e;
    bool exists, dir;
    const FileError e = lookup(user_root_, p, p.components.size(), out, exists, dir, d);
    if (e != FileError::ok) return e;
    if (exists && dir) return report(d, FileError::is_directory, "is a directory: " + printable(hsp));
    return FileError::ok;
}

FileError GameFileSystem::prepare_write(std::string_view hsp, fs::path& target, std::string& key, FileDiagnostic* d) {
    LogicalPath p;
    const FileError pe = parse(hsp, p);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(hsp));
    key.clear();
    if (const Grant* g = find_grant(p)) {
        if (!g->writable) return report(d, FileError::not_writable, "read-only grant: " + printable(hsp));
        target = g->native;
        return FileError::ok;
    }
    if (const FileError e = check_writable(p, d); e != FileError::ok) return e;
    fs::path parent;
    if (const FileError e = materialize_parent(p, parent, d); e != FileError::ok) return e;
    bool exists, dir;
    LogicalPath last;
    last.components = {p.components.back()};
    if (const FileError e = lookup(parent, last, 1, target, exists, dir, d); e != FileError::ok) return e;
    if (exists && dir) return report(d, FileError::is_directory, "is a directory: " + printable(hsp));
    if (!exists) {
        // A directory of that name in the game root also blocks the file.
        fs::path game_native;
        bool ge, gd;
        if (lookup(game_root_, p, p.components.size(), game_native, ge, gd, nullptr) == FileError::ok && ge && gd)
            return report(d, FileError::is_directory, "is a directory in the game root: " + printable(hsp));
    }
    key = cp932_ascii_lower(p.cp932());
    return FileError::ok;
}

FileError GameFileSystem::write_bytes_atomic(const fs::path& target, const std::vector<std::uint8_t>& bytes,
                                             FileDiagnostic* d) const {
    std::random_device rd;
    char suffix[32];
    std::snprintf(suffix, sizeof suffix, ".elonatmp-%08x", static_cast<unsigned>(rd()));
    fs::path temp = target;
    temp += suffix;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return report(d, FileError::io_error, "cannot create " + path_utf8(temp));
        if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            fs::remove(temp, ec);
            return report(d, FileError::io_error, "write failed " + path_utf8(temp));
        }
    }
#ifndef _WIN32
    {
        const int fd = ::open(temp.c_str(), O_RDONLY);
        if (fd >= 0) { ::fsync(fd); ::close(fd); }
    }
#endif
    std::error_code ec;
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    fs::rename(temp, target, ec);
#endif
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return report(d, FileError::io_error, "cannot replace " + path_utf8(target) + ": " + ec.message());
    }
    return FileError::ok;
}

FileError GameFileSystem::set_tombstone(const std::string& key, bool present, FileDiagnostic* d) {
    const auto it = std::lower_bound(tombstones_.begin(), tombstones_.end(), key);
    const bool has = it != tombstones_.end() && *it == key;
    if (has == present) return FileError::ok;
    std::vector<std::string> next = tombstones_;
    if (present) next.insert(next.begin() + (it - tombstones_.begin()), key);
    else next.erase(next.begin() + (it - tombstones_.begin()));
    std::vector<std::uint8_t> text;
    for (const auto& k : next) {
        std::string utf8;
        if (!cp932_to_utf8(k, utf8)) return report(d, FileError::encoding_error, "tombstone key not CP932");
        text.insert(text.end(), utf8.begin(), utf8.end());
        text.push_back('\n');
    }
    std::error_code ec;
    fs::create_directories(user_root_ / kMetaDir, ec);
    if (ec) return report(d, FileError::io_error, "cannot create metadata dir: " + ec.message());
    if (const FileError e = write_bytes_atomic(user_root_ / kMetaDir / "deleted.txt", text, d); e != FileError::ok) return e;
    tombstones_ = std::move(next);
    return FileError::ok;
}

FileError GameFileSystem::file_size(std::string_view hsp, std::int64_t& size) const {
    size = -1;
    Located loc;
    const FileError e = locate_read(hsp, loc, nullptr);
    if (e == FileError::not_found) return FileError::ok;
    if (e != FileError::ok) return e;
    if (loc.source == Located::Source::packed) { size = static_cast<std::int64_t>(loc.packed->bytes.size()); return FileError::ok; }
    if (loc.is_dir) return FileError::ok;   // HSP exist opens the file; a directory is -1 (see docs)
    std::error_code ec;
    const auto s = fs::file_size(loc.native, ec);
    if (ec) return FileError::io_error;
    size = static_cast<std::int64_t>(s);
    return FileError::ok;
}

FileError GameFileSystem::read_file(std::string_view hsp, std::vector<std::uint8_t>& out, std::uint64_t offset,
                                    std::size_t max_bytes, FileDiagnostic* d) const {
    out.clear();
    Located loc;
    const FileError e = locate_read(hsp, loc, d);
    if (e != FileError::ok) return e;
    if (loc.source == Located::Source::packed) {
        const auto& b = loc.packed->bytes;
        const std::size_t start = static_cast<std::size_t>(std::min<std::uint64_t>(offset, b.size()));
        const std::size_t count = std::min(b.size() - start, max_bytes);
        out.assign(b.begin() + static_cast<long>(start), b.begin() + static_cast<long>(start + count));
        return FileError::ok;
    }
    if (loc.is_dir) return report(d, FileError::is_directory, "is a directory: " + printable(hsp));
    return read_native(loc.native, out, offset, max_bytes, d);
}

FileError GameFileSystem::write_file(std::string_view hsp, const void* data, std::size_t size, std::int64_t offset,
                                     FileDiagnostic* d) {
    std::vector<std::uint8_t> bytes;
    if (offset >= 0) {
        // Patch semantics of bsave with offset: existing content, zero gap, overwrite.
        if (const FileError e = read_file(hsp, bytes, 0, SIZE_MAX, d); e != FileError::ok) return e;
        const std::size_t end = static_cast<std::size_t>(offset) + size;
        if (bytes.size() < end) bytes.resize(end, 0);
        if (size) std::memcpy(bytes.data() + offset, data, size);
    } else if (size) {
        bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
    }
    fs::path target;
    std::string key;
    if (const FileError e = prepare_write(hsp, target, key, d); e != FileError::ok) return e;
    if (const FileError e = write_bytes_atomic(target, bytes, d); e != FileError::ok) return e;
    if (!key.empty()) return set_tombstone(key, false, d);
    return FileError::ok;
}

FileError GameFileSystem::begin_stream_write(std::string_view hsp, fs::path& native_target, FileDiagnostic* d) {
    std::string key;
    return prepare_write(hsp, native_target, key, d);
}

FileError GameFileSystem::end_stream_write(std::string_view hsp, FileDiagnostic* d) {
    fs::path target;
    std::string key;
    if (const FileError e = prepare_write(hsp, target, key, d); e != FileError::ok) return e;
    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) return report(d, FileError::not_found, "stream target missing after close: " + path_utf8(target));
    return key.empty() ? FileError::ok : set_tombstone(key, false, d);
}

FileError GameFileSystem::copy_file(std::string_view source, std::string_view destination, FileDiagnostic* d) {
    std::vector<std::uint8_t> bytes;
    if (const FileError e = read_file(source, bytes, 0, SIZE_MAX, d); e != FileError::ok) return e;
    return write_file(destination, bytes.data(), bytes.size(), -1, d);
}

FileError GameFileSystem::remove_file(std::string_view hsp, FileDiagnostic* d) {
    LogicalPath p;
    const FileError pe = parse(hsp, p);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(hsp));
    if (find_grant(p)) return report(d, FileError::not_writable, "external grants cannot be deleted: " + printable(hsp));
    if (const FileError e = check_writable(p, d); e != FileError::ok) return e;
    const std::string key = cp932_ascii_lower(p.cp932());
    fs::path user_native, game_native;
    bool ue, ud, ge, gd;
    FileError e = lookup(user_root_, p, p.components.size(), user_native, ue, ud, d);
    if (e != FileError::ok && e != FileError::not_directory) return e;
    ue = ue && e == FileError::ok;
    e = lookup(game_root_, p, p.components.size(), game_native, ge, gd, d);
    if (e != FileError::ok && e != FileError::not_directory) return e;
    ge = ge && e == FileError::ok && !std::binary_search(tombstones_.begin(), tombstones_.end(), key);
    if ((ue && ud) || (!ue && ge && gd)) return report(d, FileError::is_directory, "delete of a directory: " + printable(hsp));
    if (!ue && !ge) return report(d, FileError::not_found, "missing: " + printable(hsp));
    if (ge) {
        // Record the tombstone first: if removing the user copy then fails, the
        // original stays hidden rather than reappearing.
        if (const FileError te = set_tombstone(key, true, d); te != FileError::ok) return te;
    }
    if (ue) {
        std::error_code ec;
        if (!fs::remove(user_native, ec) || ec) return report(d, FileError::io_error, "cannot delete " + path_utf8(user_native) + ": " + ec.message());
    }
    return FileError::ok;
}

FileError GameFileSystem::make_directory(std::string_view hsp, FileDiagnostic* d) {
    LogicalPath p;
    const FileError pe = parse(hsp, p);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(hsp));
    if (p.empty() || find_grant(p)) return report(d, FileError::not_writable, "mkdir target not allowed: " + printable(hsp));
    if (const FileError e = check_writable(p, d); e != FileError::ok) return e;
    Located loc;
    if (locate_read(hsp, loc, nullptr) == FileError::ok) return report(d, FileError::already_exists, "exists: " + printable(hsp));
    fs::path parent;
    if (const FileError e = materialize_parent(p, parent, d); e != FileError::ok) return e;
    fs::path name;
    if (!component_to_native(p.components.back(), name)) return report(d, FileError::encoding_error, "unconvertible name");
    std::error_code ec;
    if (!fs::create_directory(parent / name, ec) || ec)
        return report(d, ec ? FileError::io_error : FileError::already_exists, "mkdir failed " + path_utf8(parent / name) + (ec ? ": " + ec.message() : ""));
    return FileError::ok;
}

FileError GameFileSystem::list_directory(std::string_view pattern, int mode, std::vector<DirectoryEntry>& out,
                                         std::size_t* skipped, FileDiagnostic* d) const {
    out.clear();
    if (skipped) *skipped = 0;
    LogicalPath p;
    const FileError pe = parse_impl(pattern, p, true);
    if (pe != FileError::ok) return report(d, pe, std::string(to_string(pe)) + ": " + printable(pattern));
    // A pattern ending with a separator lists nothing in Windows (FindFirstFile fails).
    if (p.empty() || (!pattern.empty() && is_sep(static_cast<unsigned char>(pattern.back())))) return FileError::ok;
    const std::string mask = p.components.back();
    LogicalPath dir;
    dir.components.assign(p.components.begin(), p.components.end() - 1);
    const std::string dir_key = cp932_ascii_lower(dir.cp932());
    struct Item { DirectoryEntry entry; std::string key; };
    std::vector<Item> items;
    auto scan = [&](const fs::path& root, bool user) -> FileError {
        fs::path native;
        bool exists, is_dir;
        const FileError e = lookup(root, dir, dir.components.size(), native, exists, is_dir, d);
        if (e == FileError::not_directory) return FileError::ok;
        if (e != FileError::ok) return e;
        if (!exists || !is_dir) return FileError::ok;
        std::error_code ec;
        for (fs::directory_iterator it(native, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name;
            if (!native_to_component(it->path().filename(), name)) { if (skipped) ++*skipped; continue; }
            if (dir.empty() && ascii_ci_equal(name, kMetaDir)) continue;
            if (name.find(".elonatmp-") != std::string::npos) continue;   // in-flight atomic write
            bool e2, link;
            const bool entry_dir = is_dir_no_follow(it->path(), e2, link);
            if (link) { if (skipped) ++*skipped; continue; }
            const std::string key = cp932_ascii_lower(name);
            if (!user && !entry_dir) {
                const std::string full = dir_key.empty() ? key : dir_key + "\\" + key;
                if (std::binary_search(tombstones_.begin(), tombstones_.end(), full)) continue;
            }
            if (!cp932_wildcard_match(mask, name)) continue;
            // dirlist bit semantics (see header).
            bool selected = ((mode & 1) && entry_dir);   // no hidden/system attribute in the overlay
            if ((mode & 4) == 0) selected = !selected;
            if (!selected) continue;
            if (std::any_of(items.begin(), items.end(), [&](const Item& i) { return i.key == key; })) continue;
            items.push_back({DirectoryEntry{name, entry_dir, user}, key});
        }
        if (ec) return report(d, FileError::io_error, "cannot list " + path_utf8(native) + ": " + ec.message());
        return FileError::ok;
    };
    if (const FileError e = scan(user_root_, true); e != FileError::ok) return e;
    if (const FileError e = scan(game_root_, false); e != FileError::ok) return e;
    // NTFS returns names in upper-case collation order; approximate with ASCII
    // upper-case UTF-16 order (documented difference for non-ASCII names).
    auto sort_key = [](const std::string& cp932) {
        std::string utf8;
        cp932_to_utf8(cp932, utf8);
        std::u32string k;
        for (std::size_t i = 0; i < utf8.size();) {
            char32_t cp = static_cast<unsigned char>(utf8[i]);
            std::size_t l = 1;
            if (cp >= 0xf0) { cp &= 0x07; l = 4; } else if (cp >= 0xe0) { cp &= 0x0f; l = 3; } else if (cp >= 0xc0) { cp &= 0x1f; l = 2; }
            for (std::size_t j = 1; j < l && i + j < utf8.size(); ++j) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + j]) & 0x3f);
            if (cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
            k.push_back(cp);
            i += l;
        }
        return k;
    };
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        return sort_key(a.entry.name_cp932) < sort_key(b.entry.name_cp932);
    });
    for (auto& i : items) out.push_back(std::move(i.entry));
    return FileError::ok;
}

FileError GameFileSystem::grant_external_file(const fs::path& native_file, bool writable, std::string& virtual_path,
                                              FileDiagnostic* d) {
    std::error_code ec;
    const fs::path abs = fs::absolute(native_file, ec);
    if (ec) return report(d, FileError::invalid_path, "bad grant path");
    std::string name;
    if (!native_to_component(abs.filename(), name) || name.empty())
        return report(d, FileError::encoding_error, "file name not representable in CP932: " + path_utf8(abs));
    std::string why;
    if (validate_component(name, false, why) != FileError::ok) return report(d, FileError::invalid_path, why);
    bool exists, link;
    if (is_dir_no_follow(abs, exists, link) || link) return report(d, FileError::invalid_path, "grant must be a regular file path");
    grants_.push_back(Grant{abs, name, writable});
    virtual_path = virtual_root_ + "\\" + kExternal + "\\" + std::to_string(grants_.size() - 1) + "\\" + name;
    return FileError::ok;
}

// -----------------------------------------------------------------------------

FileError import_save(GameFileSystem& files, const fs::path& source, std::string_view slot, SaveImportReport& report_out,
                      int max_depth, FileDiagnostic* d) {
    report_out = SaveImportReport{};
    std::error_code ec;
    bool exists, link;
    if (!is_dir_no_follow(source, exists, link) || link)
        return report(d, FileError::not_found, "save source is not a real directory: " + path_utf8(source));
    std::string why;
    auto contains_separator = [](const std::string& name) {
        for (const auto& ch : split_chars(name))
            if (ch == "\\" || ch == "/") return true;
        return false;
    };
    if (slot.empty() || contains_separator(std::string(slot)) || slot == "." || slot == ".." ||
        validate_component(std::string(slot), false, why) != FileError::ok)
        return report(d, FileError::invalid_path, "invalid slot name " + printable(slot) + " " + why);
    const std::string logical = files.virtual_root() + "\\save\\" + std::string(slot);
    std::vector<DirectoryEntry> existing;
    if (const FileError e = files.list_directory(files.virtual_root() + "\\save\\*", dirlist_mode::all, existing, nullptr, d);
        e != FileError::ok) return e;
    for (const auto& entry : existing)
        if (cp932_ascii_lower(entry.name_cp932) == cp932_ascii_lower(slot))
            return report(d, FileError::already_exists, "slot exists: " + printable(slot));

    fs::path final_dir;
    if (const FileError e = files.resolve_write(logical, final_dir, d); e != FileError::ok) {
        if (e == FileError::is_directory) return report(d, FileError::already_exists, "slot exists");
        return e;
    }
    struct Entry { fs::path native; std::string rel; bool directory; };
    std::vector<Entry> collected;
    struct Walk { fs::path native; std::string rel; int depth; };
    std::vector<Walk> stack{{source, "", 0}};
    while (!stack.empty()) {
        Walk w = stack.back();
        stack.pop_back();
        for (fs::directory_iterator it(w.native, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name;
            if (!native_to_component(it->path().filename(), name))
                return report(d, FileError::encoding_error, "save entry not representable in CP932: " + path_utf8(it->path()));
            if (contains_separator(name) || validate_component(name, false, why) != FileError::ok)
                return report(d, FileError::invalid_path, "save entry name rejected: " + path_utf8(it->path()) + " " + why);
            const bool dir = is_dir_no_follow(it->path(), exists, link);
            if (link) return report(d, FileError::invalid_path, "symlink in save: " + path_utf8(it->path()));
            const std::string rel = w.rel.empty() ? name : w.rel + "\\" + name;
            if (dir) {
                if (w.depth + 1 > max_depth) return report(d, FileError::invalid_path, "save nested too deep: " + path_utf8(it->path()));
                stack.push_back({it->path(), rel, w.depth + 1});
            } else if (!fs::is_regular_file(it->path(), ec)) {
                return report(d, FileError::invalid_path, "special file in save: " + path_utf8(it->path()));
            }
            collected.push_back({it->path(), rel, dir});
        }
        if (ec) return report(d, FileError::io_error, "cannot read save dir: " + ec.message());
    }
    // Stage into a temporary sibling inside <user_root>/save, verify, then rename.
    const fs::path save_dir = final_dir.parent_path();
    fs::create_directories(save_dir, ec);
    if (ec) return report(d, FileError::io_error, "cannot create save dir: " + ec.message());
    fs::path staging;
    std::random_device random;
    do {
        staging = save_dir / (std::string(kMetaDir) + "-import-tmp-" + std::to_string(random()));
        if (fs::create_directory(staging, ec)) break;
        if (ec && ec != std::errc::file_exists)
            return report(d, FileError::io_error, "cannot create staging dir: " + ec.message());
    } while (true);
    struct StagingGuard {
        fs::path path;
        ~StagingGuard() {
            if (!path.empty()) { std::error_code ignored; fs::remove_all(path, ignored); }
        }
    } guard{staging};
    SaveImportReport staged_report;
    for (const auto& entry : collected) {
        const fs::path target = staging / entry.native.lexically_relative(source);
        if (entry.directory) {
            fs::create_directories(target, ec);
            if (ec) return report(d, FileError::io_error, "cannot create " + path_utf8(target));
            continue;
        }
        std::vector<std::uint8_t> bytes;
        if (const FileError e = read_native(entry.native, bytes, 0, SIZE_MAX, d); e != FileError::ok) return e;
        {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out) return report(d, FileError::io_error, "cannot write " + path_utf8(target));
        }
        std::vector<std::uint8_t> verify;
        if (read_native(target, verify, 0, SIZE_MAX, d) != FileError::ok || verify != bytes) {
            return report(d, FileError::io_error, "verification failed for " + path_utf8(target));
        }
        staged_report.files.push_back({entry.rel, bytes.size(), sha256_hex(bytes.data(), bytes.size())});
    }
    // Publish only if the slot is still absent, including when importing empty saves.
#ifdef _WIN32
    if (!MoveFileExW(staging.c_str(), final_dir.c_str(), 0))
        ec = std::error_code(GetLastError(), std::system_category());
#elif defined(__APPLE__)
    if (::renamex_np(staging.c_str(), final_dir.c_str(), RENAME_EXCL) != 0)
        ec = std::error_code(errno, std::generic_category());
#elif defined(__linux__)
    if (::renameat2(AT_FDCWD, staging.c_str(), AT_FDCWD, final_dir.c_str(), RENAME_NOREPLACE) != 0)
        ec = std::error_code(errno, std::generic_category());
#else
    ec = std::make_error_code(std::errc::operation_not_supported);
#endif
    if (ec == std::errc::file_exists || ec == std::errc::directory_not_empty)
        return report(d, FileError::already_exists, "slot appeared during import");
    if (ec) return report(d, FileError::io_error, "cannot move staged save into place: " + ec.message());
    guard.path.clear();
    staged_report.destination = final_dir;
    report_out = std::move(staged_report);
    return FileError::ok;
}

}  // namespace elona::platform

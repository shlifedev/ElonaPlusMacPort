#pragma once
// Filesystem policy for the native ElonaPlus 2.335 host.
//
// Model: a read-only original game root overlaid by a writable user root.
//   * HSP paths are CP932 byte strings. The game builds every path as
//     `exedir + "user\\..."` where exedir = dirinfo(1) + "\\"; the host must make
//     dirinfo(1)/dir_exe return virtual_root() so those strings parse here.
//   * Separators '\\' and '/' are recognised only at single-byte positions; the
//     trail byte of a CP932 double-byte character (e.g. 0x95 0x5C) is never a
//     separator. Repeated separators ("exedir + \"\\graphic\"") collapse.
//   * Reads: user root first (copy-on-write overrides), then the game root.
//   * Writes/mkdir: only inside the user root, and only below the writable
//     prefixes (static scan of 2.335: save\ user\ tmp\ map\ config.txt
//     error.txt server.txt). Parent directories that exist only in the game
//     root are materialised in the user root (copy-on-write).
//   * delete: removes the user-root file and, if the game root also has that
//     file, records a persistent tombstone (user_root/.elonaplus/deleted.txt) so
//     the original stays hidden after restart. Writing the path again clears
//     the tombstone. The original file itself is never deleted.
//   * DPM packed members (scene1.hsp, scene2.hsp) are matched first for bare
//     relative names, like HSP's filepack, and are read-only.
//   * _fdialog results outside the namespace are reachable only through an
//     explicit per-file grant (grant_external_file), never by raw host paths.
//   * The game root is never opened for writing.
//   * Rejected: "..", escaping the root, other absolute paths/drives/UNC, NUL,
//     control bytes, <>:"|?* (wildcards only in list patterns), Windows device
//     names, truncated CP932, symlinks inside the roots. Trailing dots/spaces
//     of a component are stripped like Win32 ("a.txt." == "a.txt").
//   * ASCII case-insensitive lookup fallback (Windows semantics on case-sensitive
//     volumes); ambiguous matches fail instead of picking one.
//   * Writes are atomic (temporary file in the same directory + rename), so a
//     failure keeps the previous save. Bytes are written verbatim: int32 arrays
//     and save data are never re-encoded.
// No function here reports success for an unimplemented operation.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace elona::platform {

enum class FileError {
    ok = 0,
    invalid_path,      // malformed CP932, forbidden byte/name, wildcard misuse
    outside_root,      // "..", other drive, absolute path not under virtual root
    not_found,
    not_writable,      // game-root-only target, or outside writable prefixes
    already_exists,
    is_directory,
    not_directory,
    ambiguous_name,    // several case-insensitive matches on a case-sensitive volume
    encoding_error,    // native filename not representable in CP932 (or vice versa)
    io_error,
    invalid_config,
};
const char* to_string(FileError error);

struct FileSystemConfig {
    std::filesystem::path game_root;   // original 2.335 install (never written)
    std::filesystem::path user_root;   // writable; created if missing
    std::string virtual_root = "C:\\ElonaPlus";   // CP932/ASCII, no trailing separator
    // Logical (CP932, '\\'-separated, ASCII case-insensitive) prefixes that may be
    // written. Entries ending in '\\' are directories, others exact files.
    std::vector<std::string> writable_prefixes = {
        "save\\", "user\\", "tmp\\", "map\\", "config.txt", "error.txt", "server.txt"};
    // Read-only members of the executable's DPM archive (name -> bytes).
    struct PackedMember { std::string name_cp932; std::vector<std::uint8_t> bytes; };
    std::vector<PackedMember> packed_members;
};

struct LogicalPath {
    std::vector<std::string> components;   // CP932 bytes, no separators
    std::string cp932() const;             // components joined with '\\'
    bool empty() const { return components.empty(); }
};

struct DirectoryEntry {
    std::string name_cp932;
    bool is_directory = false;
    bool from_user_root = false;
};

struct FileDiagnostic {
    FileError error = FileError::ok;
    std::string detail;   // UTF-8 human-readable cause (path, OS error)
};

// HSP dirlist mode bits (supio_win.cpp dirlist): bit0 selects directories,
// bit1 selects hidden/system, bit2 inverts (keep selected instead of excluding).
// 0 = all, 1 = files only, 5 = directories only (2.335 uses 0, 1 and 5).
// There is no hidden/system attribute on the overlay: only our reserved
// metadata directory is treated as hidden (and is never listed anyway).
namespace dirlist_mode {
constexpr int all = 0;
constexpr int files_only = 1;
constexpr int directories_only = 5;
}

class GameFileSystem {
public:
    static FileError open(const FileSystemConfig& config,
                          std::unique_ptr<GameFileSystem>& out,
                          FileDiagnostic* diagnostic = nullptr);

    const std::string& virtual_root() const { return virtual_root_; }
    const std::filesystem::path& game_root() const { return game_root_; }
    const std::filesystem::path& user_root() const { return user_root_; }

    // HSP path (absolute under virtual_root, or relative to it) -> logical path.
    FileError parse(std::string_view hsp_path_cp932, LogicalPath& out) const;

    // Native path to read: user override if present, else game asset.
    FileError resolve_read(std::string_view hsp_path_cp932, std::filesystem::path& out,
                           FileDiagnostic* diagnostic = nullptr) const;
    // Native path to write inside the user root (parent dirs not created). Checks
    // policy only; prefer write_file()/copy_file() which are atomic.
    FileError resolve_write(std::string_view hsp_path_cp932, std::filesystem::path& out,
                            FileDiagnostic* diagnostic = nullptr) const;

    // Grant one host file chosen in a platform file dialog (_fdialog). Returns a
    // virtual CP932 path "<virtual_root>\\@external\\<n>\\<name>" usable with
    // resolve_read/read_file and, when writable, resolve_write/write_file.
    FileError grant_external_file(const std::filesystem::path& native_file, bool writable,
                                  std::string& virtual_path_cp932, FileDiagnostic* diagnostic = nullptr);

    bool is_tombstoned(std::string_view hsp_path_cp932) const;

    // Streaming writers (e.g. the z.hpi gzip zOpen write mode): applies the write
    // policy, materialises parent directories in the user root and returns the
    // native target to open directly (not atomic). After the stream is closed
    // successfully call end_stream_write() so a tombstone on that path is cleared.
    FileError begin_stream_write(std::string_view hsp_path_cp932, std::filesystem::path& native_target,
                                 FileDiagnostic* diagnostic = nullptr);
    FileError end_stream_write(std::string_view hsp_path_cp932, FileDiagnostic* diagnostic = nullptr);

    // exist: file size, or -1 when missing or a directory (HSP opens the file).
    FileError file_size(std::string_view hsp_path_cp932, std::int64_t& size) const;
    // bload: read [offset, offset+max_bytes) (max_bytes = SIZE_MAX: to EOF).
    FileError read_file(std::string_view hsp_path_cp932, std::vector<std::uint8_t>& out,
                        std::uint64_t offset = 0, std::size_t max_bytes = SIZE_MAX,
                        FileDiagnostic* diagnostic = nullptr) const;
    // bsave/notesave: offset < 0 replaces the file atomically; offset >= 0 patches
    // an existing file (copy-on-write from the game root first) atomically.
    FileError write_file(std::string_view hsp_path_cp932, const void* data, std::size_t size,
                         std::int64_t offset = -1, FileDiagnostic* diagnostic = nullptr);
    // bcopy: source via overlay, destination in user root, atomic.
    FileError copy_file(std::string_view source_cp932, std::string_view destination_cp932,
                        FileDiagnostic* diagnostic = nullptr);
    // delete: removes the user-root copy and tombstones a game-root original
    // (logical delete; the original file is untouched). Missing -> not_found.
    FileError remove_file(std::string_view hsp_path_cp932, FileDiagnostic* diagnostic = nullptr);
    // mkdir: creates exactly one directory level in the user root; the parent
    // must exist logically. Existing (either root) -> already_exists.
    FileError make_directory(std::string_view hsp_path_cp932, FileDiagnostic* diagnostic = nullptr);
    // dirlist: pattern's last component may contain * and ?; merged, de-duplicated
    // (ASCII case-insensitive) view, sorted by CP932 bytes. Native names that are
    // not representable in CP932 are skipped and counted in `skipped`.
    FileError list_directory(std::string_view hsp_pattern_cp932, int hsp_mode,
                             std::vector<DirectoryEntry>& out, std::size_t* skipped = nullptr,
                             FileDiagnostic* diagnostic = nullptr) const;

private:
    GameFileSystem() = default;
    struct Located;
    struct Grant { std::filesystem::path native; std::string name_cp932; bool writable; };
    FileError parse_impl(std::string_view hsp, LogicalPath& out, bool allow_wildcard_last) const;
    FileError lookup(const std::filesystem::path& root, const LogicalPath& path, std::size_t count,
                     std::filesystem::path& native, bool& exists, bool& is_dir, FileDiagnostic* d) const;
    FileError locate_read(std::string_view hsp, Located& out, FileDiagnostic* d) const;
    FileError check_writable(const LogicalPath& path, FileDiagnostic* d) const;
    FileError materialize_parent(const LogicalPath& path, std::filesystem::path& parent, FileDiagnostic* d);
    FileError prepare_write(std::string_view hsp, std::filesystem::path& target, std::string& key,
                            FileDiagnostic* d);
    FileError write_bytes_atomic(const std::filesystem::path& target,
                                 const std::vector<std::uint8_t>& bytes, FileDiagnostic* d) const;
    const Grant* find_grant(const LogicalPath& path) const;
    const FileSystemConfig::PackedMember* find_packed(std::string_view hsp) const;
    FileError set_tombstone(const std::string& key, bool present, FileDiagnostic* d);

    std::filesystem::path game_root_;
    std::filesystem::path user_root_;
    std::string virtual_root_;
    std::vector<std::string> writable_prefixes_;   // ASCII-lowercased
    std::vector<FileSystemConfig::PackedMember> packed_;
    std::vector<std::string> tombstones_;          // sorted lowercase logical keys
    std::vector<Grant> grants_;
};

// CP932-aware ASCII lowercase (trail bytes of double-byte characters untouched).
std::string cp932_ascii_lower(std::string_view cp932);

// HSP '*'/'?' wildcard match on CP932 bytes, ASCII case-insensitive, DBCS-aware.
bool cp932_wildcard_match(std::string_view pattern, std::string_view name);

// ---- Save import foundation ---------------------------------------------------
// Copy an original 2.335 save directory (e.g. <original>/save/<player>) into
// <user_root>/save/<slot> byte-for-byte. The source is only read. Refuses to
// overwrite an existing slot, symlinks, nested dirs deeper than `max_depth`, or
// names not representable in CP932. Copies into a temporary sibling first,
// verifies size+SHA-256 of every file, then renames into place.
struct ImportedFile {
    std::string relative_cp932;
    std::uint64_t size = 0;
    std::string sha256_hex;
};
struct SaveImportReport {
    std::vector<ImportedFile> files;
    std::filesystem::path destination;
};
FileError import_save(GameFileSystem& files, const std::filesystem::path& source_directory,
                      std::string_view slot_name_cp932, SaveImportReport& report,
                      int max_depth = 4, FileDiagnostic* diagnostic = nullptr);

std::string sha256_hex(const void* data, std::size_t size);

}  // namespace elona::platform

// GameFileSystem policy tests on a synthetic game root (no original assets).
// argv[1]: scratch directory (recreated). The synthetic game root is hashed
// before and after; any change fails the test.
#include "platform/encoding.h"
#include "platform/game_files.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <string>
#include <vector>

using namespace elona::platform;
namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s == %s (got %s)\n", __FILE__, __LINE__, #a, #b, to_string(_a)); } } while (0)

static fs::path native(const std::string& utf8) { return fs::u8path(utf8); }

static void put(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << data;
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

static std::map<std::string, std::string> tree_hash(const fs::path& root) {
    std::map<std::string, std::string> m;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        const std::string rel = fs::relative(e.path(), root).u8string();
        if (e.is_directory()) m[rel] = "<dir>";
        else { const std::string d = slurp(e.path()); m[rel] = sha256_hex(d.data(), d.size()); }
    }
    return m;
}

static std::string vr(const std::string& rel) { return "C:\\ElonaPlus\\" + rel; }
static std::string cp(const std::string& utf8) { return utf8_to_cp932_or_throw(utf8); }

static std::string read_str(GameFileSystem& f, const std::string& hsp, FileError* err = nullptr) {
    std::vector<std::uint8_t> b;
    const FileError e = f.read_file(hsp, b);
    if (err) *err = e;
    return std::string(b.begin(), b.end());
}

static bool listed(const std::vector<DirectoryEntry>& v, const std::string& name) {
    for (const auto& e : v) if (e.name_cp932 == name) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: test_game_files SCRATCH\n"); return 2; }
    const fs::path scratch = fs::absolute(argv[1]);
    fs::remove_all(scratch);
    const fs::path game = scratch / native("game root 原本"), user = scratch / native("user データ");
    put(game / "original" / "config.txt", "default-config");
    put(game / "user" / "music" / "musiclist.txt", "music");
    put(game / "user" / "lastwords.txt", "lastwords");
    put(game / "user" / "npc1.npc", "npc");
    put(game / "save" / "readme.txt", "readme");
    put(game / "graphic" / "Item.bmp", "BMitem");
    put(game / "data" / "x.bin", "data");
    put(game / "map" / "town.map", "map");
    put(game / native("user") / native("表.txt"), "hyou");          // CP932 95 5C: trail byte is '\'
    put(game / native("user") / native("か\xe3\x82\x99.txt"), "nfd");   // NFD name on disk
    const auto before = tree_hash(game);

    FileSystemConfig cfg;
    cfg.game_root = game;
    cfg.user_root = user;
    cfg.packed_members.push_back({"scene1.hsp", {'s', '1'}});
    std::unique_ptr<GameFileSystem> fsys;
    FileDiagnostic diag;
    CHECK_EQ(GameFileSystem::open(cfg, fsys, &diag), FileError::ok);
    if (!fsys) { std::fprintf(stderr, "open failed: %s\n", diag.detail.c_str()); return 1; }
    GameFileSystem& f = *fsys;

    // Root isolation.
    {
        FileSystemConfig bad = cfg;
        bad.user_root = game / "user";
        std::unique_ptr<GameFileSystem> x;
        CHECK_EQ(GameFileSystem::open(bad, x), FileError::invalid_config);
        bad.user_root = user;
        bad.game_root = user / "sub";
        fs::create_directories(bad.game_root);
        CHECK_EQ(GameFileSystem::open(bad, x), FileError::invalid_config);
        fs::remove(bad.game_root);
    }

    // ---- parsing -------------------------------------------------------------
    LogicalPath lp;
    CHECK_EQ(f.parse(vr("user\\npc1.npc"), lp), FileError::ok);
    CHECK(lp.cp932() == "user\\npc1.npc");
    CHECK_EQ(f.parse("c:/elonaplus/User//Graphic\\\\x.bmp", lp), FileError::ok);   // case, '/', doubled separators
    CHECK(lp.components.size() == 3 && lp.components[2] == "x.bmp");
    CHECK_EQ(f.parse("C:\\ElonaPlus\\\\graphic\\a.bmp", lp), FileError::ok);       // exedir + "\\graphic\\"
    CHECK(lp.cp932() == "graphic\\a.bmp");
    CHECK_EQ(f.parse("relative\\x.txt", lp), FileError::ok);
    CHECK_EQ(f.parse(vr("user\\") + cp("表.txt"), lp), FileError::ok);           // trail 0x5C is not a separator
    CHECK(lp.components.size() == 2 && lp.components[1] == cp("表.txt"));
    CHECK_EQ(f.parse(vr("save\\..\\config.txt"), lp), FileError::ok);
    CHECK(lp.cp932() == "config.txt");
    CHECK_EQ(f.parse(vr("..\\x"), lp), FileError::outside_root);
    CHECK_EQ(f.parse(vr("save\\..\\..\\x"), lp), FileError::outside_root);
    CHECK_EQ(f.parse("C:\\ElonaPlusX\\a", lp), FileError::outside_root);
    CHECK_EQ(f.parse("D:\\x", lp), FileError::outside_root);
    CHECK_EQ(f.parse("\\\\server\\share\\x", lp), FileError::outside_root);
    CHECK_EQ(f.parse("\\x", lp), FileError::outside_root);
    CHECK_EQ(f.parse(vr("user\\\x82"), lp), FileError::invalid_path);            // truncated lead
    CHECK_EQ(f.parse(vr("user\\con.txt"), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(vr("user\\a:b"), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(vr("user\\x. .."), lp), FileError::ok);                      // Win32 strips trailing dots/spaces
    CHECK(lp.cp932() == "user\\x");
    CHECK_EQ(f.parse(vr("user\\...\\a"), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(vr("user\\*.npc"), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(vr(".elonaplus\\deleted.txt"), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(std::string("user\0x", 6), lp), FileError::invalid_path);
    CHECK_EQ(f.parse(vr("user\\\x80\x41"), lp), FileError::ok);                   // 0x80 single byte is valid CP932
    CHECK_EQ(f.parse(vr("user\\\xa0"), lp), FileError::encoding_error);           // 0xA0 is not

    // ---- reads ---------------------------------------------------------------
    std::filesystem::path p;
    CHECK_EQ(f.resolve_read(vr("original\\config.txt"), p), FileError::ok);
    CHECK(p == game / "original" / "config.txt");
    CHECK_EQ(f.resolve_read(vr("GRAPHIC\\item.BMP"), p), FileError::ok);          // case-insensitive
    CHECK(slurp(p) == "BMitem");
    CHECK(read_str(f, vr("user\\") + cp("表.txt")) == "hyou");
    CHECK(read_str(f, vr("user\\") + cp("が.txt")) == "nfd");                     // NFC CP932 name finds NFD file
    CHECK(read_str(f, "scene1.hsp") == "s1");                                     // DPM member, bare name
    FileError e;
    read_str(f, vr("scene1.hsp"), &e);
    CHECK_EQ(e, FileError::not_found);                                            // exedir + name is disk, not DPM
    CHECK_EQ(f.resolve_read("scene1.hsp", p), FileError::not_found);              // packed has no native path
    std::int64_t size = 0;
    CHECK_EQ(f.file_size("scene1.hsp", size), FileError::ok);
    CHECK(size == 2);
    CHECK_EQ(f.file_size(vr("user\\npc1.npc"), size), FileError::ok);
    CHECK(size == 3);
    CHECK_EQ(f.file_size(vr("user\\npc1.npc. "), size), FileError::ok);
    CHECK(size == 3);
    CHECK_EQ(f.file_size(vr("user\\missing.npc"), size), FileError::ok);
    CHECK(size == -1);
    CHECK_EQ(f.file_size(vr("user"), size), FileError::ok);
    CHECK(size == -1);                                                            // directory
    std::vector<std::uint8_t> buf;
    CHECK_EQ(f.read_file(vr("user"), buf), FileError::is_directory);
    CHECK_EQ(f.read_file(vr("data\\x.bin"), buf, 1, 2), FileError::ok);           // bload offset/size
    CHECK(std::string(buf.begin(), buf.end()) == "at");

    // ---- writes: confined to the user root ------------------------------------
    CHECK_EQ(f.copy_file(vr("original\\config.txt"), vr("config.txt")), FileError::ok);   // first-run bcopy
    CHECK(slurp(user / "config.txt") == "default-config");
    CHECK(!fs::exists(game / "config.txt"));
    const std::string cfgdata = "cfg2";
    CHECK_EQ(f.write_file(vr("config.txt"), cfgdata.data(), cfgdata.size()), FileError::ok);
    CHECK(read_str(f, vr("config.txt")) == "cfg2");
    CHECK_EQ(f.write_file(vr("data\\x.bin"), "z", 1, -1, &diag), FileError::not_writable);
    CHECK(diag.detail.find("data") != std::string::npos);
    CHECK_EQ(f.write_file(vr("newroot.txt"), "z", 1), FileError::not_writable);
    CHECK_EQ(f.write_file("D:\\evil.txt", "z", 1), FileError::outside_root);
    CHECK_EQ(f.write_file(vr("..\\evil.txt"), "z", 1), FileError::outside_root);
    CHECK_EQ(f.write_file(vr("user"), "z", 1), FileError::is_directory);
    // COW parent materialisation: user\music exists only in the game root.
    CHECK_EQ(f.write_file(vr("user\\music\\new.txt"), "n", 1), FileError::ok);
    CHECK(slurp(user / "user" / "music" / "new.txt") == "n");
    CHECK(read_str(f, vr("user\\music\\musiclist.txt")) == "music");             // merged view still sees original
    CHECK_EQ(f.write_file(vr("save\\nope\\a.txt"), "z", 1), FileError::not_found);   // HSP: missing parent fails
    CHECK_EQ(f.write_file(vr("tmp\\a.txt"), "z", 1), FileError::not_found);          // tmp\ absent in this root
    CHECK_EQ(f.make_directory(vr("tmp")), FileError::ok);
    CHECK_EQ(f.write_file(vr("tmp\\a.txt"), "z", 1), FileError::ok);
    CHECK_EQ(f.write_file(vr("map\\town.map"), "edited", 6), FileError::ok);           // map editor COW
    // bsave with offset: copy-on-write patch, zero gap, original untouched.
    CHECK_EQ(f.write_file(vr("user\\lastwords.txt"), "XY", 2, 4), FileError::ok);
    CHECK(read_str(f, vr("user\\lastwords.txt")) == "lastXYrds");
    CHECK_EQ(f.write_file(vr("tmp\\a.txt"), "Q", 1, 3), FileError::ok);
    CHECK(read_str(f, vr("tmp\\a.txt")) == std::string("z\0\0Q", 4));
    CHECK_EQ(f.write_file(vr("tmp\\none.txt"), "Q", 1, 0), FileError::not_found);
    // int32 save data is written verbatim (little-endian host bytes, no re-encoding).
    const std::int32_t ints[] = {0, -1, INT32_MIN, 0x12345678};
    CHECK_EQ(f.write_file(vr("tmp\\cdata.s1"), ints, sizeof ints), FileError::ok);
    CHECK_EQ(f.read_file(vr("tmp\\cdata.s1"), buf), FileError::ok);
    CHECK(buf.size() == 16 && std::memcmp(buf.data(), ints, 16) == 0);
    CHECK(buf[12] == 0x78 && buf[15] == 0x12);
    CHECK(fs::file_size(user / "tmp" / "cdata.s1") == 16);
    // Binary payload with every byte value survives.
    std::string all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
    CHECK_EQ(f.write_file(vr("tmp\\bytes.bin"), all.data(), all.size()), FileError::ok);
    CHECK(read_str(f, vr("tmp\\bytes.bin")) == all);
    std::filesystem::path wp;
    CHECK_EQ(f.resolve_write(vr("user\\npc9.npc"), wp), FileError::ok);
    CHECK(wp.parent_path() == user / "user");
    CHECK_EQ(f.resolve_write(vr("graphic\\x.bmp"), wp), FileError::not_writable);

    // ---- delete / tombstones ----------------------------------------------------
    CHECK_EQ(f.remove_file(vr("config.txt")), FileError::ok);                     // user-only file: physical delete
    CHECK(!fs::exists(user / "config.txt"));
    CHECK(!f.is_tombstoned(vr("config.txt")));
    CHECK_EQ(f.remove_file(vr("config.txt")), FileError::not_found);
    CHECK_EQ(f.remove_file(vr("user\\npc1.npc")), FileError::ok);                 // game-only: logical delete
    CHECK(fs::exists(game / "user" / "npc1.npc"));
    CHECK(f.is_tombstoned(vr("user\\npc1.npc")));
    read_str(f, vr("user\\npc1.npc"), &e);
    CHECK_EQ(e, FileError::not_found);
    CHECK_EQ(f.file_size(vr("USER\\NPC1.NPC"), size), FileError::ok);
    CHECK(size == -1);
    CHECK_EQ(f.remove_file(vr("user\\npc1.npc")), FileError::not_found);
    CHECK_EQ(f.remove_file(vr("user\\lastwords.txt")), FileError::ok);            // override + original
    CHECK(!fs::exists(user / "user" / "lastwords.txt"));
    read_str(f, vr("user\\lastwords.txt"), &e);
    CHECK_EQ(e, FileError::not_found);                                            // original must not resurface
    CHECK_EQ(f.remove_file(vr("user")), FileError::is_directory);
    CHECK_EQ(f.remove_file(vr("data\\x.bin")), FileError::not_writable);
    std::vector<DirectoryEntry> list;
    CHECK_EQ(f.list_directory(vr("user\\*.npc"), dirlist_mode::all, list), FileError::ok);
    CHECK(!listed(list, "npc1.npc"));
    // Persistence across restart.
    fsys.reset();
    CHECK_EQ(GameFileSystem::open(cfg, fsys), FileError::ok);
    GameFileSystem& g = *fsys;
    read_str(g, vr("user\\npc1.npc"), &e);
    CHECK_EQ(e, FileError::not_found);
    read_str(g, vr("user\\lastwords.txt"), &e);
    CHECK_EQ(e, FileError::not_found);
    CHECK(slurp(user / ".elonaplus" / "deleted.txt") == "user\\lastwords.txt\nuser\\npc1.npc\n");
    // Writing again clears the tombstone and shows the new content.
    CHECK_EQ(g.write_file(vr("user\\npc1.npc"), "new", 3), FileError::ok);
    CHECK(read_str(g, vr("user\\npc1.npc")) == "new");
    CHECK(!g.is_tombstoned(vr("user\\npc1.npc")));
    CHECK(slurp(user / ".elonaplus" / "deleted.txt") == "user\\lastwords.txt\n");
    CHECK_EQ(g.copy_file(vr("original\\config.txt"), vr("user\\lastwords.txt")), FileError::ok);
    CHECK(read_str(g, vr("user\\lastwords.txt")) == "default-config");

    // ---- streaming writers (zOpen write) ------------------------------------------
    CHECK_EQ(g.remove_file(vr("user\\music\\musiclist.txt")), FileError::ok);       // tombstone the original
    fs::remove_all(user / "user" / "music");                                        // parent only in game root
    std::filesystem::path stream_target;
    CHECK_EQ(g.begin_stream_write(vr("user\\music\\musiclist.txt"), stream_target), FileError::ok);
    CHECK(fs::is_directory(stream_target.parent_path()) && stream_target.parent_path() == user / "user" / "music");
    read_str(g, vr("user\\music\\musiclist.txt"), &e);
    CHECK_EQ(e, FileError::not_found);                                              // still hidden until written
    put(stream_target, "streamed");
    CHECK_EQ(g.end_stream_write(vr("user\\music\\musiclist.txt")), FileError::ok);
    CHECK(!g.is_tombstoned(vr("user\\music\\musiclist.txt")));
    CHECK(read_str(g, vr("user\\music\\musiclist.txt")) == "streamed");
    CHECK_EQ(g.begin_stream_write(vr("data\\x.bin"), stream_target), FileError::not_writable);
    CHECK_EQ(g.end_stream_write(vr("user\\never.t")), FileError::not_found);

    // ---- dirlist ------------------------------------------------------------------
    CHECK_EQ(g.make_directory(vr("save\\player1")), FileError::ok);
    CHECK_EQ(g.make_directory(vr("save\\player1")), FileError::already_exists);
    CHECK_EQ(g.make_directory(vr("user")), FileError::already_exists);           // exists in game root
    CHECK_EQ(g.make_directory(vr("save\\a\\b")), FileError::not_found);
    CHECK_EQ(g.make_directory(vr("graphic\\new")), FileError::not_writable);
    CHECK_EQ(g.list_directory(vr("save\\*"), dirlist_mode::directories_only, list), FileError::ok);
    CHECK(list.size() == 1 && list[0].name_cp932 == "player1" && list[0].is_directory);
    CHECK_EQ(g.list_directory(vr("save\\*"), dirlist_mode::files_only, list), FileError::ok);
    CHECK(list.size() == 1 && list[0].name_cp932 == "readme.txt");
    CHECK_EQ(g.list_directory(vr("save\\*"), dirlist_mode::all, list), FileError::ok);
    CHECK(list.size() == 2);
    CHECK_EQ(g.list_directory(vr("user\\*.*"), dirlist_mode::files_only, list), FileError::ok);
    CHECK(listed(list, "npc1.npc") && listed(list, "lastwords.txt") && listed(list, "npc9.npc") == false);
    CHECK(listed(list, cp("表.txt")) && listed(list, cp("が.txt")));
    CHECK_EQ(g.list_directory(vr("user\\*"), dirlist_mode::directories_only, list), FileError::ok);
    CHECK(list.size() == 1 && list[0].name_cp932 == "music");                    // merged, de-duplicated
    CHECK_EQ(g.list_directory(vr("user\\*.NPC"), dirlist_mode::all, list), FileError::ok);
    CHECK(list.size() == 1 && list[0].from_user_root);
    CHECK_EQ(g.list_directory(vr("*"), dirlist_mode::directories_only, list), FileError::ok);
    CHECK(!listed(list, ".elonaplus"));
    CHECK_EQ(g.list_directory(vr("nothing\\*"), dirlist_mode::all, list), FileError::ok);
    CHECK(list.empty());
    CHECK(cp932_wildcard_match("npc?.t", "npc1.t") && !cp932_wildcard_match("npc?.t", "npc12.t"));
    CHECK(cp932_wildcard_match("*_tmp_*.npc", "a_tmp_3.NPC"));
    CHECK(cp932_wildcard_match("plan*.bmp", "plan.bmp"));
    CHECK(cp932_wildcard_match("x.*", "x"));
    CHECK(cp932_wildcard_match(cp("表*"), cp("表.txt")) && !cp932_wildcard_match(cp("表*"), cp("ソ")));
    CHECK(cp932_ascii_lower(cp("アA")) == cp("ア") + "a");                      // 0x83 0x41: trail 'A' untouched
    CHECK(cp932_ascii_lower("\x83\x41") == "\x83\x41");

    // ---- symlinks are rejected --------------------------------------------------------
#ifndef _WIN32
    fs::create_directory_symlink(scratch, user / "user" / "escape");
    CHECK_EQ(g.resolve_read(vr("user\\escape\\x"), p), FileError::invalid_path);
    CHECK_EQ(g.write_file(vr("user\\escape\\x.txt"), "z", 1), FileError::invalid_path);
    CHECK(!fs::exists(scratch / "x.txt"));
    fs::remove(user / "user" / "escape");
#endif

    // ---- external grants (_fdialog) -----------------------------------------------------
    const fs::path outside = scratch / "outside";
    put(outside / "export.pum", "pum");
    std::string granted;
    CHECK_EQ(g.grant_external_file(outside / "export.pum", false, granted), FileError::ok);
    CHECK(granted == "C:\\ElonaPlus\\@external\\0\\export.pum");
    CHECK(read_str(g, granted) == "pum");
    CHECK_EQ(g.write_file(granted, "x", 1), FileError::not_writable);
    CHECK_EQ(g.grant_external_file(outside / "new.pum", true, granted), FileError::ok);
    CHECK_EQ(g.write_file(granted, "w", 1), FileError::ok);
    CHECK(slurp(outside / "new.pum") == "w");
    CHECK_EQ(g.write_file("C:\\ElonaPlus\\@external\\1\\other.pum", "x", 1), FileError::not_writable);   // name must match

    // ---- save import ------------------------------------------------------------------------
    const fs::path original_save = scratch / "orig-save" / "sav_tester";
    std::string bin;
    for (int i = 0; i < 1000; ++i) bin.push_back(static_cast<char>((i * 7) & 0xff));
    put(original_save / "cdata.s1", bin);
    put(original_save / "header.txt", "header");
    put(original_save / "sub" / "m4_1.t", "t");
    put(original_save / "sub" / native("表"), bin);
    fs::create_directories(original_save / native("能"));
    const fs::path sentinel = user / "save" / ".elonaplus-import-tmp" / "keep";
    put(sentinel, "existing directory");
    const auto save_before = tree_hash(original_save.parent_path());
    SaveImportReport rep;
    CHECK_EQ(import_save(g, original_save, "sav_tester", rep, 4, &diag), FileError::ok);
    if (failures) std::fprintf(stderr, "import diag: %s\n", diag.detail.c_str());
    CHECK(rep.files.size() == 4);
    CHECK(tree_hash(rep.destination) == tree_hash(original_save));
    using Digest = std::pair<std::uintmax_t, std::string>;
    std::map<std::string, Digest> manifest, expected;
    for (const auto& file : rep.files) manifest[file.relative_cp932] = {file.size, file.sha256_hex};
    for (const auto& entry : fs::recursive_directory_iterator(original_save)) {
        if (!entry.is_regular_file()) continue;
        const auto bytes = slurp(entry.path());
        auto rel = entry.path().lexically_relative(original_save).generic_u8string();
        std::replace(rel.begin(), rel.end(), '/', '\\');
        expected[cp(rel)] = {entry.file_size(), sha256_hex(bytes.data(), bytes.size())};
    }
    CHECK(manifest == expected);
    CHECK(slurp(sentinel) == "existing directory");
    CHECK(slurp(user / "save" / "sav_tester" / "cdata.s1") == bin);
    CHECK(read_str(g, vr("save\\sav_tester\\sub\\m4_1.t")) == "t");
    for (const auto& file : rep.files)
        if (file.relative_cp932 == "cdata.s1") CHECK(file.sha256_hex == sha256_hex(bin.data(), bin.size()) && file.size == 1000);
    CHECK_EQ(import_save(g, original_save, "sav_tester", rep), FileError::already_exists);
    CHECK_EQ(import_save(g, original_save, "SAV_TESTER", rep), FileError::already_exists);
    CHECK_EQ(import_save(g, original_save, "..", rep), FileError::invalid_path);
    CHECK_EQ(import_save(g, scratch / "missing", "x", rep), FileError::not_found);
    CHECK(tree_hash(original_save.parent_path()) == save_before);
    auto start_import = [&](fs::path src, std::string slot, std::shared_future<void> start) {
        return std::async(std::launch::async, [&, src, slot, start] {
            SaveImportReport result;
            start.wait();
            return import_save(g, src, slot, result);
        });
    };
    const auto second = scratch / "second-save";
    put(second / "header.txt", "different save");
    std::promise<void> ready;
    auto start = ready.get_future().share();
    auto first_import = start_import(original_save, "concurrent1", start);
    auto second_import = start_import(second, cp("表"), start);
    ready.set_value();
    CHECK_EQ(first_import.get(), FileError::ok);
    CHECK_EQ(second_import.get(), FileError::ok);
    CHECK(tree_hash(user / "save" / "concurrent1") == tree_hash(original_save));
    CHECK(tree_hash(user / "save" / native("表")) == tree_hash(second));

    const auto empty = scratch / "empty-save";
    fs::create_directories(empty);
    std::promise<void> shared_slot_ready;
    auto shared_start = shared_slot_ready.get_future().share();
    first_import = start_import(empty, "shared", shared_start);
    second_import = start_import(second, "shared", shared_start);
    shared_slot_ready.set_value();
    const auto a = first_import.get(), b = second_import.get();
    CHECK((a == FileError::ok && b == FileError::already_exists) ||
          (b == FileError::ok && a == FileError::already_exists));
    CHECK(tree_hash(user / "save" / "shared") == tree_hash(a == FileError::ok ? empty : second));
    CHECK(slurp(sentinel) == "existing directory");
    for (const auto& e : fs::directory_iterator(user / "save"))
        CHECK(e.path().filename().u8string().find(".elonaplus-import-tmp-") != 0);
    CHECK(sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // ---- the original tree is untouched -----------------------------------------------
    CHECK(tree_hash(game) == before);
    std::printf("%s: %d/%d checks failed\n", failures ? "FAILED" : "OK", failures, checks);
    return failures ? 1 : 0;
}

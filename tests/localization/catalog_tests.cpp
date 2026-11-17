#include "localization/catalog.h"
#include "localization/rich_text.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    auto file = root / "catalog-test.tsv";
    auto write = [&](const std::string& data) { std::ofstream(file, std::ios::binary) << data; };
    auto bad = [&](const std::string& data) {
        write(data);
        try { auto ignored = elona::localization::Catalog::load(file); (void)ignored; return false; }
        catch (const std::exception&) { return true; }
    };
    int n = 0;
    auto check = [&](bool value) { ++n; if (!value) throw std::runtime_error("catalog check " + std::to_string(n)); };
    try {
        write("# format\nStarting Menu\t모험의 이정표\n日本語\t일본어\nspace \\t\t공백 \\t\n");
        auto catalog = elona::localization::Catalog::load(file);
        check(catalog.size() == 3);
        check(catalog.find("Starting Menu") && *catalog.find("Starting Menu") == "모험의 이정표");
        check(catalog.find("日本語") && *catalog.find("日本語") == "일본어");
        check(catalog.find("space \t") && *catalog.find("space \t") == "공백 \t");
        check(!catalog.find("A Starting Menu")); // no unsafe substring substitution
        check(!catalog.find("starting menu")); // preserve intentional case/context
        check(bad("x\ta\nx\tb\n"));
        check(bad("x\t\xc0\x80\n")); // overlong UTF-8
        check(bad("x\t\xed\xa0\x80\n")); // surrogate
        check(bad("x\t\xf4\x90\x80\x80\n")); // > U+10FFFF
        check(bad("x\t\xe3\x81\n")); // truncated
        check(bad("x\tbad\\q\n"));
        check(bad("x\tline\\nline\n")); // requires paragraph-aware layout
        check(bad("x\t\n"));
        check(bad("\ty\n"));
        write("\xef\xbb\xbfx\ty\r\nx\ty\r\n");
        check(elona::localization::Catalog::load(file).size() == 1);
        auto rich = elona::localization::layout_rich_text("<emp1>마법서<def>를 읽으면 주문을 배운다.", 84, 0);
        check(rich.height > 18);
        check(rich.glyphs.front().style == 4 && rich.glyphs.front().color != 0);
        check(rich.final_style == 0 && rich.final_color == 0);
        std::string visible;
        for (const auto& g : rich.glyphs) {
            check(g.x >= 0 && g.x + g.advance <= 84);
            check(g.y >= 0 && g.y + g.size <= rich.height);
            visible += g.utf8;
        }
        // Only wrap spaces may disappear; no syllable or punctuation may be lost.
        std::string compact;
        for (char c : visible) if (c != ' ') compact += c;
        check(compact == "마법서를읽으면주문을배운다.");
        auto explicit_break = elona::localization::layout_rich_text("첫째<br>둘째<p>셋째", 100, 0);
        check(explicit_break.height == 58);
        bool rejected = false;
        try { elona::localization::layout_rich_text("<unknown>bad", 100, 0); }
        catch (const std::exception&) { rejected = true; }
        check(rejected);
        std::cout << "catalog " << n << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

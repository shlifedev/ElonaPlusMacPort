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
        write("@log/You regain consciousness.\t정신을 차렸다.\n@log/*saving*\t*저장 중*\n"
              "@log/a b c d e f g h i j k l\t첫 집\n"
              "@hud/Lv\t레벨\n@clock/Midnight\t한밤중\n@weather/Rain\t비\n"
              "@log/home-rank\t가구 {1}점 · 가보 {2}점 · 집 순위 {3}위→{4}위. 새 칭호는 〈{5}〉.\n"
              "@home/Beggar's shack\t거지의 판잣집\n");
        auto home = elona::localization::Catalog::load(file);
        check(home.hud_text("Lv1/3024") == "레벨 1/3024");
        check(home.hud_text("Midnight Rain") == "한밤중 · 비");
        check(!home.hud_text("Lv1/3024A") && !home.hud_text("My Midnight Rain"));
        check(home.log_text("[1:10] *saving*") == "[1:10] *저장 중*");
        check(!home.log_text("Alice says You regain consciousness."));
        std::string narrow_rows;
        for (const auto& word : home.log_words("a b c d e f g h i j k l", 0, 2))
            narrow_rows += word.display;
        check(narrow_rows == "첫 집");
        const std::string rank = "Furniture Value:5 Heirloom Value:10 Home Rank:100th->91st "
                                 "Your home is now known as <Beggar's shack>.";
        check(home.log_text(rank) == "가구 5점 · 가보 10점 · 집 순위 100위→91위. 새 칭호는 〈거지의 판잣집〉.");
        check(!home.log_text(rank + " ") && !home.log_text("home-rank"));
        for (int initial : {0, 2, 19}) {
            std::string displayed, original;
            for (const auto& word : home.log_words(rank, initial, 35)) {
                original += word.source;
                displayed += word.display;
                check(elona::localization::valid_utf8(word.display));
            }
            check(original == rank + " " && displayed == *home.log_text(rank));
        }
        write("@item/7\t흉갑\n@item/11\t중장화\n@item/14\t감정의 두루마리\n@item/233\t휴대 식량\n"
              "@item-mod/bronze\t청동\n@item-mod/ブロンズ\t청동\n");
        auto inventory = elona::localization::Catalog::load(file);
        check(inventory.item_label("a bronze breastplate [0,3]", "breastplate", "", 7) == "청동 흉갑 [0,3]");
        check(inventory.item_label("a pair of bronze heavy boots", "heavy boots", "", 11) == "청동 중장화");
        check(inventory.item_label("2 scrolls of identify", "identify", "scroll", 14) == "감정의 두루마리 ×2");
        check(inventory.item_label("4 rations", "ration", "", 233) == "휴대 식량 ×4");
        check(inventory.item_label("★ブロンズの鎧 [0,3]", "鎧", "", 7) == "★청동 흉갑 [0,3]");
        check(!inventory.item_label("a scroll titled Ari", "identify", "scroll", 14));
        check(!inventory.item_label("a breastplate <Ari>", "breastplate", "", 7));
        const auto fallback_file = root / "draft-test.tsv";
        std::ofstream(fallback_file, std::ios::binary) <<
            "1774c8b890bd7ae052be670183279bdc938d38f01937359cbf2a955c94ee0e62\t비가 또 온다.\na6d20aa6a4c73f0d2eb57088f6d57ff3e8ef6fa0c4e726b6a6b7d31a2e587662\t빗방울\n";
        auto layered = elona::localization::Catalog::load(file, fallback_file);
        check(layered.find("It is raining again.") && *layered.find("It is raining again.") == "비가 또 온다.");
        check(!layered.find("Rain") && *layered.find("Rain", true) == "빗방울");
        write("It is raining again.\t또 비가 내린다.\n");
        layered = elona::localization::Catalog::load(file, fallback_file);
        check(*layered.find("It is raining again.") == "또 비가 내린다.");
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
        const std::string nickname = "<red>Fox$^/A";
        auto literal = elona::localization::layout_rich_text(nickname, 200, 0, false);
        std::string spelling;
        for (const auto& glyph : literal.glyphs) {
            spelling += glyph.utf8;
            check(glyph.color == 0);
        }
        check(spelling == nickname);
        write("@log-template/{name} gets furious!\t{name}의 분노가 폭발했다.\n");
        auto angry = elona::localization::Catalog::load(file);
        check(angry.log_text("[1:20] <red>Fox$^/A gets furious!") == "[1:20] <red>Fox$^/A의 분노가 폭발했다.");
        check(!angry.log_text(" gets furious!"));
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

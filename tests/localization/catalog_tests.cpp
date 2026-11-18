#include "localization/catalog.h"
#include "localization/rich_text.h"
#include "localization/quest_board.h"
#include "localization/journal.h"
#include "localization/options.h"
#include "platform/game_files.h"
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
              "@hud/Lv\t레벨\n@hud/gp\t금화\n@hud/pp\t백금화\n@clock/Midnight\t한밤중\n@weather/Rain\t비\n"
              "@day/Training-day\t수련의 날\n@hud/destiny\t운명\n"
              "@log/home-rank\t가구 {1}점 · 가보 {2}점 · 집 순위 {3}위→{4}위. 새 칭호는 〈{5}〉.\n"
              "@home/Beggar's shack\t거지의 판잣집\n");
        auto home = elona::localization::Catalog::load(file);
        check(home.hud_text("Lv1/3024") == "레벨 1/3024");
        check(home.hud_text("Midnight Rain") == "한밤중 · 비");
        check(home.hud_text("3140 gp") == "금화 3140" && home.hud_text("0 pp") == "백금화 0");
        check(!home.hud_text("my 3140 gp") && !home.hud_text("3140 gp left"));
        check(home.hud_text("Training-day / ") == "수련의 날");
        check(home.hud_text("Training-day / DES") == "수련의 날 / 운명 3/7");
        check(home.hud_text("Training-day / DESTINY") == "수련의 날 / 운명 7/7");
        check(!home.hud_text("Training-day / DESX") && !home.hud_text("Ari / D"));
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
        write("@catch/618/9\t망둑어\n@catch/619/46\t구멍 난 부츠\n");
        auto fishing = elona::localization::Catalog::load(file);
        check(fishing.item_label("a 12cm goby", "goby", "9", 618) == "12센티미터 망둑어");
        check(fishing.item_label("2匹の腐った12cmのハゼ", "ハゼ", "9", 618) == "썩은 12센티미터 망둑어 ×2");
        check(fishing.item_label("an old goby", "goby", "9", 618) == std::nullopt);
        check(fishing.item_label("a goby <Ari>", "goby", "9", 618) == std::nullopt);
        check(fishing.item_label("a goby", "goby", "9", 619) == std::nullopt);
        check(fishing.item_label("2 tattered bootss", "tattered boots", "46", 619) == "구멍 난 부츠 ×2");
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
        write("@log-template/You see {object} here.\t발견 · {object}\n"
              "@log-noun/{object}/a square chair\t네모난 의자\n"
              "@starter-pet/165/dog\t개\n@starter-pet/164/cat\t고양이\n"
              "@starter-pet/108/brown bear\t불곰\n@starter-pet/166/little girl\t소녀\n"
              "@log-template/{name} joins your party!\t〈{name}〉 합류\n");
        auto adventure = elona::localization::Catalog::load(file);
        check(adventure.log_text("[9:01] You see a square chair here.") == "[9:01] 발견 · 네모난 의자");
        check(!adventure.log_text("You see my named chair here."));
        check(adventure.log_text("<red>Fox$^/A joins your party!") == "〈<red>Fox$^/A〉 합류");
        check(adventure.log_text("[7:21] Feykka the dog joins your party!", 165) == "[7:21] 〈개 Feykka〉 합류");
        check(adventure.log_text("Feykka the dog joins your party!") == "〈Feykka the dog〉 합류");
        check(adventure.log_text("Feykka the dog joins your party!", 164) == "〈Feykka the dog〉 합류");
        check(adventure.log_text("Ari the cat joins your party!", 164) == "〈고양이 Ari〉 합류");
        check(adventure.log_text("Ari the brown bear joins your party!", 108) == "〈불곰 Ari〉 합류");
        check(adventure.log_text("Ari the little girl joins your party!", 166) == "〈소녀 Ari〉 합류");
        check(!adventure.log_text("[text] You see a square chair here."));
        check(bad("@log-template/You see {object} here.\t{enemy} 발견\n"));
        check(bad("@log-template/You see {object} here.\t} 발견 {object}\n"));
        write("@news/tag\t[소식]\n@news/accomplishment\t{name}의 의뢰 달성 · 명성 {number} 획득\n"
              "@news/discovery\t{name}의 발견 · {place}에서 {item} 획득\n"
              "@news/growth\t{name}의 성장 · 레벨 {number} 달성\n@news/recovery\t{name}의 부상 완치\n"
              "@news/retirement\t{name} 은퇴\n@news-item/armored boots\t장갑 장화\n"
              "@item-mod/heavy\t묵직한\n@log-noun/{place}/Port Kapul\t카풀 항\n");
        auto news = elona::localization::Catalog::load(file);
        check(news.log_text("[7:2] [News] {number} has finished a quest and gained 6 fame.")
              == "[7:2] [소식] {number}의 의뢰 달성 · 명성 6 획득");
        check(news.log_text("[News] A has discovered the pair of heavy armored boots <My Boots> [0,7] in Port Kapul.")
              == "[소식] A의 발견 · 카풀 항에서 묵직한 장갑 장화 〈My Boots〉 [0,7] 획득");
        check(news.news_text("A has gained experience and achieved level 11.") == "A의 성장 · 레벨 11 달성");
        check(news.news_text("A has fully recovered from injury.") == "A의 부상 완치");
        check(news.news_text("A realized the limitations and retired.") == "A 은퇴");
        check(!news.news_text("A has finished a quest and gained some fame."));
        check(!news.log_text("[Other] A has fully recovered from injury."));
        const std::string board_row = "A request:{client} {ref} {reward}";
        const std::string board_key = "@board/%COLLECT,EN/" + elona::platform::sha256_hex(board_row.data(), board_row.size());
        write(board_key + "/title\t의뢰\n" + board_key + "/body\t{client} / {ref} / {reward}\n"
              "@item/1\t장검\n@board-reward/armor\t방어구\n");
        auto postings = elona::localization::Catalog::load(file);
        std::array<std::string, 7> fields = {"%COLLECT,EN", board_row, "", "a long sword(Heavy!)",
                                             "94 gold pieces and armor", "<red>{ref}^$", "Vernis"};
        auto rendered = elona::localization::board_post(postings, fields, 1);
        check(rendered && rendered->title == "의뢰" && rendered->body == "<red>{ref}^$ / 장검 (매우 무거움!) / 금화 94닢, 방어구");
        fields[0] = "%COOK,1,JP";
        check(!elona::localization::board_post(postings, fields, 1));
        write("@sheet/race/Fairy\t요정\n@sheet/race/妖精\t요정\n@sheet/Race\t종족\n@sheet/extra\t추가\n"
              "@metric/page\t{1}/{2}쪽\n@metric/items\t{1}개 (무게 {2}/{3} · 화물 {4}/{5})\n"
              "@metric/appearance\t{1} {2}\n@appearance/Hair CL\t머리 색\n"
              "@appearance/髪の色\t머리 색\n@appearance/N/A\t해당 없음\n"
              "@weight/Medium\t보통\n@metric/equipment\t장비 {1} ({2}) 명중 {3} 피해 {4} DV/PV {5}/{6}\n");
        auto sheet = elona::localization::Catalog::load(file);
        check(sheet.sheet_text("Race: Fairy") == "종족: 요정");
        check(sheet.sheet_text("種族: (extra)妖精") == "종족: (추가) 요정");
        check(sheet.sheet_text("(extra)Fairy") == "(추가) 요정");
        check(!sheet.sheet_text("Race: Fox") && !sheet.sheet_text("(extra)Fox") && !sheet.sheet_text("Fairy"));
        check(sheet.sheet_text("Page.2/10") == "2/10쪽" && !sheet.sheet_text("My Page.2/10"));
        check(sheet.sheet_text("Hair CL  21") == "머리 색 21" && !sheet.sheet_text("Ari 21"));
        check(sheet.sheet_text("髪の色　 N/A") == "머리 색 해당 없음");
        check(sheet.sheet_text("Hair CL  u17") == "머리 색 u17");
        check(sheet.sheet_text("12 items  (Weight 33.8s/64.5s Cargo 16.0s/80.0s)") ==
              "12개 (무게 33.8s/64.5s · 화물 16.0s/80.0s)");
        check(sheet.sheet_text("Equip weight: 32.0s(Medium) Hit Bonus:0 Damage Bonus:-1  DV/PV:1/10") ==
              "장비 32.0s (보통) 명중 0 피해 -1 DV/PV 1/10");
        write("@journal/ - Quest - \t의뢰\n@journal/progress\t<진행도 {1}>\n"
              "@journal/Investigate the dungeon south of Vernis.\t베르니스 남쪽의 미궁을 조사하자.\n"
              "@log-noun/{place}/Your Home\t우리 집\n"
              "@option/1/7/Show\t일부만 표시\n@option/1/8/Show\t모두 표시\n");
        auto menus = elona::localization::Catalog::load(file);
        auto journal = elona::localization::journal_rows(menus,
            " - Quest - \r\n@QM<Progress 9>\r\nInvestigate the dungeon \r\nsouth of Vernis.\r\n");
        check(journal.size() == 4 && journal[0] == "의뢰" && journal[1] == "<진행도 9>");
        check(journal[2] == "베르니스 남쪽의 미궁을 조사하자." && journal[3] == "");
        check(!elona::localization::journal_rows(menus, "Unknown text.")[0]);
        check(elona::localization::save_header(menus, "Rose Lv:2 Your Home") == "Rose · 레벨 2 · 우리 집");
        check(elona::localization::save_header(menus, "Rose Lv:2 My Map") == "Rose · 레벨 2 · My Map");
        check(!elona::localization::save_header(menus, "Rose Lv:foo My Map"));
        check(!elona::localization::save_header(menus, "Rose Lv:1 My Lv:7 Arena"));
        check(elona::localization::option_text(menus, "@option/1/7/", "Show") == "일부만 표시");
        check(elona::localization::option_text(menus, "@option/1/8/", "Show") == "모두 표시");
        check(elona::localization::option_text(menus, "@option/1/4/", "After 4 steps") == "4걸음부터");
        check(elona::localization::option_text(menus, "@option/5/11/", "Button3") == "버튼 3");
        auto explicit_break = elona::localization::layout_rich_text("첫째<br>둘째<p>셋째", 100, 0);
        check(explicit_break.height == 58);
        bool rejected = false;
        try { elona::localization::layout_rich_text("<unknown>bad", 100, 0); }
        catch (const std::exception&) { rejected = true; }
        check(rejected);
        write("@npc-title/baker\t제빵사\n@npc-title/apprentice\t견습생\n"
              "@npc-prefix/パン屋の\t제빵사\nFemale\t여성\n"
              "<Guide> the wanderer\t방랑자 『길잡이』\n<Guide> the wanderer Female\t방랑자 『길잡이』 (여성)\n");
        auto town = elona::localization::Catalog::load(file);
        auto caption = [&](int scope, const std::string& value) {
            return elona::localization::town_caption(town, scope, value);
        };
        check(caption(24, "Page 2/12") == "2/12쪽");
        check(!caption(24, "Page 2/X"));
        check(caption(25, "Arrorel the baker\tArrorel the bak") == "제빵사 Arrorel");
        check(caption(25, "パン屋のシホ\tパン屋のシホ") == "제빵사 シホ");
        check(!caption(25, "The custom name\tThe custom name"));
        check(!caption(25, "Arrorel the baker\tbad\trow"));
        check(caption(26, "<Miches> the apprentice\t\t<Miches> the apprentice Female") == "견습생 <Miches> 여성");
        check(caption(26, "<Guide> the wanderer\t\t<Guide> the wanderer Female") == "방랑자 『길잡이』 (여성)");
        check(caption(26, "Female\t\tFemale Female") == "Female 여성");
        check(caption(26, "Ada\tCustom Fame: 3\tAda of Custom Fame: 3 Female Fame: 57") ==
              "Ada 〈Custom Fame: 3〉 여성 · 명성 57");
        check(!caption(26, "Ada\t\tSomeone else Female"));
        std::cout << "catalog " << n << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

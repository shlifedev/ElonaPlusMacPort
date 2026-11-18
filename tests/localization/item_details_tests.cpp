#include "localization/catalog.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto catalog = elona::localization::Catalog::load(argv[1]);
    struct Example { const char* source; const char* expected; };
    const Example cases[] = {
        {"It is made of bronze.", "재질 · 청동"},
        {"それはブロンズで作られている", "재질 · 청동"},
        {"It can be wielded as a weapon. (3d3 Pierce 0%)", "무기로 사용할 수 있다. (피해 3d3, 관통 0%)"},
        {"それは武器として扱うことができる (1d12 貫通 25%)", "무기로 사용할 수 있다. (피해 1d12, 관통 25%)"},
        {"It modifies hit bonus by -2 and damage bonus by 3.", "명중 보정 -2, 피해 보정 3."},
        {"それはDVを-1上昇させ、PVを12上昇させる", "회피 보정 -1, 방어 보정 12."},
        {"It grants your resistance to lightning. [#]", "전격 내성을 높인다. [#]"},
        {"それは電撃への耐性を弱化する [*****+]", "전격 내성을 낮춘다. [*****+]"},
        {"It make possible to remote blow.", "멀리 떨어진 적에게 타격을 가할 수 있다."},
        {"It is alive. [Lv:3 Exp:25%]", "살아 있는 무기다. [레벨 3, 경험치 25%]"},
        {"Exp:253 Next:1000", "경험치 253 / 다음 레벨 1000"},
    };
    for (const auto& row : cases) {
        if (catalog.item_label(row.source, "", "detail", -1) != row.expected) {
            std::cerr << row.source << '\n';
            return 1;
        }
    }
    for (const auto* unknown : {"It is made of My Custom Material.", "It make possible to remote blow. [Ari]",
                                "It can be wielded as a weapon. (3d3 Pierce 0%)x"}) {
        if (catalog.item_label(unknown, "", "detail", -1)) return 1;
    }
    if (catalog.item_label("a dish of goldfish sushi(Rank9)", "goldfish", "1", 618) != "금붕어 초밥 (등급 9)" ||
        catalog.item_label("金魚の活け作り(Rank9)", "金魚", "1", 618) != "금붕어 활어회 (등급 9)" ||
        catalog.item_label("2 blessed dishs of goldfish soup(Rank5)", "goldfish", "1", 618) != "축복받은 금붕어 수프 (등급 5) ×2" ||
        catalog.item_label("my dish of goldfish tart", "goldfish", "1", 618) ||
        catalog.item_label("Paper x 46", "Paper", "43", -2) != "종이 ×46" ||
        catalog.item_label("紙 × 46", "紙", "43", -2) != "종이 ×46" ||
        catalog.item_label("White paper.", "White paper.", "43", -3) ||
        catalog.item_label("Foreign money.", "Foreign money.", "41", -3) != "외국 돈이다." ||
        catalog.item_label("Paper x 46", "Paper", "41", -2)) return 1;
    if (catalog.log_text("You get a 13cm goldfish!") != "낚시로 건졌다! 〈13센티미터 금붕어〉" ||
        catalog.log_text("You get 5 Paper. ") != "재료 획득 · 종이 ×5" ||
        catalog.log_text("You get a 13cm Custom!") || catalog.log_text("You get 4 Custom. ")) return 1;
    return catalog.item_label("It make possible to remote blow.", "", "", -1).has_value();
}

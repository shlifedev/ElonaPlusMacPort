#include "localization/alias.h"
#include "localization/catalog.h"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    const auto catalog = elona::localization::Catalog::load(argv[1]);
    const auto translate = [&](const std::string& text) {
        return elona::localization::localized_alias(catalog, text);
    };
    struct Example { const char* source; const char* korean; };
    const Example examples[] = {
        {"Dangerous Tomorrow", "위험한 내일"}, {"Drunken Ram", "술에 취한 숫양"},
        {"Fire Protecter", "불의 수호자"}, {"Sword of Flame", "불꽃의 검"},
        {"White Tiger of Dawn", "여명의 백호"}, {"The White Tiger", "백호"},
        {"The army of Sword of Flame", "불꽃의 검 군단"}, {"Dangerous Tomorrow Band", "위험한 내일 무리"},
        {"月の騎士", "달의 기사"}, {"闇にひそむ剣", "어둠에 숨는 검"},
        {"ザ・プリンス", "왕자"}, {"ソード・オブ・ムーン", "달의 검"},
        {"プリンスのの槍", "왕자의 창"}, {"ソード・ムーン", "검의 달"},
        {"月の騎士の一家", "달의 기사 일가"}, {"*Debug*", "시험용 이명"},
    };
    for (const auto& example : examples) {
        auto result = translate(example.source);
        if (result != example.korean) {
            std::cerr << example.source << " -> " << result.value_or("<missing>") << '\n';
            return 1;
        }
    }
    for (const char* unknown : {"", "Fox", "Ari's Custom Name", "Dangerous UnknownName",
             "Moon of Unmatched", "The Dangerous", "Dangerous of Moon",
             "Moon Drunken", "Moon of Drunken", "Sword ・Moon", "ザ・Moon", "月のMoon", "ザ・未登録", "月の騎士の余分な語", "   ", "<red>Dragon</red>"}) {
        if (translate(unknown)) return 1;
    }

    // Optional private corpus produced by the stock vocabulary audit, decoded
    // to UTF-8 before this boundary exactly as in the SDL display renderer.
    if (argc == 3) {
        std::ifstream cases(argv[2], std::ios::binary);
        if (!cases) return 2;
        std::size_t count = 0;
        for (std::string line; std::getline(cases, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto result = translate(line);
            if (!result || result->find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ|") != result->npos) {
                std::cerr << "untranslated generated alias: " << line << '\n';
                return 1;
            }
            ++count;
        }
        std::cout << count << " generated alias cases passed\n";
    }
}

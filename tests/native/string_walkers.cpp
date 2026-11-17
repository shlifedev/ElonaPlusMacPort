#include <cstdio>
#include <cstring>
#include "hsp3config.h"
#include "supio.h"
#include "strbuf.h"

// Exercise the actual upstream helper implementations, including characters
// whose second byte looks like ASCII A or a Windows path separator.
int main() {
    int failures = 0;
    auto require = [&](bool condition, const char* name) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    char ascii[] = "AbC.TXT";
    strcase(ascii);
    require(std::strcmp(ascii, "abc.txt") == 0, "ASCII lowercase");
#ifdef ELONA_EXPECT_CP932
    char punctuation[] = "\x81\x41" "A.TXT";
    strcase(punctuation);
    require(std::strcmp(punctuation, "\x81\x41" "a.txt") == 0, "CP932 trail 0x41 preserved");
    char kana[] = "\x83\x5c" "A";
    strcase(kana);
    require(std::strcmp(kana, "\x83\x5c" "a") == 0, "CP932 trail 0x5c preserved");
    char search[] = "\x81\x41" "A";
    char match[] = "A";
    require(strstr2(search, match) == search + 2, "Search skips CP932 trail byte");
    char path[] = "\x83\x5c" "\\file";
    require(strchr2(path, '\\') == path + 2, "Separator ignores CP932 trail byte");
    char no_separator[] = "\x83\x5c";
    require(strchr2(no_separator, '\\') == nullptr, "No phantom separator in CP932 character");
    char split_source[] = "\x81\x41" "Arest";
    char split_output[32] = {};
    strsp_ini();
    strsp_get(split_source, split_output, 'A', sizeof(split_output) - 1);
    require(std::strcmp(split_output, "\x81\x41") == 0, "Split skips CP932 trail byte");
    char trim[] = "\x81\x41" "A";
    TrimCode(trim, 'A');
    require(std::strcmp(trim, "\x81\x41") == 0, "Trim preserves matching trail byte");
    TrimCode(trim, 0x4181);
    require(trim[0] == 0, "Trim matches a complete CP932 character");
    sbInit();
    char replacement[] = "X";
    char* output = sbAlloc(64);
    ReplaceSetMatch(search, match, output, sizeof(search) - 1, 1, 64);
    output = ReplaceStr(replacement);
    require(std::strcmp(output, "\x81\x41" "X") == 0, "Replace skips CP932 trail byte");
    sbFree(output);
    sbBye();
#else
    char utf8[] = "\xe3\x81\x82" "A.TXT";
    strcase(utf8);
    require(std::strcmp(utf8, "\xe3\x81\x82" "a.txt") == 0, "Default UTF-8 walker preserved");
#endif
    return failures ? 1 : 0;
}

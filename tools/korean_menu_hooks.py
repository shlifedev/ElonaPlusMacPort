"""Bound Korean menu keys to the original 2.335 draw that owns each value.

Line numbers refer to the SHA-pinned CP932 reference. The begin/end pair spans
only the existing draw call, including its shadow passes. Never look up text
from a save row, an item instance, or a player/NPC name as a menu caption.
"""
from korean_journal_hooks import JOURNAL_DRAWS

# scope 1 = body slot, 2 = inventory action, 3 = column, 4 = tab/window,
# 5 = status, 6 = sprite editor, 7 = portrait editor, 8 = character sheet and generated counters, 9 = biography.
MENU_DRAWS = (
    (223244, 'display_window (windoww - 400) / 2 + inf_screenx, winposy(458, 1) + 20, 400, 458', 9, 's'),
    (223252, 'display_topic lang("異名の候補", "Alias List"), wx + 28, wy + 30', 9, 'lang("異名の候補", "Alias List")'),
    (291380, 'display_window (windoww - 400) / 2 + inf_screenx, winposy(458), 400, 458', 4, 's'),
    (291381, 'display_topic lang("名前の候補", "Alias List"), wx + 28, wy + 30', 4, 'lang("名前の候補", "Alias List")'),
    (223547, 'cs_list listn(0, p), wx + 64, wy + 66 + cnt * 19 - 1, 19', 8, 'listn(0, p)'),
    (224038, 'cs_list listn(0, cnt), wx + 64, wy + 66 + cnt * 23 - 1, 19', 8, 'listn(0, cnt)'),
    (224583, 'mes lang("種族: ", "Race: ") + cmrace(1)', 8, 'lang("種族: ", "Race: ") + cmrace(1)'),
    (289375, 'display_topic s, wx + 46, wy + 36', 8, 's'),
    (289376, 'display_topic s(2), wx + 255, wy + 36', 8, 's(2)'),
    (289462, 'cs_list listn(0, p), wx + x, wy + 66 + cnt * 19 - 1, 19, 0, -1', 17, 'listn(0, p)'),
    (292551, 'display_topic lang("能力の名称", "Name"), wx + 28, wy + 36', 8, 'lang("能力の名称", "Name")'),
    (292553, 'display_topic lang("能力の効果", "Detail"), wx + 320, wy + 36', 8, 'lang("能力の効果", "Detail")'),
    (292592, 'cs_list skillname(list(0, p)) + s, wx + 84, wy + 66 + cnt * 19 - 1, 19', 8, 'skillname(list(0, p)) + s'),
    (297735, 'display_topic lang("能力の名称", "Name"), wx + 28, wy + 36', 8, 'lang("能力の名称", "Name")'),
    (297739, 'display_topic lang("効果", "Detail"), wx + 350, wy + 36', 8, 'lang("効果", "Detail")'),
    (297849, 'cs_list s, wx + x, wy + 66 + cnt * 19 - 1, 19', 8, 's'),
    (297948, 'cs_list listn(0, p), wx + 88, wy + 66 + cnt * 19, 19', 8, 'listn(0, p)'),
    (304609, 'gosub *label_6185', 4, '"Cancel"'),
    # The first row is the reroll command. Character sheet row 1 is the alias,
    # directly adjacent to the saved player name at row 0.
    (223271, 'cs_list listn(0, cnt), wx + 64, wy + 66 + cnt * 19 - 1, 19', '11 + 10 * (cnt == 0)', 'listn(0, cnt)'),
    (291403, 'cs_list listn(0, cnt), wx + 64, wy + 66 + cnt * 19 - 1, 19', '11 + 10 * (cnt == 0)', 'listn(0, cnt)'),
    (297591, 'mes s(cnt)',
     '11 * (cnt == 1) + 10 * ((cnt == 0) | ((cnt == 2) & (cdatan(7, cc) != ""))'
     ' | ((cnt == 3) & (cdatan(8, cc) != "") & (cdatan(8, cc) != lang("なし", "none")))'
     ' | ((cnt == 4) & (cdatan(6, cc) != "")))', 's(cnt)'),
    (224669, 'mes strmid(skillname(r), 0, 4 - (jp == 0)) + ": " + s(p)', 8, 'strmid(skillname(r), 0, 4 - (jp == 0)) + ": " + s(p)'),
    (224697, 'mes s', 8, 's'),
    (224719, 'mes s + s(1)', 8, 's + s(1)'),
    (37814, 'mes s', 8, 's'),
    (37998, 'mes s', 8, 's'),
    (38031, 'mes s', 8, 's'),
    (38042, 'mes display_note_prm0', 8, 'display_note_prm0'),
    (223536, 'display_topic lang("種族の説明", "Detail"), wx + 188, wy + 30', 8, 'lang("種族の説明", "Detail")'),
    (223807, 'display_topic lang("職業の説明", "Detail"), wx + 188, wy + 30', 8, 'lang("職業の説明", "Detail")'),
    (224024, 'display_topic lang("能力", "Attributes"), wx + 28, wy + 30', 8, 'lang("能力", "Attributes")'),
    (224187, 'display_topic lang("各モードの説明", "Detail"), wx + 178, wy + 30', 8, 'lang("各モードの説明", "Detail")'),
    (273972, 'display_topic lang("説明", "Detail"), wx + 296, wy + 36', 8, 'lang("説明", "Detail")'),
    (274271, 'display_topic lang("説明", "Detail"), wx + 296, wy + 36', 8, 'lang("説明", "Detail")'),
    (283486, 'mes s@m195', 8, 's@m195'),
    (297431, 'mes s(cnt)', 8, 's(cnt)'),
    (297437, 'mes s(cnt)', 8, 's(cnt)'),
    (297456, 'mes s(cnt)', 8, 's(cnt)'),
    (297462, 'mes s(cnt)', 8, 's(cnt)'),
    (297557, 'mes s(cnt)', 8, 's(cnt)'),
    (297607, 'mes "Supreme"', 8, '"Supreme"'),
    (297611, 'mes "Amazing"', 8, '"Amazing"'),
    (297615, 'mes "Superb"', 8, '"Superb"'),
    (297619, 'mes "Great"', 8, '"Great"'),
    (297623, 'mes "Good"', 8, '"Good"'),
    (297627, 'mes "Bad"', 8, '"Bad"'),
    (297630, 'mes "Hopeless"', 8, '"Hopeless"'),
    (297653, 'mes s(cnt)', 8, 's(cnt)'),
    (297689, 'mes s(cnt)', 8, 's(cnt)'),
    (223393, 'display_topic lang("経歴", "Attributes"), wx + 28, wy + 30', 9, 'lang("経歴", "Attributes")'),
    (298963, 'cs_list s, wx + 60, wy + 66 + cnt * 21 - 1, 19, 0', 8, 's'),
    (299507, 'mes q', 1, 'q'),
    (301073, 'mes "" + bodyn(p / 10000)', 1, 'bodyn(p / 10000)'),
    (300922, 'bmes invtitle(p), 255, 255, 255', 2, 'invtitle(p)'),
    (300925, 'bmes invtitle(p), 165, 165, 165', 2, 'invtitle(p)'),
    (300957, 'display_window (windoww - 640) / 2 + inf_screenx, winposy(432), 640, 432', 2, 's'),
    (300974, 'display_topic lang("アイテムの名称", "Name"), wx + 28, wy + 30', 3, 'lang("アイテムの名称", "Name")'),
    (300975, 'display_topic s, wx + 526, wy + 30', 3, 's'),
    (299455, 'display_topic lang("重さ", "Weight"), wx + 524, wy + 30', 3, 'lang("重さ", "Weight")'),
    (38928, 'bmes s(cnt), 255, 255, 255', 4, 's(cnt)'),
    (38931, 'bmes s(cnt), 165, 165, 165', 4, 's(cnt)'),
    (308696, 'display_window (windoww - 440) / 2 + inf_screenx, winposy(288, 1), 440, 288', 4, 's'),
    (206180, 'mes _conwet', 5, '_conwet'),
    (298663, 'display_window (windoww - 380) / 2 + inf_screenx, winposy(340) - 12, 380, 300', 6, 's'),
    (298879, 'display_window (windoww - 380) / 2 + inf_screenx, winposy(340) - 12, 380, 340', 7, 's'),
) + tuple(
    (line, 'mes "Gene from " + geneuse', 12, '"Gene from " + geneuse')
    for line in (223231, 223336, 223487, 223655, 223682, 223779,
                 223913, 223940, 223980, 224120, 224145, 224176)
) + JOURNAL_DRAWS


def menu_inserts(source):
    start = 0
    next_line = 1
    for number, command, scope, value in sorted(MENU_DRAWS):
        while next_line < number:
            end = source.find(b'\n', start)
            if end < 0:
                raise ValueError(f'menu draw anchor missing at {number}')
            start = end + 1
            next_line += 1
        end = source.index(b'\n', start) + 1
        line = source[start:end]
        if line.strip() != command.encode('cp932'):
            raise ValueError(f'menu draw anchor differs at {number}')
        indent = line[:len(line) - len(line.lstrip(b'\t'))]
        begin = f'native_ko_menu {scope}, {value}\r\n'.encode('cp932')
        finish = b'native_ko_menu 0, ""\r\n'
        if number in (223271, 291403):
            begin += indent + b'if ( cs == cnt ) : native_ko_alias_width native_alias_width\r\n'
            finish += indent + b'native_alias_width = 0\r\n'
        yield start, indent + begin, 'menu_begin'
        yield end, indent + finish, 'menu_end'

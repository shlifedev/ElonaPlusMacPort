"""Identify authored dialogue before the 2.335 engine wraps the active language."""
from collections import defaultdict
import re

from extract_localization import LITERAL, PAIR
from korean_town_hooks import town_caption_inserts

STATIC = re.compile(rf'buff = lang\(\s*({LITERAL}),\s*{LITERAL}\s*\)')


def static_dialogue_inserts(source, reserved):
    yield from town_caption_inserts(source)
    lines = source.splitlines(keepends=True)
    # These labels enclose the shipped NPC story branches and the dialogue
    # window. An unknown revision must be inspected before reusing the range.
    if lines[430318].strip() != b'cs = 0' or lines[430320].strip() != b'talk_conv buff, 56 - en * 3':
        raise ValueError("NPC dialogue range end changed")
    offset = 0
    for number, raw in enumerate(lines, 1):
        offset += len(raw)
        if not 370000 <= number < 430321 or not raw.lstrip().startswith(b'buff = '):
            continue
        if offset in reserved:
            continue  # previously registered names and reviewed home lines
        match = STATIC.fullmatch(raw.decode('cp932').strip())
        if match:
            # The English side can be a shared placeholder. Keep the literal
            # Japanese key separate from buff; it is used only for Korean pixels.
            insert = (b'\tnative_dialog_original = buff\r\n\tnative_dialog_pending = ' +
                      match[1].encode('cp932') + b'\r\n')
            yield offset, insert, 'static_npc_dialogue'
        else:
            yield offset, b'\tnative_dialog_pending = ""\r\n', 'clear_static_dialogue'
    yield from story_choice_inserts(source, lines)
    # Preserve the selected authored data/talk.txt line before speech endings
    # are substituted. No lookup is generated from an editable custom reply.
    anchor = b'\tnoteget s, p\r\n\tbuff = s\r\n\tgosub *label_0441\r\n\treturn\r\n*label_0448\r\n'
    if source.count(anchor) != 1:
        raise ValueError("town speech selection anchor changed")
    begin = source.index(anchor) + len(b'\tnoteget s, p\r\n\tbuff = s\r\n')
    yield begin, b'\tnative_dialog_pending = "@town/" + buff\r\n', 'town_original'
    yield begin + len(b'\tgosub *label_0441\r\n'), b'\tnative_dialog_original = buff\r\n', 'town_display_match'
    custom = b'\t\t\t\tcustomtalk tc, 106\r\n\t\t\t\tif ( stat ) {\r\n'
    if source.count(custom) != 1:
        raise ValueError("town custom reply anchor changed")
    yield source.index(custom) + len(custom), b'\t\t\t\t\tnative_dialog_pending = ""\r\n', 'town_custom_reply'

CHOICE = re.compile(rf'listn\(0, listmax\) = lang\(\s*({LITERAL}),\s*({LITERAL})\s*\)')


def story_choice_inserts(source, lines):
    # Some option texts are unique among NPCs but are ambiguous elsewhere in
    # the catalogue. Use the same original literal identities as the extractor.
    english_keys = defaultdict(set)
    for japanese, english in PAIR.findall(source.decode('cp932')):
        english_keys[english].add(japanese)
    dimension = b'sdim listn, 40, 2, 500'
    draw = b'cs_list listn(0, cnt), x + 30, y, 18, 4, 0'
    if lines[23813].strip() != dimension or lines[430274].strip() != draw:
        raise ValueError('NPC choice dimensions or selection draw changed')
    offset = 0
    for number, raw in enumerate(lines, 1):
        offset += len(raw)
        if number == 23814:
            # The game owns both listn columns. Use the original slot capacity
            # with independent storage, including when a saved game is loaded.
            yield offset, (b'\tsdim native_reply_key, 96, 500\r\n'
                           b'\tdim native_reply_generation, 500\r\n'
                           b'\tnative_reply_page = 1\r\n'), 'npc_reply_init'
        if not 370000 <= number <= 430320:
            continue
        command = raw.strip()
        if command == b'listmax = 0':
            # Page rebuilds can discard a partially populated list without
            # presenting the window. Invalidate all 500 slots in constant time.
            yield offset, b'\tnative_reply_page++\r\n', 'npc_reply_reset'
        elif number == 430275:
            yield offset - len(raw), (b'\t\tif ( native_reply_generation(cnt) == native_reply_page ) {\r\n'
                b'\t\t\tnative_ko_menu 20, native_reply_key(cnt) + "\\t" + listn(0, cnt)\r\n'
                b'\t\t}\r\n'), 'npc_reply_begin'
            yield offset, b'\t\tnative_ko_menu 0, ""\r\n', 'npc_reply_end'
        elif command.startswith(b'listn(0, listmax) = lang('):
            match = CHOICE.fullmatch(command.decode('cp932'))
            if match and (len(english_keys[match[2]]) > 1 or not match[2][1:-1].strip()):
                yield offset, (b'\tnative_reply_key(listmax) = ' + match[1].encode('cp932') +
                               b'\r\n\tnative_reply_generation(listmax) = native_reply_page\r\n'), 'npc_reply_identity'

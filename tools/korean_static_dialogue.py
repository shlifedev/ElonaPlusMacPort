"""Identify authored dialogue before the 2.335 engine wraps the active language."""
import re

from extract_localization import LITERAL

STATIC = re.compile(rf'buff = lang\(\s*({LITERAL}),\s*{LITERAL}\s*\)')


def static_dialogue_inserts(source, reserved):
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
    # English "Alright." covers five different authored replies. Keep the choice
    # identity through the redraw loop and clear it when the dialog closes.
    replies = {373951: (31, 'わかった'), 380932: (32, 'いいだろう'),
               393890: (31, 'わかった'), 393943: (31, 'わかった'),
               421598: (33, '欲しい…'), 421903: (34, '挑戦する'), 426312: (35, '頼む')}
    reply_draw = b'cs_list listn(0, cnt), x + 30, y, 18, 4, 0'
    offset = 0
    for number, raw in enumerate(lines, 1):
        offset += len(raw)
        if number in replies:
            scope, japanese = replies[number]
            if raw.strip() != f'listn(0, listmax) = lang("{japanese}", "Alright.")'.encode('cp932'):
                raise ValueError(f'NPC reply assignment changed at {number}')
            yield offset, (f'\tnative_reply_scope = {scope}\r\n'
                           '\tnative_reply_slot = listmax\r\n').encode(), 'npc_reply_identity'
        elif number == 430275:
            if raw.strip() != reply_draw:
                raise ValueError('NPC reply draw changed')
            yield offset - len(raw), (b'\t\tif ( native_reply_scope > 0 & cnt == native_reply_slot ) {\r\n'
                                      b'\t\t\tnative_ko_menu native_reply_scope, listn(0, cnt)\r\n'
                                      b'\t\t}\r\n'), 'npc_reply_begin'
            yield offset, b'\t\tnative_ko_menu 0, ""\r\n', 'npc_reply_end'
        elif number == 430313:
            if raw.strip() != b'listmax = 0':
                raise ValueError('NPC reply reset changed')
            yield offset, b'\tnative_reply_scope = 0\r\n', 'npc_reply_reset'
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

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

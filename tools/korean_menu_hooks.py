"""Bound Korean menu keys to the original 2.335 draw that owns each value.

Line numbers refer to the SHA-pinned CP932 reference. The begin/end pair spans
only the existing draw call, including its shadow passes. Never look up text
from a save row, an item instance, or a player/NPC name as a menu caption.
"""
# scope 1 = body slot, 2 = inventory action, 3 = column, 4 = tab/window,
# 5 = status, 6 = sprite editor, 7 = portrait editor.
MENU_DRAWS = (
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
)


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
        yield start, indent + begin, 'menu_begin'
        yield end, indent + b'native_ko_menu 0, ""\r\n', 'menu_end'

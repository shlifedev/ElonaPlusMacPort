"""Bind the stock manual's topic index, action labels and complete topic at its own window."""

MANUAL_DRAWS = (
    (603567, 'display_window (windoww - 780) / 2 + inf_screenx, winposy(496, 1), 780, 496', 23, 's'),
    (603570, 'display_window (windoww - 780) / 2 + inf_screenx, winposy(496) - 24, 780, 496', 23, 's'),
    (603572, 'display_topic lang("項目", "Topic"), wx + 34, wy + 36', 23, 'lang("項目", "Topic")'),
    (603600, 'display_topic lang("アイテムに関するキー", "Item Keys"), x + 18, y + 30', 23, 'lang("アイテムに関するキー", "Item Keys")'),
    (603601, 'display_topic lang("行動に関するキー", "Action Keys"), x + 18, y + 142', 23, 'lang("行動に関するキー", "Action Keys")'),
    (603602, 'display_topic lang("情報に関するキー", "Information Keys"), x + 18, y + 256', 23, 'lang("情報に関するキー", "Information Keys")'),
    (603603, 'display_topic lang("その他のキー", "Other Keys"), x + 18, y + 328', 23, 'lang("その他のキー", "Other Keys")'),
    (603678, 'display_topic s(1), wx + 206, wy + 36', 23, 's(1)'),
    (603706, 'cs_list s, wx + 66, wy + 66 + cnt * 19 - 1, 19, 0', 23, 's'),
) + tuple((n, 'mes s(cnt * 2)', 23, 's(cnt * 2)') for n in
          (603612, 603620, 603634, 603642, 603655, 603666))

MANUAL_LOAD = b'\tnoteload exedir + "data\\\\" + lang("manual_JP.txt", "manual_ENG.txt")\r\n'
MANUAL_BODY = b'\t\tgmestype = 0\r\n\t\trepeat noteinfo(0) - p, p\r\n'
MANUAL_END = b'\t\t\tgmes s1\r\n\t\tloop\r\n\t}\r\n*label_9615\r\n'
MANUAL_CLOSE = b'\tif ( key == key_cancel ) {\r\n\t\treturn\r\n\t}\r\n*label_9618\r\n'


def manual_inserts(source):
    anchors = (
        (MANUAL_LOAD, len(MANUAL_LOAD), b'\tnative_ko_menu 22, buff\r\n', 'manual_load'),
        (MANUAL_BODY, MANUAL_BODY.index(b'\t\trepeat'),
         b'\t\tnative_ko_gmes "@manual/page/" + p, gmesx, gmesy, gmesw, gmestype, gmescol, gmescol(1), gmescol(2)\r\n'
         b'\t\tif ( stat >= 0 ) : goto *native_manual_done\r\n', 'manual_page'),
        (MANUAL_END, MANUAL_END.index(b'\t}\r\n'), b'*native_manual_done\r\n', 'manual_page_end'),
        (MANUAL_CLOSE, MANUAL_CLOSE.index(b'\t\treturn'), b'\t\tnative_ko_menu 22, ""\r\n', 'manual_close'),
    )
    for anchor, position, insertion, name in anchors:
        if source.count(anchor) != 1:
            raise ValueError(f'{name} anchor differs in the 2.335 manual')
        yield source.index(anchor) + position, insertion, name

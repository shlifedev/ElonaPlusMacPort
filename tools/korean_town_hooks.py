"""Identify the original board and NPC caption, before its stored name is clipped."""


def town_caption_inserts(source):
    draws = (
        (b'\tbmes "Page " + (page + 1) + "/" + (pagemax + 1), 255, 255, 255\r\n',
         b'\tnative_ko_menu 24, "Page " + (page + 1) + "/" + (pagemax + 1)\r\n'),
        (b'\t\tpos wx + 392, y + 2\r\n\t\tmes s(4)\r\n',
         b'\t\tnative_ko_menu 25, cdatan(0, tc) + "\\t" + s(4)\r\n'),
        (b'\t\tpos wx + 120, wy + 16\r\n\t\tmes s\r\n',
         b'\t\tif ( scenemode == 0 ) {\r\n\t\t\tnative_ko_menu 26, cdatan(0, tc) + "\\t" + cdatan(1, tc) + "\\t" + s\r\n\t\t}\r\n'),
    )
    for anchor, before in draws:
        if source.count(anchor) != 1:
            raise ValueError('2.335 town caption boundary changed')
        offset = source.index(anchor)
        yield offset, before, 'town_caption_begin'
        yield offset + len(anchor), b'\t\tnative_ko_menu 0, ""\r\n', 'town_caption_end'

"""Read the selected 2.335 race/class description before the original word wrap."""
import re
from extract_localization import LITERAL


def creation_inserts(source):
    wrap = b'\tty = wy + 62\r\n\ttalk_conv buff, 60 + en * 2\r\n'
    draw = b'\tpos tx - 20, ty\r\n\tmes buff\r\n\tcolor 0, 0, 0\r\n'
    if source.count(wrap) != 1 or source.count(draw) != 1:
        raise ValueError('character creation biography anchor changed')
    lines = source.decode('cp932').splitlines()
    key = b'\tnative_creation_bio = "@creation-bio/" + buff\r\n'
    for number, job in [(439794, 'predator'), (440055, 'gunner')]:
        match = re.fullmatch(rf'buff = lang\(({LITERAL}), "This is untranslated sentence\."\)', lines[number-1].strip())
        if not match:
            raise ValueError(f'expected the original class placeholder at line {number}')
        key += (f'\tif ( val == 1 & dbidn == "{job}" & buff == "This is untranslated sentence." ) {{\r\n'
                f'\t\tnative_creation_bio = "@creation-bio/" + {match[1]}\r\n\t}}\r\n').encode('cp932')
    yield source.index(wrap), key, 'race_class_description'
    before = source.index(draw) + len(b'\tpos tx - 20, ty\r\n')
    yield before, (b'\tnative_ko_gmes native_creation_bio, tx - 20, ty, 425, 0, 0, 0, 0\r\n'
                   b'\tif ( stat < 0 ) {\r\n'), 'draw_creation_description'
    yield before + len(b'\tmes buff\r\n'), b'\t}\r\n', 'end_creation_description'

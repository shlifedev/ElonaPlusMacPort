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

    yield from authored_creation_inserts(source)


def authored_creation_inserts(source):
    """Use each authored Japanese line as its draw key even when English repeats."""
    lines = source.splitlines(keepends=True)
    assignment = re.compile(rf's = lang\(({LITERAL}), {LITERAL}\)')
    draws = {}
    for first, last in ((224207, 224328), (415931, 416077)):
        count = 0
        for number in range(first, last + 1):
            line = lines[number - 1]
            if not line.startswith(b'\t\ts = lang('):
                continue
            match = assignment.fullmatch(line.decode('cp932').strip())
            if not match or not lines[number].startswith(b'\t\tpos wx + 165, wy + '):
                raise ValueError(f'mode description changed at line {number}')
            draws[number + 2] = [(None, match[1])]
            count += 1
        if count != 38:
            raise ValueError(f'expected 38 mode description lines starting at {first}')

    # The original English repeats one family history three times and one
    # personality twice. Bind these alternatives at the two profile displays.
    for numbers, draw, sheet_draw in (((296477, 296480, 296486), 223371, 297481),
                                       ((297046, 297049), 223391, 297525)):
        for number in numbers:
            branch = re.fullmatch(r'if \( (ohanasi[15] == \d+) \) \{',
                                  lines[number - 2].decode('ascii').strip())
            match = assignment.fullmatch(lines[number - 1].decode('cp932').strip())
            if not branch or not match:
                raise ValueError(f'ambiguous character history changed at {number}')
            for target in (draw, sheet_draw):
                draws.setdefault(target, []).append((branch[1], match[1]))

    offset = 0
    for number, line in enumerate(lines, 1):
        if number in draws:
            if line.strip() != b'mes s':
                raise ValueError(f'character creation draw changed at line {number}')
            indent = line[:len(line) - len(line.lstrip(b'\t'))]
            for condition, japanese in draws[number]:
                command = f'native_ko_menu 20, {japanese} + "\\t" + s'
                if condition:
                    command = f'if ( {condition} ) {{\r\n\t{command}\r\n}}'
                insertion = b''.join(indent + part.encode('cp932') + b'\r\n'
                                     for part in command.splitlines())
                yield offset, insertion, 'authored_creation_begin'
            yield offset + len(line), indent + b'native_ko_menu 0, ""\r\n', 'authored_creation_end'
        offset += len(line)
        if number == 416079:
            break

"""Carry the selected built-in card ID through TCG help's timed priority check."""


import re

from extract_localization import PAIR


def card_lore_table(source):
    """Collect the first matching DB branch as card_ref does, keeping only UI inputs."""
    start = source.index(b'*label_5197\r\n\tif ( dbid == 1164 )')
    end = source.index(b'*label_5198\r\n#defcfunc cdbit', start)
    blocks = re.split(r'\s*if \( dbid == (\d+) \) \{', source[start:end].decode('cp932'))
    keys = {}
    for card, block in zip(blocks[1::2], blocks[2::2]):
        card = int(card)
        if card in keys:
            continue
        skill = next(line.strip() for line in block.splitlines() if line.strip().startswith('cardrefskill = '))
        pair = PAIR.fullmatch(skill.removeprefix('cardrefskill = '))
        if not pair:
            raise ValueError(f'card {card}: description is no longer an authored literal')
        keys[card] = pair[1]
    if keys.keys() != set(range(1165)):
        raise ValueError('unexpected first-branch 2.335 card IDs')
    return keys


def card_inserts(source):
    # An accepted non-card message clears the old card identity. A timed
    # message which rejects a new selection keeps its existing identity.
    changes = {
        271482: (b'helpmsg@tcg = cardhelp_prm0',
                 b'native_card_help = native_card_request\r\n', b''),
        271507: (b'cardhelp carddetailn@tcg(tcgdrawcard_prm0)',
                 b'native_card_request = card@tcg(18, tcgdrawcard_prm0) + 1\r\n',
                 b'native_card_request = 0\r\n'),
    }
    # Bind the original Japanese paragraph even when the active English slot
    # is empty. The normal prose lookup retains the launcher's draft policy.
    keys = card_lore_table(source)
    table = ''.join(f'native_card_ja({card}) = {keys[card]}\r\n' for card in sorted(keys))
    changes[272403] = (b'sdim carddetailn@tcg, 200, maxcard@tcg', b'',
                       b'sdim native_card_ja, 64, 1165\r\n' + table.encode('cp932'))
    for number, x, y in ((273035, 125, 542), (273040, 120, 537)):
        # Header and biography are separate catalogue entries. Retain the
        # original wrapped description when a card only has a Korean heading.
        before = (
            'native_card_painted = 0\r\n'
            'if ( native_card_help > 0 ) {\r\n'
            f'\tnative_ko_gmes "@card/header/" + (native_card_help - 1), basex@tcg + {x}, '
            f'basey@tcg + 536, 650, 0, 215, 215, 215\r\n'
            '\tif ( stat >= 0 ) {\r\n'
            '\t\tnative_card_painted = 1\r\n'
            f'\t\tnative_ko_gmes "@card/lore/" + (native_card_help - 1), basex@tcg + {x}, '
            f'basey@tcg + 550, 650, 0, 215, 215, 215\r\n'
            '\t\tif ( stat < 0 & native_card_help <= 1165 ) {\r\n'
            f'\t\t\tnative_ko_gmes native_card_ja(native_card_help - 1), basex@tcg + {x}, '
            'basey@tcg + 550, 650, 0, 215, 215, 215\r\n'
            '\t\t}\r\n'
            '\t\tif ( stat < 0 ) {\r\n'
            '\t\t\tnative_card_break = instr(helpmsg@tcg, 0, "\\n")\r\n'
            '\t\t\tif ( native_card_break >= 0 ) {\r\n'
            '\t\t\t\tnative_card_body = strmid(helpmsg@tcg, native_card_break + 2, strlen(helpmsg@tcg))\r\n'
            # English-mode biographies can be absent. The DB still prefixes
            # that empty string with "data:", which supplies no information.
            '\t\t\t\tif ( strmid(native_card_body, 0, 5) == "data:" ) {\r\n'
            '\t\t\t\t\tnative_card_body = strmid(native_card_body, 5, strlen(native_card_body))\r\n'
            '\t\t\t\t}\r\n'
            f'\t\t\t\tpos basex@tcg + {x}, basey@tcg + {y + 16}\r\n'
            '\t\t\t\tmes native_card_body\r\n'
            '\t\t\t}\r\n'
            '\t\t}\r\n'
            '\t}\r\n'
            '}\r\n'
            'if ( native_card_painted == 0 ) {\r\n')
        changes[number] = (b'mes helpmsg@tcg', before.encode(), b'}\r\n')
    for number, value in ((272888, 'cfname@tcg(p@tcg)'), (272984, '"Player"'),
                          (272988, '"Opponent"'), (272993, 'phasen@tcg(cnt - 1)')):
        key = f'native_ko_menu 20, "@card-ui/" + {value} + "\\t" + {value}\r\n'
        changes[number] = (('mes ' + value).encode(), key.encode(), b'native_ko_menu 0, ""\r\n')
    for number, command in (
        (272935, 'mes "Page " + dsc@tcg / 8 / 3 + "/" + (dlistmax@tcg - 1) / 8 / 3'),
        (272953, 'mes "Turn " + turn@tcg'),
        (272956, 'mes "Turn " + turn@tcg'),
        (273001, 'mes "Deck\\n Editor"'),
        (273004, 'mes "Deck\\n Editor"'),
        (273012, 'mes "Deck " + gdata(830 + curdeck) + "/" + 30'),
        (273015, 'mes "Life " + cpdata@tcg(4, 0)'),
        (273017, 'mes "Mana " + cpdata@tcg(6, 0)'),
        (273019, 'mes "Card " + calcstartcard(0)'),
        (273021, 'mes "Domain * " + cpdata@tcg(9, 0)'),
    ):
        changes[number] = (command.encode(), b'native_ko_menu 27, ""\r\n', b'native_ko_menu 0, ""\r\n')
    offset = 0
    for n, line in enumerate(source.splitlines(keepends=True), 1):
        if n in changes:
            expected, before, after = changes[n]
            if line.strip() != expected:
                raise ValueError(f'card display changed at line {n}')
            indent = line[:len(line) - len(line.lstrip(b'\t'))]
            for at, part in ((offset, before), (offset + len(line), after)):
                if part:
                    yield at, b''.join(indent + x for x in part.splitlines(keepends=True)), 'card_help'
        offset += len(line)
        if n == 273040:
            break

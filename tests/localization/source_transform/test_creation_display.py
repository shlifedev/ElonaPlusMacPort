"""Verify the managed draws against the optional 2.335 source supplied locally."""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'work/decompiled/2.335-reference/start.hsp'
sys.path.insert(0, str(ROOT / 'tools'))
from korean_creation_hooks import authored_creation_inserts
from korean_menu_hooks import menu_inserts


class ProfileLayoutTests(unittest.TestCase):
    def test_history_fits_before_combat_column(self):
        path = ROOT / 'localization/ko-KR/original-creation-prompts.json'
        histories = [row for row in json.loads(path.read_text(encoding='utf-8'))
                     if row.get('id', '').startswith('creation.prompt.29')
                     and 296390 <= int(row['id'].split('.')[2]) <= 297080]
        self.assertEqual(len(histories), 222)
        for row in histories:
            # Sheet source places profile at wx+220 and combat at wx+422.
            # The Japanese-based font uses 12 px per Hangul glyph, 6 per ASCII.
            pixels = sum(6 if c.isascii() else 12 for c in row['target'])
            with self.subTest(line=row['id']):
                self.assertLessEqual(pixels, 202)


@unittest.skipUnless(SOURCE.is_file(), 'requires the local ElonaPlus 2.335 source')
class CreationDisplayTests(unittest.TestCase):
    def test_all_mode_and_ambiguous_history_draws_have_reviewed_keys(self):
        data = ROOT / 'localization/ko-KR'
        catalog = {}
        for name in ('original-creation-prompts.json', 'original-ui.json'):
            for row in json.loads((data / name).read_text(encoding='utf-8')):
                if row.get('review_status') == 'reviewed':
                    for key in ('source', 'source_ja'):
                        if key in row:
                            catalog[row[key]] = row['target']
        source = SOURCE.read_bytes()
        sites = {}
        for offset, addition, kind in authored_creation_inserts(source):
            if kind == 'authored_creation_begin':
                self.assertEqual(source[offset:].split(b'\n', 1)[0].strip(), b'mes s')
                match = re.search(r'native_ko_menu 20, "([^"]+)" \+ "\\t" \+ s',
                                  addition.decode('cp932'))
                self.assertIsNotNone(match)
                target = catalog[match[1]]
                self.assertRegex(target, '[가-힣]')
                sites.setdefault(offset, []).append(target)
        # Both mode screens have 38 lines. The two profile screens each
        # disambiguate three family descriptions and two personality traits.
        self.assertEqual(sorted(map(len, sites.values())), [1] * 76 + [2, 2, 3, 3])
        for targets in sites.values():
            self.assertEqual(len(set(targets)), len(targets))

    def test_wrap_every_gene_source_without_touching_the_name(self):
        source = SOURCE.read_bytes()
        draw = b'mes "Gene from " + geneuse'
        hooks = [pos for pos, code, _ in menu_inserts(source) if b'native_ko_menu 12,' in code]
        self.assertEqual(len(hooks), source.count(draw))
        self.assertEqual(len(hooks), 12)
        for pos in hooks:
            self.assertEqual(source[pos:].split(b'\n', 1)[0].strip(), draw)

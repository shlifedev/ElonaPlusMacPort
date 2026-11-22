"""Check labels that have different meanings on adjacent character-creation screens."""
import json
from pathlib import Path
import unittest


DATA = Path(__file__).resolve().parents[3] / 'src/localization/ko-KR'


class EarlyScreenTests(unittest.TestCase):
    def test_ambiguous_labels_keep_their_screen_context(self):
        entries = json.loads((DATA / 'original-creation-prompts.json').read_text(encoding='utf-8'))
        labels = {row['source']: row['target'] for row in entries}
        for english, creation, naming in [('Alias Selection', '이명 선택', '이름 선택'),
                                           ('Alias List', '이명 후보', '이름 후보')]:
            self.assertEqual(labels['@profile/' + english], creation)
            self.assertEqual(labels['@game-menu/' + english], naming)
        for english, feat, ability in [('Fire Breath', '불 뿜기 재주', '화염 방사'),
                                       ('Knockout', '힘 조절', '녹아웃 공격')]:
            self.assertEqual(labels['@feat/' + english], feat)
            self.assertEqual(labels['@sheet/' + english], ability)

    def test_every_builtin_race_has_both_preview_labels(self):
        rows = json.loads((DATA / 'original-races-classes.json').read_text(encoding='utf-8'))
        for race in (row for row in rows if row['id'].startswith('race.label.')):
            with self.subTest(race=race['id']):
                # The original database's cnven capitalizes the first byte of its English ID.
                english = race['source'][0].upper() + race['source'][1:]
                for label in (english, race['source_ja']):
                    self.assertIn('@sheet/race/' + label, race['source_aliases'])


if __name__ == '__main__':
    unittest.main()

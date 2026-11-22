"""A Japanese source pair and its abridged English alias are separate identities."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts'))
from build_draft_display import build, sha


def pair(ja, en):
    return {'source_ja': ja, 'source': en, 'id': sha(ja+'\0'+en)[:20]}


class DraftBindingTests(unittest.TestCase):
    def test_non_equivalent_english_can_never_bind(self):
        row = pair('今日は雪が降る。', 'Look out!')
        keys, covered = build([row], {row['id']: '오늘은 눈이 온다.'}, set(), set())
        self.assertEqual(covered, {row['id']})
        self.assertEqual(keys, {sha(row['source_ja']): '오늘은 눈이 온다.'})

    def test_shared_placeholder_and_raw_markup_remain_original(self):
        rows = [pair('町を出る。','Same prompt.'), pair('船を降りる。','Same prompt.'),pair('港に戻る。','<port>')]
        ids = {row['id'] for row in rows}
        converted = dict(zip((r['id'] for r in rows), ['마을을 떠난다.','배에서 내린다.','항구로 돌아간다.']))
        keys, covered = build(rows, converted, set(), ids)
        self.assertEqual(covered, ids)
        self.assertNotIn(sha('Same prompt.'), keys)
        self.assertNotIn(sha('<port>'), keys)

    def test_both_languages_must_retain_each_number(self):
        row = pair('硬貨20枚を渡す。','Give 20 coins.')
        ids = {row['id']}
        self.assertEqual(build([row], {row['id']:'동전 200개를 준다.'}, set(), ids), ({},set()))
        keys, _ = build([row], {row['id']:'동전 20개를 준다.'}, {sha(row['source'])}, ids)
        self.assertEqual(list(keys), [sha(row['source_ja'])])


if __name__ == '__main__':
    unittest.main()

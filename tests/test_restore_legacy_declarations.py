from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from restore_legacy_declarations import decode_flags, encode_flags, restore


def inventory(types=(-10, 4, 4, 4), *, subid=-5):
    return {"libraries": [{"name": "test.dll"}], "functions": [
        {"index": 0, "kind": "external", "library_index": 0, "name": "_Bit@16",
         "subid": subid, "parameters": [{"type": t} for t in types], "target_or_flags": 2}]}


class LegacyDeclarationTests(unittest.TestCase):
    def test_preserves_cp932_non_roundtripping_bytes_and_line_endings(self):
        # NEC duplicate CP932 character: whole-file Unicode roundtrip changes it.
        payload = b'mes "\x87\x90"\r\n'
        self.assertNotEqual(payload.decode("cp932").encode("cp932"), payload)
        source = b'#uselib "test.dll"\r\n\t#func bit "_Bit@16" var, int, int, int\r\n' + payload
        result, changes = restore(source, inventory(), expected_count=1)
        self.assertEqual(result, b'#uselib "test.dll"\r\n\t#func bit "_Bit@16" $1\r\n' + payload)
        self.assertEqual(changes[0]["source_line"], 2)

    def test_modern_decl_is_unchanged(self):
        source = b'#uselib "test.dll"\n#func bit "_Bit@16" var,int,int,int\n'
        result, changes = restore(source, inventory(subid=-3), expected_count=0)
        self.assertEqual(result, source)
        self.assertEqual(changes, [])

    def test_special_parameter_layouts(self):
        cases = [(0x202, [-17, -19, -19, -19]), (0x83, [-8, 4, 4, 4]),
                 (0x16, [-9, -6, 4, -16]), (0x11, [-10, 4, 4, -16]),
                 (0x5, [-10, -6, 4, 4]), (0x22, [-9, 4, 4, -18])]
        for flags, parameters in cases:
            with self.subTest(flags=flags):
                self.assertEqual(encode_flags(parameters, 2), flags)
                self.assertEqual(decode_flags(flags), (parameters, 2))
        self.assertEqual(encode_flags([4, 4, 4, 4], 3), 0x100)

    def test_unsupported_layout_is_rejected(self):
        for parameters in ([4, 4, -10, 4], [4, 4, 4], [-17, 4, 4, 4]):
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                encode_flags(parameters, 2)
        for flags in (3, 0x201, 0x400):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                decode_flags(flags)

    def test_mismatched_parameter_or_export_is_rejected(self):
        for line in (b'#func bit "_Bit@16" int,int,int,int\n',
                     b'#func bit "_Different@16" var,int,int,int\n'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                restore(b'#uselib "test.dll"\n' + line, inventory())

    def test_missing_duplicate_and_already_transformed_are_rejected(self):
        line = b'#func bit "_Bit@16" var,int,int,int\n'
        for source in (b'#uselib "test.dll"\n',
                       b'#uselib "test.dll"\n' + line + line,
                       b'#uselib "test.dll"\n#func bit "_Bit@16" $1\n'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                restore(source, inventory(), expected_count=1)

    def test_expected_count_guard(self):
        source = b'#uselib "test.dll"\n#func bit "_Bit@16" var,int,int,int\n'
        with self.assertRaises(ValueError):
            restore(source, inventory(), expected_count=54)


if __name__ == "__main__":
    unittest.main()

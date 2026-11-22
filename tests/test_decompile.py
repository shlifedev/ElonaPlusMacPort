"""Small independent bytecode fixtures for lexical coverage checks."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from decompile_game import token_inventory


DICTIONARY = b"""$Code,,,,
0x02,-1,,String,,
0x04,-1,,Integer,,
0x09,0x000f,mes,HspFunction,,
0x0b,0x0000,if,IfStatement,HasExtraInt16
0x0f,0x0010,end,HspFunction,,
0x10,-1,,DllFunction,,
$End
"""


def ax(code: bytes) -> bytes:
    header = bytearray(96)
    header[:4] = b"HSP3"
    struct.pack_into("<III", header, 12, 96 + len(code), 96, len(code))
    return bytes(header) + code


class TokenCoverageTests(unittest.TestCase):
    def test_short_long_and_dll_tokens_account_for_every_byte(self):
        code = bytes.fromhex("09200f00 02c004000000 10200300 0f201000")
        report = token_inventory(ax(code), DICTIONARY)
        self.assertEqual(report["tokens"], 4)
        self.assertEqual(report["bytes_accounted_for"], 18)
        self.assertEqual(report["dll_function_index_references"], {"3": 1})
        self.assertEqual(report["unknown_count"], 0)

    def test_if_offset_word_is_not_a_separate_token(self):
        code = bytes.fromhex("0b200000 0800 04400100 0f201000")
        report = token_inventory(ax(code), DICTIONARY)
        self.assertEqual(report["tokens"], 3)
        self.assertEqual(report["bytes_accounted_for"], 14)

    def test_unknown_instruction_is_reported_at_its_offset(self):
        report = token_inventory(ax(bytes.fromhex("09200f00 0820ffff")), DICTIONARY)
        self.assertEqual(report["unknown_count"], 1)
        self.assertEqual(report["unknowns_first_100"][0]["cs_byte_offset"], 4)

    def test_truncated_long_value_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Truncated token value"):
            token_inventory(ax(bytes.fromhex("04800000")), DICTIONARY)

    def test_truncated_if_offset_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Truncated extra word"):
            token_inventory(ax(bytes.fromhex("0b200000")), DICTIONARY)

    def test_unsupported_extended_type_bits_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unsupported token type/flags"):
            token_inventory(ax(bytes.fromhex("04210100")), DICTIONARY)

    def test_bad_code_bounds_are_rejected(self):
        data = bytearray(ax(bytes.fromhex("0f201000")))
        struct.pack_into("<I", data, 20, 100)
        with self.assertRaisesRegex(ValueError, "Invalid HSP3 code bounds"):
            token_inventory(bytes(data), DICTIONARY)


if __name__ == "__main__":
    unittest.main()
